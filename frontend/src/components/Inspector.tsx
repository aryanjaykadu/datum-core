"use client";

import type { Evaluation } from "@/lib/ipc";
import { fmtLen, type Settings } from "@/lib/settings";

const sci = (v: number) => (Math.abs(v) < 1e-15 ? "0" : Math.abs(v) < 1e-3 || Math.abs(v) >= 1e4 ? v.toExponential(3) : v.toPrecision(4));

function Flag({ label, bad, text }: { label: string; bad: boolean; text: [string, string] }) {
  return (
    <div className="kv">
      <span>{label}</span>
      <span className={bad ? "bad" : "ok"}>{bad ? text[1] : text[0]}</span>
    </div>
  );
}

export default function Inspector({ result, settings }: { result: Evaluation | null; settings: Settings }) {
  if (!result) return <div className="section note">Mass properties and checks appear after the first rebuild.</div>;
  const { contract: c, geometry: g, analysis: a } = result;
  const t = c.telemetry_and_4d.inertia_tensor_current;
  const floor = 1e-9 * Math.max(...t.flat().map(Math.abs));
  const size = g.bbox_max.map((v, i) => v - g.bbox_min[i]);
  const vol = settings.units === "in" ? g.volume_mm3 / 16387.064 : g.volume_mm3 / 1000;
  return (
    <>
      <div className="section mono">
        <h3>Mass properties · {result.material.name}</h3>
        <div className="kv"><span>Mass</span><span>{(c.telemetry_and_4d.mass_kg * 1000).toFixed(2)} g</span></div>
        <div className="kv"><span>Volume</span><span>{vol.toFixed(3)} {settings.units === "in" ? "in³" : "cm³"}</span></div>
        <div className="kv"><span>Surface area</span><span>{(g.area_mm2 / (settings.units === "in" ? 645.16 : 100)).toFixed(2)} {settings.units === "in" ? "in²" : "cm²"}</span></div>
        <div className="kv"><span>Centre of mass</span><span>{g.centroid_mm.map((v) => fmtLen(v, settings, false)).join(", ")}</span></div>
        <div className="kv"><span>Bounding box</span><span>{size.map((v) => fmtLen(v, settings, false)).join(" × ")}</span></div>
        <h3>Inertia tensor about CoM (kg·m²)</h3>
        <div className="tensor">
          {t.flat().map((v, i) => <div key={i}>{sci(Math.abs(v) < floor ? 0 : v)}</div>)}
        </div>
      </div>

      <div className="section mono">
        <h3>Checks</h3>
        <Flag label="Mesh" bad={!g.watertight} text={["watertight", "open edges"]} />
        <Flag label="Hidden cavities" bad={c.security_forensics.steganography_detected} text={["none", "DETECTED"]} />
        {a.findings.filter((f) => f.suspicious).slice(0, 3).map((f, i) => (
          <div key={i} className="kv sub"><span>⌀{f.size_mm.toFixed(2)} at</span><span>{f.centre_mm.map((v) => v.toFixed(1)).join(", ")}</span></div>
        ))}
        {a.findings.some((f) => f.designed) && (
          <div className="kv sub"><span>designed lattice voids</span><span>{a.findings.filter((f) => f.designed).length}</span></div>
        )}
        <Flag label="Device lock" bad={!c.security_forensics.itar_tpm_locked} text={["locked to this device", "not protected"]} />
        <div className="kv"><span>Tool access</span><span>{a.access.covered_pct.toFixed(1)}% · {a.access.setups} setups</span></div>
        <Flag label="Drop test 5 kg / 0.5 m" bad={c.advanced_fea_and_cad.explicit_fracture_detected} text={["no fracture", "fracture"]} />
        <div className="kv sub"><span>deflection / stress</span><span>{c.advanced_fea_and_cad.mbd_max_deflection_mm.toFixed(3)} mm · {a.impact.stress_mpa.toFixed(0)} MPa</span></div>
        <div className="note">Impact is a screening estimate ({a.impact.model}), not FEA.</div>
      </div>

      <div className="section mono">
        <h3>Kernel</h3>
        <div className="kv"><span>DAG features evaluated</span><span>{c.kernel_metrics.dag_nodes_computed}</span></div>
        <div className="kv"><span>Field program</span><span>{g.program_instructions} instr · {g.graph_nodes} nodes</span></div>
        <div className="kv"><span>Mesh</span><span>{g.triangles.toLocaleString()} tris @ {fmtLen(g.cell_mm, settings)}</span></div>
        <div className="kv"><span>Rebuild</span><span>{result.timing_ms.total.toFixed(0)} ms</span></div>
        <div className="kv"><span>GPU scene</span><span>{c.kernel_metrics.gpu_vram_usage_mb.toFixed(2)} MB</span></div>
      </div>
    </>
  );
}
