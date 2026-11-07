// 2D sketch profiles and the solids made from them.
//
// A profile is a closed polygon in the sketch plane (local X, Y; mm). Each vertex may carry
// a fillet radius; filleted corners are tessellated into arcs (sagitta < 0.5 % of r).
//
// Field of a profile: the exact unsigned distance to its boundary (min over edges of the
// segment distance), signed by an inside test built from an ear-clipping triangulation
// (min over triangles of max over edge half-planes) through copysign(). The result is an
// exact signed distance for any simple polygon, convex or not.
#pragma once

#include <array>
#include <vector>

#include "shapes.hpp"

namespace dk {

struct ProfilePoint { double x, y, r = 0; };
using Polygon2 = std::vector<std::array<double, 2>>;

// Corners filleted and orientation made counter-clockwise. Throws std::runtime_error for
// fewer than 3 points, zero area, or a self-intersecting outline.
Polygon2 tessellate_profile(const std::vector<ProfilePoint>& profile);

// Signed distance of a counter-clockwise simple polygon in the (u, v) plane.
Expr polygon_sdf(Expr u, Expr v, const Polygon2& poly);
std::vector<std::array<int, 3>> triangulate(const Polygon2& poly);  // ear clipping
double polygon_area(const Polygon2& poly);                          // signed, CCW > 0

// Extrude along local +Z from 0 to `distance` (or centred on the sketch plane).
Expr extrude_profile(const P3& p, const Polygon2& poly, double distance, bool symmetric);
// Revolve about the local Y axis (profile must stay at x >= 0), sweeping `angle_deg`.
// Loft: the 2D field blends linearly from `bottom` at z = 0 to `top` at z = height. A distance
// bound: scaled by the worst slope |sA - sB| / height, which is at most the profiles' joint extent.
Expr loft_profiles(const P3& p, const Polygon2& bottom, const Polygon2& top, double height);
Expr revolve_profile(const P3& p, const Polygon2& poly, double angle_deg);

}  // namespace dk
