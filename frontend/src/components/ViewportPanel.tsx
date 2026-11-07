"use client";

import { useEffect, useRef, useState } from "react";
import { click } from "@/lib/audio";
import { Viewport, type DisplayMode, type PickResult, type Section, type ViewName } from "@/lib/gpu";
import type { Vec3 } from "@/lib/ipc";
import { fmtLen, type Settings } from "@/lib/settings";
import ViewCube from "./ViewCube";

export interface Marker { id: string; label: string; position: Vec3; tone: "teal" | "amber" | "fail" }

const VIEW_BUTTONS: [ViewName, string, string][] = [
  ["iso", "Iso", "0"], ["front", "Front", "1"], ["back", "Back", "2"], ["left", "Left", "3"],
  ["right", "Right", "4"], ["top", "Top", "5"], ["bottom", "Bottom", "6"],
];

export default function ViewportPanel({ insets, onReady, markers, settings, onSettings, onHover, onError, empty, busy }: {
  insets: { left: number; right: number; top: number; bottom: number };
  onReady: (v: Viewport) => void;
  markers: Marker[];
  settings: Settings;
  onSettings: (patch: Partial<Settings>) => void;
  onHover: (p: PickResult | null) => void;
  onError: (msg: string) => void;
  empty: boolean;
  busy: boolean;
}) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const vpRef = useRef<Viewport | null>(null);
  const markerEls = useRef(new Map<string, HTMLElement>());
  const [mode, setMode] = useState<DisplayMode>("shaded");
  const [section, setSection] = useState<Section>({ axis: "off", offset: 0, flip: false });
  const [measure, setMeasure] = useState(false);
  const [points, setPoints] = useState<Vec3[]>([]);
  const [triad, setTriad] = useState<{ x: number[]; y: number[]; z: number[] }>({ x: [1, 0], y: [0, 1], z: [0, -1] });
  const [ms, setMs] = useState<number | null>(null);
  const [angles, setAngles] = useState<[number, number]>([-2.35, 0.55]);
  const markersRef = useRef(markers);
  markersRef.current = markers;
  const pointsRef = useRef(points);
  pointsRef.current = points;

  useEffect(() => {
    let disposed = false;
    Viewport.create(canvasRef.current!).then((v) => {
      if (disposed) return v.destroy();
      vpRef.current = v;
      (window as unknown as { __datumViewport?: Viewport }).__datumViewport = v;
      v.setInsets(insets);
      v.onFrame = (t) => {
        setMs(t);
        placeOverlays();
      };
      onReady(v);
    }).catch((e) => onError(String(e)));
    return () => {
      disposed = true;
      vpRef.current?.destroy();
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // keep HTML annotations and the axis triad glued to the camera
  function placeOverlays() {
    const v = vpRef.current;
    if (!v) return;
    for (const m of [...markersRef.current, ...pointsRef.current.map((p, i) => ({ id: `pt${i}`, position: p }))]) {
      const el = markerEls.current.get(m.id);
      if (!el) continue;
      const s = v.project(m.position);
      el.style.transform = `translate3d(${s.x.toFixed(1)}px, ${s.y.toFixed(1)}px, 0)`;
      el.style.visibility = s.visible ? "visible" : "hidden";
    }
    const o = v.project(v.cam.target);
    const axis = (d: Vec3) => {
      const p = v.project([v.cam.target[0] + d[0], v.cam.target[1] + d[1], v.cam.target[2] + d[2]]);
      const len = Math.hypot(p.x - o.x, p.y - o.y) || 1;
      return [(p.x - o.x) / len, (p.y - o.y) / len];
    };
    const unit = v.cam.dist * 0.05;
    setTriad({ x: axis([unit, 0, 0]), y: axis([0, unit, 0]), z: axis([0, 0, unit]) });
    setAngles([v.cam.yaw, v.cam.pitch]);
  }

  useEffect(() => {
    const v = vpRef.current;
    if (!v) return;
    v.mode = mode;
    v.grid = settings.grid;
    v.cam.ortho = settings.orthographic;
    v.section = section;
    v.request();
  }, [mode, settings.grid, settings.orthographic, section]);

  useEffect(() => placeOverlays(), [markers, points]); // eslint-disable-line react-hooks/exhaustive-deps
  useEffect(() => {
    vpRef.current?.setInsets(insets);
  }, [insets.left, insets.right, insets.top, insets.bottom]); // eslint-disable-line react-hooks/exhaustive-deps

  // pointer: left orbit, right/middle or shift pan, wheel zoom, click to measure
  useEffect(() => {
    const canvas = canvasRef.current!;
    let drag: { x: number; y: number; pan: boolean; moved: boolean } | null = null;
    let hoverTimer = 0;
    const down = (e: PointerEvent) => {
      drag = { x: e.clientX, y: e.clientY, pan: e.button !== 0 || e.shiftKey, moved: false };
      canvas.setPointerCapture(e.pointerId);
    };
    const move = (e: PointerEvent) => {
      const v = vpRef.current;
      if (!v) return;
      if (drag) {
        const dx = e.clientX - drag.x, dy = e.clientY - drag.y;
        if (Math.abs(dx) + Math.abs(dy) > 2) drag.moved = true;
        drag.x = e.clientX;
        drag.y = e.clientY;
        if (drag.pan) v.pan(dx, dy);
        else v.orbit(dx, dy);
        return;
      }
      clearTimeout(hoverTimer);
      const r = canvas.getBoundingClientRect();
      hoverTimer = window.setTimeout(() => v.pick(e.clientX - r.left, e.clientY - r.top).then(onHover).catch(() => undefined), 60);
    };
    const up = async (e: PointerEvent) => {
      const wasClick = drag && !drag.moved && e.button === 0;
      if (drag?.moved && !drag.pan) vpRef.current?.release();
      drag = null;
      if (!wasClick || !measureRef.current) return;
      const r = canvas.getBoundingClientRect();
      const hit = await vpRef.current?.pick(e.clientX - r.left, e.clientY - r.top);
      if (!hit) return;
      click(330, 0.4);
      setPoints((p) => (p.length >= 2 ? [hit.position] : [...p, hit.position]));
    };
    const wheel = (e: WheelEvent) => {
      e.preventDefault();
      const r = canvas.getBoundingClientRect();
      vpRef.current?.zoomAt(e.deltaY, e.clientX - r.left, e.clientY - r.top);
    };
    const dbl = async (e: MouseEvent) => {
      const r = canvas.getBoundingClientRect();
      const hit = await vpRef.current?.pick(e.clientX - r.left, e.clientY - r.top);
      if (hit) vpRef.current?.lookAlong(hit.position, hit.normal);
      else vpRef.current?.fit();
    };
    canvas.addEventListener("dblclick", dbl);
    canvas.addEventListener("pointerdown", down);
    canvas.addEventListener("pointermove", move);
    canvas.addEventListener("pointerup", up);
    canvas.addEventListener("pointerleave", () => onHover(null));
    canvas.addEventListener("wheel", wheel, { passive: false });
    canvas.addEventListener("contextmenu", (e) => e.preventDefault());
    const ro = new ResizeObserver(() => vpRef.current?.request());
    ro.observe(canvas);
    return () => {
      canvas.removeEventListener("pointerdown", down);
      canvas.removeEventListener("pointermove", move);
      canvas.removeEventListener("pointerup", up);
      canvas.removeEventListener("wheel", wheel);
      canvas.removeEventListener("dblclick", dbl);
      ro.disconnect();
    };
  }, [onHover]);
  const measureRef = useRef(measure);
  measureRef.current = measure;

  // number keys switch views, F fits, M toggles measure (ignored while typing)
  useEffect(() => {
    const key = (e: KeyboardEvent) => {
      const t = e.target as HTMLElement;
      if (e.ctrlKey || e.metaKey || e.altKey || ["INPUT", "SELECT", "TEXTAREA"].includes(t.tagName)) return;
      const view = VIEW_BUTTONS.find(([, , k]) => k === e.key);
      if (view) vpRef.current?.setView(view[0]);
      else if (e.key === "f" || e.key === "F") vpRef.current?.fit();
      else if (e.key === "m" || e.key === "M") toggleMeasure();
      else if (e.key === "Escape") setPoints([]);
    };
    window.addEventListener("keydown", key);
    return () => window.removeEventListener("keydown", key);
  });

  const toggleMeasure = () => {
    click(247, 0.4);
    setMeasure((m) => !m);
    setPoints([]);
  };
  const bounds = vpRef.current?.bounds;
  const axisIndex = { off: 0, x: 0, y: 1, z: 2 }[section.axis];
  const range = bounds ? [bounds.lo[axisIndex], bounds.hi[axisIndex]] : [-50, 50];
  const distance = points.length === 2 ? Math.hypot(...points[0].map((v, i) => v - points[1][i])) : null;

  return (
    <main className="area-view viewport">
      <canvas ref={canvasRef} className={measure ? "measuring" : ""} />

      <div className="annotations">
        {settings.annotations && markers.map((m) => (
          <div key={m.id} className="annotation" ref={(el) => { if (el) markerEls.current.set(m.id, el); else markerEls.current.delete(m.id); }}>
            <span className={`dot ${m.tone}`} />
            {m.label}
          </div>
        ))}
        {points.map((p, i) => (
          <div key={`pt${i}`} className="annotation point" ref={(el) => { if (el) markerEls.current.set(`pt${i}`, el); else markerEls.current.delete(`pt${i}`); }}>
            <span className="dot amber" />
            {String.fromCharCode(65 + i)}
          </div>
        ))}
      </div>

      <div className="vp-toolbar glass-strong">
        {VIEW_BUTTONS.map(([name, label, k]) => (
          <button key={name} title={`${label} view (${k})`} onClick={() => { click(196, 0.3); vpRef.current?.setView(name); }}>{label}</button>
        ))}
        <span className="sep" />
        <button title="Fit to part (F)" onClick={() => vpRef.current?.fit()}>Fit</button>
        <button aria-pressed={settings.orthographic} title="Orthographic projection" onClick={() => onSettings({ orthographic: !settings.orthographic })}>Ortho</button>
        <button aria-pressed={settings.grid} onClick={() => onSettings({ grid: !settings.grid })}>Grid</button>
        <span className="sep" />
        <select value={mode} onChange={(e) => setMode(e.target.value as DisplayMode)} title="Display mode">
          <option value="shaded">Shaded</option>
          <option value="xray">X-ray</option>
          <option value="normals">Normals</option>
          <option value="dfm">Manufacturability</option>
          <option value="cost">GPU cost</option>
        </select>
        <select value={section.axis} onChange={(e) => {
          const axis = e.target.value as Section["axis"];
          const i = { off: 0, x: 0, y: 1, z: 2 }[axis];
          setSection({ axis, flip: section.flip, offset: bounds ? (bounds.lo[i] + bounds.hi[i]) / 2 : 0 });
        }} title="Section plane">
          <option value="off">No section</option>
          <option value="x">Section X</option>
          <option value="y">Section Y</option>
          <option value="z">Section Z</option>
        </select>
        <span className="sep" />
        <button aria-pressed={measure} title="Measure distance (M)" onClick={toggleMeasure}>Measure</button>
      </div>

      {section.axis !== "off" && (
        <div className="section-slider glass-strong">
          <span className="mono">{section.axis.toUpperCase()} = {fmtLen(section.offset, settings)}</span>
          <input type="range" min={range[0]} max={range[1]} step={(range[1] - range[0]) / 400} value={section.offset}
            onChange={(e) => setSection({ ...section, offset: Number(e.target.value) })} />
          <button className="btn" onClick={() => setSection({ ...section, flip: !section.flip })}>Flip</button>
        </div>
      )}

      {mode === "dfm" && (
        <div className="dfm-legend glass-strong">
          <b>Manufacturability (3D print, Z up)</b>
          <span><i style={{ background: "#2ec77a" }} />Wall &gt; 1.5 mm</span>
          <span><i style={{ background: "#f9b32e" }} />Wall 0.5–1.5 mm</span>
          <span><i style={{ background: "#f2332e" }} />Wall &lt; 0.5 mm</span>
          <span><i style={{ background: "#9e52f2" }} />Overhang &gt; 45° (needs support)</span>
        </div>
      )}

      {measure && (
        <div className="measure-readout glass-strong mono">
          {points.length === 0 && "Click a point on the part"}
          {points.length === 1 && "Click a second point"}
          {distance !== null && (
            <>
              <b>{fmtLen(distance, settings)}</b>
              <span>ΔX {fmtLen(points[1][0] - points[0][0], settings, false)} · ΔY {fmtLen(points[1][1] - points[0][1], settings, false)} · ΔZ {fmtLen(points[1][2] - points[0][2], settings, false)}</span>
            </>
          )}
        </div>
      )}

      <ViewCube yaw={angles[0]} pitch={angles[1]} onView={(name) => { click(196, 0.3); vpRef.current?.setView(name); }} />

      <svg className="triad" viewBox="-40 -40 80 80" aria-hidden>
        {([["x", "#ff6b6b"], ["y", "#6fdc8c"], ["z", "#5ea8ff"]] as const).map(([a, color]) => (
          <g key={a}>
            <line x1={0} y1={0} x2={triad[a][0] * 26} y2={triad[a][1] * 26} stroke={color} strokeWidth={2} strokeLinecap="round" />
            <text x={triad[a][0] * 34} y={triad[a][1] * 34 + 4} fill={color} fontSize={11} textAnchor="middle">{a.toUpperCase()}</text>
          </g>
        ))}
      </svg>

      {ms !== null && <div className="fps mono">{ms.toFixed(1)} ms</div>}
      {busy && <div className="busy">Rebuilding…</div>}
      {empty && (
        <div className="empty">
          <div>
            <b>No geometry yet</b>
            Insert a primitive, open a .datum file, or start from File → New from example.
          </div>
        </div>
      )}
    </main>
  );
}
