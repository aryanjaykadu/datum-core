"""Commodity pricing for BOM costing and 5-year forecasting.

Providers, tried in order:
1. Bloomberg Desktop API via `blpapi` (needs a logged-in Bloomberg Terminal on
   this machine, which listens on localhost:8194). Pulls PX_LAST and five
   years of monthly history from //blp/refdata.
2. The local reference table `data/commodity_prices.json`. These are static
   reference values, not market data; every result says which source it used.

Forecast: least-squares fit of log(price) against time over the history, so
the projection extrapolates the historical compound growth rate. With no
history the reference table's `annual_drift` is used.
"""
from __future__ import annotations

import datetime as dt
import json
import math
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from .. import events

DATA_FILE = Path(__file__).resolve().parent.parent / "data" / "commodity_prices.json"

BLOOMBERG_TICKERS = {
    "aluminium": "LMAHDS03 Comdty",  # LME aluminium 3-month, USD/t
    "copper": "LMCADS03 Comdty",
    "nickel": "LMNIDS03 Comdty",
    "steel_hrc": "HRC1 Comdty",  # CME US Midwest HRC, USD/short ton
}
TICKER_UNIT_TO_KG = {"LMAHDS03 Comdty": 1 / 1000, "LMCADS03 Comdty": 1 / 1000, "LMNIDS03 Comdty": 1 / 1000, "HRC1 Comdty": 1 / 907.185}


@dataclass
class Quote:
    commodity: str
    usd_per_kg: float
    source: str
    as_of: str
    history: list[tuple[float, float]]  # (years since start, usd_per_kg)
    annual_drift: float


def _reference(commodity: str) -> Quote:
    table = json.loads(DATA_FILE.read_text())
    entry = table["commodities"][commodity]
    return Quote(commodity, entry["usd_per_kg"], "reference-table", table["as_of"], [], entry["annual_drift"])


def _bloomberg(commodity: str) -> Quote | None:
    ticker = BLOOMBERG_TICKERS.get(commodity)
    if ticker is None:
        return None
    try:
        import blpapi  # type: ignore
    except ImportError:
        return None
    options = blpapi.SessionOptions()
    options.setServerHost("localhost")
    options.setServerPort(8194)
    session = blpapi.Session(options)
    if not session.start() or not session.openService("//blp/refdata"):
        return None
    try:
        service = session.getService("//blp/refdata")
        request = service.createRequest("HistoricalDataRequest")
        request.getElement("securities").appendValue(ticker)
        request.getElement("fields").appendValue("PX_LAST")
        today = dt.date.today()
        request.set("startDate", (today - dt.timedelta(days=5 * 365)).strftime("%Y%m%d"))
        request.set("endDate", today.strftime("%Y%m%d"))
        request.set("periodicitySelection", "MONTHLY")
        session.sendRequest(request)

        history: list[tuple[dt.date, float]] = []
        while True:
            event = session.nextEvent(5000)
            for msg in event:
                if msg.hasElement("securityData"):
                    rows = msg.getElement("securityData").getElement("fieldData")
                    for i in range(rows.numValues()):
                        row = rows.getValueAsElement(i)
                        history.append((row.getElementAsDatetime("date"), row.getElementAsFloat("PX_LAST")))
            if event.eventType() == blpapi.Event.RESPONSE:
                break
            if event.eventType() == blpapi.Event.TIMEOUT:
                return None
    finally:
        session.stop()
    if not history:
        return None
    k = TICKER_UNIT_TO_KG[ticker]
    t0 = history[0][0]
    series = [((d - t0).days / 365.25, p * k) for d, p in history]
    return Quote(commodity, series[-1][1], f"bloomberg:{ticker}", history[-1][0].isoformat(), series, 0.0)


def quote(commodity: str) -> Quote:
    try:
        live = _bloomberg(commodity)
        if live:
            return live
    except Exception as exc:  # a Terminal that is not logged in raises here
        events.log(f"Bloomberg unavailable for {commodity}: {exc}", level="warn", stage="economics")
    return _reference(commodity)


def growth_rate(q: Quote) -> float:
    """Annual compound growth fitted to the history (log-linear)."""
    if len(q.history) >= 6:
        t = np.array([h[0] for h in q.history])
        y = np.log([max(h[1], 1e-9) for h in q.history])
        slope = float(np.polyfit(t, y, 1)[0])
        return math.exp(slope) - 1
    return q.annual_drift


def forecast(q: Quote, years: float = 5.0) -> float:
    return q.usd_per_kg * (1 + growth_rate(q)) ** years
