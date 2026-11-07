"""STEP -> OBJ converter sidecar (OpenCASCADE via cascadio).

    step_convert <input.step> <output.obj>

Tessellation tolerance scales with the part: 0.02 % of the bounding-box diagonal for the
linear deflection (min 0.002 mm) and 0.25 rad angular, fine enough that the baked field is
limited by the grid, not by the tessellation. Exit code 0 on success; errors go to stderr.
"""
import re
import sys


def part_scale(path: str) -> float:
    """Rough size from the CARTESIAN_POINTs in the file (mm), to choose the tolerance."""
    lo, hi = [1e300] * 3, [-1e300] * 3
    pat = re.compile(r"CARTESIAN_POINT\s*\(\s*'[^']*'\s*,\s*\(\s*([-\d.eE+]+)\s*,\s*([-\d.eE+]+)\s*,\s*([-\d.eE+]+)")
    with open(path, errors="ignore") as f:
        for m in pat.finditer(f.read()):
            for a in range(3):
                v = float(m.group(a + 1))
                lo[a], hi[a] = min(lo[a], v), max(hi[a], v)
    if lo[0] > hi[0]:
        return 100.0
    return max(sum((h - l) ** 2 for l, h in zip(lo, hi)) ** 0.5, 1e-3)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: step_convert <input.step> <output.obj>", file=sys.stderr)
        return 2
    src, dst = sys.argv[1], sys.argv[2]
    import cascadio

    tol = max(part_scale(src) * 2e-4, 0.002)
    rc = cascadio.step_to_obj(src, dst, tol_linear=tol, tol_angular=0.25, tol_relative=False, use_colors=False)
    if rc != 0:
        print(f"OpenCASCADE could not read {src} (code {rc})", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
