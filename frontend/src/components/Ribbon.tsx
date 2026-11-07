"use client";

import type { Catalog } from "@/lib/ipc";
import type { Operation } from "@/lib/document";

const GROUPS = ["Sketch", "Primitive", "Modify", "Pattern", "Parts", "Lattice", "Boolean", "Robotics"];

export default function Ribbon({ catalog, op, onOp, onInsert, hasBody }: {
  catalog: Catalog | null;
  op: Operation;
  onOp: (op: Operation) => void;
  onInsert: (type: string) => void;
  hasBody: boolean;
}) {
  if (!catalog) return <div className="ribbon glass area-ribbon" />;
  return (
    <div className="ribbon glass area-ribbon">
      <div className="tabs" role="group" title="How a new primitive combines with the body">
        {(["add", "cut", "intersect", "new"] as Operation[]).map((o) => (
          <button key={o} aria-pressed={op === o} onClick={() => onOp(o)}>{{ add: "Add", cut: "Cut", intersect: "Intersect", new: "New body" }[o]}</button>
        ))}
      </div>
      {GROUPS.map((group) => {
        const items = Object.entries(catalog.features).filter(([, s]) => s.group === group);
        return (
          <div key={group} className="ribbon-group">
            <span className="ribbon-label">{group}</span>
            <div>
              {items.map(([type, spec]) => (
                <button key={type} className="btn" disabled={spec.inputs !== 0 && !hasBody} onClick={() => onInsert(type)}
                  title={spec.inputs === 0 ? `Insert ${spec.label} (${op})` : `Apply ${spec.label} to the current result`}>
                  {spec.label}
                </button>
              ))}
            </div>
          </div>
        );
      })}
    </div>
  );
}
