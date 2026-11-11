"use client";

import { useState } from "react";
import type { Catalog, DatumDocument } from "@/lib/ipc";
import Icon from "./Icon";


export default function FeatureTree({ doc, catalog, selected, onSelect, onRename, onToggle, onDelete, warnings }: {
  doc: DatumDocument;
  catalog: Catalog | null;
  selected: string | null;
  onSelect: (id: string | null) => void;
  onRename: (id: string, name: string) => void;
  onToggle: (id: string) => void;
  onDelete: (id: string) => void;
  warnings: string[];
}) {
  const [editing, setEditing] = useState<string | null>(null);
  const used = new Set(doc.features.flatMap((f) => f.inputs ?? []));

  return (
    <aside className="glass area-tree scroll" onKeyDown={(e) => {
      if (e.key === "Delete" && selected && !editing) onDelete(selected);
      if (e.key === "F2" && selected) setEditing(selected);
    }} tabIndex={0}>
      <div className="panel-title">
        Feature tree
        <span className="muted mono">{doc.features.length}</span>
      </div>
      {doc.features.length === 0 && <div className="note pad">Your features appear here as you add them, top to bottom.</div>}
      <div className="tree-group">{doc.name ?? "Part"} · {catalog?.materials.find((m) => m.key === doc.material)?.name ?? doc.material}</div>
      {doc.features.map((f) => {
        const spec = catalog?.features[f.type];
        const isRoot = f.id === doc.root;
        const orphan = !isRoot && !used.has(f.id);
        return (
          <div
            key={f.id}
            className={`tree-row ${selected === f.id ? "hot" : ""} ${f.suppressed ? "suppressed" : ""}`}
            onClick={() => onSelect(f.id)}
            onDoubleClick={() => setEditing(f.id)}
            title={`${spec?.label ?? f.type} · ${f.id}${f.inputs?.length ? " ← " + f.inputs.join(", ") : ""}`}
          >
            <span className="glyph"><Icon name={f.type} size={15} /></span>
            {editing === f.id ? (
              <input
                autoFocus
                defaultValue={f.name ?? f.id}
                onBlur={(e) => { onRename(f.id, e.target.value.trim() || f.id); setEditing(null); }}
                onKeyDown={(e) => {
                  if (e.key === "Enter") (e.target as HTMLInputElement).blur();
                  if (e.key === "Escape") setEditing(null);
                  e.stopPropagation();
                }}
              />
            ) : (
              <span className="label">{f.name ?? f.id}</span>
            )}
            {isRoot && <span className="tag">result</span>}
            {orphan && !f.suppressed && <span className="tag dim" title="Not used by the result">unused</span>}
            <button className="icon" title={f.suppressed ? "Unsuppress" : "Suppress"} onClick={(e) => { e.stopPropagation(); onToggle(f.id); }}>
              {f.suppressed ? "○" : "●"}
            </button>
          </div>
        );
      })}
      {warnings.length > 0 && <div className="note pad warn">{warnings.join(" · ")}</div>}
    </aside>
  );
}
