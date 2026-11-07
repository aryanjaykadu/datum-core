"""Sonic boom ground overpressure from the part's own area distribution.

This is linearised supersonic theory, not CFD:
1. Slice the mesh along the flight axis to get the equivalent-body area A(x)
   (zero-lift, so the Mach-plane tilt is ignored).
2. Whitham F-function, F(y) = 1/(2π) ∫₀ʸ A''(ξ)/√(y−ξ) dξ, computed in the
   regular form F = (1/2π)·d/dy ∫₀ʸ 2·A'(ξ)·√(y−ξ) dξ.
3. Far-field N-wave bow shock (equal-area rule):
   Δp/p = 2^¼·γ·(M²−1)^⅛ / √(γ+1) · √(max ∫₀ʸ F dη) · r^-¾
4. Ground value: reflection factor K_r = 2, with p = √(p_altitude·p_ground)
   as in Carlson's simplified method (NASA TP-1122).

Only meaningful for slender bodies in steady supersonic flight.
"""
from __future__ import annotations

import math

import numpy as np
import trimesh

GAMMA = 1.4
P_SEA_LEVEL = 101_325.0


def isa_pressure(altitude_m: float) -> float:
    if altitude_m <= 11_000:
        return P_SEA_LEVEL * (1 - 2.25577e-5 * altitude_m) ** 5.25588
    return 22_632.1 * math.exp(-1.57688e-4 * (altitude_m - 11_000))


def area_distribution(mesh: trimesh.Trimesh, stations: int = 160) -> tuple[np.ndarray, np.ndarray, int]:
    axis = int(np.argmax(mesh.extents))
    normal = np.zeros(3)
    normal[axis] = 1.0
    lo, hi = mesh.bounds[0][axis], mesh.bounds[1][axis]
    xs = np.linspace(lo, hi, stations)
    origin = mesh.bounds[0].copy()
    sections = mesh.section_multiplane(plane_origin=origin, plane_normal=normal, heights=(xs - lo).tolist())
    areas = np.array([float(s.area) if s is not None else 0.0 for s in sections])
    # The nose is the pointier end: flip so the body flies toward x = 0.
    tenth = max(stations // 10, 1)
    if areas[:tenth].mean() > areas[-tenth:].mean():
        areas = areas[::-1]
    return xs - lo, areas, axis


def whitham_f(x: np.ndarray, area: np.ndarray) -> np.ndarray:
    dx = x[1] - x[0]
    a_prime = np.gradient(area, dx)
    g = np.array([np.sum(2.0 * a_prime[: i + 1] * np.sqrt(np.maximum(x[i] - x[: i + 1], 0.0))) * dx for i in range(len(x))])
    return np.gradient(g, dx) / (2.0 * math.pi)


def ground_overpressure(
    mesh: trimesh.Trimesh,
    mach: float = 1.6,
    altitude_m: float = 15_000.0,
    reflection_factor: float = 2.0,
) -> dict:
    if mach <= 1.0:
        return {"supersonic": False, "overpressure_pa": 0.0, "overpressure_psf": 0.0}
    x, area, axis = area_distribution(mesh)
    f = whitham_f(x, area)
    integral = np.cumsum(f) * (x[1] - x[0])
    i_max = float(max(integral.max(), 0.0))

    beta = math.sqrt(mach**2 - 1)
    p_eff = math.sqrt(isa_pressure(altitude_m) * P_SEA_LEVEL)
    ratio = 2**0.25 * GAMMA * beta**0.25 / math.sqrt(GAMMA + 1) * math.sqrt(i_max) * altitude_m**-0.75
    dp = reflection_factor * p_eff * ratio
    return {
        "supersonic": True,
        "mach": mach,
        "altitude_m": altitude_m,
        "body_length_m": float(x[-1]),
        "max_area_m2": float(area.max()),
        "flight_axis": "xyz"[axis],
        "f_function_peak": float(f.max()),
        "overpressure_pa": dp,
        "overpressure_psf": dp / 47.880259,
    }
