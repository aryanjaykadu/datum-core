// Export the bracket as a brick-pruned program for the WebGPU viewport.
//
//   export_scene [bricks_per_axis] [out.json]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "bracket_model.hpp"
#include "dk/bricks.hpp"

using namespace dk;

int main(int argc, char** argv) {
    int n = argc > 1 ? std::atoi(argv[1]) : 24;
    std::string out = argc > 2 ? argv[2] : "scene.json";

    Graph g;
    Program f = compile(build_bracket(g, BracketParams{}));
    auto t0 = std::chrono::steady_clock::now();
    // a cube around the part keeps bricks cubic, so the ray marcher's DDA stays simple
    BrickGrid grid = build_bricks(f, {-42, -42, -16}, {42, 42, 68}, n);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

    size_t surface = 0;
    for (auto k : grid.kind) surface += k == 2;
    std::printf("program %zu instrs -> %zu surface bricks of %zu, avg %.1f instrs, max %zu (%.1f ms)\n", f.size(), surface,
                grid.kind.size(), surface ? double(grid.code.size()) / double(surface) : 0.0, grid.max_length, ms);
    std::ofstream(out) << to_json(grid, "bracket");
    std::printf("wrote %s\n", out.c_str());
}
