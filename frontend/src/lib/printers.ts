/**
 * Printer and filament profiles for print checks and estimates. Build volumes are the makers'
 * published figures; speeds are typical sustained volumetric rates for quality profiles, not
 * peak marketing numbers. Estimates are for planning; the slicer has the final word.
 */
export interface Printer { key: string; label: string; bed: [number, number, number]; flow: number; layerChange: number }
export interface Filament { key: string; label: string; density: number; usdPerKg: number; note: string }

export const PRINTERS: Printer[] = [
  { key: "bambu-x1c", label: "Bambu Lab X1 Carbon", bed: [256, 256, 256], flow: 18, layerChange: 0.6 },
  { key: "bambu-p1s", label: "Bambu Lab P1S", bed: [256, 256, 256], flow: 18, layerChange: 0.6 },
  { key: "bambu-a1", label: "Bambu Lab A1", bed: [256, 256, 256], flow: 15, layerChange: 0.7 },
  { key: "bambu-a1mini", label: "Bambu Lab A1 mini", bed: [180, 180, 180], flow: 14, layerChange: 0.7 },
  { key: "prusa-mk4s", label: "Prusa MK4S", bed: [250, 210, 220], flow: 14, layerChange: 0.8 },
  { key: "prusa-core1", label: "Prusa CORE One", bed: [250, 220, 270], flow: 16, layerChange: 0.7 },
  { key: "prusa-xl", label: "Prusa XL", bed: [360, 360, 360], flow: 14, layerChange: 0.9 },
  { key: "ender3-v3", label: "Creality Ender-3 V3", bed: [220, 220, 250], flow: 13, layerChange: 0.8 },
  { key: "ender3-v2", label: "Creality Ender-3 V2", bed: [220, 220, 250], flow: 6, layerChange: 1.5 },
  { key: "neptune4pro", label: "Elegoo Neptune 4 Pro", bed: [225, 225, 265], flow: 12, layerChange: 0.9 },
  { key: "generic", label: "Other printer (typical)", bed: [200, 200, 200], flow: 8, layerChange: 1.2 },
];

export const FILAMENTS: Filament[] = [
  { key: "pla", label: "PLA", density: 1.24, usdPerKg: 20, note: "Easy, stiff; softens above ~55 °C" },
  { key: "petg", label: "PETG", density: 1.27, usdPerKg: 22, note: "Tougher than PLA, handles heat to ~70 °C" },
  { key: "abs", label: "ABS", density: 1.04, usdPerKg: 22, note: "Heat resistant; needs an enclosure" },
  { key: "asa", label: "ASA", density: 1.07, usdPerKg: 26, note: "Like ABS, UV stable for outdoor parts" },
  { key: "tpu", label: "TPU 95A", density: 1.21, usdPerKg: 35, note: "Flexible; print slowly" },
  { key: "pla-cf", label: "PLA-CF", density: 1.29, usdPerKg: 35, note: "Stiffer, matte; needs a hardened nozzle" },
  { key: "pa-cf", label: "PA-CF (nylon carbon)", density: 1.12, usdPerKg: 70, note: "Strong, heat resistant; dry it first" },
];

export const printerOf = (key: string) => PRINTERS.find((p) => p.key === key) ?? PRINTERS[PRINTERS.length - 1];
export const filamentOf = (key: string) => FILAMENTS.find((f) => f.key === key) ?? FILAMENTS[0];

/**
 * Time, mass and cost for an FDM print at 0.2 mm layers: ~0.9 mm of solid walls and top/bottom,
 * the rest at `infill` (0..1). Parts with their own lattice are printed solid: their walls are
 * already the structure, so set infill = 1 for them.
 */
export function printEstimate(volume_mm3: number, area_mm2: number, height_mm: number, printer: Printer, filament: Filament, infill = 0.2) {
  const skin = Math.min(volume_mm3, area_mm2 * 0.9);
  const printed = skin + (volume_mm3 - skin) * infill;
  const layers = Math.ceil(height_mm / 0.2);
  const seconds = printed / printer.flow + layers * printer.layerChange;
  const grams = (printed * filament.density) / 1000;
  return { seconds, grams, metres: printed / (Math.PI * 0.8737 * 0.8737) / 1000, cost: (grams / 1000) * filament.usdPerKg, layers };
}

export function formatDuration(seconds: number) {
  const h = Math.floor(seconds / 3600), m = Math.max(1, Math.round((seconds % 3600) / 60));
  return h > 0 ? `${h} h ${m} min` : `${m} min`;
}

/** Does a part of this size fit the build volume as modelled, or only when rotated? */
export function fitOnBed(size: number[], bed: [number, number, number]): "fits" | "rotate" | "too-big" {
  if (size[0] <= bed[0] && size[1] <= bed[1] && size[2] <= bed[2]) return "fits";
  const s = [...size].sort((a, b) => a - b), b = [...bed].sort((a, c) => a - c);
  return s.every((v, i) => v <= b[i]) ? "rotate" : "too-big";
}
