// Sphere tracing a brick-pruned field program.
//
// The scene is an n^3 grid of bricks over [lo, hi]. Each brick is empty (skip to its exit),
// solid, or holds the field program pruned to that brick (bytecode in `code`). The fragment
// shader marches each pixel's ray brick by brick and interprets only the local program.
// MAX_SLOTS is substituted by the host with the register count from the exporter.

struct Uniforms {
    eye: vec4f,        // xyz eye, w = vertical tan(fov/2)
    forward: vec4f,
    right: vec4f,      // xyz, w = aspect
    up: vec4f,
    lo: vec4f,         // xyz grid min, w = n
    hi: vec4f,         // xyz grid max, w = brick size
    view: vec4f,       // x = width, y = height, z = mode (0 shaded, 1 cost heatmap, 2 normals), w = hovered brick or -1
};

struct Instr { op: u32, a: u32, b: u32, c: f32 };

@group(0) @binding(0) var<uniform> U: Uniforms;
@group(0) @binding(1) var<storage, read> bricks: array<u32>;   // [kind, offset, length] per brick
@group(0) @binding(2) var<storage, read> code: array<Instr>;

const MAX_SLOTS: u32 = __MAX_SLOTS__;
const FAR: f32 = 1e5;

fn eval_program(p: vec3f, off: u32, len: u32) -> f32 {
    // register-allocated bytecode: op = opcode | dst << 8, a / b are slots
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
            default: { v = 0.0; }
        }
        out = ins.op >> 8u;
        s[out] = v;
    }
    return s[out];
}

fn brick_coord(p: vec3f) -> vec3i {
    let n = i32(U.lo.w);
    return clamp(vec3i(floor((p - U.lo.xyz) / U.hi.w)), vec3i(0), vec3i(n - 1));
}
fn brick_index(c: vec3i) -> u32 {
    let n = u32(U.lo.w);
    return u32(c.x) + n * (u32(c.y) + n * u32(c.z));
}
fn inside_grid(p: vec3f) -> bool {
    return all(p >= U.lo.xyz) && all(p <= U.hi.xyz);
}

// Field anywhere: empty / solid bricks return a conservative +/- brick-size value.
fn field(p: vec3f) -> f32 {
    if (!inside_grid(p)) { return U.hi.w; }
    let b = brick_index(brick_coord(p)) * 3u;
    let kind = bricks[b];
    if (kind == 0u) { return U.hi.w * 0.5; }
    if (kind == 1u) { return -U.hi.w * 0.5; }
    return eval_program(p, bricks[b + 1u], bricks[b + 2u]);
}

fn normal_at(p: vec3f, brick: i32) -> vec3f {
    // Tetrahedral finite differences with the hit brick's own program. Using field() here
    // would let a sample 4 um across a seam into an "empty" brick return its constant
    // +brick/2 and wreck the normal, which drew dark lines along brick planes.
    let e = 0.004;
    let k = vec2f(1.0, -1.0);
    let b = u32(brick) * 3u;
    if (bricks[b] != 2u) { return vec3f(0.0, 0.0, 1.0); }
    let off = bricks[b + 1u];
    let len = bricks[b + 2u];
    return normalize(k.xyy * eval_program(p + k.xyy * e, off, len) + k.yyx * eval_program(p + k.yyx * e, off, len) +
                     k.yxy * eval_program(p + k.yxy * e, off, len) + k.xxx * eval_program(p + k.xxx * e, off, len));
}

// distance along the ray to leave the brick with min corner bmin
fn brick_exit(ro: vec3f, inv: vec3f, t: f32, bmin: vec3f) -> f32 {
    let t0 = (bmin - ro) * inv;
    let t1 = (bmin + vec3f(U.hi.w) - ro) * inv;
    let tf = max(t0, t1);
    return min(min(tf.x, tf.y), tf.z);
}

struct Hit { t: f32, steps: u32, evals: u32, brick: i32 };

