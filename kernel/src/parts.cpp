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

// ------------------------------------------------------------------ aerospace
Polygon2 naca4_polygon(const std::string& code, double chord, int n) {
    if (code.size() != 4 || code.find_first_not_of("0123456789") != std::string::npos) throw std::runtime_error("NACA code must be 4 digits, e.g. 2412");
    if (chord <= 0) throw std::runtime_error("chord must be > 0");
    const double m = (code[0] - '0') / 100.0, pc = (code[1] - '0') / 10.0, t = std::stoi(code.substr(2)) / 100.0;
    if (t < 0.04) throw std::runtime_error("thickness must be at least 4 % (last two digits >= 04)");
    if (m > 0 && pc == 0) throw std::runtime_error("a cambered section needs a camber position (second digit 1-9)");
    auto section = [&](double x, double sign) -> std::array<double, 2> {
        double yt = 5 * t * (0.2969 * std::sqrt(x) - 0.1260 * x - 0.3516 * x * x + 0.2843 * x * x * x - 0.1015 * x * x * x * x);
        double yc = 0, dyc = 0;
        if (m > 0) {
            if (x < pc) yc = m / (pc * pc) * (2 * pc * x - x * x), dyc = 2 * m / (pc * pc) * (pc - x);
            else yc = m / ((1 - pc) * (1 - pc)) * ((1 - 2 * pc) + 2 * pc * x - x * x), dyc = 2 * m / ((1 - pc) * (1 - pc)) * (pc - x);
        }
        double th = std::atan(dyc);
        return {(x - sign * yt * std::sin(th)) * chord, (yc + sign * yt * std::cos(th)) * chord};
    };
    Polygon2 poly;
    // lower surface leading edge -> trailing edge, then upper surface back (counter-clockwise)
    for (int i = 0; i <= n; ++i) poly.push_back(section(0.5 * (1 - std::cos(pi * i / n)), -1));
    for (int i = n; i >= 1; --i) poly.push_back(section(0.5 * (1 - std::cos(pi * i / n)), +1));
    return poly;
}

Expr wing(const P3& p, const std::string& naca, double root, double taper, double span, double sweep_deg, double dihedral_deg) {
    if (span <= 0) throw std::runtime_error("span must be > 0");
    if (taper < 0.1 || taper > 1.5) throw std::runtime_error("taper ratio must be 0.1 .. 1.5");
    if (std::abs(sweep_deg) > 60 || std::abs(dihedral_deg) > 30) throw std::runtime_error("sweep must be within 60 deg and dihedral within 30 deg");
    Polygon2 poly = naca4_polygon(naca, root);
    const double sx = std::tan(sweep_deg * pi / 180), sy = std::tan(dihedral_deg * pi / 180);
    Expr t = min(max(p.z * (1.0 / span), 0.0), 1.0);
    Expr scale = 1.0 + t * (taper - 1.0);
    // station coordinates: undo the tip offset, then the taper
    Expr u = (p.x - t * (sx * span)) / scale, v = (p.y - t * (sy * span)) / scale;
    Expr d2 = polygon_sdf(u, v, poly) * scale;
    // worst change per mm of z: the section slides (sweep, dihedral) and shrinks (taper)
    double extent = 0;
    for (auto& q : poly) extent = std::max(extent, std::hypot(q[0], q[1]));
    double shrink = std::abs(1 - taper) * extent / span / std::min(taper, 1.0);
    double k = 1.0 / std::sqrt(1.0 + sx * sx + sy * sy + shrink * shrink + 2 * shrink * std::hypot(sx, sy));
    return max(d2 * k, abs(p.z - span / 2) - span / 2);
}

Expr nose_cone(const P3& p, const std::string& shape, double diameter, double length) {
    if (diameter <= 0 || length <= 0) throw std::runtime_error("nose cone sizes must be > 0");
    const double R = diameter / 2, L = length;
    auto radius = [&](double z) {  // z from the base (0) to the tip (L)
        double s = z / L;
        if (shape == "conical") return R * (1 - s);
        if (shape == "elliptical") return R * std::sqrt(std::max(0.0, 1 - s * s));
        if (shape == "parabolic") return R * (1 - s * s);
        double rho = (R * R + L * L) / (2 * R);  // tangent ogive
        return std::sqrt(std::max(0.0, rho * rho - z * z)) + R - rho;
    };
    if (shape != "ogive" && shape != "conical" && shape != "elliptical" && shape != "parabolic")
        throw std::runtime_error("nose cone shape must be ogive, conical, elliptical or parabolic");
    // the profile reaches 1 mm across the axis so the axis itself is inside, not on an edge
    Polygon2 poly = {{-1, 0}, {R, 0}};
    const int N = 14;
    for (int i = 1; i < N; ++i) {
        // denser near the tip, where the profile turns fastest
        double z = L * std::sin(0.5 * pi * i / N);
        poly.push_back({radius(z), z});
    }
    poly.push_back({0, L});
    poly.push_back({-1, L});
    // revolve_profile turns (x = radius, y = height) about y: feed it z as the height
    P3 q{p.x, p.z, p.y};
    return revolve_profile(q, poly, 360);
}

