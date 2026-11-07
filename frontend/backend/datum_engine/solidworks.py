"""SolidWorks COM automation (Pillar 2).

Rules this module follows:
* If SolidWorks is already running, attach to it, leave it visible, and never
  kill it: that session belongs to the user and may hold unsaved work.
* If it has to start one, it records the new SLDWORKS.exe PIDs in a pidfile.
  Only those PIDs are ever terminated, by this engine or by the Rust shell.
* Mass properties come from IMassProperty2 in SI units (kg, m, kg·m²).
"""
from __future__ import annotations

import csv
import io
import os
import subprocess
import tempfile
import threading
import types
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

from . import events

SW_DOC_PART = 1
SW_DOC_ASSEMBLY = 2
SW_OPEN_SILENT = 1
SW_OPEN_READONLY = 2
SW_SAVEAS_SILENT = 1
SW_SAVEAS_COPY = 2
SW_MOMENT_ABOUT_CENTER_OF_MASS = 0


def _get(obj, name: str, *args):
    """Late-bound COM exposes zero-argument methods as properties; call only if callable."""
    attr = getattr(obj, name)
    # A real method comes back as a bound method; a property already returned its value
    # (possibly a COM object, which is "callable" but must not be invoked).
    return attr(*args) if isinstance(attr, types.MethodType) else attr


def state_dir() -> Path:
    base = Path(os.environ.get("LOCALAPPDATA", tempfile.gettempdir())) / "DatumCore"
    base.mkdir(parents=True, exist_ok=True)
    return base


PIDFILE = state_dir() / "sw_owned.pids"


def _sldworks_pids() -> set[int]:
    try:
        out = subprocess.run(
            ["tasklist", "/FI", "IMAGENAME eq SLDWORKS.exe", "/FO", "CSV", "/NH"],
            capture_output=True, text=True, timeout=10, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        ).stdout
    except Exception:
        return set()
    pids = set()
    for row in csv.reader(io.StringIO(out)):
        if len(row) >= 2 and row[0].lower() == "sldworks.exe":
            try:
                pids.add(int(row[1]))
            except ValueError:
                pass
    return pids


