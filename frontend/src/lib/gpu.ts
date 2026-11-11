import { POINTS_WGSL } from "./pointShader";
import { RAYMARCH_WGSL } from "./shader";
import { MESH_WGSL } from "./meshShader";
import type { Vec3 } from "./ipc";

export type DisplayMode = "shaded" | "cost" | "normals" | "xray" | "dfm";
export type ViewName = "iso" | "front" | "back" | "top" | "bottom" | "left" | "right";
export interface Section { axis: "off" | "x" | "y" | "z"; offset: number; flip: boolean }
export interface PickResult { position: Vec3; normal: Vec3; cap: boolean; probe?: number[] }

const MODE_INDEX: Record<DisplayMode, number> = { shaded: 0, cost: 1, normals: 2, xray: 3, dfm: 4 };
// z-up camera presets: [yaw, pitch]
const VIEWS: Record<ViewName, [number, number]> = {
  iso: [-2.35, 0.55], front: [-Math.PI / 2, 0], back: [Math.PI / 2, 0], right: [0, 0], left: [Math.PI, 0],
  top: [-Math.PI / 2, Math.PI / 2 - 1e-3], bottom: [-Math.PI / 2, -Math.PI / 2 + 1e-3],
};

interface SceneInfo { n: number; registers: number; lo: Vec3; hi: Vec3; bytes: number }

/**
 * WebGPU viewport for brick-pruned field programs. Renders on demand: while the camera
 * moves it draws at half resolution, then refines to full resolution when input stops.
 */
export class Viewport {
  private device!: GPUDevice;
  private context!: GPUCanvasContext;
  private format!: GPUTextureFormat;
  private uniform!: GPUBuffer;
  private pickBuffer!: GPUBuffer;
  private pickRead!: GPUBuffer;
  private bricks: GPUBuffer | null = null;
  private code: GPUBuffer | null = null;
  private grids: GPUBuffer | null = null;
  private pipeline: GPURenderPipeline | null = null;
  private pickPipeline: GPUComputePipeline | null = null;
  private bind: GPUBindGroup | null = null;
  private pickBind: GPUBindGroup | null = null;
  private registers = 0;
  private depth: GPUTexture | null = null;
  private scene: SceneInfo | null = null;
  private queued = false;
  private interacting = false;
  private idle = 0;
  private picking: Promise<unknown> = Promise.resolve();
  private cloudPipeline: GPURenderPipeline | null = null;
  private cloudBuffer: GPUBuffer | null = null;
  private cloudUniform: GPUBuffer | null = null;
  private cloudBind: GPUBindGroup | null = null;
  private cloudCount = 0;
  cloud = { visible: true, mode: 0 as 0 | 1, alpha: 0.16, voxel: 1, threshold: 0.05 };

  private anim = 0;
  private meshPipeline: GPURenderPipeline | null = null;
  private meshVB: GPUBuffer | null = null;
  private meshIB: GPUBuffer | null = null;
  private meshBind: GPUBindGroup | null = null;
  private meshIndices = 0;
  /** This frame draws the part from its triangle mesh (background-only ray pass). */
  private meshFrame = false;
  /** Stills of this scene are slow on this GPU: draw them from the mesh too. */
  private slowStills = 0;
  /** Resolution scale while the camera moves; steered every frame towards perf.targetMs. */
  private dyn = 0.5;
  /** Resolution scale of the full-quality still; lowered when a still takes too long. */
  private still = 1;
  private rendering = false;
  private again = false;
  private lastPoke = 0;
  perf = { targetMs: 16.7, maxPixels: 3.5e6, shadows: true, auto: true, alwaysMesh: false };
  private spin: [number, number] = [0, 0];
  cam = { yaw: VIEWS.iso[0], pitch: VIEWS.iso[1], dist: 200, target: [0, 0, 0] as Vec3, fov: 32, ortho: false };
  mode: DisplayMode = "shaded";
  /** Index into the shader's material looks (same order as the kernel's material table). */
  material = 0;
  /** Theme colours (0..1) for the background and rim lights. */
  colours = { accent: [0, 0.85, 0.75], accent2: [0.95, 0.68, 0.25], bg: [0.035, 0.038, 0.05] };
  grid = true;
  section: Section = { axis: "off", offset: 0, flip: false };
  onFrame?: (ms: number, width: number, height: number) => void;

