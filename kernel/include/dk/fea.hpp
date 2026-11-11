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

#include <algorithm>
#include <cmath>
#include <vector>

#include "expr.hpp"
#include "math.hpp"
#include "shapes.hpp"

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
//
// `stiffness` (per voxel, optional) scales each element's modulus: a lattice zone treated as an
// equivalent material. `stress_amplify` (per voxel, optional) turns that zone's averaged stress
// into the stress in its thin walls (e.g. 1 / relative density).
FeaResult solve_elastic(const FeaGrid& g, double youngs_mpa, double poisson,
                        const std::vector<FeaSphere>& fixtures, const std::vector<FeaSphere>& loads,
                        int max_iterations = 6000, double tolerance = 1e-6,
                        const std::vector<float>* stiffness = nullptr, const std::vector<float>* stress_amplify = nullptr);

// Gyroid sheet of wall t in a cell c: relative density and the equivalent stiffness factor
// (Gibson-Ashby style scaling for sheet TPMS, E* / E = rho^1.5).
inline double gyroid_density(double wall, double cell) { return std::clamp(3.1 * wall / cell, 0.03, 1.0); }
inline double gyroid_stiffness(double density) { return std::pow(density, 1.5); }

// Smooth von Mises at a point: trilinear in the node field (0 outside the grid).
double sample_node_field(const FeaGrid& g, const std::vector<float>& node_field, Vec3 p);

// Tight box around a part (mm), from interval-pruned bricks. Throws when empty or unbounded.
void part_bounds(const Program& part, Vec3& lo, Vec3& hi);

// ---- stress-graded lightweighting
struct LightenOptions {
    double target = 0.4;        // fraction of the mass to remove (0.1 .. 0.8)
    double cell = 0;            // gyroid cell (mm); 0 = about 1/7 of the part's smallest side, 4..20 mm
    double skin = 1.6;          // solid outer wall kept (mm)
    double min_wall = 0.6;      // thinnest lattice wall (mm): printable with a 0.4 mm nozzle
    double voxels = 25000;      // resolution of the stress solve and the thickness field
};
struct LightenStudy { std::vector<FeaSphere> fixtures, loads; double youngs_mpa = 69000; };
struct LightenResult {
    Expr shape;                 // the lightened part
    double removed = 0;         // achieved mass fraction removed (sampled)
    double wall_min = 0, wall_max = 0, cell = 0;
    bool graded = false;        // true when a stress study shaped the lattice
    int grid_id = -1;
};
// Keeps a solid skin and fills the core with a gyroid whose wall thickness follows the stress
// field (thick where the part works, thin where it idles), scaled to remove `target` of the
// mass. Without a study (no fixtures or loads) the lattice is uniform.
LightenResult lighten(const P3& p, Expr body, const LightenStudy& study, const LightenOptions& opt);


}  // namespace dk
