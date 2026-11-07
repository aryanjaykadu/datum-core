#include "dk/mesh.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace dk {
namespace {

using ProgramPtr = std::shared_ptr<const Program>;

struct Leaf {
    int32_t i, j, k;           // cell index (min corner) on the fine grid
    ProgramPtr program;        // pruned to this cell
    double corner[8];          // f at corners, bit order: x = 1, y = 2, z = 4
    int8_t edge_comp[12];      // surface component that crosses each edge (-1: no crossing)
    uint8_t components = 0;
    int32_t vertex = -1;       // index of component 0's vertex; component c is vertex + c
};

uint64_t key(int32_t i, int32_t j, int32_t k) {
    // 21 bits per axis, offset so small negative indices stay valid
    constexpr int32_t bias = 1 << 20;
    return (uint64_t(uint32_t(i + bias)) << 42) | (uint64_t(uint32_t(j + bias)) << 21) | uint64_t(uint32_t(k + bias));
}

struct Octree {
    Vec3 origin;
    double cell;
    std::vector<Leaf>* out;

    void descend(const ProgramPtr& prog, int32_t i, int32_t j, int32_t k, int32_t size) {
        Interval x{origin.x + i * cell, origin.x + (i + size) * cell};
        Interval y{origin.y + j * cell, origin.y + (j + size) * cell};
        Interval z{origin.z + k * cell, origin.z + (k + size) * cell};
        Interval b = prog->bounds(x, y, z);
        // Entirely outside / inside. Intervals are not outward-rounded, so a corner value of
        // ~1e-16 can sit just past a bound; the 1 nm margin keeps such cells.
        constexpr double margin = 1e-6;  // mm
        if (b.lo > margin || b.hi < -margin) return;
        auto pruned = std::make_shared<const Program>(prog->prune(x, y, z));
        if (size == 1) {
            out->push_back(Leaf{i, j, k, pruned, {}, -1});
            return;
        }
        int32_t h = size / 2;
        for (int c = 0; c < 8; ++c) descend(pruned, i + (c & 1) * h, j + ((c >> 1) & 1) * h, k + ((c >> 2) & 1) * h, h);
    }
};

// Symmetric 3x3 eigen-decomposition (cyclic Jacobi). Columns of v are eigenvectors.
void jacobi3(double a[3][3], double w[3], double v[3][3]) {
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) v[r][c] = r == c;
    for (int sweep = 0; sweep < 32; ++sweep) {
        double off = a[0][1] * a[0][1] + a[0][2] * a[0][2] + a[1][2] * a[1][2];
        if (off < 1e-30) break;
        for (int p = 0; p < 2; ++p)
            for (int q = p + 1; q < 3; ++q) {
                if (std::abs(a[p][q]) < 1e-300) continue;
                double theta = (a[q][q] - a[p][p]) / (2 * a[p][q]);
                double t = (theta >= 0 ? 1 : -1) / (std::abs(theta) + std::sqrt(theta * theta + 1));
                double c = 1 / std::sqrt(t * t + 1), s = t * c;
                for (int r = 0; r < 3; ++r) {  // A <- A J
                    double arp = a[r][p], arq = a[r][q];
                    a[r][p] = c * arp - s * arq;
                    a[r][q] = s * arp + c * arq;
                }
                for (int r = 0; r < 3; ++r) {  // A <- J^T A
                    double apr = a[p][r], aqr = a[q][r];
                    a[p][r] = c * apr - s * aqr;
                    a[q][r] = s * apr + c * aqr;
                }
                for (int r = 0; r < 3; ++r) {
                    double vrp = v[r][p], vrq = v[r][q];
                    v[r][p] = c * vrp - s * vrq;
                    v[r][q] = s * vrp + c * vrq;
                }
            }
    }
    for (int i = 0; i < 3; ++i) w[i] = a[i][i];
}

