// Scalar types the field program is evaluated over:
//   double    exact point queries (CPU, double precision)
//   Interval  conservative bounds over a box (culling, pruning)
//   Dual3     value + exact gradient (forward-mode autodiff) for normals / Hermite data
#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dk {

struct Vec3 {
    double x = 0, y = 0, z = 0;
    constexpr Vec3() = default;
    constexpr Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
    constexpr double operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
    double& operator[](int i) { return i == 0 ? x : i == 1 ? y : z; }
};
constexpr Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
constexpr Vec3 operator*(double s, Vec3 a) { return a * s; }
constexpr double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
    double l = length(a);
    return l > 0 ? a * (1.0 / l) : Vec3{0, 0, 0};
}

// ---------------------------------------------------------------- Interval
// Outward rounding is not applied: bounds are conservative to ~1 ulp, which is far
// below meshing resolution. Do not use these bounds for exact predicates.
struct Interval {
    double lo = 0, hi = 0;
    constexpr Interval() = default;
    constexpr Interval(double v) : lo(v), hi(v) {}
    constexpr Interval(double l, double h) : lo(l), hi(h) {}
    constexpr bool contains(double v) const { return lo <= v && v <= hi; }
    constexpr double width() const { return hi - lo; }
};
constexpr Interval operator+(Interval a, Interval b) { return {a.lo + b.lo, a.hi + b.hi}; }
constexpr Interval operator-(Interval a, Interval b) { return {a.lo - b.hi, a.hi - b.lo}; }
constexpr Interval operator-(Interval a) { return {-a.hi, -a.lo}; }
inline Interval operator*(Interval a, Interval b) {
    double p[4] = {a.lo * b.lo, a.lo * b.hi, a.hi * b.lo, a.hi * b.hi};
    return {*std::min_element(p, p + 4), *std::max_element(p, p + 4)};
}
inline Interval operator/(Interval a, Interval b) {
    if (b.contains(0.0)) return {-INFINITY, INFINITY};
    return a * Interval{1.0 / b.hi, 1.0 / b.lo};
}
inline Interval min(Interval a, Interval b) { return {std::min(a.lo, b.lo), std::min(a.hi, b.hi)}; }
inline Interval max(Interval a, Interval b) { return {std::max(a.lo, b.lo), std::max(a.hi, b.hi)}; }
inline Interval abs(Interval a) {
    if (a.lo >= 0) return a;
    if (a.hi <= 0) return -a;
    return {0.0, std::max(-a.lo, a.hi)};
}
inline Interval square(Interval a) {
    Interval m = abs(a);
    return {m.lo * m.lo, m.hi * m.hi};
}
inline Interval sqrt(Interval a) { return {std::sqrt(std::max(a.lo, 0.0)), std::sqrt(std::max(a.hi, 0.0))}; }
inline Interval sin(Interval a) {
    constexpr double two_pi = 2 * std::numbers::pi;
    if (a.width() >= two_pi) return {-1, 1};
    double lo = std::min(std::sin(a.lo), std::sin(a.hi));
    double hi = std::max(std::sin(a.lo), std::sin(a.hi));
    // does [a.lo, a.hi] contain pi/2 + 2k pi (max) or 3pi/2 + 2k pi (min)?
    auto hits = [&](double phase) { return std::ceil((a.lo - phase) / two_pi) <= std::floor((a.hi - phase) / two_pi); };
    if (hits(std::numbers::pi / 2)) hi = 1;
    if (hits(3 * std::numbers::pi / 2)) lo = -1;
    return {lo, hi};
}
inline Interval cos(Interval a) { return sin(a + Interval{std::numbers::pi / 2}); }
inline Interval copysign(Interval mag, Interval sign) {
    Interval m = abs(mag);
    if (sign.lo > 0) return m;
    if (sign.hi < 0) return -m;
    return {-m.hi, m.hi};
}

inline Interval floor(Interval a) { return {std::floor(a.lo), std::floor(a.hi)}; }
// atan2 over a box: exact at the corners unless the box touches the origin or the branch cut
inline Interval atan2(Interval y, Interval x) {
    constexpr double pi = std::numbers::pi;
    if (x.lo <= 0 && y.lo <= 0 && y.hi >= 0) return {-pi, pi};
    double c[4] = {std::atan2(y.lo, x.lo), std::atan2(y.lo, x.hi), std::atan2(y.hi, x.lo), std::atan2(y.hi, x.hi)};
    return {std::min({c[0], c[1], c[2], c[3]}), std::max({c[0], c[1], c[2], c[3]})};
}

// ---------------------------------------------------------------- Dual3
struct Dual3 {
    double v = 0;
    Vec3 d{};
    constexpr Dual3() = default;
    constexpr Dual3(double val) : v(val) {}
    constexpr Dual3(double val, Vec3 grad) : v(val), d(grad) {}
};
constexpr Dual3 operator+(Dual3 a, Dual3 b) { return {a.v + b.v, a.d + b.d}; }
constexpr Dual3 operator-(Dual3 a, Dual3 b) { return {a.v - b.v, a.d - b.d}; }
constexpr Dual3 operator-(Dual3 a) { return {-a.v, a.d * -1.0}; }
constexpr Dual3 operator*(Dual3 a, Dual3 b) { return {a.v * b.v, a.d * b.v + b.d * a.v}; }
inline Dual3 operator/(Dual3 a, Dual3 b) { return {a.v / b.v, (a.d * b.v - b.d * a.v) * (1.0 / (b.v * b.v))}; }
inline Dual3 min(Dual3 a, Dual3 b) { return a.v <= b.v ? a : b; }
inline Dual3 max(Dual3 a, Dual3 b) { return a.v >= b.v ? a : b; }
inline Dual3 abs(Dual3 a) { return a.v >= 0 ? a : -a; }
inline Dual3 square(Dual3 a) { return a * a; }
inline Dual3 sqrt(Dual3 a) {
    double s = std::sqrt(std::max(a.v, 0.0));
    return {s, s > 0 ? a.d * (0.5 / s) : Vec3{}};
}
inline Dual3 sin(Dual3 a) { return {std::sin(a.v), a.d * std::cos(a.v)}; }
inline Dual3 cos(Dual3 a) { return {std::cos(a.v), a.d * -std::sin(a.v)}; }
inline Dual3 floor(Dual3 a) { return {std::floor(a.v)}; }
inline Dual3 atan2(Dual3 y, Dual3 x) {
    double r2 = x.v * x.v + y.v * y.v;
    return {std::atan2(y.v, x.v), r2 > 0 ? (y.d * x.v - x.d * y.v) * (1.0 / r2) : Vec3{}};
}
inline Dual3 copysign(Dual3 mag, Dual3 sign) {
    double s = sign.v < 0 ? -1.0 : 1.0;
    Dual3 m = abs(mag);
    return {s * m.v, m.d * s};
}

using std::abs;
using std::atan2;
using std::floor;
using std::cos;
using std::sin;
using std::sqrt;
inline double square(double a) { return a * a; }
inline double copysign(double mag, double sign) { return sign < 0 ? -std::abs(mag) : std::abs(mag); }
inline double min(double a, double b) { return a < b ? a : b; }
inline double max(double a, double b) { return a > b ? a : b; }

}  // namespace dk
