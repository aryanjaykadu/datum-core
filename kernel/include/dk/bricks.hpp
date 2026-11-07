// View-independent GPU acceleration structure: the bounding box is split into n^3
// bricks and the program is interval-pruned once per brick. A ray marcher looks up the
// brick containing the current point and runs only that brick's (much shorter) program;
// bricks proven empty or solid are skipped without evaluating anything.
#pragma once

#include <string>
#include <vector>

#include "expr.hpp"

namespace dk {

struct BrickGrid {
    Vec3 lo, hi;
    int n = 0;
    // per brick: kind 0 = empty (outside), 1 = solid (inside), 2 = surface (run program)
    std::vector<uint8_t> kind;
    std::vector<uint32_t> offset, length;  // into `code` for surface bricks
    // All brick programs after register allocation: operands a/b and the destination
    // `dst` are slot numbers, not instruction indices. Each brick's result is in the
    // destination slot of its last instruction.
    std::vector<Instr> code;
    std::vector<uint16_t> dst;
    size_t max_length = 0;
    size_t max_registers = 0;  // slots a GPU thread needs: max over bricks of live values
};

BrickGrid build_bricks(const Program& f, Vec3 lo, Vec3 hi, int n);

// Compact JSON for the WebGPU viewport (viewport/scene.json).
std::string to_json(const BrickGrid& g, const std::string& name);

}  // namespace dk
