"""Scrap-metal routing: fit a CAD part inside an irregular scanned offcut.

1. Voxelise the scan surface and fill the interior, giving solid occupancy.
2. A Euclidean distance transform gives every voxel's clearance to the skin.
3. For each candidate orientation (24 axis-aligned rotations plus random ones
   from a uniform SO(3) sample), the part's sample points are rotated about
   their centroid and the centroid is tried on the highest-clearance voxels.
   A placement is valid if every point lands in material with at least
   `margin_m` of clearance; the best one maximises the worst clearance.

The returned 4×4 matrix maps CAD coordinates into the scan frame. It is a
brute-force search with the scan treated as rigid stock, not a learned model.
"""
from __future__ import annotations

import itertools

import numpy as np
from scipy import ndimage
from scipy.spatial.transform import Rotation


def _occupancy(points: np.ndarray, voxel: float) -> tuple[np.ndarray, np.ndarray]:
    origin = points.min(axis=0) - 2 * voxel
    idx = np.floor((points - origin) / voxel).astype(int)
    shape = idx.max(axis=0) + 3
    grid = np.zeros(shape, dtype=bool)
    grid[tuple(idx.T)] = True
    grid = ndimage.binary_closing(grid, iterations=2)
    return ndimage.binary_fill_holes(grid), origin


def _axis_rotations() -> list[Rotation]:
    mats = []
    for perm in itertools.permutations(range(3)):
        for signs in itertools.product((1, -1), repeat=3):
            m = np.zeros((3, 3))
            for r, c in enumerate(perm):
                m[r, c] = signs[r]
            if np.linalg.det(m) > 0:
                mats.append(Rotation.from_matrix(m))
    return mats


def fit(part_points: np.ndarray, scan_points: np.ndarray, voxel_m: float = 0.002, margin_m: float = 0.001, random_orientations: int = 200, candidates: int = 64, seed: int = 11) -> dict:
    solid, origin = _occupancy(scan_points, voxel_m)
    clearance = ndimage.distance_transform_edt(solid) * voxel_m
    flat = np.argsort(clearance.ravel())[::-1][:candidates]
    seeds = np.column_stack(np.unravel_index(flat, clearance.shape)) * voxel_m + origin

    centroid = part_points.mean(axis=0)
    local = part_points - centroid
    rotations = _axis_rotations() + list(Rotation.random(random_orientations, random_state=seed))

    best = None
    for rot in rotations:
        rotated = rot.apply(local)
        for seed_point in seeds:
            pts = rotated + seed_point
            idx = np.floor((pts - origin) / voxel_m).astype(int)
            if (idx < 0).any() or (idx >= clearance.shape).any():
                continue
            worst = float(clearance[tuple(idx.T)].min())
            if worst >= margin_m and (best is None or worst > best[0]):
                best = (worst, rot, seed_point)

    if best is None:
        return {"fits": False, "searched_orientations": len(rotations)}
    worst, rot, target = best
    transform = np.eye(4)
    transform[:3, :3] = rot.as_matrix()
    transform[:3, 3] = target - rot.apply(centroid)
    return {
        "fits": True,
        "min_clearance_mm": worst * 1e3,
        "transform": transform.tolist(),
        "searched_orientations": len(rotations),
        "stock_volume_cm3": float(solid.sum() * voxel_m**3 * 1e6),
    }
