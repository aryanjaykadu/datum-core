"use client";

import type { ViewName } from "@/lib/gpu";

type V3 = [number, number, number];

// z-up faces: normal, label, the view that looks at it
const FACES: { n: V3; label: string; view: ViewName }[] = [
  { n: [0, -1, 0], label: "FRONT", view: "front" },
  { n: [0, 1, 0], label: "BACK", view: "back" },
  { n: [1, 0, 0], label: "RIGHT", view: "right" },
  { n: [-1, 0, 0], label: "LEFT", view: "left" },
  { n: [0, 0, 1], label: "TOP", view: "top" },
  { n: [0, 0, -1], label: "BOTTOM", view: "bottom" },
];

/** Orientation cube drawn with the camera's own basis; click a face to go to that view. */
export default function ViewCube({ yaw, pitch, onView }: { yaw: number; pitch: number; onView: (v: ViewName) => void }) {
  const cp = Math.cos(pitch), sp = Math.sin(pitch);
  const dir: V3 = [Math.cos(yaw) * cp, Math.sin(yaw) * cp, sp]; // toward the viewer
  const fwd: V3 = [-dir[0], -dir[1], -dir[2]];
  const ref: V3 = Math.abs(sp) > 0.999 ? [Math.sin(yaw), -Math.cos(yaw), 0] : [0, 0, 1];
  const right = normalize(cross(fwd, ref));
  const up = cross(right, fwd);
  const S = 26;
  const proj = (p: V3) => [dot(p, right) * S, -dot(p, up) * S];

  const faces = FACES.map((f) => {
    // two in-plane axes for the face square
    const a: V3 = Math.abs(f.n[2]) > 0.5 ? [0, 1, 0] : [0, 0, 1];
    const b = cross(a, f.n);
    const corners = [[-1, -1], [1, -1], [1, 1], [-1, 1]].map(([u, v]) =>
      proj([f.n[0] + a[0] * u + b[0] * v, f.n[1] + a[1] * u + b[1] * v, f.n[2] + a[2] * u + b[2] * v] as V3));
    const facing = dot(f.n, dir);
    const c = proj(f.n);
    // text basis follows the face's in-plane axes so labels lie on the face
    const ax = proj([f.n[0] + b[0], f.n[1] + b[1], f.n[2] + b[2]] as V3);
    const ay = proj([f.n[0] - a[0], f.n[1] - a[1], f.n[2] - a[2]] as V3);
    return { ...f, corners, facing, c, ax: [ax[0] - c[0], ax[1] - c[1]], ay: [ay[0] - c[0], ay[1] - c[1]] };
  }).filter((f) => f.facing > 0.02).sort((p, q) => p.facing - q.facing);

  return (
    <svg className="viewcube" viewBox="-48 -48 96 96" role="group" aria-label="View cube">
      {faces.map((f) => (
        <g key={f.label} className="vc-face" onClick={() => onView(f.view)} role="button" aria-label={`${f.label.toLowerCase()} view`}>
          <polygon points={f.corners.map((p) => p.join(",")).join(" ")} style={{ opacity: 0.55 + 0.45 * f.facing }} />
          <text transform={`matrix(${f.ax[0] / S} ${f.ax[1] / S} ${f.ay[0] / S} ${f.ay[1] / S} ${f.c[0]} ${f.c[1]})`}
            textAnchor="middle" dominantBaseline="central" fontSize={9}>{f.label}</text>
        </g>
      ))}
    </svg>
  );
}

function cross(a: V3, b: V3): V3 { return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]; }
function dot(a: V3, b: V3) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
function normalize(a: V3): V3 { const l = Math.hypot(...a) || 1; return [a[0] / l, a[1] / l, a[2] / l]; }
