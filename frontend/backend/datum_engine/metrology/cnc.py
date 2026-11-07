"""Virtual CAM collision check for 3-axis setups.

For each setup the tool approaches along −axis. A sampled face is machinable
in that setup if the tool can reach it: the face points toward the spindle and
a ring of rays at the cutter radius (plus the centre ray) travels up the tool
axis without hitting the part. Faces reachable in no setup are undercuts;
faces only reachable through a gap narrower than the cutter are flagged as
tool collisions. Positions are returned so the viewport can pin them.
"""
from __future__ import annotations

import numpy as np
import trimesh

SETUPS = {"+Z": (0, 0, 1), "-Z": (0, 0, -1), "+X": (1, 0, 0), "-X": (-1, 0, 0), "+Y": (0, 1, 0), "-Y": (0, -1, 0)}


def _ring(axis: np.ndarray, radius: float, count: int = 8) -> np.ndarray:
    helper = np.array([1.0, 0, 0]) if abs(axis[0]) < 0.9 else np.array([0, 1.0, 0])
    u = np.cross(axis, helper)
    u /= np.linalg.norm(u)
    v = np.cross(axis, u)
    angles = np.linspace(0, 2 * np.pi, count, endpoint=False)
    return np.vstack([[0, 0, 0], radius * (np.outer(np.cos(angles), u) + np.outer(np.sin(angles), v))])


def check(mesh: trimesh.Trimesh, setups: tuple[str, ...] = ("+Z",), cutter_radius_m: float = 0.003, samples: int = 1200, seed: int = 5) -> dict:
    rng = np.random.default_rng(seed)
    count = min(samples, len(mesh.faces))
    areas = mesh.area_faces
    picked = rng.choice(len(mesh.faces), size=count, replace=False, p=areas / areas.sum())
    centres = mesh.triangles_center[picked]
    normals = mesh.face_normals[picked]
    eps = 1e-4 * float(np.max(mesh.extents))

    centre_clear = np.zeros(count, dtype=bool)
    tool_clear = np.zeros(count, dtype=bool)
    for name in setups:
        axis = np.asarray(SETUPS[name], dtype=float)
        facing = normals @ axis > 0.05
        if not facing.any():
            continue
        idx = np.flatnonzero(facing)
        offsets = _ring(axis, cutter_radius_m)
        clear_all = np.ones(len(idx), dtype=bool)
        for k, offset in enumerate(offsets):
            origins = centres[idx] + normals[idx] * eps + offset + axis * eps
            hit = mesh.ray.intersects_any(origins, np.repeat(axis[None, :], len(idx), axis=0))
            if k == 0:
                centre_clear[idx[~hit]] = True
            clear_all &= ~hit
        tool_clear[idx[clear_all]] = True

    undercut = ~centre_clear
    collision = centre_clear & ~tool_clear
    flags = [{"xyz_mm": (centres[i] * 1e3).round(2).tolist(), "severity": "HIGH", "kind": "undercut"} for i in np.flatnonzero(undercut)[:40]]
    flags += [{"xyz_mm": (centres[i] * 1e3).round(2).tolist(), "severity": "MEDIUM", "kind": "tool_collision"} for i in np.flatnonzero(collision)[:40]]
    weights = areas[picked]
    return {
        "setups": list(setups),
        "cutter_diameter_mm": cutter_radius_m * 2e3,
        "undercut_area_pct": round(100 * float(weights[undercut].sum() / weights.sum()), 2),
        "collision_area_pct": round(100 * float(weights[collision].sum() / weights.sum()), 2),
        "flags": flags,
    }
