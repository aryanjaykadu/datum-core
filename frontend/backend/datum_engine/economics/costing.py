"""Should-cost and embodied carbon for a machined part.

Stock mass = part mass × buy-to-fly ratio. Removed material sets the cutting
time (at the material's roughing MRR) and the cutting energy (specific cutting
energy × removed volume, divided by spindle/machine efficiency).

    cost   = stock mass · price/kg + cutting hours · machine rate + setup
    carbon = stock mass · embodied factor + machine kWh · grid intensity
"""
from __future__ import annotations

from ..materials import Material
from . import grid, pricing

MACHINE_RATE_USD_H = 85.0
SETUP_USD = 40.0
MACHINE_EFFICIENCY = 0.30  # spindle cutting energy / total machine draw
LABOUR_INFLATION = 0.03


def evaluate(material: Material, mass_kg: float, buy_to_fly: float = 3.0, zone: str = "WORLD", years: float = 5.0) -> dict:
    q = pricing.quote(material.commodity)
    stock_kg = mass_kg * buy_to_fly
    removed_mm3 = (stock_kg - mass_kg) / material.density_kg_m3 * 1e9
    hours = removed_mm3 / material.machinability_mm3_s / 3600.0
    kwh = removed_mm3 * material.specific_cutting_energy_j_mm3 / MACHINE_EFFICIENCY / 3.6e6

    material_cost = stock_kg * q.usd_per_kg
    conversion = hours * MACHINE_RATE_USD_H + SETUP_USD
    future_material = stock_kg * pricing.forecast(q, years)
    future_conversion = conversion * (1 + LABOUR_INFLATION) ** years

    g = grid.intensity(zone)
    carbon = stock_kg * material.carbon_kg_per_kg + kwh * g["g_co2e_per_kwh"] / 1000.0
    return {
        "live_cost_usd": material_cost + conversion,
        "projected_cost_5yr_usd": future_material + future_conversion,
        "dynamic_carbon_kg_co2e": carbon,
        "breakdown": {
            "stock_kg": stock_kg,
            "material_usd": material_cost,
            "machining_hours": hours,
            "conversion_usd": conversion,
            "machine_kwh": kwh,
            "price_usd_per_kg": q.usd_per_kg,
            "price_source": q.source,
            "price_as_of": q.as_of,
            "price_growth_rate": pricing.growth_rate(q),
            "grid": g,
        },
    }
