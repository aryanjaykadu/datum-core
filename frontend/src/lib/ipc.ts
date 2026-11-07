/**
 * Typed bridge to the Rust shell. All kernel work goes webview -> invoke -> Rust ->
 * dk_engine (stdio). Outside the desktop app the calls reject with a clear message.
 */

export type Vec3 = [number, number, number];
export type Matrix3 = [Vec3, Vec3, Vec3];

export interface Feature {
  id: string;
  type: string;
  name?: string;
  suppressed?: boolean;
  inputs?: string[];
  params?: Record<string, unknown>;
}

export interface DatumDocument {
  version: 1;
  robots?: Record<string, import("./urdf").RobotDef>;
  units: "mm";
  name?: string;
  material: string;
  root: string;
  /** Named design values; feature fields reference them as "=name * 2" (resolved before the kernel). */
  parameters?: Record<string, string | number>;
  features: Feature[];
}

/** Pillar 6 contract (validated by the shell against a strict schema). */
export interface DatumPayload {
  system_id: string;
  kernel_metrics: { dag_nodes_computed: number; gpu_vram_usage_mb: number };
  commerce_and_supply_chain: { live_cnc_quote_usd: number; embodied_carbon_kg: number; transaction_fee_revenue_usd: number };
  security_forensics: { itar_tpm_locked: boolean; steganography_detected: boolean };
  advanced_fea_and_cad: { mbd_max_deflection_mm: number; explicit_fracture_detected: boolean };
  telemetry_and_4d: { mass_kg: number; inertia_tensor_current: Matrix3 };
}

export interface Evaluation {
  contract: DatumPayload;
  features: { id: string; type: string; name: string; suppressed: boolean; inputs: string[] }[];
  warnings: string[];
  geometry: {
    bbox_min: Vec3; bbox_max: Vec3; volume_mm3: number; area_mm2: number; centroid_mm: Vec3;
    triangles: number; cell_mm: number; watertight: boolean; program_instructions: number; graph_nodes: number;
  };
  material: { key: string; name: string; density_kg_m3: number };
  analysis: {
    shells: number;
    findings: { centre_mm: Vec3; volume_mm3: number; size_mm: number; cavity: boolean; suspicious: boolean; designed: boolean }[];
    access: { reachable_pct: number[]; covered_pct: number; setups: number };
    impact: { deflection_mm: number; stress_mpa: number; fracture: boolean; energy_j: number; model: string };
  };
  timing_ms: { build: number; mesh: number; total: number };
}

export interface ParamSpec {
  kind: "number" | "vec3" | "choice" | "profile" | "file";
  label: string;
  default: unknown;
  min?: number;
  unit?: string;
  options?: string[];
}
export interface FeatureSpec {
  hidden?: boolean;
  label: string;
  group: string;
  inputs: number | string;
  params: Record<string, ParamSpec>;
}
export interface Catalog {
  features: Record<string, FeatureSpec>;
  materials: { key: string; name: string; density_kg_m3: number; yield_mpa: number }[];
  robots: { key: string; name: string; dof: number; reach_mm: number; limits_deg: [number, number][]; link_radius: number }[];
}

export interface Quote {
  price_usd: number; unit_price_usd: number; lead_days: number; setups: number; stock_kg: number;
  machining_h: number; carbon_kg: number; fee_usd: number; fee_rate: number; provider: string; material: string; quantity: number;
}

export const isTauri = () => typeof window !== "undefined" && "__TAURI_INTERNALS__" in window;

async function invoke<T>(cmd: string, args?: Record<string, unknown>): Promise<T> {
  if (!isTauri()) throw new Error("The kernel is only available in the Datum Core desktop app.");
  const { invoke } = await import("@tauri-apps/api/core");
  return invoke<T>(cmd, args);
}

export interface ReachStats {
  robot: string; voxels: number; voxel_mm: number; reach_mm: number; volume_m3: number; singular_pct: number;
  dexterous_pct: number; samples: number; near_singular_samples: number; colliding_samples: number; threshold: number; ms: number;
}
export interface ReachCloud { stats: ReachStats; voxels: Float32Array } // x, y, z, best, worst per voxel

