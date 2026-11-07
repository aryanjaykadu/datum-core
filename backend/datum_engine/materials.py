"""Material library.

Mechanical values are typical handbook minima (ASM / MMPDS-style); corrosion
parameters follow ISO 9223 first-year rates for corrosivity category C3 with
ISO 9224 long-term exponents; HAZ factors follow EN 1999-1-1 Table 3.2 for the
aluminium alloys. Embodied carbon is cradle-to-gate from the ICE database v3.
Treat these as engineering defaults, not certified allowables.
"""
from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Material:
    key: str
    name: str
    density_kg_m3: float
    youngs_modulus_gpa: float
    yield_mpa: float
    ultimate_mpa: float
    elongation: float  # strain at failure
    haz_factor: float  # ratio of HAZ yield to parent yield
    corrosion_um_first_year: float  # ISO 9223 C3, first-year thickness loss
    corrosion_exponent: float  # ISO 9224 b in D = r * t^b
    galvanic_potential_v: float  # seawater galvanic series vs SCE
    uv_erosion_um_per_year: float  # polymers only; 0 for metals
    uv_ductility_loss_20yr: float  # fractional loss of elongation after 20 yr outdoor
    carbon_kg_per_kg: float
    commodity: str  # key into the price providers
    machinability_mm3_s: float  # representative roughing MRR on a 3-axis VMC
    specific_cutting_energy_j_mm3: float


MATERIALS: dict[str, Material] = {
    m.key: m
    for m in [
        Material("al6061t6", "Aluminium 6061-T6", 2700, 68.9, 276, 310, 0.12, 0.48, 1.0, 0.728, -0.76, 0.0, 0.0, 13.0, "aluminium", 800, 0.8),
        Material("al7075t6", "Aluminium 7075-T6", 2810, 71.7, 503, 572, 0.11, 0.45, 1.4, 0.728, -0.76, 0.0, 0.0, 13.0, "aluminium", 650, 0.9),
        Material("s355", "Structural steel S355", 7850, 210.0, 355, 510, 0.22, 1.0, 35.0, 0.523, -0.61, 0.0, 0.0, 1.55, "steel_hrc", 250, 2.8),
        Material("ss316", "Stainless steel 316L", 8000, 193.0, 170, 485, 0.40, 1.0, 0.1, 0.5, -0.05, 0.0, 0.0, 6.15, "stainless_316", 120, 3.4),
        Material("ti64", "Titanium Ti-6Al-4V", 4430, 113.8, 880, 950, 0.14, 0.95, 0.01, 0.5, -0.10, 0.0, 0.0, 35.0, "titanium", 60, 4.0),
        Material("cu110", "Copper C110", 8890, 115.0, 69, 220, 0.45, 1.0, 1.2, 0.667, -0.36, 0.0, 0.0, 2.71, "copper", 400, 2.2),
        Material("abs", "ABS (injection grade)", 1050, 2.3, 40, 44, 0.20, 1.0, 0.0, 1.0, 0.0, 8.0, 0.75, 3.55, "abs", 3000, 0.1),
        Material("pa12", "Nylon PA12", 1010, 1.7, 48, 50, 0.20, 1.0, 0.0, 1.0, 0.0, 4.0, 0.55, 6.5, "pa12", 3000, 0.1),
    ]
}

DEFAULT_MATERIAL = "al6061t6"


def get(key: str | None) -> Material:
    return MATERIALS.get(key or DEFAULT_MATERIAL, MATERIALS[DEFAULT_MATERIAL])


def match_solidworks_name(name: str) -> str | None:
    """Map a SolidWorks material name to a library key (best effort)."""
    lowered = name.lower()
    table = [
        ("6061", "al6061t6"), ("7075", "al7075t6"), ("316", "ss316"), ("ti-6al-4v", "ti64"),
        ("ti6al4v", "ti64"), ("s355", "s355"), ("plain carbon steel", "s355"), ("aisi 1020", "s355"),
        ("copper", "cu110"), ("abs", "abs"), ("nylon", "pa12"), ("pa 12", "pa12"),
    ]
    for needle, key in table:
        if needle in lowered:
            return key
    return None


def nearest_by_density(density_kg_m3: float) -> str:
    return min(MATERIALS.values(), key=lambda m: abs(m.density_kg_m3 - density_kg_m3)).key
