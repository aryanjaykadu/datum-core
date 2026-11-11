"use client";

import type { DatumDocument, Evaluation } from "@/lib/ipc";
import { FILAMENTS, PRINTERS, filamentOf, fitOnBed, formatDuration, printEstimate, printerOf } from "@/lib/printers";
import type { Settings } from "@/lib/settings";
import Icon from "./Icon";

/** Last step of the workflow: pick the printer and filament, check it fits, see the estimate, send it. */
export default function PrintDialog({ doc, result, settings, onSettings, onSend, onExport, onClose }: {
  doc: DatumDocument;
  result: Evaluation;
  settings: Settings;
  onSettings: (p: Partial<Settings>) => void;
  onSend: () => void;
  onExport: () => void;
  onClose: () => void;
}) {
  const printer = printerOf(settings.printer), filament = filamentOf(settings.filament);
  const g = result.geometry;
  const size = g.bbox_max.map((v, i) => v - g.bbox_min[i]);
  const lattice = doc.features.some((f) => (f.type === "lighten" || f.type === "gyroid_infill") && !f.suppressed);
  const est = printEstimate(g.volume_mm3, g.area_mm2, size[2], printer, filament, lattice ? 1 : 0.2);
  const fit = fitOnBed(size, printer.bed);
  return (
    <div className="modal-backdrop" onPointerDown={onClose}>
      <div className="modal glass-strong print-dialog" role="dialog" aria-label="Print" onPointerDown={(e) => e.stopPropagation()}>
        <div className="panel-title">
          Print {doc.name ?? "this part"}
          <button className="icon" onClick={onClose} aria-label="Close">×</button>
        </div>
        <div className="modal-body">
          <div className="print-pick">
            <label className="prop">
              <span>Printer</span>
              <select value={printer.key} onChange={(e) => onSettings({ printer: e.target.value })}>
                {PRINTERS.map((p) => <option key={p.key} value={p.key}>{p.label}</option>)}
              </select>
            </label>
            <label className="prop">
              <span>Filament</span>
              <select value={filament.key} onChange={(e) => onSettings({ filament: e.target.value })}>
                {FILAMENTS.map((f) => <option key={f.key} value={f.key}>{f.label}</option>)}
              </select>
            </label>
            <p className="note small">{filament.note}</p>
          </div>

          <div className={`fit ${fit}`}>
            <Icon name={fit === "fits" ? "box" : "fit"} size={18} />
            <div>
              <b>{{ fits: "Fits on the bed", rotate: "Fits if you rotate it", "too-big": "Too big for this printer" }[fit]}</b>
              <span>Part {size.map((v) => v.toFixed(0)).join(" × ")} mm · bed {printer.bed.join(" × ")} mm
                {fit === "too-big" ? ". Split it, scale it, or choose a larger printer." : fit === "rotate" ? ". Turn it in the slicer before printing." : ""}</span>
            </div>
          </div>

          <div className="stat-tiles print-tiles">
            <div className="stat"><small>Print time</small><b>{formatDuration(est.seconds)}</b><span>{est.layers} layers of 0.2 mm</span></div>
            <div className="stat"><small>Filament</small><b>{est.grams.toFixed(est.grams < 10 ? 1 : 0)} g</b><span>{est.metres.toFixed(1)} m of 1.75 mm</span></div>
            <div className="stat"><small>Material cost</small><b>${est.cost.toFixed(2)}</b><span>at ${filament.usdPerKg}/kg</span></div>
            {(() => {
              const holes = g.boundary_edges ?? (g.watertight ? 0 : 1), seams = g.nonmanifold_edges ?? 0;
              return holes > 0
                ? <div className="stat bad"><small>Model</small><b>Has holes</b><span>{holes} open edges: fix before printing</span></div>
                : <div className="stat good"><small>Model</small><b>Closed</b><span>{seams === 0 ? "watertight, slicers accept it as is" : `${seams} touching edge${seams > 1 ? "s" : ""}: slicers repair automatically`}</span></div>;
            })()}
          </div>
          <p className="note small">
            {lattice ? "This part has a designed lattice: print it with 100 % infill (or 2+ walls and solid infill) so the lattice is the structure. " : "Estimate at 2 walls and 20 % infill. "}
            Your slicer has the final word on time and settings.
          </p>
        </div>
        <div className="modal-foot">
          <button className="btn" onClick={onExport}><Icon name="download" size={14} />Save 3MF…</button>
          <button className="btn primary" disabled={fit === "too-big"} onClick={onSend}><Icon name="play" size={14} />Open in my slicer</button>
        </div>
      </div>
    </div>
  );
}
