"use client";

import type { Catalog } from "@/lib/ipc";
import type { Operation } from "@/lib/document";
import type { Settings } from "@/lib/settings";

const ALL = ["Sketch", "Primitive", "Modify", "Pattern", "Parts", "Structural", "Automotive", "Aerospace", "Lattice", "Boolean", "Robotics"];
/** Tool groups per discipline (order = ribbon order). */
export const WORKSPACE_GROUPS: Record<Settings["workspace"], string[]> = {
  all: ALL,
  mechanical: ["Sketch", "Primitive", "Modify", "Pattern", "Parts", "Structural", "Lattice", "Boolean"],
  robotics: ["Robotics", "Primitive", "Modify", "Parts", "Structural", "Boolean"],
  automotive: ["Automotive", "Sketch", "Primitive", "Modify", "Parts", "Pattern", "Boolean"],
  aerospace: ["Aerospace", "Sketch", "Primitive", "Modify", "Structural", "Lattice", "Boolean"],
};
/** Simple mode: the tools a first model needs, each with a one-line explanation. */
const SIMPLE: Record<string, string> = {
  box: "A block with optional rounded edges", cylinder: "A round bar or a hole cutter", sphere: "A ball",
  extrude: "Draw a 2D outline, pull it into a solid", revolve: "Spin a 2D outline around an axis",
  hole: "A standard screw hole (M3–M20)", shell: "Hollow the part to a wall thickness",
  gear: "A spur gear from module and teeth", thread: "A threaded rod (M3–M20)", spring: "A compression spring", bearing: "A standard ball bearing",
  beam: "An I-beam, channel, angle or tube", wing: "A wing from a NACA aerofoil", nose_cone: "A rocket or fairing nose",
  brake_disc: "A drilled brake rotor", wheel: "A spoked road wheel", robot_arm: "An industrial robot arm to pose",
};

export default function Ribbon({ catalog, op, onOp, onInsert, hasBody, mode, workspace }: {
  catalog: Catalog | null;
  op: Operation;
  onOp: (op: Operation) => void;
  onInsert: (type: string) => void;
  hasBody: boolean;
  mode: Settings["mode"];
  workspace: Settings["workspace"];
}) {
  if (!catalog) return <div className="ribbon glass area-ribbon" />;
  const simple = mode === "simple";
  return (
    <div className={`ribbon glass area-ribbon${simple ? " simple" : ""}`}>
      <div className="tabs" role="group" title="How a new shape combines with the part">
        {(["add", "cut", "intersect", "new"] as Operation[]).map((o) => (
          <button key={o} aria-pressed={op === o} onClick={() => onOp(o)}>{{ add: "Add", cut: "Cut", intersect: "Intersect", new: "New body" }[o]}</button>
        ))}
      </div>
      {WORKSPACE_GROUPS[workspace].map((group) => {
        const items = Object.entries(catalog.features).filter(([type, s]) => s.group === group && !s.hidden && (!simple || type in SIMPLE));
        if (!items.length) return null;
        return (
          <div key={group} className="ribbon-group">
            <span className="ribbon-label">{group}</span>
            <div>
              {items.map(([type, spec]) => (
                <button key={type} className="btn" disabled={spec.inputs !== 0 && !hasBody} onClick={() => onInsert(type)}
                  title={SIMPLE[type] ?? (spec.inputs === 0 ? `Insert ${spec.label} (${op})` : `Apply ${spec.label} to the current result`)}>
                  {spec.label}
                  {simple && <small>{SIMPLE[type]}</small>}
                </button>
              ))}
            </div>
          </div>
        );
      })}
    </div>
  );
}
