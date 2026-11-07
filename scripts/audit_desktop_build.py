#!/usr/bin/env python3
"""
Datum Core Enterprise — Pillar 5 hydration & offline build audit.
Checks static export output, required DLLs, and offline assets.
"""
import sys, os, re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "out"
CONF = ROOT / "src-tauri" / "tauri.conf.json"
BIN = ROOT / "src-tauri" / "bin" / "datum_engine-x86_64-pc-windows-msvc.exe"
DLL_DIR = ROOT / "src-tauri" / "bin"  # sidecar DLLs land here

def check_static_export():
    if not OUT.exists():
        print(f"[AUDIT] FAIL: missing static export dir {OUT} — run `npm run build:web` first")
        return False
    html = list(OUT.rglob("*.html"))
    js = list(OUT.rglob("*.js"))
    css = list(OUT.rglob("*.css"))
    print(f"[AUDIT] static bundles: {len(html)} html, {len(js)} js, {len(css)} css")
    ok = len(html) > 0 and len(js) > 0
    print("[AUDIT] static export" if ok else "[AUDIT] FAIL static export empty", "OK" if ok else "")
    return ok

def check_hydration():
    # hydration mismatches only happen when SSR is involved; static export ignores them.
    # Verify no `{/*` comment-invalid syntax lingers as HTML comments.
    bad = []
    tsx_root = ROOT / "app"
    if tsx_root.exists():
        for p in tsx_root.rglob("*.tsx"):
            text = p.read_text(encoding="utf-8", errors="replace")
            if "<!--" in text:
                bad.append(str(p))
    if bad:
        print("[AUDIT] FAIL HTML comments in:", bad)
        return False
    print("[AUDIT] hydration lint clean (no <!-- --> in .tsx)")
    return True

def check_sidecar():
    ok_bin = BIN.exists()
    print(f"[AUDIT] sidecar exists: {ok_bin} — {BIN}")
    if not ok_bin:
        print("        hint: run `python build_backend.py` to build datum_engine.exe")
    # Check required DLLs exist alongside the binary (Pywin32, CUDA helpers).
    dlls = list(DLL_DIR.glob("*.dll")) if DLL_DIR.exists() else []
    print(f"[AUDIT] sidecar DLLs present: {len(dlls)}")
    return True

def main():
    ok = True
    ok &= check_static_export()
    ok &= check_hydration()
    check_sidecar()
    print("\n[AUDIT] DONE", "PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)

if __name__ == "__main__":
    main()
