"use client";

import { useEffect, useMemo, useRef, useState } from "react";
import { click } from "@/lib/audio";
import { fromDisplay, toDisplay, type Settings } from "@/lib/settings";

export type ProfilePt = [number, number, number]; // x, y, fillet radius (mm)

const W = 300, H = 220, PAD = 22;

/**
 * Interactive 2D sketch of a closed profile: drag points (1 mm snap, Shift for 0.1 mm,
 * Alt for free), click "+" on an edge to insert a point, select a point to type exact
 * coordinates and a fillet radius, click a dimension to set an edge length.
 * Changes are committed on release, so the kernel rebuilds once per edit.
 */
export default function SketchEditor({ value, onCommit, settings, axisHint }: {
  value: ProfilePt[];
  onCommit: (pts: ProfilePt[]) => void;
  settings: Settings;
  axisHint?: string;
}) {
  const [pts, setPts] = useState<ProfilePt[]>(value);
  const [sel, setSel] = useState<number | null>(null);
  const [drag, setDrag] = useState<number | null>(null);
  const moved = useRef(false);
  const [editEdge, setEditEdge] = useState<number | null>(null);
  const svg = useRef<SVGSVGElement>(null);
  useEffect(() => setPts(value), [value]);

  // view transform: fit the profile with a margin, keep 1:1 aspect
  const view = useMemo(() => {
    const xs = pts.map((p) => p[0]), ys = pts.map((p) => p[1]);
    const minX = Math.min(...xs, 0), maxX = Math.max(...xs, 1), minY = Math.min(...ys, 0), maxY = Math.max(...ys, 1);
    const span = Math.max(maxX - minX, maxY - minY, 1) * 1.25;
    const scale = Math.min((W - 2 * PAD) / span, (H - 2 * PAD) / span);
    const cx = (minX + maxX) / 2, cy = (minY + maxY) / 2;
    return { scale, cx, cy };
    // the view only re-fits when not dragging, so points do not run away from the cursor
  }, [drag === null ? JSON.stringify(value) : "drag"]); // eslint-disable-line react-hooks/exhaustive-deps
  const sx = (x: number) => W / 2 + (x - view.cx) * view.scale;
  const sy = (y: number) => H / 2 - (y - view.cy) * view.scale;
  const toModel = (e: React.PointerEvent) => {
    const r = svg.current!.getBoundingClientRect();
    const px = ((e.clientX - r.left) / r.width) * W, py = ((e.clientY - r.top) / r.height) * H;
    return [(px - W / 2) / view.scale + view.cx, -(py - H / 2) / view.scale + view.cy];
  };
  const snap = (v: number, e: React.PointerEvent) => (e.altKey ? v : e.shiftKey ? Math.round(v * 10) / 10 : Math.round(v));

  const commit = (next: ProfilePt[]) => {
    setPts(next);
    onCommit(next);
  };
  const fmt = (mm: number) => `${+toDisplay(mm, settings).toFixed(settings.units === "in" ? 3 : 1)}`;

  // grid step: 1-2-5 sequence near 20 px
  const step = useMemo(() => {
    const raw = 20 / view.scale;
    const p = Math.pow(10, Math.floor(Math.log10(raw)));
    return [1, 2, 5, 10].map((k) => k * p).find((s) => s >= raw) ?? raw;
  }, [view.scale]);
  const gridLines = [];
  const x0 = Math.floor((view.cx - W / 2 / view.scale) / step) * step, x1 = view.cx + W / 2 / view.scale;
  const y0 = Math.floor((view.cy - H / 2 / view.scale) / step) * step, y1 = view.cy + H / 2 / view.scale;
  for (let x = x0; x <= x1; x += step) gridLines.push(<line key={`x${x}`} x1={sx(x)} x2={sx(x)} y1={0} y2={H} className={Math.abs(x) < 1e-9 ? "axis" : ""} />);
  for (let y = y0; y <= y1; y += step) gridLines.push(<line key={`y${y}`} y1={sy(y)} y2={sy(y)} x1={0} x2={W} className={Math.abs(y) < 1e-9 ? "axis" : ""} />);

  const s = sel !== null ? pts[sel] : null;
  const setPoint = (i: number, patch: Partial<{ x: number; y: number; r: number }>) =>
    commit(pts.map((p, k) => (k === i ? [patch.x ?? p[0], patch.y ?? p[1], patch.r ?? p[2]] : p)));
  const setEdgeLength = (i: number, len: number) => {
    const a = pts[i], b = pts[(i + 1) % pts.length];
    const d = Math.hypot(b[0] - a[0], b[1] - a[1]) || 1;
    const nb: ProfilePt = [a[0] + ((b[0] - a[0]) / d) * len, a[1] + ((b[1] - a[1]) / d) * len, b[2]];
    commit(pts.map((p, k) => (k === (i + 1) % pts.length ? nb : p)));
  };

  return (
    <div className="sketch">
      <svg ref={svg} viewBox={`0 0 ${W} ${H}`} className="sketch-canvas"
        onPointerMove={(e) => {
          if (drag === null) return;
          const [x, y] = toModel(e);
          moved.current = true;
          setPts(pts.map((p, k) => (k === drag ? [snap(x, e), snap(y, e), p[2]] : p)));
        }}
        onPointerUp={() => {
          if (drag !== null) {
            setDrag(null);
            if (moved.current) onCommit(pts);  // a click only selects: no rebuild
          }
        }}
        onPointerDown={() => setSel(null)}>
        <g className="grid">{gridLines}</g>
        <polygon className="profile" points={pts.map((p) => `${sx(p[0])},${sy(p[1])}`).join(" ")} />
        {pts.map((p, i) => {
          const q = pts[(i + 1) % pts.length];
          const mx = (sx(p[0]) + sx(q[0])) / 2, my = (sy(p[1]) + sy(q[1])) / 2;
          const len = Math.hypot(q[0] - p[0], q[1] - p[1]);
          return (
            <g key={`e${i}`}>
              <text x={mx} y={my - 6} className="dim" onPointerDown={(e) => { e.stopPropagation(); setEditEdge(i); }}>{fmt(len)}</text>
              <g className="insert" onPointerDown={(e) => {
                e.stopPropagation();
                click(330, 0.3);
                const mid: ProfilePt = [Math.round((p[0] + q[0]) / 2 * 10) / 10, Math.round((p[1] + q[1]) / 2 * 10) / 10, 0];
                const next = [...pts.slice(0, i + 1), mid, ...pts.slice(i + 1)];
                commit(next);
                setSel(i + 1);
              }}>
                <circle cx={mx} cy={my} r={5} />
                <text x={mx} y={my + 3}>+</text>
              </g>
            </g>
          );
        })}
        {pts.map((p, i) => (
          <g key={`p${i}`} className={`pt ${sel === i ? "sel" : ""}`}
            onPointerDown={(e) => {
              e.stopPropagation();
              setSel(i);
              setDrag(i);
              moved.current = false;
              try {
                (e.target as Element).setPointerCapture(e.pointerId);
              } catch {
                // not a tracked pointer (e.g. synthetic): dragging still works via the svg handlers
              }
            }}>
            <circle cx={sx(p[0])} cy={sy(p[1])} r={p[2] > 0 ? 6 : 4.5} />
            {p[2] > 0 && <text x={sx(p[0]) + 8} y={sy(p[1]) - 6} className="rad">R{fmt(p[2])}</text>}
          </g>
        ))}
      </svg>
      {editEdge !== null && (
        <div className="sketch-row">
          <span>Edge {editEdge + 1}–{(editEdge + 1) % pts.length + 1} length</span>
          <input autoFocus type="number" defaultValue={fmt(Math.hypot(pts[(editEdge + 1) % pts.length][0] - pts[editEdge][0], pts[(editEdge + 1) % pts.length][1] - pts[editEdge][1]))}
            onBlur={(e) => { const v = Number(e.target.value); if (v > 0) setEdgeLength(editEdge, fromDisplay(v, settings)); setEditEdge(null); }}
            onKeyDown={(e) => { if (e.key === "Enter") (e.target as HTMLInputElement).blur(); if (e.key === "Escape") setEditEdge(null); }} />
          <em>{settings.units}</em>
        </div>
      )}
      {s && sel !== null && (
        <div className="sketch-row" key={`sel${sel}-${s.join()}`}>
          <span>P{sel + 1}</span>
          <input type="number" defaultValue={fmt(s[0])} title="X" onBlur={(e) => setPoint(sel, { x: fromDisplay(Number(e.target.value), settings) })} />
          <input type="number" defaultValue={fmt(s[1])} title="Y" onBlur={(e) => setPoint(sel, { y: fromDisplay(Number(e.target.value), settings) })} />
          <input type="number" min={0} defaultValue={fmt(s[2])} title="Fillet radius" onBlur={(e) => setPoint(sel, { r: Math.max(0, fromDisplay(Number(e.target.value), settings)) })} />
          <button className="icon" title="Delete point" disabled={pts.length <= 3} onClick={() => { commit(pts.filter((_, k) => k !== sel)); setSel(null); }}>×</button>
        </div>
      )}
      <div className="note">
        {axisHint ? `${axisHint} · ` : ""}Drag points (1 mm snap; Shift 0.1 mm; Alt free). “+” inserts a point; click a dimension to set its length; select a point for exact X, Y and fillet R.
      </div>
    </div>
  );
}
