import type { DatumDocument } from "./ipc";

/** Starter parts that show off the feature set; each is an ordinary editable feature tree. */
export const TEMPLATES: { key: string; label: string; doc: () => DatumDocument }[] = [
  {
    key: "bolt",
    label: "M8 hex bolt",
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
    doc: () => ({
      version: 1, units: "mm", name: "Knurled knob", material: "al6061t6", root: "bored",
      features: [
        { id: "blank", type: "cylinder", name: "Knob Ø30", params: { radius: 15, height: 20, center: [0, 0, 10] } },
        { id: "knurl", type: "texture", name: "Diagonal knurl", inputs: ["blank"], params: { pattern: "knurl", cell: 2.5, depth: 0.5 } },
        { id: "bored", type: "hole", name: "M6 counterbore", inputs: ["knurl"], params: { size: "M6", kind: "counterbore", depth: 20, center: [0, 0, 20] } },
      ],
    }),
  },
];
