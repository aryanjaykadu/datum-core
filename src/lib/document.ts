import type { Catalog, DatumDocument, Feature } from "./ipc";

/** A fresh id for a feature type: box1, box2, ... */
export function newId(doc: DatumDocument, type: string): string {
  const used = new Set(doc.features.map((f) => f.id));
  for (let i = 1; ; i++) if (!used.has(`${type}${i}`)) return `${type}${i}`;
}

export function defaults(catalog: Catalog, type: string): Record<string, unknown> {
  const spec = catalog.features[type];
  return Object.fromEntries(Object.entries(spec?.params ?? {}).map(([k, p]) => [k, structuredClone(p.default)]));
}

export const clone = (d: DatumDocument): DatumDocument => structuredClone(d);

export type Operation = "add" | "cut" | "intersect" | "new";

/**
 * Insert a feature the way feature-based CAD does: a primitive is combined with the
 * current body (add / cut / intersect) and the combination becomes the new root;
 * a modifier (shell, pattern, infill, ...) is applied to the current body.
 */
export function insertFeature(doc: DatumDocument, catalog: Catalog, type: string, op: Operation = "add", params?: Record<string, unknown>) {
  const d = clone(doc);
  const spec = catalog.features[type];
  const id = newId(d, type);
  const label = spec?.label ?? type;
  const f: Feature = { id, type, name: `${label} ${id.replace(type, "")}`, params: { ...defaults(catalog, type), ...params } };
  const hasBody = d.features.length > 0;
  if (spec && spec.inputs !== 0) {
    if (!hasBody) throw new Error(`${label} needs an existing body`);
    f.inputs = [d.root];
    d.features.push(f);
    d.root = id;
    return { doc: d, selected: id };
  }
  d.features.push(f);
  if (!hasBody || op === "new") {
    d.root = id;
    return { doc: d, selected: id };
  }
  const boolType = op === "add" ? "union" : op === "cut" ? "subtract" : "intersect";
  const bid = newId(d, boolType);
  d.features.push({ id: bid, type: boolType, name: `${op === "add" ? "Add" : op === "cut" ? "Cut" : "Intersect"} ${f.name}`, inputs: [d.root, id], params: { blend: 0 } });
  d.root = bid;
  return { doc: d, selected: id };
}

/** Remove a feature; anything that used it now uses its first input instead. */
export function deleteFeature(doc: DatumDocument, id: string): DatumDocument {
  const d = clone(doc);
  const victim = d.features.find((f) => f.id === id);
  if (!victim) return d;
  const replacement = victim.inputs?.[0];
  d.features = d.features.filter((f) => f.id !== id);
  for (const f of d.features) {
    if (!f.inputs?.includes(id)) continue;
    f.inputs = f.inputs.flatMap((i) => (i === id ? (replacement ? [replacement] : []) : [i]));
  }
  // a boolean left with one input is a pass-through; that is fine for the kernel
  if (d.root === id) d.root = replacement ?? d.features[d.features.length - 1]?.id ?? "";
  return d;
}

export function updateFeature(doc: DatumDocument, id: string, patch: Partial<Feature>): DatumDocument {
  const d = clone(doc);
  const f = d.features.find((x) => x.id === id);
  if (f) Object.assign(f, patch);
  return d;
}

export function emptyDocument(): DatumDocument {
  return { version: 1, units: "mm", name: "Untitled", material: "al6061t6", root: "", features: [] };
}

/** Linear undo/redo history of whole documents (documents are small JSON). */
export class History {
  private past: DatumDocument[] = [];
  private future: DatumDocument[] = [];
  push(previous: DatumDocument) {
    this.past.push(previous);
    if (this.past.length > 200) this.past.shift();
    this.future = [];
  }
  undo(current: DatumDocument): DatumDocument | null {
    const prev = this.past.pop();
    if (prev) this.future.push(current);
    return prev ?? null;
  }
  redo(current: DatumDocument): DatumDocument | null {
    const next = this.future.pop();
    if (next) this.past.push(current);
    return next ?? null;
  }
  clear() {
    this.past = [];
    this.future = [];
  }
  get canUndo() {
    return this.past.length > 0;
  }
  get canRedo() {
    return this.future.length > 0;
  }
}
