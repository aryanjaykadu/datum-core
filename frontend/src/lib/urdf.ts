/**
 * URDF -> Datum chain robot.
 *
 * Takes the serial chain with the most revolute joints from the root link, ending at the
 * conventional end-effector link (tool0, ee_link, flange, tcp) when there is one: gripper
 * joints beyond it are not arm joints. Fixed joints are
 * folded into the next movable joint's origin list; whatever follows the last movable joint
 * becomes the tool offset. Lengths are converted from metres to millimetres and angles to
 * degrees. Link meshes are not imported: the arm is modelled as capsules along the chain.
 */
export interface RobotDef {
  name: string;
  format: "dh" | "chain";
  link_radius?: number;
  mount_offset?: number;
  joints: Record<string, unknown>[];
  tool?: { origins: Origin[] };
}
interface Origin { xyz: number[]; rpy_deg: number[] }
interface UJoint { name: string; type: string; parent: string; child: string; origin: Origin; axis: number[]; lo: number; hi: number }

const nums = (s: string | null | undefined, n: number, fallback: number[]) => {
  if (!s) return fallback;
  const v = s.trim().split(/\s+/).map(Number);
  return v.length === n && v.every(Number.isFinite) ? v : fallback;
};
const DEG = 180 / Math.PI;
// plain DOM traversal (no CSS selectors), so the parser runs in any standards DOM
const kids = (el: Element, tag: string) =>
  Array.from(el.childNodes).filter((n): n is Element => n.nodeType === 1 && (n as Element).tagName === tag);
const kid = (el: Element, tag: string) => kids(el, tag)[0];

export function parseUrdf(xml: string, Parser: { new (): DOMParser } = DOMParser): { robot: RobotDef; notes: string[] } {
  const dom = new Parser().parseFromString(xml, "application/xml");
  if (dom.getElementsByTagName("parsererror").length) throw new Error("not valid XML");
  const root = dom.documentElement;
  if (!root || root.tagName !== "robot") throw new Error("no <robot> element: not a URDF file");
  const notes: string[] = [];

  const joints: UJoint[] = kids(root, "joint").map((j) => {
    const o = kid(j, "origin");
    const limit = kid(j, "limit");
    const type = j.getAttribute("type") ?? "fixed";
    return {
      name: j.getAttribute("name") ?? "joint",
      type,
      parent: kid(j, "parent")?.getAttribute("link") ?? "",
      child: kid(j, "child")?.getAttribute("link") ?? "",
      origin: { xyz: nums(o?.getAttribute("xyz"), 3, [0, 0, 0]).map((m) => m * 1000), rpy_deg: nums(o?.getAttribute("rpy"), 3, [0, 0, 0]).map((r) => r * DEG) },
      axis: nums(kid(j, "axis")?.getAttribute("xyz"), 3, [1, 0, 0]),  // URDF default axis is x
      lo: type === "continuous" ? -180 : Number(limit?.getAttribute("lower") ?? -Math.PI) * DEG,
      hi: type === "continuous" ? 180 : Number(limit?.getAttribute("upper") ?? Math.PI) * DEG,
    };
  });
  if (!joints.length) throw new Error("the URDF has no joints");

  const children = new Map<string, UJoint[]>();
  for (const j of joints) children.set(j.parent, [...(children.get(j.parent) ?? []), j]);
  const childLinks = new Set(joints.map((j) => j.child));
  const base = joints.map((j) => j.parent).find((l) => !childLinks.has(l));
  if (!base) throw new Error("could not find the root link (cyclic joint graph?)");

  // deepest chain by count of movable joints
  const movable = (j: UJoint) => j.type === "revolute" || j.type === "continuous";
  const best = (link: string, seen: Set<string>): UJoint[] => {
    let pick: UJoint[] = [];
    for (const j of children.get(link) ?? []) {
      if (seen.has(j.child)) continue;
      const rest = [j, ...best(j.child, new Set([...seen, j.child]))];
      if (rest.filter(movable).length > pick.filter(movable).length) pick = rest;
    }
    return pick;
  };
  let chain = best(base, new Set([base]));
  const flange = chain.findIndex((j) => /^(tool0|ee_link|flange|tcp|tool_flange|end_effector)$/i.test(j.child));
  if (flange >= 0 && flange < chain.length - 1) {
    const dropped = chain.slice(flange + 1).filter(movable).length;
    if (dropped) notes.push(`${dropped} end-effector joint(s) after "${chain[flange].child}" were ignored (gripper, not arm)`);
    chain = chain.slice(0, flange + 1);
  }
  const prismatic = chain.filter((j) => j.type === "prismatic").length;
  if (prismatic) throw new Error(`${prismatic} prismatic joint(s) in the chain: only revolute joints are supported so far`);
  if (!chain.some(movable)) throw new Error("no revolute joints found in the main chain");
  const branches = joints.filter(movable).length - chain.filter(movable).length;
  if (branches > 0) notes.push(`${branches} revolute joint(s) on side branches (e.g. a gripper) were ignored`);

  const out: Record<string, unknown>[] = [];
  let pending: Origin[] = [];
  for (const j of chain) {
    pending.push(j.origin);
    if (!movable(j)) continue;
    out.push({ origins: pending, axis: j.axis, lo_deg: j.lo, hi_deg: j.hi, name: j.name });
    pending = [];
  }
  if (out.length > 12) throw new Error(`${out.length} joints: at most 12 are supported`);
  notes.push(`imported ${out.length} revolute joints from "${root.getAttribute("name") ?? "robot"}" (meshes replaced by capsules)`);
  return {
    robot: { name: root.getAttribute("name") ?? "Imported arm", format: "chain", link_radius: 35, mount_offset: 0, joints: out, tool: { origins: pending } },
    notes,
  };
}

export function newDhRobot(): RobotDef {
  return {
    name: "Custom DH arm",
    format: "dh",
    link_radius: 35,
    mount_offset: 0,
    joints: [
      { d: 150, a: 0, alpha_deg: 90, lo_deg: -180, hi_deg: 180 },
      { d: 0, a: 400, alpha_deg: 0, lo_deg: -135, hi_deg: 135 },
      { d: 0, a: 350, alpha_deg: 0, lo_deg: -150, hi_deg: 150 },
    ],
  };
}
