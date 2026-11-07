"""Engine tests that run without SolidWorks (mesh input path).

    cd backend && python -m pytest -q
"""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
import pytest
import trimesh

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from datum_engine import contract, materials, pipeline  # noqa: E402
from datum_engine.physics import crash, degradation, lubrication, sonic_boom  # noqa: E402
from datum_engine.security import voids  # noqa: E402


def bracket_mm(void: bool) -> trimesh.Trimesh:
    """120 × 80 × 40 mm block, optionally with a 0.8 mm cavity 1 mm under the top skin."""
    block = trimesh.creation.box(extents=(120, 80, 40))
    if not void:
        return block
    cavity = trimesh.creation.icosphere(subdivisions=2, radius=0.4)
    cavity.apply_translation((10, 5, 19.0 - 0.4))
    cavity.invert()  # inward-facing shell = cavity
    return trimesh.util.concatenate([block, cavity])


@pytest.fixture()
def stl(tmp_path):
    def make(void: bool) -> Path:
        path = tmp_path / ("void.stl" if void else "clean.stl")
        bracket_mm(void).export(path)
        return path

    return make


def test_box_inertia_matches_closed_form():
    mesh = trimesh.creation.box(extents=(0.12, 0.08, 0.04))
    m = 0.12 * 0.08 * 0.04 * 2700
    ixx = m * (0.08**2 + 0.04**2) / 12
    props = __import__("datum_engine.geometry", fromlist=["x"]).mesh_mass_properties(mesh, 2700)
    assert props["mass_kg"] == pytest.approx(m)
    assert props["inertia_tensor_kgm2"][0][0] == pytest.approx(ixx)


def test_shell_moment_matches_box_formula():
    extents = np.array([0.12, 0.08, 0.04])
    mesh = trimesh.creation.box(extents=extents).subdivide().subdivide().subdivide()
    s_mesh = degradation.surface_second_moment(mesh.triangles_center, mesh.area_faces, np.zeros(3))
    s_box = degradation.box_surface_second_moment(extents)
    assert np.allclose(np.diag(s_mesh), np.diag(s_box), rtol=0.05)


def test_degradation_reduces_inertia_for_steel():
    tensor = np.diag([1e-3, 2e-3, 3e-3])
    s = degradation.box_surface_second_moment(np.array([0.12, 0.08, 0.04]))
    r = degradation.degrade(tensor, 3.0, materials.get("s355"), s, 0.0352, 10.0)
    assert r.recession_mm > 0.1  # ISO 9224 C3 carbon steel, 20 years
    assert all(r.inertia_tensor_degraded[i][i] < tensor[i][i] for i in range(3))


def test_crash_fracture_triggers_on_brittle_overload():
    soft = crash.simulate(materials.get("al6061t6"), 0.12, 0.08, 0.04, 1.04, impactor_mass_kg=1.0, drop_height_m=0.05)
    hard = crash.simulate(materials.get("abs"), 0.30, 0.01, 0.004, 0.012, impactor_mass_kg=20.0, drop_height_m=2.0)
    assert not soft["fracture_detected"]
    assert hard["fracture_detected"] and hard["deleted_elements_at_mm"]


def test_lubrication_shear_thins():
    out = lubrication.analyse()
    assert 0.05 < out["film_thickness_um"] < 5
    assert out["viscosity_retained"] < 1.0


def test_sonic_boom_is_in_plausible_range_for_slender_body():
    body = trimesh.creation.cone(radius=1.5, height=30.0, sections=48)  # 30 m nose-like body
    out = sonic_boom.ground_overpressure(body, mach=1.6, altitude_m=15000)
    assert 5 < out["overpressure_pa"] < 300


def test_void_scanner(stl):
    assert not voids.scan(trimesh.load_mesh(stl(False)).apply_scale(1e-3))["steganography_detected"]
    report = voids.scan(trimesh.load_mesh(stl(True)).apply_scale(1e-3))
    assert report["steganography_detected"]


def test_pipeline_contract_is_sealed(stl):
    result = pipeline.evaluate(stl(False), pipeline.EvaluationOptions(generate_lattice=False))
    payload = result.payload
    assert contract.verify(payload)
    assert payload.telemetry_and_4d.mass_kg == pytest.approx(0.12 * 0.08 * 0.04 * 2700, rel=1e-3)
    assert set(payload.model_dump()) == {
        "system_id", "audit_hash", "security_forensics", "advanced_fea_and_cad", "telemetry_and_4d", "enterprise_economics",
    }
    with pytest.raises(Exception):
        contract.DatumPayload.model_validate({**payload.model_dump(), "extra": 1})
