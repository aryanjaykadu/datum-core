// Kernel tests. Each check states the property it guards; tolerances are physical (mm).
#include <cstdio>
#include <filesystem>
#include <functional>
#include <numbers>
#include <random>
#include <string>
#include <vector>

#include "dk/bricks.hpp"
#include "dk/document.hpp"
#include "dk/mesh.hpp"
#include "dk/robot.hpp"
#include "dk/meshfield.hpp"
#include "dk/parts.hpp"
#include "dk/fea.hpp"
#include "dk/sketch.hpp"
#include "dk/shapes.hpp"

using namespace dk;

static int failures = 0, checks = 0;
#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        ++checks;                                                              \
        if (!(cond)) {                                                         \
            ++failures;                                                        \
            std::printf("  FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond);      \
            std::printf(__VA_ARGS__);                                          \
            std::printf("\n");                                                 \
        }                                                                      \
    } while (0)

struct Test {
    const char* name;
    std::function<void()> fn;
};
static std::vector<Test>& registry() {
    static std::vector<Test> r;
    return r;
}
#define TEST(name)                                                   \
    static void name();                                              \
    static bool name##_reg = (registry().push_back({#name, name}), true); \
    static void name()

static std::mt19937_64 rng(12345);
static double uniform(double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); }

// A part that exercises every op: smooth union, holes, shell, rotation, lattice.
static Expr test_part(Graph& g) {
    P3 p = coords(g);
    Expr plate = box(p, {60, 30, 6});
    Expr boss = cylinder_z(translate(p, {15, 0, 6}), 8, 12);
    Expr body = smooth_unite(plate, boss, 3);
    Expr hole = cylinder_z(translate(p, {-15, 0, 0}), 3.3, 20);
    Expr tilted = box(rotate_y(translate(p, {0, 0, 10}), 30), {10, 10, 10});
    Expr lattice = intersect(gyroid(p, 8, 1.2), box(translate(p, {-15, 0, 0}), {20, 20, 4}));
    return unite(subtract(unite(body, shell(tilted, 1.0)), hole), lattice);
}

TEST(hash_consing_shares_subexpressions) {
    Graph g;
    P3 p = coords(g);
    Expr a = sphere(p, 5), b = sphere(p, 5);
    CHECK(a.id() == b.id(), "identical expressions should share a node");
    size_t before = g.size();
    (void)sphere(p, 5);
    CHECK(g.size() == before, "rebuilding should add no nodes (%zu -> %zu)", before, g.size());
}

TEST(box_sdf_is_exact) {
    Graph g;
    Program f = compile(box(coords(g), {20, 10, 4}));
    CHECK(std::abs(f({0, 0, 0}) + 2) < 1e-12, "centre depth %g", f({0, 0, 0}));
    CHECK(std::abs(f({13, 0, 0}) - 3) < 1e-12, "face distance %g", f({13, 0, 0}));
    CHECK(std::abs(f({13, 9, 0}) - 5) < 1e-12, "edge distance %g", f({13, 9, 0}));  // (3,4) -> 5
}

TEST(interval_sin_cos_bound_samples) {
    for (int n = 0; n < 2000; ++n) {
        double lo = uniform(-20, 20), hi = lo + uniform(0, 8);
        Interval s = sin(Interval{lo, hi}), c = cos(Interval{lo, hi});
        for (int k = 0; k <= 20; ++k) {
            double x = lo + (hi - lo) * k / 20;
            CHECK(s.lo - 1e-12 <= std::sin(x) && std::sin(x) <= s.hi + 1e-12, "sin [%g,%g]", lo, hi);
            CHECK(c.lo - 1e-12 <= std::cos(x) && std::cos(x) <= c.hi + 1e-12, "cos [%g,%g]", lo, hi);
        }
    }
}

TEST(interval_eval_bounds_every_point) {
    Graph g;
    Program f = compile(test_part(g));
    for (int n = 0; n < 300; ++n) {
        Vec3 c{uniform(-35, 35), uniform(-20, 20), uniform(-5, 18)};
        double h = uniform(0.1, 8);
        Interval b = f.bounds({c.x, c.x + h}, {c.y, c.y + h}, {c.z, c.z + h});
        for (int k = 0; k < 30; ++k) {
            double v = f({c.x + uniform(0, h), c.y + uniform(0, h), c.z + uniform(0, h)});
            CHECK(b.lo - 1e-9 <= v && v <= b.hi + 1e-9, "value %g outside [%g, %g]", v, b.lo, b.hi);
        }
    }
}

TEST(pruned_program_is_exact_inside_its_box) {
    Graph g;
    Program f = compile(test_part(g));
    size_t shrunk = 0;
    for (int n = 0; n < 300; ++n) {
        Vec3 c{uniform(-35, 35), uniform(-20, 20), uniform(-5, 18)};
        double h = uniform(0.2, 4);
        Program pf = f.prune({c.x, c.x + h}, {c.y, c.y + h}, {c.z, c.z + h});
        shrunk += pf.size() < f.size();
        for (int k = 0; k < 20; ++k) {
            Vec3 q{c.x + uniform(0, h), c.y + uniform(0, h), c.z + uniform(0, h)};
            CHECK(f(q) == pf(q), "pruned %g != full %g", pf(q), f(q));
        }
    }
    CHECK(shrunk > 150, "pruning should shorten most small-box programs (%zu/300)", shrunk);
}

TEST(autodiff_gradient_matches_finite_differences) {
    Graph g;
    Program f = compile(test_part(g));
    const double h = 1e-6;
    for (int n = 0; n < 500; ++n) {
        Vec3 p{uniform(-35, 35), uniform(-20, 20), uniform(-5, 18)};
        Vec3 grad = f.gradient(p).d;
        for (int ax = 0; ax < 3; ++ax) {
            Vec3 a = p, b = p;
            a[ax] -= h;
            b[ax] += h;
            double fd = (f(b) - f(a)) / (2 * h);
            // min/max/abs kinks make FD meaningless right at a switch; skip those samples
            if (std::abs(f(b) - f(a) - 2 * h * grad[ax]) > 1e-4) continue;
            CHECK(std::abs(fd - grad[ax]) < 1e-5, "axis %d: ad %g fd %g", ax, grad[ax], fd);
        }
    }
}

TEST(sphere_mesh_is_closed_and_accurate) {
    Graph g;
    Program f = compile(sphere(coords(g), 10));
    Mesh m = dual_contour(f, {{-10, -10, -10}, {10, 10, 10}, 0.25});
    Topology t = topology(m);
    CHECK(t.watertight(), "boundary %zu nonmanifold %zu", t.boundary_edges, t.nonmanifold_edges);
    CHECK(t.euler == 2, "euler %ld", t.euler);
    double exact = 4.0 / 3.0 * std::numbers::pi * 1000;
    MassProperties mp = mass_properties(m);
    CHECK(std::abs(mp.volume_mm3 / exact - 1) < 2e-3, "volume %g vs %g", mp.volume_mm3, exact);
    Deviation d = deviation(m, f);
    CHECK(d.max_abs < 0.01, "max deviation %g mm", d.max_abs);
}

TEST(sharp_features_survive_meshing) {
    // A box minus a through-hole: exact volume and inertia are known in closed form.
    Graph g;
    P3 p = coords(g);
    Program f = compile(subtract(box(p, {40, 20, 10}), cylinder_z(p, 5, 30)));
    // deliberately misaligned with the grid so edges do not land on cell faces
    Mesh m = dual_contour(f, {{-20.13, -10.07, -5.11}, {20, 10, 5}, 0.3});
    Topology t = topology(m);
    CHECK(t.watertight(), "boundary %zu nonmanifold %zu", t.boundary_edges, t.nonmanifold_edges);
    CHECK(t.euler == 0, "a block with one hole is genus 1 (euler 0), got %ld", t.euler);
    double exact = 40 * 20 * 10 - std::numbers::pi * 25 * 10;
    MassProperties mp = mass_properties(m);
    CHECK(std::abs(mp.volume_mm3 / exact - 1) < 1e-3, "volume %g vs %g", mp.volume_mm3, exact);
    // box corners must be reproduced exactly by the QEF (marching cubes would chamfer them)
    double best = 1e9;
    for (auto& v : m.vertices) best = std::min(best, length(v - Vec3{20, 10, 5}));
    CHECK(best < 1e-6, "nearest vertex to the corner is %g mm away", best);
    double izz_box = 8000.0 * (40 * 40 + 20 * 20) / 12, izz_hole = std::numbers::pi * 250 * 25 / 2;
    CHECK(std::abs(mp.inertia[2][2] / (izz_box - izz_hole) - 1) < 2e-3, "Izz %g vs %g", mp.inertia[2][2], izz_box - izz_hole);
}

TEST(notch_fillet_is_exact_and_does_not_bulge) {
    // L-profile 80 x 50, wall 6, width 40, inner fillet r = 6 (the bracket construction)
    Graph g;
    P3 p = coords(g);
    double L = 80, H = 50, t = 6, W = 40, r = 6, big = 4 * (L + H);
    Expr envelope = box(translate(p, {0, 0, H / 2}), {L, W, H});
    Expr notch = rounded_box(translate(p, {-L / 2 + t + big / 2, 0, t + big / 2}), {big, 4 * big, big}, r);
    Program f = compile(subtract(envelope, notch));
    MeshStats st;
    Mesh m = dual_contour(f, {{-L / 2, -W / 2, 0}, {L / 2, W / 2, H}, 0.25}, &st);
    CHECK(st.clipped_cells == 0 && st.missing_quads == 0, "clipped %zu missing %zu", st.clipped_cells, st.missing_quads);
    CHECK(topology(m).watertight(), "not watertight");
    double area = L * t + (H - t) * t + r * r * (1 - std::numbers::pi / 4);
    double vol = mass_properties(m).volume_mm3;
    CHECK(std::abs(vol / (area * W) - 1) < 1e-3, "volume %g vs %g", vol, area * W);
}

TEST(register_allocated_bricks_match_the_program) {
    // reference interpreter for the GPU bytecode: same semantics as raymarch.wgsl
    Graph g;
    // includes an extruded concave sketch so copysign is exercised on the GPU path
    P3 sp = translate(coords(g), {-30, -30, 20});
    Expr sketch = extrude_profile(sp, tessellate_profile({{0, 0, 3}, {25, 0}, {25, 8}, {8, 8}, {8, 20, 2}, {0, 20}}), 6, false);
    Program f = compile(unite(test_part(g), sketch));
    BrickGrid grid = build_bricks(f, {-36, -36, -18}, {36, 36, 54}, 12);
    CHECK(grid.max_registers < 32, "registers %zu", grid.max_registers);
    std::vector<double> s(grid.max_registers);
    size_t tested = 0;
    double bs = 72.0 / 12;
    for (size_t b = 0; b < grid.kind.size(); ++b) {
        if (grid.kind[b] != 2) continue;
        int i = int(b % 12), j = int((b / 12) % 12), k = int(b / 144);
        for (int n = 0; n < 8; ++n) {
            Vec3 p{-36 + (i + uniform(0, 1)) * bs, -36 + (j + uniform(0, 1)) * bs, -18 + (k + uniform(0, 1)) * bs};
            uint16_t out = 0;
            for (uint32_t q = grid.offset[b]; q < grid.offset[b] + grid.length[b]; ++q) {
                const Instr& in = grid.code[q];
                double a = s[in.a], c = s[in.b], v = 0;
                switch (in.op) {
                    case Op::X: v = p.x; break;
                    case Op::Y: v = p.y; break;
                    case Op::Z: v = p.z; break;
                    case Op::Const: v = in.c; break;
                    case Op::Add: v = a + c; break;
                    case Op::Sub: v = a - c; break;
                    case Op::Mul: v = a * c; break;
                    case Op::Div: v = a / c; break;
                    case Op::Neg: v = -a; break;
                    case Op::Min: v = std::min(a, c); break;
                    case Op::Max: v = std::max(a, c); break;
                    case Op::Abs: v = std::abs(a); break;
                    case Op::Sqrt: v = std::sqrt(std::max(a, 0.0)); break;
                    case Op::Square: v = a * a; break;
                    case Op::Sin: v = std::sin(a); break;
                    case Op::Cos: v = std::cos(a); break;
                    case Op::Atan2: v = std::atan2(a, c); break;
                    case Op::Floor: v = std::floor(a); break;
                    case Op::CopySign: v = c < 0 ? -std::abs(a) : std::abs(a); break;
                    case Op::Grid: v = grid_eval(int(in.c), a, c, s[in.d]); break;
                }
                out = grid.dst[q];
                s[out] = v;
            }
            CHECK(s[out] == f(p), "brick %zu: %g vs %g", b, s[out], f(p));
            ++tested;
        }
    }
    CHECK(tested > 100, "only %zu samples", tested);
}

TEST(robot_forward_kinematics_matches_published_home_pose) {
    // UR5 at q = 0: flange at (a2 + a3, -(d4 + d6), d1 - d5) = (-817.25, -191.45, -5.491) mm
    auto f = forward_kinematics(robot_preset("ur5"), std::vector<double>(6, 0.0));
    Vec3 e = f.back().o;
    CHECK(length(e - Vec3{-817.25, -191.45, -5.491}) < 1e-6, "UR5 home flange at (%g, %g, %g)", e.x, e.y, e.z);
    auto p = forward_kinematics(robot_preset("planar3r"), {0.3, -0.5, 0.9});
    double x = 300 * std::cos(0.3) + 250 * std::cos(-0.2) + 150 * std::cos(0.7);
    CHECK(std::abs(p.back().o.x - x) < 1e-9, "planar tip x %g vs %g", p.back().o.x, x);
}

TEST(robot_jacobian_matches_finite_differences) {
    const Robot& r = robot_preset("puma560");
    std::vector<double> q = {0.3, -0.7, 1.1, 0.4, -0.6, 0.2};
    double L = r.reach();
    auto J = jacobian(r, forward_kinematics(r, q), L);
    for (int i = 0; i < 6; ++i) {
        auto qp = q, qm = q;
        qp[i] += 1e-6;
        qm[i] -= 1e-6;
        Vec3 d = (forward_kinematics(r, qp).back().o - forward_kinematics(r, qm).back().o) * (1.0 / 2e-6 / L);
        for (int k = 0; k < 3; ++k) CHECK(std::abs(J[k * 6 + i] - d[k]) < 1e-6, "J[%d][%d] %g vs %g", k, i, J[k * 6 + i], d[k]);
    }
}

TEST(robot_singularities_are_detected) {
    const Robot& planar = robot_preset("planar3r");
    double stretched = inverse_condition(jacobian(planar, forward_kinematics(planar, {0.4, 0, 0}), planar.reach()), 6, 3);
    double bent = inverse_condition(jacobian(planar, forward_kinematics(planar, {0.4, 1.2, -0.9}), planar.reach()), 6, 3);
    CHECK(stretched < 1e-6, "fully stretched arm must be singular: %g", stretched);
    CHECK(bent > 0.05, "bent arm should be well conditioned: %g", bent);
    // UR5 wrist singularity: q5 = 0 aligns joints 4 and 6
    const Robot& ur = robot_preset("ur5");
    double wrist = inverse_condition(jacobian(ur, forward_kinematics(ur, {0.2, -1.0, 1.2, -0.5, 0.0, 0.3}), ur.reach()), 6, 6);
    CHECK(wrist < 1e-6, "UR5 wrist singularity at q5 = 0: %g", wrist);
}

TEST(robot_reach_envelope_is_bounded_and_flags_lock_zones) {
    const Robot& r = robot_preset("planar3r");
    ReachResult rr = reachability(r, {0, 0, 0}, 60000, 0.05);
    double maxr = 0;
    for (auto& v : rr.voxels) maxr = std::max(maxr, std::hypot(double(v.x), double(v.y)));
    CHECK(maxr <= 700 + rr.voxel_mm, "envelope radius %g exceeds 700 mm reach", maxr);
    CHECK(rr.voxels.size() > 100 && rr.volume_m3 > 0, "voxels %zu", rr.voxels.size());
    CHECK(rr.singular_pct >= 0 && rr.singular_pct < 50, "lock-zone share %g%%", rr.singular_pct);
    Graph g;
    Program arm = compile(robot_geometry(coords(g), robot_preset("ur5"), {0, -1.57, 0, -1.57, 0, 0}, {0, 0, 0}));
    CHECK(arm({0, 0, 20}) < 0, "pedestal / base joint should be solid at the base");
}

TEST(segment_distance_matches_hand_cases) {
    CHECK(std::abs(segment_distance({0, 0, 0}, {10, 0, 0}, {5, 3, 0}, {5, 3, 8}) - 3) < 1e-12, "crossing offset");
    CHECK(std::abs(segment_distance({0, 0, 0}, {10, 0, 0}, {12, 0, 0}, {20, 0, 0}) - 2) < 1e-12, "collinear gap");
    CHECK(std::abs(segment_distance({0, 0, 0}, {10, 0, 0}, {0, 4, 0}, {10, 4, 0}) - 4) < 1e-12, "parallel");
    CHECK(segment_distance({0, 0, 0}, {10, 0, 0}, {5, -1, 0}, {5, 1, 0}) < 1e-12, "intersecting");
}

TEST(robot_self_collision_detects_folds_and_the_mount) {
    const Robot& ur = robot_preset("ur5");
    const double d = std::numbers::pi / 180;
    auto check = [&](std::vector<double> deg_q, bool floor) {
        for (auto& v : deg_q) v *= d;
        return self_collision(ur, deg_q, {0, 0, 0}, floor);
    };
    CHECK(!check({0, -90, 0, -90, 0, 0}, true).hit, "UR5 upright home pose must be clear");
    { Collision z = check({0, 0, 0, 0, 0, 0}, true); CHECK(!z.hit, "UR5 zero pose must be clear (links %d/%d floor %d)", z.link_a, z.link_b, int(z.floor)); }
    Collision fold = check({0, -90, 175, 0, 0, 0}, false);
    CHECK(fold.hit && !fold.floor, "forearm folded onto the upper arm must self-collide (links %d/%d)", fold.link_a, fold.link_b);
    Collision down = check({0, 60, 0, 0, 0, 0}, true);
    CHECK(down.hit && down.floor, "upper arm pointing below the mount must hit the mounting surface");
    CHECK(!check({0, 60, 0, 0, 0, 0}, false).hit, "...but not when the mount check is off");

    ReachResult free = reachability(ur, {0, 0, 0}, 40000, 0.05, 7, false, false);
    ReachResult real = reachability(ur, {0, 0, 0}, 40000, 0.05, 7, true, true);
    CHECK(free.colliding_samples == 0 && real.colliding_samples > 0, "rejected %zu of %zu", real.colliding_samples, real.samples);
    CHECK(real.voxels.size() < free.voxels.size(), "collision-free envelope %zu should be smaller than %zu", real.voxels.size(), free.voxels.size());
    for (auto& v : real.voxels) {
        if (v.z < -ur.link_radius) {
            CHECK(false, "with the mount check, no reachable voxel should sit below the mount: z = %g", v.z);
            break;
        }
    }
}

TEST(chain_format_matches_dh_and_urdf_conventions) {
    // the planar 3R written as a URDF-style chain must equal its DH preset everywhere
    json chain = json::parse(R"({"format": "chain", "joints": [
        {"axis": [0, 0, 1], "lo_deg": -180, "hi_deg": 180},
        {"origins": [{"xyz": [300, 0, 0]}], "axis": [0, 0, 1], "lo_deg": -150, "hi_deg": 150},
        {"origins": [{"xyz": [250, 0, 0]}], "axis": [0, 0, 1], "lo_deg": -150, "hi_deg": 150}],
        "tool": {"origins": [{"xyz": [150, 0, 0]}]}})");
    Robot c = robot_from_json(chain);
    const Robot& d = robot_preset("planar3r");
    for (int n = 0; n < 50; ++n) {
        std::vector<double> q = {uniform(-3, 3), uniform(-2.5, 2.5), uniform(-2.5, 2.5)};
        Vec3 a = forward_kinematics(c, q).back().o, b = forward_kinematics(d, q).back().o;
        CHECK(length(a - b) < 1e-9, "chain tip (%g, %g) vs DH (%g, %g)", a.x, a.y, b.x, b.y);
    }
    // rpy: a joint rolled +90 deg about x turns its local z axis into world -y
    Robot rolled = robot_from_json(json::parse(R"({"format": "chain", "joints": [{"origins": [{"rpy_deg": [90, 0, 0]}], "axis": [0, 0, 1]}],
                                                   "tool": {"origins": [{"xyz": [0, 0, 100]}]}})"));
    Vec3 z = forward_kinematics(rolled, {0}).front().z;
    CHECK(length(z - Vec3{0, -1, 0}) < 1e-12, "rolled axis (%g, %g, %g)", z.x, z.y, z.z);
    // a DH definition in JSON equals the built-in preset
    json dh = json::parse(R"({"format": "dh", "joints": [{"d": 89.159, "alpha_deg": 90, "lo_deg": -360, "hi_deg": 360},
        {"a": -425, "lo_deg": -360, "hi_deg": 360}, {"a": -392.25}, {"d": 109.15, "alpha_deg": 90}, {"d": 94.65, "alpha_deg": -90}, {"d": 82.3}]})");
    Vec3 e = forward_kinematics(robot_from_json(dh), std::vector<double>(6, 0.0)).back().o;
    CHECK(length(e - Vec3{-817.25, -191.45, -5.491}) < 1e-6, "UR5 from JSON home (%g, %g, %g)", e.x, e.y, e.z);
    bool threw = false;
    try { robot_from_json(json::parse(R"({"format": "dh", "joints": [{"a": 100, "lo_deg": 10, "hi_deg": -10}]})")); } catch (const std::exception&) { threw = true; }
    CHECK(threw, "inverted joint limits must be rejected");
}

