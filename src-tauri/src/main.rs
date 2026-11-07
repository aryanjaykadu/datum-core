#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod commands;
mod contract;
mod docfile;
mod engine;
mod support;

use std::sync::Mutex;
use tauri::{DragDropEvent, Emitter, Manager, RunEvent, WindowEvent};

const OPENABLE: &[&str] = &["datum"];

fn openable_arg(args: &[String]) -> Option<String> {
    args.iter()
        .skip(1)
        .find(|a| {
            std::path::Path::new(a)
                .extension()
                .and_then(|e| e.to_str())
                .is_some_and(|e| OPENABLE.contains(&e.to_ascii_lowercase().as_str()))
        })
        .cloned()
}

fn main() {
    let launch_file = openable_arg(&std::env::args().collect::<Vec<_>>());

    let app = tauri::Builder::default()
        // double-clicking another .datum while the app runs opens it in this window
        .plugin(tauri_plugin_single_instance::init(|app, args, _cwd| {
            if let Some(path) = openable_arg(&args) {
                let _ = app.emit("file://open", path);
            }
            if let Some(window) = app.get_webview_window("main") {
                let _ = window.unminimize();
                let _ = window.set_focus();
            }
        }))
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_window_state::Builder::default().build())
        .manage(engine::Engine::new())
        .manage(commands::LaunchFile(Mutex::new(launch_file)))
        .manage(commands::Protection(Mutex::new(false)))
        .invoke_handler(tauri::generate_handler![
            commands::engine_call,
            commands::engine_binary,
            commands::evaluate,
            commands::open_document,
            commands::save_document,
            commands::new_document,
            commands::read_robot_file,
            commands::take_launch_file,
            support::autosave_write,
            support::autosave_read,
            support::autosave_clear,
            support::log_line,
            support::open_log_folder,
            support::app_info,
        ])
        .on_window_event(|window, event| match event {
            WindowEvent::DragDrop(DragDropEvent::Drop { paths, .. }) => {
                if let Some(path) = paths.first() {
                    let _ = window.emit("file://open", path.display().to_string());
                }
            }
            WindowEvent::CloseRequested { api, .. } if window.label() == "main" => {
                // the UI asks about unsaved changes, then calls window.destroy()
                api.prevent_close();
                let _ = window.emit("app://close-requested", ());
            }
            _ => {}
        })
        .build(tauri::generate_context!())
        .expect("error while building Datum Core");

    app.run(|handle, event| {
        if let RunEvent::Exit = event {
            support::log("info", "clean exit");
            handle.state::<engine::Engine>().shutdown();
        }
    });
}
