"""Event stream to the Rust shell.

stdout is reserved for newline-delimited JSON events; the shell reads it line by
line and re-emits each one to the webview as `engine://event`. Request/response
traffic goes over the loopback HTTP API instead.
"""
from __future__ import annotations

import json
import sys
import threading
import time

_lock = threading.Lock()
_stream = sys.stdout


def redirect_to_stderr() -> None:
    """One-shot CLI mode prints the result on stdout, so events move to stderr."""
    global _stream
    _stream = sys.stderr


def emit(kind: str, **fields) -> None:
    record = {"type": kind, "t": round(time.time(), 3), **fields}
    line = json.dumps(record, separators=(",", ":"), default=str)
    with _lock:
        _stream.write(line + "\n")
        _stream.flush()


def log(message: str, level: str = "info", stage: str | None = None) -> None:
    emit("log", level=level, msg=message, stage=stage)


def progress(job_id: str, stage: str, fraction: float) -> None:
    emit("progress", job=job_id, stage=stage, fraction=round(max(0.0, min(1.0, fraction)), 3))