static double brute_polygon_sdf(const Polygon2& poly, double x, double y) {
    double best = 1e300;
    bool in = false;
    for (size_t i = 0, n = poly.size(); i < n; ++i) {
        auto a = poly[i], b = poly[(i + 1) % n];
        double ex = b[0] - a[0], ey = b[1] - a[1];
        double h = std::clamp(((x - a[0]) * ex + (y - a[1]) * ey) / (ex * ex + ey * ey), 0.0, 1.0);
        best = std::min(best, std::hypot(x - a[0] - h * ex, y - a[1] - h * ey));
        if ((a[1] > y) != (b[1] > y) && x < a[0] + (y - a[1]) * ex / ey) in = !in;  // ray crossing parity
    }
    return in ? -best : best;
}

TEST(polygon_sdf_is_exact_for_concave_profiles) {
    // an L-shaped (concave) profile, given clockwise to exercise reorientation
    Polygon2 L = tessellate_profile({{0, 0}, {0, 30}, {10, 30}, {10, 10}, {40, 10}, {40, 0}});
    Graph g;
    P3 p = coords(g);
    Program f = compile(polygon_sdf(p.x, p.y, L));
    for (int n = 0; n < 3000; ++n) {
        double x = uniform(-10, 50), y = uniform(-10, 40);
        double want = brute_polygon_sdf(L, x, y), got = f({x, y, 0});
        CHECK(std::abs(got - want) < 1e-9, "(%g, %g): %g vs %g", x, y, got, want);
    }
    CHECK(std::abs(polygon_area(L) - (30 * 10 + 30 * 10)) < 1e-9, "L area %g", polygon_area(L));
}

