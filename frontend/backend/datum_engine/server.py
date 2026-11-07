"""FastAPI daemon. Binds to 127.0.0.1 only and requires the per-launch token
the Rust shell passes in `DATUM_ENGINE_TOKEN`, so other local processes and
web pages cannot drive SolidWorks through it."""
from __future__ import annotations

import os
import secrets
import tempfile
import threading
from collections import OrderedDict
from dataclasses import asdict
from pathlib import Path

import numpy as np
from fastapi import Depends, FastAPI, Header, HTTPException
from fastapi.responses import Response
from pydantic import BaseModel

from . import events, geometry, pipeline
from .contract import DatumPayload
from .metrology import cnc, pointcloud, scrap_fit, webxr
from .physics import lubrication, sonic_boom

TOKEN = os.environ.get("DATUM_ENGINE_TOKEN") or secrets.token_hex(16)
CACHE_SIZE = 8

app = FastAPI(title="Datum Core Engine", version="1.0.0", docs_url=None, redoc_url=None)
_results: "OrderedDict[str, pipeline.Evaluation]" = OrderedDict()
_results_lock = threading.Lock()


def authorize(x_datum_token: str = Header(default="")) -> None:
    if not secrets.compare_digest(x_datum_token, TOKEN):
        raise HTTPException(status_code=401, detail="bad engine token")


def _get(system_id: str) -> pipeline.Evaluation:
    with _results_lock:
        result = _results.get(system_id)
    if result is None:
        raise HTTPException(status_code=404, detail="unknown system_id (evaluate the file first)")
    return result


class EvaluateRequest(BaseModel):
    path: str
    options: dict = {}


class XrRequest(BaseModel):
    title: str = "Assembly instructions"
    annotations: list[dict] = []


class ScrapRequest(BaseModel):
    scan_path: str
    margin_mm: float = 1.0
    voxel_mm: float = 2.0


@app.get("/health")
def health() -> dict:
    return {"ok": True, "engine": "datum_engine", "version": app.version}


@app.post("/evaluate", response_model=DatumPayload, dependencies=[Depends(authorize)])
def evaluate(req: EvaluateRequest) -> DatumPayload:
    path = Path(req.path)
    if not path.exists():
        raise HTTPException(status_code=404, detail=f"file not found: {path}")
    known = set(pipeline.EvaluationOptions.__dataclass_fields__)
    opts = pipeline.EvaluationOptions(**{k: v for k, v in req.options.items() if k in known})
    try:
        result = pipeline.evaluate(path, opts)
    except Exception as exc:
        events.log(f"evaluation failed: {exc}", level="error")
        raise HTTPException(status_code=422, detail=str(exc)) from exc
    with _results_lock:
        _results[result.payload.system_id] = result
        while len(_results) > CACHE_SIZE:
            _results.popitem(last=False)
    return result.payload


@app.get("/details/{system_id}", dependencies=[Depends(authorize)])
def details(system_id: str) -> dict:
    return _get(system_id).details


@app.get("/mesh/{system_id}", dependencies=[Depends(authorize)])
def mesh(system_id: str, layer: str = "part") -> Response:
    result = _get(system_id)
    target = result.lattice if layer == "lattice" else result.mesh
    if target is None:
        raise HTTPException(status_code=404, detail=f"no {layer} mesh")
    return Response(geometry.encode_mesh(target), media_type="application/octet-stream")


@app.post("/cnc/{system_id}", dependencies=[Depends(authorize)])
def cnc_check(system_id: str, setups: str = "+Z", cutter_diameter_mm: float = 6.0) -> dict:
    result = _get(system_id)
    names = tuple(s for s in setups.split(",") if s in cnc.SETUPS) or ("+Z",)
    return cnc.check(geometry.decimate_for_analysis(result.mesh), names, cutter_diameter_mm / 2e3)


@app.post("/scrap-fit/{system_id}", dependencies=[Depends(authorize)])
def scrap(system_id: str, req: ScrapRequest) -> dict:
    result = _get(system_id)
    scan = pointcloud.load(Path(req.scan_path))
    if float(np.max(np.ptp(scan, axis=0))) > 50:  # scans are usually exported in mm
        scan = scan * 1e-3
    samples = np.vstack([result.mesh.sample(4000), result.mesh.vertices[:: max(1, len(result.mesh.vertices) // 2000)]])
    return scrap_fit.fit(samples, scan, voxel_m=req.voxel_mm * 1e-3, margin_m=req.margin_mm * 1e-3)


@app.post("/xr/{system_id}", dependencies=[Depends(authorize)])
def xr_package(system_id: str, req: XrRequest) -> dict:
    result = _get(system_id)
    out = Path(tempfile.gettempdir()) / "DatumCoreXR" / system_id
    return webxr.build_package(result.mesh, out, system_id, req.title, req.annotations)


@app.post("/physics/sonic-boom/{system_id}", dependencies=[Depends(authorize)])
def boom(system_id: str, mach: float = 1.6, altitude_m: float = 15000.0) -> dict:
    return sonic_boom.ground_overpressure(_get(system_id).mesh, mach=mach, altitude_m=altitude_m)


@app.post("/physics/lubrication", dependencies=[Depends(authorize)])
def lube(contact: dict | None = None, oil: dict | None = None) -> dict:
    return lubrication.analyse(
        lubrication.GearContact(**(contact or {})), lubrication.CarreauYasudaOil(**(oil or {}))
    )


@app.post("/shutdown", dependencies=[Depends(authorize)])
def shutdown() -> dict:
    from .solidworks import SESSION

    SESSION.shutdown()
    threading.Timer(0.2, lambda: os._exit(0)).start()
    return {"ok": True}
