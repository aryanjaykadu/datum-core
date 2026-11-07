"""Electricity grid carbon intensity of the manufacturing node.

With `ELECTRICITYMAPS_TOKEN` set, the latest intensity for the zone is fetched
from the Electricity Maps API. Otherwise (and on any network error) the
reference annual averages in `data/grid_intensity.json` are used.
"""
from __future__ import annotations

import json
import os
from pathlib import Path

DATA_FILE = Path(__file__).resolve().parent.parent / "data" / "grid_intensity.json"
API = "https://api.electricitymap.org/v3/carbon-intensity/latest"


def intensity(zone: str) -> dict:
    token = os.environ.get("ELECTRICITYMAPS_TOKEN")
    if token:
        try:
            import httpx

            r = httpx.get(API, params={"zone": zone}, headers={"auth-token": token}, timeout=4.0)
            r.raise_for_status()
            body = r.json()
            return {"zone": zone, "g_co2e_per_kwh": float(body["carbonIntensity"]), "source": "electricitymaps", "as_of": body.get("datetime")}
        except Exception:
            pass
    table = json.loads(DATA_FILE.read_text())
    value = table["zones"].get(zone, table["zones"]["WORLD"])
    return {"zone": zone if zone in table["zones"] else "WORLD", "g_co2e_per_kwh": value, "source": "reference-table", "as_of": table["as_of"]}
