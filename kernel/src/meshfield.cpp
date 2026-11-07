#include "dk/meshfield.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace dk {

// ------------------------------------------------------------------ loading
static TriMesh load_obj(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    TriMesh m;
    std::string line;
    while (std::getline(in, line)) {
        if (line.size() < 2) continue;
        if (line[0] == 'v' && line[1] == ' ') {
            std::istringstream s(line.substr(2));
            Vec3 p;
            s >> p.x >> p.y >> p.z;
            m.v.push_back(p);
        } else if (line[0] == 'f' && line[1] == ' ') {
            std::istringstream s(line.substr(2));
            std::vector<uint32_t> idx;
            std::string tok;
            while (s >> tok) {
                long k = std::stol(tok.substr(0, tok.find('/')));
                idx.push_back(uint32_t(k < 0 ? long(m.v.size()) + k : k - 1));
            }
            for (size_t t = 1; t + 1 < idx.size(); ++t) m.f.push_back({idx[0], idx[t], idx[t + 1]});
        }
    }
    return m;
}

static TriMesh load_stl(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    TriMesh m;
    auto add = [&](Vec3 a, Vec3 b, Vec3 c) {
        uint32_t base = uint32_t(m.v.size());
        m.v.push_back(a), m.v.push_back(b), m.v.push_back(c);
        m.f.push_back({base, base + 1, base + 2});
    };
    if (data.size() >= 84) {
        uint32_t count;
        std::memcpy(&count, data.data() + 80, 4);
        if (data.size() == 84 + size_t(count) * 50) {  // binary
            for (uint32_t t = 0; t < count; ++t) {
                float f[12];
                std::memcpy(f, data.data() + 84 + size_t(t) * 50, 48);
                add({f[3], f[4], f[5]}, {f[6], f[7], f[8]}, {f[9], f[10], f[11]});
            }
            return m;
        }
    }
    std::istringstream s(data);  // ASCII
    std::string word;
    std::vector<Vec3> tri;
    while (s >> word)
        if (word == "vertex") {
            Vec3 p;
            s >> p.x >> p.y >> p.z;
            tri.push_back(p);
            if (tri.size() == 3) add(tri[0], tri[1], tri[2]), tri.clear();
        }
    return m;
}

TriMesh load_mesh(const std::string& path) {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    TriMesh m = lower.size() > 4 && lower.substr(lower.size() - 4) == ".stl" ? load_stl(path) : load_obj(path);
    if (m.f.empty()) throw std::runtime_error("no triangles in " + path);
    for (auto& t : m.f)
        for (auto i : t)
            if (i >= m.v.size()) throw std::runtime_error("face refers to a missing vertex in " + path);
    return m;
}

// ------------------------------------------------------------------ baking
// closest point on triangle abc to p (Ericson, Real-Time Collision Detection 5.1.5)
static Vec3 closest_on_triangle(Vec3 p, Vec3 a, Vec3 b, Vec3 c) {
    Vec3 ab = b - a, ac = c - a, ap = p - a;
    double d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return a;
    Vec3 bp = p - b;
    double d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return b;
    double vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
    Vec3 cp = p - c;
    double d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return c;
    double vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
    double va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    double denom = 1 / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
}

