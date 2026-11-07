"use client";

import { useState } from "react";
import { resolveParameters } from "@/lib/params";

/** Editable table of named design values; expressions may reference other parameters. */
export default function ParametersTable({ parameters, onChange }: {
  parameters: Record<string, string | number>;
  onChange: (p: Record<string, string | number>) => void;
}) {
  const { values, errors } = resolveParameters(parameters);
  const [draft, setDraft] = useState({ name: "", expr: "" });
  const entries = Object.entries(parameters);
  const valid = /^[A-Za-z_][A-Za-z0-9_]*$/.test(draft.name) && !(draft.name in parameters) && draft.expr.trim() !== "";

  const set = (name: string, expr: string) => {
    const n = Number(expr);
    onChange({ ...parameters, [name]: expr.trim() !== "" && Number.isFinite(n) ? n : expr.trim() });
  };
  const add = () => {
    if (!valid) return;
    set(draft.name, draft.expr);
    setDraft({ name: "", expr: "" });
  };

  return (
    <div className="params-table">
      <h3>Parameters</h3>
      <p className="note">Reference them in any number field by typing <code>=name</code>, e.g. <code>=width / 2 - wall</code>.</p>
      {entries.map(([name, raw]) => (
        <div key={name} className={`param-row${errors[name] ? " invalid" : ""}`} title={errors[name] ?? ""}>
          <span className="mono name">{name}</span>
          <input className="mono" defaultValue={String(raw)} key={`${name}:${raw}`}
            onBlur={(e) => e.target.value !== String(raw) && set(name, e.target.value)}
            onKeyDown={(e) => e.key === "Enter" && (e.target as HTMLInputElement).blur()} />
          <span className="mono value">{errors[name] ? "error" : +values[name].toFixed(4)}</span>
          <button className="icon" title={`Delete ${name}`} onClick={() => {
            const next = { ...parameters };
            delete next[name];
            onChange(next);
          }}>×</button>
        </div>
      ))}
      <div className="param-row add">
        <input className="mono name" placeholder="name" value={draft.name} onChange={(e) => setDraft({ ...draft, name: e.target.value })} />
        <input className="mono" placeholder="value or expression" value={draft.expr} onChange={(e) => setDraft({ ...draft, expr: e.target.value })}
          onKeyDown={(e) => e.key === "Enter" && add()} />
        <button className="btn" disabled={!valid} onClick={add}>Add</button>
      </div>
    </div>
  );
}
