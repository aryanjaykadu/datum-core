"""Mesh loading, unit reconciliation and the binary mesh buffer format.

Mesh buffer layout (little-endian), consumed by `src/lib/mesh.ts`:
    magic   b"DMSH"
    u32     version (1)
    u32     vertex_count
    u32     index_count
    f32[3]  bbox_min (metres)
    f32[3]  bbox_max (metres)
    f32[vertex_count*3] positions (metres)
    f32[vertex_count*3] normals
    u32[index_count]    triangle indices
"""
from __future__ import annotations

import struct
from pathlib import Path

import numpy as np
import trimesh

MESH_EXTENSIONS = {".stl", ".obj", ".ply", ".glb", ".gltf", ".off", ".3mf"}
UNIT_CANDIDATES = (1.0, 1e-3, 1e-2, 0.0254)  # m, mm, cm, inch → metres


def load_mesh(path: Path) -> trimesh.Trimesh:
    if path.is_dir():
        parts = [trimesh.load_mesh(p, force="mesh") for p in sorted(path.glob("*.stl"))]
        if not parts:
            raise FileNotFoundError(f"no STL files in {path}")
        mesh = trimesh.util.concatenate(parts)
    else:
        mesh = trimesh.load_mesh(path, force="mesh")
    if not isinstance(mesh, trimesh.Trimesh) or len(mesh.faces) == 0:
        raise ValueError(f"{path.name} contains no triangle mesh")
    return mesh


def to_metres(mesh: trimesh.Trimesh, reference_volume_m3: float | None) -> trimesh.Trimesh:
    """Scale a mesh to metres.

    With a reference volume (from SolidWorks) the unit is chosen by matching
    volumes; otherwise a bbox larger than 5 units is assumed to be millimetres.
    """
    mesh = mesh.copy()
    if reference_volume_m3 and reference_volume_m3 > 0 and abs(mesh.volume) > 0:
        errors = [abs(abs(mesh.volume) * s**3 / reference_volume_m3 - 1.0) for s in UNIT_CANDIDATES]
        scale = UNIT_CANDIDATES[int(np.argmin(errors))]
    else:
        scale = 1e-3 if float(np.max(mesh.extents)) > 5.0 else 1.0
    mesh.apply_scale(scale)
    return mesh


def mesh_mass_properties(mesh: trimesh.Trimesh, density: float) -> dict:
    """Mass properties of a closed mesh at uniform density (SI units)."""
    if mesh.volume < 0:
        mesh.invert()
    mesh.density = density
    return {
        "mass_kg": float(mesh.mass),
        "volume_m3": float(mesh.volume),
        "surface_area_m2": float(mesh.area),
        "center_of_mass_m": mesh.center_mass.tolist(),
        "inertia_tensor_kgm2": mesh.moment_inertia.tolist(),  # about the centre of mass
        "principal_moments_kgm2": mesh.principal_inertia_components.tolist(),
        "bbox_m": [*mesh.bounds[0].tolist(), *mesh.bounds[1].tolist()],
        "watertight": bool(mesh.is_watertight),
    }


def encode_mesh(mesh: trimesh.Trimesh) -> bytes:
    positions = np.ascontiguousarray(mesh.vertices, dtype="<f4")
    normals = np.ascontiguousarray(mesh.vertex_normals, dtype="<f4")
    indices = np.ascontiguousarray(mesh.faces.reshape(-1), dtype="<u4")
    header = struct.pack(
        "<4sIII6f", b"DMSH", 1, len(positions), len(indices), *mesh.bounds[0].tolist(), *mesh.bounds[1].tolist()
    )
    return header + positions.tobytes() + normals.tobytes() + indices.tobytes()


def decimate_for_analysis(mesh: trimesh.Trimesh, max_faces: int = 60_000) -> trimesh.Trimesh:
    """Cap face count for ray-casting analyses; keeps the original if small enough."""
    if len(mesh.faces) <= max_faces:
        return mesh
    try:
        return mesh.simplify_quadric_decimation(face_count=max_faces)
    except Exception:
        return mesh
