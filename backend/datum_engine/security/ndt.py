"""NDT inspectability: share of surface a probe or camera can reach in line of sight.

Each sampled face is tested from 26 approach directions (cube faces, edges and
corners). A face counts as inspectable from a direction if the direction is
within ~78° of its outward normal and a ray from just above the face escapes
the part without hitting it. Score = area-weighted percentage of faces with at
least `min_directions` clear approaches (2 by default, so a probe can be
angled for UT and visual inspection).
"""
from __future__ import annotations

import itertools

import numpy as np
import trimesh

DIRECTIONS = np.array([d for d in itertools.product((-1, 0, 1), repeat=3) if any(d)], dtype=float)
DIRECTIONS /= np.linalg.norm(DIRECTIONS, axis=1, keepdims=True)


def inspectability(mesh: trimesh.Trimesh, samples: int = 1500, min_directions: int = 2, seed: int = 7) -> dict:
    rng = np.random.default_rng(seed)
    areas = mesh.area_faces
    count = min(samples, len(mesh.faces))
    picked = rng.choice(len(mesh.faces), size=count, replace=False, p=areas / areas.sum())
    centres = mesh.triangles_center[picked]
    normals = mesh.face_normals[picked]
    eps = 1e-4 * float(np.max(mesh.extents))

    clear = np.zeros(count, dtype=int)
    intersector = mesh.ray
    for d in DIRECTIONS:
        facing = normals @ d > 0.2
        if not facing.any():
            continue
        origins = centres[facing] + normals[facing] * eps
        hits = intersector.intersects_any(origins, np.repeat(d[None, :], len(origins), axis=0))
        clear[np.flatnonzero(facing)[~hits]] += 1

    ok = clear >= min_directions
    weights = areas[picked]
    score = 100.0 * float(weights[ok].sum() / weights.sum())
    return {"score": round(score, 2), "samples": int(count), "directions": len(DIRECTIONS), "min_directions": min_directions}
