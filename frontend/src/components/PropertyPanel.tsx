"use client";

import { useEffect, useState } from "react";
import type { Catalog, DatumDocument, Feature, ParamSpec } from "@/lib/ipc";
import { fromDisplay, toDisplay, type Settings } from "@/lib/settings";
import SketchEditor, { type ProfilePt } from "./SketchEditor";
import { evalExpr, isExpr, resolveParameters } from "@/lib/params";
import ParametersTable from "./ParametersTable";

type Lookup = (name: string) => number;

function NumberField({ value, onCommit, unit, settings, min, lookup }: { value: number | string; onCommit: (v: number | string) => void; unit?: string; settings: Settings; min?: number; lookup?: Lookup }) {
  const isLength = unit === "mm";
  const expr = isExpr(value) ? value : null;
  let resolved = 0, bad = false;
  try {
    resolved = expr ? evalExpr(expr, lookup ?? (() => { throw new Error("no parameters"); })) : Number(value);
  } catch {
    bad = true;
  }
  const shown = isLength ? toDisplay(resolved, settings) : resolved;
  const initial = expr ?? String(+shown.toFixed(6));
  const [text, setText] = useState(initial);
  useEffect(() => setText(initial), [initial]);
  const commit = () => {
    // "=expr" keeps a live link to the parameters (expressions are in mm / native units)
    if (isExpr(text)) {
      if (text.trim() !== expr) onCommit(text.trim());
      return;
    }
    const v = Number(text);
    if (!Number.isFinite(v)) return setText(String(+shown.toFixed(6)));
    const mm = isLength ? fromDisplay(v, settings) : v;
    if (min !== undefined && mm < min) return setText(String(+shown.toFixed(6)));
    if (mm !== value) onCommit(mm);
  };
  const title = expr ? (bad ? "Expression error" : `= ${+resolved.toFixed(6)} ${unit ?? ""}`) : "Type =name to link a parameter";
  return (
    <span className="field">
      <input className={`mono${expr ? " linked" : ""}${bad ? " invalid" : ""}`} value={text} title={title} onChange={(e) => setText(e.target.value)} onBlur={commit}
        onKeyDown={(e) => {
          if (e.key === "Enter") (e.target as HTMLInputElement).blur();
          if (e.key === "ArrowUp" || e.key === "ArrowDown") {
            e.preventDefault();
            const step = (e.shiftKey ? 10 : 1) * (e.key === "ArrowUp" ? 1 : -1) * (isLength && settings.units === "in" ? 0.1 : 1);
            if (expr) return;
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

function ParamRow({ name, spec, value, onChange, settings, featureType, lookup }: { name: string; spec: ParamSpec; value: unknown; onChange: (v: unknown) => void; settings: Settings; featureType?: string; lookup: Lookup }) {
  if (spec.kind === "file") {
    const path = String(value ?? "");
    return (
      <label className="prop">
        <span>{spec.label}</span>
        <span className="file-path" title={path}>{path.split(/[\\/]/).pop() || "—"}</span>
      </label>
    );
  }
  if (spec.kind === "profile")
    return (
      <div className="prop-profile">
        <span>{spec.label}</span>
        <SketchEditor value={((value ?? spec.default) as ProfilePt[]).map((p) => [p[0], p[1], p[2] ?? 0] as ProfilePt)} onCommit={onChange} settings={settings}
          axisHint={featureType === "revolve" ? "Revolves about the Y axis (x = radius)" : "Extrudes along +Z of the sketch plane"} />
      </div>
    );
  if (spec.kind === "number")
    return (
      <label className="prop">
        <span>{spec.label}</span>
        <NumberField value={isExpr(value) ? value : Number(value ?? spec.default)} onCommit={onChange} unit={spec.unit} settings={settings} min={spec.min} lookup={lookup} />
      </label>
    );
  if (spec.kind === "vec3") {
    const v = (Array.isArray(value) ? value : spec.default) as (number | string)[];
    const unit = name === "rotation" || spec.label.includes("(deg)") ? "deg" : name === "normal" ? "" : "mm";
    return (
      <div className="prop vec">
        <span>{spec.label}</span>
        <div>
          {["X", "Y", "Z"].map((axis, i) => (
            <NumberField key={axis} value={v[i]} unit={unit || undefined} settings={settings} lookup={lookup}
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
  const { values } = resolveParameters(doc.parameters);
  const lookup: Lookup = (n) => {
    if (n in values) return values[n];
    throw new Error(`unknown parameter ${n}`);
  };
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
        <ParametersTable parameters={doc.parameters ?? {}} onChange={(parameters) => onDocChange({ parameters })} />
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
        <ParamRow key={name} name={name} spec={p} value={f.params?.[name]} settings={settings} featureType={f.type} lookup={lookup}
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
