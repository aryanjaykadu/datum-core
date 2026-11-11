/**
 * Colour themes. Each sets the UI's CSS variables and the viewport's background and accent
 * light colours, so panels and the 3D scene change together.
 */

export interface Theme {
  key: string;
  label: string;
  /** primary accent (selection, focus, highlights) and secondary accent (warnings, warm rim light) */
  accent: string;
  accent2: string;
  canvas: string;
  text: string;
  textDim: string;
  textFaint: string;
  hairline: string;
  /** glass panel tint, as "r, g, b" */
  glass: string;
}

export const THEMES: Theme[] = [
  { key: "midnight", label: "Midnight Teal", accent: "#4fd1c5", accent2: "#f0b45a", canvas: "#0b0c10", text: "#ececee", textDim: "#8b8d96", textFaint: "#5b5d66", hairline: "#222222", glass: "20, 22, 28" },
  { key: "graphite", label: "Graphite", accent: "#9aa7b8", accent2: "#d9b26f", canvas: "#0e0f11", text: "#e8e9ec", textDim: "#8a8d94", textFaint: "#5a5d63", hairline: "#26282c", glass: "26, 27, 30" },
  { key: "ocean", label: "Deep Ocean", accent: "#4ea8ff", accent2: "#7ee0c3", canvas: "#070b14", text: "#e6eefb", textDim: "#7f90ad", textFaint: "#4f5f7a", hairline: "#1a2436", glass: "14, 22, 38" },
  { key: "aurora", label: "Aurora", accent: "#a78bfa", accent2: "#5eead4", canvas: "#0c0a14", text: "#efeaff", textDim: "#938aac", textFaint: "#5f5878", hairline: "#251f36", glass: "26, 20, 40" },
  { key: "ember", label: "Ember", accent: "#ff8a4c", accent2: "#ffd166", canvas: "#110b08", text: "#f7ece6", textDim: "#a8928a", textFaint: "#6e5c55", hairline: "#2e211b", glass: "36, 24, 18" },
  { key: "forest", label: "Forest", accent: "#5bd68a", accent2: "#e8c468", canvas: "#080d0a", text: "#e7f2ea", textDim: "#86a090", textFaint: "#55685c", hairline: "#1b2a21", glass: "16, 28, 21" },
  { key: "rose", label: "Rose Gold", accent: "#f48fb1", accent2: "#f6c177", canvas: "#100a0d", text: "#f8ebef", textDim: "#a88e98", textFaint: "#6f5a63", hairline: "#2e1f25", glass: "36, 22, 28" },
  { key: "neon", label: "Neon", accent: "#22e5ff", accent2: "#ff3df2", canvas: "#05050a", text: "#f2f6ff", textDim: "#8a8fb0", textFaint: "#55597a", hairline: "#1c1c2e", glass: "14, 14, 28" },
];

export function hexToRgb(hex: string): [number, number, number] {
  const m = /^#?([0-9a-f]{6})$/i.exec(hex.trim());
  if (!m) return [79, 209, 197];
  const n = parseInt(m[1], 16);
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
}

/** Resolve the theme, with an optional custom accent overriding the preset's. */
export function resolveTheme(key: string, customAccent?: string): Theme {
  const base = THEMES.find((t) => t.key === key) ?? THEMES[0];
  return customAccent && /^#[0-9a-f]{6}$/i.test(customAccent) ? { ...base, accent: customAccent } : base;
}

export function applyTheme(t: Theme) {
  if (typeof document === "undefined") return;
  const s = document.documentElement.style;
  const a = hexToRgb(t.accent), b = hexToRgb(t.accent2);
  s.setProperty("--canvas", t.canvas);
  s.setProperty("--text", t.text);
  s.setProperty("--text-dim", t.textDim);
  s.setProperty("--text-faint", t.textFaint);
  s.setProperty("--hairline", t.hairline);
  s.setProperty("--teal", t.accent);
  s.setProperty("--amber", t.accent2);
  s.setProperty("--warn", t.accent2);
  s.setProperty("--accent-rgb", a.join(", "));
  s.setProperty("--accent2-rgb", b.join(", "));
  s.setProperty("--glass", `rgba(${t.glass}, 0.4)`);
  s.setProperty("--glass-strong", `rgba(${t.glass}, 0.72)`);
  s.setProperty("--glass-rgb", t.glass);
}

/** Viewport colours as linear-ish 0..1 triples for the shader. */
export function viewportColours(t: Theme) {
  const f = (hex: string) => hexToRgb(hex).map((v) => v / 255) as [number, number, number];
  const bg = f(t.canvas).map((v) => Math.max(v, 0.02) * 0.9) as [number, number, number];
  return { accent: f(t.accent), accent2: f(t.accent2), bg };
}
