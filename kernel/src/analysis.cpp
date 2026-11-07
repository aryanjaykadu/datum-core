#include "dk/analysis.hpp"

#include <algorithm>
#include <numeric>
#include <random>
#include <unordered_map>

namespace dk {

const std::vector<Material>& materials() {
    static const std::vector<Material> table = {
        {"al6061t6", "Aluminium 6061-T6", 2700, 68.9, 276, 310, 2.6, 13.0, 800},
        {"al7075t6", "Aluminium 7075-T6", 2810, 71.7, 503, 572, 4.5, 13.0, 650},
        {"ti64", "Titanium Ti-6Al-4V", 4430, 113.8, 880, 950, 28.0, 35.0, 60},
        {"ss316", "Stainless steel 316L", 8000, 193.0, 170, 485, 4.2, 6.15, 120},
        {"s355", "Structural steel S355", 7850, 210.0, 355, 510, 0.85, 1.55, 250},
        {"pa12", "Nylon PA12", 1010, 1.7, 48, 50, 9.0, 6.5, 3000},
    };
    return table;
}

const Material& material(const std::string& key) {
    for (auto& m : materials())
        if (m.key == key) return m;
    return materials()[0];
}

bool ShellReport::suspicious() const {
    return std::any_of(findings.begin(), findings.end(), [](auto& f) { return f.suspicious; });
}

ShellReport scan_shells(const Mesh& m, const MassProperties& whole) {
    // connected components over shared vertices
    std::vector<uint32_t> parent(m.vertices.size());
    std::iota(parent.begin(), parent.end(), 0u);
    auto find = [&](uint32_t v) {
        while (parent[v] != v) v = parent[v] = parent[parent[v]];
        return v;
    };
    for (auto& t : m.triangles) {
        parent[find(t[0])] = find(t[1]);
        parent[find(t[1])] = find(t[2]);
    }
    std::unordered_map<uint32_t, Mesh> parts;
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> remap;
    for (auto& t : m.triangles) {
        uint32_t c = find(t[0]);
        Mesh& part = parts[c];
        auto& map = remap[c];
        std::array<uint32_t, 3> tri;
        for (int k = 0; k < 3; ++k) {
            auto [it, fresh] = map.emplace(t[k], uint32_t(part.vertices.size()));
            if (fresh) part.vertices.push_back(m.vertices[t[k]]);
            tri[k] = it->second;
        }
        part.triangles.push_back(tri);
    }
    ShellReport report;
    report.shells = parts.size();
    double outer = 0;
    for (auto& [c, part] : parts) outer = std::max(outer, mass_properties(part).volume_mm3);
    for (auto& [c, part] : parts) {
        MassProperties mp = mass_properties(part);
        if (mp.volume_mm3 == outer) continue;  // the main body
        Vec3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
        for (auto& v : part.vertices)
            for (int a = 0; a < 3; ++a) lo[a] = std::min(lo[a], v[a]), hi[a] = std::max(hi[a], v[a]);
        double size = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z});
        bool cavity = mp.volume_mm3 < 0;  // inward-facing shell
        double vol = std::abs(mp.volume_mm3);
        // a separate body sitting next to the part is not hidden; only small enclosed shells count
        bool small = vol < 0.005 * std::abs(whole.volume_mm3);
        report.findings.push_back({(lo + hi) * 0.5, vol, size, cavity, cavity && small});
    }
    return report;
}

static bool escapes(const Program& f, Vec3 p, Vec3 dir, double limit, std::vector<double>& slots) {
    double t = 0.05;  // start just off the surface
    for (int i = 0; i < 256 && t < limit; ++i) {
        Vec3 q = p + dir * t;
        double d = f.eval<double>(q.x, q.y, q.z, slots);
        if (d < -1e-4) return false;  // re-entered the material
        t += std::max(std::abs(d), 0.02);
    }
    return true;
}

