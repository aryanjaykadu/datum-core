//! Sidecar lifecycle for `dk_engine.exe`, the native Datum kernel.
//!
//! Protocol: one JSON request per line on stdin; one JSON response line on stdout,
//! optionally followed by `binary` raw bytes (meshes, GPU scenes). Requests are
//! serialised through a mutex, so the pipe never interleaves two conversations.
//! The engine sits in a Job Object with KILL_ON_JOB_CLOSE, so it dies with the shell.

use serde_json::{json, Value};
use std::io::{BufRead, BufReader, Read, Write};
use std::path::PathBuf;
use std::process::{Child, ChildStdin, ChildStdout, Command, Stdio};
use std::sync::Mutex;
use tauri::{AppHandle, Emitter, Manager};

#[cfg(windows)]
use std::os::windows::process::CommandExt;

const CREATE_NO_WINDOW: u32 = 0x0800_0000;

struct Pipe {
    child: Child,
    stdin: ChildStdin,
    stdout: BufReader<ChildStdout>,
    next_id: u64,
    #[cfg(windows)]
    _job: Option<job::Job>,
}

pub struct Engine(Mutex<Option<Pipe>>);

pub struct Reply {
    pub result: Value,
    pub binary: Option<Vec<u8>>,
}

impl Engine {
    pub fn new() -> Self {
        Self(Mutex::new(None))
    }

    fn start(app: &AppHandle) -> Result<Pipe, String> {
        let exe = engine_path(app).ok_or("dk_engine.exe was not found next to the application")?;
        let mut cmd = Command::new(&exe);
        cmd.stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::null());
        #[cfg(windows)]
        cmd.creation_flags(CREATE_NO_WINDOW);
        let mut child = cmd.spawn().map_err(|e| format!("could not start {}: {e}", exe.display()))?;
        #[cfg(windows)]
        let job = job::Job::assign(&child);
        let stdin = child.stdin.take().ok_or("engine stdin unavailable")?;
        let stdout = BufReader::new(child.stdout.take().ok_or("engine stdout unavailable")?);
        let _ = app.emit("engine://event", json!({"type": "log", "level": "info", "msg": format!("kernel engine started (pid {})", child.id())}));
        Ok(Pipe { child, stdin, stdout, next_id: 0, #[cfg(windows)] _job: job })
    }

    /// Blocking call; run it from `spawn_blocking`.
    pub fn call(&self, app: &AppHandle, method: &str, params: Value) -> Result<Reply, String> {
        let mut guard = self.0.lock().map_err(|_| "engine lock poisoned")?;
        // (re)start lazily, and once more if the previous process died
        for attempt in 0..2 {
            if guard.is_none() {
                *guard = Some(Self::start(app)?);
            }
            let outcome = Self::exchange(guard.as_mut().unwrap(), method, &params);
            match outcome {
                Ok(reply) => return reply,
                Err(io) => {
                    if let Some(mut dead) = guard.take() {
                        let _ = dead.child.kill();
                    }
                    let _ = app.emit("engine://event", json!({"type": "log", "level": "warn", "msg": format!("engine restarted after: {io}")}));
                    if attempt == 1 {
                        return Err(format!("kernel engine failed: {io}"));
                    }
                }
            }
        }
        unreachable!()
    }

    /// Outer Err = transport failure (restart); inner Err = the engine reported an error.
    fn exchange(pipe: &mut Pipe, method: &str, params: &Value) -> std::io::Result<Result<Reply, String>> {
        pipe.next_id += 1;
        let id = pipe.next_id;
        let line = json!({"id": id, "method": method, "params": params}).to_string();
        pipe.stdin.write_all(line.as_bytes())?;
        pipe.stdin.write_all(b"\n")?;
        pipe.stdin.flush()?;
        let mut head = String::new();
        if pipe.stdout.read_line(&mut head)? == 0 {
            return Err(std::io::Error::new(std::io::ErrorKind::UnexpectedEof, "engine closed its output"));
        }
        let head: Value = serde_json::from_str(&head).map_err(|e| std::io::Error::new(std::io::ErrorKind::InvalidData, e))?;
        let binary = match head.get("binary").and_then(Value::as_u64) {
            Some(n) => {
                let mut buf = vec![0u8; n as usize];
                pipe.stdout.read_exact(&mut buf)?;
                Some(buf)
            }
            None => None,
        };
        if head.get("id").and_then(Value::as_u64) != Some(id) {
            return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "engine response out of order"));
        }
        if let Some(err) = head.get("error") {
            return Ok(Err(err.as_str().unwrap_or("engine error").to_string()));
        }
        Ok(Ok(Reply { result: head.get("result").cloned().unwrap_or(Value::Null), binary }))
    }

    pub fn shutdown(&self) {
        if let Ok(mut guard) = self.0.lock() {
            if let Some(mut pipe) = guard.take() {
                let _ = pipe.child.kill();
                let _ = pipe.child.wait();
            }
        }
    }
}

fn engine_path(app: &AppHandle) -> Option<PathBuf> {
    if let Some(dev) = std::env::var_os("DATUM_KERNEL_ENGINE") {
        return Some(PathBuf::from(dev));
    }
    let exe_dir = std::env::current_exe().ok()?.parent()?.to_path_buf();
    [exe_dir.join("dk_engine.exe"), app.path().resource_dir().ok()?.join("dk_engine.exe")]
        .into_iter()
        .find(|p| p.exists())
}

#[cfg(windows)]
mod job {
    use std::os::windows::io::AsRawHandle;
    use std::process::Child;
    use windows_sys::Win32::Foundation::{CloseHandle, HANDLE};
    use windows_sys::Win32::System::JobObjects::{
        AssignProcessToJobObject, CreateJobObjectW, JobObjectExtendedLimitInformation, SetInformationJobObject,
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION, JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE,
    };

    pub struct Job(isize);
    unsafe impl Send for Job {}

    impl Job {
        pub fn assign(child: &Child) -> Option<Job> {
            unsafe {
                let job = CreateJobObjectW(std::ptr::null(), std::ptr::null());
                if job.is_null() {
                    return None;
                }
                let mut info: JOBOBJECT_EXTENDED_LIMIT_INFORMATION = std::mem::zeroed();
                info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                let ok = SetInformationJobObject(
                    job,
                    JobObjectExtendedLimitInformation,
                    &info as *const _ as *const core::ffi::c_void,
                    std::mem::size_of::<JOBOBJECT_EXTENDED_LIMIT_INFORMATION>() as u32,
                );
                if ok == 0 || AssignProcessToJobObject(job, child.as_raw_handle() as HANDLE) == 0 {
                    CloseHandle(job);
                    return None;
                }
                Some(Job(job as isize))
            }
        }
    }

    impl Drop for Job {
        fn drop(&mut self) {
            unsafe {
                CloseHandle(self.0 as HANDLE);
            }
        }
    }
}
