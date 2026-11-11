//! Product support: autosave / crash recovery, the error log, and app info.
//!
//! Everything lives under %LOCALAPPDATA%\DatumCore:
//!   autosave.datum   the open design, written every few seconds while it has unsaved changes
//!   logs\datum.log   errors and warnings from the UI and the shell (rotated at 1 MB)

use serde_json::{json, Value};
use std::io::Write;
use std::path::PathBuf;

pub fn data_dir() -> PathBuf {
    let base = std::env::var_os("LOCALAPPDATA").map(PathBuf::from).unwrap_or_else(std::env::temp_dir);
    let dir = base.join("DatumCore");
    let _ = std::fs::create_dir_all(dir.join("logs"));
    dir
}
fn autosave_path() -> PathBuf {
    data_dir().join("autosave.datum")
}
fn log_path() -> PathBuf {
    data_dir().join("logs").join("datum.log")
}

pub fn log(level: &str, message: &str) {
    let path = log_path();
    if std::fs::metadata(&path).map(|m| m.len() > 1_000_000).unwrap_or(false) {
        let _ = std::fs::rename(&path, path.with_extension("log.1"));
    }
    if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(&path) {
        let secs = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0);
        let _ = writeln!(f, "{secs} [{level}] {}", message.replace('\n', " | "));
    }
}

/// Write the current design for crash recovery (plain JSON; the source path is kept so the
/// recovered design can be saved back where it came from).
#[tauri::command]
pub fn autosave_write(document: Value, path: Option<String>) -> Result<(), String> {
    let body = json!({"saved_at": std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0),
                      "path": path, "document": document});
    let tmp = autosave_path().with_extension("tmp");
    std::fs::write(&tmp, body.to_string()).map_err(|e| e.to_string())?;
    std::fs::rename(&tmp, autosave_path()).map_err(|e| e.to_string())
}

/// A recovery snapshot left by a session that did not close cleanly, if any.
#[tauri::command]
pub fn autosave_read() -> Option<Value> {
    let text = std::fs::read_to_string(autosave_path()).ok()?;
    serde_json::from_str(&text).ok()
}

#[tauri::command]
pub fn autosave_clear() {
    let _ = std::fs::remove_file(autosave_path());
}

#[tauri::command]
pub fn log_line(level: String, message: String) {
    log(&level, &message);
}

/// Open a page of this project on GitHub (feedback, releases) in the default browser.
/// Anything else is refused, so a compromised page cannot launch arbitrary URLs.
#[tauri::command]
pub fn open_url(url: String) -> Result<(), String> {
    const ALLOWED: &str = "https://github.com/aryanjaykadu/datum-core/";
    if !url.starts_with(ALLOWED) || url.chars().any(|c| c == '"' || c.is_control()) {
        return Err("only Datum Core's GitHub pages can be opened".into());
    }
    // rundll32 hands the URL to the default browser without a shell parsing it
    std::process::Command::new("rundll32").args(["url.dll,FileProtocolHandler", &url]).spawn().map(|_| ()).map_err(|e| e.to_string())
}

#[tauri::command]
pub fn open_log_folder() -> Result<(), String> {
    std::process::Command::new("explorer").arg(data_dir().join("logs")).spawn().map(|_| ()).map_err(|e| e.to_string())
}

#[tauri::command]
pub fn app_info(app: tauri::AppHandle) -> Value {
    json!({
        "name": app.package_info().name,
        "version": app.package_info().version.to_string(),
        "tauri": tauri::VERSION,
        "data_dir": data_dir(),
    })
}
