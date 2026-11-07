// WebGPU host for the brick ray marcher: loads scene.json from the kernel exporter,
// uploads bricks + bytecode, and renders on demand. While the camera moves it renders
// at reduced resolution and refines to full resolution once input stops.

const $ = (id) => document.getElementById(id);

function fail(message) {
  document.body.insertAdjacentHTML("beforeend", `<div class="err">${message}</div>`);
  throw new Error(message);
}

const canvas = $("view");
if (!navigator.gpu) fail("WebGPU is not available in this browser.");
const adapter = await navigator.gpu.requestAdapter({ powerPreference: "high-performance" });
if (!adapter) fail("No WebGPU adapter found.");
const device = await adapter.requestDevice();
const context = canvas.getContext("webgpu");
const format = navigator.gpu.getPreferredCanvasFormat();
context.configure({ device, format, alphaMode: "opaque" });

const scene = await (await fetch("scene.json")).json();
const shaderSource = (await (await fetch("raymarch.wgsl")).text()).replace("__MAX_SLOTS__", String(Math.max(scene.max_registers, 1)));

// ---- buffers
const brickData = new Uint32Array(scene.bricks);
const codeData = new ArrayBuffer(Math.max(scene.code.length / 4, 1) * 16);
{
  const u = new Uint32Array(codeData), f = new Float32Array(codeData);
  for (let i = 0; i < scene.code.length / 4; i++) {
    u[i * 4] = scene.code[i * 4];
    u[i * 4 + 1] = scene.code[i * 4 + 1];
    u[i * 4 + 2] = scene.code[i * 4 + 2];
    f[i * 4 + 3] = scene.code[i * 4 + 3];
  }
}
const storage = (data) => {
  const buffer = device.createBuffer({ size: Math.max(data.byteLength, 16), usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
  device.queue.writeBuffer(buffer, 0, data);
  return buffer;
};
const brickBuffer = storage(brickData);
const codeBuffer = storage(codeData);
const uniformBuffer = device.createBuffer({ size: 112, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });

const module = device.createShaderModule({ code: shaderSource });
const info = await module.getCompilationInfo();
const errors = info.messages.filter((m) => m.type === "error");
if (errors.length) fail("WGSL error: " + errors.map((m) => `${m.lineNum}:${m.linePos} ${m.message}`).join("<br>"));

const pipeline = device.createRenderPipeline({
  layout: "auto",
  vertex: { module, entryPoint: "vs" },
  fragment: { module, entryPoint: "fs", targets: [{ format }] },
  primitive: { topology: "triangle-list" },
});
const bindGroup = device.createBindGroup({
  layout: pipeline.getBindGroupLayout(0),
  entries: [
    { binding: 0, resource: { buffer: uniformBuffer } },
    { binding: 1, resource: { buffer: brickBuffer } },
    { binding: 2, resource: { buffer: codeBuffer } },
  ],
});

// ---- stats
const surface = scene.bricks.filter((_, i) => i % 3 === 0 && scene.bricks[i] === 2).length;
$("s-name").textContent = scene.name;
$("s-bricks").textContent = `${surface} / ${scene.n ** 3}`;
$("s-prog").textContent = `${(scene.code.length / 4 / Math.max(surface, 1)).toFixed(1)} / ${scene.max_length}`;

// ---- camera: z-up orbit around the part
const lo = scene.lo, hi = scene.hi;
const cam = { yaw: -2.3, pitch: 0.42, dist: 190, target: [0, 0, 22], fov: 32 };
let mode = 0;

function basis() {
  const cp = Math.cos(cam.pitch), sp = Math.sin(cam.pitch);
  const dir = [Math.cos(cam.yaw) * cp, Math.sin(cam.yaw) * cp, sp]; // from target to eye
  const eye = cam.target.map((t, i) => t + dir[i] * cam.dist);
  const forward = dir.map((v) => -v);
  const right = norm(cross(forward, [0, 0, 1]));
  const up = cross(right, forward);
  return { eye, forward, right, up };
}
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const norm = (a) => { const l = Math.hypot(...a) || 1; return a.map((v) => v / l); };

// ---- rendering on demand with progressive resolution
let interacting = false, idleTimer = 0, queued = false, scale = 1;

function resize(s) {
  const dpr = Math.min(window.devicePixelRatio || 1, 2);
  const w = Math.max(1, Math.floor(canvas.clientWidth * dpr * s));
  const h = Math.max(1, Math.floor(canvas.clientHeight * dpr * s));
  if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
}

async function render() {
  queued = false;
  scale = interacting ? 0.5 : 1;
  resize(scale);
  const { eye, forward, right, up } = basis();
  const tanHalf = Math.tan((cam.fov * Math.PI) / 360);
  const u = new Float32Array([
    ...eye, tanHalf, ...forward, 0, ...right, canvas.width / canvas.height, ...up, 0,
    ...lo, scene.n, ...hi, (hi[0] - lo[0]) / scene.n,
    canvas.width, canvas.height, mode, -1,
  ]);
  device.queue.writeBuffer(uniformBuffer, 0, u);
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginRenderPass({
    colorAttachments: [{ view: context.getCurrentTexture().createView(), loadOp: "clear", storeOp: "store", clearValue: { r: 0.04, g: 0.05, b: 0.06, a: 1 } }],
  });
  pass.setPipeline(pipeline);
  pass.setBindGroup(0, bindGroup);
  pass.draw(3);
  pass.end();
  const t0 = performance.now();
  device.queue.submit([encoder.finish()]);
  await device.queue.onSubmittedWorkDone();
  const ms = performance.now() - t0;
  $("s-ms").textContent = `${ms.toFixed(1)} ms`;
  $("s-res").textContent = `${canvas.width}×${canvas.height}${scale < 1 ? " (preview)" : ""}`;
  window.__lastFrame = { ms, width: canvas.width, height: canvas.height, mode };
}

function request() {
  if (!queued) { queued = true; requestAnimationFrame(render); }
}
function poke() {
  interacting = true;
  clearTimeout(idleTimer);
  idleTimer = setTimeout(() => { interacting = false; request(); }, 140);
  request();
}

// ---- input
let drag = null;
canvas.addEventListener("contextmenu", (e) => e.preventDefault());
canvas.addEventListener("pointerdown", (e) => { drag = { x: e.clientX, y: e.clientY, pan: e.button === 2 || e.shiftKey }; canvas.setPointerCapture(e.pointerId); });
canvas.addEventListener("pointerup", () => { drag = null; });
canvas.addEventListener("pointermove", (e) => {
  if (!drag) return;
  const dx = e.clientX - drag.x, dy = e.clientY - drag.y;
  drag.x = e.clientX; drag.y = e.clientY;
  if (drag.pan) {
    const { right, up } = basis();
    const k = cam.dist * 0.0016;
    cam.target = cam.target.map((t, i) => t - right[i] * dx * k + up[i] * dy * k);
  } else {
    cam.yaw -= dx * 0.008;
    cam.pitch = Math.max(-1.45, Math.min(1.45, cam.pitch + dy * 0.008));
  }
  poke();
});
canvas.addEventListener("wheel", (e) => { e.preventDefault(); cam.dist = Math.max(15, Math.min(800, cam.dist * Math.exp(e.deltaY * 0.0012))); poke(); }, { passive: false });
window.addEventListener("resize", request);
document.querySelectorAll("[data-mode]").forEach((b) =>
  b.addEventListener("click", () => {
    mode = Number(b.dataset.mode);
    document.querySelectorAll("[data-mode]").forEach((x) => x.setAttribute("aria-pressed", String(x === b)));
    request();
  }),
);

// test hook: set the camera and render at full resolution, resolves with the frame stats
window.__setView = async (patch) => { Object.assign(cam, patch); interacting = false; await render(); return window.__lastFrame; };

request();
