"""MBD drop coupled to an explicit flexible crash model with element deletion.

Stage 1 (rigid multibody): an impactor of mass m falls from height h, so it
arrives with v = √(2gh). Stage 2 (explicit dynamics): it strikes the tip of
the part modelled as a cantilever Euler–Bernoulli beam (span = longest bbox
side, rectangular section from the other two sides, scaled to the part's real
mass) through a Hertz contact, F = 4/3·E*·√R·δ^1.5, for a steel impactor
nose of radius R. Central-difference time integration with lumped mass, the step set at
90 % of the stability limit 2/ω_max. After each step the extreme-fibre bending
stress of every element is checked; an element above the material's ultimate
strength is deleted (its stiffness removed) and fracture is reported.

A screening model: no plasticity, no shear deformation, no 3D stress states.
`beam_theory_valid` is false for stubby parts (span < 5 × depth), where the
Euler-Bernoulli result should only be read as an order of magnitude.
"""
from __future__ import annotations

import math

import numpy as np
from scipy.linalg import eigh

from ..materials import Material

G = 9.80665


def _element_stiffness(ei: float, length: float) -> np.ndarray:
    l = length
    return ei / l**3 * np.array(
        [[12, 6 * l, -12, 6 * l], [6 * l, 4 * l * l, -6 * l, 2 * l * l], [-12, -6 * l, 12, -6 * l], [6 * l, 2 * l * l, -6 * l, 4 * l * l]]
    )


def simulate(
    material: Material,
    span_m: float,
    width_m: float,
    height_m: float,
    part_mass_kg: float,
    impactor_mass_kg: float = 5.0,
    drop_height_m: float = 0.5,
    elements: int = 24,
    duration_s: float = 0.02,
    damping_ratio: float = 0.02,
    impactor_radius_m: float = 0.01,
) -> dict:
    n_nodes = elements + 1
    le = span_m / elements
    e_pa = material.youngs_modulus_gpa * 1e9
    inertia = width_m * height_m**3 / 12.0
    ei = e_pa * inertia
    m_elem = part_mass_kg / elements
    dof = 2 * n_nodes

    k_e = _element_stiffness(ei, le)
    m_diag = np.zeros(dof)
    for e in range(elements):
        idx = [2 * e, 2 * e + 1, 2 * e + 2, 2 * e + 3]
        m_diag[idx[0]] += m_elem / 2
        m_diag[idx[2]] += m_elem / 2
        m_diag[idx[1]] += m_elem * le**2 / 78
        m_diag[idx[3]] += m_elem * le**2 / 78
    tip = 2 * elements
    nu = 0.3
    e_star = 1.0 / ((1 - nu**2) / e_pa + (1 - nu**2) / 210e9)
    hertz = 4.0 / 3.0 * e_star * math.sqrt(impactor_radius_m)

    alive = np.ones(elements, dtype=bool)

    def assemble() -> np.ndarray:
        k = np.zeros((dof, dof))
        for e in np.flatnonzero(alive):
            idx = np.array([2 * e, 2 * e + 1, 2 * e + 2, 2 * e + 3])
            k[np.ix_(idx, idx)] += k_e
        return k

    free = np.arange(2, dof)  # node 0 clamped (w = θ = 0)
    k = assemble()
    kf = k[np.ix_(free, free)]
    mf = m_diag[free]
    omega_max = math.sqrt(eigh(kf, np.diag(mf), eigvals_only=True, subset_by_index=[len(free) - 1, len(free) - 1])[0])
    omega_1 = math.sqrt(max(eigh(kf, np.diag(mf), eigvals_only=True, subset_by_index=[0, 0])[0], 0.0))
    dt = 0.9 * 2.0 / omega_max
    # cover at least ~0.6 of the first period so the peak response is captured
    duration = max(duration_s, 0.6 * 2 * math.pi / max(omega_1, 1e-9))
    steps = min(int(duration / dt), 200_000)
    c_damp = 2 * damping_ratio * omega_1 * mf  # mass-proportional damping

    v_impact = math.sqrt(2 * G * drop_height_m)
    u = np.zeros(len(free))
    v = np.zeros(len(free))
    z_imp, v_imp = 0.0, -v_impact  # impactor touches the tip at t = 0, moving down
    peak_force = 0.0

    # strain-displacement for curvature at both ends of an element
    b0 = np.array([-6 / le**2, -4 / le, 6 / le**2, -2 / le])
    b1 = np.array([6 / le**2, 2 / le, -6 / le**2, 4 / le])
    c_fibre = e_pa * height_m / 2
    curv0 = np.zeros((elements, dof))
    curv1 = np.zeros((elements, dof))
    for e in range(elements):
        curv0[e, 2 * e : 2 * e + 4] = b0
        curv1[e, 2 * e : 2 * e + 4] = b1
    ultimate_pa = material.ultimate_mpa * 1e6

    max_deflection = 0.0
    max_stress = 0.0
    fractured_at: list[float] = []
    fracture_time = None
    full = np.zeros(dof)
    def contact_force(w_tip: float, z: float) -> float:
        return hertz * (w_tip - z) ** 1.5 if w_tip > z else 0.0

    f_ext = np.zeros(len(free))
    a = -(kf @ u) / mf
    a_imp = 0.0
    for step in range(steps):
        v_half = v + 0.5 * dt * a
        v_imp_half = v_imp + 0.5 * dt * a_imp
        u = u + dt * v_half
        z_imp += dt * v_imp_half
        full[free] = u
        stresses = c_fibre * np.maximum(np.abs(curv0 @ full), np.abs(curv1 @ full)) * alive
        max_stress = max(max_stress, float(stresses.max()))
        broken = alive & (stresses > ultimate_pa)
        if broken.any():
            alive &= ~broken
            fractured_at += [round((e + 0.5) * le * 1e3, 2) for e in np.flatnonzero(broken)]
            fracture_time = fracture_time or step * dt
            kf = assemble()[np.ix_(free, free)]
        # deflection is measured on the part still attached to the clamp
        first_dead = int(np.argmin(alive)) if not alive.all() else elements
        attached_w = full[0 : 2 * first_dead + 1 : 2]
        max_deflection = max(max_deflection, float(np.abs(attached_w).max()))
        f = contact_force(u[tip - 2], z_imp)
        peak_force = max(peak_force, f)
        f_ext[tip - 2] = -f
        a = (f_ext - (kf @ u) - c_damp * v_half) / mf
        a_imp = f / impactor_mass_kg - G
        v = v_half + 0.5 * dt * a
        v_imp = v_imp_half + 0.5 * dt * a_imp

    return {
        "impact_velocity_m_s": v_impact,
        "time_step_s": dt,
        "steps": steps,
        "simulated_time_ms": steps * dt * 1e3,
        "first_mode_hz": omega_1 / (2 * math.pi),
        "max_deflection_mm": max_deflection * 1e3,
        "max_bending_stress_mpa": max_stress / 1e6,
        "peak_contact_force_n": float(peak_force),
        "beam_theory_valid": bool(span_m >= 5 * height_m),
        "fracture_detected": bool(fractured_at),
        "fracture_time_ms": fracture_time * 1e3 if fracture_time is not None else None,
        "deleted_elements_at_mm": fractured_at,
    }
