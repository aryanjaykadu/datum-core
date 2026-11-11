"use client";

import { useEffect, useRef, useState } from "react";
import { click } from "@/lib/audio";
import type { Settings } from "@/lib/settings";
import { THEMES } from "@/lib/themes";
import Icon from "./Icon";

/** Title-bar theme switcher: every theme one click away, plus a custom accent and "Customise…". */
export default function ThemeQuick({ settings, onChange, onCustomise }: { settings: Settings; onChange: (p: Partial<Settings>) => void; onCustomise: () => void }) {
  const [open, setOpen] = useState(false);
  const ref = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (!open) return;
    const away = (e: PointerEvent) => !ref.current?.contains(e.target as Node) && setOpen(false);
    window.addEventListener("pointerdown", away);
    return () => window.removeEventListener("pointerdown", away);
  }, [open]);
  return (
    <div className="pop-host theme-quick" ref={ref}>
      <button className={`tb-icon${open ? " on" : ""}`} title="Colour theme" aria-label="Colour theme" onClick={() => setOpen(!open)}><Icon name="palette" /></button>
      {open && (
        <div className="pop pop-down glass-strong theme-pop">
          <div className="theme-dots">
            {[...THEMES, { key: "custom", label: "Custom", canvas: settings.custom.canvas, accent: settings.custom.accent, accent2: settings.custom.accent2 }].map((t) => (
              <button key={t.key} className="theme-dot" aria-pressed={settings.theme === t.key} title={t.label}
                onClick={() => { click(262, 0.35); onChange({ theme: t.key, accent: "" }); }}
                style={{ background: `linear-gradient(135deg, ${t.canvas} 0 45%, ${t.accent} 45% 75%, ${t.accent2} 75%)` }}>
                <span>{t.label}</span>
              </button>
            ))}
          </div>
          <button className="btn wide" onClick={() => { setOpen(false); onCustomise(); }}>Customise colours…</button>
        </div>
      )}
    </div>
  );
}
