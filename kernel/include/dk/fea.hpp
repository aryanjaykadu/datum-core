// Linear-elastic stress analysis on voxels.
//
// The part is sampled on a regular grid; every voxel whose centre is inside becomes an 8-node
// trilinear hexahedron (all identical, so one element stiffness matrix serves the whole mesh).
// K u = f is solved matrix-free with Jacobi-preconditioned conjugate gradients, gathering per
// node so it runs on all cores without locks. Units: mm, N, MPa; displacements in mm.
//
// Fixtures clamp every node inside a sphere; loads spread a force evenly over the nodes in a
// sphere. Results are von Mises stress per voxel and averaged to nodes for smooth sampling.
// Accuracy is that of first-order hexes: a few elements through a section bend about 5-15 %
// stiff (tested against beam theory), and stresses at clamps and point loads are singular.
#pragma once

#include <vector>

#include "expr.hpp"
#include "math.hpp"

namespace dk {

struct FeaSphere { Vec3 centre; double radius; Vec3 force{}; };  // force (N) only for loads

struct FeaGrid {
    Vec3 lo;            // corner of voxel (0, 0, 0)
    double h = 1;       // voxel edge (mm)
    int nx = 0, ny = 0, nz = 0;
    std::vector<uint8_t> solid;  // nx * ny * nz, x fastest
    size_t solid_count() const;
};

// Sample a part on voxels of edge h covering [lo, hi].
FeaGrid voxelize(const Program& part, Vec3 lo, Vec3 hi, double h);
// Edge length that gives about `target` voxels in the box.
double voxel_size_for(Vec3 lo, Vec3 hi, double target_voxels);

struct FeaResult {
    std::vector<float> von_mises;   // per voxel (MPa), 0 for empty voxels
    std::vector<float> node_vm;     // per node, averaged over the solid voxels around it
    std::vector<Vec3> displacement; // per node (mm)
    double max_vm = 0, max_displacement = 0;
    int iterations = 0;
    bool converged = false;
    size_t fixed_nodes = 0, loaded_nodes = 0;
    double seconds = 0;
};

// Throws std::runtime_error when there is nothing to solve (no solid voxels, no fixture
// touching the part, or a load that touches no node).
FeaResult solve_elastic(const FeaGrid& g, double youngs_mpa, double poisson,
                        const std::vector<FeaSphere>& fixtures, const std::vector<FeaSphere>& loads,
                        int max_iterations = 6000, double tolerance = 1e-6);

// Smooth von Mises at a point: trilinear in the node field (0 outside the grid).
double sample_node_field(const FeaGrid& g, const std::vector<float>& node_field, Vec3 p);

}  // namespace dk
