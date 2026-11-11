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

// ---- aerospace
// NACA 4-digit section ("2412": 2 % camber at 40 % chord, 12 % thick), chord along +x from the
// leading edge at the origin, counter-clockwise, with the standard finite trailing edge.
Polygon2 naca4_polygon(const std::string& code, double chord, int points_per_side = 10);

// Half wing: root section at z = 0, tip at z = span. The tip chord is root * taper, moved
// back by sweep (deg, leading edge) and up by dihedral (deg). A distance bound.
Expr wing(const P3& p, const std::string& naca, double root_chord, double taper, double span, double sweep_deg, double dihedral_deg);

// Solid nose cone revolved about +z: base radius at z = 0, tip at z = length.
// shape: "ogive", "conical", "elliptical" or "parabolic".
Expr nose_cone(const P3& p, const std::string& shape, double diameter, double length);

// ---- machine elements
// Compression spring about z, centred: mean coil diameter, wire diameter, free length, turns.
// Ends are cut flat. A conservative distance bound.
Expr coil_spring(const P3& p, double mean_d, double wire_d, double length, double turns);

struct BearingSize { double bore, od, width; };
BearingSize bearing_size(const std::string& designation);  // "608", "6000".."6205" (ISO 15 dimensions)
// Deep-groove ball bearing about z: two rings and a full set of balls.
Expr ball_bearing(const P3& p, const BearingSize& s);

struct BeamSize { double h, b, tw, tf; };
BeamSize ipe_size(const std::string& name);  // "IPE 80".."IPE 200" (EN 10365)
// Cross-section polygon in (x, y), centred on the bounding box: "I-beam", "C-channel",
// "L-angle" or "T-bar".
Polygon2 beam_polygon(const std::string& kind, const BeamSize& s);
// Structural member along z, centred. kind may also be "Rect tube" (wall = tw).
Expr beam(const P3& p, const std::string& kind, const BeamSize& s, double length);

// ---- automotive
// Brake rotor about z: outer diameter, thickness, centre bore, wheel bolts on a pitch circle,
// and `drilled` cross-drilled holes per ring (0 = plain), two staggered rings.
Expr brake_disc(const P3& p, double od, double thickness, double bore, int bolts, double pcd, int drilled);

// Spoked wheel about z: barrel of the given diameter and width, hub, spokes, bolt circle.
Expr wheel(const P3& p, double diameter, double width, int spokes, int bolts, double pcd, double bore);

}  // namespace dk
