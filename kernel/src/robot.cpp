#include "dk/robot.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <unordered_map>

namespace dk {

static constexpr double PI = std::numbers::pi;
static double deg(double d) { return d * PI / 180.0; }

Joint dh_joint(double d, double a, double alpha, double lo, double hi) {
    Joint j;
    j.axis = {0, 0, 1};
    // T = Rz(q) Tz(d) Tx(a) Rx(alpha)
    if (d != 0) j.post.push_back({false, {0, 0, d}, 0});
    if (a != 0) j.post.push_back({false, {a, 0, 0}, 0});
    if (alpha != 0) j.post.push_back({true, {1, 0, 0}, alpha});
    j.lo = lo;
    j.hi = hi;
    return j;
}

double Robot::reach() const {
    double r = 0;
    for (auto& j : joints) {
        for (auto& s : j.pre) r += s.rotate ? 0 : length(s.v);
        for (auto& s : j.post) r += s.rotate ? 0 : length(s.v);
    }
    return r;
}

const std::vector<Robot>& robot_presets() {
    // Published DH parameters: UR5 (Universal Robots), PUMA 560 (P. Corke, Robotics Toolbox).
    static const std::vector<Robot> presets = {
        {"ur5", "6R collaborative arm (UR5 DH)",
         {dh_joint(89.159, 0, PI / 2, -2 * PI, 2 * PI), dh_joint(0, -425, 0, -2 * PI, 2 * PI), dh_joint(0, -392.25, 0, -PI, PI),
          dh_joint(109.15, 0, PI / 2, -2 * PI, 2 * PI), dh_joint(94.65, 0, -PI / 2, -2 * PI, 2 * PI), dh_joint(82.3, 0, 0, -2 * PI, 2 * PI)},
         42, 0},
        {"puma560", "6R industrial arm (PUMA 560 DH)",
         {dh_joint(0, 0, PI / 2, deg(-160), deg(160)), dh_joint(0, 431.8, 0, deg(-225), deg(45)), dh_joint(150.05, 20.3, -PI / 2, deg(-45), deg(225)),
          dh_joint(431.8, 0, PI / 2, deg(-110), deg(170)), dh_joint(0, 0, -PI / 2, deg(-100), deg(100)), dh_joint(0, 0, 0, deg(-266), deg(266))},
         45, 672},  // frame 0 is the shoulder, 0.672 m above the floor mount (Corke)
        {"planar3r", "3R planar arm (300/250/150 mm)",
         {dh_joint(0, 300, 0, -PI, PI), dh_joint(0, 250, 0, deg(-150), deg(150)), dh_joint(0, 150, 0, deg(-150), deg(150))},
         25, 0},
    };
    return presets;
}

const Robot& robot_preset(const std::string& key) {
    for (auto& r : robot_presets())
        if (r.key == key) return r;
    return robot_presets()[0];
}

namespace {

// rotate v about unit axis u by angle (Rodrigues)
Vec3 rotate_vec(Vec3 u, double angle, Vec3 v) {
    double c = std::cos(angle), s = std::sin(angle);
    return v * c + cross(u, v) * s + u * (dot(u, v) * (1 - c));
}

struct Walker {
    Frame f{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    Vec3 world(Vec3 local) const { return f.x * local.x + f.y * local.y + f.z * local.z; }
    void step(const Step& s) {
        if (!s.rotate) {
            f.o = f.o + world(s.v);
            return;
        }
        Vec3 u = normalize(s.v);
        f = {f.o, world(rotate_vec(u, s.angle, {1, 0, 0})), world(rotate_vec(u, s.angle, {0, 1, 0})), world(rotate_vec(u, s.angle, {0, 0, 1}))};
    }
};

template <class OnPoint>
std::vector<Frame> walk(const Robot& r, const std::vector<double>& q, OnPoint on_point) {
    std::vector<Frame> frames;
    frames.reserve(r.joints.size() + 1);
    Walker w;
    on_point(w.f.o, 0);
    for (size_t i = 0; i < r.joints.size(); ++i) {
        const Joint& j = r.joints[i];
        for (auto& s : j.pre) {
            w.step(s);
            if (!s.rotate) on_point(w.f.o, int(i));
        }
        Vec3 axis = normalize(j.axis);
        Frame at = w.f;
        at.z = normalize(w.world(axis));
        frames.push_back(at);
        w.step({true, axis, i < q.size() ? q[i] : 0.0});
        for (auto& s : j.post) {
            w.step(s);
            if (!s.rotate) on_point(w.f.o, int(i + 1));
        }
    }
    frames.push_back(w.f);
    return frames;
}

}  // namespace

std::vector<Frame> forward_kinematics(const Robot& r, const std::vector<double>& q) {
    return walk(r, q, [](Vec3, int) {});
}

std::vector<LinkPoint> chain_polyline(const Robot& r, const std::vector<double>& q) {
    std::vector<LinkPoint> pts;
    walk(r, q, [&](Vec3 p, int link) { pts.push_back({p, link}); });
    return pts;
}

std::vector<double> jacobian(const Robot& r, const std::vector<Frame>& frames, double length_scale) {
    size_t n = r.joints.size();
    std::vector<double> J(6 * n);
    Vec3 pe = frames.back().o;
    for (size_t i = 0; i < n; ++i) {
        Vec3 z = frames[i].z;
        Vec3 v = cross(z, pe - frames[i].o) * (1.0 / length_scale);
        for (int k = 0; k < 3; ++k) {
            J[k * n + i] = v[k];
            J[(k + 3) * n + i] = z[k];
        }
    }
    return J;
}

static IkResult ik_from(const Robot& r, std::vector<double> q, Vec3 target, double tol) {
    const size_t n = r.joints.size();
    q.resize(n, 0.0);
    for (size_t i = 0; i < n; ++i) q[i] = std::clamp(q[i], r.joints[i].lo, r.joints[i].hi);
    IkResult out;
    const double reach = std::max(r.reach(), 1.0);
    double lambda = 0.02 * reach;  // damping (mm): smooth near singularities
    double err = 1e300;
    for (int it = 0; it < 400; ++it) {
        auto frames = forward_kinematics(r, q);
        Vec3 e = target - frames.back().o;
        err = std::sqrt(dot(e, e));
        out.iterations = it;
        if (err < tol) break;
        auto J6 = jacobian(r, frames, 1.0);  // rows 0-2: mm per rad
        // A = J J^T + lambda^2 I (3x3), solve A y = e, dq = J^T y
        double A[3][3] = {};
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b) {
                for (size_t i = 0; i < n; ++i) A[a][b] += J6[a * n + i] * J6[b * n + i];
                if (a == b) A[a][b] += lambda * lambda;
            }
        double det = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
        if (std::abs(det) < 1e-300) break;
        double inv[3][3] = {
            {(A[1][1] * A[2][2] - A[1][2] * A[2][1]) / det, (A[0][2] * A[2][1] - A[0][1] * A[2][2]) / det, (A[0][1] * A[1][2] - A[0][2] * A[1][1]) / det},
            {(A[1][2] * A[2][0] - A[1][0] * A[2][2]) / det, (A[0][0] * A[2][2] - A[0][2] * A[2][0]) / det, (A[0][2] * A[1][0] - A[0][0] * A[1][2]) / det},
            {(A[1][0] * A[2][1] - A[1][1] * A[2][0]) / det, (A[0][1] * A[2][0] - A[0][0] * A[2][1]) / det, (A[0][0] * A[1][1] - A[0][1] * A[1][0]) / det}};
        double y[3] = {};
        for (int a = 0; a < 3; ++a) y[a] = inv[a][0] * e.x + inv[a][1] * e.y + inv[a][2] * e.z;
        std::vector<double> dq(n);
        double biggest = 0;
        for (size_t i = 0; i < n; ++i) {
            dq[i] = J6[0 * n + i] * y[0] + J6[1 * n + i] * y[1] + J6[2 * n + i] * y[2];
            biggest = std::max(biggest, std::abs(dq[i]));
        }
        double scale = biggest > 0.2 ? 0.2 / biggest : 1.0;  // at most ~11 deg per step
        std::vector<double> trial(n);
        for (size_t i = 0; i < n; ++i) trial[i] = std::clamp(q[i] + dq[i] * scale, r.joints[i].lo, r.joints[i].hi);
        Vec3 e2 = target - forward_kinematics(r, trial).back().o;
        if (dot(e2, e2) < err * err) {
            q = trial;
            lambda = std::max(lambda * 0.7, 1e-3 * reach);
        } else {
            lambda = std::min(lambda * 2.5, reach);  // overshoot: damp harder and retry
        }
    }
    out.q = q;
    Vec3 e = target - forward_kinematics(r, q).back().o;  // the pose actually returned
    out.error_mm = std::sqrt(dot(e, e));
    out.reached = out.error_mm < tol;
    return out;
}

IkResult inverse_kinematics(const Robot& r, std::vector<double> q0, Vec3 target, double tol) {
    // from the current pose first (smallest motion), then from a fixed set of spread-out
    // poses: damped least squares can stall in a local minimum near a joint limit
    IkResult best = ik_from(r, q0, target, tol);
    std::mt19937 rng(1234);
    std::uniform_real_distribution<double> u(0.1, 0.9);
    for (int k = 0; k < 12 && !best.reached; ++k) {
        std::vector<double> seed(r.joints.size());
        for (size_t i = 0; i < seed.size(); ++i) seed[i] = r.joints[i].lo + (r.joints[i].hi - r.joints[i].lo) * u(rng);
        IkResult t = ik_from(r, seed, target, tol);
        // a restart means a large jump in pose: take it only if it reaches the target or is clearly
        // closer (an unreachable jog should stop near the current pose, not leap across the workspace)
        if (t.reached || t.error_mm < best.error_mm - std::max(5.0, 0.2 * best.error_mm)) best = t;
    }
    return best;
}

// eigenvalues of a symmetric n x n matrix (cyclic Jacobi), in place
static std::vector<double> sym_eigenvalues(std::vector<double> a, int n) {
    for (int sweep = 0; sweep < 60; ++sweep) {
        double off = 0;
        for (int p = 0; p < n; ++p)
            for (int q = p + 1; q < n; ++q) off += a[p * n + q] * a[p * n + q];
        if (off < 1e-24) break;
        for (int p = 0; p < n; ++p)
            for (int q = p + 1; q < n; ++q) {
                double apq = a[p * n + q];
                if (std::abs(apq) < 1e-300) continue;
                double theta = (a[q * n + q] - a[p * n + p]) / (2 * apq);
                double t = (theta >= 0 ? 1 : -1) / (std::abs(theta) + std::sqrt(theta * theta + 1));
                double c = 1 / std::sqrt(t * t + 1), s = t * c;
                for (int k = 0; k < n; ++k) {
                    double akp = a[k * n + p], akq = a[k * n + q];
                    a[k * n + p] = c * akp - s * akq;
                    a[k * n + q] = s * akp + c * akq;
                }
                for (int k = 0; k < n; ++k) {
                    double apk = a[p * n + k], aqk = a[q * n + k];
                    a[p * n + k] = c * apk - s * aqk;
                    a[q * n + k] = s * apk + c * aqk;
                }
            }
    }
    std::vector<double> w(n);
    for (int i = 0; i < n; ++i) w[i] = a[i * n + i];
    return w;
}

double inverse_condition(const std::vector<double>& J, int rows, int cols, int rank) {
    // Gram matrix of the smaller dimension carries the same non-zero singular values
    bool tall = rows >= cols;
    int m = tall ? cols : rows;
    std::vector<double> G(m * m, 0.0);
    for (int i = 0; i < m; ++i)
        for (int j = 0; j < m; ++j) {
            double s = 0;
            if (tall)
                for (int k = 0; k < rows; ++k) s += J[k * cols + i] * J[k * cols + j];
            else
                for (int k = 0; k < cols; ++k) s += J[i * cols + k] * J[j * cols + k];
            G[i * m + j] = s;
        }
    auto w = sym_eigenvalues(G, m);
    std::sort(w.begin(), w.end(), std::greater<>());
    int k = rank > 0 ? std::min(rank, m) : m;
    if (w[0] <= 0) return 0;
    return std::sqrt(std::max(w[k - 1], 0.0) / w[0]);
}

static Expr capsule(const P3& p, Vec3 a, Vec3 b, double r) {
    Vec3 ba = b - a;
    double len2 = dot(ba, ba);
    Expr px = p.x - a.x, py = p.y - a.y, pz = p.z - a.z;
    if (len2 < 1e-12) return length(px, py, pz) - r;
    Expr h = min(max((px * ba.x + py * ba.y + pz * ba.z) * (1.0 / len2), 0.0), 1.0);
    return length(px - h * ba.x, py - h * ba.y, pz - h * ba.z) - r;
}

double segment_distance(Vec3 p1, Vec3 q1, Vec3 p2, Vec3 q2) {
    Vec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
    double a = dot(d1, d1), e = dot(d2, d2), f = dot(d2, r);
    double s = 0, t = 0;
    const double eps = 1e-12;
    if (a <= eps && e <= eps) return length(p1 - p2);
    if (a <= eps) {
        t = std::clamp(f / e, 0.0, 1.0);
    } else {
        double c = dot(d1, r);
        if (e <= eps) {
            s = std::clamp(-c / a, 0.0, 1.0);
        } else {
            double b = dot(d1, d2), denom = a * e - b * b;
            s = denom > eps ? std::clamp((b * f - c * e) / denom, 0.0, 1.0) : 0.0;
            t = (b * s + f) / e;
            if (t < 0) t = 0, s = std::clamp(-c / a, 0.0, 1.0);
            else if (t > 1) t = 1, s = std::clamp((b - c) / a, 0.0, 1.0);
        }
    }
    return length((p1 + d1 * s) - (p2 + d2 * t));
}

static double link_radius_at(const Robot& r, size_t i) { return r.link_radius * std::max(0.55, 1.0 - 0.08 * double(i - 1)); }

std::vector<Capsule> robot_capsules(const Robot& r, const std::vector<double>& q, Vec3 base) {
    std::vector<Capsule> caps;
    auto pts = chain_polyline(r, q);
    for (size_t k = 1; k < pts.size(); ++k) {
        if (length(pts[k].p - pts[k - 1].p) < 1e-6) continue;
        int link = pts[k].link;
        caps.push_back({pts[k - 1].p + base, pts[k].p + base, link_radius_at(r, size_t(std::max(link, 1))), link});
    }
    return caps;
}

Collision self_collision(const Robot& r, const std::vector<double>& q, Vec3 base, bool floor) {
    Collision c;
    auto caps = robot_capsules(r, q, base);
    for (size_t i = 0; i < caps.size(); ++i) {
        // The first link stands on the mount. A later link hits it when its centreline goes
        // more than its own radius below the plane: grazing the surface (a planar arm lying
        // in the mount plane, a UR5 wrist 5 mm under it at zero pose) is not a crash.
        if (floor && caps[i].link >= 2) {
            double low = std::min(caps[i].a.z, caps[i].b.z);
            if (low < base.z - r.mount_offset - caps[i].r) return {true, caps[i].link, -1, true};
        }
        // neighbours share a joint and always touch there; links two apart are checked
        for (size_t j = i + 2; j < caps.size(); ++j) {
            if (caps[j].link - caps[i].link < 2) continue;  // same or adjacent link
            // 10% interpenetration allowance: the capsules are a slightly fat model of the links
            if (segment_distance(caps[i].a, caps[i].b, caps[j].a, caps[j].b) < 0.9 * (caps[i].r + caps[j].r))
                return {true, caps[i].link, caps[j].link, false};
        }
    }
    return c;
}

Expr robot_geometry(const P3& p, const Robot& r, const std::vector<double>& q, Vec3 base) {
    auto frames = forward_kinematics(r, q);
    const double R = r.link_radius;
    // pedestal from the mounting surface up to the first joint
    double h = r.mount_offset + R * 1.5;
    Expr body = cylinder_z(translate(p, base + Vec3{0, 0, R * 0.75 - h / 2}), R * 1.6, h);
    for (auto& c : robot_capsules(r, q, base)) body = smooth_unite(body, capsule(p, c.a, c.b, c.r), c.r * 0.3);
    for (size_t i = 0; i + 1 < frames.size(); ++i) body = unite(body, sphere(translate(p, frames[i].o + base), link_radius_at(r, i + 1) * 1.25));
    // tool flange
    return unite(body, sphere(translate(p, frames.back().o + base), R * 0.8));
}

ReachResult reachability(const Robot& r, Vec3 base, size_t samples, double threshold, uint64_t seed, bool collisions, bool floor) {
    ReachResult out;
    out.samples = samples;
    out.reach_mm = r.reach();
    double L = std::max(out.reach_mm, 1.0);
    out.voxel_mm = L / 24.0;
    const double v = out.voxel_mm;
    std::mt19937_64 rng(seed);
    size_t n = r.joints.size();
    std::vector<std::uniform_real_distribution<double>> dist;
    for (auto& j : r.joints) dist.emplace_back(j.lo, j.hi);
    std::unordered_map<int64_t, size_t> index;
    std::vector<double> q(n);
    // task dimensions the arm can actually move in: max rank over random configurations
    auto rank_of = [&](const std::vector<double>& J, int rows) {
        std::vector<double> G(rows * rows, 0.0);
        for (int i = 0; i < rows; ++i)
            for (int j = 0; j < rows; ++j)
                for (size_t k = 0; k < n; ++k) G[i * rows + j] += J[i * n + k] * J[j * n + k];
        auto w = sym_eigenvalues(G, rows);
        double top = *std::max_element(w.begin(), w.end());
        return int(std::count_if(w.begin(), w.end(), [&](double x) { return x > 1e-9 * top; }));
    };
    int pos_rank = 0, full_rank = 0;
    for (int t = 0; t < 32; ++t) {
        for (size_t i = 0; i < n; ++i) q[i] = dist[i](rng);
        auto J = jacobian(r, forward_kinematics(r, q), L);
        full_rank = std::max(full_rank, rank_of(J, 6));
        J.resize(3 * n);
        pos_rank = std::max(pos_rank, rank_of(J, 3));
    }
    for (size_t s = 0; s < samples; ++s) {
        for (size_t i = 0; i < n; ++i) q[i] = dist[i](rng);
        if (collisions && self_collision(r, q, base, floor).hit) {
            out.colliding_samples++;
            continue;
        }
        auto frames = forward_kinematics(r, q);
        auto J = jacobian(r, frames, L);
        // full-pose (6-D) conditioning catches wrist alignment; reported as a sample count
        if (inverse_condition(J, 6, int(n), full_rank) < 1e-3) out.near_singular_samples++;
        // positional dexterity: the 3 x N translational block is scale-free (no mm/rad mixing)
        J.resize(3 * n);
        double dex = inverse_condition(J, 3, int(n), pos_rank);
        Vec3 pe = frames.back().o + base;
        int64_t ix = int64_t(std::floor(pe.x / v)), iy = int64_t(std::floor(pe.y / v)), iz = int64_t(std::floor(pe.z / v));
        int64_t key = ((ix + (1 << 20)) << 42) | ((iy + (1 << 20)) << 21) | (iz + (1 << 20));
        auto [it, fresh] = index.emplace(key, out.voxels.size());
        if (fresh) out.voxels.push_back({float((ix + 0.5) * v), float((iy + 0.5) * v), float((iz + 0.5) * v), float(dex), float(dex)});
        else {
            auto& vx = out.voxels[it->second];
            vx.best = std::max(vx.best, float(dex));
            vx.worst = std::min(vx.worst, float(dex));
        }
    }
    size_t locked = 0, dexterous = 0;
    for (auto& vx : out.voxels) {
        locked += vx.best < threshold;
        dexterous += vx.best >= 0.3;
    }
    double count = double(std::max<size_t>(out.voxels.size(), 1));
    out.singular_pct = 100.0 * double(locked) / count;
    out.dexterous_pct = 100.0 * double(dexterous) / count;
    out.volume_m3 = double(out.voxels.size()) * v * v * v * 1e-9;
    return out;
}

}  // namespace dk
