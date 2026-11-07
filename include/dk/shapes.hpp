// Modeling vocabulary built on Expr: primitives, transforms, booleans, blends, shells,
// lattices. All lengths in millimetres.
//
// Distance quality, which matters for offsets, shells and sphere tracing:
//   exact SDF   sphere, box, cylinder (capped), torus, plane, rigid transforms
//   bound       union / intersection / difference (exact outside, bound inside),
//               smooth blends (bound, off by at most k/4 in the blend zone)
//   not an SDF  gyroid (normalised to approximate distance near the surface)
#pragma once

#include "expr.hpp"

namespace dk {

// A point expression: what primitives are evaluated at. Transforms produce new points.
struct P3 {
    Expr x, y, z;
};

P3 coords(Graph& g);
Expr length(Expr a, Expr b);
Expr length(Expr a, Expr b, Expr c);

// ---- primitives (centred at the origin of the point they are given)
Expr sphere(const P3& p, double radius);
Expr box(const P3& p, Vec3 size);                            // full extents
Expr rounded_box(const P3& p, Vec3 size, double radius);     // edges rounded by radius
Expr cylinder_z(const P3& p, double radius, double height);  // axis z, capped
Expr torus_z(const P3& p, double major, double minor);
Expr half_space(const P3& p, Vec3 normal, double offset);    // dot(n, p) - offset <= 0

// ---- rigid transforms (keep exact SDFs exact)
P3 translate(const P3& p, Vec3 t);                           // evaluate a shape moved by +t
P3 rotate_x(const P3& p, double degrees);
P3 rotate_y(const P3& p, double degrees);
P3 rotate_z(const P3& p, double degrees);

// ---- booleans and blends
Expr unite(Expr a, Expr b);
Expr intersect(Expr a, Expr b);
Expr subtract(Expr a, Expr b);
Expr smooth_unite(Expr a, Expr b, double k);      // polynomial smin, blend width ~k
Expr smooth_subtract(Expr a, Expr b, double k);   // concave fillet-like blend on the cut

// ---- engineering modifiers
Expr offset(Expr f, double distance);             // grow (+) or shrink (-)
Expr shell(Expr f, double thickness);             // hollow wall of given thickness, centred on the old surface
Expr shell_inward(Expr f, double thickness);      // wall grown inwards from the surface (keeps outer dims)

// ---- lattices
Expr gyroid(const P3& p, double cell, double thickness);  // sheet gyroid, approx. wall thickness

}  // namespace dk
