"""Gyroid TPMS lattice infill.

The contract field is named `quantum_lattice_generated`; what is actually
generated is a triply-periodic minimal surface (gyroid) infill, clipped to the
part's interior and meshed with marching cubes. There is nothing quantum here.
"""
from __future__ import annotations

import numpy as np
import trimesh


def gyroid_infill(mesh: trimesh.Trimesh, cell_size_m: float | None = None, wall: float = 0.35, resolution: int = 48) -> tuple[trimesh.Trimesh | None, dict]:
    from skimage.measure import marching_cubes

    lo, hi = mesh.bounds
    extent = hi - lo
    cell = cell_size_m or float(np.min(extent)) / 3.0
    if cell <= 0:
        return None, {"generated": False, "reason": "degenerate bounding box"}
    step = float(np.max(extent)) / resolution
    axes = [np.arange(lo[i], hi[i] + step, step) for i in range(3)]
    gx, gy, gz = np.meshgrid(*axes, indexing="ij")
    k = 2 * np.pi / cell
    g = np.sin(k * gx) * np.cos(k * gy) + np.sin(k * gy) * np.cos(k * gz) + np.sin(k * gz) * np.cos(k * gx)
    solid = np.abs(g) < wall

    points = np.column_stack([gx.ravel(), gy.ravel(), gz.ravel()])
    try:
        inside = mesh.contains(points).reshape(gx.shape)
    except Exception as exc:
        return None, {"generated": False, "reason": f"containment test failed: {exc}"}
    field = (solid & inside).astype(np.float32)
    if field.sum() < 8:
        return None, {"generated": False, "reason": "part too thin for the requested cell size"}

    verts, faces, _, _ = marching_cubes(np.pad(field, 1), level=0.5, spacing=(step, step, step))
    lattice = trimesh.Trimesh(vertices=verts + lo - step, faces=faces, process=True)
    fill = float(field.sum() / max(inside.sum(), 1))
    return lattice, {"generated": True, "cell_size_mm": cell * 1e3, "relative_density": fill, "faces": int(len(faces))}
