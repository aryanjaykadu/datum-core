"""Evaluation pipeline: one CAD file in, one sealed contract payload out."""
from __future__ import annotations

import time
from dataclasses import asdict, dataclass, field
from pathlib import Path

import numpy as np
import trimesh

from . import events, geometry, materials
from .contract import (
    AdvancedFeaAndCad, DatumPayload, EnterpriseEconomics, SecurityForensics, TelemetryAnd4D, new_system_id, seal,
)
from .economics import costing
from .physics import crash, degradation, lattice, weld
from .security import itar, ndt, voids

CAD_EXTENSIONS = {".sldprt", ".sldasm", ".step", ".stp", ".igs", ".iges"}
SW_NO_MATERIAL_DENSITY = 1000.0  # what SolidWorks uses when no material is assigned


@dataclass
class EvaluationOptions:
    material: str | None = None
    zone: str = "WORLD"
    years: float = 20.0
    exposed_fraction: float = 1.0
    cathode_material: str | None = None
    cathode_area_ratio: float = 0.0
    impactor_mass_kg: float = 5.0
    drop_height_m: float = 0.5
    buy_to_fly: float = 3.0
    generate_lattice: bool = True


@dataclass
class Evaluation:
    payload: DatumPayload
    mesh: trimesh.Trimesh
    lattice: trimesh.Trimesh | None
    details: dict = field(default_factory=dict)


def _wall_thickness_mm(mesh: trimesh.Trimesh, samples: int = 400) -> float:
    rng = np.random.default_rng(1)
    picked = rng.choice(len(mesh.faces), size=min(samples, len(mesh.faces)), replace=False)
    eps = 1e-5 * float(np.max(mesh.extents))
    origins = mesh.triangles_center[picked] - mesh.face_normals[picked] * eps
    locations, index_ray, _ = mesh.ray.intersects_location(origins, -mesh.face_normals[picked], multiple_hits=False)
    if len(locations) == 0:
        return float(np.min(mesh.extents)) * 1e3
    d = np.linalg.norm(locations - origins[index_ray], axis=1)
    return float(np.percentile(d, 5)) * 1e3


class Stage:
    def __init__(self, job: str, name: str, fraction: float):
        self.job, self.name, self.fraction = job, name, fraction

    def __enter__(self):
        self.t = time.perf_counter()
        events.progress(self.job, self.name, self.fraction)
        return self

    def __exit__(self, *exc):
        ms = (time.perf_counter() - self.t) * 1e3
        events.log(f"{self.name} {'failed' if exc[0] else 'done'} in {ms:.0f} ms", level="error" if exc[0] else "info", stage=self.name)
        return False


