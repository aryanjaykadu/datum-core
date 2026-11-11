import { beamBending, boltTorque, CALCULATORS, driveline, gearPair, sectionInertia, springRate, wingAero } from "../src/lib/calc.ts";

const near = (a: number, b: number, rel = 1e-3) => Math.abs(a - b) <= rel * Math.abs(b);
const ok = (c: boolean, m: string) => { if (!c) { console.log("FAIL", m); process.exitCode = 1; } };

// M10 8.8: 0.75 * 580 MPa * 58 mm² = 25.23 kN; T = 0.2 * 25.23 kN * 10 mm = 50.5 N·m
const b = boltTorque(10, 8.8, 0.2, 0.75);
ok(near(b.preload, 25230) && near(b.torque, 50.46), `bolt ${b.preload} ${b.torque}`);
ok(near(boltTorque(20, 8.8, 0.2, 0.75).preload, 0.75 * 600 * 245), "M20 uses the 600 MPa proof strength");

const g = gearPair(2, 18, 54, 1500, 5, 1);
ok(g.ratio === 3 && g.centre === 72 && g.rpmOut === 500 && g.torqueOut === 15, "gear pair");

// 20 x 40 rectangle: I = 20 * 40³ / 12 = 106 667 mm⁴; cantilever 1 kN at 500 mm, E = 69 GPa
const s = sectionInertia(0, 20, 40);
ok(near(s.i, 106666.67) && s.c === 20, "rect inertia");
const c = beamBending(0, 1000, 500, 69, s.i, s.c);
ok(near(c.deflection, (1000 * 500 ** 3) / (3 * 69000 * s.i)) && near(c.stress, 93.75), `cantilever ${c.deflection} ${c.stress}`);
ok(near(beamBending(1, 1000, 500, 69, s.i, s.c).deflection, c.deflection / 16), "simply supported is 1/16 of the cantilever");
ok(near(sectionInertia(1, 0, 20).i, 7853.98), "round bar inertia");

// d 2, D 20, 7 coils, G 79.3 GPa: k = 79300 * 16 / (8 * 8000 * 7) = 2.832 N/mm
const sp = springRate(2, 20, 7, 79.3, 50);
ok(near(sp.rate, 2.8321) && near(sp.deflection, 50 / 2.8321) && near(sp.index, 10), `spring ${sp.rate}`);
ok(near(sp.shear, ((36 + 3) / 36 + 0.0615) * 8 * 50 * 20 / (Math.PI * 8)), "Wahl shear stress");

// 1.5 m x 0.2 m at 15 m/s, CL 0.5: L = 0.5 * 1.225 * 225 * 0.3 * 0.5 = 20.67 N
const w = wingAero(1.5, 0.2, 15, 0.5, 1.2, 2);
ok(near(w.lift, 20.672) && near(w.aspect, 7.5) && near(w.stall, Math.sqrt((2 * 2 * 9.80665) / (1.225 * 0.3 * 1.2))), `wing ${w.lift}`);

// 205/55 R16 = 631.9 mm; 3000 rpm / 3.9 -> 769.2 rpm -> 91.6 km/h
const d = driveline(205, 55, 16, 3000, 1, 3.9, 0.8);
ok(near(d.diameter, 631.9) && near(d.kmh, 91.62, 2e-3), `driveline ${d.diameter} ${d.kmh}`);

for (const calc of CALCULATORS) {
  const out = calc.run(Object.fromEntries(calc.fields.map((x) => [x.key, x.value])));
  ok(out.length > 0 && out.every((r) => !r.value.includes("NaN") && !r.value.includes("—")), `${calc.key} defaults produce numbers`);
}
console.log(process.exitCode ? "calc tests FAILED" : "calc tests: all passed");