SdfGrid bake_sdf(const TriMesh& mesh, int cells) {
    Vec3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
    for (auto& p : mesh.v)
        for (int a = 0; a < 3; ++a) lo[a] = std::min(lo[a], p[a]), hi[a] = std::max(hi[a], p[a]);
    double extent = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z});
    if (!(extent > 0)) throw std::runtime_error("the mesh has no size");
    SdfGrid g;
    g.h = extent / std::max(cells, 8);
    g.lo = lo - Vec3{3 * g.h, 3 * g.h, 3 * g.h};
    g.nx = int(std::ceil((hi.x - lo.x) / g.h)) + 7;
    g.ny = int(std::ceil((hi.y - lo.y) / g.h)) + 7;
    g.nz = int(std::ceil((hi.z - lo.z) / g.h)) + 7;
    const size_t N = size_t(g.nx) * g.ny * g.nz;
    if (N > 40'000'000) throw std::runtime_error("import resolution too high for this part");
    auto pos = [&](int i, int j, int k) { return g.lo + Vec3{i * g.h, j * g.h, k * g.h}; };
    auto at = [&](int i, int j, int k) { return (size_t(k) * g.ny + j) * g.nx + i; };

    std::vector<float> dist(N, INFINITY);
    std::vector<Vec3> near(N);
    // 1. exact distance in a band of 2 cells around every triangle
    for (auto& t : mesh.f) {
        Vec3 a = mesh.v[t[0]], b = mesh.v[t[1]], c = mesh.v[t[2]];
        int i0[3], i1[3];
        for (int ax = 0; ax < 3; ++ax) {
            double mn = std::min({a[ax], b[ax], c[ax]}) - 2 * g.h, mx = std::max({a[ax], b[ax], c[ax]}) + 2 * g.h;
            int n = ax == 0 ? g.nx : ax == 1 ? g.ny : g.nz;
            i0[ax] = std::clamp(int(std::floor((mn - g.lo[ax]) / g.h)), 0, n - 1);
            i1[ax] = std::clamp(int(std::ceil((mx - g.lo[ax]) / g.h)), 0, n - 1);
        }
        for (int k = i0[2]; k <= i1[2]; ++k)
            for (int j = i0[1]; j <= i1[1]; ++j)
                for (int i = i0[0]; i <= i1[0]; ++i) {
                    Vec3 p = pos(i, j, k), q = closest_on_triangle(p, a, b, c);
                    double d = length(p - q);
                    size_t idx = at(i, j, k);
                    if (d < dist[idx]) dist[idx] = float(d), near[idx] = q;
                }
    }
    // 2. vector distance transform: forward and backward raster sweeps, twice
    for (int round = 0; round < 2; ++round)
        for (int dir : {1, -1}) {
            int ks = dir > 0 ? 0 : g.nz - 1, js = dir > 0 ? 0 : g.ny - 1, is = dir > 0 ? 0 : g.nx - 1;
            for (int k = ks; k >= 0 && k < g.nz; k += dir)
                for (int j = js; j >= 0 && j < g.ny; j += dir)
                    for (int i = is; i >= 0 && i < g.nx; i += dir) {
                        size_t idx = at(i, j, k);
                        Vec3 p = pos(i, j, k);
                        for (int dk = -1; dk <= 0; ++dk)
                            for (int dj = -1; dj <= 1; ++dj)
                                for (int di = -1; di <= 1; ++di) {
                                    // only neighbours already visited in this sweep direction
                                    if (dk == 0 && (dj > 0 || (dj == 0 && di >= 0))) continue;
                                    int ni = i + di * dir, nj = j + dj * dir, nk = k + dk * dir;
                                    if (ni < 0 || nj < 0 || nk < 0 || ni >= g.nx || nj >= g.ny || nk >= g.nz) continue;
                                    size_t n = at(ni, nj, nk);
                                    if (!std::isfinite(dist[n])) continue;
                                    double d = length(p - near[n]);
                                    if (d < dist[idx]) dist[idx] = float(d), near[idx] = near[n];
                                }
                    }
        }
    // 3. sign by ray parity along +X for every (j, k) row of voxel centres
    std::vector<std::vector<float>> rows(size_t(g.ny) * g.nz);
    const double jy = 1.234567e-5 * g.h, jz = 7.654321e-6 * g.h;  // off-grid jitter: avoid exact edge hits
    for (auto& t : mesh.f) {
        Vec3 a = mesh.v[t[0]], b = mesh.v[t[1]], c = mesh.v[t[2]];
        double ymin = std::min({a.y, b.y, c.y}), ymax = std::max({a.y, b.y, c.y});
        double zmin = std::min({a.z, b.z, c.z}), zmax = std::max({a.z, b.z, c.z});
        int j0 = std::max(0, int(std::ceil((ymin - g.lo.y - jy) / g.h))), j1 = std::min(g.ny - 1, int(std::floor((ymax - g.lo.y - jy) / g.h)));
        int k0 = std::max(0, int(std::ceil((zmin - g.lo.z - jz) / g.h))), k1 = std::min(g.nz - 1, int(std::floor((zmax - g.lo.z - jz) / g.h)));
        double det = (b.y - a.y) * (c.z - a.z) - (c.y - a.y) * (b.z - a.z);
        if (std::abs(det) < 1e-18) continue;  // triangle parallel to the ray
        for (int k = k0; k <= k1; ++k)
            for (int j = j0; j <= j1; ++j) {
                double y = g.lo.y + j * g.h + jy, z = g.lo.z + k * g.h + jz;
                double u = ((y - a.y) * (c.z - a.z) - (c.y - a.y) * (z - a.z)) / det;
                double v = ((b.y - a.y) * (z - a.z) - (y - a.y) * (b.z - a.z)) / det;
                if (u < 0 || v < 0 || u + v > 1) continue;
                rows[size_t(k) * g.ny + j].push_back(float(a.x + u * (b.x - a.x) + v * (c.x - a.x)));
            }
    }
    g.d.resize(N);
    for (int k = 0; k < g.nz; ++k)
        for (int j = 0; j < g.ny; ++j) {
            auto& r = rows[size_t(k) * g.ny + j];
            std::sort(r.begin(), r.end());
            size_t crossed = 0;
            for (int i = 0; i < g.nx; ++i) {
                double x = g.lo.x + i * g.h;
                while (crossed < r.size() && r[crossed] < x) ++crossed;
                bool inside = crossed % 2 == 1;
                size_t idx = at(i, j, k);
                g.d[idx] = inside ? -dist[idx] : dist[idx];
            }
        }
    return g;
}

