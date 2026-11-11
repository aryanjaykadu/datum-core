"use client";

import { useCallback, useEffect, useState } from "react";

export interface Settings {
  units: "mm" | "in";
  quality: "draft" | "fine";
  bricks: 16 | 24 | 32;
  sound: boolean;
  grid: boolean;
  annotations: boolean;
  orthographic: boolean;
  decimals: number;
  /** Viewport speed/quality trade-off; "auto" adapts to the GPU. */
  performance: "auto" | "quality" | "balanced" | "performance";
  /** Simple shows a short, explained tool set; Pro shows everything. */
  mode: "simple" | "pro";
  workspace: "all" | "mechanical" | "robotics" | "automotive" | "aerospace";
  theme: string;
  /** "" = the theme's own accent */
  accent: string;
  soundPack: "guitar" | "soft" | "mechanical" | "scifi" | "chiptune" | "marimba";
  /** Panels refract the live 3D view (liquid glass). */
  liquidGlass: boolean;
  /** Colours of the "custom" theme. */
  custom: { canvas: string; panel: string; text: string; accent: string; accent2: string };
  printer: string;
  filament: string;
  volume: number;
}

export const DEFAULT_SETTINGS: Settings = {
  units: "mm", quality: "draft", bricks: 24, sound: true, grid: true, annotations: true, orthographic: false, decimals: 2,
  performance: "auto", mode: "pro", workspace: "all",
  theme: "midnight", accent: "", soundPack: "guitar", volume: 0.6, liquidGlass: true,
  custom: { canvas: "#0d1016", panel: "#181c26", text: "#eef1f6", accent: "#6ee7b7", accent2: "#fbbf24" },
  printer: "bambu-p1s", filament: "pla",
};

const KEY = "datum.settings.v1";

export function useSettings() {
  const [settings, setSettings] = useState<Settings>(DEFAULT_SETTINGS);
  useEffect(() => {
    try {
      const saved = localStorage.getItem(KEY);
      if (saved) setSettings({ ...DEFAULT_SETTINGS, ...JSON.parse(saved) });
    } catch {}
  }, []);
  const update = useCallback((patch: Partial<Settings>) => {
    setSettings((s) => {
      const next = { ...s, ...patch };
      try {
        localStorage.setItem(KEY, JSON.stringify(next));
      } catch {}
      return next;
    });
  }, []);
  return [settings, update] as const;
}

/** Length formatting in the user's units (values are always stored in mm). */
export function fmtLen(mm: number, s: Settings, withUnit = true) {
  const v = s.units === "in" ? mm / 25.4 : mm;
  return v.toFixed(s.units === "in" ? s.decimals + 1 : s.decimals) + (withUnit ? ` ${s.units}` : "");
}
export const toDisplay = (mm: number, s: Settings) => (s.units === "in" ? mm / 25.4 : mm);
export const fromDisplay = (v: number, s: Settings) => (s.units === "in" ? v * 25.4 : v);

const RECENT = "datum.recent.v1";
export function recentFiles(): string[] {
  try {
    return JSON.parse(localStorage.getItem(RECENT) ?? "[]");
  } catch {
    return [];
  }
}
export function rememberFile(path: string) {
  try {
    const list = [path, ...recentFiles().filter((p) => p !== path)].slice(0, 8);
    localStorage.setItem(RECENT, JSON.stringify(list));
  } catch {}
}