// Root of f on the segment p0 -> p1 where f(p0), f(p1) have opposite signs (Illinois).
Vec3 edge_root(const Program& f, Vec3 p0, Vec3 p1, double f0, double f1, std::vector<double>& slots) {
    double t0 = 0, t1 = 1;
    int side = 0;
    double t = 0.5;
    for (int it = 0; it < 48; ++it) {
        t = (t0 * f1 - t1 * f0) / (f1 - f0);
        if (!(t > t0 && t < t1)) t = 0.5 * (t0 + t1);
        Vec3 p = p0 + (p1 - p0) * t;
        double ft = f.eval<double>(p.x, p.y, p.z, slots);
        if (std::abs(ft) < 1e-12 || (t1 - t0) < 1e-12) break;
        if ((ft < 0) == (f0 < 0)) {
            t0 = t, f0 = ft;
            if (side == -1) f1 *= 0.5;
            side = -1;
        } else {
            t1 = t, f1 = ft;
            if (side == 1) f0 *= 0.5;
            side = 1;
        }
    }
    return p0 + (p1 - p0) * t;
}

constexpr int kEdges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};

struct Qef {
    double ata[3][3] = {}, atb[3] = {};
    Vec3 mass{};
    int points = 0;
};

// Minimiser of the QEF, clamped to the cell. Without usable normals: the mass point.
Vec3 solve_qef(const Qef& q, Vec3 base, double cell, double trunc) {
    Vec3 mass = q.mass * (1.0 / q.points);
    const auto& ata = q.ata;
    const auto& atb = q.atb;
    // Solve ATA (x - m) = ATb - ATA m with a truncated pseudo-inverse.
    double rhs[3];
    for (int r = 0; r < 3; ++r) rhs[r] = atb[r] - (ata[r][0] * mass.x + ata[r][1] * mass.y + ata[r][2] * mass.z);
    double a[3][3], w[3], v[3][3];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) a[r][c] = ata[r][c];
    jacobi3(a, w, v);
    double wmax = std::max({std::abs(w[0]), std::abs(w[1]), std::abs(w[2])});
    Vec3 dx{};
    for (int e = 0; e < 3; ++e) {
        if (std::abs(w[e]) <= trunc * wmax) continue;
        double proj = v[0][e] * rhs[0] + v[1][e] * rhs[1] + v[2][e] * rhs[2];
        dx = dx + Vec3{v[0][e], v[1][e], v[2][e]} * (proj / w[e]);
    }
    Vec3 vertex = mass + dx;
    for (int ax = 0; ax < 3; ++ax) vertex[ax] = std::clamp(vertex[ax], base[ax], base[ax] + cell);
    return vertex;
}

