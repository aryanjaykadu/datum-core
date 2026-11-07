"use client";

import type { PickResult } from "@/lib/gpu";
import type { Evaluation } from "@/lib/ipc";
import { fmtLen, type Settings } from "@/lib/settings";

export default function StatusBar({ hover, result, settings, busy, error, path, locked }: {
  hover: PickResult | null;
  result: Evaluation | null;
  settings: Settings;
  busy: boolean;
  error: string | null;
  path: string | null;
  locked: boolean;
}) {
  return (
    <footer className="statusbar area-status glass-strong mono">
      <span className={error ? "bad" : busy ? "warn" : "ok"}>● {error ? "Error" : busy ? "Rebuilding" : "Ready"}</span>
      {error && <span className="bad ellipsis" title={error}>{error}</span>}
      <span className="ellipsis" title={path ?? ""}>{path ?? "Not saved"}</span>
      {locked && <span className="ok">🔒 device-locked</span>}
      <span className="spacer" />
      {hover && <span>X {fmtLen(hover.position[0], settings, false)}  Y {fmtLen(hover.position[1], settings, false)}  Z {fmtLen(hover.position[2], settings, false)}</span>}
      {result && <span>{result.geometry.triangles.toLocaleString()} tris</span>}
      {result && <span>{result.timing_ms.total.toFixed(0)} ms</span>}
      <span>{settings.units}</span>
      <span>{settings.quality}</span>
    </footer>
  );
}
