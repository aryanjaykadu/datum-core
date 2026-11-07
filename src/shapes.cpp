#include "dk/shapes.hpp"

#include <numbers>

namespace dk {

P3 coords(Graph& g) { return {g.x(), g.y(), g.z()}; }

Expr length(Expr a, Expr b) { return sqrt(square(a) + square(b)); }
Expr length(Expr a, Expr b, Expr c) { return sqrt(square(a) + square(b) + square(c)); }

Expr sphere(const P3& p, double r) { return length(p.x, p.y, p.z) - r; }

Expr box(const P3& p, Vec3 size) {
    // exact: distance outside = |max(q, 0)|, inside = max component of q
    Expr qx = abs(p.x) - size.x / 2, qy = abs(p.y) - size.y / 2, qz = abs(p.z) - size.z / 2;
    Expr outside = length(max(qx, 0.0), max(qy, 0.0), max(qz, 0.0));
    Expr inside = min(max(qx, max(qy, qz)), 0.0);
    return outside + inside;
}

Expr rounded_box(const P3& p, Vec3 size, double r) {
    return box(p, {size.x - 2 * r, size.y - 2 * r, size.z - 2 * r}) - r;
}

Expr cylinder_z(const P3& p, double r, double h) {
    Expr dr = length(p.x, p.y) - r;
    Expr dz = abs(p.z) - h / 2;
    return min(max(dr, dz), 0.0) + length(max(dr, 0.0), max(dz, 0.0));
}

Expr torus_z(const P3& p, double R, double r) { return length(length(p.x, p.y) - R, p.z) - r; }

Expr half_space(const P3& p, Vec3 n, double offset) {
    n = normalize(n);
    return p.x * n.x + p.y * n.y + p.z * n.z - offset;
}

P3 translate(const P3& p, Vec3 t) { return {p.x - t.x, p.y - t.y, p.z - t.z}; }

static void rot(double deg, double& c, double& s) {
    double a = deg * std::numbers::pi / 180.0;
    c = std::cos(a);
    s = std::sin(a);
}
// The shape is rotated by +deg, so the point is rotated by -deg.
P3 rotate_x(const P3& p, double deg) {
    double c, s;
    rot(deg, c, s);
    return {p.x, p.y * c + p.z * s, p.z * c - p.y * s};
}
P3 rotate_y(const P3& p, double deg) {
    double c, s;
    rot(deg, c, s);
    return {p.x * c - p.z * s, p.y, p.z * c + p.x * s};
}
P3 rotate_z(const P3& p, double deg) {
    double c, s;
    rot(deg, c, s);
    return {p.x * c + p.y * s, p.y * c - p.x * s, p.z};
}

Expr unite(Expr a, Expr b) { return min(a, b); }
Expr intersect(Expr a, Expr b) { return max(a, b); }
Expr subtract(Expr a, Expr b) { return max(a, -b); }

Expr smooth_unite(Expr a, Expr b, double k) {
    // h = max(k - |a - b|, 0) / k ;  smin = min(a, b) - h^2 k / 4
    Expr h = max(k - abs(a - b), 0.0) * (1.0 / k);
    return min(a, b) - square(h) * (k / 4);
}

Expr smooth_subtract(Expr a, Expr b, double k) {
    // -smin(-a, b) : rounds the concave edge where the cut meets the body
    return -smooth_unite(-a, b, k);
}

Expr offset(Expr f, double d) { return f - d; }
Expr shell(Expr f, double t) { return abs(f) - t / 2; }
Expr shell_inward(Expr f, double t) { return max(f, -(f + t)); }

Expr gyroid(const P3& p, double cell, double thickness) {
    double k = 2 * std::numbers::pi / cell;
    Expr x = p.x * k, y = p.y * k, z = p.z * k;
    Expr g = sin(x) * cos(y) + sin(y) * cos(z) + sin(z) * cos(x);
    // |grad g| peaks near sqrt(2)*k on the surface; dividing by it gives ~mm distance.
    double scale = 1.0 / (std::numbers::sqrt2 * k);
    return abs(g) * scale - thickness / 2;
}

}  // namespace dk