TEST(sketch_fillets_and_validation) {
    Polygon2 rounded = tessellate_profile({{0, 0, 5}, {20, 0, 5}, {20, 20, 5}, {0, 20, 5}});
    double exact = 400 - (4 - std::numbers::pi) * 25;  // square with four r = 5 corners
    CHECK(std::abs(polygon_area(rounded) / exact - 1) < 2e-3, "filleted area %g vs %g", polygon_area(rounded), exact);
    bool threw = false;
    try { tessellate_profile({{0, 0}, {10, 10}, {10, 0}, {0, 10}}); } catch (const std::exception&) { threw = true; }
    CHECK(threw, "a bow-tie profile must be rejected as self-intersecting");
    threw = false;
    try { tessellate_profile({{0, 0}, {10, 0}}); } catch (const std::exception&) { threw = true; }
    CHECK(threw, "two points are not a profile");
    // a point inserted exactly on an edge must not break the field far from the part
    Graph g;
    P3 p = coords(g);
    Program f = compile(extrude_profile(p, tessellate_profile({{0, 0}, {20, 0}, {40, 0}, {40, 25, 5}, {0, 25}}), 10, false));  // the case found in the app
    // every brick of the engine's 4 m bounds probe that is far from the part must cull
    const double R = 2000, step = 2 * R / 64;
    int stuck = 0;
    for (int i = 0; i < 64; ++i)
        for (int j = 0; j < 64; ++j)
            for (int k = 0; k < 64; ++k) {
                double x = -R + i * step, y = -R + j * step, z = -R + k * step;
                if (std::max({std::abs(x), std::abs(y), std::abs(z)}) < 300) continue;
                if (f.bounds({x, x + step}, {y, y + step}, {z, z + step}).lo <= 0) stuck++;
            }
    CHECK(stuck == 0, "%d far bricks cannot be proven empty (fillet + collinear point profile)", stuck);
    CHECK(f({20, 10, 5}) < 0 && f({60, 10, 5}) > 0, "inside / outside after inserting a collinear point");
}

