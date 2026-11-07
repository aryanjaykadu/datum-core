"""XR work-instruction package: GLB model + manifest of anchored annotations.

The package is a folder with `model.glb`, `manifest.json` and an `index.html`
that renders it with <model-viewer> and its hotspots. Android Chrome opens it
in WebXR AR. iPadOS Safari does not implement WebXR immersive-ar; there
<model-viewer> falls back to AR Quick Look, which needs a USDZ export
(`model.usdz`, produced from the GLB with Apple's Reality Converter) placed in
the same folder. Serve the folder over HTTPS on the shop network to cast it.
"""
from __future__ import annotations

import html
import json
from pathlib import Path

import trimesh

MODEL_VIEWER_URL = "https://cdn.jsdelivr.net/npm/@google/model-viewer@3.5.0/dist/model-viewer.min.js"


def build_package(mesh: trimesh.Trimesh, out_dir: Path, system_id: str, title: str, annotations: list[dict]) -> dict:
    """annotations: [{"label", "position_m": [x,y,z], "normal": [x,y,z], "torque_nm"?, "step"?}]"""
    out_dir.mkdir(parents=True, exist_ok=True)
    mesh.export(out_dir / "model.glb")
    manifest = {
        "schema": "datum.xr-manifest/1",
        "system_id": system_id,
        "title": title,
        "model": "model.glb",
        "ios_model": "model.usdz",
        "units": "m",
        "annotations": [
            {
                "id": f"a{i}",
                "step": a.get("step", i + 1),
                "label": a["label"],
                "position": a["position_m"],
                "normal": a.get("normal", [0, 0, 1]),
                "torque_nm": a.get("torque_nm"),
            }
            for i, a in enumerate(annotations)
        ],
    }
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2))

    hotspots = "\n".join(
        f'<button class="hs" slot="hotspot-{a["id"]}" data-position="{" ".join(map(str, a["position"]))}" '
        f'data-normal="{" ".join(map(str, a["normal"]))}">{a["step"]}. {html.escape(a["label"])}'
        + (f' · {a["torque_nm"]} N·m' if a["torque_nm"] is not None else "")
        + "</button>"
        for a in manifest["annotations"]
    )
    (out_dir / "index.html").write_text(
        f"""<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{html.escape(title)}</title><script type="module" src="{MODEL_VIEWER_URL}"></script>
<style>body{{margin:0;background:#0B0C10}}model-viewer{{width:100vw;height:100vh}}
.hs{{background:rgba(20,22,28,.72);backdrop-filter:blur(16px);color:#EDEDED;border:1px solid #333;border-radius:8px;padding:4px 8px;font:12px Inter,system-ui}}</style>
</head><body><model-viewer src="model.glb" ios-src="model.usdz" ar ar-modes="webxr quick-look" camera-controls>
{hotspots}
</model-viewer></body></html>"""
    )
    return {"path": str(out_dir), "annotations": len(manifest["annotations"])}
