"use client";

import { DEFAULT_SETTINGS, type Settings } from "@/lib/settings";

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
          <h3>Viewport</h3>
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
          <h3>Interface</h3>
          {row("Sounds", "Subtle tones for clicks and completed checks.", <input type="checkbox" checked={settings.sound} onChange={(e) => onChange({ sound: e.target.checked })} />)}
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
