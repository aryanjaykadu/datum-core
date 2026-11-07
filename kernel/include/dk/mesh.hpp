// Meshing an implicit field: interval-culled sparse octree + Dual Contouring.
//
// 1. Octree descent over the bounding cube. At each node the program is evaluated over
//    the node's box in interval arithmetic: if the bound excludes 0 the node is entirely
//    inside or outside and is dropped. Otherwise the program is pruned to that box and
//    the node is split. Leaves are cells of edge length `cell` that may hold surface.
// 2. Hermite data: on every cell edge whose end signs differ, the crossing is found by
//    a safeguarded root solve and the exact normal comes from forward-mode autodiff.
// 3. One vertex per cell minimises the QEF sum((n_i . (x - p_i))^2) (pseudo-inverse with
//    eigenvalue truncation, clamped to the cell). This keeps sharp edges and corners,
//    which marching cubes would round off.
// 4. Each sign-changing grid edge emits a quad joining the 4 cells around it.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "expr.hpp"

namespace dk {

struct Mesh {
    std::vector<Vec3> vertices;
    std::vector<std::array<uint32_t, 3>> triangles;
};

struct MeshOptions {
    Vec3 lo, hi;                  // region that contains the part (mm)
    double cell = 0.25;           // finest cell edge (mm)
    double qef_truncation = 0.1;  // relative eigenvalue cut-off: lower = sharper but noisier
    int threads = 0;              // 0 = hardware concurrency
};

struct MeshStats {
    size_t leaves = 0, surface_cells = 0, missing_quads = 0;
    size_t clipped_cells = 0;     // surface found at the edge of the grid: widen lo/hi
    double avg_pruned_size = 0;   // mean instruction count of leaf programs
    size_t full_size = 0;         // instruction count of the unpruned program
    double seconds = 0;
};

Mesh dual_contour(const Program& f, const MeshOptions& opt, MeshStats* stats = nullptr);

// ---- quality and properties
struct Topology {
    size_t boundary_edges = 0;     // used by one triangle  (0 for a closed mesh)
    size_t nonmanifold_edges = 0;  // used by >2 triangles or twice the same direction
    long euler = 0;                // V - E + F (2 per genus-0 shell)
    bool watertight() const { return boundary_edges == 0 && nonmanifold_edges == 0; }
};
Topology topology(const Mesh& m);

struct Deviation {
    double max_abs = 0, mean_abs = 0;  // |f| sampled at vertices, edge midpoints, face centres
    size_t samples = 0;
};
Deviation deviation(const Mesh& m, const Program& f);

struct MassProperties {
    double volume_mm3 = 0, area_mm2 = 0;
    Vec3 centroid;
    double inertia[3][3] = {};  // per unit density, about the centroid (mm^5)
};
MassProperties mass_properties(const Mesh& m);

bool write_stl(const Mesh& m, const std::string& path, const std::string& header = "datum-kernel");
bool write_obj(const Mesh& m, const std::string& path);

}  // namespace dk