Accessibility tool_access(const Mesh& m, const Program& f, size_t samples) {
    Accessibility acc;
    if (m.triangles.empty()) return acc;
    std::vector<double> area(m.triangles.size());
    double total = 0;
    Vec3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
    for (auto& v : m.vertices)
        for (int a = 0; a < 3; ++a) lo[a] = std::min(lo[a], v[a]), hi[a] = std::max(hi[a], v[a]);
    double limit = length(hi - lo) + 1;
    for (size_t i = 0; i < m.triangles.size(); ++i) {
        auto& t = m.triangles[i];
        area[i] = 0.5 * length(cross(m.vertices[t[1]] - m.vertices[t[0]], m.vertices[t[2]] - m.vertices[t[0]]));
        total += area[i];
    }
    std::mt19937_64 rng(7);
    std::discrete_distribution<size_t> pick(area.begin(), area.end());
    const Vec3 dirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    std::vector<uint8_t> mask(samples, 0);
    std::vector<double> slots;
    int count[6] = {};
    for (size_t s = 0; s < samples; ++s) {
        auto& t = m.triangles[pick(rng)];
        Vec3 a = m.vertices[t[0]], b = m.vertices[t[1]], c = m.vertices[t[2]];
        Vec3 n = normalize(cross(b - a, c - a));
        Vec3 p = (a + b + c) * (1.0 / 3);
        for (int d = 0; d < 6; ++d) {
            if (dot(n, dirs[d]) < 0.05) continue;
            if (escapes(f, p, dirs[d], limit, slots)) {
                mask[s] |= uint8_t(1 << d);
                count[d]++;
            }
        }
    }
    size_t covered = 0;
    for (auto m8 : mask) covered += m8 != 0;
    for (int d = 0; d < 6; ++d) acc.reachable_pct[d] = 100.0 * count[d] / double(samples);
    acc.covered_pct = 100.0 * double(covered) / double(samples);
    // greedy set cover: fewest setups reaching 99% of the reachable samples
    uint8_t used = 0;
    size_t got = 0;
    while (got < 0.99 * double(covered) && acc.setups < 6) {
        int best = -1;
        size_t gain = 0;
        for (int d = 0; d < 6; ++d) {
            if (used & (1 << d)) continue;
            size_t g = 0;
            for (auto m8 : mask) g += (m8 & (1 << d)) && !(m8 & used);
            if (g > gain) gain = g, best = d;
        }
        if (best < 0) break;
        used |= uint8_t(1 << best);
        got += gain;
        acc.setups++;
    }
    return acc;
}

ImpactResult impact_screen(const Mesh& m, const Material& mat, double impactor_kg, double drop_m) {
    Vec3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
    for (auto& v : m.vertices)
        for (int a = 0; a < 3; ++a) lo[a] = std::min(lo[a], v[a]), hi[a] = std::max(hi[a], v[a]);
    double e[3] = {hi.x - lo.x, hi.y - lo.y, hi.z - lo.z};
    std::sort(e, e + 3, std::greater<>());
    double L = e[0] * 1e-3, b = e[1] * 1e-3, h = e[2] * 1e-3;  // m
    double I = b * h * h * h / 12, E = mat.youngs_gpa * 1e9;
    double k = 3 * E * I / (L * L * L);
    double U = impactor_kg * 9.80665 * drop_m;
    double d = std::sqrt(2 * U / k);
    double sigma = k * d * L * (h / 2) / I;  // M c / I with M = F L
    return {d * 1e3, sigma / 1e6, sigma > mat.ultimate_mpa * 1e6, U};
}

Quote estimate_quote(const MassProperties& mp, const Vec3& size, const Accessibility& acc, const Material& mat, int quantity) {
    Quote q{};
    const double margin = 3;  // mm stock allowance per side
    double stock_mm3 = (size.x + 2 * margin) * (size.y + 2 * margin) * (size.z + 2 * margin);
    q.stock_kg = stock_mm3 * 1e-9 * mat.density_kg_m3;
    double removed = std::max(stock_mm3 - mp.volume_mm3, 0.0);
    q.setups = std::max(acc.setups, 1);
    // roughing at the material MRR, finishing proportional to surface area, plus per-setup handling
    q.machining_h = removed / mat.mrr_mm3_s / 3600 + mp.area_mm2 / 20000.0 / 60 * 10 + 0.25 * q.setups;
    const double rate = 95, setup_fee = 60;  // USD/h on a 5-axis cell, programming + fixturing
    double unit = q.stock_kg * mat.usd_per_kg + q.machining_h * rate;
    q.price_usd = (unit * quantity + setup_fee * q.setups) * (acc.covered_pct < 99 ? 1.25 : 1.0);  // undercuts need 5-axis
    q.lead_days = 3 + q.setups + (quantity > 10 ? 2 : 0);
    q.carbon_kg = q.stock_kg * quantity * mat.carbon_kg_per_kg;
    q.fee_usd = q.price_usd * kPlatformFeeRate;
    q.provider = "datum-estimate";
    return q;
}

}  // namespace dk
