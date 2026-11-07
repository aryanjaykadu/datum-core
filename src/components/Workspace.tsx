"use client";

import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { click, reject, setSoundEnabled, validate } from "@/lib/audio";
import { deleteFeature, emptyDocument, History, insertFeature, updateFeature, type Operation } from "@/lib/document";
import type { PickResult, Viewport } from "@/lib/gpu";
import {
  confirmDialog, files, isTauri, kernel, listen, pickOpenPath, pickSavePath, setWindowTitle, support, windowAction,
  type Catalog, type DatumDocument, type Evaluation, type Feature,
} from "@/lib/ipc";
import { recentFiles, rememberFile, useSettings } from "@/lib/settings";
import { installSpringVariables } from "@/lib/springs";
import CommandPalette, { parseIntents } from "./CommandPalette";
import DagCanvas from "./DagCanvas";
import FeatureTree from "./FeatureTree";
import Inspector from "./Inspector";
import MenuBar, { type Menus } from "./MenuBar";
import PropertyPanel from "./PropertyPanel";
import QuotePanel from "./QuotePanel";
import RoboticsPanel from "./RoboticsPanel";
import Ribbon from "./Ribbon";
import RunLog, { type LogLine } from "./RunLog";
import SettingsDialog from "./SettingsDialog";
import StatusBar from "./StatusBar";
import ViewportPanel, { type Marker } from "./ViewportPanel";

const basename = (p: string | null) => (p ? p.split(/[\\/]/).pop()! : null);