def evaluate(path: Path, opts: EvaluationOptions | None = None) -> Evaluation:
    opts = opts or EvaluationOptions()
    system_id = new_system_id()
    ext = path.suffix.lower()
    errors: dict[str, str] = {}

    with Stage(system_id, "geometry", 0.05):
        features: list[dict] = []
        sw_material = ""
        sw_density = 0.0
        if ext in geometry.MESH_EXTENSIONS:
            mesh = geometry.to_metres(geometry.load_mesh(path), None)
            if mesh.volume < 0:
                mesh.invert()
            material = materials.get(opts.material)
            props = geometry.mesh_mass_properties(mesh, material.density_kg_m3)
            source = "mesh"
        elif ext in CAD_EXTENSIONS:
            from .solidworks import SESSION

            sw = SESSION.evaluate(path.resolve())
            if not sw.mesh_path:
                raise RuntimeError("SolidWorks did not export a mesh for this model")
            mesh = geometry.to_metres(geometry.load_mesh(Path(sw.mesh_path)), sw.volume_m3)
            features, sw_material = sw.features, sw.material_name
            # Judge the material by the density SolidWorks actually used, not only its name:
            # 1000 kg/m³ is SolidWorks' "no material" placeholder.
            sw_density = sw.mass_kg / max(sw.volume_m3, 1e-12)
            unassigned = abs(sw_density - SW_NO_MATERIAL_DENSITY) < 1.0
            key = opts.material or materials.match_solidworks_name(sw_material) or (None if unassigned else materials.nearest_by_density(sw_density))
            material = materials.get(key)
            props = {k: getattr(sw, k) for k in ("mass_kg", "volume_m3", "surface_area_m2", "center_of_mass_m", "inertia_tensor_kgm2", "principal_moments_kgm2", "bbox_m")}
            if unassigned or opts.material:
                # mass and inertia both scale linearly with density
                scale = material.density_kg_m3 / sw_density
                props["mass_kg"] *= scale
                props["inertia_tensor_kgm2"] = (np.array(props["inertia_tensor_kgm2"]) * scale).tolist()
            source = "solidworks"
        else:
            raise ValueError(f"unsupported file type {ext}")

    analysis_mesh = geometry.decimate_for_analysis(mesh)
    cog = np.asarray(props["center_of_mass_m"])
    tensor = np.asarray(props["inertia_tensor_kgm2"])
    extents = np.sort(mesh.extents)[::-1]

    with Stage(system_id, "degradation", 0.2):
        wall_mm = _wall_thickness_mm(analysis_mesh)
        s = degradation.surface_second_moment(analysis_mesh.triangles_center, analysis_mesh.area_faces, cog)
        deg = degradation.degrade(
            tensor, props["mass_kg"], material, s, float(analysis_mesh.area), wall_mm,
            years=opts.years, exposed_fraction=opts.exposed_fraction,
            cathode=materials.MATERIALS.get(opts.cathode_material or ""), cathode_area_ratio=opts.cathode_area_ratio,
        )

    with Stage(system_id, "explicit-crash", 0.35):
        crash_result = crash.simulate(
            material, float(extents[0]), float(extents[1]), float(extents[2]), props["mass_kg"],
            impactor_mass_kg=opts.impactor_mass_kg, drop_height_m=opts.drop_height_m,
        )

    lattice_mesh, lattice_info = None, {"generated": False, "reason": "disabled"}
    if opts.generate_lattice:
        with Stage(system_id, "lattice", 0.5):
            try:
                lattice_mesh, lattice_info = lattice.gyroid_infill(analysis_mesh, resolution=32)
            except Exception as exc:
                lattice_info = {"generated": False, "reason": str(exc)}

    with Stage(system_id, "security", 0.65):
        # Security checks fail closed: if a scan cannot run, the part is not cleared.
        try:
            void_report = voids.scan(mesh)
        except Exception as exc:
            errors["void_scan"] = str(exc)
            void_report = {"steganography_detected": True, "findings": [], "error": str(exc)}
        try:
            ndt_report = ndt.inspectability(analysis_mesh)
        except Exception as exc:
            errors["ndt"] = str(exc)
            ndt_report = {"score": 0.0, "error": str(exc)}
        itar_report = itar.status()

    with Stage(system_id, "economics", 0.85):
        econ = costing.evaluate(material, props["mass_kg"], buy_to_fly=opts.buy_to_fly, zone=opts.zone)

    payload = seal(
        DatumPayload(
            system_id=system_id,
            audit_hash="",
            security_forensics=SecurityForensics(
                steganography_detected=void_report["steganography_detected"],
                itar_tpm_locked=itar_report["locked"],
                ndt_inspectability_score=ndt_report["score"],
            ),
            advanced_fea_and_cad=AdvancedFeaAndCad(
                mbd_max_deflection_mm=round(crash_result["max_deflection_mm"], 4),
                explicit_fracture_detected=crash_result["fracture_detected"],
                weldment_haz_yield_mpa=round(weld.haz_yield_mpa(material), 2),
                quantum_lattice_generated=bool(lattice_info.get("generated")),
            ),
            telemetry_and_4d=TelemetryAnd4D(
                mass_kg=round(props["mass_kg"], 6),
                inertia_tensor_current=tensor.tolist(),
                inertia_tensor_20yr_degraded=deg.inertia_tensor_degraded,
            ),
            enterprise_economics=EnterpriseEconomics(
                live_cost_usd=round(econ["live_cost_usd"], 2),
                projected_cost_5yr_usd=round(econ["projected_cost_5yr_usd"], 2),
                dynamic_carbon_kg_co2e=round(econ["dynamic_carbon_kg_co2e"], 3),
            ),
        )
    )
    events.progress(system_id, "complete", 1.0)

    details = {
        "system_id": system_id,
        "file": str(path),
        "source": source,
        "material": {"key": material.key, "name": material.name, "solidworks_name": sw_material, "solidworks_density_kg_m3": sw_density if source == "solidworks" else None},
        "mass_properties": {**props, "inertia_tensor_kgm2": tensor.tolist()},
        "features": features,
        "has_weld_features": weld.has_weld_features(features),
        "wall_thickness_p5_mm": wall_mm,
        "degradation": asdict(deg),
        "crash": crash_result,
        "lattice": lattice_info,
        "voids": void_report,
        "ndt": ndt_report,
        "itar": itar_report,
        "economics": econ["breakdown"],
        "errors": errors,
    }
    return Evaluation(payload=payload, mesh=mesh, lattice=lattice_mesh, details=details)