def kill_owned_instances() -> None:
    """Terminate only the SolidWorks processes this engine started."""
    if not PIDFILE.exists():
        return
    for line in PIDFILE.read_text().split():
        if line.isdigit():
            subprocess.run(
                ["taskkill", "/F", "/T", "/FI", f"PID eq {line}", "/FI", "IMAGENAME eq SLDWORKS.exe"],
                capture_output=True, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
    PIDFILE.unlink(missing_ok=True)


@dataclass
class MassProperties:
    mass_kg: float
    volume_m3: float
    surface_area_m2: float
    center_of_mass_m: list[float]
    inertia_tensor_kgm2: list[list[float]]  # about the CoG, aligned with the part frame
    principal_moments_kgm2: list[float]
    bbox_m: list[float]  # xmin ymin zmin xmax ymax zmax
    material_name: str
    features: list[dict] = field(default_factory=list)
    mesh_path: str | None = None


class SolidWorksSession:
    """One SolidWorks connection living on one dedicated STA thread.

    COM objects are bound to the apartment that created them, so every call is
    marshalled onto the same single worker thread instead of FastAPI's pool.
    """

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._worker = ThreadPoolExecutor(max_workers=1, thread_name_prefix="sw-com")
        self._app = None
        self._owned = False

    def evaluate(self, path: Path, export_mesh: bool = True) -> "MassProperties":
        return self._worker.submit(self._evaluate, path, export_mesh).result()

    def shutdown(self) -> None:
        try:
            self._worker.submit(self._shutdown).result(timeout=20)
        except Exception:
            pass
        kill_owned_instances()

    def _connect(self):
        import pythoncom  # type: ignore
        import win32com.client  # type: ignore

        pythoncom.CoInitialize()
        if self._app is not None:
            return self._app
        try:
            self._app = win32com.client.GetActiveObject("SldWorks.Application")
            self._owned = False
            events.log("Attached to the running SolidWorks session (left visible, never terminated)", stage="solidworks")
        except Exception:
            before = _sldworks_pids()
            self._app = win32com.client.DispatchEx("SldWorks.Application")
            started = _sldworks_pids() - before
            self._owned = bool(started)
            if started:
                with PIDFILE.open("a") as handle:
                    handle.write("".join(f"{pid}\n" for pid in started))
            self._app.Visible = False
            events.log(f"Started headless SolidWorks (pids {sorted(started) or 'unknown'})", stage="solidworks")
        return self._app

    def _shutdown(self) -> None:
        with self._lock:
            if self._app is not None and self._owned:
                try:
                    self._app.ExitApp()
                except Exception:
                    pass
            self._app = None

    def _evaluate(self, path: Path, export_mesh: bool) -> MassProperties:
        import pythoncom  # type: ignore
        import win32com.client  # type: ignore

        with self._lock:
            app = self._connect()
            ext = path.suffix.lower()
            err = win32com.client.VARIANT(pythoncom.VT_BYREF | pythoncom.VT_I4, 0)
            warn = win32com.client.VARIANT(pythoncom.VT_BYREF | pythoncom.VT_I4, 0)
            if ext in (".sldprt", ".sldasm"):
                doc_type = SW_DOC_ASSEMBLY if ext == ".sldasm" else SW_DOC_PART
                doc = app.OpenDoc6(str(path), doc_type, SW_OPEN_SILENT | SW_OPEN_READONLY, "", err, warn)
            else:  # STEP / IGES etc. go through the import translator
                import_data = app.GetImportFileData(str(path))
                doc = app.LoadFile4(str(path), "r", import_data, err)
            if doc is None:
                raise RuntimeError(f"SolidWorks could not open {path.name} (error code {err.value})")

            title = _get(doc, "GetTitle")
            try:
                doc.ForceRebuild3(False)
                result = self._extract(doc)
                if export_mesh:
                    result.mesh_path = self._export_stl(doc, path.stem)
                return result
            finally:
                app.CloseDoc(title)

    def _extract(self, doc) -> MassProperties:
        ext = doc.Extension
        try:
            mp = _get(ext, "CreateMassProperty2")
        except Exception:
            mp = _get(ext, "CreateMassProperty")
        mp.UseSystemUnits = True
        moment = list(mp.GetMomentOfInertia(SW_MOMENT_ABOUT_CENTER_OF_MASS))
        tensor = [[float(moment[r * 3 + c]) for c in range(3)] for r in range(3)]
        try:
            principal = [float(v) for v in mp.PrincipalMomentsOfInertia]
        except Exception:
            principal = []

        bbox = [0.0] * 6
        try:
            box = doc.GetPartBox(True)
            if box and len(box) >= 6:
                bbox = [float(v) for v in box[:6]]
        except Exception:
            try:  # assemblies
                box = doc.GetBox(0)
                bbox = [float(v) for v in box[:6]]
            except Exception:
                pass

        material = ""
        try:  # needs the active configuration name; the database name is an out-parameter
            config = _get(_get(doc, "ConfigurationManager"), "ActiveConfiguration").Name
            result = doc.GetMaterialPropertyName2(config, "")
            material = str((result[0] if isinstance(result, tuple) else result) or "")
        except Exception:
            pass

        return MassProperties(
            mass_kg=float(mp.Mass),
            volume_m3=float(mp.Volume),
            surface_area_m2=float(mp.SurfaceArea),
            center_of_mass_m=[float(v) for v in mp.CenterOfMass],
            inertia_tensor_kgm2=tensor,
            principal_moments_kgm2=principal,
            bbox_m=bbox,
            material_name=material,
            features=self._feature_tree(doc),
        )

    @staticmethod
    def _feature_tree(doc) -> list[dict]:
        features: list[dict] = []
        try:
            feat = _get(doc, "FirstFeature")
            while feat is not None and len(features) < 2000:
                features.append({"name": str(feat.Name), "type": str(_get(feat, "GetTypeName2"))})
                feat = _get(feat, "GetNextFeature")
        except Exception as exc:
            events.log(f"Feature tree walk stopped early: {exc}", level="warn", stage="solidworks")
        return features

    @staticmethod
    def _export_stl(doc, stem: str) -> str | None:
        out_dir = Path(tempfile.mkdtemp(prefix="datum_mesh_"))
        target = out_dir / f"{stem}.stl"
        try:
            doc.SaveAs3(str(target), 0, SW_SAVEAS_SILENT | SW_SAVEAS_COPY)
        except Exception as exc:
            events.log(f"STL export failed: {exc}", level="warn", stage="solidworks")
            return None
        # Assemblies may write one STL per component; the caller concatenates them.
        return str(out_dir) if any(out_dir.glob("*.stl")) else None


SESSION = SolidWorksSession()