TEST(extrude_and_revolve_volumes_match_closed_form) {
    Graph g;
    P3 p = coords(g);
    Polygon2 L = tessellate_profile({{0, 0}, {40, 0}, {40, 10}, {10, 10}, {10, 30}, {0, 30}});
    Program ext = compile(extrude_profile(p, L, 15, false));
    Mesh m = dual_contour(ext, {{-1, -1, -1}, {41, 31, 16}, 0.25});
    double v = mass_properties(m).volume_mm3;
    CHECK(topology(m).watertight(), "extrusion mesh closed");
    CHECK(std::abs(v / (600 * 15) - 1) < 1e-3, "extruded L volume %g vs %g", v, 600.0 * 15);
    Program rev = compile(revolve_profile(p, tessellate_profile({{0, 0}, {20, 0}, {20, 30}, {0, 30}}), 360));
    Mesh c = dual_contour(rev, {{-21, -1, -21}, {21, 31, 21}, 0.25});
    double vc = mass_properties(c).volume_mm3, want = std::numbers::pi * 400 * 30;
    CHECK(std::abs(vc / want - 1) < 2e-3, "revolved cylinder volume %g vs %g", vc, want);
    Program half = compile(revolve_profile(p, tessellate_profile({{0, 0}, {20, 0}, {20, 30}, {0, 30}}), 180));
    double vh = mass_properties(dual_contour(half, {{-21, -1, -21}, {21, 31, 21}, 0.25})).volume_mm3;
    CHECK(std::abs(vh / (want / 2) - 1) < 3e-3, "half revolve volume %g vs %g", vh, want / 2);
    // GPU bytecode (incl. copysign) agrees with the CPU program
    BrickGrid grid = build_bricks(ext, {-5, -5, -5}, {45, 45, 45}, 8);
    size_t surface = 0;
    for (auto k : grid.kind) surface += k == 2;
    CHECK(surface > 0 && grid.max_registers < 32, "bricks %zu, registers %zu", surface, grid.max_registers);
}

