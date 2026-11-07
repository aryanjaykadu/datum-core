//! Pillar 6: the strict JSON-RPC contract between shell and UI. Every struct rejects
//! unknown fields, so engine/shell/UI drift fails loudly instead of dropping data.

use serde::{Deserialize, Serialize};

pub type Matrix3 = [[f64; 3]; 3];

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct KernelMetrics {
    pub dag_nodes_computed: u64,
    pub gpu_vram_usage_mb: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CommerceAndSupplyChain {
    pub live_cnc_quote_usd: f64,
    pub embodied_carbon_kg: f64,
    pub transaction_fee_revenue_usd: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct SecurityForensics {
    pub itar_tpm_locked: bool,
    pub steganography_detected: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct AdvancedFeaAndCad {
    pub mbd_max_deflection_mm: f64,
    pub explicit_fracture_detected: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct TelemetryAnd4d {
    pub mass_kg: f64,
    pub inertia_tensor_current: Matrix3,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DatumPayload {
    pub system_id: String,
    pub kernel_metrics: KernelMetrics,
    pub commerce_and_supply_chain: CommerceAndSupplyChain,
    pub security_forensics: SecurityForensics,
    pub advanced_fea_and_cad: AdvancedFeaAndCad,
    pub telemetry_and_4d: TelemetryAnd4d,
}

impl DatumPayload {
    pub fn validate(&self) -> Result<(), String> {
        if uuid::Uuid::parse_str(&self.system_id).map(|u| u.get_version_num()) != Ok(4) {
            return Err("system_id is not a UUID v4".into());
        }
        let finite = [
            self.telemetry_and_4d.mass_kg,
            self.commerce_and_supply_chain.live_cnc_quote_usd,
            self.advanced_fea_and_cad.mbd_max_deflection_mm,
        ];
        if finite.iter().any(|v| !v.is_finite()) || self.telemetry_and_4d.mass_kg < 0.0 {
            return Err("non-finite or negative value in contract".into());
        }
        Ok(())
    }
}
