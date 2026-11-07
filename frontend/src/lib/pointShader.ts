// Translucent instanced discs for volumetric data (robot reach clouds). Shares the camera
// conventions of the ray marcher (lens shift, ortho / perspective) so the cloud registers
// exactly with the rendered geometry.
export const POINTS_WGSL = /* wgsl */ `
struct Uniforms {
  eye: vec4f, forward: vec4f, right: vec4f, up: vec4f, lo: vec4f, hi: vec4f,
  view: vec4f, section: vec4f, misc: vec4f, shift: vec4f,
};
struct Cloud { voxel: f32, threshold: f32, alpha: f32, mode: f32 };  // mode 0 best, 1 worst

@group(0) @binding(0) var<uniform> U: Uniforms;
@group(0) @binding(1) var<storage, read> pts: array<f32>;   // x, y, z, best, worst
@group(0) @binding(2) var<uniform> C: Cloud;

struct VOut { @builtin(position) pos: vec4f, @location(0) uv: vec2f, @location(1) color: vec4f };

fn ramp(d: f32) -> vec3f {
  // lock zone red -> amber -> teal -> bright green as dexterity rises
  if (d < C.threshold) { return vec3f(1.0, 0.2, 0.2); }
  let t = clamp((d - C.threshold) / (0.6 - C.threshold), 0.0, 1.0);
  if (t < 0.4) { return mix(vec3f(0.94, 0.62, 0.20), vec3f(0.0, 0.80, 0.72), t / 0.4); }
  return mix(vec3f(0.0, 0.80, 0.72), vec3f(0.0, 1.0, 0.4), (t - 0.4) / 0.6);
}

@vertex
fn vs(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> VOut {
  let base = ii * 5u;
  let p = vec3f(pts[base], pts[base + 1u], pts[base + 2u]);
  let dex = select(pts[base + 3u], pts[base + 4u], C.mode > 0.5);
  let corners = array<vec2f, 6>(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));
  let c = corners[vi];
  let d = p - U.eye.xyz;
  let z = dot(d, U.forward.xyz);
  var o: VOut;
  if (z <= 0.0) { o.pos = vec4f(2.0, 2.0, 2.0, 1.0); return o; }
  let ortho = U.forward.w > 0.5;
  let scale = select(z * U.eye.w, U.eye.w, ortho);  // world units per NDC unit (vertical)
  let half = C.voxel * 0.55 / scale;
  let x = dot(d, U.right.xyz) / scale / U.right.w + U.shift.x;
  let y = -(dot(d, U.up.xyz) / scale) + U.shift.y;  // screen-down convention of the marcher
  let depth = z / (z + U.hi.w * U.lo.w);  // matches view_depth() in the ray marcher
  o.pos = vec4f(x + c.x * half / U.right.w, -(y) + c.y * half, depth, 1.0);
  o.uv = c;
  let lock = dex < C.threshold;
  o.color = vec4f(ramp(dex), select(C.alpha, min(C.alpha * 2.5, 0.9), lock));
  return o;
}

@fragment
fn fs(i: VOut) -> @location(0) vec4f {
  let r = dot(i.uv, i.uv);
  if (r > 1.0) { discard; }
  let a = i.color.a * (1.0 - r) ;
  return vec4f(i.color.rgb * a, a);  // premultiplied
}
`;
