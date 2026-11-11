"use client";

import { useMemo, useState } from "react";
import { CALCULATORS } from "@/lib/calc";

/** Engineering hand calculations; results update as you type. */
export default function CalculatorsDialog({ onClose, initial }: { onClose: () => void; initial?: string }) {
  const [key, setKey] = useState(initial && CALCULATORS.some((c) => c.key === initial) ? initial : CALCULATORS[0].key);
  const calc = CALCULATORS.find((c) => c.key === key)!;
  const [values, setValues] = useState<Record<string, Record<string, string>>>({});
  const text = (field: string, fallback: number) => values[key]?.[field] ?? String(fallback);
  const results = useMemo(() => {
    const v = Object.fromEntries(calc.fields.map((f) => [f.key, Number(text(f.key, f.value))]));
    if (Object.values(v).some((x) => !Number.isFinite(x))) return { error: "Enter a number in every field." };
    try {
      return { rows: calc.run(v) };
    } catch (e) {
      return { error: (e as Error).message };
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [calc, values]);

  return (
    <div className="modal-backdrop" onPointerDown={onClose}>
      <div className="modal glass-strong calc" role="dialog" aria-label="Engineering calculators" onPointerDown={(e) => e.stopPropagation()}>
        <div className="panel-title">
          Engineering calculators
          <button className="icon" onClick={onClose} aria-label="Close">×</button>
        </div>
        <div className="calc-body">
          <nav className="calc-nav">
            {CALCULATORS.map((c) => (
              <button key={c.key} aria-pressed={c.key === key} onClick={() => setKey(c.key)}>
                {c.label}
                <small>{c.domain}</small>
              </button>
            ))}
          </nav>
          <div className="calc-main">
            <p className="note">{calc.note}</p>
            <div className="calc-fields">
              {calc.fields.map((f) => (
                <label key={f.key} className="prop">
                  <span>{f.label}</span>
                  {f.options ? (
                    <select value={text(f.key, f.value)} onChange={(e) => setValues({ ...values, [key]: { ...values[key], [f.key]: e.target.value } })}>
                      {f.options.map((o) => <option key={o.value} value={o.value}>{o.label}</option>)}
                    </select>
                  ) : (
                    <span className="field">
                      <input className="mono" inputMode="decimal" value={text(f.key, f.value)}
                        onChange={(e) => setValues({ ...values, [key]: { ...values[key], [f.key]: e.target.value } })} />
                      {f.unit && <em>{f.unit}</em>}
                    </span>
                  )}
                </label>
              ))}
            </div>
            <div className="calc-results mono">
              {"error" in results
                ? <div className="kv"><span className="bad">{results.error}</span></div>
                : results.rows.map((r) => (
                  <div key={r.label} className="kv"><span>{r.label}</span><span className={r.warn ? "bad" : ""}>{r.value}</span></div>
                ))}
            </div>
            <p className="note small">Textbook closed-form formulas for sizing. Verify critical parts with a full analysis.</p>
          </div>
        </div>
      </div>
    </div>
  );
}
