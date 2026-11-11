import { RAYMARCH_WGSL } from "./shader";

// Same uniform block as the ray marcher, so both passes share one buffer and one camera.
const UNIFORMS = /struct Uniforms \{[\s\S]*?\};/.exec(RAYMARCH_WGSL)![0];

/**
 * Rasterised triangle mesh of the part (the engine's watertight dual-contoured mesh).
 * Costs a few operations per pixel instead of a full field evaluation per ray step, so it
 * stays fluid on integrated GPUs. Shading mirrors the ray marcher's material and theme lights
 * so switching between the two is hard to notice.
 */
export const MESH_WGSL = /* wgsl */ `
${UNIFORMS}
@group(0) @binding(0) var<uniform> U: Uniforms;

struct VIn { @location(0) pos: vec3f, @location(1) nrm: vec3f, @location(2) stress: f32 };
struct VOut { @builtin(position) clip: vec4f, @location(0) world: vec3f, @location(1) nrm: vec3f, @location(2) stress: f32 };

@vertex
fn vs(v: VIn) -> VOut {
  let d = v.pos - U.eye.xyz;
  let z = max(dot(d, U.forward.xyz), 1e-4);
  let ortho = U.forward.w > 0.5;
  let scale = select(z * U.eye.w, U.eye.w, ortho);  // world units per NDC unit (vertical)
  let x = dot(d, U.right.xyz) / scale / U.right.w + U.shift.x;
  let y = dot(d, U.up.xyz) / scale - U.shift.y;
  let depth = z / (z + U.hi.w * U.lo.w);              // matches view_depth() in the ray marcher
  // clip w = z in perspective: the rasteriser then interpolates depth and normals correctly
  let w = select(z, 1.0, ortho);
  var o: VOut;
  o.clip = vec4f(x * w, y * w, depth * w, w);
  o.world = v.pos;
  o.nrm = v.nrm;
  o.stress = v.stress;
  return o;
}

fn env(r: vec3f) -> vec3f {
  var c = mix(vec3f(0.035, 0.038, 0.048), vec3f(0.20, 0.22, 0.26), clamp(r.z * 0.5 + 0.5, 0.0, 1.0));
  c += vec3f(1.6) * pow(max(dot(r, normalize(vec3f(0.3, -0.5, 0.8))), 0.0), 60.0);
  c += vec3f(0.8) * pow(max(dot(r, normalize(vec3f(-0.7, 0.4, 0.6))), 0.0), 30.0);
  return c;
}

@fragment
fn fs(i: VOut, @builtin(front_facing) front: bool) -> @location(0) vec4f {
  var n = normalize(i.nrm);
  if (!front) { n = -n; }
  let ortho = U.forward.w > 0.5;
  let v = select(normalize(U.eye.xyz - i.world), -U.forward.xyz, ortho);
  let key = normalize(vec3f(0.45, -0.55, 0.85));
  var base = vec3f(0.90, 0.91, 0.92);
  var metal = 0.9;
  var rough = 0.34;
  let look = u32(U.up.w);
  if (look == 1u) { base = vec3f(0.86, 0.87, 0.89); rough = 0.26; }
  else if (look == 2u) { base = vec3f(0.62, 0.60, 0.57); rough = 0.40; }
  else if (look == 3u) { base = vec3f(0.78, 0.79, 0.80); metal = 1.0; rough = 0.20; }
  else if (look == 4u) { base = vec3f(0.42, 0.43, 0.45); metal = 0.85; rough = 0.52; }
  else if (look == 5u) { base = vec3f(0.92, 0.91, 0.87); metal = 0.0; rough = 0.72; }
  // stress view: blue (low) -> green -> yellow -> red (at the scale's maximum); matte, so the colours read
  if (i.stress >= 0.0) {
    let t = clamp(i.stress, 0.0, 1.0);
    let c0 = vec3f(0.12, 0.30, 0.95);
    let c1 = vec3f(0.10, 0.80, 0.55);
    let c2 = vec3f(0.98, 0.85, 0.20);
    let c3 = vec3f(0.95, 0.18, 0.15);
    base = select(select(mix(c2, c3, (t - 0.66) / 0.34), mix(c1, c2, (t - 0.33) / 0.33), t < 0.66), mix(c0, c1, t / 0.33), t < 0.33);
    metal = 0.0;
    rough = 0.65;
  }
  let f0 = mix(vec3f(0.04), base, metal);
  let nl = max(dot(n, key), 0.0);
  let h = normalize(v + key);
  let a2 = rough * rough * rough * rough;
  let nh = max(dot(n, h), 0.0);
  let dterm = a2 / (3.14159 * pow(nh * nh * (a2 - 1.0) + 1.0, 2.0));
  let fres = f0 + (1.0 - f0) * pow(1.0 - max(dot(n, v), 0.0), 5.0);
  var col = (base * (1.0 - metal) / 3.14159 + dterm * fres * 0.25) * nl * vec3f(3.2, 3.05, 2.9);
  let r = reflect(-v, n);
  col += env(normalize(mix(r, n, rough * rough))) * fres;
  col += base * (1.0 - metal) * env(n) * 0.35;
  let rim = pow(1.0 - abs(dot(n, v)), 3.0);
  col += U.accent.xyz * 0.22 * rim * max(-n.x, 0.0);
  col += U.accent2.xyz * 0.18 * rim * max(n.x, 0.0);
  col = clamp((col * (2.51 * col + 0.03)) / (col * (2.43 * col + 0.59) + 0.14), vec3f(0.0), vec3f(1.0));
  return vec4f(pow(col, vec3f(1.0 / 2.2)), 1.0);
}
`;
