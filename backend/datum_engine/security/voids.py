"""B-Rep "steganography" scanner: hidden internal micro-voids and inclusions.

A void that is invisible from outside but sits just under a stressed surface
is a plausible way to plant a deterministic fatigue failure in a model that
otherwise passes inspection. On a tessellated B-Rep it shows up as an extra
closed shell inside the main body:
* an inward-facing shell (negative signed volume) is a cavity;
* a separate closed body fully inside the main one is an embedded inclusion.

A shell is flagged when it is small (below `max_void_fraction` of the part
volume) and its centre lies within `depth_factor` void diameters of the outer
skin, where a spherical void raises local stress by Kt ≈ 2.05 and fatigue
cracks initiate. Large intentional cavities (pockets, cores) are reported but
not flagged.
"""
from __future__ import annotations

import numpy as np
import trimesh


def scan(mesh: trimesh.Trimesh, max_void_fraction: float = 0.005, depth_factor: float = 3.0) -> dict:
    shells = mesh.split(only_watertight=False)
    if len(shells) <= 1:
        return {"steganography_detected": False, "shells": 1, "findings": []}

    shells = sorted(shells, key=lambda s: abs(s.volume), reverse=True)
    outer = shells[0]
    if outer.volume < 0:
        outer = outer.copy()
        outer.invert()
    part_volume = abs(mesh.volume) or abs(outer.volume)

    findings = []
    for shell in shells[1:]:
        centre = shell.bounds.mean(axis=0)
        try:
            inside = bool(outer.contains([centre])[0])
        except Exception:
            inside = bool(np.all(shell.bounds[0] >= outer.bounds[0]) and np.all(shell.bounds[1] <= outer.bounds[1]))
        if not inside:
            continue  # a separate body sitting beside the part, not a hidden feature
        volume = abs(shell.volume)
        diameter = float(np.max(shell.extents))
        _, depth, _ = trimesh.proximity.closest_point(outer, [centre])
        depth = float(depth[0])
        kind = "cavity" if shell.volume < 0 else "inclusion"
        suspicious = volume < max_void_fraction * part_volume and depth < depth_factor * max(diameter, 1e-9)
        findings.append(
            {
                "kind": kind,
                "centre_mm": (centre * 1e3).round(3).tolist(),
                "diameter_mm": round(diameter * 1e3, 4),
                "volume_mm3": round(float(volume) * 1e9, 5),
                "depth_below_skin_mm": round(depth * 1e3, 4),
                "stress_concentration_kt": 2.05 if kind == "cavity" else None,
                "suspicious": bool(suspicious),
            }
        )
    return {
        "steganography_detected": any(f["suspicious"] for f in findings),
        "shells": len(shells),
        "findings": findings,
    }
