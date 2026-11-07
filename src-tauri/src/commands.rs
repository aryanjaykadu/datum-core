//! IPC commands exposed to the UI.

use crate::contract::DatumPayload;
use crate::docfile;
use crate::engine::Engine;
use serde_json::{json, Value};
use std::sync::Mutex;
use tauri::ipc::Response;
use tauri::{AppHandle, Manager, State};

pub struct LaunchFile(pub Mutex<Option<String>>);
/// Whether the open document came from (or was last saved as) a device-locked file.
pub struct Protection(pub Mutex<bool>);

/// Methods the UI may call directly; anything else is refused.
const METHODS: &[&str] = &["catalog", "example", "quote", "export_stl", "robot_fk", "ping"];

async fn call(app: AppHandle, method: &'static str, params: Value) -> Result<crate::engine::Reply, String> {
    tauri::async_runtime::spawn_blocking(move || app.state::<Engine>().call(&app, method, params))
        .await
        .map_err(|e| e.to_string())?
}

#[tauri::command]
pub async fn engine_call(app: AppHandle, method: String, params: Option<Value>) -> Result<Value, String> {
    let method: &'static str = METHODS.iter().copied().find(|m| *m == method).ok_or(format!("method not allowed: {method}"))?;
    Ok(call(app, method, params.unwrap_or(json!({}))).await?.result)
}

/// Rebuild the document. The contract is validated against the strict schema; the shell
/// fills in the fields it owns (file protection and the viewport's GPU memory).
#[tauri::command]
pub async fn evaluate(app: AppHandle, document: Value, quality: Option<String>, gpu_mb: Option<f64>) -> Result<Value, String> {
    let mut result = call(app.clone(), "evaluate", json!({"document": document, "quality": quality.unwrap_or_else(|| "draft".into())}))
        .await?
        .result;
    let mut contract: DatumPayload = serde_json::from_value(result["contract"].take()).map_err(|e| format!("contract violation: {e}"))?;
    contract.security_forensics.itar_tpm_locked = *app.state::<Protection>().0.lock().unwrap();
    contract.kernel_metrics.gpu_vram_usage_mb = gpu_mb.unwrap_or(0.0);
    contract.validate().map_err(|e| format!("contract violation: {e}"))?;
    result["contract"] = serde_json::to_value(contract).map_err(|e| e.to_string())?;
    Ok(result)
}

/// GPU scene (brick-pruned bytecode) or triangle mesh as a raw ArrayBuffer.
#[tauri::command]
pub async fn engine_binary(app: AppHandle, method: String, params: Option<Value>) -> Result<Response, String> {
    let method: &'static str = match method.as_str() {
        "scene" => "scene",
        "mesh" => "mesh",
        "robot_reach" => "robot_reach",
        other => return Err(format!("not a binary method: {other}")),
    };
    let reply = call(app, method, params.unwrap_or(json!({}))).await?;
    let data = reply.binary.ok_or("engine sent no data")?;
    if method != "robot_reach" {
        return Ok(Response::new(data));
    }
    // statistics travel with the voxels: u32 JSON length | JSON | float32 voxel records
    let meta = reply.result.to_string().into_bytes();
    let mut out = Vec::with_capacity(4 + meta.len() + data.len());
    out.extend_from_slice(&(meta.len() as u32).to_le_bytes());
    out.extend_from_slice(&meta);
    out.extend_from_slice(&data);
    Ok(Response::new(out))
}

#[tauri::command]
pub fn open_document(app: AppHandle, path: String) -> Result<Value, String> {
    let loaded = docfile::load(&path)?;
    *app.state::<Protection>().0.lock().unwrap() = loaded.protected;
    Ok(json!({"document": loaded.document, "protected": loaded.protected, "path": path}))
}

#[tauri::command]
pub fn save_document(app: AppHandle, path: String, document: Value, protect: bool) -> Result<(), String> {
    docfile::save(&path, &document, protect)?;
    *app.state::<Protection>().0.lock().unwrap() = protect;
    Ok(())
}

/// Read a robot description chosen in the open dialog (URDF / XML only, at most 10 MB).
#[tauri::command]
pub fn read_robot_file(path: String) -> Result<String, String> {
    let lower = path.to_ascii_lowercase();
    if !(lower.ends_with(".urdf") || lower.ends_with(".xml")) {
        return Err("only .urdf or .xml robot descriptions can be imported".into());
    }
    let meta = std::fs::metadata(&path).map_err(|e| format!("could not read {path}: {e}"))?;
    if meta.len() > 10 * 1024 * 1024 {
        return Err("robot description is larger than 10 MB".into());
    }
    std::fs::read_to_string(&path).map_err(|e| format!("could not read {path}: {e}"))
}

#[tauri::command]
pub fn new_document(app: AppHandle) {
    *app.state::<Protection>().0.lock().unwrap() = false;
}

#[tauri::command]
pub fn take_launch_file(launch: State<'_, LaunchFile>) -> Option<String> {
    launch.0.lock().unwrap().take()
}
