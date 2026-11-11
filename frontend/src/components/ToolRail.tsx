"use client";

import { useEffect, useRef, useState } from "react";
import { click } from "@/lib/audio";
import type { Catalog } from "@/lib/ipc";
import type { Operation } from "@/lib/document";
import type { Settings } from "@/lib/settings";
import Icon from "./Icon";

const ALL = ["Sketch", "Primitive", "Modify", "Pattern", "Parts", "Structural", "Automotive", "Aerospace", "Lattice", "Boolean", "Robotics"];
/** Tool groups per discipline (order = rail order). */
export const WORKSPACE_GROUPS: Record<Settings["workspace"], string[]> = {
  all: ALL,
  mechanical: ["Sketch", "Primitive", "Modify", "Pattern", "Parts", "Structural", "Lattice", "Boolean"],
  robotics: ["Robotics", "Primitive", "Modify", "Parts", "Structural", "Boolean"],
  automotive: ["Automotive", "Sketch", "Primitive", "Modify", "Parts", "Pattern", "Boolean"],
  aerospace: ["Aerospace", "Sketch", "Primitive", "Modify", "Structural", "Lattice", "Boolean"],
};
const GROUP_HINT: Record<string, string> = {
  Sketch: "Draw a 2D outline and turn it into a solid", Primitive: "Ready-made basic shapes", Modify: "Change the part you already have",
  Pattern: "Repeat and mirror", Parts: "Standard machine elements", Structural: "Beams and profiles", Automotive: "Vehicle parts",
  Aerospace: "Wings and fairings", Lattice: "Lightweight internal structures", Boolean: "Combine bodies", Robotics: "Robot arms to pose and analyse",
};
/** One-line description of every tool. Simple mode shows only the tools listed with `simple`. */
export const TOOL_INFO: Record<string, { hint: string; simple?: boolean }> = {
  extrude: { hint: "Draw an outline, pull it into a solid", simple: true },
  revolve: { hint: "Spin an outline around an axis", simple: true },
  loft: { hint: "Blend between two outlines" },
  box: { hint: "Block with optional rounded edges", simple: true },
  cylinder: { hint: "Round bar, or a hole cutter", simple: true },
  sphere: { hint: "A ball", simple: true },
  torus: { hint: "A ring" },
  half_space: { hint: "Slice everything on one side away" },
  hole: { hint: "Standard screw hole, M3–M20", simple: true },
  shell: { hint: "Hollow to a wall thickness", simple: true },
  offset: { hint: "Grow or shrink the whole part" },
  texture: { hint: "Knurl, bumps or ribs on the surface" },
  transform: { hint: "Move or rotate a body" },
  mirror: { hint: "Copy across a plane" },
  pattern_linear: { hint: "Copies in a row" },
  pattern_circular: { hint: "Copies around a circle" },
  gear: { hint: "Spur gear from module and teeth", simple: true },
  thread: { hint: "Threaded rod, M3–M20", simple: true },
  spring: { hint: "Compression spring", simple: true },
  bearing: { hint: "Standard ball bearing", simple: true },
  beam: { hint: "I-beam, channel, angle, T or tube", simple: true },
  wing: { hint: "Wing from a NACA aerofoil", simple: true },
  nose_cone: { hint: "Rocket or fairing nose", simple: true },
  brake_disc: { hint: "Drilled brake rotor", simple: true },
  wheel: { hint: "Spoked road wheel", simple: true },
  gyroid_infill: { hint: "Fill a body with a light gyroid" },
  union: { hint: "Join bodies into one" },
  subtract: { hint: "Cut one body out of another" },
  intersect: { hint: "Keep only the overlap" },
  robot_arm: { hint: "Industrial arm to pose and check reach", simple: true },
};
const OPS: { op: Operation; label: string; icon: string; hint: string }[] = [
  { op: "add", label: "Add", icon: "union", hint: "Join the new shape to the part" },
  { op: "cut", label: "Cut", icon: "subtract", hint: "Cut the new shape out of the part" },
  { op: "intersect", label: "Keep overlap", icon: "intersect", hint: "Keep only where they overlap" },
  { op: "new", label: "New body", icon: "new", hint: "Start a separate body" },
];

