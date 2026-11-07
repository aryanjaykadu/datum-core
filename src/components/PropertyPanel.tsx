"use client";

import { useEffect, useState } from "react";
import type { Catalog, DatumDocument, Feature, ParamSpec } from "@/lib/ipc";
import { fromDisplay, toDisplay, type Settings } from "@/lib/settings";

function NumberField({ value, onCommit, unit, settings, min }: { value: number; onCommit: (v: number) => void; unit?: string; settings: Settings; min?: number }) {
  const isLength = unit === "mm";
  const shown = isLength ? toDisplay(value, settings) : value;
  const [text, setText] = useState(String(+shown.toFixed(6)));
  useEffect(() => setText(String(+shown.toFixed(6))), [shown]);
  const commit = () => {
    const v = Number(text);
    if (!Number.isFinite(v)) return setText(String(+shown.toFixed(6)));
    const mm = isLength ? fromDisplay(v, settings) : v;
    if (min !== undefined && mm < min) return setText(String(+shown.toFixed(6)));
    if (mm !== value) onCommit(mm);
  };
  return (
    <span className="field">
      <input className="mono" value={text} inputMode="decimal" onChange={(e) => setText(e.target.value)} onBlur={commit}
        onKeyDown={(e) => {
          if (e.key === "Enter") (e.target as HTMLInputElement).blur();
          if (e.key === "ArrowUp" || e.key === "ArrowDown") {
            e.preventDefault();
            const step = (e.shiftKey ? 10 : 1) * (e.key === "ArrowUp" ? 1 : -1) * (isLength && settings.units === "in" ? 0.1 : 1);
            const v = (Number(text) || 0) + step;
            setText(String(+v.toFixed(6)));
            const mm = isLength ? fromDisplay(v, settings) : v;
            if (min === undefined || mm >= min) onCommit(mm);
          }
        }} />
      {unit && <em>{isLength ? settings.units : unit}</em>}
    </span>
  );
}

function ParamRow({ name, spec, value, onChange, settings }: { name: string; spec: ParamSpec; value: unknown; onChange: (v: unknown) => void; settings: Settings }) {
  if (spec.kind === "number")
    return (
      <label className="prop">
        <span>{spec.label}</span>
        <NumberField value={Number(value ?? spec.default)} onCommit={onChange} unit={spec.unit} settings={settings} min={spec.min} />
      </label>
    );
  if (spec.kind === "vec3") {
    const v = (Array.isArray(value) ? value : spec.default) as number[];
    const unit = name === "rotation" || spec.label.includes("(deg)") ? "deg" : name === "normal" ? "" : "mm";
    return (
      <div className="prop vec">
        <span>{spec.label}</span>
        <div>
          {["X", "Y", "Z"].map((axis, i) => (
            <NumberField key={axis} value={v[i]} unit={unit || undefined} settings={settings}
              onCommit={(x) => onChange(v.map((c, j) => (j === i ? x : c)))} />
          ))}
        </div>
      </div>
    );
  }
  return (
    <label className="prop">
      <span>{spec.label}</span>
      <select value={String(value ?? spec.default)} onChange={(e) => onChange(e.target.value)}>
        {spec.options?.map((o) => <option key={o}>{o}</option>)}
      </select>
    </label>
  );
}

export default function PropertyPanel({ doc, catalog, selected, settings, onChange, onDocChange, onDelete }: {
  doc: DatumDocument;
  catalog: Catalog | null;
  selected: string | null;
  settings: Settings;
  onChange: (id: string, patch: Partial<Feature>) => void;
  onDocChange: (patch: Partial<DatumDocument>) => void;
  onDelete: (id: string) => void;
}) {
  const f = doc.features.find((x) => x.id === selected);
  if (!catalog) return null;

  if (!f)
    return (
      <div className="section">
        <h3>Part</h3>
        <label className="prop">
          <span>Name</span>
          <input defaultValue={doc.name ?? ""} key={doc.name} onBlur={(e) => e.target.value !== doc.name && onDocChange({ name: e.target.value })} />
        </label>
        <label className="prop">
          <span>Material</span>
          <select value={doc.material} onChange={(e) => onDocChange({ material: e.target.value })}>
            {catalog.materials.map((m) => <option key={m.key} value={m.key}>{m.name}</option>)}
          </select>
        </label>
        <label className="prop">
          <span>Result feature</span>
          <select value={doc.root} onChange={(e) => onDocChange({ root: e.target.value })}>
            {doc.features.map((x) => <option key={x.id} value={x.id}>{x.name ?? x.id}</option>)}
          </select>
        </label>
      </div>
    );

  const spec = catalog.features[f.type];
  const others = doc.features.filter((x) => x.id !== f.id);
  const inputs = f.inputs ?? [];
  const multi = spec?.inputs === "2+";
  return (
    <div className="section">
      <h3>
        {spec?.label ?? f.type} <span className="muted mono">{f.id}</span>
      </h3>
      {spec && Object.entries(spec.params).map(([name, p]) => (
        <ParamRow key={name} name={name} spec={p} value={f.params?.[name]} settings={settings}
          onChange={(v) => onChange(f.id, { params: { ...f.params, [name]: v } })} />
      ))}
      {spec && spec.inputs !== 0 && (
        <div className="prop-inputs">
          <span>Inputs</span>
          {inputs.map((input, i) => (
            <div key={i} className="input-row">
              <select value={input} onChange={(e) => onChange(f.id, { inputs: inputs.map((x, j) => (j === i ? e.target.value : x)) })}>
                {others.map((o) => <option key={o.id} value={o.id}>{o.name ?? o.id}</option>)}
              </select>
              {multi && inputs.length > 2 && <button className="icon" title="Remove input" onClick={() => onChange(f.id, { inputs: inputs.filter((_, j) => j !== i) })}>×</button>}
            </div>
          ))}
          {multi && others.length > 0 && <button className="btn" onClick={() => onChange(f.id, { inputs: [...inputs, others[0].id] })}>+ Input</button>}
        </div>
      )}
      <div className="row-actions">
        <button className="btn" onClick={() => onChange(f.id, { suppressed: !f.suppressed })}>{f.suppressed ? "Unsuppress" : "Suppress"}</button>
        <button className="btn danger" onClick={() => onDelete(f.id)}>Delete</button>
      </div>
    </div>
  );
}