TEST(imported_mesh_bakes_to_an_accurate_field) {
    // export a known solid with the kernel's own mesher, re-import it as a sampled grid
    Graph g0;
    P3 p0 = coords(g0);
    Program truth = compile(subtract(box(p0, {40, 20, 10}), cylinder_z(p0, 5, 30)));
    Mesh m = dual_contour(truth, {{-20.1, -10.1, -5.1}, {20.1, 10.1, 5.1}, 0.2});
    std::string path = (std::filesystem::temp_directory_path() / "dk_import_test.obj").string();
    CHECK(write_obj(m, path), "write obj");
    SdfGrid grid = bake_sdf(load_mesh(path), 160);
    int id = register_grid(std::make_shared<const SdfGrid>(grid));
    Graph g;
    P3 p = coords(g);
    Program f = compile(sample_grid(id, p.x, p.y, p.z));
    double worst = 0;
    for (int n = 0; n < 4000; ++n) {
        Vec3 q{uniform(-20, 20), uniform(-10.5, 10.5), uniform(-5.5, 5.5)};  // inside the grid
        if (std::abs(truth(q)) > 1.5) continue;  // near the surface, where the reference is exact
        worst = std::max(worst, std::abs(f(q) - truth(q)));
    }
    CHECK(worst < 0.75 * grid.h, "imported field error %g mm (cell %g mm)", worst, grid.h);
    double exact = 40 * 20 * 10 - std::numbers::pi * 25 * 10;
    double vol = mass_properties(dual_contour(f, {{-21, -11, -6}, {21, 11, 6}, 0.2})).volume_mm3;
    CHECK(std::abs(vol / exact - 1) < 0.01, "imported volume %g vs %g", vol, exact);
    // interval bound is conservative and the program composes with transforms
    Interval b = f.bounds({1, 3}, {-1, 1}, {-1, 1});
    for (int n = 0; n < 200; ++n) {
        double v = f({uniform(1, 3), uniform(-1, 1), uniform(-1, 1)});
        CHECK(b.lo <= v && v <= b.hi, "grid value %g outside interval [%g, %g]", v, b.lo, b.hi);
    }
    // outside the grid the field must never exceed the true distance (no overshoot)
    for (int n = 0; n < 500; ++n) {
        Vec3 q{uniform(-60, 60), uniform(-40, 40), uniform(-30, 30)};
        Vec3 c{std::clamp(q.x, -20.0, 20.0), std::clamp(q.y, -10.0, 10.0), std::clamp(q.z, -5.0, 5.0)};
        double lower_true = length(q - c);  // the part is inside its 40 x 20 x 10 box
        if (lower_true > 2) CHECK(f(q) <= lower_true + std::sqrt(40 * 40 + 20 * 20 + 10 * 10.0), "grid overshoots far away");
        if (std::abs(q.x) > 22 || std::abs(q.y) > 12 || std::abs(q.z) > 7) {
            // nearest surface point is at least as far as the bounding box: f must not exceed true distance
            // (true distance <= distance to the far corner region); check the stronger property f <= dist to part
            double true_d = truth(q);  // exact outside the box faces' extensions
            CHECK(f(q) <= true_d + grid.h, "outside sample %g exceeds true distance %g", f(q), true_d);
        }
    }
    Program moved = compile(sample_grid(id, p.x - 100, p.y, p.z));
    CHECK(std::abs(moved({107, 0, 0}) - f({7, 0, 0})) < 1e-12, "translated sampling");
}

TEST(thin_walls_stay_closed_and_manifold) {
    // sheet gyroid with walls ~2 cells thick: many cells see two separate sheets
    Graph g;
    P3 p = coords(g);
    Program f = compile(intersect(gyroid(p, 6, 0.5), box(p, {20, 20, 20})));
    MeshStats st;
    Mesh m = dual_contour(f, {{-10, -10, -10}, {10, 10, 10}, 0.25}, &st);
    Topology t = topology(m);
    CHECK(st.missing_quads == 0, "missing quads %zu", st.missing_quads);
    CHECK(t.boundary_edges == 0, "boundary edges %zu", t.boundary_edges);
    CHECK(t.nonmanifold_edges == 0, "nonmanifold edges %zu of %zu tris", t.nonmanifold_edges, m.triangles.size());
}

TEST(mesher_reports_surface_cut_by_the_region) {
    Graph g;
    Program f = compile(sphere(coords(g), 10));
    MeshStats st;
    // the grid grows from lo, so a part below lo is cut off
    dual_contour(f, {{-10, -10, -4}, {10, 10, 10}, 0.5}, &st);
    CHECK(st.clipped_cells > 0, "a cut-off part must be reported");
}

TEST(complex_part_meshes_watertight) {
    Graph g;
    Program f = compile(test_part(g));
    MeshStats st;
    Mesh m = dual_contour(f, {{-32, -16, -4}, {32, 16, 17}, 0.25}, &st);
    Topology t = topology(m);
    CHECK(st.missing_quads == 0, "missing quads %zu", st.missing_quads);
    CHECK(t.boundary_edges == 0, "boundary edges %zu", t.boundary_edges);
    // DC can join two sheets through one cell vertex (thin lattice walls); report, keep rare
    CHECK(t.nonmanifold_edges * 1000 < m.triangles.size(), "nonmanifold %zu of %zu tris", t.nonmanifold_edges, m.triangles.size());
    std::printf("  part: %zu tris, %zu leaves, program %zu -> avg %.1f instrs, %.2fs\n", m.triangles.size(), st.leaves, st.full_size, st.avg_pruned_size, st.seconds);
}

TEST(interval_atan2_floor_bound_samples) {
    std::mt19937 rng(11);
    std::uniform_real_distribution<double> u(-5, 5), w(0, 3);
    for (int n = 0; n < 4000; ++n) {
        double x0 = u(rng), y0 = u(rng), x1 = x0 + w(rng), y1 = y0 + w(rng);
        Interval a = atan2(Interval{y0, y1}, Interval{x0, x1}), f = floor(Interval{x0, x1});
        for (int k = 0; k < 6; ++k) {
            double x = x0 + (x1 - x0) * k / 5.0, y = y1 - (y1 - y0) * k / 5.0;
            double t = std::atan2(y, x);
            CHECK(a.lo - 1e-12 <= t && t <= a.hi + 1e-12, "atan2 [%g,%g]x[%g,%g]", x0, x1, y0, y1);
            CHECK(f.lo <= std::floor(x) && std::floor(x) <= f.hi, "floor");
        }
    }
    Dual3 d = atan2(Dual3(2, {0, 1, 0}), Dual3(1, {1, 0, 0}));  // d/dx atan2(y, x) = -y / r2, d/dy = x / r2
    CHECK(std::abs(d.d.x + 0.4) < 1e-12 && std::abs(d.d.y - 0.2) < 1e-12, "atan2 grad %g %g", d.d.x, d.d.y);
}

// |f(a) - f(b)| <= |a - b| for random nearby pairs: the field never overshoots (safe to trace)
static double worst_lipschitz(const Program& f, Vec3 lo, Vec3 hi, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> ux(lo.x, hi.x), uy(lo.y, hi.y), uz(lo.z, hi.z), e(-0.3, 0.3);
    double worst = 0;
    for (int n = 0; n < 20000; ++n) {
        Vec3 a{ux(rng), uy(rng), uz(rng)}, b{a.x + e(rng), a.y + e(rng), a.z + e(rng)};
        double dl = std::sqrt(dot(a - b, a - b));
        if (dl > 1e-6) worst = std::max(worst, std::abs(f(a) - f(b)) / dl);
    }
    return worst;
}

