"use client";

import { useState } from "react";
import { TEMPLATES, type Domain } from "@/lib/templates";

const DOMAINS: { key: Domain | "all"; label: string }[] = [
  { key: "all", label: "All" }, { key: "mechanical", label: "Mechanical" }, { key: "robotics", label: "Robotics" },
  { key: "automotive", label: "Automotive" }, { key: "aerospace", label: "Aerospace" },
];

/** Starter designs grouped by discipline; every one opens as an ordinary editable feature tree. */
export default function TemplateGallery({ initial, onPick, onClose }: { initial: Domain | "all"; onPick: (key: string) => void; onClose: () => void }) {
  const [domain, setDomain] = useState<Domain | "all">(initial);
  const list = TEMPLATES.filter((t) => domain === "all" || t.domain === domain);
  return (
    <div className="modal-backdrop" onPointerDown={onClose}>
      <div className="modal glass-strong gallery" role="dialog" aria-label="Templates" onPointerDown={(e) => e.stopPropagation()}>
        <div className="panel-title">
          New from template
          <button className="icon" onClick={onClose} aria-label="Close">×</button>
        </div>
        <div className="modal-body">
          <div className="chips" role="tablist">
            {DOMAINS.map((d) => (
              <button key={d.key} role="tab" aria-selected={domain === d.key} onClick={() => setDomain(d.key)}>{d.label}</button>
            ))}
          </div>
          <div className="gallery-grid">
            {list.map((t) => (
              <button key={t.key} className={`welcome-card domain-${t.domain}`} onClick={() => onPick(t.key)}>
                <em>{t.domain}</em>
                <b>{t.label}</b>
                <span>{t.desc}</span>
              </button>
            ))}
          </div>
        </div>
      </div>
    </div>
  );
}