fn trace(ro: vec3f, rd: vec3f) -> Hit {
    var hit = Hit(FAR, 0u, 0u, -1);
    let inv = 1.0 / rd;
    let t0 = (U.lo.xyz - ro) * inv;
    let t1 = (U.hi.xyz - ro) * inv;
    let tnear = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), min(t0.z, t1.z));
    let tfar = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), max(t0.z, t1.z));
    if (tnear > tfar || tfar < 0.0) { return hit; }
    var t = max(tnear, 0.0) + 1e-4;
    for (var i = 0u; i < 512u; i++) {
        if (t > tfar) { break; }
        hit.steps = i;
        let p = ro + rd * t;
        let c = brick_coord(p);
        let bmin = U.lo.xyz + vec3f(c) * U.hi.w;
        let exit = brick_exit(ro, inv, t, bmin);
        let b = brick_index(c) * 3u;
        let kind = bricks[b];
        if (kind == 0u) { t = exit + 1e-4; continue; }
        if (kind == 1u) { hit.t = t; hit.brick = i32(b / 3u); return hit; }
        let d = eval_program(p, bricks[b + 1u], bricks[b + 2u]);
        hit.evals += bricks[b + 2u];
        if (d < 2e-4 * t) { hit.t = t; hit.brick = i32(b / 3u); return hit; }
        // the pruned program is only exact inside its brick: never step past the exit
        t += min(d * 0.9, exit - t + 1e-4);
    }
    return hit;
}

fn soft_ao(p: vec3f, n: vec3f) -> f32 {
    var occ = 0.0;
    var w = 1.0;
    for (var i = 1; i <= 5; i++) {
        let h = 0.6 * f32(i);
        occ += w * (h - field(p + n * h));
        w *= 0.6;
    }
    return clamp(1.0 - occ * 0.35, 0.0, 1.0);
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

@fragment
fn fs(@builtin(position) frag: vec4f) -> @location(0) vec4f {
    let uv = (frag.xy / U.view.xy) * 2.0 - 1.0;
    let rd = normalize(U.forward.xyz + U.right.xyz * uv.x * U.eye.w * U.right.w - U.up.xyz * uv.y * U.eye.w);
    let ro = U.eye.xyz;

    // background: canvas with faint teal / amber blooms
    var col = vec3f(0.043, 0.047, 0.063);
    col += vec3f(0.31, 0.82, 0.77) * 0.06 * exp(-4.0 * dot(uv - vec2f(-0.7, -0.4), uv - vec2f(-0.7, -0.4)));
    col += vec3f(0.94, 0.71, 0.35) * 0.05 * exp(-4.0 * dot(uv - vec2f(0.8, -0.5), uv - vec2f(0.8, -0.5)));

    // floor at z = 0 with a mm grid
    var tfloor = FAR;
    if (rd.z < 0.0) { tfloor = -ro.z / rd.z; }

    let h = trace(ro, rd);
    if (h.t < FAR && h.t < tfloor + 1e-3) {
        let p = ro + rd * h.t;
        let n = normal_at(p, h.brick);
        if (U.view.z == 1.0) {
            return vec4f(heat(f32(h.evals) / 2500.0), 1.0);
        }
        if (U.view.z == 2.0) {
            return vec4f(n * 0.5 + 0.5, 1.0);
        }
        let key = normalize(vec3f(0.5, -0.6, 0.8));
        let v = -rd;
        let hv = normalize(key + v);
        let diff = max(dot(n, key), 0.0);
        let spec = pow(max(dot(n, hv), 0.0), 48.0) * 0.5;
        let fres = pow(1.0 - max(dot(n, v), 0.0), 4.0);
        let ao = soft_ao(p, n);
        let base = vec3f(0.62, 0.65, 0.69);
        col = base * (0.10 + 0.75 * diff) * ao + vec3f(spec);
        col += vec3f(0.31, 0.82, 0.77) * fres * 0.35 * max(-n.x, 0.0);
        col += vec3f(0.94, 0.71, 0.35) * fres * 0.30 * max(n.x, 0.0);
        col += vec3f(0.08, 0.09, 0.11) * (0.5 + 0.5 * n.z) * ao;
        if (U.view.w >= 0.0 && i32(U.view.w) == h.brick) { col = mix(col, vec3f(0.31, 0.82, 0.77), 0.35); }
    } else if (tfloor < FAR) {
        let p = ro + rd * tfloor;
        let g = abs(fract(p.xy / 10.0 + 0.5) - 0.5) * 10.0;
        let line = 1.0 - smoothstep(0.0, 0.06 * tfloor / 100.0 + 0.04, min(g.x, g.y));
        let fade = exp(-length(p.xy) / 140.0);
        col = mix(col, vec3f(0.16, 0.17, 0.2), line * fade * 0.8);
        // contact shadow from the field
        let occ = clamp(field(p + vec3f(0.0, 0.0, 1.5)) / 6.0, 0.0, 1.0);
        col *= mix(0.45, 1.0, occ * occ);
    }
    // tone map + gamma
    col = col / (col + vec3f(0.85)) * 1.6;
    return vec4f(pow(col, vec3f(1.0 / 2.2)), 1.0);
}
