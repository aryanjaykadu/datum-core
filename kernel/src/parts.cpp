#include "dk/parts.hpp"

#include <cmath>
#include <map>
#include <numbers>
#include <stdexcept>

namespace dk {

namespace {
constexpr double pi = std::numbers::pi;
double involute(double a) { return std::tan(a) - a; }
}  // namespace

P3 polar_repeat(const P3& p, int n) {
    if (n <= 1) return p;
    const double sector = 2 * pi / n;
    Expr theta = atan2(p.y, p.x);
    Expr k = floor(theta * (1.0 / sector) + 0.5);
    Expr phi = theta - k * sector;
    Expr r = length(p.x, p.y);
    return {r * cos(phi), r * sin(phi), p.z};
}

Polygon2 gear_tooth_polygon(double m, int z, double pressure_deg) {
    const double alpha = pressure_deg * pi / 180;
    const double rp = m * z / 2, rb = rp * std::cos(alpha), ra = rp + m, rf = rp - 1.25 * m;
    const double r_in = std::max(rf - 0.5 * m, 0.25 * rp);  // dips into the root disc so the union is solid
    // polar half-angle of the tooth at radius r (tooth centred on +x)
    const double half_pitch = pi / (2 * z) + involute(alpha);
    auto half_angle = [&](double r) { return half_pitch - (r > rb ? involute(std::acos(rb / r)) : 0.0); };
    const double r0 = std::max(rb, rf);
    std::vector<double> radii;
    radii.push_back(r_in);
    const int N = 8;
    for (int i = 0; i <= N; ++i) radii.push_back(r0 + (ra - r0) * i / N);
    Polygon2 poly;
    for (double r : radii) {  // lower flank, root to tip
        double a = -half_angle(std::max(r, r0));
        poly.push_back({r * std::cos(a), r * std::sin(a)});
    }
    for (auto it = radii.rbegin(); it != radii.rend(); ++it) {  // upper flank, tip to root
        double a = half_angle(std::max(*it, r0));
        poly.push_back({*it * std::cos(a), *it * std::sin(a)});
    }
    return poly;
}

Expr spur_gear(const P3& p, double m, int z, double pressure_deg, double width, double bore) {
    if (z < 6 || z > 200) throw std::runtime_error("teeth must be 6..200");
    if (m <= 0) throw std::runtime_error("module must be > 0");
    const double rp = m * z / 2, ra = rp + m, rf = rp - 1.25 * m;
    if (bore >= 2 * rf * 0.9) throw std::runtime_error("bore is larger than the gear root");
    P3 s = polar_repeat(p, z);
    Expr tooth = polygon_sdf(s.x, s.y, gear_tooth_polygon(m, z, pressure_deg));
    Expr r = length(p.x, p.y);
    Expr profile = intersect(unite(r - rf, tooth), r - ra);
    Expr solid = max(profile, abs(p.z) - width / 2);
    if (bore > 0) solid = max(solid, bore / 2 - r);
    return solid;
}

Expr threaded_rod(const P3& p, double d, double pitch, double len) {
    if (d <= 0 || pitch <= 0 || len <= 0) throw std::runtime_error("thread sizes must be > 0");
    const double R = d / 2, h = 0.61343 * pitch;   // ISO external thread depth
    const double slope = std::tan(pi / 3);          // radial change per axial mm on a 60 deg flank
    Expr r = length(p.x, p.y);
    Expr theta = atan2(p.y, p.x);
    Expr u = p.z - theta * (pitch / (2 * pi));
    Expr w = abs(u - floor(u * (1.0 / pitch) + 0.5) * pitch);  // axial distance to the nearest crest
    // flank radius: crest flat of pitch/8, then a 60 deg V down to the root
    Expr flank = R - max(w - pitch / 16, 0.0) * slope;
    Expr radius = max(flank, R - h);
    // |grad| of (r - radius) is ~2 (60 deg flank) plus the helix lead term pitch / (2 pi r),
    // which grows near the axis. Union with the solid core keeps the thread term out there.
    Expr d_thread = min((r - radius) * 0.48, r - (R - h));
    Expr ends = abs(p.z) - len / 2;
    return max(d_thread, ends);
}

HoleSpec metric_hole(const std::string& size) {
    // clearance (ISO 273 normal), counterbore dia/depth (ISO 4762), countersink dia (ISO 10642)
    static const std::map<std::string, HoleSpec> table = {
        {"M3", {3.4, 6.5, 3.3}},   {"M4", {4.5, 8.0, 4.4}},   {"M5", {5.5, 10.0, 5.4}},
        {"M6", {6.6, 11.0, 6.5}},  {"M8", {9.0, 15.0, 8.6}},  {"M10", {11.0, 18.0, 10.8}},
        {"M12", {13.5, 20.0, 13.0}}, {"M16", {17.5, 26.0, 17.5}}, {"M20", {22.0, 33.0, 21.5}},
    };
    auto it = table.find(size);
    if (it == table.end()) throw std::runtime_error("unknown hole size " + size);
    return it->second;
}

Expr hole_cutter(const P3& q, const HoleSpec& h, double depth, const std::string& kind) {
    const double above = std::max(depth, 10.0);  // clears the surface the hole starts from
    Expr r = length(q.x, q.y);
    Expr shaft = max(r - h.clearance_d / 2, max(-q.z - depth, q.z - above));
    if (kind == "counterbore") {
        Expr cb = max(r - h.head_d / 2, max(-q.z - h.head_depth, q.z - above));
        return unite(shaft, cb);
    }
    if (kind == "countersink") {
        // 90 deg cone: radius head_d/2 at the entry, shrinking 1:1 with depth
        Expr cone = max((r - h.head_d / 2 - q.z) * std::numbers::sqrt2 / 2, q.z - above);
        return unite(shaft, cone);
    }
    if (kind != "simple") throw std::runtime_error("hole type must be simple, counterbore or countersink");
    return shaft;
}

Expr textured(Expr body, const P3& p, const std::string& pattern, double cell, double depth) {
    if (cell <= 0 || depth <= 0) throw std::runtime_error("texture cell and depth must be > 0");
    const double k = 2 * pi / cell;
    Expr g;
    double lip;  // Lipschitz bound of g
    if (pattern == "bumps") {
        g = sin(p.x * k) * sin(p.y * k) * sin(p.z * k);
        lip = k * std::sqrt(3.0);
    } else if (pattern == "ribs") {
        g = sin(p.z * k);
        lip = k;
    } else if (pattern == "knurl") {
        // crossed diagonal grooves around z: min of two |sin| ridges
        Expr a = abs(sin((p.x + p.z) * (k / 2))), b = abs(sin((p.y - p.z) * (k / 2)));
        g = min(a, b) * 2.0 - 1.0;
        lip = k * std::sqrt(2.0);
    } else {
        throw std::runtime_error("texture must be knurl, bumps or ribs");
    }
    Expr shaped = body - g * (depth / 2);
    return shaped * (1.0 / (1.0 + lip * depth / 2));
}

}  // namespace dk