// ------------------------------------------------------------------ machine elements
Expr coil_spring(const P3& p, double D, double d, double len, double turns) {
    if (D <= 0 || d <= 0 || len <= 0 || turns < 1) throw std::runtime_error("spring sizes must be > 0 and turns >= 1");
    if (d >= D * 0.6) throw std::runtime_error("wire diameter must be well below the coil diameter");
    const double R = D / 2, pitch = len / turns;
    if (pitch < d * 1.02) throw std::runtime_error("coils overlap: use fewer turns, thinner wire or a longer spring");
    Expr r = length(p.x, p.y);
    Expr theta = atan2(p.y, p.x);
    Expr u = p.z - theta * (pitch / (2 * pi));
    Expr w = u - floor(u * (1.0 / pitch) + 0.5) * pitch;  // axial offset to the nearest coil
    Expr tube = length(r - R, w) - d / 2;
    // same-angle distance overestimates: by the helix angle near the wire, by the missed
    // closer coil (up to pitch / 2 away) near the axis
    double k = std::min(1.0 / std::sqrt(1.0 + std::pow(pitch / (2 * pi * (R - d / 2)), 2)), R / std::hypot(R, pitch / 2));
    return max(tube * (k * 0.98), abs(p.z) - len / 2);
}

BearingSize bearing_size(const std::string& name) {
    static const std::map<std::string, BearingSize> table = {
        {"608", {8, 22, 7}},    {"6000", {10, 26, 8}},  {"6200", {10, 30, 9}},  {"6201", {12, 32, 10}},
        {"6202", {15, 35, 11}}, {"6203", {17, 40, 12}}, {"6204", {20, 47, 14}}, {"6205", {25, 52, 15}},
    };
    auto it = table.find(name);
    if (it == table.end()) throw std::runtime_error("unknown bearing " + name);
    return it->second;
}

Expr ball_bearing(const P3& p, const BearingSize& s) {
    if (!(s.bore > 0 && s.od > s.bore * 1.5 && s.width > 0)) throw std::runtime_error("bearing needs bore < outer diameter and a width");
    const double ri = s.bore / 2, ro = s.od / 2, gap = ro - ri;
    const double rp = (ri + ro) / 2;                       // pitch radius
    const double rb = std::min(0.3 * gap, 0.42 * s.width);  // ball radius
    const double ring = 0.30 * gap;                        // ring thickness
    int n = std::max(5, int(std::floor(2 * pi * rp / (2.35 * rb))));
    Expr r = length(p.x, p.y);
    Expr slab = abs(p.z) - s.width / 2;
    Expr inner = max(max(ri - r, r - (ri + ring)), slab);
    Expr outer = max(max((ro - ring) - r, r - ro), slab);
    P3 q = polar_repeat(p, n);
    Expr ball = length(q.x - rp, q.y, q.z) - rb;
    ball = max(ball, r - ro);  // radially bounded, so far-away boxes can be culled
    return unite(unite(inner, outer), ball);
}

BeamSize ipe_size(const std::string& name) {
    static const std::map<std::string, BeamSize> table = {
        {"IPE 80", {80, 46, 3.8, 5.2}},   {"IPE 100", {100, 55, 4.1, 5.7}}, {"IPE 120", {120, 64, 4.4, 6.3}},
        {"IPE 160", {160, 82, 5.0, 7.4}}, {"IPE 200", {200, 100, 5.6, 8.5}},
    };
    auto it = table.find(name);
    if (it == table.end()) throw std::runtime_error("unknown section " + name);
    return it->second;
}

Polygon2 beam_polygon(const std::string& kind, const BeamSize& s) {
    const double h = s.h, b = s.b, tw = s.tw, tf = s.tf;
    if (!(h > 0 && b > 0 && tw > 0 && tf > 0)) throw std::runtime_error("section sizes must be > 0");
    if (tw >= b || 2 * tf >= h) throw std::runtime_error("web / flange thickness is too large for the section");
    const double x = b / 2, y = h / 2, w = tw / 2;
    if (kind == "I-beam") return {{-x, -y}, {x, -y}, {x, -y + tf}, {w, -y + tf}, {w, y - tf}, {x, y - tf}, {x, y}, {-x, y}, {-x, y - tf}, {-w, y - tf}, {-w, -y + tf}, {-x, -y + tf}};
    if (kind == "C-channel") return {{-x, -y}, {x, -y}, {x, -y + tf}, {-x + tw, -y + tf}, {-x + tw, y - tf}, {x, y - tf}, {x, y}, {-x, y}};
    if (kind == "L-angle") return {{-x, -y}, {x, -y}, {x, -y + tf}, {-x + tw, -y + tf}, {-x + tw, y}, {-x, y}};
    if (kind == "T-bar") return {{-w, -y}, {w, -y}, {w, y - tf}, {x, y - tf}, {x, y}, {-x, y}, {-x, y - tf}, {-w, y - tf}};
    throw std::runtime_error("section must be I-beam, C-channel, L-angle, T-bar or Rect tube");
}

