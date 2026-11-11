"use client";

import { useEffect, useState } from "react";
import { click, validate } from "@/lib/audio";
import type { Viewport } from "@/lib/gpu";
import { kernel, pickRobotFile, readRobotFile, type Catalog, type DatumDocument, type Feature, type ReachStats, type Vec3 } from "@/lib/ipc";
import { newDhRobot, parseUrdf, type RobotDef } from "@/lib/urdf";

const DH_COLUMNS: [string, string][] = [["d", "d mm"], ["a", "a mm"], ["alpha_deg", "α°"], ["lo_deg", "min°"], ["hi_deg", "max°"]];

/**
 * Robotics workspace module: choose or define an arm (presets, DH table, URDF import), pose
 * it joint by joint, then compute its reachable envelope and kinematic lock zones.
 */
export default function RoboticsPanel({ doc, catalog, viewport, onPatch, onDocChange, onInsert, onLog, setPickHandler }: {
  doc: DatumDocument;
  catalog: Catalog | null;
  viewport: Viewport | null;
  onPatch: (id: string, patch: Partial<Feature>) => void;
  onDocChange: (patch: Partial<DatumDocument>) => void;
  onInsert: (type: string) => void;
  onLog: (level: "info" | "warn" | "error", msg: string) => void;
  /** While set, a click on the part sends its point here instead of selecting a feature. */
  setPickHandler: (h: ((p: Vec3) => void) | null) => void;
}) {
  const arms = doc.features.filter((f) => f.type === "robot_arm" && !f.suppressed);
  const [armId, setArmId] = useState<string | null>(null);
  const arm = arms.find((a) => a.id === armId) ?? arms[0];
  const [samples, setSamples] = useState(150000);
  const [threshold, setThreshold] = useState(0.05);
  const [stats, setStats] = useState<ReachStats | null>(null);
  const [busy, setBusy] = useState(false);
  const [visible, setVisible] = useState(true);
  const [mode, setMode] = useState<0 | 1>(0);
  const [collisions, setCollisions] = useState(true);
  const [floor, setFloor] = useState(true);
  const [editTable, setEditTable] = useState(false);
  const [tcp, setTcp] = useState<{ tool: Vec3; dexterity: number } | null>(null);
  const [jog, setJog] = useState(20);
  const [targeting, setTargeting] = useState(false);
  const [ikNote, setIkNote] = useState<string | null>(null);

  useEffect(() => {
    if (!viewport) return;
    viewport.cloud.visible = visible;
    viewport.cloud.mode = mode;
    viewport.request();
  }, [viewport, visible, mode]);

  const preset = String(arm?.params?.preset ?? "ur5");
  const custom: RobotDef | undefined = doc.robots?.[preset];
  const center = (arm?.params?.center as Vec3 | undefined) ?? [0, 0, 0];
  const definitionKey = JSON.stringify(custom ?? preset);
  useEffect(() => {
    setStats(null);
    viewport?.setCloud(null);
  }, [definitionKey, center[0], center[1], center[2], arm?.id]); // eslint-disable-line react-hooks/exhaustive-deps

  // live tool-centre-point readout for the current pose
  const poseKey = JSON.stringify([arm?.params?.joints_a, arm?.params?.joints_b, arm?.params?.joints_more, definitionKey]);
  useEffect(() => {
    if (!arm) return;
    const qs = [...((arm.params?.joints_a as number[]) ?? [0, -90, 0]), ...((arm.params?.joints_b as number[]) ?? [-90, 0, 0]), ...((arm.params?.joints_more as number[]) ?? [])];
    let live = true;
    kernel.robotFk(preset, qs, custom).then((r) => live && setTcp({ tool: r.tool.map((v, i) => v + center[i]) as Vec3, dexterity: r.dexterity })).catch(() => live && setTcp(null));
    return () => {
      live = false;
    };
  }, [poseKey]); // eslint-disable-line react-hooks/exhaustive-deps

  if (!catalog) return null;

  // add a robot definition and point the current arm at it, as ONE document change
  const addRobot = (def: RobotDef, pose: number[]) => {
    const base = def.name.toLowerCase().replace(/[^a-z0-9]+/g, "_").replace(/^_|_$/g, "") || "arm";
    let key = `custom_${base}`;
    for (let i = 2; doc.robots?.[key] || catalog.robots.some((r) => r.key === key); i++) key = `custom_${base}_${i}`;
    const padded = [...pose, 0, 0, 0, 0, 0, 0];
    const features = doc.features.map((f) => f.id !== arm?.id ? f : {
      ...f, params: { ...f.params, preset: key, joints_a: padded.slice(0, 3), joints_b: padded.slice(3, 6), joints_more: pose.slice(6) },
    });
    onDocChange({ robots: { ...(doc.robots ?? {}), [key]: def }, features });
    return key;
  };
  const importUrdf = async () => {
    const path = await pickRobotFile();
    if (!path) return;
    try {
      const { robot, notes } = parseUrdf(await readRobotFile(path));
      addRobot(robot, robot.joints.map(() => 0));
      notes.forEach((n) => onLog("info", n));
      click(262);
    } catch (e) {
      onLog("error", `URDF import failed: ${e instanceof Error ? e.message : e}`);
    }
  };
  const newTable = () => {
    addRobot(newDhRobot(), [0, 45, -45]);
    setEditTable(true);
  };

  if (!arm)
    return (
      <div className="section">
        <h3>Robotics module</h3>
        <p className="note">Add a robot arm to analyse its reachable workspace, kinematic singularities and self-collisions.</p>
        <button className="btn primary wide" onClick={() => onInsert("robot_arm")}>Insert robot arm</button>
      </div>
    );

  // joint count and limits come from the preset or the custom definition
  const presetInfo = catalog.robots.find((r) => r.key === preset);
  const limits: [number, number][] = custom
    ? custom.joints.map((j) => [Number(j.lo_deg ?? -180), Number(j.hi_deg ?? 180)])
    : (presetInfo ?? catalog.robots[0]).limits_deg;
  const dof = limits.length;
  const qa = (arm.params?.joints_a as number[]) ?? [0, -90, 0];
  const qb = (arm.params?.joints_b as number[]) ?? [-90, 0, 0];
  const more = (arm.params?.joints_more as number[]) ?? [];
  const all = [...qa, ...qb, ...more];
  const q = Array.from({ length: dof }, (_, i) => all[i] ?? 0);
  const setJoint = (i: number, v: number) => {
    const next = [...q];
    next[i] = v;
    const padded = [...next, 0, 0, 0, 0, 0, 0];
    onPatch(arm.id, { params: { ...arm.params, joints_a: padded.slice(0, 3), joints_b: padded.slice(3, 6), joints_more: next.slice(6) } });
  };
  const setAll = (next: number[]) => {
    const padded = [...next, 0, 0, 0, 0, 0, 0];
    onPatch(arm.id, { params: { ...arm.params, joints_a: padded.slice(0, 3), joints_b: padded.slice(3, 6), joints_more: next.slice(6) } });
  };
  /** Inverse kinematics: move the tool flange to a world point (mm), starting from the current pose. */
  const moveTool = async (target: Vec3) => {
    try {
      const r = await kernel.robotIk(preset, q, target, center, custom);
      setAll(r.q_deg.map((v) => Math.round(v * 10) / 10));
      setIkNote(r.reached ? null : `Out of reach: stopped ${r.error_mm.toFixed(1)} mm short of the target.`);
      if (r.reached) click(294, 0.35);
    } catch (e) {
      onLog("error", String(e));
    }
  };
  const jogBy = (axis: number, sign: number) => {
    if (!tcp) return;
    const t = [...tcp.tool] as Vec3;
    t[axis] += sign * jog;
    void moveTool(t);
  };
  const setCustom = (def: RobotDef) => onDocChange({ robots: { ...(doc.robots ?? {}), [preset]: def } });

  const compute = async () => {
    setBusy(true);
    click(196);
    try {
      const r = await kernel.robotReach(preset, center, samples, threshold, collisions, floor, custom);
      setStats(r.stats);
      viewport?.setCloud(r.voxels, r.stats.voxel_mm, r.stats.threshold);
      const R = r.stats.reach_mm;
      viewport?.fitBox([center[0] - R, center[1] - R, center[2] - R * 0.6], [center[0] + R, center[1] + R, center[2] + R]);
      onLog("info", `reach envelope: ${r.stats.voxels.toLocaleString()} voxels, ${r.stats.singular_pct.toFixed(1)}% lock zones, ${((100 * r.stats.colliding_samples) / r.stats.samples).toFixed(1)}% poses rejected for collision, ${r.stats.ms.toFixed(0)} ms`);
      validate();
    } catch (e) {
      onLog("error", String(e));
    } finally {
      setBusy(false);
    }
  };

  return (
    <>
      <div className="section">
        <h3>Robotics module</h3>
        {arms.length > 1 && (
          <label className="prop">
            <span>Arm</span>
            <select value={arm.id} onChange={(e) => setArmId(e.target.value)}>
              {arms.map((a) => <option key={a.id} value={a.id}>{a.name ?? a.id}</option>)}
            </select>
          </label>
        )}
        <label className="prop">
          <span>Model</span>
          <select value={preset} onChange={(e) => onPatch(arm.id, { params: { ...arm.params, preset: e.target.value } })}>
            <optgroup label="Built-in">
              {catalog.robots.map((r) => <option key={r.key} value={r.key}>{r.name}</option>)}
            </optgroup>
            {doc.robots && Object.keys(doc.robots).length > 0 && (
              <optgroup label="This document">
                {Object.entries(doc.robots).map(([k, r]) => <option key={k} value={k}>{r.name}</option>)}
              </optgroup>
            )}
          </select>
        </label>
        <div className="note">{dof} DOF · {custom ? `custom ${custom.format === "dh" ? "DH table" : "URDF chain"}` : presetInfo ? `reach bound ${presetInfo.reach_mm.toFixed(0)} mm` : "unknown model"}</div>
        <div className="row-actions">
          <button className="btn" onClick={importUrdf}>Import URDF…</button>
          <button className="btn" onClick={newTable}>New DH arm</button>
          {custom?.format === "dh" && <button className="btn" aria-pressed={editTable} onClick={() => setEditTable(!editTable)}>{editTable ? "Close table" : "Edit DH table"}</button>}
        </div>
      </div>

      {custom && editTable && custom.format === "dh" && (
        <div className="section" key={preset}>
          <h3>DH table · {custom.name}</h3>
          <label className="prop">
            <span>Name</span>
            <input defaultValue={custom.name} onBlur={(e) => e.target.value && setCustom({ ...custom, name: e.target.value })} />
          </label>
          <div className="dh-table mono">
            <span>#</span>
            {DH_COLUMNS.map(([, label]) => <span key={label}>{label}</span>)}
            <span />
            {custom.joints.map((j, i) => (
              <div key={i} className="dh-row">
                <span>{i + 1}</span>
                {DH_COLUMNS.map(([key]) => (
                  <input key={key} type="number" defaultValue={Number(j[key] ?? (key === "lo_deg" ? -180 : key === "hi_deg" ? 180 : 0))}
                    onBlur={(e) => {
                      const v = Number(e.target.value);
                      if (!Number.isFinite(v)) return;
                      const joints = custom.joints.map((x, k) => (k === i ? { ...x, [key]: v } : x));
                      setCustom({ ...custom, joints });
                    }} />
                ))}
                <button className="icon" title="Remove joint" disabled={custom.joints.length <= 1}
                  onClick={() => setCustom({ ...custom, joints: custom.joints.filter((_, k) => k !== i) })}>×</button>
              </div>
            ))}
          </div>
          <div className="row-actions">
            <button className="btn" disabled={custom.joints.length >= 12}
              onClick={() => setCustom({ ...custom, joints: [...custom.joints, { d: 0, a: 100, alpha_deg: 0, lo_deg: -180, hi_deg: 180 }] })}>+ Joint</button>
          </div>
          <label className="prop">
            <span>Link radius</span>
            <input type="number" defaultValue={custom.link_radius ?? 35} onBlur={(e) => Number(e.target.value) > 0 && setCustom({ ...custom, link_radius: Number(e.target.value) })} />
          </label>
          <label className="prop">
            <span>Mount offset</span>
            <input type="number" defaultValue={custom.mount_offset ?? 0} onBlur={(e) => setCustom({ ...custom, mount_offset: Number(e.target.value) || 0 })} />
          </label>
          <div className="note">Standard DH: T = Rz(θ)·Tz(d)·Tx(a)·Rx(α). Mount offset is the distance from frame 0 down to the mounting surface.</div>
        </div>
      )}

      <div className="section">
        <h3>Move the tool</h3>
        <div className="jog">
          {(["X", "Y", "Z"] as const).map((a, i) => (
            <div key={a} className="jog-axis">
              <button className="btn" disabled={!tcp} onClick={() => jogBy(i, -1)} title={`Move the tool ${jog} mm along -${a}`}>−{a}</button>
              <button className="btn" disabled={!tcp} onClick={() => jogBy(i, +1)} title={`Move the tool ${jog} mm along +${a}`}>+{a}</button>
            </div>
          ))}
          <select value={jog} onChange={(e) => setJog(Number(e.target.value))} title="Jog step">
            {[1, 5, 20, 50, 100].map((s) => <option key={s} value={s}>{s} mm</option>)}
          </select>
        </div>
        <button className={`btn wide${targeting ? " primary" : ""}`} onClick={() => {
          const on = !targeting;
          setTargeting(on);
          setPickHandler(on ? (p) => void moveTool(p) : null);
        }}>{targeting ? "Click the part to send the tool there (click again to stop)" : "Move tool to a point on the part…"}</button>
        {ikNote && <div className="note bad">{ikNote}</div>}
        <h3>Joint pose</h3>
        {tcp && (
          <div className="mono" style={{ marginBottom: 6 }}>
            <div className="kv"><span>Tool position</span><span>{tcp.tool.map((v) => v.toFixed(1)).join(", ")} mm</span></div>
            <div className="kv"><span>Dexterity here</span><span className={tcp.dexterity < threshold ? "bad" : "ok"}>{tcp.dexterity.toFixed(3)}</span></div>
          </div>
        )}
        {q.map((v, i) => {
          const [lo, hi] = limits[i];
          const min = Math.max(lo, -360), max = Math.min(hi, 360);
          return (
            <label key={i} className="joint">
              <span className="mono">J{i + 1}</span>
              <input type="range" min={min} max={max} step={1} value={Math.max(min, Math.min(max, v))} onChange={(e) => setJoint(i, Number(e.target.value))} />
              <span className="mono">{v.toFixed(0)}°</span>
            </label>
          );
        })}
      </div>

      <div className="section">
        <h3>Reachability &amp; singularities</h3>
        <label className="prop">
          <span>Samples</span>
          <select value={samples} onChange={(e) => setSamples(Number(e.target.value))}>
            <option value={50000}>50k (fast)</option>
            <option value={150000}>150k</option>
            <option value={500000}>500k (detailed)</option>
          </select>
        </label>
        <label className="prop">
          <span>Lock threshold</span>
          <span className="field-inline">
            <input type="range" min={0.01} max={0.2} step={0.01} value={threshold} onChange={(e) => setThreshold(Number(e.target.value))} />
            <span className="mono">{threshold.toFixed(2)}</span>
          </span>
        </label>
        <label className="prop">
          <span>Self-collision</span>
          <input type="checkbox" checked={collisions} onChange={(e) => setCollisions(e.target.checked)} />
        </label>
        <label className="prop">
          <span>Mounting surface</span>
          <input type="checkbox" checked={floor} disabled={!collisions} onChange={(e) => setFloor(e.target.checked)} />
        </label>
        <button className="btn primary wide" disabled={busy} onClick={compute}>{busy ? "Sampling joint space…" : "Compute reach envelope"}</button>
        {stats && (
          <div className="mono" style={{ marginTop: 10 }}>
            <div className="kv"><span>Reachable volume</span><span>{stats.volume_m3.toFixed(2)} m³</span></div>
            <div className="kv"><span>Lock zones</span><span className={stats.singular_pct > 5 ? "bad" : "ok"}>{stats.singular_pct.toFixed(1)}%</span></div>
            <div className="kv"><span>Dexterous (κ⁻¹ ≥ 0.3)</span><span>{stats.dexterous_pct.toFixed(1)}%</span></div>
            <div className="kv"><span>Poses rejected (collision)</span><span>{((100 * stats.colliding_samples) / stats.samples).toFixed(1)}%</span></div>
            <div className="kv"><span>Full-pose singular samples</span><span>{((100 * stats.near_singular_samples) / stats.samples).toFixed(2)}%</span></div>
            <div className="kv"><span>Voxel / samples</span><span>{stats.voxel_mm.toFixed(0)} mm · {(stats.samples / 1000).toFixed(0)}k · {stats.ms.toFixed(0)} ms</span></div>
            <div className="legend">
              <span><i style={{ background: "#ff3333" }} />lock zone</span>
              <span><i style={{ background: "#f09e33" }} />poor</span>
              <span><i style={{ background: "#00ccb8" }} />good</span>
              <span><i style={{ background: "#00ff66" }} />isotropic</span>
            </div>
            <div className="row-actions">
              <button className="btn" aria-pressed={visible} onClick={() => setVisible(!visible)}>{visible ? "Hide cloud" : "Show cloud"}</button>
              <button className="btn" onClick={() => setMode(mode ? 0 : 1)}>Colour: {mode ? "worst" : "best"} pose</button>
            </div>
            <div className="note">
              Colour is positional dexterity: the inverse condition number of the translational Jacobian (1 = isotropic, 0 = singular).
              A lock zone is a region where even the best reaching pose is below the threshold. With self-collision on, poses where
              non-neighbouring links overlap (capsule model) or a link passes through the mounting surface are rejected.
            </div>
          </div>
        )}
      </div>
    </>
  );
}
