import type { DatumDocument, Feature } from "./ipc";

/**
 * Design parameters: named values (`width = 80`, `wall = width / 40`) that any numeric
 * feature field can reference by starting its text with "=" (e.g. "=width / 2").
 * A small recursive-descent parser: numbers, + - * / ^, parentheses, names and a few
 * functions. No eval.
 */

const FUNCS: Record<string, (...a: number[]) => number> = {
  sqrt: Math.sqrt, abs: Math.abs, sin: (d) => Math.sin((d * Math.PI) / 180), cos: (d) => Math.cos((d * Math.PI) / 180),
  tan: (d) => Math.tan((d * Math.PI) / 180), min: Math.min, max: Math.max, round: Math.round, floor: Math.floor, ceil: Math.ceil,
};
const CONSTS: Record<string, number> = { pi: Math.PI };

export function isExpr(v: unknown): v is string {
  return typeof v === "string" && v.trimStart().startsWith("=");
}

export function evalExpr(src: string, lookup: (name: string) => number): number {
  const s = src.trim().replace(/^=/, "");
  let i = 0;
  const ws = () => { while (s[i] === " " || s[i] === "\t") i++; };
  const fail = (msg: string): never => { throw new Error(`${msg} in "${src}"`); };
  const peek = () => (ws(), s[i]);

  function primary(): number {
    const c = peek();
    if (c === "(") {
      i++;
      const v = sum();
      if (peek() !== ")") fail("missing )");
      i++;
      return v;
    }
    if (c === "-") { i++; return -power(); }
    if (c === "+") { i++; return power(); }
    const num = /^(\d+\.?\d*|\.\d+)(e[+-]?\d+)?/i.exec(s.slice(i));
    if (num) { i += num[0].length; return Number(num[0]); }
    const id = /^[A-Za-z_][A-Za-z0-9_]*/.exec(s.slice(i));
    if (!id) return fail(c === undefined ? "unexpected end" : `unexpected "${c}"`);
    i += id[0].length;
    const name = id[0];
    if (peek() === "(") {
      const fn = FUNCS[name.toLowerCase()];
      if (!fn) fail(`unknown function ${name}`);
      i++;
      const args: number[] = [];
      if (peek() !== ")") {
        args.push(sum());
        while (peek() === ",") { i++; args.push(sum()); }
      }
      if (peek() !== ")") fail("missing )");
      i++;
      return fn(...args);
    }
    if (name.toLowerCase() in CONSTS) return CONSTS[name.toLowerCase()];
    return lookup(name);
  }
  function power(): number {
    const b = primary();
    if (peek() === "^") { i++; return Math.pow(b, power()); }
    return b;
  }
  function product(): number {
    let v = power();
    for (let c = peek(); c === "*" || c === "/"; c = peek()) {
      i++;
      v = c === "*" ? v * power() : v / power();
    }
    return v;
  }
  function sum(): number {
    let v = product();
    for (let c = peek(); c === "+" || c === "-"; c = peek()) {
      i++;
      v = c === "+" ? v + product() : v - product();
    }
    return v;
  }
  const v = sum();
  if (peek() !== undefined) fail(`unexpected "${s[i]}"`);
  if (!Number.isFinite(v)) fail("result is not a finite number");
  return v;
}

/** Values of all parameters (each may reference others); cycles and unknown names are errors. */
export function resolveParameters(params: Record<string, string | number> | undefined): { values: Record<string, number>; errors: Record<string, string> } {
  const values: Record<string, number> = {};
  const errors: Record<string, string> = {};
  const visiting = new Set<string>();
  const get = (name: string): number => {
    if (name in values) return values[name];
    if (!params || !(name in params)) throw new Error(`unknown parameter "${name}"`);
    if (visiting.has(name)) throw new Error(`"${name}" refers to itself`);
    visiting.add(name);
    try {
      const raw = params[name];
      const v = typeof raw === "number" ? raw : evalExpr(String(raw).startsWith("=") ? String(raw) : `=${raw}`, get);
      values[name] = v;
      return v;
    } finally {
      visiting.delete(name);
    }
  };
  for (const name of Object.keys(params ?? {})) {
    try { get(name); } catch (e) { errors[name] = (e as Error).message; }
  }
  return { values, errors };
}

/** Document as the kernel sees it: every "=expr" replaced by its number, parameters removed. */
export function resolveDocument(doc: DatumDocument): { doc: DatumDocument; errors: string[] } {
  const { values, errors: perr } = resolveParameters(doc.parameters);
  const errors = Object.entries(perr).map(([k, v]) => `parameter ${k}: ${v}`);
  const lookup = (n: string) => {
    if (n in values) return values[n];
    throw new Error(`unknown parameter "${n}"`);
  };
  const fix = (v: unknown, where: string): unknown => {
    if (isExpr(v)) {
      try { return evalExpr(v, lookup); } catch (e) { errors.push(`${where}: ${(e as Error).message}`); return 0; }
    }
    if (Array.isArray(v)) return v.map((x, i) => fix(x, `${where}[${i}]`));
    return v;
  };
  const features: Feature[] = doc.features.map((f) => f.params
    ? { ...f, params: Object.fromEntries(Object.entries(f.params).map(([k, v]) => [k, fix(v, `${f.name ?? f.id}.${k}`)])) }
    : f);
  const { parameters: _drop, study: _study, ...rest } = doc;
  void _drop;
  void _study;
  return { doc: { ...rest, features }, errors };
}
