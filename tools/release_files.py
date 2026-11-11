"""Stage a release for GitHub: the signed MSI, its signature and latest.json for auto-update.

    python tools/release_files.py 2.0.0 "One-line notes" OUT_DIR

Run after a signed build (TAURI_SIGNING_PRIVATE_KEY and _PASSWORD set; the key lives outside
the repo). Upload everything in OUT_DIR to the GitHub release tagged v<version>.
"""
import datetime
import json
import shutil
import sys
from pathlib import Path

version, notes, out = sys.argv[1], sys.argv[2], Path(sys.argv[3])
bundle = Path(__file__).resolve().parent.parent / "frontend/src-tauri/target/release/bundle/msi"
msi = bundle / f"Datum Core_{version}_x64_en-US.msi"
sig = msi.with_name(msi.name + ".sig")
if not msi.exists() or not sig.exists():
    sys.exit(f"missing {msi.name} or its .sig: build with the signing key set")
out.mkdir(parents=True, exist_ok=True)
name = f"DatumCore_{version}_x64.msi"
shutil.copy(msi, out / name)
shutil.copy(sig, out / (name + ".sig"))
latest = {
    "version": version,
    "notes": notes,
    "pub_date": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    "platforms": {"windows-x86_64": {
        "signature": sig.read_text(encoding="utf8").strip(),
        "url": f"https://github.com/aryanjaykadu/datum-core/releases/download/v{version}/{name}",
    }},
}
(out / "latest.json").write_text(json.dumps(latest, indent=2), encoding="utf8")
print("staged", ", ".join(p.name for p in sorted(out.iterdir())))