// Surface components inside one cell. Corners that are inside and joined by a cube edge
// belong to the same solid component; every sign-changing edge is assigned to the
// component of its inside corner. Using the same rule in every cell makes neighbours
// agree on ambiguous faces (diagonally opposite inside corners are always separate),
// so each sheet gets its own vertex and two sheets never share one: that sharing is
// what made plain DC non-manifold on thin walls.
// Fills leaf.edge_comp / leaf.components and writes one vertex per component.
int solve_cell(Leaf& leaf, Vec3 origin, double cell, double trunc, Vec3* vertices, std::vector<double>& slots) {
    const Program& f = *leaf.program;
    Vec3 base = origin + Vec3{leaf.i * cell, leaf.j * cell, leaf.k * cell};
    auto corner_pos = [&](int c) { return base + Vec3{(c & 1) * cell, ((c >> 1) & 1) * cell, ((c >> 2) & 1) * cell}; };
    auto inside = [&](int c) { return leaf.corner[c] < 0; };

    int parent[8];
    for (int c = 0; c < 8; ++c) parent[c] = c;
    auto find = [&](int c) {
        while (parent[c] != c) c = parent[c] = parent[parent[c]];
        return c;
    };
    for (auto& e : kEdges)
        if (inside(e[0]) && inside(e[1])) parent[find(e[0])] = find(e[1]);

    int label[8];
    std::fill(label, label + 8, -1);
    int count = 0;
    Qef qef[4];
    for (int ei = 0; ei < 12; ++ei) {
        const auto& e = kEdges[ei];
        leaf.edge_comp[ei] = -1;
        if (inside(e[0]) == inside(e[1])) continue;
        int root = find(inside(e[0]) ? e[0] : e[1]);
        if (label[root] < 0) label[root] = count++;  // at most 4 inside components in a cube
        int comp = label[root];
        leaf.edge_comp[ei] = int8_t(comp);

        Vec3 p = edge_root(f, corner_pos(e[0]), corner_pos(e[1]), leaf.corner[e[0]], leaf.corner[e[1]], slots);
        // Exactly on a face, kinks such as sqrt(0) or a min/max tie can give a zero
        // gradient; step a hair along the edge to either side until it is defined.
        Vec3 nrm = normalize(f.gradient(p).d);
        Vec3 dir = corner_pos(e[1]) - corner_pos(e[0]);
        for (double s : {1e-9, -1e-9, 1e-7, -1e-7})
            if (dot(nrm, nrm) == 0) nrm = normalize(f.gradient(p + dir * s).d);
        Qef& q = qef[comp];
        q.mass = q.mass + p;
        q.points++;
        double d = dot(nrm, p);  // a zero normal adds nothing: the vertex falls back to the mass point
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) q.ata[r][c] += nrm[r] * nrm[c];
            q.atb[r] += nrm[r] * d;
        }
    }
    for (int c = 0; c < count; ++c) vertices[c] = solve_qef(qef[c], base, cell, trunc);
    leaf.components = uint8_t(count);
    return count;
}

// Local edge index of the cube edge from corner a to corner b.
int local_edge(int a, int b) {
    for (int ei = 0; ei < 12; ++ei)
        if ((kEdges[ei][0] == a && kEdges[ei][1] == b) || (kEdges[ei][0] == b && kEdges[ei][1] == a)) return ei;
    return -1;
}

template <class Fn>
void parallel_for(size_t count, int threads, Fn fn) {
    std::atomic<size_t> next{0};
    auto worker = [&] {
        for (size_t i; (i = next.fetch_add(256)) < count;)
            for (size_t e = std::min(i + 256, count); i < e; ++i) fn(i);
    };
    std::vector<std::thread> pool;
    for (int t = 1; t < threads; ++t) pool.emplace_back(worker);
    worker();
    for (auto& t : pool) t.join();
}

}  // namespace

