#include "dk/sketch.hpp"

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>

namespace dk {

double polygon_area(const Polygon2& poly) {
    double a = 0;
    for (size_t i = 0, n = poly.size(); i < n; ++i) {
        auto& p = poly[i];
        auto& q = poly[(i + 1) % n];
        a += p[0] * q[1] - q[0] * p[1];
    }
    return a / 2;
}

static double cross2(std::array<double, 2> o, std::array<double, 2> a, std::array<double, 2> b) {
    return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
}

static bool segments_cross(std::array<double, 2> a, std::array<double, 2> b, std::array<double, 2> c, std::array<double, 2> d) {
    double d1 = cross2(c, d, a), d2 = cross2(c, d, b), d3 = cross2(a, b, c), d4 = cross2(a, b, d);
    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0)) && d1 != 0 && d2 != 0 && d3 != 0 && d4 != 0;
}

Polygon2 tessellate_profile(const std::vector<ProfilePoint>& profile) {
    if (profile.size() < 3) throw std::runtime_error("a profile needs at least 3 points");
    const size_t n = profile.size();
    // base polygon, CCW
    std::vector<ProfilePoint> pts = profile;
    Polygon2 raw;
    for (auto& p : pts) raw.push_back({p.x, p.y});
    if (std::abs(polygon_area(raw)) < 1e-9) throw std::runtime_error("the profile has no area");
    if (polygon_area(raw) < 0) std::reverse(pts.begin(), pts.end());
    for (size_t i = 0; i < n; ++i) {
        auto& a = pts[i];
        auto& b = pts[(i + 1) % n];
        if (std::hypot(b.x - a.x, b.y - a.y) < 1e-9) throw std::runtime_error("the profile has two coincident points");
    }

    Polygon2 out;
    for (size_t i = 0; i < n; ++i) {
        const auto& prev = pts[(i + n - 1) % n];
        const auto& cur = pts[i];
        const auto& next = pts[(i + 1) % n];
        if (cur.r <= 0) {
            out.push_back({cur.x, cur.y});
            continue;
        }
        double ux = prev.x - cur.x, uy = prev.y - cur.y, vx = next.x - cur.x, vy = next.y - cur.y;
        double lu = std::hypot(ux, uy), lv = std::hypot(vx, vy);
        ux /= lu, uy /= lu, vx /= lv, vy /= lv;
        double cosang = std::clamp(ux * vx + uy * vy, -1.0, 1.0);
        double half = std::acos(cosang) / 2;  // half the interior angle between the edges
        if (half < 1e-6 || std::abs(half - std::numbers::pi / 2) < 1e-9) {
            out.push_back({cur.x, cur.y});  // straight or degenerate corner: nothing to round
            continue;
        }
        double t = cur.r / std::tan(half);  // tangent distance from the corner
        double tmax = 0.5 * std::min(lu, lv);
        double r = cur.r;
        if (t > tmax) {  // radius too large for the adjacent edges: shrink to fit
            t = tmax;
            r = t * std::tan(half);
        }
        double ax = cur.x + ux * t, ay = cur.y + uy * t;  // tangent points
        double bx = cur.x + vx * t, by = cur.y + vy * t;
        double bis_x = ux + vx, bis_y = uy + vy, bl = std::hypot(bis_x, bis_y);
        double d = r / std::sin(half);
        double cx = cur.x + bis_x / bl * d, cy = cur.y + bis_y / bl * d;  // arc centre
        double a0 = std::atan2(ay - cy, ax - cx), a1 = std::atan2(by - cy, bx - cx);
        double sweep = a1 - a0;
        while (sweep > std::numbers::pi) sweep -= 2 * std::numbers::pi;
        while (sweep < -std::numbers::pi) sweep += 2 * std::numbers::pi;
        // segments so the sagitta stays below 0.5 % of r
        int segs = std::max(2, int(std::ceil(std::abs(sweep) / (2 * std::acos(1 - 0.005)))));
        for (int k = 0; k <= segs; ++k) {
            double ang = a0 + sweep * k / segs;
            out.push_back({cx + r * std::cos(ang), cy + r * std::sin(ang)});
        }
    }
    // simple polygon check (non-adjacent edges must not cross)
    const size_t m = out.size();
    for (size_t i = 0; i < m; ++i)
        for (size_t j = i + 2; j < m; ++j) {
            if (i == 0 && j == m - 1) continue;
            if (segments_cross(out[i], out[(i + 1) % m], out[j], out[(j + 1) % m]))
                throw std::runtime_error("the profile crosses itself");
        }
    return out;
}

