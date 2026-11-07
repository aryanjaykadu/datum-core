"""Shear-thinning gear lubrication in the elastohydrodynamic (EHL) mesh contact.

Film thickness: Dowson–Higginson minimum film thickness for line contact,
    h_min = 2.65 · U^0.70 · G^0.54 · W^-0.13 · R
with U = η0·u / (E'·R), G = α·E', W = w / (E'·R) (w = load per unit face width).

Viscosity: Carreau–Yasuda, η(γ̇) = η∞ + (η0 − η∞)·[1 + (λγ̇)^a]^((n−1)/a),
evaluated at the inlet shear rate γ̇ = u_s / h_min. The specific film thickness
Λ = h_min / σ (composite roughness) gives the lubrication regime.

η0 here is the inlet (ambient pressure) viscosity at operating temperature.
The shear-thinned viscosity is reported to show how much the polymer-thickened
oil thins at mesh shear rates; it does not feed back into h_min, which the
Dowson-Higginson fit already calibrates against real (non-Newtonian) oils.
"""
from __future__ import annotations

from dataclasses import asdict, dataclass


@dataclass
class GearContact:
    pitch_line_velocity_m_s: float = 10.0
    sliding_velocity_m_s: float = 1.5
    load_per_width_n_m: float = 2.0e5
    equivalent_radius_m: float = 0.008
    reduced_modulus_pa: float = 2.27e11  # steel on steel E'
    roughness_composite_m: float = 0.4e-6


@dataclass
class CarreauYasudaOil:
    eta0_pa_s: float = 0.06  # ISO VG 220 near 80 °C
    eta_inf_pa_s: float = 0.012
    relaxation_time_s: float = 2.0e-6
    power_law_index: float = 0.55
    yasuda_a: float = 2.0
    pressure_viscosity_pa_inv: float = 2.0e-8


def carreau_yasuda(oil: CarreauYasudaOil, shear_rate: float) -> float:
    lam_g = oil.relaxation_time_s * shear_rate
    return oil.eta_inf_pa_s + (oil.eta0_pa_s - oil.eta_inf_pa_s) * (1 + lam_g**oil.yasuda_a) ** (
        (oil.power_law_index - 1) / oil.yasuda_a
    )


def analyse(contact: GearContact | None = None, oil: CarreauYasudaOil | None = None) -> dict:
    c = contact or GearContact()
    o = oil or CarreauYasudaOil()
    u = c.pitch_line_velocity_m_s  # mean entraining velocity
    e, r = c.reduced_modulus_pa, c.equivalent_radius_m
    U = o.eta0_pa_s * u / (e * r)
    G = o.pressure_viscosity_pa_inv * e
    W = c.load_per_width_n_m / (e * r)
    h_min = 2.65 * U**0.70 * G**0.54 * W**-0.13 * r

    shear_rate = c.sliding_velocity_m_s / h_min
    eta_eff = carreau_yasuda(o, shear_rate)
    lam = h_min / c.roughness_composite_m
    regime = "full film" if lam >= 3 else "mixed" if lam >= 1 else "boundary"
    return {
        "film_thickness_um": h_min * 1e6,
        "inlet_shear_rate_s": shear_rate,
        "eta0_pa_s": o.eta0_pa_s,
        "eta_shear_thinned_pa_s": eta_eff,
        "viscosity_retained": eta_eff / o.eta0_pa_s,
        "specific_film_thickness": lam,
        "regime": regime,
        "inputs": {"contact": asdict(c), "oil": asdict(o)},
    }
