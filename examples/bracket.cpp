// End-to-end demo: a parametric L-bracket with smooth fillets, bolt holes, a lightening
// pocket and a gyroid-filled web, meshed with Dual Contouring and exported to STL.
//
//   bracket [cell_mm] [out.stl]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "dk/mesh.hpp"
#include "bracket_model.hpp"

using namespace dk;

int main(int argc, char** argv) {
    double cell = argc > 1 ? std::atof(argv[1]) : 0.2;
    std::string out = argc > 2 ? argv[2] : "bracket.stl";

    Graph g;
    BracketParams params;
    auto t0 = std::chrono::steady_clock::now();
    Program f = compile(build_bracket(g, params));
    double t_compile = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    MeshStats st;
    Mesh m = dual_contour(f, {{-41, -21, -1}, {41, 21, 51}, cell}, &st);
    Topology topo = topology(m);
    Deviation dev = deviation(m, f);
    MassProperties mp = mass_properties(m);
    const double rho = 2.70e-6;  // aluminium, kg/mm^3

    std::printf("Datum implicit kernel - bracket demo\n");
    std::printf("  dropped quads      %zu, surface cells clipped by the grid %zu\n", st.missing_quads, st.clipped_cells);
    std::printf("  graph nodes        %zu, program %zu instrs (compile %.1f ms)\n", g.size(), f.size(), t_compile * 1e3);
    std::printf("  octree leaves      %zu at %.3f mm, avg pruned program %.1f instrs\n", st.leaves, cell, st.avg_pruned_size);
    std::printf("  mesh               %zu vertices, %zu triangles in %.2f s\n", m.vertices.size(), m.triangles.size(), st.seconds);
    std::printf("  topology           boundary %zu, nonmanifold %zu, euler %ld -> %s\n", topo.boundary_edges, topo.nonmanifold_edges,
                topo.euler, topo.watertight() ? "watertight" : "NOT watertight");
    std::printf("  deviation |f|      max %.4f mm, mean %.5f mm over %zu samples\n", dev.max_abs, dev.mean_abs, dev.samples);
    std::printf("  volume             %.1f mm^3   area %.1f mm^2\n", mp.volume_mm3, mp.area_mm2);
    std::printf("  mass (Al 2700)     %.2f g, CoG (%.3f, %.3f, %.3f) mm\n", mp.volume_mm3 * rho * 1e3, mp.centroid.x, mp.centroid.y, mp.centroid.z);
    std::printf("  inertia @CoG kg.mm2  Ixx %.2f  Iyy %.2f  Izz %.2f\n", mp.inertia[0][0] * rho, mp.inertia[1][1] * rho, mp.inertia[2][2] * rho);
    if (!write_stl(m, out, "datum-kernel bracket")) {
        std::printf("  could not write %s\n", out.c_str());
        return 1;
    }
    std::printf("  wrote %s\n", out.c_str());
    return 0;
}