// ------------------------------------------------------------------ registry + sampling
static std::mutex registry_lock;
static std::vector<std::shared_ptr<const SdfGrid>> registry;

int register_grid(std::shared_ptr<const SdfGrid> grid) {
    std::lock_guard lock(registry_lock);
    registry.push_back(std::move(grid));
    return int(registry.size() - 1);
}
const SdfGrid& grid_by_id(int id) {
    std::lock_guard lock(registry_lock);
    if (id < 0 || size_t(id) >= registry.size()) throw std::runtime_error("unknown sampled grid");
    return *registry[size_t(id)];
}
size_t grid_count() {
    std::lock_guard lock(registry_lock);
    return registry.size();
}

// value and gradient (world units) of the trilinear field, plus the outside-the-box term
static double sample(const SdfGrid& g, Vec3 p, Vec3* grad) {
    Vec3 hi = g.hi();
    Vec3 q{std::clamp(p.x, g.lo.x, hi.x), std::clamp(p.y, g.lo.y, hi.y), std::clamp(p.z, g.lo.z, hi.z)};
    double fx = (q.x - g.lo.x) / g.h, fy = (q.y - g.lo.y) / g.h, fz = (q.z - g.lo.z) / g.h;
    int i = std::min(int(fx), g.nx - 2), j = std::min(int(fy), g.ny - 2), k = std::min(int(fz), g.nz - 2);
    double tx = fx - i, ty = fy - j, tz = fz - k;
    double c000 = g.at(i, j, k), c100 = g.at(i + 1, j, k), c010 = g.at(i, j + 1, k), c110 = g.at(i + 1, j + 1, k);
    double c001 = g.at(i, j, k + 1), c101 = g.at(i + 1, j, k + 1), c011 = g.at(i, j + 1, k + 1), c111 = g.at(i + 1, j + 1, k + 1);
    double x00 = c000 + (c100 - c000) * tx, x10 = c010 + (c110 - c010) * tx, x01 = c001 + (c101 - c001) * tx, x11 = c011 + (c111 - c011) * tx;
    double y0 = x00 + (x10 - x00) * ty, y1 = x01 + (x11 - x01) * ty;
    double v = y0 + (y1 - y0) * tz;
    Vec3 out = p - q;
    double outside = length(out);
    // Outside the grid, keep a LOWER bound on the true distance (sphere tracing must never
    // overshoot): the surface lies inside the grid box, so it is at least |p - q| away, and by
    // the Lipschitz bound at least v - |p - q|. (v + |p - q| would be an upper bound.)
    if (outside > 0) {
        if (grad) *grad = out * (1.0 / outside);
        return std::max(outside, v - outside);
    }
    if (grad) {
        double dx = ((c100 - c000) * (1 - ty) + (c110 - c010) * ty) * (1 - tz) + ((c101 - c001) * (1 - ty) + (c111 - c011) * ty) * tz;
        double dy = (x10 - x00) * (1 - tz) + (x11 - x01) * tz;
        double dz = y1 - y0;
        *grad = Vec3{dx, dy, dz} * (1.0 / g.h);
    }
    return v;
}

double grid_eval(int id, double x, double y, double z) { return sample(grid_by_id(id), {x, y, z}, nullptr); }

Interval grid_eval(int id, Interval x, Interval y, Interval z) {
    // trilinear interpolation of a distance field changes by at most sqrt(3) per unit length;
    // the outside extension max(|p - q|, v - |p - q|) by at most sqrt(3) + 1
    Vec3 c{(x.lo + x.hi) / 2, (y.lo + y.hi) / 2, (z.lo + z.hi) / 2};
    double r = 0.5 * std::sqrt(x.width() * x.width() + y.width() * y.width() + z.width() * z.width());
    double v = sample(grid_by_id(id), c, nullptr);
    const double L = 2.7320508075688772;
    return {v - L * r, v + L * r};
}

Dual3 grid_eval(int id, Dual3 x, Dual3 y, Dual3 z) {
    Vec3 grad;
    double v = sample(grid_by_id(id), {x.v, y.v, z.v}, &grad);
    return {v, x.d * grad.x + y.d * grad.y + z.d * grad.z};
}

}  // namespace dk
