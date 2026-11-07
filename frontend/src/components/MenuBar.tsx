"use client";

import { useEffect, useRef, useState } from "react";
import { click } from "@/lib/audio";
import { windowAction } from "@/lib/ipc";

export interface MenuItem {
  label?: string;
  shortcut?: string;
  action?: () => void;
  disabled?: boolean;
  checked?: boolean;
  separator?: boolean;
}
export type Menus = Record<string, MenuItem[]>;

export default function MenuBar({ menus, title, dirty, badge }: { menus: Menus; title: string; dirty: boolean; badge?: string }) {
  const [open, setOpen] = useState<string | null>(null);
  const ref = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const close = (e: PointerEvent) => {
      if (!ref.current?.contains(e.target as Node)) setOpen(null);
    };
    const esc = (e: KeyboardEvent) => e.key === "Escape" && setOpen(null);
    window.addEventListener("pointerdown", close);
    window.addEventListener("keydown", esc);
    return () => {
      window.removeEventListener("pointerdown", close);
      window.removeEventListener("keydown", esc);
    };
  }, []);

  return (
    <header className="titlebar area-title glass-strong" data-tauri-drag-region>
      <div className="brand" data-tauri-drag-region>
        {/* eslint-disable-next-line @next/next/no-img-element */}
        <img src="brand-mark.png" alt="" width={20} height={20} draggable={false} />
        Datum Core
      </div>
      <nav className="menubar" ref={ref}>
        {Object.entries(menus).map(([name, items]) => (
          <div key={name} className="menu">
            <button
              className={open === name ? "active" : ""}
              onPointerDown={() => setOpen(open === name ? null : name)}
              onPointerEnter={() => open && setOpen(name)}
            >
              {name}
            </button>
            {open === name && (
              <div className="dropdown glass-strong" role="menu">
                {items.map((item, i) =>
                  item.separator ? (
                    <hr key={i} />
                  ) : (
                    <button
                      key={i}
                      role="menuitem"
                      disabled={item.disabled}
                      onClick={() => {
                        setOpen(null);
                        click(220, 0.35);
                        item.action?.();
                      }}
                    >
                      <span className="check">{item.checked ? "✓" : ""}</span>
                      <span>{item.label}</span>
                      <kbd>{item.shortcut}</kbd>
                    </button>
                  ),
                )}
              </div>
            )}
          </div>
        ))}
      </nav>
      <span className="doc-title" data-tauri-drag-region>
        {title}
        {dirty && <span className="dirty" title="Unsaved changes"> ●</span>}
        {badge && <span className="badge">{badge}</span>}
      </span>
      <div className="spacer" data-tauri-drag-region />
      <div className="window-controls">
        <button aria-label="Minimize" onClick={() => windowAction("minimize")}>
          <svg width="10" height="10" viewBox="0 0 10 10"><path d="M0 5h10" stroke="currentColor" /></svg>
        </button>
        <button aria-label="Maximize" onClick={() => windowAction("toggleMaximize")}>
          <svg width="10" height="10" viewBox="0 0 10 10"><rect x="0.5" y="0.5" width="9" height="9" fill="none" stroke="currentColor" /></svg>
        </button>
        <button aria-label="Close" className="close" onClick={() => windowAction("close")}>
          <svg width="10" height="10" viewBox="0 0 10 10"><path d="M0 0l10 10M10 0L0 10" stroke="currentColor" /></svg>
        </button>
      </div>
    </header>
  );
}