export default function ToolRail({ catalog, op, onOp, onInsert, hasBody, mode, workspace, openGroup, onOpenGroup }: {
  catalog: Catalog | null;
  op: Operation;
  onOp: (op: Operation) => void;
  onInsert: (type: string) => void;
  hasBody: boolean;
  mode: Settings["mode"];
  workspace: Settings["workspace"];
  openGroup: string | null;
  onOpenGroup: (g: string | null) => void;
}) {
  const ref = useRef<HTMLDivElement>(null);
  const [filter, setFilter] = useState("");
  useEffect(() => {
    if (!openGroup) return;
    const away = (e: PointerEvent) => !ref.current?.contains(e.target as Node) && onOpenGroup(null);
    const esc = (e: KeyboardEvent) => e.key === "Escape" && onOpenGroup(null);
    window.addEventListener("pointerdown", away);
    window.addEventListener("keydown", esc);
    return () => (window.removeEventListener("pointerdown", away), window.removeEventListener("keydown", esc));
  }, [openGroup, onOpenGroup]);
  useEffect(() => setFilter(""), [openGroup]);
  if (!catalog) return <nav className="rail glass area-rail" />;

  const simple = mode === "simple";
  const tools = (group: string) => Object.entries(catalog.features)
    .filter(([type, s]) => s.group === group && !s.hidden && (!simple || TOOL_INFO[type]?.simple));
  const groups = WORKSPACE_GROUPS[workspace].filter((g) => tools(g).length > 0);
  const shown = openGroup === "*"
    ? Object.entries(catalog.features).filter(([type, s]) => !s.hidden && (!simple || TOOL_INFO[type]?.simple) &&
      `${s.label} ${TOOL_INFO[type]?.hint ?? ""} ${s.group}`.toLowerCase().includes(filter.toLowerCase()))
    : openGroup ? tools(openGroup) : [];

  return (
    <div ref={ref}>
      <nav className="rail glass area-rail" aria-label="Tools">
        <button className={`rail-btn${openGroup === "*" ? " on" : ""}`} title="All tools (search)" onClick={() => { click(240, 0.3); onOpenGroup(openGroup === "*" ? null : "*"); }}>
          <Icon name="search" size={20} />
        </button>
        <span className="rail-sep" />
        {groups.map((g) => (
          <button key={g} className={`rail-btn${openGroup === g ? " on" : ""}`} title={`${g}: ${GROUP_HINT[g] ?? ""}`}
            onClick={() => { click(220, 0.3); onOpenGroup(openGroup === g ? null : g); }}>
            <Icon name={g} size={20} />
            <small>{g}</small>
          </button>
        ))}
      </nav>
      {openGroup && (
        <div className="flyout glass-strong" role="dialog" aria-label={openGroup === "*" ? "All tools" : openGroup}>
          <header>
            {openGroup === "*" ? (
              <input autoFocus className="flyout-search" placeholder="Find a tool… (gear, hole, wing)" value={filter} onChange={(e) => setFilter(e.target.value)} />
            ) : (
              <div>
                <h2>{openGroup}</h2>
                <p>{GROUP_HINT[openGroup]}</p>
              </div>
            )}
            <div className="op-switch" role="group" aria-label="How new shapes combine">
              {OPS.map((o) => (
                <button key={o.op} aria-pressed={op === o.op} title={o.hint} disabled={!hasBody && o.op !== "new" && o.op !== "add"} onClick={() => onOp(o.op)}>
                  <Icon name={o.icon} size={15} /> {o.label}
                </button>
              ))}
            </div>
          </header>
          <div className="tool-grid">
            {shown.map(([type, spec]) => {
              const needsBody = spec.inputs !== 0 && !hasBody;
              return (
                <button key={type} className="tool-card" disabled={needsBody}
                  title={needsBody ? "Add a shape first: this tool changes an existing part" : TOOL_INFO[type]?.hint}
                  onClick={() => { onInsert(type); onOpenGroup(null); }}>
                  <span className="tool-icon"><Icon name={type} size={26} /></span>
                  <b>{spec.label}</b>
                  <span>{TOOL_INFO[type]?.hint ?? (spec.inputs === 0 ? "Insert" : "Apply to the part")}</span>
                </button>
              );
            })}
            {shown.length === 0 && <p className="note">No tool matches “{filter}”.</p>}
          </div>
          {simple && <footer>Simple mode shows the essentials. Switch to <b>Pro</b> in the title bar for every tool.</footer>}
        </div>
      )}
    </div>
  );
}
