#!/usr/bin/env python3
"""Package backend/datum_engine into one PyInstaller binary for the Tauri sidecar.

    python build_backend.py

Tauri's `externalBin` expects the binary name suffixed with the Rust target
triple, so the output is src-tauri/bin/datum_engine-<triple>.exe. The bundler
strips the suffix and installs it next to the app as datum_engine.exe.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BACKEND = ROOT / "backend"
BIN_DIR = ROOT / "src-tauri" / "bin"
WORK = ROOT / ".pyinstaller"
ENTRY = WORK / "entry.py"

REQUIRED = ["fastapi", "uvicorn", "pydantic", "numpy", "scipy", "trimesh", "rtree", "skimage", "shapely", "networkx", "win32com.client", "PyInstaller"]
OPTIONAL = ["pye57", "embreex", "blpapi", "fast_simplification"]


def fail(message: str) -> None:
    print(f"[build_backend] ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def target_triple() -> str:
    try:
        out = subprocess.run(["rustc", "-vV"], capture_output=True, text=True, check=True).stdout
        return next(line.split(": ", 1)[1] for line in out.splitlines() if line.startswith("host: "))
    except Exception:
        return "x86_64-pc-windows-msvc"


def check_host() -> list[str]:
    if os.name != "nt":
        fail("datum_engine is Windows-only (SolidWorks COM)")
    missing = []
    for name in REQUIRED:
        try:
            __import__(name)
        except ImportError:
            missing.append(name)
    if missing:
        fail(f"missing modules: {', '.join(missing)} — run: python -m pip install -r backend/requirements.txt")
    present = []
    for name in OPTIONAL:
        try:
            __import__(name)
            present.append(name)
        except ImportError:
            print(f"[build_backend] optional module not installed: {name}")
    return present


def build(optional: list[str]) -> Path:
    WORK.mkdir(exist_ok=True)
    # A tiny launcher so PyInstaller sees `datum_engine` as a package (relative imports intact).
    ENTRY.write_text("import sys\nfrom datum_engine.__main__ import main\nsys.exit(main())\n")
    triple = target_triple()
    name = f"datum_engine-{triple}"
    BIN_DIR.mkdir(parents=True, exist_ok=True)

    cmd = [
        sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean", "--onefile", "--console",
        "--name", name, "--distpath", str(BIN_DIR), "--workpath", str(WORK / "build"), "--specpath", str(WORK),
        "--paths", str(BACKEND),
        "--add-data", f"{BACKEND / 'datum_engine' / 'data'}{os.pathsep}datum_engine/data",
        "--collect-submodules", "datum_engine",
        "--collect-submodules", "uvicorn",
        "--collect-data", "trimesh",
        "--collect-binaries", "rtree",
        "--hidden-import", "win32com.client", "--hidden-import", "pythoncom", "--hidden-import", "pywintypes",
        "--hidden-import", "skimage.measure",
        "--exclude-module", "tkinter", "--exclude-module", "matplotlib", "--exclude-module", "IPython",
    ]
    for module in optional:
        cmd += ["--collect-all", module]
    cmd.append(str(ENTRY))

    print("[build_backend]", " ".join(cmd))
    subprocess.run(cmd, check=True, cwd=ROOT)
    out = BIN_DIR / f"{name}.exe"
    if not out.exists():
        fail(f"PyInstaller finished but {out} is missing")
    return out


def smoke_test(exe: Path) -> None:
    sample = WORK / "smoke.stl"
    import trimesh

    trimesh.creation.box(extents=(60, 40, 20)).export(sample)
    proc = subprocess.run([str(exe), "--evaluate", str(sample)], capture_output=True, text=True, timeout=300)
    if proc.returncode != 0 or '"audit_hash"' not in proc.stdout:
        fail(f"smoke test failed:\n{proc.stderr[-2000:]}")
    print("[build_backend] smoke test passed")


if __name__ == "__main__":
    exe = build(check_host())
    smoke_test(exe)
    print(f"[build_backend] wrote {exe} ({exe.stat().st_size / 1e6:.1f} MB)")
    shutil.rmtree(WORK / "build", ignore_errors=True)
