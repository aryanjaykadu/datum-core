// URDF import check: parse with the app's parser (xmldom standing in for the browser DOM),
// then run forward kinematics in the real kernel engine and compare with known geometry.
//
//   node tests/urdf.test.ts <path-to-dk_engine.exe>
import { spawn } from "node:child_process";
import { createInterface } from "node:readline";
import { DOMParser } from "@xmldom/xmldom";
import { parseUrdf } from "../src/lib/urdf.ts";

// UR5 joint origins as published in ros-industrial ur_description (metres, radians)
const UR5_URDF = `<?xml version="1.0"?>
<robot name="ur5">
  <link name="world"/><link name="base_link"/><link name="shoulder_link"/><link name="upper_arm_link"/>
  <link name="forearm_link"/><link name="wrist_1_link"/><link name="wrist_2_link"/><link name="wrist_3_link"/>
  <link name="ee_link"/><link name="finger_link"/>
  <joint name="world_joint" type="fixed"><parent link="world"/><child link="base_link"/><origin xyz="0 0 0" rpy="0 0 0"/></joint>
  <joint name="shoulder_pan_joint" type="revolute"><parent link="base_link"/><child link="shoulder_link"/>
    <origin xyz="0 0 0.089159" rpy="0 0 0"/><axis xyz="0 0 1"/><limit lower="-6.2832" upper="6.2832" effort="150" velocity="3.15"/></joint>
  <joint name="shoulder_lift_joint" type="revolute"><parent link="shoulder_link"/><child link="upper_arm_link"/>
    <origin xyz="0 0.13585 0" rpy="0 1.570796325 0"/><axis xyz="0 1 0"/><limit lower="-6.2832" upper="6.2832" effort="150" velocity="3.15"/></joint>
  <joint name="elbow_joint" type="revolute"><parent link="upper_arm_link"/><child link="forearm_link"/>
    <origin xyz="0 -0.1197 0.425" rpy="0 0 0"/><axis xyz="0 1 0"/><limit lower="-3.1416" upper="3.1416" effort="150" velocity="3.15"/></joint>
  <joint name="wrist_1_joint" type="revolute"><parent link="forearm_link"/><child link="wrist_1_link"/>
    <origin xyz="0 0 0.39225" rpy="0 1.570796325 0"/><axis xyz="0 1 0"/><limit lower="-6.2832" upper="6.2832" effort="28" velocity="3.2"/></joint>
  <joint name="wrist_2_joint" type="revolute"><parent link="wrist_1_link"/><child link="wrist_2_link"/>
    <origin xyz="0 0.093 0" rpy="0 0 0"/><axis xyz="0 0 1"/><limit lower="-6.2832" upper="6.2832" effort="28" velocity="3.2"/></joint>
  <joint name="wrist_3_joint" type="revolute"><parent link="wrist_2_link"/><child link="wrist_3_link"/>
    <origin xyz="0 0 0.09465" rpy="0 0 0"/><axis xyz="0 1 0"/><limit lower="-6.2832" upper="6.2832" effort="28" velocity="3.2"/></joint>
  <joint name="ee_fixed_joint" type="fixed"><parent link="wrist_3_link"/><child link="ee_link"/>
    <origin xyz="0 0.0823 0" rpy="0 0 1.570796325"/></joint>
  <joint name="finger_joint" type="revolute"><parent link="ee_link"/><child link="finger_link"/>
    <origin xyz="0.02 0 0" rpy="0 0 0"/><axis xyz="0 0 1"/><limit lower="0" upper="0.5" effort="1" velocity="1"/></joint>
</robot>`;

let failures = 0;
const check = (ok: boolean, msg: string) => {
  console.log(`${ok ? "  ok  " : "  FAIL"} ${msg}`);
  if (!ok) failures++;
};

const engine = spawn(process.argv[2], [], { stdio: ["pipe", "pipe", "ignore"] });
const lines = createInterface({ input: engine.stdout! })[Symbol.asyncIterator]();
let id = 0;
async function call(method: string, params: unknown) {
  engine.stdin!.write(JSON.stringify({ id: ++id, method, params }) + "\n");
  const head = JSON.parse((await lines.next()).value as string);
  if (head.error) throw new Error(head.error);
  return head.result;
}

const { robot, notes } = parseUrdf(UR5_URDF, DOMParser as unknown as { new (): globalThis.DOMParser });
check(robot.joints.length === 6, `6 revolute joints in the main chain (got ${robot.joints.length})`);
check(notes.some((n) => n.includes("end-effector joint")), "the gripper finger after ee_link is not taken as an arm joint");
check((robot.joints[1].origins as { rpy_deg: number[] }[])[0].rpy_deg[1] > 89.9, "rpy converted to degrees");

// URDF zero pose is the same stretched pose as the DH home (-817.25, -191.45, -5.491) mm,
// seen from ROS base_link, which is rotated 180 deg about z relative to the DH base frame.
const zero = await call("robot_fk", { robot, q_deg: [0, 0, 0, 0, 0, 0] });
const [x, y, z] = zero.tool as number[];
check(Math.hypot(x - 817.25, y - 191.45, z + 5.491) < 1e-3, `UR5 URDF zero-pose tool at (${x.toFixed(3)}, ${y.toFixed(3)}, ${z.toFixed(3)}) mm`);
const offsets = [[0, 0, 89.159], [0, 135.85, 0], [0, -119.7, 425], [0, 0, 392.25], [0, 93, 0], [0, 0, 94.65], [0, 82.3, 0]];
const bound = offsets.reduce((s, v) => s + Math.hypot(...v), 0);
check(Math.abs(zero.reach_mm - bound) < 1e-6, `reach bound ${zero.reach_mm.toFixed(3)} mm = sum of origin lengths ${bound.toFixed(3)}`);

// same robot evaluated as a document feature: geometry builds and the pose is collision-free
const doc = { version: 1, material: "al6061t6", root: "arm", robots: { custom_ur5: robot },
  features: [{ id: "arm", type: "robot_arm", name: "Arm", params: { preset: "custom_ur5", joints_a: [0, -45, 60], joints_b: [0, 0, 0] } }] };
const ev = await call("evaluate", { document: doc });
check(ev.geometry.watertight && ev.warnings.length === 0, `custom arm geometry builds (${ev.geometry.triangles} tris, warnings: ${ev.warnings.join("; ") || "none"})`);

const reach = await call("robot_reach", { robot, samples: 20000 });
check(reach.voxels > 100, `reach envelope from the URDF: ${reach.voxels} voxels, ${reach.singular_pct.toFixed(1)}% lock zones`);

let threw = false;
try { parseUrdf(`<robot name="x"><joint name="j" type="prismatic"><parent link="a"/><child link="b"/></joint></robot>`, DOMParser as never); } catch { threw = true; }
check(threw, "prismatic-only chain is rejected with a clear error");

engine.stdin!.end();
console.log(`failures: ${failures}`);
process.exit(failures ? 1 : 0);
