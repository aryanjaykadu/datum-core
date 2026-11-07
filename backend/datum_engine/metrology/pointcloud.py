"""Point-cloud loading.

`.e57` goes through pye57, a binding to the C++ libE57Format reader; scans
with several setups are merged into one cloud in the file's world frame.
`.ply`, `.xyz`, `.pts` and `.txt` are read with trimesh / numpy.
"""
from __future__ import annotations

from pathlib import Path

import numpy as np

CLOUD_EXTENSIONS = {".e57", ".ply", ".xyz", ".pts", ".txt"}


def load(path: Path, max_points: int = 2_000_000, seed: int = 3) -> np.ndarray:
    ext = path.suffix.lower()
    if ext == ".e57":
        try:
            import pye57  # type: ignore
        except ImportError as exc:
            raise RuntimeError("E57 support needs pye57 (pip install pye57)") from exc
        e57 = pye57.E57(str(path))
        clouds = []
        for index in range(e57.scan_count):
            data = e57.read_scan(index, ignore_missing_fields=True, transform=True)
            clouds.append(np.column_stack([data["cartesianX"], data["cartesianY"], data["cartesianZ"]]))
        points = np.vstack(clouds)
    elif ext == ".ply":
        import trimesh

        points = np.asarray(trimesh.load(path).vertices)
    else:
        points = np.loadtxt(path, usecols=(0, 1, 2), comments="#")
    points = points[np.isfinite(points).all(axis=1)]
    if len(points) > max_points:
        points = points[np.random.default_rng(seed).choice(len(points), max_points, replace=False)]
    return points.astype(np.float64)
