"""Heat-affected-zone yield strength for weldments.

HAZ yield = ρ_haz · parent yield. ρ_haz comes from the material library
(EN 1999-1-1 Table 3.2 for aluminium; ~1.0 for structural and stainless
steels, whose HAZ does not soften under normal heat input).
"""
from __future__ import annotations

from ..materials import Material

WELD_FEATURE_TYPES = ("weldbead", "weldmentfeat", "filletbead", "weldmenttrimextend", "structuralweldment")


def has_weld_features(features: list[dict]) -> bool:
    return any(any(tag in f.get("type", "").lower() for tag in WELD_FEATURE_TYPES) for f in features)


def haz_yield_mpa(material: Material) -> float:
    return material.yield_mpa * material.haz_factor
