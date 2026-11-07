"""4D temporal degradation: 20-year surface loss applied to the inertia tensor.

Metals: atmospheric corrosion per ISO 9224, D(t) = r_corr · t^b, where r_corr is
the ISO 9223 first-year loss. A galvanic couple accelerates the anode by a
factor that grows with the potential gap and the cathode/anode area ratio.
Polymers: UV photodegradation erodes the exposed surface at a constant rate
and embrittles the remaining material (reported, does not change inertia).

The removed material is a thin shell of thickness d over the surface, so its
inertia about the CoG is I_shell = ρ·d·(tr(S)·E − S) with S = Σ A_i r_i r_iᵀ,
summed over face centroids r_i relative to the CoG. That shell is subtracted
from the current tensor. This is first-order in d, which holds while d is much
smaller than the wall thickness; the result flags when that is not the case.
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from ..materials import Material


@dataclass
class DegradationResult:
    years: float
    recession_mm: float
    mass_loss_kg: float
    mass_loss_fraction: float
    galvanic_factor: float
    ductility_retained: float
    inertia_tensor_degraded: list[list[float]]
    thin_shell_valid: bool


def galvanic_factor(anode: Material, cathode: Material | None, area_ratio: float) -> float:
    """Empirical acceleration of the anode's corrosion rate in a galvanic couple."""
    if cathode is None:
        return 1.0
    gap = cathode.galvanic_potential_v - anode.galvanic_potential_v
    if gap <= 0.05:  # anode is actually the nobler side, or the couple is benign
        return 1.0
    return float(min(1.0 + (gap / 0.25) * max(area_ratio, 0.0), 20.0))


def surface_second_moment(face_centroids: np.ndarray, face_areas: np.ndarray, cog: np.ndarray) -> np.ndarray:
    r = face_centroids - cog
    return np.einsum("i,ij,ik->jk", face_areas, r, r)


def box_surface_second_moment(extents: np.ndarray) -> np.ndarray:
    """S for an axis-aligned box centred on its CoG (fallback with no mesh)."""
    a, b, c = extents / 2.0
    s = np.zeros((3, 3))
    # pairs of faces normal to x, y, z: area, and the second moments over each face
    for axis, (u, v, w) in enumerate([(a, b, c), (b, c, a), (c, a, b)]):
        area = 4 * v * w
        diag = np.zeros(3)
        diag[axis] = 2 * area * u**2
        diag[(axis + 1) % 3] = 2 * area * v**2 / 3
        diag[(axis + 2) % 3] = 2 * area * w**2 / 3
        s += np.diag(diag)
    return s


def degrade(
    inertia_tensor: np.ndarray,
    mass_kg: float,
    material: Material,
    surface_moment: np.ndarray,
    surface_area_m2: float,
    min_wall_mm: float,
    years: float = 20.0,
    exposed_fraction: float = 1.0,
    cathode: Material | None = None,
    cathode_area_ratio: float = 0.0,
) -> DegradationResult:
    g = galvanic_factor(material, cathode, cathode_area_ratio)
    corrosion_um = material.corrosion_um_first_year * years**material.corrosion_exponent * g
    uv_um = material.uv_erosion_um_per_year * years
    recession_m = (corrosion_um + uv_um) * 1e-6 * exposed_fraction

    shell_s = surface_moment * exposed_fraction
    i_shell = material.density_kg_m3 * recession_m * (np.trace(shell_s) * np.eye(3) - shell_s)
    mass_loss = min(material.density_kg_m3 * recession_m * surface_area_m2 * exposed_fraction, mass_kg)
    degraded = inertia_tensor - i_shell

    ductility = 1.0 - material.uv_ductility_loss_20yr * min(years / 20.0, 1.0) * exposed_fraction
    return DegradationResult(
        years=years,
        recession_mm=recession_m * 1e3,
        mass_loss_kg=float(mass_loss),
        mass_loss_fraction=float(mass_loss / mass_kg) if mass_kg else 0.0,
        galvanic_factor=g,
        ductility_retained=float(ductility),
        inertia_tensor_degraded=degraded.tolist(),
        thin_shell_valid=bool(recession_m * 1e3 < 0.1 * max(min_wall_mm, 1e-6)),
    )
