// Standard engineering parts and surface treatments built on the field language:
// polar repetition, involute spur gears, ISO metric threads, drilled holes, textures.
#pragma once

#include <string>

#include "shapes.hpp"
#include "sketch.hpp"

namespace dk {

// Fold the plane around z into one of n equal sectors centred on +x (a rotation, so
// distances are preserved for shapes that stay inside their sector).
P3 polar_repeat(const P3& p, int n);

// One tooth gap profile of an involute spur gear (module m, z teeth, pressure angle in
// degrees), as a polygon in the sector frame; exposed for tests.
Polygon2 gear_tooth_polygon(double module, int teeth, double pressure_deg);

// Spur gear centred at the origin, axis z, face width `width`, optional bore (0 = none).
Expr spur_gear(const P3& p, double module, int teeth, double pressure_deg, double width, double bore);

// External ISO metric thread (60 deg V) on a rod: major diameter d, pitch, length along z,
// centred at the origin. A distance bound (Lipschitz <= 1).
Expr threaded_rod(const P3& p, double major_d, double pitch, double length);

struct HoleSpec {
    double clearance_d;   // drilled diameter
    double head_d;        // counterbore / countersink diameter
    double head_depth;    // counterbore depth
};
// ISO 273 normal-fit clearance and ISO 4762 / 10642 head sizes for "M3".."M20".
HoleSpec metric_hole(const std::string& size);

// Hole cutter in its local frame: entry at z = 0, drilling towards -z to `depth`.
// kind: "simple", "counterbore" or "countersink".
Expr hole_cutter(const P3& q, const HoleSpec& h, double depth, const std::string& kind);

// Surface texture added to a body: "knurl", "bumps" or "ribs" with a cell size and depth.
// Rescaled so the result stays a distance bound.
Expr textured(Expr body, const P3& p, const std::string& pattern, double cell, double depth);

}  // namespace dk
