"""Pillar 7 — the strict JSON-RPC contract shared with the Rust shell.

Every model forbids unknown keys, and the Rust side mirrors this with
`#[serde(deny_unknown_fields)]`, so a schema drift fails loudly on both ends.
Anything that is not part of the contract (provenance, CoG, feature tree, etc.)
travels in a separate `EvaluationDetails` document.
"""
from __future__ import annotations

import hashlib
import json
import uuid
from typing import Annotated

from pydantic import BaseModel, ConfigDict, Field

Matrix3 = Annotated[list[Annotated[list[float], Field(min_length=3, max_length=3)]], Field(min_length=3, max_length=3)]


class _Strict(BaseModel):
    model_config = ConfigDict(extra="forbid", frozen=False)


class SecurityForensics(_Strict):
    steganography_detected: bool
    itar_tpm_locked: bool
    ndt_inspectability_score: float = Field(ge=0.0, le=100.0)


class AdvancedFeaAndCad(_Strict):
    mbd_max_deflection_mm: float
    explicit_fracture_detected: bool
    weldment_haz_yield_mpa: float
    quantum_lattice_generated: bool


class TelemetryAnd4D(_Strict):
    mass_kg: float
    inertia_tensor_current: Matrix3
    inertia_tensor_20yr_degraded: Matrix3


class EnterpriseEconomics(_Strict):
    live_cost_usd: float
    projected_cost_5yr_usd: float
    dynamic_carbon_kg_co2e: float


class DatumPayload(_Strict):
    system_id: str
    audit_hash: str
    security_forensics: SecurityForensics
    advanced_fea_and_cad: AdvancedFeaAndCad
    telemetry_and_4d: TelemetryAnd4D
    enterprise_economics: EnterpriseEconomics


def canonical_json(data: dict) -> bytes:
    return json.dumps(data, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode()


def seal(payload: DatumPayload) -> DatumPayload:
    """Fill `audit_hash` with SHA-256 over the canonical payload (hash field blanked)."""
    body = payload.model_dump()
    body["audit_hash"] = ""
    payload.audit_hash = hashlib.sha256(canonical_json(body)).hexdigest()
    return payload


def verify(payload: DatumPayload) -> bool:
    body = payload.model_dump()
    expected = body.pop("audit_hash")
    body["audit_hash"] = ""
    return hashlib.sha256(canonical_json(body)).hexdigest() == expected


def new_system_id() -> str:
    return str(uuid.uuid4())
