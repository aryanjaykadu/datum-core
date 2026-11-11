"use client";

import type { PickResult } from "@/lib/gpu";
import type { Evaluation } from "@/lib/ipc";
import { fmtLen, type Settings } from "@/lib/settings";
import Icon from "./Icon";

export default function StatusBar({ hover, result, settings, busy, error, path, locked, dock, onDock }: {
  hover: PickResult | null;
  result: Evaluation | null;
  settings: Settings;
  busy: boolean;
  error: string | null;
  path: string | null;
  locked: boolean;
  dock: "graph" | "history" | "log" | null;
  onDock: (d: "graph" | "history" | "log" | null) => void;
}) {
  return (
    <footer className="statusbar area-status glass-strong mono">
      <span className={error ? "bad" : busy ? "warn" : "ok"}>● {error ? "Error" : busy ? "Rebuilding" : "Ready"}</span>
      {error && <span className="bad ellipsis" title={error}>{error}</span>}
      <span className="ellipsis" title={path ?? ""}>{path ?? "Not saved"}</span>
      {locked && <span className="ok">🔒 device-locked</span>}
      <span className="spacer" />
      <span className="dock-toggles" role="group" aria-label="Bottom panel">
        {([["graph", "graph", "Graph"], ["history", "history", "History"], ["log", "log", "Log"]] as const).map(([d, icon, label]) => (
          <button key={d} aria-pressed={dock === d} title={`${dock === d ? "Hide" : "Show"} ${label.toLowerCase()}`} onClick={() => onDock(d)}>
            <Icon name={icon} size={13} />{label}
          </button>
        ))}
      </span>
      {hover && <span>X {fmtLen(hover.position[0], settings, false)}  Y {fmtLen(hover.position[1], settings, false)}  Z {fmtLen(hover.position[2], settings, false)}</span>}
      {hover?.probe && !hover.cap && <span title="Wall thickness under the cursor, probed along the surface normal">{wallText(hover.probe, settings)}</span>}
      {result && <span>{result.geometry.triangles.toLocaleString()} tris</span>}
      {result && <span>{result.timing_ms.total.toFixed(0)} ms</span>}
      <span>{settings.units}</span>
      <span>{settings.quality}</span>
    </footer>
  );
}

/** Probes at 0.5, 1, 2, 4 mm under the surface: the first one outside bounds the wall. */
function wallText(probe: number[], settings: Settings) {
  const depths = [0.5, 1, 2, 4];
  const k = probe.findIndex((v) => v > 0);
  if (k < 0) return `wall > ${fmtLen(4, settings)}`;
  if (k === 0) return `wall < ${fmtLen(0.5, settings)} ⚠`;
  return `wall ${fmtLen(depths[k - 1], settings, false)}–${fmtLen(depths[k], settings)}`;
}