TEST(spur_gear_has_the_right_size_and_meshes) {
    Graph g;
    P3 p = coords(g);
    const double m = 2, z = 20, rp = m * z / 2;
    Program gear = compile(spur_gear(p, m, int(z), 20, 10, 8));
    Mesh mesh = dual_contour(gear, {{-23, -23, -6}, {23, 23, 6}, 0.2});
    CHECK(topology(mesh).watertight(), "gear mesh closed");
    // a standard gear's area is close to the pitch disc (teeth and gaps balance), minus the bore
    double v = mass_properties(mesh).volume_mm3, want = (std::numbers::pi * rp * rp - std::numbers::pi * 16) * 10;
    CHECK(std::abs(v / want - 1) < 0.05, "gear volume %g vs ~%g", v, want);
    CHECK(gear({rp + m + 0.5, 0, 0}) > 0 && gear({rp + m - 0.3, 0, 0}) < 0, "tooth tip on +x at the addendum");
    CHECK(gear({0, 0, 0}) > 0, "bore is open");
    double L = worst_lipschitz(gear, {-23, -23, -6}, {23, 23, 6}, 4);
    CHECK(L < 1.05, "gear field Lipschitz %g", L);
    BrickGrid grid = build_bricks(gear, {-23, -23, -6}, {23, 23, 6}, 8);
    CHECK(grid.max_registers < 48, "gear registers %zu", grid.max_registers);
}

TEST(threaded_rod_sits_between_root_and_major) {
    Graph g;
    P3 p = coords(g);
    Program rod = compile(threaded_rod(p, 8, 1.25, 20));
    Mesh mesh = dual_contour(rod, {{-5, -5, -11}, {5, 5, 11}, 0.08});
    CHECK(topology(mesh).watertight(), "thread mesh closed");
    double v = mass_properties(mesh).volume_mm3, h = 0.61343 * 1.25;
    double vmin = std::numbers::pi * (4 - h) * (4 - h) * 20, vmax = std::numbers::pi * 16 * 20;
    CHECK(v > vmin && v < vmax, "thread volume %g in (%g, %g)", v, vmin, vmax);
    double L = worst_lipschitz(rod, {-5, -5, -11}, {5, 5, 11}, 5);
    CHECK(L < 1.0, "thread field Lipschitz %g", L);
}

TEST(hole_wizard_removes_the_drilled_volume) {
    Graph g;
    P3 p = coords(g);
    Expr block = box(p, {40, 40, 20});
    HoleSpec h = metric_hole("M6");
    P3 top = translate(p, {0, 0, 10});
    Program simple = compile(subtract(block, hole_cutter(top, h, 8, "simple")));
    double v = mass_properties(dual_contour(simple, {{-21, -21, -11}, {21, 21, 11}, 0.2})).volume_mm3;
    double want = 40 * 40 * 20 - std::numbers::pi * 3.3 * 3.3 * 8;
    CHECK(std::abs(v / want - 1) < 1e-3, "M6 hole volume %g vs %g", v, want);
    Program cb = compile(subtract(block, hole_cutter(top, h, 8, "counterbore")));
    double vcb = mass_properties(dual_contour(cb, {{-21, -21, -11}, {21, 21, 11}, 0.2})).volume_mm3;
    double want_cb = want - std::numbers::pi * (5.5 * 5.5 - 3.3 * 3.3) * 6.5;
    CHECK(std::abs(vcb / want_cb - 1) < 2e-3, "counterbore volume %g vs %g", vcb, want_cb);
    Program cs = compile(subtract(block, hole_cutter(top, h, 8, "countersink")));
    CHECK(cs({5.0, 0, 9.8}) > 0 && cs({5.0, 0, 4}) < 0, "countersink cone opens at the top only");
}

TEST(surface_textures_stay_distance_bounds) {
    Graph g;
    P3 p = coords(g);
    for (const char* pat : {"knurl", "bumps", "ribs"}) {
        Program f = compile(textured(cylinder_z(p, 10, 30), p, pat, 3, 0.6));
        double L = worst_lipschitz(f, {-12, -12, -16}, {12, 12, 16}, 6);
        CHECK(L < 1.0, "%s Lipschitz %g", pat, L);
        CHECK(topology(dual_contour(f, {{-11, -11, -16}, {11, 11, 16}, 0.25})).watertight(), "%s mesh closed", pat);
    }
}

TEST(loft_matches_a_frustum_and_stays_a_bound) {
    Graph g;
    P3 p = coords(g);
    // square 40 -> square 16 over 30 mm: a square frustum, V = h/3 (A1 + A2 + sqrt(A1 A2))
    Polygon2 a = tessellate_profile({{-20, -20}, {20, -20}, {20, 20}, {-20, 20}});
    Polygon2 b = tessellate_profile({{-8, -8}, {8, -8}, {8, 8}, {-8, 8}});
    Program loft = compile(loft_profiles(p, a, b, 30));
    Mesh m = dual_contour(loft, {{-21.037, -21.053, -1.071}, {21.1, 21.1, 31.1}, 0.2});
    double v = mass_properties(m).volume_mm3, want = 30.0 / 3 * (1600 + 256 + 640);
    CHECK(topology(m).watertight(), "loft mesh closed");
    // blending distance fields rounds the corners mid-way (slightly less than a ruled frustum)
    CHECK(v < want && v > want * 0.98, "loft volume %g vs frustum %g", v, want);
    CHECK(loft({19.9, 0, 0.01}) < 0 && loft({20.1, 0, 0.01}) > 0, "bottom profile exact");
    CHECK(loft({7.9, 0, 29.99}) < 0 && loft({8.1, 0, 29.99}) > 0, "top profile exact");
    CHECK(loft({0, 13.9, 15}) < 0 && loft({0, 14.1, 15}) > 0, "mid-height edge at the average");
    CHECK(worst_lipschitz(loft, {-25, -25, -5}, {25, 25, 35}, 9) <= 1.0 + 1e-9, "loft Lipschitz");
}

TEST(naca_wing_matches_section_area_and_taper_volume) {
    Polygon2 sec = naca4_polygon("0012", 100);
    double area = polygon_area(sec);
    // a NACA 00xx section has area 0.6851 * t * c^2
    CHECK(area > 0 && std::abs(area / (0.6851 * 0.12 * 1e4) - 1) < 0.02, "0012 area %g", area);
    double top = 0;
    for (auto& q : sec) top = std::max(top, q[1]);
    CHECK(std::abs(top - 6.0) < 0.1, "0012 half thickness %g of 6", top);
    Polygon2 cam = naca4_polygon("2412", 100);
    double mid = 0;
    for (auto& q : cam) mid += q[1] / cam.size();
    CHECK(mid > 0.5, "2412 is cambered upwards (mean y %g)", mid);

    Graph g;
    P3 p = coords(g);
    const double root = 120, taper = 0.5, span = 240;
    Program w = compile(wing(p, "0015", root, taper, span, 20, 5));
    Mesh m = dual_contour(w, {{-5.03, -20.07, -2.05}, {215, 45, 243}, 0.5});
    CHECK(topology(m).watertight(), "wing mesh closed");
    double a_root = polygon_area(naca4_polygon("0015", root));
    double want = a_root * span * (1 + taper + taper * taper) / 3;
    double v = mass_properties(m).volume_mm3;
    CHECK(std::abs(v / want - 1) < 0.02, "wing volume %g vs %g", v, want);
    // leading edge follows the sweep: at the tip it sits at span * tan(20 deg)
    double le = span * std::tan(20 * std::numbers::pi / 180), up = span * std::tan(5 * std::numbers::pi / 180);
    CHECK(w({le + 5, up, span - 1}) < 0 && w({le - 3, up, span - 1}) > 0, "swept tip leading edge");
    CHECK(worst_lipschitz(w, {-10, -25, -5}, {220, 50, 245}, 21) <= 1.0 + 1e-9, "wing field is a bound");
    // swept-wing sections are deep expressions: the GPU program must stay small anyway
    BrickGrid grid = build_bricks(w, {-10, -25, -5}, {220, 205, 245}, 24);
    CHECK(grid.max_registers <= 48, "wing GPU registers %zu", grid.max_registers);
    Program nose = compile(nose_cone(p, "ogive", 50, 130));
    BrickGrid gn = build_bricks(nose, {-26, -26, -1}, {26, 26, 131}, 24);
    CHECK(gn.max_registers <= 24, "nose cone GPU registers %zu", gn.max_registers);
    // mirrored halves share hundreds of constants: they must not all stay live (re-emitted per use)
    P3 mirror{p.x, p.y, -p.z};
    Program both = compile(unite(wing(p, "2412", 180, 0.5, 450, 20, 4), wing(mirror, "2412", 180, 0.5, 450, 20, 4)));
    BrickGrid whole = build_bricks(both, {-500, -500, -500}, {500, 500, 500}, 1);
    CHECK(whole.max_registers <= 32, "mirrored wing GPU registers %zu (unpruned)", whole.max_registers);
}

