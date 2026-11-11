#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <thread>

#include "dk/fea.hpp"
#include "dk/bricks.hpp"
#include "dk/meshfield.hpp"
#include "dk/shapes.hpp"

namespace dk {

size_t FeaGrid::solid_count() const {
    size_t n = 0;
    for (auto s : solid) n += s;
    return n;
}

double voxel_size_for(Vec3 lo, Vec3 hi, double target) {
    Vec3 d = hi - lo;
    return std::cbrt(std::max(d.x * d.y * d.z, 1e-9) / std::max(target, 1.0));
}

FeaGrid voxelize(const Program& part, Vec3 lo, Vec3 hi, double h) {
    FeaGrid g;
    g.h = h;
    g.lo = lo;
    g.nx = std::max(1, int(std::ceil((hi.x - lo.x) / h)));
    g.ny = std::max(1, int(std::ceil((hi.y - lo.y) / h)));
    g.nz = std::max(1, int(std::ceil((hi.z - lo.z) / h)));
    g.solid.assign(size_t(g.nx) * g.ny * g.nz, 0);
    for (int k = 0; k < g.nz; ++k)
        for (int j = 0; j < g.ny; ++j)
            for (int i = 0; i < g.nx; ++i) {
                Vec3 c{lo.x + (i + 0.5) * h, lo.y + (j + 0.5) * h, lo.z + (k + 0.5) * h};
                g.solid[size_t(i) + size_t(g.nx) * (j + size_t(g.ny) * k)] = part(c) < 0;
            }
    return g;
}

namespace {

constexpr int corner[8][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}};

// Strain-displacement matrix (6 x 24) of a cube of edge h at natural point (xi, eta, zeta).
void b_matrix(double h, double xi, double eta, double zeta, double B[6][24]) {
    for (int a = 0; a < 6; ++a)
        for (int c = 0; c < 24; ++c) B[a][c] = 0;
    for (int a = 0; a < 8; ++a) {
        double sx = 2.0 * corner[a][0] - 1, sy = 2.0 * corner[a][1] - 1, sz = 2.0 * corner[a][2] - 1;
        // dN/dx = (2/h) dN/dxi
        double dx = 0.125 * sx * (1 + sy * eta) * (1 + sz * zeta) * 2 / h;
        double dy = 0.125 * sy * (1 + sx * xi) * (1 + sz * zeta) * 2 / h;
        double dz = 0.125 * sz * (1 + sx * xi) * (1 + sy * eta) * 2 / h;
        B[0][3 * a] = dx;
        B[1][3 * a + 1] = dy;
        B[2][3 * a + 2] = dz;
        B[3][3 * a] = dy, B[3][3 * a + 1] = dx;          // gamma_xy
        B[4][3 * a + 1] = dz, B[4][3 * a + 2] = dy;      // gamma_yz
        B[5][3 * a] = dz, B[5][3 * a + 2] = dx;          // gamma_zx
    }
}

void d_matrix(double E, double nu, double D[6][6]) {
    for (int a = 0; a < 6; ++a)
        for (int c = 0; c < 6; ++c) D[a][c] = 0;
    double c = E / ((1 + nu) * (1 - 2 * nu));
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) D[i][j] = c * (i == j ? 1 - nu : nu);
    for (int i = 3; i < 6; ++i) D[i][i] = c * (1 - 2 * nu) / 2;
}

// 24 x 24 element stiffness by 2 x 2 x 2 Gauss quadrature
std::array<double, 576> element_stiffness(double h, double E, double nu) {
    double D[6][6];
    d_matrix(E, nu, D);
    std::array<double, 576> K{};
    const double gp = 1 / std::sqrt(3.0), detJ = h * h * h / 8;
    double B[6][24], DB[6][24];
    for (int q = 0; q < 8; ++q) {
        b_matrix(h, (q & 1 ? gp : -gp), (q & 2 ? gp : -gp), (q & 4 ? gp : -gp), B);
        for (int i = 0; i < 6; ++i)
            for (int c = 0; c < 24; ++c) {
                double s = 0;
                for (int k = 0; k < 6; ++k) s += D[i][k] * B[k][c];
                DB[i][c] = s;
            }
        for (int r = 0; r < 24; ++r)
            for (int c = 0; c < 24; ++c) {
                double s = 0;
                for (int k = 0; k < 6; ++k) s += B[k][r] * DB[k][c];
                K[r * 24 + c] += s * detJ;
            }
    }
    return K;
}

