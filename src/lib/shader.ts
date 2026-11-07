// WGSL for the brick ray marcher (see kernel/src/bricks.cpp for the data layout).
// MAX_SLOTS is substituted with the register count the kernel's allocator reports.
export const RAYMARCH_WGSL = /* wgsl */ `
struct Uniforms {
  eye: vec4f,      // xyz, w = tan(fov/2) (perspective) or half-height in mm (ortho)
  forward: vec4f,  // xyz, w = 1 ortho / 0 perspective
  right: vec4f,    // xyz, w = aspect
  up: vec4f,
  lo: vec4f,       // grid min, w = n
  hi: vec4f,       // grid max, w = brick size
  view: vec4f,     // width, height, mode (0 shaded 1 cost 2 normals 3 xray), floor z
  section: vec4f,  // plane normal xyz (0 = off), offset: keep dot(p, n) <= w
  misc: vec4f,     // grid on, pick x, pick y, unused
  shift: vec4f,    // lens shift (NDC) x, y to centre the open area; z = high quality (shadows)
};
struct Instr { op: u32, a: u32, b: u32, c: f32 };
struct PickOut { pos: vec4f, normal: vec4f };

@group(0) @binding(0) var<uniform> U: Uniforms;
@group(0) @binding(1) var<storage, read> bricks: array<u32>;
@group(0) @binding(2) var<storage, read> code: array<Instr>;
@group(0) @binding(4) var<storage, read> grids: array<f32>;  // count | headers | data

// trilinear signed-distance grid (imported parts); matches meshfield.cpp
fn grid_sample(gid: u32, p: vec3f) -> f32 {
  let b = 1u + gid * 8u;
  let lo = vec3f(grids[b], grids[b + 1u], grids[b + 2u]);
  let h = grids[b + 3u];
  let n = vec3u(bitcast<u32>(grids[b + 4u]), bitcast<u32>(grids[b + 5u]), bitcast<u32>(grids[b + 6u]));
  let off = bitcast<u32>(grids[b + 7u]);
  let hi = lo + vec3f(n - vec3u(1u)) * h;
  let q = clamp(p, lo, hi);
  let f = (q - lo) / h;
  let i = min(vec3u(f), n - vec3u(2u));
  let t = f - vec3f(i);
  let sx = n.x;
  let sxy = n.x * n.y;
  let i000 = off + i.z * sxy + i.y * sx + i.x;
  let c000 = grids[i000];
  let c100 = grids[i000 + 1u];
  let c010 = grids[i000 + sx];
  let c110 = grids[i000 + sx + 1u];
  let c001 = grids[i000 + sxy];
  let c101 = grids[i000 + sxy + 1u];
  let c011 = grids[i000 + sxy + sx];
  let c111 = grids[i000 + sxy + sx + 1u];
  let x00 = mix(c000, c100, t.x);
  let x10 = mix(c010, c110, t.x);
  let x01 = mix(c001, c101, t.x);
  let x11 = mix(c011, c111, t.x);
  let v = mix(mix(x00, x10, t.y), mix(x01, x11, t.y), t.z);
  let out = length(p - q);
  return select(v, max(out, v - out), out > 0.0);  // lower bound outside the grid (see meshfield.cpp)
}

const MAX_SLOTS: u32 = __MAX_SLOTS__;
const FAR: f32 = 1e7;

// Depth shared by every pass: monotonic in view distance, 0 at the eye, -> 1 far away.
// Scaled by the scene size so precision stays useful near the part.
var<private> g_depth: f32 = 1.0;
fn view_depth(p: vec3f) -> f32 {
  let z = max(dot(p - U.eye.xyz, U.forward.xyz), 0.0);
  return z / (z + U.hi.w * U.lo.w);
}

fn eval_program(p: vec3f, off: u32, len: u32) -> f32 {
  var s: array<f32, MAX_SLOTS>;
  var out = 0u;
  for (var i = 0u; i < len; i++) {
    let ins = code[off + i];
    var v: f32;
    switch ins.op & 255u {
      case 0u: { v = p.x; }
      case 1u: { v = p.y; }
      case 2u: { v = p.z; }
      case 3u: { v = ins.c; }
      case 4u: { v = s[ins.a] + s[ins.b]; }
      case 5u: { v = s[ins.a] - s[ins.b]; }
      case 6u: { v = s[ins.a] * s[ins.b]; }
      case 7u: { v = s[ins.a] / s[ins.b]; }
      case 8u: { v = -s[ins.a]; }
      case 9u: { v = min(s[ins.a], s[ins.b]); }
      case 10u: { v = max(s[ins.a], s[ins.b]); }
      case 11u: { v = abs(s[ins.a]); }
      case 12u: { v = sqrt(max(s[ins.a], 0.0)); }
      case 13u: { v = s[ins.a] * s[ins.a]; }
      case 14u: { v = sin(s[ins.a]); }
      case 15u: { v = cos(s[ins.a]); }
      case 16u: { v = select(abs(s[ins.a]), -abs(s[ins.a]), s[ins.b] < 0.0); }
      case 17u: { v = grid_sample(ins.op >> 24u, vec3f(s[ins.a], s[ins.b], s[u32(ins.c)])); }
      default: { v = 0.0; }
    }
    out = (ins.op >> 8u) & 0xFFFFu;
    s[out] = v;
  }
  return s[out];
}

fn section_d(p: vec3f) -> f32 {
  if (dot(U.section.xyz, U.section.xyz) == 0.0) { return -FAR; }
  return dot(p, U.section.xyz) - U.section.w;
}
fn brick_coord(p: vec3f) -> vec3i {
  let n = i32(U.lo.w);
  return clamp(vec3i(floor((p - U.lo.xyz) / U.hi.w)), vec3i(0), vec3i(n - 1));
}
fn brick_index(c: vec3i) -> u32 {
  let n = u32(U.lo.w);
  return u32(c.x) + n * (u32(c.y) + n * u32(c.z));
}
fn in_grid(p: vec3f) -> bool { return all(p >= U.lo.xyz) && all(p <= U.hi.xyz); }

// part field without the section (empty/solid bricks: conservative +/- half brick)
fn part(p: vec3f) -> f32 {
  if (!in_grid(p)) { return U.hi.w; }
  let b = brick_index(brick_coord(p)) * 3u;
  let k = bricks[b];
  if (k == 0u) { return U.hi.w * 0.5; }
  if (k == 1u) { return -U.hi.w * 0.5; }
  return eval_program(p, bricks[b + 1u], bricks[b + 2u]);
}

fn brick_exit(ro: vec3f, inv: vec3f, bmin: vec3f) -> f32 {
  let t0 = (bmin - ro) * inv;
  let t1 = (bmin + vec3f(U.hi.w) - ro) * inv;
  let tf = max(t0, t1);
  return min(min(tf.x, tf.y), tf.z);
}

struct Hit { t: f32, evals: u32, brick: i32, cap: bool };

fn trace(ro: vec3f, rd: vec3f) -> Hit {
  var hit = Hit(FAR, 0u, -1, false);
  let inv = 1.0 / rd;
  let t0 = (U.lo.xyz - ro) * inv;
  let t1 = (U.hi.xyz - ro) * inv;
  let tnear = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), min(t0.z, t1.z));
  let tfar = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), max(t0.z, t1.z));
  if (tnear > tfar || tfar < 0.0) { return hit; }
  var t = max(tnear, 0.0) + 1e-4;
  let eps = 1e-4 * U.hi.w;
  for (var i = 0u; i < 600u; i++) {
    if (t > tfar) { break; }
    let p = ro + rd * t;
    let c = brick_coord(p);
    let exit = brick_exit(ro, inv, U.lo.xyz + vec3f(c) * U.hi.w);
    let b = brick_index(c) * 3u;
    let kind = bricks[b];
    if (kind == 0u) { t = exit + eps; continue; }
    var d = -FAR;
    if (kind == 2u) {
      d = eval_program(p, bricks[b + 1u], bricks[b + 2u]);
      hit.evals += bricks[b + 2u];
    }
    let s = section_d(p);
    let ds = max(d, s);
    if (ds < 2e-4 * max(t, 1.0)) {
      hit.t = t; hit.brick = i32(b / 3u); hit.cap = s > d;
      return hit;
    }
    // pruned programs are exact only inside their brick: never step past its exit
    t += min(ds * 0.9, exit - t + eps);
  }
  return hit;
}

fn normal_at(p: vec3f, h: Hit) -> vec3f {
  if (h.cap) { return normalize(U.section.xyz); }
  let b = u32(h.brick) * 3u;
  if (bricks[b] != 2u) { return vec3f(0.0, 0.0, 1.0); }
  let off = bricks[b + 1u];
  let len = bricks[b + 2u];
  let e = 0.002 * U.hi.w;
  let k = vec2f(1.0, -1.0);
  return normalize(k.xyy * eval_program(p + k.xyy * e, off, len) + k.yyx * eval_program(p + k.yyx * e, off, len) +
                   k.yxy * eval_program(p + k.yxy * e, off, len) + k.xxx * eval_program(p + k.xxx * e, off, len));
}

fn camera_ray(px: vec2f) -> array<vec3f, 2> {
  let uv = (px / U.view.xy) * 2.0 - 1.0 - U.shift.xy;
  let sx = uv.x * U.eye.w * U.right.w;
  let sy = -uv.y * U.eye.w;
  if (U.forward.w > 0.5) {
    return array<vec3f, 2>(U.eye.xyz + U.right.xyz * sx + U.up.xyz * sy, U.forward.xyz);
  }
  return array<vec3f, 2>(U.eye.xyz, normalize(U.forward.xyz + U.right.xyz * sx + U.up.xyz * sy));
}

fn ao(p: vec3f, n: vec3f) -> f32 {
  var occ = 0.0;
  var w = 1.0;
  let step = 0.15 * U.hi.w;
  for (var i = 1; i <= 4; i++) {
    let h = step * f32(i);
    occ += w * (h - part(p + n * h));
    w *= 0.6;
  }
  return clamp(1.0 - occ / (step * 2.2), 0.0, 1.0);
}

fn heat(x: f32) -> vec3f {
  let t = clamp(x, 0.0, 1.0);
  return clamp(vec3f(1.5 * t, 1.5 * t * t, 0.6 * (1.0 - t)) + vec3f(0.05), vec3f(0.0), vec3f(1.0));
}

@vertex
fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
  let xy = vec2f(f32((i << 1u) & 2u), f32(i & 2u)) * 2.0 - 1.0;
  return vec4f(xy, 0.0, 1.0);
}

// soft shadow toward a light, traced through the field (penumbra from the closest miss)
fn soft_shadow(p: vec3f, l: vec3f) -> f32 {
  var res = 1.0;
  var t = 0.02 * U.hi.w;
  let tmax = U.hi.w * U.lo.w * 1.5;
  for (var i = 0; i < 22; i++) {
    let q = p + l * t;
    if (!in_grid(q)) { break; }
    let h = part(q);
    if (h < 1e-3 * U.hi.w) { return 0.0; }
    res = min(res, 10.0 * h / t);
    t += clamp(h, 0.06 * U.hi.w, 0.6 * U.hi.w);
    if (t > tmax) { break; }
  }
  return clamp(res, 0.0, 1.0);
}

// studio environment: soft sky gradient, two softboxes, teal / gold bounce
fn environment(r: vec3f) -> vec3f {
  var c = mix(vec3f(0.035, 0.038, 0.048), vec3f(0.20, 0.22, 0.26), clamp(r.z * 0.5 + 0.5, 0.0, 1.0));
  c += vec3f(1.6) * pow(max(dot(r, normalize(vec3f(0.3, -0.5, 0.8))), 0.0), 60.0);
  c += vec3f(0.8) * pow(max(dot(r, normalize(vec3f(-0.7, 0.4, 0.6))), 0.0), 30.0);
  c += vec3f(0.0, 0.55, 0.50) * 0.25 * pow(max(-r.x, 0.0), 3.0);
  c += vec3f(0.85, 0.62, 0.25) * 0.20 * pow(max(r.x, 0.0), 3.0);
  return c;
}

// Cook-Torrance GGX specular with Schlick Fresnel
fn ggx(n: vec3f, v: vec3f, l: vec3f, rough: f32, f0: vec3f) -> vec3f {
  let h = normalize(v + l);
  let nl = max(dot(n, l), 0.0);
  let nv = max(dot(n, v), 1e-3);
  let nh = max(dot(n, h), 0.0);
  let a2 = rough * rough * rough * rough;
  let d = a2 / (3.14159 * pow(nh * nh * (a2 - 1.0) + 1.0, 2.0));
  let k = (rough + 1.0) * (rough + 1.0) / 8.0;
  let g = nl / (nl * (1.0 - k) + k) * nv / (nv * (1.0 - k) + k);
  let f = f0 + (1.0 - f0) * pow(1.0 - max(dot(h, v), 0.0), 5.0);
  return d * g * f / (4.0 * nv * max(nl, 1e-3));
}

struct FOut { @location(0) color: vec4f, @builtin(frag_depth) depth: f32 };

@fragment
fn fs(@builtin(position) frag: vec4f) -> FOut {
  g_depth = 1.0;
  let c = shade(frag);
  return FOut(c, g_depth);
}

fn shade(frag: vec4f) -> vec4f {
  let ray = camera_ray(frag.xy);
  let ro = ray[0];
  let rd = ray[1];
  let uv = (frag.xy / U.view.xy) * 2.0 - 1.0 - U.shift.xy;
  let hq = U.shift.z > 0.5;
  // canvas with teal / gold ambient blooms
  var col = vec3f(0.035, 0.038, 0.050);
  col += vec3f(0.0, 0.85, 0.75) * 0.075 * exp(-2.2 * dot(uv - vec2f(-0.75, -0.55), uv - vec2f(-0.75, -0.55)));
  col += vec3f(0.95, 0.68, 0.25) * 0.060 * exp(-2.2 * dot(uv - vec2f(0.85, -0.65), uv - vec2f(0.85, -0.65)));
  col += vec3f(0.30, 0.35, 0.45) * 0.035 * exp(-1.5 * dot(uv - vec2f(0.0, 0.9), uv - vec2f(0.0, 0.9)));

  var tfloor = FAR;
  if (U.misc.x > 0.5 && rd.z < 0.0 && ro.z > U.view.w) { tfloor = (U.view.w - ro.z) / rd.z; }
  let key = normalize(vec3f(0.45, -0.55, 0.85));

  let h = trace(ro, rd);
  if (h.t < FAR && h.t <= tfloor + 1e-3) {
    let p = ro + rd * h.t;
    g_depth = view_depth(p);
    if (U.view.z == 1.0) { return vec4f(heat(f32(h.evals) / 2500.0), 1.0); }
    let n = normal_at(p, h);
    if (U.view.z == 2.0) { return vec4f(n * 0.5 + 0.5, 1.0); }
    let v = -rd;
    if (U.view.z == 3.0) {
      let fx = pow(1.0 - abs(dot(n, v)), 2.5);
      return vec4f(pow(mix(vec3f(0.02, 0.05, 0.06), vec3f(0.0, 1.0, 0.85), fx), vec3f(1.0 / 2.2)), 1.0);
    }
    var base = vec3f(0.90, 0.91, 0.92);  // bead-blasted aluminium
    var metal = 0.9;
    var rough = 0.34;
    if (h.cap) {
      let hatch = step(0.5, fract((p.x + p.y + p.z) / (0.12 * U.hi.w)));
      base = mix(vec3f(0.0, 0.55, 0.48), vec3f(0.0, 0.70, 0.60), hatch);
      metal = 0.0;
      rough = 0.6;
    }
    let f0 = mix(vec3f(0.04), base, metal);
    let occ = ao(p, n);
    var sh = 1.0;
    if (hq) { sh = soft_shadow(p + n * (0.01 * U.hi.w), key); }
    let nl = max(dot(n, key), 0.0);
    let diffuse = base * (1.0 - metal) / 3.14159;
    col = (diffuse + ggx(n, v, key, rough, f0)) * nl * sh * vec3f(3.2, 3.05, 2.9);
    // image-based ambient: rough reflection of the studio environment
    let r = reflect(rd, n);
    let fres = f0 + (1.0 - f0) * pow(1.0 - max(dot(n, v), 0.0), 5.0);
    col += environment(normalize(mix(r, n, rough * rough))) * fres * occ;
    col += base * (1.0 - metal) * environment(n) * 0.35 * occ;
    // rim lights
    col += vec3f(0.0, 0.85, 0.75) * 0.22 * pow(1.0 - abs(dot(n, v)), 3.0) * max(-n.x, 0.0) * occ;
    col += vec3f(0.95, 0.68, 0.25) * 0.18 * pow(1.0 - abs(dot(n, v)), 3.0) * max(n.x, 0.0) * occ;
  } else if (tfloor < FAR) {
    let p = ro + rd * tfloor;
    g_depth = view_depth(p);
    let major = pow(10.0, floor(log2(max(U.hi.w * 6.0, 1.0)) * 0.30103));
    let g = abs(fract(p.xy / major + 0.5) - 0.5) * major;
    let width = 0.012 * major + 0.0015 * tfloor * select(U.eye.w, 0.0, U.forward.w > 0.5);
    let line = 1.0 - smoothstep(0.0, width, min(g.x, g.y));
    let fade = exp(-length(p.xy - (U.lo.xy + U.hi.xy) * 0.5) / (U.hi.w * U.lo.w * 1.5));
    col = mix(col, vec3f(0.14, 0.15, 0.18), line * fade * 0.8);
    var sh = 1.0;
    if (hq) { sh = soft_shadow(p + vec3f(0.0, 0.0, 0.01 * U.hi.w), key); }
    let contact = clamp(part(p + vec3f(0.0, 0.0, 0.4 * U.hi.w)) / (1.2 * U.hi.w), 0.0, 1.0);
    col *= mix(0.35, 1.0, sh) * mix(0.55, 1.0, contact * contact);
  }
  // ACES filmic tone map + gamma
  col = clamp((col * (2.51 * col + 0.03)) / (col * (2.43 * col + 0.59) + 0.14), vec3f(0.0), vec3f(1.0));
  return vec4f(pow(col, vec3f(1.0 / 2.2)), 1.0);
}

@group(0) @binding(3) var<storage, read_write> pick_out: PickOut;

@compute @workgroup_size(1)
fn pick() {
  let ray = camera_ray(U.misc.yz);
  let h = trace(ray[0], ray[1]);
  if (h.t >= FAR) { pick_out.pos = vec4f(0.0, 0.0, 0.0, 0.0); return; }
  let p = ray[0] + ray[1] * h.t;
  pick_out.pos = vec4f(p, 1.0);
  pick_out.normal = vec4f(normal_at(p, h), select(0.0, 1.0, h.cap));
}
`;