std::vector<std::array<int, 3>> triangulate(const Polygon2& poly) {
    std::vector<int> idx(poly.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = int(i);
    std::vector<std::array<int, 3>> tris;
    auto inside = [&](int a, int b, int c, int p) {
        return cross2(poly[a], poly[b], poly[p]) >= 0 && cross2(poly[b], poly[c], poly[p]) >= 0 && cross2(poly[c], poly[a], poly[p]) >= 0;
    };
    size_t guard = 0;
    while (idx.size() > 3 && guard++ < 100000) {
        bool clipped = false;
        for (size_t i = 0; i < idx.size(); ++i) {
            int a = idx[(i + idx.size() - 1) % idx.size()], b = idx[i], c = idx[(i + 1) % idx.size()];
            if (cross2(poly[a], poly[b], poly[c]) <= 1e-12) continue;  // reflex or flat corner
            bool ear = true;
            for (int p : idx)
                if (p != a && p != b && p != c && inside(a, b, c, p)) {
                    ear = false;
                    break;
                }
            if (!ear) continue;
            tris.push_back({a, b, c});
            idx.erase(idx.begin() + long(i));
            clipped = true;
            break;
        }
        if (!clipped) break;  // numerically degenerate remainder
    }
    if (idx.size() == 3) tris.push_back({idx[0], idx[1], idx[2]});
    return tris;
}

Expr polygon_sdf(Expr u, Expr v, const Polygon2& poly) {
    const size_t n = poly.size();
    // exact unsigned distance to the boundary
    Expr dist;
    for (size_t i = 0; i < n; ++i) {
        auto a = poly[i];
        auto b = poly[(i + 1) % n];
        double ex = b[0] - a[0], ey = b[1] - a[1], len2 = ex * ex + ey * ey;
        Expr wx = u - a[0], wy = v - a[1];
        Expr h = min(max((wx * ex + wy * ey) * (1.0 / len2), 0.0), 1.0);
        Expr d = length(wx - h * ex, wy - h * ey);
        dist = i == 0 ? d : min(dist, d);
    }
    // inside test: union of triangles, each the intersection of its edge half-planes
    Expr sign;
    bool first = true;
    for (auto& t : triangulate(poly)) {
        Expr tri;
        for (int k = 0; k < 3; ++k) {
            auto a = poly[t[k]];
            auto b = poly[t[(k + 1) % 3]];
            double ex = b[0] - a[0], ey = b[1] - a[1], l = std::hypot(ex, ey);
            // > 0 outside (CCW). Written as u*nx + v*ny - c rather than reusing (u - a.x):
            // sharing those with the distance part would keep every vertex's offsets alive
            // across the whole program and multiply the GPU registers needed.
            double nx = ey / l, ny = -ex / l;
            Expr hp = u * nx + v * ny - (a[0] * nx + a[1] * ny);
            tri = k == 0 ? hp : max(tri, hp);
        }
        sign = first ? tri : min(sign, tri);
        first = false;
    }
    return copysign(dist, sign);
}

Expr extrude_profile(const P3& p, const Polygon2& poly, double distance, bool symmetric) {
    Expr d2 = polygon_sdf(p.x, p.y, poly);
    double c = symmetric ? 0.0 : distance / 2;
    Expr dz = abs(p.z - c) - distance / 2;
    // exact combination, like a box: outside corner distance + inside max
    return min(max(d2, dz), 0.0) + length(max(d2, 0.0), max(dz, 0.0));
}

Expr revolve_profile(const P3& p, const Polygon2& poly, double angle_deg) {
    Expr radial = length(p.x, p.z);
    Expr body = polygon_sdf(radial, p.y, poly);
    if (angle_deg >= 360 - 1e-9) return body;
    // sweep from +X toward +Z by angle_deg: intersect with the angular wedge (a bound field)
    double th = angle_deg * std::numbers::pi / 180;
    Expr start = -p.z;                                                 // keep z >= 0
    Expr end = p.x * -std::sin(th) + p.z * std::cos(th);               // keep the side before theta
    Expr wedge = th <= std::numbers::pi ? max(start, end) : min(start, end);
    return max(body, wedge);
}

}  // namespace dk
