import type { DatumDocument } from "./ipc";

/** Starter parts that show off the feature set; each is an ordinary editable feature tree. */
export type Domain = "mechanical" | "robotics" | "automotive" | "aerospace";

export const TEMPLATES: { key: string; label: string; domain: Domain; desc: string; doc: () => DatumDocument }[] = [
  {
    key: "bolt",
    label: "M8 hex bolt",
    domain: "mechanical",
    desc: "Hex head with a real ISO M8 thread.",
    doc: () => {
      const r = 13 / Math.sqrt(3); // 13 mm across flats
      const hex = Array.from({ length: 6 }, (_, i) => [+(r * Math.cos((i * Math.PI) / 3)).toFixed(4), +(r * Math.sin((i * Math.PI) / 3)).toFixed(4)]);
      return {
        version: 1, units: "mm", name: "M8 hex bolt", material: "ss316", root: "bolt",
        features: [
          { id: "head", type: "extrude", name: "Hex head (13 AF)", params: { profile: hex, distance: 5.3, center: [0, 0, -5.3] } },
          { id: "shank", type: "thread", name: "M8 x 30 thread", params: { size: "M8", length: 30, center: [0, 0, 15] } },
          { id: "bolt", type: "union", name: "Bolt", inputs: ["head", "shank"], params: { blend: 0.4 } },
        ],
      };
    },
  },
  {
    key: "gear_shaft",
    label: "Gear on shaft",
    domain: "mechanical",
    desc: "Involute spur gear blended onto a shaft and hub.",
    doc: () => ({
      version: 1, units: "mm", name: "Gear on shaft", material: "s355", root: "assembly",
      features: [
        { id: "gear", type: "gear", name: "Spur gear m2 z24", params: { module: 2, teeth: 24, width: 12, bore: 0, pressure: 20 } },
        { id: "shaft", type: "cylinder", name: "Shaft Ø12", params: { radius: 6, height: 70, center: [0, 0, 0] } },
        { id: "hub", type: "cylinder", name: "Hub", params: { radius: 12, height: 10, center: [0, 0, 11] } },
        { id: "assembly", type: "union", name: "Gear shaft", inputs: ["gear", "shaft", "hub"], params: { blend: 1.5 } },
      ],
    }),
  },
  {
    key: "enclosure",
    label: "Electronics enclosure",
    domain: "mechanical",
    desc: "Shelled box with four screw bosses and M3 holes.",
    doc: () => ({
      version: 1, units: "mm", name: "Enclosure", material: "pa12", root: "mount4",
      features: [
        { id: "outer", type: "box", name: "Outer 90×60×35", params: { size: [90, 60, 35], radius: 4, center: [0, 0, 17.5] } },
        { id: "walls", type: "shell", name: "2 mm walls", inputs: ["outer"], params: { thickness: 2, mode: "inward" } },
        { id: "lid_cut", type: "box", name: "Open top", params: { size: [100, 70, 10], center: [0, 0, 37] } },
        { id: "open", type: "subtract", name: "Open box", inputs: ["walls", "lid_cut"] },
        { id: "boss", type: "cylinder", name: "Screw boss", params: { radius: 4, height: 30, center: [37, 22, 17] } },
        { id: "bosses", type: "mirror", name: "Bosses (mirror X)", inputs: ["boss"], params: { plane: "yz" } },
        { id: "bosses4", type: "mirror", name: "Bosses (mirror Y)", inputs: ["bosses"], params: { plane: "xz" } },
        { id: "body", type: "union", name: "Body", inputs: ["open", "bosses4"], params: { blend: 1 } },
        { id: "mount1", type: "hole", name: "M3 hole", inputs: ["body"], params: { size: "M3", kind: "simple", depth: 12, center: [37, 22, 32] } },
        { id: "mount2", type: "hole", name: "M3 hole", inputs: ["mount1"], params: { size: "M3", kind: "simple", depth: 12, center: [-37, 22, 32] } },
        { id: "mount3", type: "hole", name: "M3 hole", inputs: ["mount2"], params: { size: "M3", kind: "simple", depth: 12, center: [37, -22, 32] } },
        { id: "mount4", type: "hole", name: "M3 hole", inputs: ["mount3"], params: { size: "M3", kind: "simple", depth: 12, center: [-37, -22, 32] } },
      ],
    }),
  },
  {
    key: "knob",
    label: "Knurled knob",
    domain: "mechanical",
    desc: "Knurled grip with an M6 counterbore.",
    doc: () => ({
      version: 1, units: "mm", name: "Knurled knob", material: "al6061t6", root: "bored",
      features: [
        { id: "blank", type: "cylinder", name: "Knob Ø30", params: { radius: 15, height: 20, center: [0, 0, 10] } },
        { id: "knurl", type: "texture", name: "Diagonal knurl", inputs: ["blank"], params: { pattern: "knurl", cell: 2.5, depth: 0.5 } },
        { id: "bored", type: "hole", name: "M6 counterbore", inputs: ["knurl"], params: { size: "M6", kind: "counterbore", depth: 20, center: [0, 0, 20] } },
      ],
    }),
  },
  {
    key: "pillow_block",
    label: "Bearing block",
    domain: "mechanical",
    desc: "Housing bored for a 6204 bearing, with two M8 mounting holes.",
    doc: () => ({
      version: 1, units: "mm", name: "Bearing block", material: "al6061t6", root: "hole2",
      features: [
        { id: "base", type: "box", name: "Base 110×24×16", params: { size: [110, 24, 16], radius: 2, center: [0, 0, 8] } },
        { id: "housing", type: "cylinder", name: "Housing Ø66", params: { radius: 33, height: 24, axis: "y", center: [0, 0, 40] } },
        { id: "body", type: "union", name: "Body", inputs: ["base", "housing"], params: { blend: 6 } },
        { id: "bore", type: "cylinder", name: "Bore Ø47 (6204)", params: { radius: 23.5, height: 40, axis: "y", center: [0, 0, 40] } },
        { id: "bored", type: "subtract", name: "Bored housing", inputs: ["body", "bore"] },
        { id: "hole1", type: "hole", name: "M8 mounting hole", inputs: ["bored"], params: { size: "M8", kind: "simple", depth: 20, center: [44, 0, 16] } },
        { id: "hole2", type: "hole", name: "M8 mounting hole", inputs: ["hole1"], params: { size: "M8", kind: "simple", depth: 20, center: [-44, 0, 16] } },
      ],
    }),
  },
  {
    key: "ur5_cell",
    label: "Robot on a table",
    domain: "robotics",
    desc: "A UR5 arm posed on a mounting table. Open the Robotics tab to check its reach.",
    doc: () => ({
      version: 1, units: "mm", name: "Robot cell", material: "al6061t6", root: "cell",
      features: [
        { id: "table", type: "box", name: "Table 700×700×30", params: { size: [700, 700, 30], radius: 6, center: [0, 0, -15] } },
        { id: "arm", type: "robot_arm", name: "UR5", params: { preset: "ur5", center: [0, 0, 0] } },
        { id: "cell", type: "union", name: "Cell", inputs: ["table", "arm"] },
      ],
    }),
  },
  {
    key: "gripper_finger",
    label: "Gripper finger",
    domain: "robotics",
    desc: "A light, ribbed parallel-gripper finger with two M4 mounting holes.",
    doc: () => ({
      version: 1, units: "mm", name: "Gripper finger", material: "pa12", root: "m2",
      features: [
        { id: "outline", type: "extrude", name: "Finger outline", params: { profile: [[0, 0, 2], [30, 0, 2], [30, 14, 3], [16, 22, 6], [12, 80, 3], [0, 80, 3]], distance: 12 } },
        { id: "grip", type: "texture", name: "Grip ribs", inputs: ["outline"], params: { pattern: "ribs", cell: 3, depth: 0.6 } },
        { id: "m1", type: "hole", name: "M4 mount", inputs: ["grip"], params: { size: "M4", kind: "counterbore", depth: 14, center: [8, 8, 12] } },
        { id: "m2", type: "hole", name: "M4 mount", inputs: ["m1"], params: { size: "M4", kind: "counterbore", depth: 14, center: [22, 8, 12] } },
      ],
    }),
  },
  {
    key: "brake_disc",
    label: "Drilled brake disc",
    domain: "automotive",
    desc: "280 mm rotor, 5×112 bolt circle, two rings of cross-drilled holes.",
    doc: () => ({
      version: 1, units: "mm", name: "Brake disc", material: "s355", root: "disc",
      features: [{ id: "disc", type: "brake_disc", name: "Rotor 280×22", params: { diameter: 280, thickness: 22, bore: 65, bolts: 5, pcd: 112, drilled: 24 } }],
    }),
  },
  {
    key: "wheel",
    label: "Alloy wheel",
    domain: "automotive",
    desc: "15-inch five-spoke wheel, 5×112, 57.1 mm centre bore.",
    doc: () => ({
      version: 1, units: "mm", name: "Alloy wheel", material: "al6061t6", root: "wheel",
      features: [{ id: "wheel", type: "wheel", name: "Wheel 15×6.5", params: { diameter: 381, width: 165, spokes: 5, bolts: 5, pcd: 112, bore: 57.1 } }],
    }),
  },
  {
    key: "coilover",
    label: "Coil-over damper",
    domain: "automotive",
    desc: "Suspension spring over a damper body with spring seats.",
    doc: () => ({
      version: 1, units: "mm", name: "Coil-over", material: "s355", root: "unit",
      features: [
        { id: "spring", type: "spring", name: "Spring Ø60 × 220", params: { mean_d: 60, wire_d: 9, length: 220, turns: 8, center: [0, 0, 0] } },
        { id: "body", type: "cylinder", name: "Damper body Ø40", params: { radius: 20, height: 190, center: [0, 0, -45] } },
        { id: "rod", type: "cylinder", name: "Piston rod Ø14", params: { radius: 7, height: 150, center: [0, 0, 110] } },
        { id: "seat_low", type: "cylinder", name: "Lower seat", params: { radius: 38, height: 8, center: [0, 0, -114] } },
        { id: "seat_top", type: "cylinder", name: "Upper seat", params: { radius: 38, height: 8, center: [0, 0, 114] } },
        { id: "unit", type: "union", name: "Coil-over", inputs: ["spring", "body", "rod", "seat_low", "seat_top"] },
      ],
    }),
  },
  {
    key: "wing",
    label: "Swept wing",
    domain: "aerospace",
    desc: "NACA 2412 wing, 0.9 m span, 20° sweep, taper 0.5, 4° dihedral.",
    doc: () => ({
      version: 1, units: "mm", name: "Swept wing", material: "al7075t6", root: "wing",
      features: [
        { id: "half", type: "wing", name: "Half wing (NACA 2412)", params: { naca: "2412", root_chord: 180, taper: 0.5, span: 450, sweep: 20, dihedral: 4 } },
        { id: "wing", type: "mirror", name: "Full wing", inputs: ["half"], params: { plane: "xy" } },
      ],
    }),
  },
  {
    key: "rocket",
    label: "Model rocket",
    domain: "aerospace",
    desc: "Ogive nose, body tube and three fins.",
    doc: () => ({
      version: 1, units: "mm", name: "Model rocket", material: "pa12", root: "rocket",
      features: [
        { id: "nose", type: "nose_cone", name: "Ogive nose", params: { shape: "ogive", diameter: 50, length: 130, center: [0, 0, 220] } },
        { id: "tube", type: "cylinder", name: "Body tube Ø50", params: { radius: 25, height: 220, center: [0, 0, 110] } },
        { id: "fin", type: "extrude", name: "Fin", params: { profile: [[24, 0, 0], [70, 0, 2], [70, 25, 2], [24, 80, 0]], distance: 3, direction: "symmetric", rotation: [90, 0, 0] } },
        { id: "fins", type: "pattern_circular", name: "3 fins", inputs: ["fin"], params: { count: 3, angle: 360 } },
        { id: "rocket", type: "union", name: "Rocket", inputs: ["nose", "tube", "fins"], params: { blend: 1 } },
      ],
    }),
  },
];
