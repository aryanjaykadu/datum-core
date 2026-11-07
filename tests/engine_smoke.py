"""End-to-end check of dk_engine's stdio JSON-RPC protocol (including binary frames).

    python tests/engine_smoke.py build/dk_engine.exe
"""
import json
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

exe = sys.argv[1] if len(sys.argv) > 1 else "build/dk_engine.exe"
proc = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
next_id = 0


def call(method, params=None):
    global next_id
    next_id += 1
    proc.stdin.write((json.dumps({"id": next_id, "method": method, "params": params or {}}) + "\n").encode())
    proc.stdin.flush()
    head = json.loads(proc.stdout.readline())
    assert head["id"] == next_id, head
    if "error" in head:
        raise RuntimeError(head["error"])
    blob = proc.stdout.read(head["binary"]) if "binary" in head else None
    return head["result"], blob


failures = 0


def check(cond, msg):
    global failures
    print(("  ok   " if cond else "  FAIL ") + msg)
    failures += not cond


cat, _ = call("catalog")
check(len(cat["features"]) >= 15 and len(cat["materials"]) >= 5, f"catalog: {len(cat['features'])} feature types")

doc, _ = call("example")
t = time.perf_counter()
res, _ = call("evaluate", {"document": doc, "quality": "draft"})
dt = time.perf_counter() - t
c = res["contract"]
g = res["geometry"]
check(set(c) == {"system_id", "kernel_metrics", "commerce_and_supply_chain", "security_forensics", "advanced_fea_and_cad", "telemetry_and_4d"}, "contract has exactly the six blocks")
check(g["watertight"], f"draft mesh watertight ({g['triangles']} tris, cell {g['cell_mm']:.3f} mm, {dt*1000:.0f} ms round trip)")
check(abs(c["telemetry_and_4d"]["mass_kg"] - 0.0854) < 0.002, f"mass {c['telemetry_and_4d']['mass_kg']*1000:.2f} g (bracket demo: 85.4 g)")
check(c["kernel_metrics"]["dag_nodes_computed"] == 14, f"dag nodes {c['kernel_metrics']['dag_nodes_computed']}")
check(not c["security_forensics"]["steganography_detected"], "no hidden cavities in the clean bracket")
print("  quote", c["commerce_and_supply_chain"], "access", res["analysis"]["access"])

# suppress the lattice: mass must rise (solid gusset) and the DAG shrinks by one node
doc2 = json.loads(json.dumps(doc))
for f in doc2["features"]:
    if f["id"] == "gusset_lattice":
        f["suppressed"] = True
for f in doc2["features"]:
    if f["id"] == "solid":
        f["inputs"] = ["profile", "gusset_lattice", "gusset"]
res2, _ = call("evaluate", {"document": doc2})
check(res2["contract"]["telemetry_and_4d"]["mass_kg"] > c["telemetry_and_4d"]["mass_kg"], "solid gusset is heavier than the lattice gusset")

# plant a 0.8 mm cavity 1 mm under the base top face
doc3 = json.loads(json.dumps(doc))
doc3["features"].append({"id": "void", "type": "sphere", "params": {"radius": 0.4, "center": [20, 0, 5.0]}})
doc3["features"].append({"id": "trap", "type": "subtract", "inputs": ["body", "void"]})
doc3["root"] = "trap"
res3, _ = call("evaluate", {"document": doc3, "quality": "fine"})
check(res3["contract"]["security_forensics"]["steganography_detected"], f"planted micro-void detected ({res3['analysis']['findings'][:1]})")

# lightweighting with a full-body lattice: designed voids must not raise the alarm
doc4 = json.loads(json.dumps(doc))
doc4["features"].append({"id": "infill", "type": "gyroid_infill", "inputs": ["body"], "params": {"cell": 14, "wall": 2.0, "skin": 1.7}})
doc4["root"] = "infill"
res4, _ = call("evaluate", {"document": doc4})
designed = sum(f["designed"] for f in res4["analysis"]["findings"])
check(not res4["contract"]["security_forensics"]["steganography_detected"] and designed > 0, f"lattice voids classified as designed ({designed}), no false alarm")
doc5 = json.loads(json.dumps(doc4))
doc5["features"] += [{"id": "void", "type": "sphere", "params": {"radius": 0.4, "center": [20, 0, 5.0]}},
                     {"id": "trap", "type": "subtract", "inputs": ["infill", "void"]}]
doc5["root"] = "trap"
res5, _ = call("evaluate", {"document": doc5, "quality": "fine"})
check(res5["contract"]["security_forensics"]["steganography_detected"], "a planted void in the solid skin is still caught")

call("evaluate", {"document": doc})
scene, blob = call("scene", {"bricks": 24})
n, regs, count, code = struct.unpack_from("<4I", blob)
check(n == 24 and count == 24**3 and len(blob) == 16 + 24 + count * 12 + code * 16, f"scene frame: {len(blob)} bytes, {regs} registers")
mesh, blob = call("mesh")
magic, ver, nv, ni = struct.unpack_from("<4I", blob)
check(magic == 0x48534D44 and len(blob) == 16 + nv * 24 + ni * 4, f"mesh frame: {nv} vertices")

q, _ = call("quote", {"material": "ti64", "quantity": 10})
check(q["price_usd"] > c["commerce_and_supply_chain"]["live_cnc_quote_usd"], f"10x titanium quote ${q['price_usd']:.2f}, {q['lead_days']} days")

out = Path(tempfile.gettempdir()) / "dk_smoke.stl"
r, _ = call("export_stl", {"path": str(out)})
check(out.stat().st_size == 84 + 50 * r["triangles"], "STL export size matches triangle count")

try:
    call("evaluate", {"document": {"version": 1, "features": [{"id": "a", "type": "nope"}]}})
    check(False, "bad document must error")
except RuntimeError as e:
    check("unknown feature type" in str(e), f"bad document error: {e}")

proc.stdin.close()
proc.wait(timeout=10)
print("failures:", failures)
sys.exit(1 if failures else 0)
