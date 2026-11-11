"use client";

import { useEffect, useState } from "react";
import { click, reject, validate } from "@/lib/audio";
import type { PickResult } from "@/lib/gpu";
import type { DatumDocument, Evaluation, StressResult, Study, Vec3 } from "@/lib/ipc";
import { kernel } from "@/lib/ipc";
import Icon from "./Icon";

type Placing = null | "fixture" | "load";
const fmtMass = (kg: number) => (kg >= 1 ? `${kg.toFixed(2)} kg` : `${(kg * 1000).toFixed(1)} g`);
const DIRECTIONS = [
  { key: "into", label: "Into the surface" },
  { key: "-z", label: "Down (−Z)" },
  { key: "+z", label: "Up (+Z)" },
  { key: "+x", label: "+X" }, { key: "-x", label: "−X" }, { key: "+y", label: "+Y" }, { key: "-y", label: "−Y" },
] as const;

/**
 * Strength study, in three steps: where the part is held, where it is pushed, then solve.
 * Results colour the part by stress and give a safety factor against the material's yield.
 */
export default function StrengthPanel({ doc, result, onDocChange, setPickHandler, onStress, onLog, onLighten, onChecked }: {
  doc: DatumDocument;
  result: Evaluation | null;
  onDocChange: (patch: Partial<DatumDocument>) => void;
  /** While set, a click on the part sends the hit here instead of selecting a feature. */
  setPickHandler: (h: ((p: PickResult) => void) | null) => void;
  /** Per-vertex stress for the viewport (null to clear) and the colour-scale maximum. */
  onStress: (values: Float32Array | null, scaleMpa: number) => void;
  onLog: (level: "info" | "warn" | "error", msg: string) => void;
  /** Add a stress-graded lattice that removes `target` % of the mass. */
  onLighten: (target: number) => void;
  /** Safety factor of the latest study (null when stale). */
  onChecked: (sf: number | null) => void;
}) {
  const study: Study = doc.study ?? { fixtures: [], loads: [] };
  const [placing, setPlacing] = useState<Placing>(null);
  const [force, setForce] = useState(100);
  const [dir, setDir] = useState<(typeof DIRECTIONS)[number]["key"]>("into");
  const [running, setRunning] = useState(false);
  const [res, setRes] = useState<StressResult | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [target, setTarget] = useState(40);
  // before/after comparison across the rebuild that adds the lattice
  const [before, setBefore] = useState<{ mass: number; sf: number; stress: number } | null>(null);
  const [compare, setCompare] = useState<"waiting" | "ready" | null>(null);
  const lightened = doc.features.some((f) => f.type === "lighten" && !f.suppressed);
  const size = result ? Math.max(...result.geometry.bbox_max.map((v, i) => v - result.geometry.bbox_min[i])) : 100;
  const radius = Math.max(1.5, size * 0.05);

  // results go stale when the part or the study changes
  const key = JSON.stringify([study, result?.geometry.volume_mm3]);
  useEffect(() => {
    setRes(null);
    onStress(null, 1);
    onChecked(null);
    // the lattice just rebuilt: check the lighter part straight away
    if (compare === "waiting" && result) {
      setCompare("ready");
      void run();
    }
  }, [key]); // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => {
    if (!placing) {
      setPickHandler(null);
      return;  // effects may only return a cleanup function or nothing
    }
    setPickHandler((hit) => {
      const centre = hit.position.map((v) => +v.toFixed(3)) as Vec3;
      if (placing === "fixture") {
        onDocChange({ study: { ...study, fixtures: [...study.fixtures, { center: centre, radius }] } });
      } else {
        const n = hit.normal;
        const unit: Vec3 = dir === "into" ? [-n[0], -n[1], -n[2]] : ({ "-z": [0, 0, -1], "+z": [0, 0, 1], "+x": [1, 0, 0], "-x": [-1, 0, 0], "+y": [0, 1, 0], "-y": [0, -1, 0] } as const)[dir] as Vec3;
        const f = unit.map((v) => +(v * force).toFixed(3)) as Vec3;
        onDocChange({ study: { ...study, loads: [...study.loads, { center: centre, radius, force: f }] } });
      }
      click(placing === "fixture" ? 196 : 262, 0.45);
    });
    return () => setPickHandler(null);
  }, [placing, study, force, dir, radius]); // eslint-disable-line react-hooks/exhaustive-deps

  const run = async () => {
    setPlacing(null);
    setRunning(true);
    setError(null);
    try {
      const r = await kernel.stress(study);
      setRes(r.stats);
      onChecked(r.stats.safety_factor);
      onStress(r.vertexStress, Math.max(r.stats.p99_mpa, 1e-6));
      onLog("info", `stress study: ${r.stats.voxels.toLocaleString()} voxels of ${r.stats.voxel_mm.toFixed(2)} mm, ${r.stats.iterations} iterations, ${r.stats.seconds.toFixed(1)} s`);
      if (r.stats.safety_factor >= 1.5) validate(); else reject();
    } catch (e) {
      setError(String(e).replace(/^Error: /, ""));
      onLog("error", String(e));
    } finally {
      setRunning(false);
    }
  };

  if (!result) return <div className="section note">Add or import a part first. Then mark where it is held and where it is pushed.</div>;
  const sf = res?.safety_factor ?? 0;
  const verdict = !res ? null : sf >= 2 ? ["good", "Strong enough", "Comfortable margin for a printed part."] : sf >= 1.2 ? ["warn", "Borderline", "It holds, but with little margin. Add material where it is red."] : ["bad", "Too weak", "It would yield. Thicken the red areas or reduce the load."];

  return (
    <div className="strength">
      <ol className="steps">
        <li className={study.fixtures.length ? "done" : "now"}>
          <header><span className="step-n">1</span><b>Where is it held?</b></header>
          <p>Click on the part where it is bolted or clamped.</p>
          <div className="step-row">
            <button className={`btn${placing === "fixture" ? " primary" : ""}`} onClick={() => setPlacing(placing === "fixture" ? null : "fixture")}>
              <Icon name="plus" size={14} />{placing === "fixture" ? "Click the part… (done)" : "Add a fixture"}
            </button>
            {study.fixtures.length > 0 && <span className="chip">{study.fixtures.length} fixture{study.fixtures.length > 1 ? "s" : ""}</span>}
            {study.fixtures.length > 0 && <button className="icon" title="Remove fixtures" onClick={() => onDocChange({ study: { ...study, fixtures: [] } })}>×</button>}
          </div>
        </li>
        <li className={study.loads.length ? "done" : study.fixtures.length ? "now" : ""}>
          <header><span className="step-n">2</span><b>Where is it pushed?</b></header>
          <p>Choose a force, then click where it acts.</p>
          <div className="step-row">
            <span className="field"><input className="mono" value={force} inputMode="decimal" onChange={(e) => setForce(Number(e.target.value) || 0)} /><em>N</em></span>
            <select value={dir} onChange={(e) => setDir(e.target.value as typeof dir)}>
              {DIRECTIONS.map((d) => <option key={d.key} value={d.key}>{d.label}</option>)}
            </select>
          </div>
          <div className="step-row">
            <button className={`btn${placing === "load" ? " primary" : ""}`} onClick={() => setPlacing(placing === "load" ? null : "load")}>
              <Icon name="plus" size={14} />{placing === "load" ? "Click the part… (done)" : "Add a load"}
            </button>
            {study.loads.length > 0 && <span className="chip">{study.loads.length} load{study.loads.length > 1 ? "s" : ""} · {Math.round(study.loads.reduce((a, l) => a + Math.hypot(...l.force), 0))} N</span>}
            {study.loads.length > 0 && <button className="icon" title="Remove loads" onClick={() => onDocChange({ study: { ...study, loads: [] } })}>×</button>}
          </div>
          <small className="hint">≈ {(force / 9.81).toFixed(1)} kg hanging weight</small>
        </li>
        <li className={res ? "done" : study.fixtures.length && study.loads.length ? "now" : ""}>
          <header><span className="step-n">3</span><b>Check the strength</b></header>
          <button className="btn primary wide" disabled={running || !study.fixtures.length || !study.loads.length} onClick={() => void run()}>
            <Icon name="play" size={14} />{running ? "Solving…" : "Run stress analysis"}
          </button>
        </li>
      </ol>

      {error && <div className="note bad pad">{error}</div>}
      {res && verdict && (
        <>
          {compare === "ready" && before && result && (
            <div className="compare">
              <div><small>Before</small><b>{fmtMass(before.mass)}</b><span>safety {before.sf.toFixed(2)}</span></div>
              <Icon name="chevron" size={20} />
              <div className="after"><small>After</small><b>{fmtMass(result.contract.telemetry_and_4d.mass_kg)}</b>
                <span>safety {sf.toFixed(2)} · {Math.round((1 - result.contract.telemetry_and_4d.mass_kg / before.mass) * 100)} % lighter</span></div>
            </div>
          )}
          <div className={`verdict ${verdict[0]}`}>
            <div><small>Safety factor</small><b>{sf >= 100 ? "100+" : sf.toFixed(sf < 10 ? 2 : 0)}</b></div>
            <div><b className="v-title">{verdict[1]}</b><span>{verdict[2]}</span></div>
          </div>
          <div className="stat-tiles">
            <div className="stat"><small>Peak stress</small><b>{res.p99_mpa.toFixed(res.p99_mpa < 10 ? 2 : 1)}</b><span>MPa · yield {res.yield_mpa}</span></div>
            <div className="stat"><small>Max deflection</small><b>{res.max_displacement_mm < 0.1 ? res.max_displacement_mm.toFixed(3) : res.max_displacement_mm.toFixed(2)}</b><span>mm</span></div>
          </div>
          <div className="stress-legend"><span>0</span><i /><span>{res.p99_mpa.toFixed(1)} MPa</span></div>
          {!lightened && (
            <div className="lighten-card">
              <header><span className="tool-icon"><Icon name="gyroid_infill" size={22} /></span><div><b>Make it lighter</b>
                <span>Hollow the part and fill it with a lattice that is dense where it is red, sparse where it is blue.</span></div></header>
              <div className="target-row">
                {[20, 30, 40, 50, 60].map((t) => (
                  <button key={t} aria-pressed={target === t} onClick={() => setTarget(t)}>−{t} %</button>
                ))}
              </div>
              <button className="btn primary wide" disabled={running} onClick={() => {
                setBefore({ mass: result.contract.telemetry_and_4d.mass_kg, sf: res.safety_factor, stress: res.p99_mpa });
                setCompare("waiting");
                click(330, 0.5);
                onLighten(target);
              }}><Icon name="sparkle" size={14} />Make it {target} % lighter</button>
            </div>
          )}
          <p className="note small pad">
            {res.material} · {res.voxels.toLocaleString()} voxels of {res.voxel_mm.toFixed(2)} mm · solved in {res.seconds.toFixed(1)} s{res.converged ? "" : " (not fully converged)"}.
            Peak is the 99th percentile: stress right at a clamp or load point is a modelling artefact. Linear elastic; a guide, not a certification.
          </p>
        </>
      )}
    </div>
  );
}