export default function Workspace() {
  const [settings, updateSettings] = useSettings();
  const [catalog, setCatalog] = useState<Catalog | null>(null);
  const [doc, setDoc] = useState<DatumDocument>(emptyDocument);
  const [result, setResult] = useState<Evaluation | null>(null);
  const [selected, setSelected] = useState<string | null>(null);
  const [path, setPath] = useState<string | null>(null);
  const [locked, setLocked] = useState(false);
  const [dirty, setDirty] = useState(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [log, setLog] = useState<LogLine[]>([]);
  const [op, setOp] = useState<Operation>("add");
  const [dock, setDock] = useState<"log" | "graph">("graph");
  const [rightTab, setRightTab] = useState<"properties" | "analysis" | "quote" | "robotics">("properties");
  const [showSettings, setShowSettings] = useState(false);
  const [showAbout, setShowAbout] = useState(false);
  const [showPalette, setShowPalette] = useState(false);
  const [panels, setPanels] = useState({ tree: true, inspector: true, dock: true });
  const [showWelcome, setShowWelcome] = useState(false);
  const [version, setVersion] = useState<string>("");
  const togglePanel = (k: keyof typeof panels) => setPanels((p) => ({ ...p, [k]: !p[k] }));
  const [hover, setHover] = useState<PickResult | null>(null);
  const [, force] = useState(0);
  const history = useRef(new History());
  const vp = useRef<Viewport | null>(null);
  const logId = useRef(0);
  const evalSeq = useRef(0);

  const pushLog = useCallback((level: LogLine["level"], stage: string, msg: string) => {
    if (level !== "info" && isTauri()) void support.log(level, `${stage}: ${msg}`).catch(() => undefined);
    const t = new Date().toLocaleTimeString("en-GB", { hour12: false });
    setLog((l) => [...l.slice(-300), { id: logId.current++, t, level, stage, msg }]);
  }, []);

  // ------------------------------------------------------------ document edits
  const docRef = useRef(doc);
  docRef.current = doc;
  const commit = useCallback((next: DatumDocument) => {
    history.current.push(docRef.current);
    setDoc(next);
    setDirty(true);
    force((n) => n + 1);
  }, []);
  const replaceDocument = useCallback((next: DatumDocument, newPath: string | null, isLocked: boolean) => {
    history.current.clear();
    setDoc(next);
    setPath(newPath);
    setLocked(isLocked);
    setDirty(false);
    setSelected(null);
    setResult(null);
    vp.current?.fit();
  }, []);
  const undo = () => {
    const prev = history.current.undo(docRef.current);
    if (prev) (setDoc(prev), setDirty(true), force((n) => n + 1));
  };
  const redo = () => {
    const next = history.current.redo(docRef.current);
    if (next) (setDoc(next), setDirty(true), force((n) => n + 1));
  };

  // ------------------------------------------------------------ startup
  useEffect(() => {
    installSpringVariables();
    if (!isTauri()) {
      pushLog("warn", "shell", "Running in a browser: the kernel is only available in the desktop app.");
      return;
    }
    kernel.catalog().then(setCatalog).catch((e) => pushLog("error", "kernel", String(e)));
    support.info().then((i) => setVersion(i.version)).catch(() => undefined);
    const onError = (e: ErrorEvent) => pushLog("error", "ui", `${e.message} at ${e.filename}:${e.lineno}`);
    const onRejection = (e: PromiseRejectionEvent) => pushLog("error", "ui", `unhandled: ${String(e.reason)}`);
    window.addEventListener("error", onError);
    window.addEventListener("unhandledrejection", onRejection);
    // a snapshot left behind means the last session did not close cleanly
    support.recovery().then(async (snap) => {
      if (snap?.document?.features?.length) {
        const when = new Date(snap.saved_at * 1000).toLocaleString();
        if (await confirmDialog(`Datum Core did not close cleanly. Recover the design autosaved at ${when}?`, "Recover design")) {
          replaceDocument(snap.document, snap.path, false);
          setDirty(true);
          pushLog("info", "recovery", `recovered autosave from ${when}`);
          return;
        }
      }
      await support.clearAutosave();
      try {
        if (!localStorage.getItem("datum.welcomed")) setShowWelcome(true);
      } catch {}
    }).catch(() => undefined);
    files.takeLaunchFile().then((p) => { if (p) void openPath(p); }).catch(() => undefined);
    const offs = [
      listen<{ type: string; level?: LogLine["level"]; msg?: string }>("engine://event", (e) => e.msg && pushLog(e.level ?? "info", "engine", e.msg)),
      listen<string>("file://open", (p) => void openPath(p)),
      listen<null>("app://close-requested", () => void closeApp()),
    ];
    return () => offs.forEach((p) => p.then((off) => off()));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => setSoundEnabled(settings.sound), [settings.sound]);
  // autosave a few seconds after each change while there are unsaved edits
  useEffect(() => {
    if (!dirty || !isTauri() || doc.features.length === 0) return;
    const t = setTimeout(() => void support.autosave(docRef.current, path).catch(() => undefined), 3000);
    return () => clearTimeout(t);
  }, [doc, dirty, path]);
  useEffect(() => {
    void setWindowTitle(`${dirty ? "● " : ""}${basename(path) ?? doc.name ?? "Untitled"} — Datum Core`);
  }, [dirty, path, doc.name]);

  // ------------------------------------------------------------ rebuild (debounced, stale results dropped)
  useEffect(() => {
    if (!isTauri() || doc.features.length === 0) {
      setResult(null);
      return;
    }
    const seq = ++evalSeq.current;
    const timer = setTimeout(async () => {
      setBusy(true);
      try {
        const r = await kernel.evaluate(doc, settings.quality, vp.current?.vramMb ?? 0);
        if (seq !== evalSeq.current) return;
        const scene = await kernel.scene(settings.bricks);
        if (seq !== evalSeq.current) return;
        vp.current?.setScene(scene, r.geometry.bbox_min, r.geometry.bbox_max);
        setResult(r);
        setError(null);
        r.warnings.forEach((w) => pushLog("warn", "model", w));
        pushLog("info", "kernel", `rebuilt ${r.contract.kernel_metrics.dag_nodes_computed} features in ${r.timing_ms.total.toFixed(0)} ms · ${r.geometry.triangles.toLocaleString()} tris`);
        const flagged = r.contract.security_forensics.steganography_detected || r.contract.advanced_fea_and_cad.explicit_fracture_detected || !r.geometry.watertight;
        if (flagged) reject();
        else validate();
      } catch (e) {
        if (seq !== evalSeq.current) return;
        setError(String(e));
        pushLog("error", "kernel", String(e));
        reject();
      } finally {
        if (seq === evalSeq.current) setBusy(false);
      }
    }, 180);
    return () => clearTimeout(timer);
  }, [doc, settings.quality, settings.bricks, pushLog]);

  // ------------------------------------------------------------ files
  const confirmDiscard = async () => !dirtyRef.current || confirmDialog("Discard unsaved changes to this design?");
  const dirtyRef = useRef(dirty);
  dirtyRef.current = dirty;

  async function openPath(p: string) {
    if (!(await confirmDiscard())) return;
    try {
      const r = await files.open(p);
      replaceDocument(r.document, p, r.protected);
      rememberFile(p);
      pushLog("info", "file", `opened ${p}${r.protected ? " (device-locked)" : ""}`);
    } catch (e) {
      pushLog("error", "file", String(e));
      setError(String(e));
    }
  }
  const newDoc = async (example: boolean) => {
    if (!(await confirmDiscard())) return;
    await files.reset().catch(() => undefined);
    await support.clearAutosave().catch(() => undefined);
    replaceDocument(example ? await kernel.example() : emptyDocument(), null, false);
    if (example) setDirty(true);
  };
  const open = async () => {
    const p = await pickOpenPath();
    if (p) await openPath(p);
  };
  const save = async (as = false, protect = locked) => {
    let target = path;
    if (as || !target) target = await pickSavePath(`${doc.name ?? "design"}.datum`, "datum");
    if (!target) return false;
    try {
      await files.save(target, doc, protect);
      setPath(target);
      setLocked(protect);
      setDirty(false);
      void support.clearAutosave().catch(() => undefined);
      rememberFile(target);
      click(262, 0.5);
      pushLog("info", "file", `saved ${target}${protect ? " (device-locked, AES-256-GCM)" : ""}`);
      return true;
    } catch (e) {
      pushLog("error", "file", String(e));
      setError(String(e));
      return false;
    }
  };
  const exportStl = async () => {
    if (!result) return;
    const p = await pickSavePath(`${doc.name ?? "part"}.stl`, "stl");
    if (!p) return;
    try {
      const r = await kernel.exportStl(p);
      pushLog("info", "export", `wrote ${r.triangles.toLocaleString()} triangles to ${p}`);
    } catch (e) {
      pushLog("error", "export", String(e));
    }
  };
  async function closeApp() {
    if (!(await confirmDiscard())) return;
    await support.clearAutosave().catch(() => undefined);
    await windowAction("destroy");
  }

  // ------------------------------------------------------------ feature commands
  const insert = (type: string, withOp: Operation = op, params?: Record<string, unknown>, keepTab = false) => {
    if (!catalog) return;
    try {
      const r = insertFeature(docRef.current, catalog, type, withOp, params);
      click(196);
      commit(r.doc);
      setSelected(r.selected);
      if (!keepTab) setRightTab("properties");
    } catch (e) {
      pushLog("warn", "model", String(e));
    }
  };
  /**
   * Lighten by a mass percentage: add a gyroid infill to the current result, then bisect
   * the infill wall thickness (mass rises monotonically with it) against kernel rebuilds.
   */
  const lighten = async (pct: number) => {
    if (!catalog || !result) return pushLog("warn", "copilot", "Build a part before lightweighting it.");
    const m0 = result.contract.telemetry_and_4d.mass_kg;
    const target = m0 * (1 - pct / 100);
    const size = result.geometry.bbox_max.map((v, i) => v - result.geometry.bbox_min[i]);
    const cell = Math.max(4, Math.min(20, Math.min(...size) / 3));
    const skin = Math.max(0.8, Math.min(2, Math.min(...size) / 25));
    const base = docRef.current;
    const withWall = (wall: number) => insertFeature(base, catalog, "gyroid_infill", "add", { cell: +cell.toFixed(2), wall: +wall.toFixed(3), skin: +skin.toFixed(2) });
    const massAt = async (wall: number) => (await kernel.evaluate(withWall(wall).doc, "draft", 0)).contract.telemetry_and_4d.mass_kg;
    setBusy(true);
    pushLog("info", "copilot", `lightening ${pct}%: ${(m0 * 1000).toFixed(1)} g -> ${(target * 1000).toFixed(1)} g target, cell ${cell.toFixed(1)} mm`);
    try {
      let lo = 0.3, hi = cell * 0.45;
      const mLo = await massAt(lo);
      if (mLo > target) {
        pushLog("warn", "copilot", `thinnest printable infill only reaches ${((1 - mLo / m0) * 100).toFixed(1)}% lighter; applying that`);
        hi = lo;
      } else {
        for (let i = 0; i < 7; i++) {
          const mid = (lo + hi) / 2;
          const m = await massAt(mid);
          pushLog("info", "copilot", `wall ${mid.toFixed(3)} mm -> ${(m * 1000).toFixed(1)} g`);
          if (m > target) hi = mid;
          else lo = mid;
        }
      }
      const r = withWall((lo + hi) / 2);
      commit(r.doc);
      setSelected(r.selected);
      pushLog("info", "copilot", `applied gyroid infill (wall ${((lo + hi) / 2).toFixed(3)} mm). Strength is not verified: no FEA solver yet.`);
    } catch (e) {
      pushLog("error", "copilot", String(e));
    } finally {
      setBusy(false);
    }
  };
  const intents = (text: string) => parseIntents(text, catalog, {
    lighten: (p) => void lighten(p),
    insert: (type, o, params) => insert(type, o, params),
    material: (key) => commit({ ...docRef.current, material: key }),
    view: (name) => vp.current?.setView(name as Parameters<Viewport["setView"]>[0]),
  });

  // build on the latest document: two edits in one event must not overwrite each other
  const patchFeature = (id: string, patch: Partial<Feature>) => commit(updateFeature(docRef.current, id, patch));
  const removeFeature = (id: string) => {
    commit(deleteFeature(doc, id));
    if (selected === id) setSelected(null);
  };

  // ------------------------------------------------------------ shortcuts
  useEffect(() => {
    const key = (e: KeyboardEvent) => {
      const typing = ["INPUT", "SELECT", "TEXTAREA"].includes((e.target as HTMLElement).tagName);
      const mod = e.ctrlKey || e.metaKey;
      const k = e.key.toLowerCase();
      if (mod && k === "s") (e.preventDefault(), void save(e.shiftKey));
      else if (mod && k === "o") (e.preventDefault(), void open());
      else if (mod && k === "n") (e.preventDefault(), void newDoc(false));
      else if (mod && k === "e") (e.preventDefault(), void exportStl());
      else if (mod && k === ",") (e.preventDefault(), setShowSettings(true));
      else if (mod && k === "k") (e.preventDefault(), setShowPalette(true));
      else if (mod && k === "1") (e.preventDefault(), togglePanel("tree"));
      else if (mod && k === "2") (e.preventDefault(), togglePanel("inspector"));
      else if (mod && k === "3") (e.preventDefault(), togglePanel("dock"));
      else if (!typing && mod && k === "z" && !e.shiftKey) (e.preventDefault(), undo());
      else if (!typing && mod && (k === "y" || (k === "z" && e.shiftKey))) (e.preventDefault(), redo());
      else if (!typing && e.key === "Delete" && selected) removeFeature(selected);
    };
    window.addEventListener("keydown", key);
    return () => window.removeEventListener("keydown", key);
  });

  // ------------------------------------------------------------ menus
  const recent = typeof window === "undefined" ? [] : recentFiles();
  const menus: Menus = {
    File: [
      { label: "New", shortcut: "Ctrl+N", action: () => void newDoc(false) },
      { label: "New from example (L-bracket)", action: () => void newDoc(true) },
      { label: "Open…", shortcut: "Ctrl+O", action: () => void open() },
      ...recent.slice(0, 5).map((p) => ({ label: `  ${basename(p)}`, action: () => void openPath(p) })),
      { separator: true },
      { label: "Save", shortcut: "Ctrl+S", action: () => void save(false) },
      { label: "Save as…", shortcut: "Ctrl+Shift+S", action: () => void save(true) },
      { label: "Save device-locked copy…", action: () => void save(true, true) },
      { separator: true },
      { label: "Export STL…", shortcut: "Ctrl+E", action: () => void exportStl(), disabled: !result },
      { separator: true },
      { label: "Exit", action: () => void closeApp() },
    ],
    Edit: [
      { label: "Undo", shortcut: "Ctrl+Z", action: undo, disabled: !history.current.canUndo },
      { label: "Redo", shortcut: "Ctrl+Y", action: redo, disabled: !history.current.canRedo },
      { separator: true },
      { label: "Delete feature", shortcut: "Del", action: () => selected && removeFeature(selected), disabled: !selected },
      { label: "Suppress / unsuppress", action: () => selected && patchFeature(selected, { suppressed: !doc.features.find((f) => f.id === selected)?.suppressed }), disabled: !selected },
      { label: "Clear selection", shortcut: "Esc", action: () => setSelected(null) },
    ],
    View: [
      { label: "Fit to part", shortcut: "F", action: () => vp.current?.fit() },
      { label: "Isometric", shortcut: "0", action: () => vp.current?.setView("iso") },
      { label: "Front", shortcut: "1", action: () => vp.current?.setView("front") },
      { label: "Top", shortcut: "5", action: () => vp.current?.setView("top") },
      { label: "Right", shortcut: "4", action: () => vp.current?.setView("right") },
      { separator: true },
      { label: "Orthographic", checked: settings.orthographic, action: () => updateSettings({ orthographic: !settings.orthographic }) },
      { label: "Floor grid", checked: settings.grid, action: () => updateSettings({ grid: !settings.grid }) },
      { label: "Annotations", checked: settings.annotations, action: () => updateSettings({ annotations: !settings.annotations }) },
      { separator: true },
      { label: "Feature tree", shortcut: "Ctrl+1", checked: panels.tree, action: () => togglePanel("tree") },
      { label: "Inspector", shortcut: "Ctrl+2", checked: panels.inspector, action: () => togglePanel("inspector") },
      { label: "Dock", shortcut: "Ctrl+3", checked: panels.dock, action: () => togglePanel("dock") },
      { separator: true },
      { label: "Feature graph", checked: dock === "graph", action: () => setDock("graph") },
      { label: "Run log", checked: dock === "log", action: () => setDock("log") },
    ],
    Insert: catalog
      ? Object.entries(catalog.features).map(([type, s]) => ({ label: `${s.group}: ${s.label}`, action: () => insert(type), disabled: s.inputs !== 0 && doc.features.length === 0 }))
      : [],
    Tools: [
      { label: "Rebuild (fine quality)", action: () => updateSettings({ quality: "fine" }), checked: settings.quality === "fine" },
      { label: "Rebuild (draft quality)", action: () => updateSettings({ quality: "draft" }), checked: settings.quality === "draft" },
      { separator: true },
      { label: "Manufacturing quote", action: () => setRightTab("quote") },
      { label: "Robot reachability", action: () => setRightTab("robotics") },
      { label: "Analysis & checks", action: () => setRightTab("analysis") },
      { separator: true },
      { label: "Command palette…", shortcut: "Ctrl+K", action: () => setShowPalette(true) },
      { label: "Settings…", shortcut: "Ctrl+,", action: () => setShowSettings(true) },
    ],
    Help: [
      { label: "Welcome / getting started", action: () => setShowWelcome(true) },
      { label: "Keyboard shortcuts", action: () => setShowAbout(true) },
      { label: "Open log folder", action: () => void support.openLogs().catch(() => undefined) },
      { label: "About Datum Core", action: () => setShowAbout(true) },
    ],
  };

  const markers = useMemo<Marker[]>(() => {
    if (!result) return [];
    const list: Marker[] = [{ id: "com", label: "Centre of mass", position: result.geometry.centroid_mm, tone: "teal" }];
    result.analysis.findings.filter((f) => f.suspicious).slice(0, 6).forEach((f, i) =>
      list.push({ id: `void${i}`, label: `Hidden cavity ⌀${f.size_mm.toFixed(2)}`, position: f.centre_mm, tone: "fail" }));
    return list;
  }, [result]);

  return (
    <div className={`workspace ${panels.tree ? "" : "no-tree"} ${panels.inspector ? "" : "no-inspector"} ${panels.dock ? "" : "no-dock"}`}>
      <MenuBar menus={menus} title={basename(path) ?? doc.name ?? "Untitled"} dirty={dirty} badge={locked ? "LOCKED" : undefined} />
      <Ribbon catalog={catalog} op={op} onOp={setOp} onInsert={(t) => insert(t)} hasBody={doc.features.length > 0} />

      <FeatureTree doc={doc} catalog={catalog} selected={selected} onSelect={setSelected} warnings={result?.warnings ?? []}
        onRename={(id, name) => patchFeature(id, { name })} onDelete={removeFeature}
        onToggle={(id) => patchFeature(id, { suppressed: !doc.features.find((f) => f.id === id)?.suppressed })} />

      <ViewportPanel insets={{ left: panels.tree ? 276 : 0, right: panels.inspector ? 346 : 0, top: 120, bottom: panels.dock ? 262 : 36 }}
        onReady={(v) => (vp.current = v)} markers={markers} settings={settings} onSettings={updateSettings}
        onHover={setHover} onError={(m) => pushLog("error", "gpu", m)} empty={doc.features.length === 0} busy={busy} />

      <aside className="glass area-inspector scroll">
        <div className="panel-title">
          <div className="tabs" role="tablist">
            {(["properties", "analysis", "quote", "robotics"] as const).map((t) => (
              <button key={t} role="tab" aria-selected={rightTab === t} onClick={() => setRightTab(t)}>{{ properties: "Properties", analysis: "Analysis", quote: "Quote", robotics: "Robotics" }[t]}</button>
            ))}
          </div>
        </div>
        {rightTab === "properties" && (
          <PropertyPanel doc={doc} catalog={catalog} selected={selected} settings={settings} onChange={patchFeature}
            onDocChange={(p) => commit({ ...doc, ...p })} onDelete={removeFeature} />
        )}
        {rightTab === "analysis" && <Inspector result={result} settings={settings} />}
        {rightTab === "quote" && <QuotePanel result={result} catalog={catalog} material={doc.material} />}
        {rightTab === "robotics" && (
          <RoboticsPanel doc={doc} catalog={catalog} viewport={vp.current} onPatch={patchFeature}
            onDocChange={(p) => commit({ ...docRef.current, ...p })}
            onInsert={(t) => insert(t, doc.features.length ? "add" : "new", undefined, true)} onLog={(level, msg) => pushLog(level, "robotics", msg)} />
        )}
      </aside>

      <section className="glass area-dock">
        <div className="panel-title">
          <div className="tabs" role="tablist">
            <button role="tab" aria-selected={dock === "graph"} onClick={() => setDock("graph")}>Feature graph</button>
            <button role="tab" aria-selected={dock === "log"} onClick={() => setDock("log")}>Run log</button>
          </div>
        </div>
        {dock === "log" ? <RunLog lines={log} progress={busy ? 0.6 : null} /> : (
          <DagCanvas doc={doc} selected={selected} onSelect={setSelected}
            onConnect={(source, target) => {
              const f = doc.features.find((x) => x.id === target);
              if (f && catalog?.features[f.type]?.inputs !== 0 && !f.inputs?.includes(source)) patchFeature(target, { inputs: [...(f.inputs ?? []), source] });
            }} />
        )}
      </section>

      <StatusBar hover={hover} result={result} settings={settings} busy={busy} error={error} path={path} locked={locked} />

      {showWelcome && (
        <div className="modal-backdrop" onPointerDown={() => setShowWelcome(false)}>
          <div className="modal glass-strong welcome" onPointerDown={(e) => e.stopPropagation()}>
            <div className="panel-title">Welcome to Datum Core <button className="icon" onClick={() => setShowWelcome(false)}>×</button></div>
            <div className="modal-body">
              <p className="note">Geometry here is an exact field, so booleans, shells, fillets and lattices never fail. Start with one of these:</p>
              <div className="welcome-grid">
                <button className="welcome-card" onClick={() => { setShowWelcome(false); void newDoc(true); }}>
                  <b>Open the example</b><span>A filleted L-bracket with bolt holes and a gyroid-filled gusset.</span>
                </button>
                <button className="welcome-card" onClick={() => {
                  setShowWelcome(false);
                  if (!catalog) return;
                  const r = insertFeature(emptyDocument(), catalog, "extrude", "new");
                  commit(r.doc);
                  setSelected(r.selected);
                  setRightTab("properties");
                }}>
                  <b>Sketch a part</b><span>Draw a 2D profile, set dimensions and fillets, extrude it.</span>
                </button>
                <button className="welcome-card" onClick={() => { setShowWelcome(false); void open(); }}>
                  <b>Open a design</b><span>Continue from a .datum file, including device-locked ones.</span>
                </button>
                <button className="welcome-card" onClick={() => { setShowWelcome(false); setRightTab("robotics"); }}>
                  <b>Analyse a robot</b><span>Reachability, singularities and self-collision for presets, DH tables or URDF.</span>
                </button>
              </div>
              <p className="note">Tip: press Ctrl+K and type what you want, for example “lighten 20%”, “cut cylinder” or “view top”.</p>
            </div>
            <div className="modal-foot">
              <button className="btn primary" onClick={() => { try { localStorage.setItem("datum.welcomed", "1"); } catch {} setShowWelcome(false); }}>Get started</button>
            </div>
          </div>
        </div>
      )}
      {showPalette && <CommandPalette menus={menus} intents={intents} onClose={() => setShowPalette(false)} />}
      {showSettings && <SettingsDialog settings={settings} onChange={updateSettings} onClose={() => setShowSettings(false)} />}
      {showAbout && (
        <div className="modal-backdrop" onPointerDown={() => setShowAbout(false)}>
          <div className="modal glass-strong" onPointerDown={(e) => e.stopPropagation()}>
            <div className="panel-title">Datum Core {version || "1.0"} <button className="icon" onClick={() => setShowAbout(false)}>×</button></div>
            <div className="modal-body">
              <p className="note">GPU implicit-modeling CAD. Geometry is an exact field program: booleans never fail, and analyses run straight on the field.</p>
              <h3>Shortcuts</h3>
              <div className="shortcuts mono">
                {[["Ctrl+K", "Command palette"], ["Ctrl+1 / 2 / 3", "Toggle tree / inspector / dock"], ["Ctrl+N / O / S", "New / open / save"], ["Ctrl+Shift+S", "Save as"], ["Ctrl+E", "Export STL"], ["Ctrl+Z / Y", "Undo / redo"],
                  ["Del, F2", "Delete / rename feature"], ["0–6", "Standard views"], ["F", "Fit"], ["M", "Measure"], ["Drag / right-drag / wheel", "Orbit / pan / zoom"], ["Ctrl+,", "Settings"]]
                  .map(([k, v]) => <div key={k} className="kv"><span>{k}</span><span>{v}</span></div>)}
              </div>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
