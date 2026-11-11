"use client";

import { click } from "@/lib/audio";
import Icon from "./Icon";

export type StepKey = "part" | "hold" | "check" | "lighten" | "printcheck" | "print";
const STEPS: { key: StepKey; label: string; icon: string; hint: string; optional?: boolean }[] = [
  { key: "part", label: "Part", icon: "box", hint: "Import a part or start from a template" },
  { key: "hold", label: "Hold & load", icon: "strength", hint: "Click where it is held and where it is pushed" },
  { key: "check", label: "Check", icon: "analysis", hint: "Run the stress analysis: will it hold?" },
  { key: "lighten", label: "Lighten", icon: "gyroid_infill", hint: "Replace the inside with a stress-graded lattice", optional: true },
  { key: "printcheck", label: "Print check", icon: "display", hint: "Thin walls and overhangs, coloured on the part" },
  { key: "print", label: "Print", icon: "play", hint: "Pick your printer and filament, send to the slicer" },
];

/** The design-to-print path, always visible: what is done, what is next, one click to get there. */
export default function WorkflowBar({ done, onStep }: { done: Record<StepKey, boolean>; onStep: (k: StepKey) => void }) {
  const current = STEPS.find((s) => !done[s.key] && !s.optional)?.key ?? null;
  return (
    <nav className="workflow glass-strong" aria-label="Design to print">
      {STEPS.map((s, i) => (
        <span key={s.key} className="wf-item">
          {i > 0 && <span className={`wf-line${done[STEPS[i - 1].key] ? " lit" : ""}`} />}
          <button className={`wf-step${done[s.key] ? " done" : ""}${current === s.key ? " now" : ""}${s.optional ? " optional" : ""}`}
            title={`${s.hint}${s.optional ? " (optional)" : ""}`} onClick={() => { click(240 + i * 20, 0.35); onStep(s.key); }}>
            <span className="wf-dot">{done[s.key] ? <Icon name="check" size={13} /> : <Icon name={s.icon} size={13} />}</span>
            <span className="wf-label">{s.label}</span>
          </button>
        </span>
      ))}
    </nav>
  );
}