export const kernel = {
  catalog: () => invoke<Catalog>("engine_call", { method: "catalog" }),
  example: () => invoke<DatumDocument>("engine_call", { method: "example" }),
  evaluate: (document: DatumDocument, quality: "draft" | "fine", gpuMb: number) =>
    invoke<Evaluation>("evaluate", { document, quality, gpuMb }),
  scene: (bricks: number) => invoke<ArrayBuffer>("engine_binary", { method: "scene", params: { bricks } }),
  quote: (material: string, quantity: number) => invoke<Quote>("engine_call", { method: "quote", params: { material, quantity } }),
  robotReach: async (preset: string, center: Vec3, samples: number, threshold: number, collisions = true, floor = true,
    robot?: import("./urdf").RobotDef): Promise<ReachCloud> => {
    const params: Record<string, unknown> = { preset, center, samples, threshold, collisions, floor };
    if (robot) params.robot = robot;
    const buf = await invoke<ArrayBuffer>("engine_binary", { method: "robot_reach", params });
    const len = new DataView(buf).getUint32(0, true);
    const stats = JSON.parse(new TextDecoder().decode(new Uint8Array(buf, 4, len))) as ReachStats;
    const start = 4 + len;
    // copy so the float view is 4-byte aligned regardless of the JSON length
    return { stats, voxels: new Float32Array(buf.slice(start, start + stats.voxels * 20)) };
  },
  robotFk: (preset: string, qDeg: number[], robot?: import("./urdf").RobotDef) =>
    invoke<{ tool: Vec3; tool_z: Vec3; joints: Vec3[]; dof: number; reach_mm: number; dexterity: number }>("engine_call", {
      method: "robot_fk", params: robot ? { robot, q_deg: qDeg } : { preset, q_deg: qDeg } }),
  exportStl: (path: string) => invoke<{ path: string; triangles: number }>("engine_call", { method: "export_stl", params: { path } }),
};

export const files = {
  open: (path: string) => invoke<{ document: DatumDocument; protected: boolean; path: string }>("open_document", { path }),
  save: (path: string, document: DatumDocument, protect: boolean) => invoke<void>("save_document", { path, document, protect }),
  reset: () => invoke<void>("new_document"),
  takeLaunchFile: () => invoke<string | null>("take_launch_file"),
};

export async function pickOpenPath(): Promise<string | null> {
  if (!isTauri()) return null;
  const { open } = await import("@tauri-apps/plugin-dialog");
  const r = await open({ multiple: false, filters: [{ name: "Datum design", extensions: ["datum"] }] });
  return typeof r === "string" ? r : null;
}

export async function pickSavePath(defaultName: string, ext: "datum" | "stl" | "3mf" | "png"): Promise<string | null> {
  if (!isTauri()) return null;
  const { save } = await import("@tauri-apps/plugin-dialog");
  const r = await save({
    defaultPath: defaultName,
    filters: [{ datum: { name: "Datum design", extensions: ["datum"] }, stl: { name: "STL mesh", extensions: ["stl"] }, "3mf": { name: "3MF print file", extensions: ["3mf"] }, png: { name: "PNG image", extensions: ["png"] } }[ext]],
  });
  return r ?? null;
}

export const support = {
  autosave: (document: DatumDocument, path: string | null) => invoke<void>("autosave_write", { document, path }),
  recovery: () => invoke<{ saved_at: number; path: string | null; document: DatumDocument } | null>("autosave_read"),
  clearAutosave: () => invoke<void>("autosave_clear"),
  log: (level: string, message: string) => invoke<void>("log_line", { level, message }),
  openLogs: () => invoke<void>("open_log_folder"),
  info: () => invoke<{ name: string; version: string; tauri: string; data_dir: string }>("app_info"),
};

export const exporter = {
  dir: () => invoke<string>("export_dir"),
  stlTo3mf: (stl: string, out: string, name: string) => invoke<{ path: string; triangles: number; vertices: number }>("stl_to_3mf", { stl, out, name }),
  savePng: (path: string, data: number[]) => invoke<void>("save_png", { path, data }),
  open: (path: string) => invoke<void>("open_export", { path }),
};

export async function pickRobotFile(): Promise<string | null> {
  if (!isTauri()) return null;
  const { open } = await import("@tauri-apps/plugin-dialog");
  const r = await open({ multiple: false, filters: [{ name: "Robot description (URDF)", extensions: ["urdf", "xml"] }] });
  return typeof r === "string" ? r : null;
}
export async function pickImportFile(): Promise<string | null> {
  if (!isTauri()) return null;
  const { open } = await import("@tauri-apps/plugin-dialog");
  const r = await open({ multiple: false, filters: [{ name: "CAD (STEP, STL, OBJ)", extensions: ["step", "stp", "stl", "obj"] }] });
  return typeof r === "string" ? r : null;
}
export const importCad = (path: string) => invoke<{ mesh: string; source: string }>("import_cad", { path });

export const readRobotFile = (path: string) => invoke<string>("read_robot_file", { path });

export async function confirmDialog(message: string, title = "Datum Core"): Promise<boolean> {
  if (!isTauri()) return window.confirm(message);
  const { ask } = await import("@tauri-apps/plugin-dialog");
  return ask(message, { title, kind: "warning" });
}

export async function listen<T>(event: string, handler: (payload: T) => void): Promise<() => void> {
  if (!isTauri()) return () => undefined;
  const { listen } = await import("@tauri-apps/api/event");
  return listen<T>(event, (e) => handler(e.payload));
}

export async function windowAction(action: "minimize" | "toggleMaximize" | "close" | "destroy") {
  if (!isTauri()) return;
  const { getCurrentWindow } = await import("@tauri-apps/api/window");
  await getCurrentWindow()[action]();
}

export async function setWindowTitle(title: string) {
  if (!isTauri()) return;
  const { getCurrentWindow } = await import("@tauri-apps/api/window");
  await getCurrentWindow().setTitle(title);
}
