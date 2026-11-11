/**
 * Liquid glass over the live 3D view.
 *
 * The refraction model (shape-aware edge, rim and base displacement, corner boost, rim ripple,
 * Gaussian frost and a gradient tint sampled from what is behind the panel) is adapted from
 * liquid-glass-js by Armagan Amcalar, MIT License: https://github.com/dashersw/liquid-glass-js
 * (see THIRD_PARTY_NOTICES.md). Differences: it runs as one WebGPU pass over the rendered
 * viewport (so panels refract the live scene instead of a page screenshot), the blur and the
 * sampled gradient use far fewer taps so it stays cheap on integrated GPUs, and the tint mixes
 * towards the theme's glass colour so text stays readable on a dark scene.
 */
export const MAX_GLASS = 16;

export const GLASS_WGSL = /* wgsl */ `
struct Glass {
  info: vec4f,                      // count, canvas width, canvas height, px per CSS px
  tint: vec4f,                      // theme glass colour (rgb), tint opacity
  fx: vec4f,                        // edgeIntensity, rimIntensity, baseIntensity, cornerBoost
  fx2: vec4f,                       // edgeDistance, rimDistance, baseDistance, rippleEffect
  rects: array<vec4f, ${MAX_GLASS}>,   // x, y, w, h (canvas px)
  shape: array<vec4f, ${MAX_GLASS}>,   // radius (canvas px), blur radius, highlight, unused
};
@group(0) @binding(0) var scene: texture_2d<f32>;
@group(0) @binding(1) var smp: sampler;
@group(0) @binding(2) var<uniform> G: Glass;

@vertex
fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
  let p = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
  return vec4f(p[i], 0.0, 1.0);
}

// signed distance to a rounded rectangle (negative inside), in pixels
fn rounded_rect(p: vec2f, size: vec2f, r: f32) -> f32 {
  let q = abs(p - size * 0.5) - (size * 0.5 - vec2f(r));
  return length(max(q, vec2f(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

@fragment
fn fs(@builtin(position) frag: vec4f) -> @location(0) vec4f {
  let screen = G.info.yz;
  let uv0 = frag.xy / screen;
  let base = textureSampleLevel(scene, smp, uv0, 0.0);
  let count = u32(G.info.x);
  for (var k = 0u; k < count; k++) {
    let rect = G.rects[k];
    let local = frag.xy - rect.xy;
    let radius = min(G.shape[k].x, min(rect.z, rect.w) * 0.5);
    let sd = rounded_rect(local, rect.zw, radius);
    if (sd > 1.0) { continue; }

    // ---- refraction (liquid-glass-js model): displacement along the shape normal,
    // strongest at the edge and rim, with a corner boost and a ripple along the rim
    let coord = local / rect.zw;
    let d = max(-sd, 0.0);                                   // pixels in from the edge
    var n = coord - vec2f(0.5);
    n = select(normalize(n), vec2f(0.0, 1.0), length(n) < 1e-5);
    let base_i = 1.0 - exp(-d * G.fx2.z);
    let edge_i = exp(-d * G.fx2.x);
    let rim_i = exp(-d * G.fx2.y);
    let total = base_i * G.fx.z + edge_i * G.fx.x + rim_i * G.fx.y;
    let cx = min(coord.x, 1.0 - coord.x);
    let cy = min(coord.y, 1.0 - coord.y);
    let corner = exp(-max(cx, cy) * min(rect.z, rect.w) * 0.3) * G.fx.w;
    let ripple = sin(d / min(rect.z, rect.w) * 25.0) * G.fx2.w * rim_i;
    let offset = n * (total + corner) + vec2f(-n.y, n.x) * ripple;
    let uv = uv0 - offset * (G.info.w * 120.0 / screen);   // tuned for panel-sized glass

    // ---- frost: a sparse Gaussian (13 taps in two rings, bilinear) instead of 13 x 13
    let blur = G.shape[k].y * G.info.w / screen;
    var col = textureSampleLevel(scene, smp, uv, 0.0).rgb * 0.2;
    for (var j = 0; j < 6; j++) {
      let a = f32(j) * 1.0471976;
      let dir = vec2f(cos(a), sin(a));
      col += textureSampleLevel(scene, smp, uv + dir * blur, 0.0).rgb * 0.08;
      col += textureSampleLevel(scene, smp, uv + vec2f(dir.y, -dir.x) * blur * 2.2, 0.0).rgb * 0.053333;
    }

    // ---- tint: gradient sampled from behind the panel (top / middle / bottom), then the theme glass
    let rtop = (rect.y + rect.w * 0.1) / screen.y;
    let rmid = (rect.y + rect.w * 0.5) / screen.y;
    let rbot = (rect.y + rect.w * 0.9) / screen.y;
    var top = vec3f(0.0);
    var mid = vec3f(0.0);
    var bot = vec3f(0.0);
    for (var s = 0; s < 8; s++) {
      let x = (rect.x + rect.z * (f32(s) + 0.5) / 8.0) / screen.x;
      top += textureSampleLevel(scene, smp, vec2f(x, rtop), 3.0).rgb;
      mid += textureSampleLevel(scene, smp, vec2f(x, rmid), 3.0).rgb;
      bot += textureSampleLevel(scene, smp, vec2f(x, rbot), 3.0).rgb;
    }
    let t = coord.y;
    let grad = select(mix(mid, bot, (t - 0.5) * 2.0), mix(top, mid, t * 2.0), t < 0.5) / 8.0;
    col = mix(col, grad, 0.3 * G.tint.w);
    col = mix(col, G.tint.rgb, G.tint.w);

    // ---- light: a bright rim on the upper-left edge, a soft inner sheen, and a darker lower edge
    let rim = exp(-d / max(G.info.w * 1.6, 1.0));
    let upper = clamp(-n.y * 0.7 - n.x * 0.3 + 0.35, 0.0, 1.0);
    col += vec3f(1.0) * rim * (0.10 + 0.22 * upper) * G.shape[k].z;
    col += vec3f(1.0) * 0.05 * (1.0 - coord.y) * exp(-d / (rect.w * 0.35)) * G.shape[k].z;
    col *= 1.0 - 0.18 * rim * (1.0 - upper);

    let mask = 1.0 - smoothstep(-1.0, 1.0, sd);
    return vec4f(mix(base.rgb, col, mask), 1.0);
  }
  return base;
}
`;
