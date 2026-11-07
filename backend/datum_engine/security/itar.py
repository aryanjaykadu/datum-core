"""ITAR / TPM lock status.

`itar_tpm_locked` is true only when both hold:
1. the deployment policy marks this workstation's data as ITAR-controlled
   (`%PROGRAMDATA%\\DatumCore\\policy.json` → {"itar_controlled": true}), and
2. Windows reports a TPM that is present, enabled and activated.

This reports posture. It does not encrypt or seal anything by itself; data at
rest protection is expected from BitLocker under the same TPM.
"""
from __future__ import annotations

import json
import os
import subprocess
from functools import lru_cache
from pathlib import Path

POLICY = Path(os.environ.get("PROGRAMDATA", r"C:\ProgramData")) / "DatumCore" / "policy.json"


def itar_controlled() -> bool:
    try:
        return bool(json.loads(POLICY.read_text()).get("itar_controlled", False))
    except Exception:
        return False


@lru_cache(maxsize=1)
def tpm_ready() -> bool:
    script = "$t = Get-CimInstance -Namespace root/cimv2/Security/MicrosoftTpm -ClassName Win32_Tpm -ErrorAction Stop; [int]($t.IsEnabled_InitialValue -and $t.IsActivated_InitialValue)"
    try:
        out = subprocess.run(
            ["powershell", "-NoProfile", "-NonInteractive", "-Command", script],
            capture_output=True, text=True, timeout=15, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        return out.stdout.strip() == "1"
    except Exception:
        return False  # querying Win32_Tpm usually needs elevation; unknown counts as not locked


def status() -> dict:
    controlled = itar_controlled()
    ready = tpm_ready() if controlled else False
    return {"itar_controlled": controlled, "tpm_ready": ready, "locked": controlled and ready, "policy_file": str(POLICY)}
