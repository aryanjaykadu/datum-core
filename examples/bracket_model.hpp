// Parametric L-bracket used by the demos: exact notch fillet, gyroid-filled gusset,
// bolt holes with edge breaks. All lengths in mm.
#pragma once

#include "dk/shapes.hpp"

namespace dk {

struct BracketParams {
    double length = 80, width = 40, thickness = 6;  // base plate
    double height = 50;                             // upright
    double fillet = 6;                              // inner fillet radius (exact)
    double hole_d = 6.6;                            // M6 clearance
    double cell = 2 * 7.0, wall = 0.8;              // gyroid lattice in the gusset
};

inline Expr build_bracket(Graph& g, const BracketParams& b) {
    P3 p = coords(g);
    // L-profile = envelope box minus the inside "notch" quadrant whose corner is rounded by
    // the fillet radius. This is an exact constant-radius fillet. smooth_unite(base,
    // upright) would also round the corner, but it bulges k/4 out of every face the two
    // boxes share (the bottom and the back), adding material that is not in the design.
    Expr envelope = box(translate(p, {0, 0, b.height / 2}), {b.length, b.width, b.height});
    const double big = 4 * (b.length + b.height);
    double xi = -b.length / 2 + b.thickness, zi = b.thickness;  // inner corner of the L
    Expr notch = rounded_box(translate(p, {xi + big / 2, 0, zi + big / 2}), {big, 4 * big, big}, b.fillet);
    Expr body = subtract(envelope, notch);

    // triangular gusset = box cut by an inclined half-space, hollowed and lattice-filled
    double gx = -b.length / 2 + b.thickness, span = 34;
    Expr gusset_box = box(translate(p, {gx + span / 2, 0, b.thickness + span / 2}), {span, 10, span});
    Expr diagonal = half_space(translate(p, {gx, 0, b.thickness + span}), {1, 0, 1}, 0);
    Expr gusset = intersect(gusset_box, diagonal);
    Expr skin = shell_inward(gusset, 1.2);
    Expr core = intersect(gyroid(p, b.cell, b.wall), offset(gusset, -1.0));
    body = smooth_unite(body, unite(skin, core), 2);

    // bolt holes in the base, two in the upright, with a small edge-break blend
    for (double x : {10.0, 30.0})
        for (double y : {-12.0, 12.0}) body = smooth_subtract(body, cylinder_z(translate(p, {x, y, 0}), b.hole_d / 2, 40), 1.0);
    for (double z : {30.0, 42.0}) {
        P3 q = rotate_y(translate(p, {-b.length / 2, 0, z}), 90);
        body = smooth_subtract(body, cylinder_z(q, b.hole_d / 2, 40), 1.0);
    }
    return body;
}

}  // namespace dk