TEST(nose_cones_match_closed_form_volumes) {
    Graph g;
    P3 p = coords(g);
    const double R = 20, L = 60;
    Program cone = compile(nose_cone(p, "conical", 2 * R, L));
    double vc = mass_properties(dual_contour(cone, {{-21.03, -21.07, -1.05}, {21, 21, 61}, 0.25})).volume_mm3;
    CHECK(std::abs(vc / (std::numbers::pi * R * R * L / 3) - 1) < 0.01, "cone volume %g", vc);
    Program ell = compile(nose_cone(p, "elliptical", 2 * R, L));
    double ve = mass_properties(dual_contour(ell, {{-21.03, -21.07, -1.05}, {21, 21, 61}, 0.25})).volume_mm3;
    CHECK(std::abs(ve / (2 * std::numbers::pi * R * R * L / 3) - 1) < 0.015, "half ellipsoid volume %g", ve);
    Program og = compile(nose_cone(p, "ogive", 2 * R, L));
    CHECK(og({0, 0, L - 0.5}) < 0 && og({0, 0, L + 0.5}) > 0 && og({R - 0.5, 0, 0.5}) < 0 && og({R + 0.5, 0, 0.5}) > 0, "ogive spans base radius to tip");
}

TEST(coil_spring_is_conservative_and_has_the_wire_volume) {
    Graph g;
    P3 p = coords(g);
    const double D = 20, d = 2.5, len = 36, turns = 6, pitch = len / turns;
    Program f = compile(coil_spring(p, D, d, len, turns));
    // true distance to the (infinite) helix tube by brute force
    auto truth = [&](Vec3 q) {
        double best = 1e300;
        for (int i = -4000; i <= 4000; ++i) {
            double th = i * 0.005, z = th * pitch / (2 * std::numbers::pi);
            best = std::min(best, std::sqrt(square(q.x - D / 2 * std::cos(th)) + square(q.y - D / 2 * std::sin(th)) + square(q.z - z)));
        }
        return best - d / 2;
    };
    std::mt19937 rng(8);
    std::uniform_real_distribution<double> ux(-14, 14), uz(-12, 12);
    double worst = -1e300;
    for (int n = 0; n < 1500; ++n) {
        Vec3 q{ux(rng), ux(rng), uz(rng)};
        double fv = f(q), tv = truth(q);
        CHECK((fv < 0) == (tv < 0) || std::abs(tv) < 0.05, "spring sign at (%g, %g, %g)", q.x, q.y, q.z);
        worst = std::max(worst, std::abs(fv) - std::abs(tv));  // never further from the surface than the truth
    }
    CHECK(worst < 0.01, "spring field overestimates distance by %g", worst);
    Mesh m = dual_contour(f, {{-12.03, -12.07, -18.55}, {12, 12, 18.6}, 0.125});
    CHECK(topology(m).watertight(), "spring mesh closed");
    double wire = turns * std::hypot(std::numbers::pi * D, pitch);
    double v = mass_properties(m).volume_mm3, want = std::numbers::pi * d * d / 4 * wire;
    CHECK(std::abs(v / want - 1) < 0.04, "spring volume %g vs %g", v, want);
}

TEST(standard_parts_have_catalogue_dimensions) {
    Graph g;
    P3 p = coords(g);
    BearingSize b = bearing_size("6204");
    CHECK(b.bore == 20 && b.od == 47 && b.width == 14, "6204 is 20 x 47 x 14");
    Program bearing = compile(ball_bearing(p, b));
    Mesh mb = dual_contour(bearing, {{-24.03, -24.07, -7.55}, {24, 24, 7.6}, 0.2});
    CHECK(topology(mb).watertight(), "bearing mesh closed");
    CHECK(bearing({10.5, 0, 0}) < 0 && bearing({9.5, 0, 0}) > 0 && bearing({23, 0, 0}) < 0 && bearing({24, 0, 0}) > 0, "bearing bore and outer diameter");

    BeamSize ipe = ipe_size("IPE 100");
    Program ib = compile(beam(p, "I-beam", ipe, 200));
    double vi = mass_properties(dual_contour(ib, {{-28.03, -51.07, -101.05}, {28, 51, 101}, 0.25})).volume_mm3;
    double area = 2 * 55 * 5.7 + (100 - 2 * 5.7) * 4.1;
    CHECK(std::abs(vi / (area * 200) - 1) < 5e-3, "IPE 100 volume %g vs %g", vi, area * 200);
    for (const char* kind : {"C-channel", "L-angle", "T-bar"})
        CHECK(polygon_area(beam_polygon(kind, {60, 40, 4, 5})) > 0, "%s is counter-clockwise", kind);
    Program tube = compile(beam(p, "Rect tube", {40, 30, 3, 3}, 100));
    double vt = mass_properties(dual_contour(tube, {{-16.03, -21.07, -51.05}, {16, 21, 51}, 0.25})).volume_mm3;
    CHECK(std::abs(vt / ((30 * 40 - 24 * 34) * 100.0) - 1) < 5e-3, "tube volume %g", vt);
}

TEST(brake_disc_and_wheel_are_solid_parts) {
    Graph g;
    P3 p = coords(g);
    Program disc = compile(brake_disc(p, 280, 22, 65, 5, 112, 24));
    Mesh md = dual_contour(disc, {{-141.03, -141.07, -12.05}, {141, 141, 12}, 0.5});
    CHECK(topology(md).watertight(), "disc mesh closed");
    double pi = std::numbers::pi;
    double want = (pi * (140.0 * 140 - 32.5 * 32.5) - 5 * pi * 6.5 * 6.5 - 48 * pi * 9) * 22;
    double v = mass_properties(md).volume_mm3;
    CHECK(std::abs(v / want - 1) < 0.01, "disc volume %g vs %g", v, want);
    CHECK(disc({56, 0, 0}) > 0 && disc({56 * std::cos(0.6), 56 * std::sin(0.6), 0}) < 0, "bolt hole on the pitch circle");

    Program wh = compile(wheel(p, 381, 165, 5, 5, 112, 57.1));
    Mesh mw = dual_contour(wh, {{-204.03, -204.07, -84.05}, {204, 204, 84}, 1.0});
    CHECK(topology(mw).watertight(), "wheel mesh closed");
    CHECK(wh({0, 0, 60}) > 0, "centre bore is open");
    CHECK(wh({189, 0, 0}) < 0 && wh({150, 30, 0}) > 0, "barrel is a shell");
    CHECK(worst_lipschitz(wh, {-200, -200, -85}, {200, 200, 85}, 31) <= 1.0 + 1e-9, "wheel field is a bound");
}

