"use client";

import { DEFAULT_SETTINGS, type Settings } from "@/lib/settings";
import { THEMES } from "@/lib/themes";
import { click, SOUND_PACKS, setSoundPack, setVolume, type SoundPack } from "@/lib/audio";

export default function SettingsDialog({ settings, onChange, onClose }: { settings: Settings; onChange: (p: Partial<Settings>) => void; onClose: () => void }) {
  const row = (label: string, hint: string, control: React.ReactNode) => (
    <label className="setting">
      <span>
        {label}
        <small>{hint}</small>
      </span>
      {control}
    </label>
  );
  return (
    <div className="modal-backdrop" onPointerDown={onClose}>
      <div className="modal glass-strong" role="dialog" aria-label="Settings" onPointerDown={(e) => e.stopPropagation()}>
        <div className="panel-title">
          Settings
          <button className="icon" onClick={onClose} aria-label="Close">×</button>
        </div>
        <div className="modal-body">
          <h3>Document</h3>
          {row("Units", "Display and input units. Designs are stored in millimetres.",
            <select value={settings.units} onChange={(e) => onChange({ units: e.target.value as Settings["units"] })}>
              <option value="mm">Millimetres</option>
              <option value="in">Inches</option>
            </select>)}
          {row("Decimal places", "Precision of displayed lengths.",
            <input type="number" min={0} max={6} value={settings.decimals} onChange={(e) => onChange({ decimals: Math.max(0, Math.min(6, Number(e.target.value))) })} />)}
          {row("Rebuild quality", "Fine meshes are 2.4× denser: exact mass properties, slower rebuilds.",
            <select value={settings.quality} onChange={(e) => onChange({ quality: e.target.value as Settings["quality"] })}>
              <option value="draft">Draft (interactive)</option>
              <option value="fine">Fine (analysis / export)</option>
            </select>)}
          <h3>Workspace</h3>
          {row("Experience", "Simple shows a short set of explained tools. Pro shows every tool.",
            <select value={settings.mode} onChange={(e) => onChange({ mode: e.target.value as Settings["mode"] })}>
              <option value="simple">Simple (learning)</option>
              <option value="pro">Pro (all tools)</option>
            </select>)}
          {row("Discipline", "Which tool groups the ribbon shows.",
            <select value={settings.workspace} onChange={(e) => onChange({ workspace: e.target.value as Settings["workspace"] })}>
              <option value="all">All disciplines</option>
              <option value="mechanical">Mechanical</option>
              <option value="robotics">Robotics</option>
              <option value="automotive">Automotive</option>
              <option value="aerospace">Aerospace</option>
            </select>)}
          <h3>Viewport</h3>
          {row("Performance", "Auto adapts resolution to hold the frame rate. Performance suits low-end PCs: fewer pixels, no shadows.",
            <select value={settings.performance} onChange={(e) => onChange({ performance: e.target.value as Settings["performance"] })}>
              <option value="auto">Auto (recommended)</option>
              <option value="performance">Performance (low-end PC)</option>
              <option value="balanced">Balanced</option>
              <option value="quality">Quality</option>
            </select>)}
          {row("GPU detail", "Bricks per axis. More bricks: shorter shader programs, more memory.",
            <select value={settings.bricks} onChange={(e) => onChange({ bricks: Number(e.target.value) as Settings["bricks"] })}>
              <option value={16}>16³ (low memory)</option>
              <option value={24}>24³ (balanced)</option>
              <option value={32}>32³ (detailed)</option>
            </select>)}
          {row("Orthographic", "Parallel projection, as used for drawings.",
            <input type="checkbox" checked={settings.orthographic} onChange={(e) => onChange({ orthographic: e.target.checked })} />)}
          {row("Floor grid", "Reference grid under the part.", <input type="checkbox" checked={settings.grid} onChange={(e) => onChange({ grid: e.target.checked })} />)}
          {row("Annotations", "Centre-of-mass and finding labels in 3D.",
            <input type="checkbox" checked={settings.annotations} onChange={(e) => onChange({ annotations: e.target.checked })} />)}
          <h3>Appearance</h3>
          <div className="theme-grid" role="group" aria-label="Colour theme">
            {THEMES.map((t) => (
              <button key={t.key} className="theme-swatch" aria-pressed={settings.theme === t.key} onClick={() => onChange({ theme: t.key, accent: "" })}>
                <span className="chips3"><i style={{ background: t.canvas }} /><i style={{ background: t.accent }} /><i style={{ background: t.accent2 }} /></span>
                {t.label}
              </button>
            ))}
          </div>
          {row("Custom accent", "Overrides the theme's highlight colour.",
            <span className="sound-row">
              <input type="color" value={settings.accent || THEMES.find((t) => t.key === settings.theme)?.accent || "#4fd1c5"} onChange={(e) => onChange({ accent: e.target.value })} />
              {settings.accent && <button className="btn" onClick={() => onChange({ accent: "" })}>Reset</button>}
            </span>)}
          <h3>Sound</h3>
          {row("Sounds", "Tones for clicks and completed checks.", <input type="checkbox" checked={settings.sound} onChange={(e) => onChange({ sound: e.target.checked })} />)}
          {row("Sound pack", SOUND_PACKS.find((p) => p.key === settings.soundPack)?.hint ?? "",
            <span className="sound-row">
              <select value={settings.soundPack} disabled={!settings.sound} onChange={(e) => {
                const p = e.target.value as SoundPack;
                setSoundPack(p);
                click(220, 0.6);
                onChange({ soundPack: p });
              }}>
                {SOUND_PACKS.map((p) => <option key={p.key} value={p.key}>{p.label}</option>)}
              </select>
              <button className="btn" disabled={!settings.sound} onClick={() => click(220, 0.6)}>Preview</button>
            </span>)}
          {row("Volume", `${Math.round(settings.volume * 100)} %`,
            <input type="range" min={0} max={1} step={0.05} value={settings.volume} disabled={!settings.sound}
              onChange={(e) => { setVolume(Number(e.target.value)); onChange({ volume: Number(e.target.value) }); }} onPointerUp={() => click(220, 0.6)} />)}
          <h3>Manufacturing partner</h3>
          <div className="note">
            Live pricing and ordering through Xometry or Protolabs require a partner agreement and API credentials. Until one is
            configured, quotes are geometry-based estimates and ordering is disabled.
          </div>
        </div>
        <div className="modal-foot">
          <button className="btn" onClick={() => onChange(DEFAULT_SETTINGS)}>Restore defaults</button>
          <button className="btn primary" onClick={onClose}>Done</button>
        </div>
      </div>
    </div>
  );
}