Mesh dual_contour(const Program& f, const MeshOptions& opt, MeshStats* stats) {
    auto t_start = std::chrono::steady_clock::now();
    int threads = opt.threads > 0 ? opt.threads : std::max(1u, std::thread::hardware_concurrency());
    const double cell = opt.cell;
    // pad by two cells so the surface never touches the grid boundary
    Vec3 origin = opt.lo - Vec3{2 * cell, 2 * cell, 2 * cell};
    double extent = std::max({opt.hi.x - opt.lo.x, opt.hi.y - opt.lo.y, opt.hi.z - opt.lo.z}) + 4 * cell;
    int32_t size = 1;
    while (size * cell < extent) size *= 2;

    // 1. octree: the top 64 sub-cubes are descended in parallel
    auto root = std::make_shared<const Program>(f);
    int32_t split = std::min<int32_t>(size, 4);
    int32_t step = size / split;
    std::vector<std::vector<Leaf>> buckets(split * split * split);
    parallel_for(buckets.size(), threads, [&](size_t b) {
        int32_t bi = int32_t(b % split), bj = int32_t((b / split) % split), bk = int32_t(b / (split * split));
        Octree tree{origin, cell, &buckets[b]};
        tree.descend(root, bi * step, bj * step, bk * step, step);
    });
    std::vector<Leaf> leaves;
    for (auto& b : buckets) std::move(b.begin(), b.end(), std::back_inserter(leaves));

    // 2+3. corner values and the QEF vertex for every leaf
    std::vector<std::array<Vec3, 4>> cell_vertex(leaves.size());
    parallel_for(leaves.size(), threads, [&](size_t li) {
        thread_local std::vector<double> slots;
        Leaf& leaf = leaves[li];
        for (int c = 0; c < 8; ++c) {
            Vec3 p = origin + Vec3{(leaf.i + (c & 1)) * cell, (leaf.j + ((c >> 1) & 1)) * cell, (leaf.k + ((c >> 2) & 1)) * cell};
            leaf.corner[c] = leaf.program->eval<double>(p.x, p.y, p.z, slots);
        }
    });
    // Neighbouring cells evaluate shared corners with different pruned programs. Those are
    // mathematically equal but not always bitwise equal at f = 0 (-0 vs -2e-32), and then
    // the two cells disagree on the corner's sign and the surface tears. Make every grid
    // corner take a single value: the first one recorded.
    {
        std::unordered_map<uint64_t, double> shared;
        shared.reserve(leaves.size() * 2);
        for (Leaf& leaf : leaves)
            for (int c = 0; c < 8; ++c) {
                auto [it, fresh] = shared.emplace(key(leaf.i + (c & 1), leaf.j + ((c >> 1) & 1), leaf.k + ((c >> 2) & 1)), leaf.corner[c]);
                leaf.corner[c] = it->second;
            }
    }
    parallel_for(leaves.size(), threads, [&](size_t li) {
        thread_local std::vector<double> slots;
        solve_cell(leaves[li], origin, cell, opt.qef_truncation, cell_vertex[li].data(), slots);
    });

    Mesh mesh;
    std::unordered_map<uint64_t, uint32_t> leaf_of;  // cell key -> index into leaves
    leaf_of.reserve(leaves.size());
    size_t clipped = 0;
    for (size_t li = 0; li < leaves.size(); ++li) {
        Leaf& l = leaves[li];
        if (l.components == 0) continue;
        // surface in a border cell means the part extends past opt.lo/opt.hi: it gets cut open
        clipped += l.i == 0 || l.j == 0 || l.k == 0 || l.i == size - 1 || l.j == size - 1 || l.k == size - 1;
        l.vertex = int32_t(mesh.vertices.size());
        leaf_of.emplace(key(l.i, l.j, l.k), uint32_t(li));
        for (int c = 0; c < l.components; ++c) mesh.vertices.push_back(cell_vertex[li][c]);
    }

    // 4. quads around sign-changing edges; each edge is owned by the cell at its start
    size_t missing = 0;
    for (const Leaf& leaf : leaves) {
        for (int axis = 0; axis < 3; ++axis) {
            double f0 = leaf.corner[0], f1 = leaf.corner[1 << axis];
            if ((f0 < 0) == (f1 < 0)) continue;
            int u = (axis + 1) % 3, v = (axis + 2) % 3;
            int32_t idx[3] = {leaf.i, leaf.j, leaf.k};
            uint32_t quad[4];
            bool ok = true;
            const int du[4] = {0, 1, 1, 0}, dv[4] = {0, 0, 1, 1};
            for (int q = 0; q < 4 && ok; ++q) {
                int32_t c[3] = {idx[0], idx[1], idx[2]};
                c[u] -= du[q];
                c[v] -= dv[q];
                auto it = leaf_of.find(key(c[0], c[1], c[2]));
                if (it == leaf_of.end()) {
                    ok = false;
                    break;
                }
                // in the neighbour, this grid edge starts at the corner offset by (du, dv)
                int start = (du[q] << u) | (dv[q] << v);
                const Leaf& nb = leaves[it->second];
                int comp = nb.edge_comp[local_edge(start, start | (1 << axis))];
                if (comp < 0) ok = false;
                else quad[q] = uint32_t(nb.vertex + comp);
            }
            if (!ok) {
                ++missing;
                continue;
            }
            // (u, v) cycle is counter-clockwise about +axis; flip when the outside is behind
            if (!(f0 < 0)) std::swap(quad[1], quad[3]);
            const Vec3* P = mesh.vertices.data();
            double d02 = length(P[quad[0]] - P[quad[2]]), d13 = length(P[quad[1]] - P[quad[3]]);
            if (d02 <= d13) {
                mesh.triangles.push_back({quad[0], quad[1], quad[2]});
                mesh.triangles.push_back({quad[0], quad[2], quad[3]});
            } else {
                mesh.triangles.push_back({quad[0], quad[1], quad[3]});
                mesh.triangles.push_back({quad[1], quad[2], quad[3]});
            }
        }
    }

    if (stats) {
        stats->leaves = leaves.size();
        stats->surface_cells = mesh.vertices.size();
        stats->missing_quads = missing;
        stats->clipped_cells = clipped;
        stats->full_size = f.size();
        double total = 0;
        for (auto& l : leaves) total += double(l.program->size());
        stats->avg_pruned_size = leaves.empty() ? 0 : total / double(leaves.size());
        stats->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
    }
    return mesh;
}

