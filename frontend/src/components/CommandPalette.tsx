"use client";

import { useEffect, useMemo, useRef, useState } from "react";
import type { Catalog } from "@/lib/ipc";
import type { MenuItem, Menus } from "./MenuBar";

export interface Intent {
  label: string;
  hint?: string;
  run: () => void;
}

/**
 * Turn a typed instruction into concrete actions. Deterministic parsing (no AI service):
 * every intent maps to a real kernel operation, and anything it cannot honour says so.
 */
export function parseIntents(text: string, catalog: Catalog | null, act: {
  lighten: (pct: number) => void;
  insert: (type: string, op: "add" | "cut" | "intersect" | "new", params?: Record<string, unknown>) => void;
  material: (key: string) => void;
  view: (name: string) => void;
}): Intent[] {
  const t = text.trim().toLowerCase();
  if (!t) return [];
  const out: Intent[] = [];
  const num = (re: RegExp) => {
    const m = t.match(re);
    return m ? Number(m[1]) : null;
  };

  const pct = num(/(?:light(?:en|weight)?|reduce (?:mass|weight)|lose)\D*?(\d+(?:\.\d+)?)\s*%/) ?? num(/(\d+(?:\.\d+)?)\s*%\s*(?:lighter|less mass|lighter part)/);
  if (pct !== null && pct > 0 && pct < 95) {
    const load = t.match(/(\d+(?:\.\d+)?)\s*(k?n)\b/);
    out.push({
      label: `Lighten the part by ${pct}% with a gyroid infill`,
      hint: load ? `The ${load[1]} ${load[2].toUpperCase()} load is not verified: no FEA solver yet. Mass target only.` : "Searches the infill wall thickness to hit the target mass",
      run: () => act.lighten(pct),
    });
  }
  const shell = num(/shell\D*?(\d+(?:\.\d+)?)/);
  if (shell !== null && shell > 0) out.push({ label: `Shell the part, ${shell} mm wall`, run: () => act.insert("shell", "add", { thickness: shell }) });
  const offset = num(/offset\D*?(-?\d+(?:\.\d+)?)/);
  if (offset !== null) out.push({ label: `Offset the surface by ${offset} mm`, run: () => act.insert("offset", "add", { distance: offset }) });

  const op = /\b(cut|hole|subtract|remove)\b/.test(t) ? "cut" : /\bintersect\b/.test(t) ? "intersect" : "add";
  for (const [type, spec] of Object.entries(catalog?.features ?? {})) {
    if (spec.inputs !== 0) continue;
    if (t.includes(spec.label.toLowerCase()) || t.includes(type)) {
      out.push({ label: `${op === "cut" ? "Cut" : op === "intersect" ? "Intersect with" : "Add"} a ${spec.label.toLowerCase()}`, run: () => act.insert(type, op) });
    }
  }
  if (/\bmaterial\b|\btitanium\b|\baluminium\b|\baluminum\b|\bsteel\b|\bnylon\b/.test(t)) {
    for (const m of catalog?.materials ?? []) {
      const words = m.name.toLowerCase().replace("aluminium", "aluminium aluminum").split(/[\s-]+/);
      if (words.some((w) => w.length > 3 && t.includes(w))) out.push({ label: `Set material: ${m.name}`, run: () => act.material(m.key) });
    }
  }
  const view = t.match(/\b(iso|isometric|front|back|left|right|top|bottom)\b/);
  if (view && /\bview\b|\blook\b|\bshow\b/.test(t)) out.push({ label: `View from ${view[1]}`, run: () => act.view(view[1] === "isometric" ? "iso" : view[1]) });
  return out;
}

export default function CommandPalette({ menus, intents, onClose }: { menus: Menus; intents: (text: string) => Intent[]; onClose: () => void }) {
  const [text, setText] = useState("");
  const [index, setIndex] = useState(0);
  const input = useRef<HTMLInputElement>(null);
  useEffect(() => input.current?.focus(), []);

  const items = useMemo(() => {
    const words = text.toLowerCase().split(/\s+/).filter(Boolean);
    const commands: Intent[] = Object.entries(menus).flatMap(([menu, list]) =>
      list
        .filter((i: MenuItem) => !i.separator && !i.disabled && i.action && i.label)
        .map((i) => ({ label: `${menu} › ${i.label!.trim()}`, hint: i.shortcut, run: i.action! })),
    );
    const matched = words.length ? commands.filter((c) => words.every((w) => c.label.toLowerCase().includes(w))) : commands;
    return [...intents(text), ...matched].slice(0, 12);
  }, [text, menus, intents]);

  useEffect(() => setIndex(0), [text]);

  const run = (i: number) => {
    const item = items[i];
    if (!item) return;
    onClose();
    item.run();
  };

  return (
    <div className="modal-backdrop palette-backdrop" onPointerDown={onClose}>
      <div className="palette glass-strong" onPointerDown={(e) => e.stopPropagation()}>
        <input
          ref={input}
          className="palette-input"
          placeholder="Type a command, e.g. “lighten 20%”, “cut cylinder”, “material titanium”, “export”"
          value={text}
          onChange={(e) => setText(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === "Escape") onClose();
            else if (e.key === "ArrowDown") (e.preventDefault(), setIndex((i) => Math.min(i + 1, items.length - 1)));
            else if (e.key === "ArrowUp") (e.preventDefault(), setIndex((i) => Math.max(i - 1, 0)));
            else if (e.key === "Enter") run(index);
          }}
        />
        <div className="palette-list">
          {items.length === 0 && <div className="note pad">No matching command.</div>}
          {items.map((item, i) => (
            <button key={item.label + i} className={i === index ? "active" : ""} onPointerEnter={() => setIndex(i)} onClick={() => run(i)}>
              <span>{item.label}</span>
              {item.hint && <small>{item.hint}</small>}
            </button>
          ))}
        </div>
      </div>
    </div>
  );
}
