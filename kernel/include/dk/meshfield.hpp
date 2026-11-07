// Imported meshes as fields: a triangle mesh (from STEP via OpenCASCADE, or STL / OBJ) is
// baked into a signed-distance grid that the field language samples with Op::Grid.
//
// Bake: exact point-triangle distance in a narrow band around the surface, propagated to
// the whole grid by a vector distance transform (each voxel keeps the closest surface point
// found so far and passes it to its neighbours); the sign comes from ray parity along +X,
// so the mesh must be closed. Sampling is trilinear; outside the grid the distance to the
// grid box is added, which keeps the field a valid distance bound everywhere.
//
// Accuracy is set by the cell size: surfaces within about half a cell, sharp edges rounded
// at the cell scale. Mass properties of an imported part are therefore approximate (they
// converge with resolution), unlike native features.
#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "math.hpp"

namespace dk {

struct TriMesh {
    std::vector<Vec3> v;
    std::vector<std::array<uint32_t, 3>> f;
};

// .obj (v / f, polygons fan-triangulated) or .stl (binary or ASCII). Lengths as stored (mm).
TriMesh load_mesh(const std::string& path);

struct SdfGrid {
    Vec3 lo;          // centre of voxel (0, 0, 0)
    double h = 1;     // cell size (mm)
    int nx = 0, ny = 0, nz = 0;
    std::vector<float> d;  // x fastest
    float at(int i, int j, int k) const { return d[(size_t(k) * ny + j) * nx + i]; }
    Vec3 hi() const { return lo + Vec3{(nx - 1) * h, (ny - 1) * h, (nz - 1) * h}; }
};

// `cells` voxels across the longest side (plus a 3-cell margin on every side).
SdfGrid bake_sdf(const TriMesh& mesh, int cells);

// Process-wide registry used by Op::Grid; returns the id to put into the program.
int register_grid(std::shared_ptr<const SdfGrid> grid);
const SdfGrid& grid_by_id(int id);
size_t grid_count();

}  // namespace dk