// ---------------------------------------------------------------- quality

Topology topology(const Mesh& m) {
    // undirected edge -> (uses as a->b with a < b, uses as b->a)
    std::unordered_map<uint64_t, std::pair<int, int>> uses;
    uses.reserve(m.triangles.size() * 2);
    for (auto& t : m.triangles)
        for (int e = 0; e < 3; ++e) {
            uint32_t a = t[e], b = t[(e + 1) % 3];
            auto& u = uses[(uint64_t(std::min(a, b)) << 32) | std::max(a, b)];
            (a < b ? u.first : u.second)++;
        }
    Topology topo;
    for (auto& [edge, u] : uses) {
        int total = u.first + u.second;
        if (total == 1) topo.boundary_edges++;
        else if (u.first != 1 || u.second != 1) topo.nonmanifold_edges++;
    }
    topo.euler = long(m.vertices.size()) - long(uses.size()) + long(m.triangles.size());
    return topo;
}

Deviation deviation(const Mesh& m, const Program& f) {
    Deviation d;
    std::vector<double> slots;
    auto sample = [&](Vec3 p) {
        double v = std::abs(f.eval<double>(p.x, p.y, p.z, slots));
        d.max_abs = std::max(d.max_abs, v);
        d.mean_abs += v;
        d.samples++;
    };
    for (auto& v : m.vertices) sample(v);
    for (auto& t : m.triangles) {
        Vec3 a = m.vertices[t[0]], b = m.vertices[t[1]], c = m.vertices[t[2]];
        sample((a + b + c) * (1.0 / 3));
        sample((a + b) * 0.5);
    }
    if (d.samples) d.mean_abs /= double(d.samples);
    return d;
}

