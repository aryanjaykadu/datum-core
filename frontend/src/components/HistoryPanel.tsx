"use client";

import type { HistoryEntry } from "@/lib/document";

/** Undo timeline: click any step to jump there; undone steps stay until a new edit. */
export default function HistoryPanel({ entries, onTravel }: { entries: { done: HistoryEntry[]; undone: HistoryEntry[] }; onTravel: (steps: number) => void }) {
  const { done, undone } = entries;
  const time = (t: number) => new Date(t).toLocaleTimeString("en-GB", { hour12: false });
  return (
    <div className="history">
      {done.length === 0 && undone.length === 0 && <div className="note">Every change you make appears here. Click a step to go back to it.</div>}
      {done.length > 0 && (
        <button className="history-row" onClick={() => onTravel(-done.length)} title="Undo everything">
          <span className="dot" /> <span>Start</span>
        </button>
      )}
      {done.map((e, i) => (
        <button key={`d${i}`} className={`history-row${i === done.length - 1 ? " current" : ""}`} onClick={() => onTravel(i - (done.length - 1))}
          title={i === done.length - 1 ? "Current state" : `Go back ${done.length - 1 - i} step(s)`}>
          <span className="dot" /> <span>{e.label}</span> <em className="mono">{time(e.at)}</em>
        </button>
      ))}
      {undone.map((e, i) => (
        <button key={`u${i}`} className="history-row undone" onClick={() => onTravel(i + 1)} title={`Redo ${i + 1} step(s)`}>
          <span className="dot" /> <span>{e.label}</span> <em className="mono">{time(e.at)}</em>
        </button>
      ))}
    </div>
  );
}
