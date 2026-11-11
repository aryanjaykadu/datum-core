/**
 * Engineering hand calculations (closed-form textbook formulas, SI with mm where noted).
 * These are sizing aids, not a substitute for a verified analysis.
 */

export interface CalcField { key: string; label: string; unit?: string; value: number; options?: { label: string; value: number }[] }
export interface CalcResult { label: string; value: string; warn?: boolean }
export interface Calculator {
  key: string;
  label: string;
  domain: "mechanical" | "automotive" | "aerospace";
  note: string;
  fields: CalcField[];
  run: (v: Record<string, number>) => CalcResult[];
}

const f = (x: number, digits = 2) => (Number.isFinite(x) ? x.toLocaleString("en-US", { maximumFractionDigits: digits, minimumFractionDigits: 0 }) : "—");

// ISO 898-1 coarse-thread stress areas (mm²) and proof strengths (MPa)
export const STRESS_AREA: Record<number, number> = { 3: 5.03, 4: 8.78, 5: 14.2, 6: 20.1, 8: 36.6, 10: 58.0, 12: 84.3, 16: 157, 20: 245 };
export function proofStrength(grade: number, d: number) {
  if (grade === 8.8) return d <= 16 ? 580 : 600;
  if (grade === 10.9) return 830;
  if (grade === 12.9) return 970;
  throw new Error("grade must be 8.8, 10.9 or 12.9");
}
export function boltTorque(d: number, grade: number, k: number, utilisation: number) {
  const as = STRESS_AREA[d];
  if (!as) throw new Error("size must be M3–M20 coarse");
  const preload = utilisation * proofStrength(grade, d) * as; // N
  return { stressArea: as, preload, torque: (k * preload * d) / 1000 }; // N·m
}

export function gearPair(module: number, z1: number, z2: number, rpmIn: number, torqueIn: number, efficiency: number) {
  const ratio = z2 / z1;
  return { ratio, centre: (module * (z1 + z2)) / 2, d1: module * z1, d2: module * z2, rpmOut: rpmIn / ratio, torqueOut: torqueIn * ratio * efficiency };
}

/** Second moment of area (mm⁴) and outer-fibre distance (mm). */
export function sectionInertia(shape: number, b: number, h: number) {
  if (shape === 0) return { i: (b * h ** 3) / 12, c: h / 2 };                 // rectangle b × h
  if (shape === 1) return { i: (Math.PI * h ** 4) / 64, c: h / 2 };            // round bar, diameter h
  const ipe: Record<number, [number, number]> = { 80: [80.1e4, 40], 100: [171e4, 50], 120: [318e4, 60], 160: [869e4, 80], 200: [1943e4, 100] };
  const row = ipe[shape];
  if (!row) throw new Error("unknown section");
  return { i: row[0], c: row[1] };
}
/** support: 0 cantilever with end load, 1 simply supported with centre load. F in N, L in mm, E in GPa. */
export function beamBending(support: number, force: number, length: number, eGpa: number, i: number, c: number) {
  const e = eGpa * 1000; // MPa
  const deflection = support === 0 ? (force * length ** 3) / (3 * e * i) : (force * length ** 3) / (48 * e * i);
  const moment = support === 0 ? force * length : (force * length) / 4; // N·mm
  return { deflection, moment, stress: (moment * c) / i };
}

/** Helical compression spring: wire d, mean coil D (mm), active coils, shear modulus G (GPa), load F (N). */
export function springRate(d: number, D: number, active: number, gGpa: number, force: number) {
  const rate = (gGpa * 1000 * d ** 4) / (8 * D ** 3 * active); // N/mm
  const index = D / d;
  const wahl = (4 * index - 1) / (4 * index - 4) + 0.615 / index;
  return { rate, index, deflection: force / rate, shear: (wahl * 8 * force * D) / (Math.PI * d ** 3), solid: (active + 2) * d };
}

/** Steady level flight. span, chord in m; speed m/s; mass kg; sea-level air unless rho given. */
export function wingAero(span: number, chord: number, speed: number, cl: number, clMax: number, mass: number, rho = 1.225) {
  const area = span * chord;
  const weight = mass * 9.80665;
  return {
    area, aspect: span / chord, lift: 0.5 * rho * speed * speed * area * cl, weight,
    stall: Math.sqrt((2 * weight) / (rho * area * clMax)),
    clNeeded: weight / (0.5 * rho * speed * speed * area),
    reynolds: (speed * chord) / 1.46e-5, loading: mass / area,
  };
}