void parallel_for(size_t n, const std::function<void(size_t, size_t)>& body) {
    unsigned t = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    if (n < 4096) t = 1;
    std::vector<std::thread> pool;
    size_t chunk = (n + t - 1) / t;
    for (unsigned w = 0; w < t; ++w) {
        size_t a = w * chunk, b = std::min(n, a + chunk);
        if (a >= b) break;
        pool.emplace_back(body, a, b);
    }
    for (auto& th : pool) th.join();
}

}  // namespace

FeaResult solve_elastic(const FeaGrid& g, double E, double nu, const std::vector<FeaSphere>& fixtures,
                        const std::vector<FeaSphere>& loads, int max_iterations, double tol,
                        const std::vector<float>* stiffness, const std::vector<float>* amplify) {
    auto kfac = [&](int i, int j, int k) { return stiffness ? double((*stiffness)[size_t(i) + size_t(g.nx) * (j + size_t(g.ny) * k)]) : 1.0; };
    auto t0 = std::chrono::steady_clock::now();
    if (g.solid_count() == 0) throw std::runtime_error("the part has no material at this resolution");
    const int NX = g.nx + 1, NY = g.ny + 1, NZ = g.nz + 1;
    const size_t nodes = size_t(NX) * NY * NZ;
    auto node_id = [&](int i, int j, int k) { return size_t(i) + size_t(NX) * (j + size_t(NY) * k); };
    auto solid_at = [&](int i, int j, int k) {
        return i >= 0 && j >= 0 && k >= 0 && i < g.nx && j < g.ny && k < g.nz && g.solid[size_t(i) + size_t(g.nx) * (j + size_t(g.ny) * k)];
    };
    auto node_pos = [&](size_t n) {
        int i = int(n % NX), j = int((n / NX) % NY), k = int(n / (size_t(NX) * NY));
        return Vec3{g.lo.x + i * g.h, g.lo.y + j * g.h, g.lo.z + k * g.h};
    };

    // active nodes: touched by at least one solid voxel
    std::vector<uint8_t> active(nodes, 0);
    for (int k = 0; k < g.nz; ++k)
        for (int j = 0; j < g.ny; ++j)
            for (int i = 0; i < g.nx; ++i)
                if (solid_at(i, j, k))
                    for (auto& c : corner) active[node_id(i + c[0], j + c[1], k + c[2])] = 1;

    FeaResult r;
    std::vector<uint8_t> fixed(nodes, 0);
    for (size_t n = 0; n < nodes; ++n) {
        if (!active[n]) continue;
        Vec3 p = node_pos(n);
        for (auto& f : fixtures)
            if (length(p - f.centre) <= f.radius) {
                fixed[n] = 1;
                ++r.fixed_nodes;
                break;
            }
    }
    if (r.fixed_nodes == 0) throw std::runtime_error("no fixture touches the part: place it on the part's surface");

    std::vector<double> f(3 * nodes, 0.0);
    for (auto& L : loads) {
        std::vector<size_t> hit;
        for (size_t n = 0; n < nodes; ++n)
            if (active[n] && !fixed[n] && length(node_pos(n) - L.centre) <= L.radius) hit.push_back(n);
        if (hit.empty()) throw std::runtime_error("a load touches no free part of the model: place it on the surface, away from fixtures");
        for (size_t n : hit)
            for (int a = 0; a < 3; ++a) f[3 * n + a] += L.force[a] / double(hit.size());
        r.loaded_nodes += hit.size();
    }

    const auto Ke = element_stiffness(g.h, E, nu);
    // y = K u (fixed and inactive dofs act as identity rows), gathered per node
    auto apply = [&](const std::vector<double>& u, std::vector<double>& y) {
        parallel_for(nodes, [&](size_t a, size_t b) {
            for (size_t n = a; n < b; ++n) {
                double acc[3] = {0, 0, 0};
                if (!active[n] || fixed[n]) {
                    for (int d = 0; d < 3; ++d) y[3 * n + d] = u[3 * n + d];
                    continue;
                }
                int i = int(n % NX), j = int((n / NX) % NY), k = int(n / (size_t(NX) * NY));
                for (int e = 0; e < 8; ++e) {
                    // the element whose corner `e` is this node
                    int ei = i - corner[e][0], ej = j - corner[e][1], ek = k - corner[e][2];
                    if (!solid_at(ei, ej, ek)) continue;
                    const double kf = kfac(ei, ej, ek);
                    for (int c = 0; c < 8; ++c) {
                        size_t m = node_id(ei + corner[c][0], ej + corner[c][1], ek + corner[c][2]);
                        if (fixed[m]) continue;
                        const double* row = &Ke[(3 * e) * 24 + 3 * c];
                        for (int d = 0; d < 3; ++d)
                            acc[d] += kf * (row[d * 24] * u[3 * m] + row[d * 24 + 1] * u[3 * m + 1] + row[d * 24 + 2] * u[3 * m + 2]);
                    }
                }
                for (int d = 0; d < 3; ++d) y[3 * n + d] = acc[d];
            }
        });
    };
    // Jacobi preconditioner: diagonal of the assembled K
    std::vector<double> inv_diag(3 * nodes, 1.0);
    for (size_t n = 0; n < nodes; ++n) {
        if (!active[n] || fixed[n]) continue;
        int i = int(n % NX), j = int((n / NX) % NY), k = int(n / (size_t(NX) * NY));
        double dsum[3] = {0, 0, 0};
        for (int e = 0; e < 8; ++e)
            if (solid_at(i - corner[e][0], j - corner[e][1], k - corner[e][2]))
                for (int d = 0; d < 3; ++d) dsum[d] += kfac(i - corner[e][0], j - corner[e][1], k - corner[e][2]) * Ke[(3 * e + d) * 24 + 3 * e + d];
        for (int d = 0; d < 3; ++d) inv_diag[3 * n + d] = dsum[d] > 0 ? 1 / dsum[d] : 1;
    }
    for (size_t n = 0; n < nodes; ++n)
        if (!active[n] || fixed[n]) f[3 * n] = f[3 * n + 1] = f[3 * n + 2] = 0;

    // preconditioned conjugate gradients
    const size_t N = 3 * nodes;
    std::vector<double> u(N, 0.0), res = f, z(N), p(N), Ap(N);
    auto dot = [&](const std::vector<double>& a, const std::vector<double>& b) {
        double s = 0;
        for (size_t i = 0; i < N; ++i) s += a[i] * b[i];
        return s;
    };
    for (size_t i = 0; i < N; ++i) z[i] = res[i] * inv_diag[i];
    p = z;
    double rz = dot(res, z), fnorm = std::sqrt(dot(f, f));
    if (fnorm == 0) throw std::runtime_error("add a load: there is no force on the part");
    for (r.iterations = 0; r.iterations < max_iterations; ++r.iterations) {
        apply(p, Ap);
        double alpha = rz / std::max(dot(p, Ap), 1e-300);
        for (size_t i = 0; i < N; ++i) u[i] += alpha * p[i], res[i] -= alpha * Ap[i];
        if (std::sqrt(dot(res, res)) < tol * fnorm) {
            r.converged = true;
            ++r.iterations;
            break;
        }
        for (size_t i = 0; i < N; ++i) z[i] = res[i] * inv_diag[i];
        double rz2 = dot(res, z);
        double beta = rz2 / rz;
        rz = rz2;
        for (size_t i = 0; i < N; ++i) p[i] = z[i] + beta * p[i];
    }

    // displacements and von Mises stress at element centres
    r.displacement.resize(nodes);
    for (size_t n = 0; n < nodes; ++n) {
        r.displacement[n] = {u[3 * n], u[3 * n + 1], u[3 * n + 2]};
        if (active[n]) r.max_displacement = std::max(r.max_displacement, length(r.displacement[n]));
    }
    double B[6][24], D[6][6];
    b_matrix(g.h, 0, 0, 0, B);
    d_matrix(E, nu, D);
    r.von_mises.assign(g.solid.size(), 0.f);
    for (int k = 0; k < g.nz; ++k)
        for (int j = 0; j < g.ny; ++j)
            for (int i = 0; i < g.nx; ++i) {
                if (!solid_at(i, j, k)) continue;
                double ue[24];
                for (int c = 0; c < 8; ++c) {
                    size_t m = node_id(i + corner[c][0], j + corner[c][1], k + corner[c][2]);
                    for (int d = 0; d < 3; ++d) ue[3 * c + d] = u[3 * m + d];
                }
                double eps[6] = {}, s[6] = {};
                for (int a = 0; a < 6; ++a)
                    for (int c = 0; c < 24; ++c) eps[a] += B[a][c] * ue[c];
                for (int a = 0; a < 6; ++a)
                    for (int b = 0; b < 6; ++b) s[a] += D[a][b] * eps[b];
                double vm = std::sqrt(0.5 * ((s[0] - s[1]) * (s[0] - s[1]) + (s[1] - s[2]) * (s[1] - s[2]) + (s[2] - s[0]) * (s[2] - s[0])) +
                                      3 * (s[3] * s[3] + s[4] * s[4] + s[5] * s[5]));
                size_t vi = size_t(i) + size_t(g.nx) * (j + size_t(g.ny) * k);
                vm *= kfac(i, j, k) * (amplify ? double((*amplify)[vi]) : 1.0);
                r.von_mises[vi] = float(vm);
                r.max_vm = std::max(r.max_vm, vm);
            }
    r.node_vm.assign(nodes, 0.f);
    for (size_t n = 0; n < nodes; ++n) {
        if (!active[n]) continue;
        int i = int(n % NX), j = int((n / NX) % NY), k = int(n / (size_t(NX) * NY));
        double sum = 0;
        int cnt = 0;
        for (int e = 0; e < 8; ++e) {
            int ei = i - corner[e][0], ej = j - corner[e][1], ek = k - corner[e][2];
            if (solid_at(ei, ej, ek)) sum += r.von_mises[size_t(ei) + size_t(g.nx) * (ej + size_t(g.ny) * ek)], ++cnt;
        }
        r.node_vm[n] = float(sum / std::max(cnt, 1));
    }
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

double sample_node_field(const FeaGrid& g, const std::vector<float>& field, Vec3 p) {
    const int NX = g.nx + 1, NY = g.ny + 1, NZ = g.nz + 1;
    double fx = (p.x - g.lo.x) / g.h, fy = (p.y - g.lo.y) / g.h, fz = (p.z - g.lo.z) / g.h;
    fx = std::clamp(fx, 0.0, double(NX - 1) - 1e-9);
    fy = std::clamp(fy, 0.0, double(NY - 1) - 1e-9);
    fz = std::clamp(fz, 0.0, double(NZ - 1) - 1e-9);
    int i = std::min(int(fx), NX - 2), j = std::min(int(fy), NY - 2), k = std::min(int(fz), NZ - 2);
    double tx = fx - i, ty = fy - j, tz = fz - k;
    auto at = [&](int a, int b, int c) { return double(field[size_t(a) + size_t(NX) * (b + size_t(NY) * c)]); };
    double v = 0, w = 0;
    // weight only nodes that carry a value, so the surface is not dragged towards 0 by empty space
    for (int c = 0; c < 8; ++c) {
        double val = at(i + corner[c][0], j + corner[c][1], k + corner[c][2]);
        double wt = (corner[c][0] ? tx : 1 - tx) * (corner[c][1] ? ty : 1 - ty) * (corner[c][2] ? tz : 1 - tz);
        if (val > 0) v += wt * val, w += wt;
    }
    return w > 0 ? v / w : 0.0;
}

void part_bounds(const Program& f, Vec3& lo, Vec3& hi) {
    const double R = 2000;  // mm
    BrickGrid probe = build_bricks(f, {-R, -R, -R}, {R, R, R}, 64);
    double step = 2 * R / 64;
    lo = {1e300, 1e300, 1e300};
    hi = {-1e300, -1e300, -1e300};
    for (size_t b = 0; b < probe.kind.size(); ++b) {
        if (probe.kind[b] == 0) continue;
        int i = int(b % 64), j = int((b / 64) % 64), k = int(b / 4096);
        Vec3 bl{-R + i * step, -R + j * step, -R + k * step};
        for (int a = 0; a < 3; ++a) lo[a] = std::min(lo[a], bl[a]), hi[a] = std::max(hi[a], bl[a] + step);
    }
    if (lo.x > hi.x) throw std::runtime_error("the part is empty");
    if (lo.x <= -R + 1e-9 || hi.x >= R - 1e-9) throw std::runtime_error("the part is unbounded");
    BrickGrid fine = build_bricks(f, lo, hi, 64);
    Vec3 s{(hi.x - lo.x) / 64, (hi.y - lo.y) / 64, (hi.z - lo.z) / 64};
    Vec3 nlo{1e300, 1e300, 1e300}, nhi{-1e300, -1e300, -1e300};
    for (size_t b = 0; b < fine.kind.size(); ++b) {
        if (fine.kind[b] == 0) continue;
        int i = int(b % 64), j = int((b / 64) % 64), k = int(b / 4096);
        Vec3 bl{lo.x + i * s.x, lo.y + j * s.y, lo.z + k * s.z};
        for (int a = 0; a < 3; ++a) nlo[a] = std::min(nlo[a], bl[a]), nhi[a] = std::max(nhi[a], bl[a] + s[a]);
    }
    lo = nlo;
    hi = nhi;
}

LightenResult lighten(const P3& p, Expr body, const LightenStudy& study, const LightenOptions& opt) {
    if (!(opt.target >= 0.05 && opt.target <= 0.85)) throw std::runtime_error("target must be 5 % .. 85 % lighter");
    if (opt.skin <= 0 || opt.min_wall <= 0) throw std::runtime_error("skin and minimum wall must be > 0");
    Program part = compile(body);
    Vec3 lo, hi;
    part_bounds(part, lo, hi);
    Vec3 size = hi - lo;
    double smallest = std::min({size.x, size.y, size.z});
    double cell = opt.cell > 0 ? opt.cell : std::clamp(smallest / 4.0, 4.0, 20.0);
    if (smallest < 2 * opt.skin + cell * 0.5) throw std::runtime_error("the part is too thin to lighten: reduce the skin or cell size");

    // stress field on the part (or uniform without a study)
    double h = voxel_size_for(lo, hi, opt.voxels);
    Vec3 pad{h, h, h};
    FeaGrid grid = voxelize(part, lo - pad, hi + pad, h);
    const int NX = grid.nx + 1, NY = grid.ny + 1, NZ = grid.nz + 1;
    std::vector<float> weight(size_t(NX) * NY * NZ, 1.0f);
    bool graded = !study.fixtures.empty() && !study.loads.empty();
    if (graded) {
        FeaResult r = solve_elastic(grid, study.youngs_mpa, 0.33, study.fixtures, study.loads);
        std::vector<float> sorted;
        for (float v : r.node_vm) if (v > 0) sorted.push_back(v);
        std::sort(sorted.begin(), sorted.end());
        double ref = sorted.empty() ? 1 : std::max<double>(sorted[size_t(0.95 * (sorted.size() - 1))], 1e-9);
        for (size_t n = 0; n < weight.size(); ++n) weight[n] = float(std::sqrt(std::clamp(r.node_vm[n] / ref, 0.0, 1.0)));
        // nodes outside the part carry 0: give them their neighbours' value so the field is smooth at the skin
        for (int pass = 0; pass < 2; ++pass)
            for (int k = 0; k < NZ; ++k)
                for (int j = 0; j < NY; ++j)
                    for (int i = 0; i < NX; ++i) {
                        size_t n = size_t(i) + size_t(NX) * (j + size_t(NY) * k);
                        if (r.node_vm[n] > 0) continue;
                        float best = 0;
                        for (int dk = -1; dk <= 1; ++dk)
                            for (int dj = -1; dj <= 1; ++dj)
                                for (int di = -1; di <= 1; ++di) {
                                    int a = i + di, b = j + dj, c = k + dk;
                                    if (a < 0 || b < 0 || c < 0 || a >= NX || b >= NY || c >= NZ) continue;
                                    best = std::max(best, weight[size_t(a) + size_t(NX) * (b + size_t(NY) * c)]);
                                }
                        weight[n] = best;
                    }
    }

    // sample points in the core: the lattice decides, everything else is fixed (skin = solid)
    const double k = 2 * std::numbers::pi / cell, gscale = 1.0 / (std::numbers::sqrt2 * k);
    std::vector<Vec3> core;
    double solid_samples = 0, skin_samples = 0;
    const int sub = 2;  // samples per voxel edge
    const double hs = h / sub;
    for (int kz = 0; kz < grid.nz * sub; ++kz)
        for (int jy = 0; jy < grid.ny * sub; ++jy)
            for (int ix = 0; ix < grid.nx * sub; ++ix) {
                Vec3 q{grid.lo.x + (ix + 0.5) * hs, grid.lo.y + (jy + 0.5) * hs, grid.lo.z + (kz + 0.5) * hs};
                double d = part(q);
                if (d >= 0) continue;
                solid_samples += 1;
                if (d > -opt.skin) skin_samples += 1;
                else core.push_back(q);
            }
    if (solid_samples == 0 || core.empty()) throw std::runtime_error("nothing to lighten inside this part");
    auto wall = [&](const std::vector<float>& field, Vec3 q) { return sample_node_field(grid, field, q); };
    std::vector<float> core_w(core.size()), core_g(core.size());
    for (size_t i = 0; i < core.size(); ++i) {
        Vec3 q = core[i] * k;
        double g = std::sin(q.x) * std::cos(q.y) + std::sin(q.y) * std::cos(q.z) + std::sin(q.z) * std::cos(q.x);
        core_g[i] = float(std::abs(g) * gscale);
        core_w[i] = graded ? float(std::max(wall(weight, core[i]), 0.0)) : 1.0f;
    }
    const double wmax_cap = cell * 0.45;  // beyond this the gyroid closes up
    auto thickness = [&](double scale, double w) { return std::clamp(scale * (0.25 + 0.75 * w), opt.min_wall, wmax_cap); };
    auto removed_at = [&](double scale) {
        double kept = skin_samples;
        for (size_t i = 0; i < core.size(); ++i) kept += core_g[i] < thickness(scale, core_w[i]) / 2;
        return 1 - kept / solid_samples;
    };
    double a = 0, b = wmax_cap * 4;
    double best_removed = removed_at(a);
    if (best_removed < opt.target - 1e-3) {
        throw std::runtime_error("cannot remove that much: even the thinnest lattice removes only " + std::to_string(int(best_removed * 100)) +
                                 " %; lower the target or use a thinner skin");
    }
    for (int it = 0; it < 40; ++it) {
        double m = 0.5 * (a + b);
        if (removed_at(m) > opt.target) a = m; else b = m;
    }
    double scale = 0.5 * (a + b);

    // thickness field on the nodes, made gentle (|grad t| <= 0.5) so the distance stays a bound
    std::vector<float> t(weight.size());
    for (size_t n = 0; n < t.size(); ++n) t[n] = float(thickness(scale, graded ? weight[n] : 1.0));
    const float step = float(0.5 * h);
    for (int pass = 0; pass < 6; ++pass)
        for (int kk = 0; kk < NZ; ++kk)
            for (int j = 0; j < NY; ++j)
                for (int i = 0; i < NX; ++i) {
                    size_t n = size_t(i) + size_t(NX) * (j + size_t(NY) * kk);
                    float lowest = t[n];
                    if (i > 0) lowest = std::min(lowest, t[n - 1] + step);
                    if (j > 0) lowest = std::min(lowest, t[n - NX] + step);
                    if (kk > 0) lowest = std::min(lowest, t[n - size_t(NX) * NY] + step);
                    if (i + 1 < NX) lowest = std::min(lowest, t[n + 1] + step);
                    if (j + 1 < NY) lowest = std::min(lowest, t[n + NX] + step);
                    if (kk + 1 < NZ) lowest = std::min(lowest, t[n + size_t(NX) * NY] + step);
                    t[n] = lowest;
                }
    auto sg = std::make_shared<SdfGrid>();
    sg->lo = grid.lo;  // nodes sit on the voxel corners
    sg->h = h;
    sg->nx = NX, sg->ny = NY, sg->nz = NZ;
    sg->d = t;
    LightenResult out;
    out.grid_id = register_grid(sg);
    out.wall_min = *std::min_element(t.begin(), t.end());
    out.wall_max = *std::max_element(t.begin(), t.end());
    out.cell = cell;
    out.graded = graded;
    // the sheet: |gyroid| as an approximate distance, minus half the local wall; the grid
    // slope (<= 0.5 / mm, halved again) and the gyroid's own error are absorbed by the 1/1.6
    Expr x = p.x * k, y = p.y * k, z = p.z * k;
    Expr g = sin(x) * cos(y) + sin(y) * cos(z) + sin(z) * cos(x);
    Expr tlocal = sample_grid(out.grid_id, p.x, p.y, p.z);
    Expr sheet = (abs(g) * gscale - tlocal * 0.5) * (1.0 / 1.6);
    Expr inner = offset(body, -opt.skin);  // shrunk by the skin: the core region
    Expr core_lattice = intersect(sheet, inner);
    out.shape = unite(shell_inward(body, opt.skin), core_lattice);
    // what the bisection measured (thickness before smoothing; smoothing only thins walls slightly)
    double kept = skin_samples;
    for (size_t i = 0; i < core.size(); ++i) kept += core_g[i] < sample_node_field(grid, t, core[i]) / 2;
    out.removed = 1 - kept / solid_samples;
    return out;
}

}  // namespace dk