Expr beam(const P3& p, const std::string& kind, const BeamSize& s, double len) {
    if (len <= 0) throw std::runtime_error("length must be > 0");
    if (kind == "Rect tube") {
        if (2 * s.tw >= std::min(s.b, s.h)) throw std::runtime_error("wall is too thick for the tube");
        Expr outer = box(p, {s.b, s.h, len});
        Expr inner = box(p, {s.b - 2 * s.tw, s.h - 2 * s.tw, len + 2});
        return subtract(outer, inner);
    }
    return extrude_profile(p, beam_polygon(kind, s), len, true);
}

// ------------------------------------------------------------------ automotive
Expr brake_disc(const P3& p, double od, double thickness, double bore, int bolts, double pcd, int drilled) {
    if (!(od > 0 && thickness > 0 && bore > 0)) throw std::runtime_error("disc sizes must be > 0");
    if (bolts < 3 || bolts > 8) throw std::runtime_error("bolt count must be 3..8");
    if (!(pcd > bore + 16 && pcd < od * 0.6)) throw std::runtime_error("bolt circle must sit between the bore and the braking surface");
    if (drilled < 0 || drilled > 60) throw std::runtime_error("drilled holes per ring must be 0..60");
    Expr r = length(p.x, p.y);
    Expr slab = abs(p.z) - thickness / 2;
    Expr disc = max(max(r - od / 2, bore / 2 - r), slab);
    P3 qb = polar_repeat(p, bolts);
    Expr cut = length(qb.x - pcd / 2, qb.y) - 6.5;  // M12 clearance
    if (drilled > 0) {
        const double r0 = pcd / 2 + 14, r1 = od / 2 - 8;  // braking band
        P3 qa = polar_repeat(p, drilled);
        P3 qc = polar_repeat(rotate_z(p, 180.0 / drilled), drilled);
        cut = unite(cut, length(qa.x - (r0 + (r1 - r0) * 0.33), qa.y) - 3.0);
        cut = unite(cut, length(qc.x - (r0 + (r1 - r0) * 0.72), qc.y) - 3.0);
    }
    return subtract(disc, cut);
}

Expr wheel(const P3& p, double diameter, double width, int spokes, int bolts, double pcd, double bore) {
    if (!(diameter > 0 && width > 0 && bore > 0)) throw std::runtime_error("wheel sizes must be > 0");
    if (spokes < 3 || spokes > 12) throw std::runtime_error("spokes must be 3..12");
    if (bolts < 3 || bolts > 8) throw std::runtime_error("bolt count must be 3..8");
    const double R = diameter / 2, wall = std::max(3.0, 0.016 * diameter);
    const double hub_r = pcd / 2 + 22, face = std::max(10.0, 0.07 * diameter);  // spoke / hub thickness
    if (!(pcd > bore + 16 && hub_r < R * 0.6)) throw std::runtime_error("bolt circle does not fit this wheel");
    Expr r = length(p.x, p.y);
    Expr barrel = max(max(r - R, (R - wall) - r), abs(p.z) - width / 2);
    // bead flanges at both edges
    Expr lips = max(max(r - (R + 0.03 * diameter), (R - wall) - r), abs(abs(p.z) - (width / 2 - wall)) - wall);
    P3 f = translate(p, {0, 0, width / 2 - face / 2 - wall});  // the face sits towards the outside
    Expr face_slab = abs(f.z) - face / 2;
    Expr hub = max(r - hub_r, face_slab);
    P3 s = polar_repeat(p, spokes);
    const double sw = std::min(0.11 * diameter, 2.2 * hub_r * std::sin(pi / spokes));  // spoke width
    Expr spoke = max(max(abs(s.y) - sw / 2, max(hub_r * 0.8 - s.x, s.x - (R - wall / 2))), face_slab);
    spoke = max(spoke, r - R);  // radially bounded, so far-away boxes can be culled
    Expr solid = unite(unite(barrel, lips), smooth_unite(hub, spoke, 6.0));
    P3 qb = polar_repeat(p, bolts);
    Expr cut = unite(r - bore / 2, length(qb.x - pcd / 2, qb.y) - 7.0);
    return subtract(solid, max(cut, abs(f.z) - face));
}

}  // namespace dk