/** Tyre such as 205/55 R16; engine speed through gearbox and final drive. */
export function driveline(width: number, aspect: number, rim: number, rpm: number, gear: number, finalDrive: number, mu: number) {
  const diameter = (2 * width * aspect) / 100 + rim * 25.4; // mm
  const wheelRpm = rpm / (gear * finalDrive);
  const kmh = (wheelRpm * Math.PI * diameter * 60) / 1e6;
  const v = kmh / 3.6;
  return { diameter, wheelRpm, kmh, braking: (v * v) / (2 * mu * 9.80665) };
}

export const CALCULATORS: Calculator[] = [
  {
    key: "bolt", label: "Bolt torque", domain: "mechanical",
    note: "Tightening torque T = K·F·d for a preload at a share of proof load (ISO 898-1, coarse thread).",
    fields: [
      { key: "d", label: "Size", value: 8, options: Object.keys(STRESS_AREA).map((k) => ({ label: `M${k}`, value: Number(k) })) },
      { key: "grade", label: "Grade", value: 8.8, options: [8.8, 10.9, 12.9].map((g) => ({ label: g.toFixed(1), value: g })) },
      { key: "k", label: "Nut factor K", value: 0.2 },
      { key: "use", label: "Share of proof load", value: 0.75 },
    ],
    run: (v) => {
      const r = boltTorque(v.d, v.grade, v.k, v.use);
      return [
        { label: "Tightening torque", value: `${f(r.torque, 1)} N·m` },
        { label: "Preload", value: `${f(r.preload / 1000, 1)} kN` },
        { label: "Stress area", value: `${f(r.stressArea)} mm²` },
      ];
    },
  },
  {
    key: "gears", label: "Gear pair", domain: "mechanical",
    note: "Spur gear pair: ratio, centre distance and output speed / torque.",
    fields: [
      { key: "m", label: "Module", unit: "mm", value: 2 }, { key: "z1", label: "Pinion teeth", value: 18 }, { key: "z2", label: "Gear teeth", value: 54 },
      { key: "rpm", label: "Input speed", unit: "rpm", value: 1500 }, { key: "t", label: "Input torque", unit: "N·m", value: 5 },
      { key: "eff", label: "Efficiency", value: 0.98 },
    ],
    run: (v) => {
      const r = gearPair(v.m, v.z1, v.z2, v.rpm, v.t, v.eff);
      return [
        { label: "Ratio", value: `${f(r.ratio, 3)} : 1` }, { label: "Centre distance", value: `${f(r.centre)} mm` },
        { label: "Pitch diameters", value: `${f(r.d1)} / ${f(r.d2)} mm` },
        { label: "Output speed", value: `${f(r.rpmOut, 1)} rpm` }, { label: "Output torque", value: `${f(r.torqueOut)} N·m` },
        { label: "Pinion undercut", value: v.z1 < 17 ? "below 17 teeth: undercut at 20°" : "none", warn: v.z1 < 17 },
      ];
    },
  },
  {
    key: "beam", label: "Beam bending", domain: "mechanical",
    note: "Euler–Bernoulli beam with a single point load.",
    fields: [
      { key: "support", label: "Support", value: 0, options: [{ label: "Cantilever, end load", value: 0 }, { label: "Simply supported, centre load", value: 1 }] },
      { key: "shape", label: "Section", value: 0, options: [{ label: "Rectangle b × h", value: 0 }, { label: "Round bar Ø h", value: 1 }, ...[80, 100, 120, 160, 200].map((n) => ({ label: `IPE ${n}`, value: n }))] },
      { key: "b", label: "Width b", unit: "mm", value: 20 }, { key: "h", label: "Height h", unit: "mm", value: 40 },
      { key: "L", label: "Length", unit: "mm", value: 500 }, { key: "F", label: "Load", unit: "N", value: 1000 },
      { key: "E", label: "Young's modulus", unit: "GPa", value: 69 }, { key: "yield", label: "Yield strength", unit: "MPa", value: 276 },
    ],
    run: (v) => {
      const s = sectionInertia(v.shape, v.b, v.h);
      const r = beamBending(v.support, v.F, v.L, v.E, s.i, s.c);
      const sf = v.yield / r.stress;
      return [
        { label: "Max deflection", value: `${f(r.deflection, 3)} mm` }, { label: "Max bending stress", value: `${f(r.stress, 1)} MPa` },
        { label: "Safety factor to yield", value: f(sf), warn: sf < 1.5 }, { label: "Second moment I", value: `${f(s.i / 1e4)} cm⁴` },
      ];
    },
  },
  {
    key: "spring", label: "Coil spring", domain: "mechanical",
    note: "Helical compression spring rate and Wahl-corrected shear stress.",
    fields: [
      { key: "d", label: "Wire diameter", unit: "mm", value: 2 }, { key: "D", label: "Mean coil diameter", unit: "mm", value: 20 },
      { key: "n", label: "Active coils", value: 7 }, { key: "G", label: "Shear modulus", unit: "GPa", value: 79.3 }, { key: "F", label: "Load", unit: "N", value: 50 },
    ],
    run: (v) => {
      const r = springRate(v.d, v.D, v.n, v.G, v.F);
      return [
        { label: "Spring rate", value: `${f(r.rate, 3)} N/mm` }, { label: "Deflection at load", value: `${f(r.deflection)} mm` },
        { label: "Shear stress", value: `${f(r.shear, 0)} MPa` }, { label: "Solid length", value: `${f(r.solid)} mm` },
        { label: "Spring index D/d", value: f(r.index, 1), warn: r.index < 4 || r.index > 12 },
      ];
    },
  },
  {
    key: "driveline", label: "Speed & braking", domain: "automotive",
    note: "Road speed from engine speed, gearing and tyre size; braking distance at constant friction.",
    fields: [
      { key: "w", label: "Tyre width", unit: "mm", value: 205 }, { key: "a", label: "Aspect ratio", unit: "%", value: 55 }, { key: "rim", label: "Rim", unit: "in", value: 16 },
      { key: "rpm", label: "Engine speed", unit: "rpm", value: 3000 }, { key: "gear", label: "Gear ratio", value: 1.0 }, { key: "fd", label: "Final drive", value: 3.9 },
      { key: "mu", label: "Tyre–road friction", value: 0.8 },
    ],
    run: (v) => {
      const r = driveline(v.w, v.a, v.rim, v.rpm, v.gear, v.fd, v.mu);
      return [
        { label: "Road speed", value: `${f(r.kmh, 1)} km/h` }, { label: "Tyre diameter", value: `${f(r.diameter, 1)} mm` },
        { label: "Wheel speed", value: `${f(r.wheelRpm, 0)} rpm` }, { label: "Braking distance from this speed", value: `${f(r.braking, 1)} m` },
      ];
    },
  },
  {
    key: "wing", label: "Wing sizing", domain: "aerospace",
    note: "Level-flight lift, stall speed and Reynolds number at sea level (rectangular-wing approximation).",
    fields: [
      { key: "span", label: "Span", unit: "m", value: 1.5 }, { key: "chord", label: "Mean chord", unit: "m", value: 0.2 },
      { key: "v", label: "Airspeed", unit: "m/s", value: 15 }, { key: "cl", label: "Lift coefficient", value: 0.5 }, { key: "clmax", label: "Max lift coefficient", value: 1.2 },
      { key: "mass", label: "Aircraft mass", unit: "kg", value: 2 },
    ],
    run: (v) => {
      const r = wingAero(v.span, v.chord, v.v, v.cl, v.clmax, v.mass);
      return [
        { label: "Lift", value: `${f(r.lift, 1)} N`, warn: r.lift < r.weight }, { label: "Weight", value: `${f(r.weight, 1)} N` },
        { label: "Lift coefficient needed", value: f(r.clNeeded, 3), warn: r.clNeeded > v.clmax },
        { label: "Stall speed", value: `${f(r.stall, 1)} m/s` }, { label: "Aspect ratio", value: f(r.aspect, 1) },
        { label: "Wing loading", value: `${f(r.loading, 1)} kg/m²` }, { label: "Reynolds number", value: f(r.reynolds, 0) },
      ];
    },
  },
];