  private constructor(private canvas: HTMLCanvasElement) {}

  static async create(canvas: HTMLCanvasElement): Promise<Viewport> {
    if (!("gpu" in navigator)) throw new Error("WebGPU is not available on this system.");
    const adapter = await navigator.gpu.requestAdapter({ powerPreference: "high-performance" });
    if (!adapter) throw new Error("No WebGPU adapter was found (update the graphics driver).");
    const v = new Viewport(canvas);
    v.device = await adapter.requestDevice();
    v.context = canvas.getContext("webgpu") as GPUCanvasContext;
    v.format = navigator.gpu.getPreferredCanvasFormat();
    v.context.configure({ device: v.device, format: v.format, alphaMode: "opaque" });
    v.uniform = v.device.createBuffer({ size: 208, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    v.pickBuffer = v.device.createBuffer({ size: 48, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
    v.pickRead = v.device.createBuffer({ size: 48, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
    return v;
  }

  /** GPU memory held by the scene buffers, in MB (reported in the contract). */
  get vramMb() {
    return ((this.scene?.bytes ?? 0) + this.cloudCount * 20) / (1024 * 1024);
  }
  get bounds() {
    return this.scene ? { lo: this.scene.lo, hi: this.scene.hi } : null;
  }

  /** Upload a scene produced by the kernel's `scene` method. */
  setScene(buffer: ArrayBuffer, partLo: Vec3, partHi: Vec3) {
    this.slowStills = 0;
    const head = new Uint32Array(buffer, 0, 4);
    const box = new Float32Array(buffer, 16, 6);
    const [n, registers, count, codeCount] = head;
    const brickBytes = count * 12, codeBytes = codeCount * 16;
    const make = (offset: number, size: number) => {
      const b = this.device.createBuffer({ size: Math.max(size, 16), usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
      if (size) this.device.queue.writeBuffer(b, 0, buffer, offset, size);
      return b;
    };
    this.bricks?.destroy();
    this.code?.destroy();
    this.grids?.destroy();
    this.bricks = make(40, brickBytes);
    this.code = make(40 + brickBytes, codeBytes);
    const gridStart = 40 + brickBytes + codeBytes;
    this.grids = make(gridStart, buffer.byteLength - gridStart);
    if (registers !== this.registers || !this.pipeline) this.buildPipelines(Math.max(registers, 1));
    this.bind = this.device.createBindGroup({
      layout: this.pipeline!.getBindGroupLayout(0),
      entries: [
        ...[0, 1, 2].map((binding) => ({ binding, resource: { buffer: [this.uniform, this.bricks!, this.code!][binding] } })),
        { binding: 4, resource: { buffer: this.grids! } },
      ],
    });
    this.pickBind = this.device.createBindGroup({
      layout: this.pickPipeline!.getBindGroupLayout(0),
      entries: [
        ...[0, 1, 2, 3].map((binding) => ({ binding, resource: { buffer: [this.uniform, this.bricks!, this.code!, this.pickBuffer][binding] } })),
        { binding: 4, resource: { buffer: this.grids! } },
      ],
    });
    const first = !this.scene;
    this.scene = { n, registers, lo: [box[0], box[1], box[2]], hi: [box[3], box[4], box[5]], bytes: buffer.byteLength - 40 };
    this.partLo = partLo;
    this.partHi = partHi;
    if (first) this.fit(false);
    this.request();
  }

  private partLo: Vec3 = [0, 0, 0];
  private insets = { left: 0, right: 0, top: 0, bottom: 0 };

  /** Screen space (CSS px) covered by floating panels; the camera centres the open area. */
  setInsets(i: { left: number; right: number; top: number; bottom: number }) {
    this.insets = i;
    this.request();
  }
  private lensShift(): [number, number] {
    const w = this.canvas.clientWidth || 1, h = this.canvas.clientHeight || 1;
    return [(this.insets.left - this.insets.right) / w, (this.insets.top - this.insets.bottom) / h];
  }
  private partHi: Vec3 = [0, 0, 0];

  private buildPipelines(registers: number) {
    const module = this.device.createShaderModule({ code: RAYMARCH_WGSL.replace("__MAX_SLOTS__", String(registers)) });
    this.pipeline = this.device.createRenderPipeline({
      layout: "auto",
      vertex: { module, entryPoint: "vs" },
      fragment: { module, entryPoint: "fs", targets: [{ format: this.format }] },
      primitive: { topology: "triangle-list" },
      depthStencil: { format: "depth24plus", depthWriteEnabled: true, depthCompare: "always" },
    });
    this.pickPipeline = this.device.createComputePipeline({ layout: "auto", compute: { module, entryPoint: "pick" } });
    this.registers = registers;
  }

  /** Volumetric overlay (x, y, z, best, worst per point), e.g. a robot reach cloud. */
  setCloud(points: Float32Array | null, voxelMm = 1, threshold = 0.05) {
    this.cloudBuffer?.destroy();
    this.cloudBuffer = null;
    this.cloudCount = 0;
    if (points && points.length >= 5) {
      if (!this.cloudPipeline) {
        const module = this.device.createShaderModule({ code: POINTS_WGSL });
        this.cloudPipeline = this.device.createRenderPipeline({
          layout: "auto",
          vertex: { module, entryPoint: "vs" },
          fragment: {
            module, entryPoint: "fs",
            targets: [{ format: this.format, blend: {
              color: { srcFactor: "one", dstFactor: "one-minus-src-alpha", operation: "add" },
              alpha: { srcFactor: "one", dstFactor: "one-minus-src-alpha", operation: "add" },
            } }],
          },
          primitive: { topology: "triangle-list" },
          // hidden behind the part, but never writing depth itself (translucent)
          depthStencil: { format: "depth24plus", depthWriteEnabled: false, depthCompare: "less" },
        });
        this.cloudUniform = this.device.createBuffer({ size: 16, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
      }
      this.cloudBuffer = this.device.createBuffer({ size: points.byteLength, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
      this.device.queue.writeBuffer(this.cloudBuffer, 0, points.buffer, points.byteOffset, points.byteLength);
      this.cloudBind = this.device.createBindGroup({
        layout: this.cloudPipeline.getBindGroupLayout(0),
        entries: [
          { binding: 0, resource: { buffer: this.uniform } },
          { binding: 1, resource: { buffer: this.cloudBuffer } },
          { binding: 2, resource: { buffer: this.cloudUniform! } },
        ],
      });
      this.cloudCount = points.length / 5;
      this.cloud.voxel = voxelMm;
      this.cloud.threshold = threshold;
    }
    this.request();
  }

  // ---------------------------------------------------------------- camera
  private basis() {
    const { yaw, pitch, dist, target } = this.cam;
    const cp = Math.cos(pitch), sp = Math.sin(pitch);
    const dir: Vec3 = [Math.cos(yaw) * cp, Math.sin(yaw) * cp, sp];
    const eye = target.map((t, i) => t + dir[i] * dist) as Vec3;
    const forward = dir.map((v) => -v) as Vec3;
    const ref: Vec3 = Math.abs(sp) > 0.999 ? [Math.sin(yaw), -Math.cos(yaw), 0] : [0, 0, 1];
    const right = norm(cross(forward, ref));
    const up = cross(right, forward);
    return { eye, forward, right, up };
  }

  /** Ease the camera to a goal over ~0.4 s (shortest way round in yaw). */
  animateTo(goal: { yaw?: number; pitch?: number; dist?: number; target?: Vec3 }, ms = 420) {
    cancelAnimationFrame(this.anim);
    this.spin = [0, 0];
    const from = { yaw: this.cam.yaw, pitch: this.cam.pitch, dist: this.cam.dist, target: [...this.cam.target] as Vec3 };
    let dyaw = (goal.yaw ?? from.yaw) - from.yaw;
    dyaw = Math.atan2(Math.sin(dyaw), Math.cos(dyaw));
    const t0 = performance.now();
    const step = () => {
      const u = Math.min(1, (performance.now() - t0) / ms);
      const e = 1 - Math.pow(1 - u, 3);
      this.cam.yaw = from.yaw + dyaw * e;
      this.cam.pitch = from.pitch + ((goal.pitch ?? from.pitch) - from.pitch) * e;
      this.cam.dist = from.dist * Math.pow((goal.dist ?? from.dist) / from.dist, e);
      const gt = goal.target ?? from.target;
      this.cam.target = from.target.map((v, i) => v + (gt[i] - v) * e) as Vec3;
      this.poke();
      if (u < 1) this.anim = requestAnimationFrame(step);
    };
    this.anim = requestAnimationFrame(step);
  }
  private fitGoal() {
    const lo = this.partLo, hi = this.partHi;
    const r = Math.hypot(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]) / 2 || 50;
    return { target: [(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2] as Vec3, dist: (r / Math.sin((this.cam.fov * Math.PI) / 360)) * 1.15 };
  }
  fit(animate = true) {
    if (animate) return this.animateTo(this.fitGoal());
    Object.assign(this.cam, this.fitGoal());
    this.request();
  }
  /** Look straight at a surface: camera moves onto the normal through the point. */
  lookAlong(point: Vec3, normal: Vec3) {
    const n = norm(normal);
    this.animateTo({ yaw: Math.atan2(n[1], n[0]), pitch: Math.asin(Math.max(-0.9995, Math.min(0.9995, n[2]))), target: point });
  }
  /** Frame an arbitrary box (e.g. a reach envelope larger than the part). */
  fitBox(lo: Vec3, hi: Vec3) {
    this.partLo = lo;
    this.partHi = hi;
    this.fit();
  }
  setView(name: ViewName) {
    const [yaw, pitch] = VIEWS[name];
    this.animateTo({ yaw, pitch, ...this.fitGoal() });
  }
  orbit(dx: number, dy: number) {
    cancelAnimationFrame(this.anim);
    this.cam.yaw -= dx * 0.008;
    this.cam.pitch = Math.max(-1.5699, Math.min(1.5699, this.cam.pitch + dy * 0.008));
    this.spin = [dx, dy];
    this.poke();
  }
  /** Release after an orbit drag: keep spinning and decay (inertia). */
  release() {
    let [dx, dy] = this.spin;
    if (Math.abs(dx) + Math.abs(dy) < 2) return;
    const step = () => {
      dx *= 0.9;
      dy *= 0.9;
      this.cam.yaw -= dx * 0.008;
      this.cam.pitch = Math.max(-1.5699, Math.min(1.5699, this.cam.pitch + dy * 0.008));
      this.poke();
      if (Math.abs(dx) + Math.abs(dy) > 0.15) this.anim = requestAnimationFrame(step);
    };
    this.anim = requestAnimationFrame(step);
  }
  /** Zoom toward a screen point (CSS px) so what's under the cursor stays put. */
  zoomAt(delta: number, x: number, y: number) {
    cancelAnimationFrame(this.anim);
    const f = Math.exp(delta * 0.0012);
    const next = Math.max(1, Math.min(1e5, this.cam.dist * f));
    const { right, up } = this.basis();
    const k = (this.cam.dist * 2 * Math.tan((this.cam.fov * Math.PI) / 360)) / this.canvas.clientHeight;
    const ox = (x - this.canvas.clientWidth / 2) * k, oy = (this.canvas.clientHeight / 2 - y) * k;
    const g = 1 - next / this.cam.dist;
    this.cam.target = this.cam.target.map((t, i) => t + (right[i] * ox + up[i] * oy) * g) as Vec3;
    this.cam.dist = next;
    this.poke();
  }
  pan(dx: number, dy: number) {
    const { right, up } = this.basis();
    const k = (this.cam.dist * 2 * Math.tan((this.cam.fov * Math.PI) / 360)) / this.canvas.clientHeight;
    this.cam.target = this.cam.target.map((t, i) => t - right[i] * dx * k + up[i] * dy * k) as Vec3;
    this.poke();
  }
  zoom(delta: number) {
    this.cam.dist = Math.max(1, Math.min(1e5, this.cam.dist * Math.exp(delta * 0.0012)));
    this.poke();
  }

  /** World point -> CSS pixel position on the canvas (for HTML annotations). */
  project(p: Vec3): { x: number; y: number; visible: boolean } {
    const { eye, forward, right, up } = this.basis();
    const d: Vec3 = [p[0] - eye[0], p[1] - eye[1], p[2] - eye[2]];
    const z = dot(d, forward);
    const half = this.halfHeight();
    const aspect = this.canvas.clientWidth / this.canvas.clientHeight;
    const sx = this.cam.ortho ? dot(d, right) / half : dot(d, right) / z / half;
    const sy = this.cam.ortho ? dot(d, up) / half : dot(d, up) / z / half;
    const [shx, shy] = this.lensShift();
    return {
      x: ((sx / aspect + shx) * 0.5 + 0.5) * this.canvas.clientWidth,
      y: ((-sy + shy) * 0.5 + 0.5) * this.canvas.clientHeight,
      visible: z > 0,
    };
  }
  private halfHeight() {
    const t = Math.tan((this.cam.fov * Math.PI) / 360);
    return this.cam.ortho ? this.cam.dist * t : t;
  }

  // ---------------------------------------------------------------- rendering
  poke() {
    this.interacting = true;
    this.lastPoke = performance.now();
    clearTimeout(this.idle);
    this.idle = window.setTimeout(() => this.settle(), 160);
    this.request();
  }
  /** Input stopped: draw the full-quality still, but never while a frame is in flight or input is still arriving. */
  private settle() {
    if (this.rendering || performance.now() - this.lastPoke < 150) {
      this.idle = window.setTimeout(() => this.settle(), 80);
      return;
    }
    this.interacting = false;
    this.request();
  }
  request() {
    if (this.queued) return;
    this.queued = true;
    requestAnimationFrame(() => void this.render());
  }

  private writeUniforms(pickX = 0, pickY = 0, picking = false) {
    const s = this.scene!;
    const { eye, forward, right, up } = this.basis();
    const sec = this.section;
    const axis = { off: [0, 0, 0], x: [1, 0, 0], y: [0, 1, 0], z: [0, 0, 1] }[sec.axis];
    const sign = sec.flip ? -1 : 1;
    this.device.queue.writeBuffer(this.uniform, 0, new Float32Array([
      ...eye, this.halfHeight(),
      ...forward, this.cam.ortho ? 1 : 0,
      ...right, this.canvas.width / this.canvas.height,
      ...up, this.material,
      ...s.lo, s.n,
      ...s.hi, (s.hi[0] - s.lo[0]) / s.n,
      this.canvas.width, this.canvas.height, MODE_INDEX[this.mode], this.partLo[2],
      axis[0] * sign, axis[1] * sign, axis[2] * sign, sec.offset * sign,
      this.grid ? 1 : 0, pickX, pickY, this.meshFrame && !picking ? 1 : 0,
      ...this.lensShift(), this.interacting || !this.perf.shadows ? 0 : 1, 0,
      ...this.colours.accent, 1, ...this.colours.accent2, 1, ...this.colours.bg, 1,
    ]));
  }

  /** Speed/quality preset. "auto" starts balanced and backs off on slow GPUs. */
  setPerformance(preset: "auto" | "quality" | "balanced" | "performance") {
    this.perf = {
      auto: { targetMs: 16.7, maxPixels: 3.5e6, shadows: true, auto: true, alwaysMesh: false },
      quality: { targetMs: 33, maxPixels: 3.5e6, shadows: true, auto: false, alwaysMesh: false },
      balanced: { targetMs: 16.7, maxPixels: 2.5e6, shadows: true, auto: false, alwaysMesh: false },
      performance: { targetMs: 11, maxPixels: 1.6e6, shadows: false, auto: false, alwaysMesh: true },
    }[preset];
    this.request();
  }
  /**
   * The part's triangle mesh from the engine ("DMSH": magic, version, vertices, indices | positions |
   * normals | u32 indices). Drawn instead of ray marching while the camera moves, and always on
   * low-end presets: rasterising costs a few operations per pixel, not a field per ray step.
   */
  setMesh(buffer: ArrayBuffer | null) {
    this.meshVB?.destroy();
    this.meshIB?.destroy();
    this.meshVB = this.meshIB = null;
    this.meshIndices = 0;
    if (buffer && buffer.byteLength >= 16) {
      const [magic, , nv, ni] = new Uint32Array(buffer, 0, 4);
      if (magic === 0x48534d44 && buffer.byteLength >= 16 + nv * 24 + ni * 4 && ni > 0) {
        const pos = new Float32Array(buffer, 16, nv * 3), nrm = new Float32Array(buffer, 16 + nv * 12, nv * 3);
        const vb = new Float32Array(nv * 6);
        for (let i = 0; i < nv; i++) {
          vb[i * 6] = pos[i * 3]; vb[i * 6 + 1] = pos[i * 3 + 1]; vb[i * 6 + 2] = pos[i * 3 + 2];
          vb[i * 6 + 3] = nrm[i * 3]; vb[i * 6 + 4] = nrm[i * 3 + 1]; vb[i * 6 + 5] = nrm[i * 3 + 2];
        }
        this.meshVB = this.device.createBuffer({ size: vb.byteLength, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
        this.device.queue.writeBuffer(this.meshVB, 0, vb);
        this.meshIB = this.device.createBuffer({ size: ni * 4, usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST });
        this.device.queue.writeBuffer(this.meshIB, 0, new Uint32Array(buffer, 16 + nv * 24, ni));
        this.meshIndices = ni;
        if (!this.meshPipeline) {
          const module = this.device.createShaderModule({ code: MESH_WGSL });
          this.meshPipeline = this.device.createRenderPipeline({
            layout: "auto",
            vertex: { module, entryPoint: "vs", buffers: [{ arrayStride: 24, attributes: [
              { shaderLocation: 0, offset: 0, format: "float32x3" }, { shaderLocation: 1, offset: 12, format: "float32x3" }] }] },
            fragment: { module, entryPoint: "fs", targets: [{ format: this.format }] },
            primitive: { topology: "triangle-list", cullMode: "none" },
            depthStencil: { format: "depth24plus", depthWriteEnabled: true, depthCompare: "less" },
          });
          this.meshBind = this.device.createBindGroup({ layout: this.meshPipeline.getBindGroupLayout(0), entries: [{ binding: 0, resource: { buffer: this.uniform } }] });
        }
      }
    }
    this.request();
  }
  /** Whether the last frame was drawn from the mesh. */
  get usingMesh() {
    return this.meshFrame;
  }
  /** Fraction of full resolution used for the last frame (1 when idle). */
  get renderScale() {
    return this.meshFrame ? 1 : this.interacting ? this.dyn : this.still;
  }
  private resize(scale: number) {
    // full quality is capped (~3.5 MP by default): beyond that the ray marcher costs more than it shows
    const css = this.canvas.clientWidth * this.canvas.clientHeight || 1;
    const dpr = Math.min(window.devicePixelRatio || 1, Math.sqrt(this.perf.maxPixels / css));
    const w = Math.max(1, Math.floor(this.canvas.clientWidth * dpr * scale));
    const h = Math.max(1, Math.floor(this.canvas.clientHeight * dpr * scale));
    if (this.canvas.width !== w || this.canvas.height !== h) {
      this.canvas.width = w;
      this.canvas.height = h;
    }
  }

  private async render() {
    this.queued = false;
    if (!this.scene || !this.pipeline || !this.bind) return;  // the cloud is drawn over a scene
    // one frame on the GPU at a time: a second submit would only queue behind the first
    if (this.rendering) {
      this.again = true;
      return;
    }
    this.rendering = true;
    const moving = this.interacting;
    // mesh when it exists, the view is plain shaded with no section, and the GPU benefits
    this.meshFrame = this.meshIndices > 0 && this.mode === "shaded" && this.section.axis === "off" &&
      (moving || this.perf.alwaysMesh || this.slowStills >= 2);
    // raster frames are cheap: always full (capped) resolution, so motion stays sharp
    this.resize(this.meshFrame ? 1 : moving ? this.dyn : this.still);
    this.writeUniforms();
    const enc = this.device.createCommandEncoder();
    if (!this.depth || this.depth.width !== this.canvas.width || this.depth.height !== this.canvas.height) {
      this.depth?.destroy();
      this.depth = this.device.createTexture({ size: [this.canvas.width, this.canvas.height], format: "depth24plus", usage: GPUTextureUsage.RENDER_ATTACHMENT });
    }
    const pass = enc.beginRenderPass({
      colorAttachments: [{ view: this.context.getCurrentTexture().createView(), loadOp: "clear", storeOp: "store", clearValue: { r: 0.04, g: 0.05, b: 0.06, a: 1 } }],
      depthStencilAttachment: { view: this.depth.createView(), depthClearValue: 1, depthLoadOp: "clear", depthStoreOp: "discard" },
    });
    pass.setPipeline(this.pipeline);
    pass.setBindGroup(0, this.bind);
    pass.draw(3);
    if (this.meshFrame && this.meshPipeline && this.meshBind) {
      pass.setPipeline(this.meshPipeline);
      pass.setBindGroup(0, this.meshBind);
      pass.setVertexBuffer(0, this.meshVB!);
      pass.setIndexBuffer(this.meshIB!, "uint32");
      pass.drawIndexed(this.meshIndices);
    }
    if (this.cloud.visible && this.cloudCount && this.cloudPipeline && this.cloudBind) {
      this.device.queue.writeBuffer(this.cloudUniform!, 0, new Float32Array([this.cloud.voxel, this.cloud.threshold, this.cloud.alpha, this.cloud.mode]));
      pass.setPipeline(this.cloudPipeline);
      pass.setBindGroup(0, this.cloudBind);
      pass.draw(6, this.cloudCount);
    }
    pass.end();
    const t0 = performance.now();
    this.device.queue.submit([enc.finish()]);
    await this.device.queue.onSubmittedWorkDone();
    const ms = performance.now() - t0;
    if (this.meshFrame) {
      // raster frames are cheap: nothing to adapt
    } else if (moving) {
      // frame cost scales with pixel count: move the scale towards the target, gently
      const k = Math.max(0.7, Math.min(1.2, Math.sqrt(this.perf.targetMs / Math.max(ms, 0.5))));
      this.dyn = Math.max(0.12, Math.min(1, this.dyn * k));
    } else {
      if (this.perf.auto && ms > 150 && this.perf.shadows) {
        // a slow GPU: full-quality stills are costing too much, so drop shadows and pixels for good
        this.perf.shadows = false;
        this.perf.maxPixels = Math.min(this.perf.maxPixels, 1.5e6);
      }
      // the GPU cannot be interrupted: keep stills short so the next input is not stuck behind one
      if (ms > 250) this.still = Math.max(0.3, this.still * Math.sqrt(250 / ms));
      // on Auto, a GPU that keeps taking long over exact stills gets mesh stills for this part
      if (this.perf.auto && ms > 120) this.slowStills++;
      else if (ms < 100 && this.still < 1) this.still = Math.min(1, this.still * 1.15);
    }
    this.rendering = false;
    this.onFrame?.(ms, this.canvas.width, this.canvas.height);
    if (this.again) {
      this.again = false;
      this.request();
    }
  }

  /** Full-quality frame as a PNG (the drawing buffer is read in the same task as the submit). */
  async capture(): Promise<Blob | null> {
    this.interacting = false;
    clearTimeout(this.idle);
    while (this.rendering) await new Promise((r) => setTimeout(r, 30));
    const keep = this.still;
    this.still = 1;  // a saved image is always full resolution
    void this.render();
    this.still = keep;
    return new Promise((resolve) => this.canvas.toBlob(resolve, "image/png"));
  }

  /** Trace one ray at a CSS pixel position: surface point and normal, or null. */
  async pick(cssX: number, cssY: number): Promise<PickResult | null> {
    if (!this.scene || !this.pickPipeline || !this.pickBind) return null;
    const run = async () => {
      const scaleX = this.canvas.width / this.canvas.clientWidth, scaleY = this.canvas.height / this.canvas.clientHeight;
      this.writeUniforms(cssX * scaleX, cssY * scaleY, true);
      const enc = this.device.createCommandEncoder();
      const pass = enc.beginComputePass();
      pass.setPipeline(this.pickPipeline!);
      pass.setBindGroup(0, this.pickBind!);
      pass.dispatchWorkgroups(1);
      pass.end();
      enc.copyBufferToBuffer(this.pickBuffer, 0, this.pickRead, 0, 48);
      this.device.queue.submit([enc.finish()]);
      await this.pickRead.mapAsync(GPUMapMode.READ);
      const f = new Float32Array(this.pickRead.getMappedRange().slice(0));
      this.pickRead.unmap();
      if (f[3] < 0.5) return null;
      return { position: [f[0], f[1], f[2]] as Vec3, normal: [f[4], f[5], f[6]] as Vec3, cap: f[7] > 0.5, probe: [f[8], f[9], f[10], f[11]] };
    };
    // serialise picks: the readback buffer can only be mapped once at a time
    const result = this.picking.then(run, run);
    this.picking = result.catch(() => undefined);
    return result;
  }

  destroy() {
    this.bricks?.destroy();
    this.code?.destroy();
    this.device?.destroy();
  }
}

const cross = (a: Vec3, b: Vec3): Vec3 => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const dot = (a: Vec3, b: Vec3) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const norm = (a: Vec3): Vec3 => {
  const l = Math.hypot(...a) || 1;
  return [a[0] / l, a[1] / l, a[2] / l];
};
