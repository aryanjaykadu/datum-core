#include "dk/fea.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <thread>

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
                        const std::vector<FeaSphere>& loads, int max_iterations, double tol) {
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
                    for (int c = 0; c < 8; ++c) {
                        size_t m = node_id(ei + corner[c][0], ej + corner[c][1], ek + corner[c][2]);
                        if (fixed[m]) continue;
                        const double* row = &Ke[(3 * e) * 24 + 3 * c];
                        for (int d = 0; d < 3; ++d)
                            acc[d] += row[d * 24] * u[3 * m] + row[d * 24 + 1] * u[3 * m + 1] + row[d * 24 + 2] * u[3 * m + 2];
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
                for (int d = 0; d < 3; ++d) dsum[d] += Ke[(3 * e + d) * 24 + 3 * e + d];
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
                r.von_mises[size_t(i) + size_t(g.nx) * (j + size_t(g.ny) * k)] = float(vm);
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

}  // namespace dk
