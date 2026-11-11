/**
 * Line icons drawn for Datum Core: 24-unit grid, 1.7 stroke, round caps. One consistent set
 * for tools, features and actions, so every control has a recognisable shape, not just a word.
 */
const P: Record<string, string> = {
  // ---- features
  box: "M4 8l8-4 8 4v8l-8 4-8-4z M4 8l8 4 8-4 M12 12v8",
  cylinder: "M5 6c0-1.7 3.1-3 7-3s7 1.3 7 3-3.1 3-7 3-7-1.3-7-3z M5 6v12c0 1.7 3.1 3 7 3s7-1.3 7-3V6",
  sphere: "M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18z M3 12c0 2 4 3.5 9 3.5s9-1.5 9-3.5",
  torus: "M12 6c5 0 9 2.7 9 6s-4 6-9 6-9-2.7-9-6 4-6 9-6z M8.5 12c0-1 1.6-1.8 3.5-1.8s3.5.8 3.5 1.8-1.6 1.8-3.5 1.8-3.5-.8-3.5-1.8z",
  half_space: "M3 16l9-5 9 5 M3 16l9 5 9-5 M12 11V3 M9 6l3-3 3 3",
  extrude: "M4 15l8 4 8-4-8-4z M12 11V3 M9 6l3-3 3 3",
  revolve: "M12 3v18 M8 6.5C5 7.3 3 9.4 3 12c0 3.3 4 6 9 6 M16 18c3-.8 5-3 5-6 0-1.8-1.1-3.4-2.9-4.5 M16 7.5l2.1 0-.2 2.2",
  loft: "M5 18h14l-2-3H7z M8 6h8l-1.5 2.5h-5z M7 15l1-6.5 M17 15l-1-6.5",
  sketch: "M4 20l2-6L16 4l4 4-10 10z M14 6l4 4 M4 20l6-2",
  union: "M9 4a6 6 0 1 0 0 12a6 6 0 1 0 0-12z M15 8a6 6 0 1 0 0 12a6 6 0 1 0 0-12z",
  subtract: "M9 4a6 6 0 1 0 0 12a6 6 0 1 0 0-12z M15 8a6 6 0 0 1 0 12a6 6 0 0 1-5.5-3.6",
  intersect: "M12 7.2a6 6 0 0 1 0 9.6a6 6 0 0 1 0-9.6z M9 4a6 6 0 1 0 0 12 M15 8a6 6 0 1 1 0 12",
  shell: "M4 8l8-4 8 4v8l-8 4-8-4z M7.5 9.5l4.5-2.3 4.5 2.3v5l-4.5 2.3-4.5-2.3z",
  offset: "M7 7h10v10H7z M3 3h18v18H3z",
  transform: "M12 3v18 M3 12h18 M12 3l-2.5 2.5 M12 3l2.5 2.5 M21 12l-2.5-2.5 M21 12l-2.5 2.5",
  mirror: "M12 3v18 M9 6L4 12l5 6z M15 6l5 6-5 6z",
  pattern_linear: "M3 9h4v6H3z M10 9h4v6h-4z M17 9h4v6h-4z",
  pattern_circular: "M12 2.5a2 2 0 1 0 0 4a2 2 0 1 0 0-4z M19 10a2 2 0 1 0 0 4a2 2 0 1 0 0-4z M12 17.5a2 2 0 1 0 0 4a2 2 0 1 0 0-4z M5 10a2 2 0 1 0 0 4a2 2 0 1 0 0-4z",
  gyroid_infill: "M3 7c3 0 3 4 6 4s3-4 6-4 3 4 6 4 M3 13c3 0 3 4 6 4s3-4 6-4 3 4 6 4",
  hole: "M4 8l8-4 8 4v8l-8 4-8-4z M9.5 10.5c0-1 1.1-1.6 2.5-1.6s2.5.7 2.5 1.6-1.1 1.6-2.5 1.6-2.5-.7-2.5-1.6z M9.5 10.5v5 M14.5 10.5v5",
  texture: "M4 4l16 16 M4 10l10 10 M10 4l10 10 M4 16l4 4 M16 4l4 4 M20 4L4 20 M14 4L4 14 M20 10L10 20",
  gear: "M12 8.5a3.5 3.5 0 1 0 0 7a3.5 3.5 0 1 0 0-7z M12 2v3 M12 19v3 M2 12h3 M19 12h3 M4.9 4.9l2.1 2.1 M17 17l2.1 2.1 M4.9 19.1L7 17 M17 7l2.1-2.1",
  thread: "M8 3h8 M8 21h8 M8 3v18 M16 3v18 M8 6l8 2.5 M8 10l8 2.5 M8 14l8 2.5 M8 18l8 2.5",
  spring: "M6 3h12 M6 21h12 M7 5l10 2-10 2 10 2-10 2 10 2-10 2 10 2",
  bearing: "M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18z M12 8a4 4 0 1 0 0 8a4 4 0 1 0 0-8z M12 4.8a1.3 1.3 0 1 0 0 2.6 M17.6 11a1.3 1.3 0 1 0 0 2.6 M12 16.6a1.3 1.3 0 1 0 0 2.6 M6.4 11a1.3 1.3 0 1 0 0 2.6",
  beam: "M5 4h14 M5 20h14 M5 4v3h5.5v10H5v3 M19 4v3h-5.5v10H19v3",
  wing: "M3 14c4-5 10-7 18-6-5 2-11 5-18 6z M3 14l2 3",
  nose_cone: "M12 2c4 4 6 9 6 15H6c0-6 2-11 6-15z M6 17v4h12v-4",
  brake_disc: "M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18z M12 9a3 3 0 1 0 0 6a3 3 0 1 0 0-6z M12 4.8v.4 M18.6 8.6l-.3.2 M18.6 15.4l-.3-.2 M12 19.2v-.4 M5.4 15.4l.3-.2 M5.4 8.6l.3.2",
  wheel: "M12 2.5a9.5 9.5 0 1 0 0 19a9.5 9.5 0 1 0 0-19z M12 10a2 2 0 1 0 0 4a2 2 0 1 0 0-4z M12 10V4 M13.9 12.6l5.5 2.4 M10.1 12.6L4.6 15 M13.2 13.6l2.8 5.4 M10.8 13.6L8 19",
  robot_arm: "M4 21h8 M8 21v-3 M6 18h4 M8 15l4-7 M12 8l6 3 M18 11l2 3 M18 11l3-1 M8 15a2 2 0 1 0 0-.1 M12 8a1.8 1.8 0 1 0 0-.1",
  import_mesh: "M12 3v12 M7 10l5 5 5-5 M4 17v3h16v-3",
  // ---- categories
  Sketch: "M4 20l2-6L16 4l4 4-10 10z M14 6l4 4",
  Primitive: "M4 8l8-4 8 4v8l-8 4-8-4z M4 8l8 4 8-4 M12 12v8",
  Modify: "M14.7 6.3a4 4 0 0 0 5 5L17 14l-7 7-4-4 7-7z M3 21l3-3",
  Pattern: "M4 4h6v6H4z M14 4h6v6h-6z M4 14h6v6H4z M14 14h6v6h-6z",
  Parts: "M12 8.5a3.5 3.5 0 1 0 0 7a3.5 3.5 0 1 0 0-7z M12 2v3 M12 19v3 M2 12h3 M19 12h3 M4.9 4.9l2.1 2.1 M17 17l2.1 2.1 M4.9 19.1L7 17 M17 7l2.1-2.1",
  Structural: "M5 4h14 M5 20h14 M5 4v3h5.5v10H5v3 M19 4v3h-5.5v10H19v3",
  Automotive: "M3 15l2-5h11l3 5 M3 15v3h18v-3z M7.5 18a1.8 1.8 0 1 0 0 .1 M16.5 18a1.8 1.8 0 1 0 0 .1",
  Aerospace: "M12 2c2 2 3 5 3 9l4 4v2l-4-1-1 3h-4l-1-3-4 1v-2l4-4c0-4 1-7 3-9z",
  Lattice: "M3 7c3 0 3 4 6 4s3-4 6-4 3 4 6 4 M3 13c3 0 3 4 6 4s3-4 6-4 3 4 6 4",
  Boolean: "M9 4a6 6 0 1 0 0 12a6 6 0 1 0 0-12z M15 8a6 6 0 1 0 0 12a6 6 0 1 0 0-12z",
  Robotics: "M4 21h8 M8 21v-3 M6 18h4 M8 15l4-7 M12 8l6 3 M18 11l2 3",
  // ---- actions
  search: "M10.5 4a6.5 6.5 0 1 0 0 13a6.5 6.5 0 1 0 0-13z M15.5 15.5L21 21",
  undo: "M9 7L4 12l5 5 M4 12h11a5 5 0 0 1 0 10h-3",
  redo: "M15 7l5 5-5 5 M20 12H9a5 5 0 0 0 0 10h3",
  palette: "M12 3a9 9 0 1 0 0 18c1.5 0 2-1 2-2s-1-1.5-1-2.5S14 15 15 15h2a4 4 0 0 0 4-4c0-4.4-4-8-9-8z M7.5 11a1 1 0 1 0 0 .1 M10 7a1 1 0 1 0 0 .1 M15 7a1 1 0 1 0 0 .1",
  settings: "M12 9a3 3 0 1 0 0 6a3 3 0 1 0 0-6z M19.4 13a7.5 7.5 0 0 0 0-2l2-1.6-2-3.4-2.4 1a7.6 7.6 0 0 0-1.7-1L15 3.5h-4l-.4 2.5a7.6 7.6 0 0 0-1.7 1l-2.4-1-2 3.4L6.6 11a7.5 7.5 0 0 0 0 2l-2 1.6 2 3.4 2.4-1a7.6 7.6 0 0 0 1.7 1l.4 2.5h4l.4-2.5a7.6 7.6 0 0 0 1.7-1l2.4 1 2-3.4z",
  measure: "M3 17l14-14 4 4L7 21z M7 13l2 2 M10 10l2 2 M13 7l2 2",
  section: "M4 8l8-4 8 4v8l-8 4-8-4z M2 12h20",
  fit: "M4 9V4h5 M20 9V4h-5 M4 15v5h5 M20 15v5h-5 M9 9h6v6H9z",
  views: "M4 8l8-4 8 4v8l-8 4-8-4z M12 12v8 M4 8l8 4 8-4",
  display: "M12 3a9 9 0 1 0 0 18V3z",
  ortho: "M4 4h16v16H4z M4 12h16 M12 4v16",
  grid: "M3 9h18 M3 15h18 M9 3v18 M15 3v18",
  analysis: "M4 20V10 M10 20V4 M16 20v-7 M22 20H2",
  quote: "M6 3h12v18l-3-2-3 2-3-2-3 2z M9 8h6 M9 12h6",
  properties: "M4 6h10 M18 6h2 M4 12h4 M12 12h8 M4 18h12 M20 18h0 M14 4v4 M8 10v4 M16 16v4",
  history: "M3 12a9 9 0 1 0 3-6.7 M3 4v5h5 M12 7v5l3 2",
  log: "M5 4h14v16H5z M8 8h8 M8 12h8 M8 16h5",
  graph: "M6 6a2 2 0 1 0 0 .1 M18 6a2 2 0 1 0 0 .1 M12 18a2 2 0 1 0 0 .1 M7.5 7.5l3.5 9 M16.5 7.5l-3.5 9",
  template: "M4 4h7v7H4z M13 4h7v4h-7z M13 10h7v10h-7z M4 13h7v7H4z",
  calculator: "M5 3h14v18H5z M8 6h8v3H8z M8 13h.01 M12 13h.01 M16 13h.01 M8 17h.01 M12 17h.01 M16 17h.01",
  folder: "M3 6h6l2 2h10v11H3z",
  download: "M12 3v12 M7 10l5 5 5-5 M4 20h16",
  plus: "M12 5v14 M5 12h14",
  close: "M6 6l12 12 M18 6L6 18",
  chevron: "M9 6l6 6-6 6",
  sparkle: "M12 3l1.8 5.2L19 10l-5.2 1.8L12 17l-1.8-5.2L5 10l5.2-1.8z M19 16l.8 2.2 2.2.8-2.2.8-.8 2.2-.8-2.2-2.2-.8 2.2-.8z",
  eye: "M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12z M12 9a3 3 0 1 0 0 6a3 3 0 1 0 0-6z",
  add: "M12 5v14 M5 12h14",
  cut: "M6 6a2.5 2.5 0 1 0 0 .1 M6 18a2.5 2.5 0 1 0 0 .1 M8 7.5L20 18 M8 16.5L20 6",
  new: "M4 8l8-4 8 4v8l-8 4-8-4z M17 2v6 M14 5h6",
  play: "M7 4l13 8-13 8z",
  strength: "M4 20h16 M7 20V9l5-5 5 5v11 M10 20v-5h4v5 M12 4v3",
};
// tools that share a drawing
const ALIAS: Record<string, string> = { add: "union", intersectOp: "intersect" };

export default function Icon({ name, size = 18, className, title }: { name: string; size?: number; className?: string; title?: string }) {
  const d = P[name] ?? P[ALIAS[name] ?? ""] ?? P.sparkle;
  return (
    <svg className={`ic ${className ?? ""}`} width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth={1.7}
      strokeLinecap="round" strokeLinejoin="round" aria-hidden={title ? undefined : true} role={title ? "img" : undefined}>
      {title && <title>{title}</title>}
      <path d={d} />
    </svg>
  );
}

export const hasIcon = (name: string) => name in P;