MassProperties mass_properties(const Mesh& m) {
    // Eberly, "Polyhedral Mass Properties (Revisited)": exact integrals over the closed mesh.
    double intg[10] = {};
    const double mult[10] = {1. / 6, 1. / 24, 1. / 24, 1. / 24, 1. / 60, 1. / 60, 1. / 60, 1. / 120, 1. / 120, 1. / 120};
    MassProperties mp;
    auto sub = [](double w0, double w1, double w2, double& f1, double& f2, double& f3, double& g0, double& g1, double& g2) {
        double t0 = w0 + w1, t1 = w0 * w0, t2 = t1 + w1 * t0;
        f1 = t0 + w2;
        f2 = t2 + w2 * f1;
        f3 = w0 * t1 + w1 * t2 + w2 * f2;
        g0 = f2 + w0 * (f1 + w0);
        g1 = f2 + w1 * (f1 + w1);
        g2 = f2 + w2 * (f1 + w2);
    };
    for (auto& t : m.triangles) {
        Vec3 p0 = m.vertices[t[0]], p1 = m.vertices[t[1]], p2 = m.vertices[t[2]];
        Vec3 n = cross(p1 - p0, p2 - p0);
        mp.area_mm2 += 0.5 * length(n);
        double f1x, f2x, f3x, g0x, g1x, g2x, f1y, f2y, f3y, g0y, g1y, g2y, f1z, f2z, f3z, g0z, g1z, g2z;
        sub(p0.x, p1.x, p2.x, f1x, f2x, f3x, g0x, g1x, g2x);
        sub(p0.y, p1.y, p2.y, f1y, f2y, f3y, g0y, g1y, g2y);
        sub(p0.z, p1.z, p2.z, f1z, f2z, f3z, g0z, g1z, g2z);
        intg[0] += n.x * f1x;
        intg[1] += n.x * f2x;
        intg[2] += n.y * f2y;
        intg[3] += n.z * f2z;
        intg[4] += n.x * f3x;
        intg[5] += n.y * f3y;
        intg[6] += n.z * f3z;
        intg[7] += n.x * (p0.y * g0x + p1.y * g1x + p2.y * g2x);
        intg[8] += n.y * (p0.z * g0y + p1.z * g1y + p2.z * g2y);
        intg[9] += n.z * (p0.x * g0z + p1.x * g1z + p2.x * g2z);
    }
    for (int i = 0; i < 10; ++i) intg[i] *= mult[i];
    double vol = intg[0];
    mp.volume_mm3 = vol;
    if (vol == 0) return mp;
    Vec3 c{intg[1] / vol, intg[2] / vol, intg[3] / vol};
    mp.centroid = c;
    double ixx = intg[5] + intg[6] - vol * (c.y * c.y + c.z * c.z);
    double iyy = intg[4] + intg[6] - vol * (c.z * c.z + c.x * c.x);
    double izz = intg[4] + intg[5] - vol * (c.x * c.x + c.y * c.y);
    double ixy = -(intg[7] - vol * c.x * c.y);
    double iyz = -(intg[8] - vol * c.y * c.z);
    double ixz = -(intg[9] - vol * c.z * c.x);
    double I[3][3] = {{ixx, ixy, ixz}, {ixy, iyy, iyz}, {ixz, iyz, izz}};
    for (int r = 0; r < 3; ++r)
        for (int col = 0; col < 3; ++col) mp.inertia[r][col] = I[r][col];
    return mp;
}

bool write_stl(const Mesh& m, const std::string& path, const std::string& header) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    char head[80] = {};
    std::snprintf(head, sizeof head, "%s", header.c_str());
    out.write(head, 80);
    uint32_t count = uint32_t(m.triangles.size());
    out.write(reinterpret_cast<const char*>(&count), 4);
    for (auto& t : m.triangles) {
        Vec3 a = m.vertices[t[0]], b = m.vertices[t[1]], c = m.vertices[t[2]];
        Vec3 n = normalize(cross(b - a, c - a));
        float data[12] = {float(n.x), float(n.y), float(n.z), float(a.x), float(a.y), float(a.z),
                          float(b.x), float(b.y), float(b.z), float(c.x), float(c.y), float(c.z)};
        out.write(reinterpret_cast<const char*>(data), sizeof data);
        uint16_t attr = 0;
        out.write(reinterpret_cast<const char*>(&attr), 2);
    }
    return bool(out);
}

bool write_obj(const Mesh& m, const std::string& path) {
    std::ofstream out(path);
    if (!out) return false;
    char buf[96];
    for (auto& v : m.vertices) {
        std::snprintf(buf, sizeof buf, "v %.9g %.9g %.9g\n", v.x, v.y, v.z);
        out << buf;
    }
    for (auto& t : m.triangles) out << "f " << t[0] + 1 << ' ' << t[1] + 1 << ' ' << t[2] + 1 << '\n';
    return bool(out);
}

}  // namespace dk
