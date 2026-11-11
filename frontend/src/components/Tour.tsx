"use client";

import { useEffect, useLayoutEffect, useState } from "react";
import { click } from "@/lib/audio";

const STEPS: { target: string; title: string; body: string; place: "below" | "right" | "left" | "above" }[] = [
  { target: ".workflow", title: "Your path from part to print", body: "These steps take you from a part to a print you can trust. The glowing one is next; click any step to jump there.", place: "below" },
  { target: ".area-rail", title: "All the tools, sorted", body: "Each icon opens a set of tools with a short description. The magnifier finds any tool by name.", place: "right" },
  { target: ".area-inspector .tabs", title: "Edit, Strength, Analyse", body: "Edit changes the selected feature. Strength checks whether the part holds and makes it lighter. Analyse shows weight, size and print estimates.", place: "left" },
  { target: ".vp-pill", title: "Look around", body: "Fit the view, pick a standard view, cut the part open, measure, or switch to Print check to see thin walls and overhangs. Drag to orbit; double-click a face to look straight at it.", place: "above" },
  { target: ".tb-search", title: "Just ask", body: "Press Ctrl+K and type what you want: a tool, a view, an export. Ctrl+P prints.", place: "below" },
];

/** A short spotlight tour of the workspace, shown once (Help → Take the tour to see it again). */
export default function Tour({ onDone }: { onDone: () => void }) {
  const [i, setI] = useState(0);
  const [box, setBox] = useState<DOMRect | null>(null);
  const step = STEPS[i];
  useLayoutEffect(() => {
    const el = document.querySelector(step.target);
    setBox(el ? el.getBoundingClientRect() : null);
  }, [i, step.target]);
  useEffect(() => {
    const key = (e: KeyboardEvent) => {
      if (e.key === "Escape") onDone();
      if (e.key === "ArrowRight" || e.key === "Enter") next();
    };
    window.addEventListener("keydown", key);
    return () => window.removeEventListener("keydown", key);
  });
  const next = () => {
    click(262 + i * 30, 0.35);
    if (i + 1 >= STEPS.length) onDone();
    else setI(i + 1);
  };
  const pad = 6;
  const hole = box ? { left: box.left - pad, top: box.top - pad, width: box.width + 2 * pad, height: box.height + 2 * pad } : null;
  const card: React.CSSProperties = !hole ? { left: "50%", top: "40%", transform: "translate(-50%, -50%)" }
    : step.place === "below" ? { left: Math.max(16, Math.min(window.innerWidth - 376, hole.left + hole.width / 2 - 180)), top: hole.top + hole.height + 14 }
    : step.place === "above" ? { left: Math.max(16, hole.left + hole.width / 2 - 180), top: hole.top - 14, transform: "translateY(-100%)" }
    : step.place === "right" ? { left: hole.left + hole.width + 14, top: Math.max(70, hole.top + 40) }
    : { left: hole.left - 374, top: hole.top + 10 };
  return (
    <div className="tour" role="dialog" aria-label="Tour">
      {hole && <div className="tour-hole" style={hole} />}
      {!hole && <div className="tour-dim" />}
      <div className="tour-card glass-strong" style={card} key={i}>
        <small>{i + 1} of {STEPS.length}</small>
        <b>{step.title}</b>
        <p>{step.body}</p>
        <div className="tour-actions">
          <button className="btn" onClick={onDone}>Skip</button>
          <span className="tour-dots">{STEPS.map((_, k) => <i key={k} className={k === i ? "on" : ""} />)}</span>
          <button className="btn primary" onClick={next}>{i + 1 === STEPS.length ? "Start designing" : "Next"}</button>
        </div>
      </div>
    </div>
  );
}
