"use client";

import { useEffect, useState } from "react";
import { isTauri } from "@/lib/ipc";
import Icon from "./Icon";

type Found = { version: string; notes?: string; install: (progress: (done: number, total: number | null) => void) => Promise<void> };
type State = { kind: "idle" } | { kind: "checking" } | { kind: "none" } | { kind: "found"; up: Found } | { kind: "installing"; up: Found; pct: number | null } | { kind: "error"; msg: string };

/**
 * Signed auto-update from GitHub releases. Checks quietly a few seconds after start; the user
 * decides when to install. Updates are verified against the public key built into the app.
 */
async function findUpdate(): Promise<Found | null> {
  const { check } = await import("@tauri-apps/plugin-updater");
  const u = await check();
  if (!u) return null;
  return {
    version: u.version,
    notes: u.body,
    install: async (progress) => {
      let done = 0, total: number | null = null;
      await u.downloadAndInstall((e) => {
        if (e.event === "Started") total = e.data.contentLength ?? null;
        else if (e.event === "Progress") (done += e.data.chunkLength, progress(done, total));
      });
      const { relaunch } = await import("@tauri-apps/plugin-process");
      await relaunch();
    },
  };
}

export default function UpdateBanner({ checkNow, onLog }: { checkNow: number; onLog: (level: "info" | "warn" | "error", msg: string) => void }) {
  const [state, setState] = useState<State>({ kind: "idle" });
  const [hidden, setHidden] = useState(false);

  const run = async (manual: boolean) => {
    if (!isTauri()) return;
    setHidden(false);
    setState({ kind: "checking" });
    try {
      const up = await findUpdate();
      if (up) {
        setState({ kind: "found", up });
        onLog("info", `update ${up.version} is available`);
      } else {
        setState(manual ? { kind: "none" } : { kind: "idle" });
      }
    } catch (e) {
      // offline, or no release published yet: only worth showing when the user asked
      onLog("warn", `update check: ${String(e)}`);
      setState(manual ? { kind: "error", msg: "No update information found. You may be offline, or no newer version has been published yet." } : { kind: "idle" });
    }
  };
  useEffect(() => {
    const t = setTimeout(() => void run(false), 6000);
    return () => clearTimeout(t);
  }, []); // eslint-disable-line react-hooks/exhaustive-deps
  useEffect(() => {
    if (checkNow > 0) void run(true);
  }, [checkNow]); // eslint-disable-line react-hooks/exhaustive-deps

  if (hidden || state.kind === "idle" || state.kind === "checking") return null;
  return (
    <div className="update-banner glass-strong" role="status">
      <span className="tool-icon"><Icon name={state.kind === "error" ? "close" : "download"} size={20} /></span>
      <div className="ub-text">
        {state.kind === "none" && <><b>You are up to date</b><span>This is the latest Datum Core.</span></>}
        {state.kind === "error" && <><b>Update check failed</b><span>{state.msg}</span></>}
        {state.kind === "found" && <><b>Datum Core {state.up.version} is ready</b><span>Install now; your work is autosaved and reopens after the restart.</span></>}
        {state.kind === "installing" && <><b>Installing {state.up.version}…</b>
          <span className="ub-bar"><i style={{ width: `${state.pct === null ? 30 : Math.round(state.pct * 100)}%` }} /></span></>}
      </div>
      {state.kind === "found" && (
        <button className="btn primary" onClick={() => {
          const up = state.up;
          setState({ kind: "installing", up, pct: 0 });
          up.install((done, total) => setState({ kind: "installing", up, pct: total ? done / total : null })).catch((e) => {
            onLog("error", `update install: ${String(e)}`);
            setState({ kind: "error", msg: "The update could not be installed. Download it from the Releases page instead." });
          });
        }}>Install &amp; restart</button>
      )}
      {state.kind !== "installing" && <button className="icon" aria-label="Dismiss" onClick={() => setHidden(true)}>×</button>}
    </div>
  );
}
