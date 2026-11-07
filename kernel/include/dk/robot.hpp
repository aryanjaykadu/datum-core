// Serial manipulators: kinematics, singularity analysis and reachable workspace.
//
// Reach analysis colours space by positional dexterity: the inverse condition number of the
// 3xN translational Jacobian, which is scale-free. Full-pose (6-D) singularities such as
// wrist alignment are counted separately, because mixing mm and rad needs an arbitrary length.
//
// Kinematics use standard Denavit-Hartenberg parameters (T_i = Rz(q) Tz(d) Tx(a) Rx(alpha)),
// lengths in mm, angles in radians. Dexterity at a configuration is the inverse condition
// number of the 6xN geometric Jacobian, with the linear rows divided by the arm's reach so
// translation and rotation are commensurate: 1 = isotropic, 0 = singular.
#pragma once

#include <string>
#include <vector>

#include "expr.hpp"
#include "shapes.hpp"

namespace dk {

// A fixed step of a kinematic chain, in the current local frame: translate by `v` (mm),
// or rotate by `angle` (rad) about the unit axis `v`.
struct Step {
    bool rotate = false;
    Vec3 v;
    double angle = 0;
};

// Revolute joint: fixed `pre` steps, rotation by q about local `axis`, fixed `post` steps.
// A DH joint is post = {Tz(d), Tx(a), Rx(alpha)}; a URDF joint is pre = {origin xyz, rpy}.
struct Joint {
    std::vector<Step> pre;
    Vec3 axis{0, 0, 1};
    std::vector<Step> post;
    double lo, hi;               // joint limits (rad)
};
Joint dh_joint(double d, double a, double alpha, double lo, double hi);

struct Robot {
    std::string key, name;
    std::vector<Joint> joints;
    double link_radius = 40;     // mm, for the geometry
    double mount_offset = 0;     // mm from DH frame 0 down to the mounting surface
    double reach() const;        // sum of all fixed translations: an upper bound on reach
};

const std::vector<Robot>& robot_presets();
const Robot& robot_preset(const std::string& key);  // falls back to the first preset

struct Frame { Vec3 o, x, y, z; };

// N + 1 frames (relative to the base origin): frames[i] sits on joint i+1 with z along its
// rotation axis (world); frames[N] is the tool flange with its full orientation.
std::vector<Frame> forward_kinematics(const Robot& r, const std::vector<double>& q);

// The arm's centre line as a polyline: every fixed translation adds a point. `link` is the
// link the segment ending at that point belongs to (0 = mount to joint 1, i = after joint i).
struct LinkPoint { Vec3 p; int link; };
std::vector<LinkPoint> chain_polyline(const Robot& r, const std::vector<double>& q);

// 6xN geometric Jacobian, rows 0-2 linear (divided by `length_scale`), rows 3-5 angular.
std::vector<double> jacobian(const Robot& r, const std::vector<Frame>& frames, double length_scale);

// Inverse condition number of an m x n matrix (row-major), via eigenvalues of J J^T.
// With `rank` > 0, only the `rank` largest singular values count: an arm whose motion is
// confined to fewer directions (a planar arm) is judged within the directions it has.
double inverse_condition(const std::vector<double>& J, int rows, int cols, int rank = 0);

// Arm geometry at a pose: capsules along each DH link plus joint spheres, at `base`.
Expr robot_geometry(const P3& p, const Robot& r, const std::vector<double>& q, Vec3 base);

// The arm as capsules in chain order (the same shapes robot_geometry draws).
struct Capsule { Vec3 a, b; double r; int link; };
std::vector<Capsule> robot_capsules(const Robot& r, const std::vector<double>& q, Vec3 base);

// Self-collision: two capsules that are not neighbours in the chain overlap, or a link
// below the first one dips under the mounting plane through the base (when `floor`).
struct Collision { bool hit = false; int link_a = -1, link_b = -1; bool floor = false; };
Collision self_collision(const Robot& r, const std::vector<double>& q, Vec3 base, bool floor);

// Closest distance between segments p1-q1 and p2-q2 (Ericson, Real-Time Collision Detection 5.1.9).
double segment_distance(Vec3 p1, Vec3 q1, Vec3 p2, Vec3 q2);

struct ReachVoxel {
    float x, y, z;               // voxel centre (mm, world)
    float best;                  // best dexterity among configurations reaching this voxel
    float worst;                 // worst dexterity seen there
};
struct ReachResult {
    std::vector<ReachVoxel> voxels;
    double voxel_mm = 0, reach_mm = 0, volume_m3 = 0;
    double singular_pct = 0;     // share of reachable voxels that are lock zones
    double dexterous_pct = 0;    // share with best dexterity >= 0.3
    size_t samples = 0, near_singular_samples = 0;  // the latter: full-pose singular (6-D)
    size_t colliding_samples = 0;  // poses rejected for self-collision / mounting-plane contact
};

// Monte-Carlo sampling of joint space within limits. A voxel is a lock zone when even its
// best reaching configuration has dexterity below `threshold`.
// With `collisions`, poses that self-collide (or hit the mounting plane when `floor`) are
// rejected: they do not reach anything.
ReachResult reachability(const Robot& r, Vec3 base, size_t samples, double threshold, uint64_t seed = 1,
                         bool collisions = true, bool floor = true);

}  // namespace dk
