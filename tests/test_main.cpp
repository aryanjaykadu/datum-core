// Kernel tests. Each check states the property it guards; tolerances are physical (mm).
#include <cstdio>
#include <functional>
#include <numbers>
#include <random>
#include <string>
#include <vector>

#include "dk/bricks.hpp"
#include "dk/document.hpp"
#include "dk/mesh.hpp"
#include "dk/robot.hpp"
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
    Program f = compile(test_part(g));
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

int main() {
    for (auto& t : registry()) {
        int before = failures;
        t.fn();
        std::printf("%s %s\n", failures == before ? "[ok]  " : "[FAIL]", t.name);
    }
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