TEST(feature_at_picks_the_feature_that_made_the_surface) {
    json doc = {{"version", 1}, {"root", "drilled"}, {"features", json::array({
        {{"id", "plate"}, {"type", "box"}, {"params", {{"size", {60, 40, 10}}}}},
        {{"id", "boss"}, {"type", "cylinder"}, {"params", {{"radius", 8}, {"height", 20}, {"center", {20, 0, 10}}}}},
        {{"id", "body"}, {"type", "union"}, {"inputs", {"plate", "boss"}}},
        {{"id", "drilled"}, {"type", "hole"}, {"inputs", {"body"}}, {"params", {{"size", "M6"}, {"depth", 12}, {"center", {-15, 0, 5}}}}},
    })}};
    CHECK(feature_at(doc, {0, 0, 5}, 0.05) == "plate", "top face is the plate");
    CHECK(feature_at(doc, {28, 0, 15}, 0.05) == "boss", "boss side is the boss");
    CHECK(feature_at(doc, {-15 + 3.3, 0, 0}, 0.05) == "drilled", "hole wall is the hole");
    CHECK(feature_at(doc, {100, 100, 100}, 0.05).empty(), "empty space picks nothing");
}

TEST(inverse_kinematics_reaches_points_and_stops_at_the_boundary) {
    for (const char* key : {"ur5", "puma560"}) {
        const Robot& r = robot_preset(key);
        std::mt19937 rng(17);
        std::uniform_real_distribution<double> u(0, 1);
        int reached = 0;
        for (int n = 0; n < 40; ++n) {
            // a target the arm can reach: forward kinematics of a random in-limits pose
            std::vector<double> q(r.joints.size());
            for (size_t i = 0; i < q.size(); ++i) q[i] = r.joints[i].lo + (r.joints[i].hi - r.joints[i].lo) * u(rng);
            Vec3 target = forward_kinematics(r, q).back().o;
            std::vector<double> start(r.joints.size(), 0.3);
            IkResult ik = inverse_kinematics(r, start, target, 0.5);
            Vec3 got = forward_kinematics(r, ik.q).back().o;
            for (size_t i = 0; i < ik.q.size(); ++i) CHECK(ik.q[i] >= r.joints[i].lo - 1e-9 && ik.q[i] <= r.joints[i].hi + 1e-9, "%s joint %zu within limits", key, i);
            CHECK(std::abs(length(got - target) - ik.error_mm) < 1e-6, "%s reported error matches", key);
            reached += ik.reached;
        }
        CHECK(reached >= 38, "%s reached %d of 40 random reachable targets", key, reached);
        IkResult far = inverse_kinematics(r, std::vector<double>(r.joints.size(), 0.3), {5 * r.reach(), 0, 0}, 0.5);
        CHECK(!far.reached && far.error_mm > 4 * r.reach() && far.error_mm < 5 * r.reach(), "%s stops near the boundary for an unreachable target (%g)", key, far.error_mm);
    }
}

TEST(fea_tension_bar_is_exact) {
    // uniform bar in tension: sigma = F / A everywhere away from the ends, delta = F L / (E A)
    Graph g;
    P3 p = coords(g);
    Program bar = compile(box(translate(p, {50, 0, 0}), {100, 10, 10}));
    FeaGrid grid = voxelize(bar, {0, -5, -5}, {100, 5, 5}, 1.0);
    CHECK(grid.solid_count() == 10000, "voxels %zu", grid.solid_count());
    // clamp the x = 0 face, pull the x = 100 face (spheres far away approximate the planes)
    FeaResult r = solve_elastic(grid, 200000, 0.0, {{{-1000, 0, 0}, 1000.03}}, {{{1100, 0, 0}, 1000.03, {1000, 0, 0}}});
    CHECK(r.converged, "converged in %d iterations", r.iterations);
    double want = 1000.0 * 100 / (200000.0 * 100);  // 0.005 mm
    double tip = 0;
    for (int k = 0; k <= 10; ++k)
        for (int j = 0; j <= 10; ++j) tip += r.displacement[100 + 101 * (j + 11 * k)].x / 121;
    CHECK(std::abs(tip / want - 1) < 0.02, "tension stretch %g vs %g", tip, want);
    double mid = r.von_mises[50 + 100 * (5 + 10 * 5)];
    CHECK(std::abs(mid / 10.0 - 1) < 0.02, "tension stress %g vs 10 MPa", mid);
}

TEST(fea_cantilever_matches_beam_theory) {
    Graph g;
    P3 p = coords(g);
    Program beam = compile(box(translate(p, {50, 0, 0}), {100, 10, 10}));
    FeaGrid grid = voxelize(beam, {0, -5, -5}, {100, 5, 5}, 1.0);
    const double E = 200000, P = 100, L = 100, I = 10.0 * 1000 / 12;
    FeaResult r = solve_elastic(grid, E, 0.3, {{{-1000, 0, 0}, 1000.03}}, {{{100, 0, 0}, 7.5, {0, 0, -P}}});
    CHECK(r.converged, "converged in %d iterations", r.iterations);
    double tip = 0;
    for (int j = 0; j <= 10; ++j) tip += -r.displacement[100 + 101 * (j + 11 * 5)].z / 11;
    double want = P * L * L * L / (3 * E * I);  // 0.2 mm (shear adds under 1 %)
    CHECK(std::abs(tip / want - 1) < 0.15, "cantilever tip %g vs %g mm", tip, want);
    // bending stress at mid-span, top fibre element (z = 4.5): M c / I
    double sigma = r.von_mises[50 + 100 * (5 + 10 * 9)], want_s = P * 50 * 4.5 / I;
    CHECK(std::abs(sigma / want_s - 1) < 0.15, "mid-span stress %g vs %g MPa", sigma, want_s);
    CHECK(r.max_vm > want_s, "peak stress at the root exceeds mid-span (%g)", r.max_vm);
    std::printf("  cantilever: %d iterations, %.2fs, tip %.4f mm (theory %.4f)\n", r.iterations, r.seconds, tip, want);
    // no fixture on the part is reported, not solved
    bool threw = false;
    try { solve_elastic(grid, E, 0.3, {{{500, 500, 500}, 1}}, {{{100, 0, 0}, 7.5, {0, 0, -P}}}); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw, "missing fixture is an error");
}

int main() {
    for (auto& t : registry()) {
        int before = failures;
        t.fn();
        std::printf("%s %s\n", failures == before ? "[ok]  " : "[FAIL]", t.name);
    }
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
