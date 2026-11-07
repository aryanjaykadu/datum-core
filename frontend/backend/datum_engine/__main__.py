"""Entry point for `datum_engine.exe`.

    datum_engine --port 47811 --parent-pid 1234   run the daemon for the Tauri shell
    datum_engine --evaluate part.sldprt           one-shot: print the contract JSON
"""
from __future__ import annotations

import argparse
import ctypes
import json
import os
import sys
import threading
from pathlib import Path


def _watch_parent(pid: int) -> None:
    """Exit (and release owned SolidWorks instances) when the shell process dies."""
    SYNCHRONIZE = 0x00100000
    INFINITE = 0xFFFFFFFF
    kernel32 = ctypes.windll.kernel32
    handle = kernel32.OpenProcess(SYNCHRONIZE, False, pid)
    if not handle:
        return
    kernel32.WaitForSingleObject(handle, INFINITE)
    from .solidworks import kill_owned_instances

    kill_owned_instances()
    os._exit(0)


def main() -> int:
    parser = argparse.ArgumentParser(prog="datum_engine", description="Datum Core Enterprise engine")
    parser.add_argument("--port", type=int, default=47811)
    parser.add_argument("--parent-pid", type=int)
    parser.add_argument("--evaluate", help="evaluate one file and print the contract payload")
    parser.add_argument("--details", action="store_true", help="with --evaluate, print the details document too")
    args = parser.parse_args()

    if args.evaluate:
        from . import events, pipeline

        events.redirect_to_stderr()
        try:
            result = pipeline.evaluate(Path(args.evaluate))
        finally:
            from .solidworks import SESSION

            SESSION.shutdown()  # one-shot mode must not leave a headless SolidWorks behind
        out = {"payload": result.payload.model_dump(), "details": result.details} if args.details else result.payload.model_dump()
        print(json.dumps(out, default=str))
        return 0

    if args.parent_pid and os.name == "nt":
        threading.Thread(target=_watch_parent, args=(args.parent_pid,), daemon=True).start()

    import uvicorn

    from . import events
    from .server import app

    events.emit("ready", port=args.port, pid=os.getpid())
    uvicorn.run(app, host="127.0.0.1", port=args.port, log_level="warning", access_log=False, log_config=None)
    return 0


if __name__ == "__main__":
    sys.exit(main())
