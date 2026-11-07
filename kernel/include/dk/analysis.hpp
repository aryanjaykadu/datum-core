// Engineering analyses on top of the field and its mesh. Each result says what it is:
// these are screening models, not certified analysis.
#pragma once

#include <string>
#include <vector>

#include "mesh.hpp"

namespace dk {

struct Material {
    std::string key, name;
    double density_kg_m3, youngs_gpa, yield_mpa, ultimate_mpa;
    double usd_per_kg;          // stock price (reference table, not market data)
    double carbon_kg_per_kg;    // ICE v3 cradle-to-gate
    double mrr_mm3_s;           // roughing material removal rate, 3-axis VMC
};
const std::vector<Material>& materials();
const Material& material(const std::string& key);  // falls back to Al 6061-T6

// Cavities and inclusions: closed shells of the mesh that sit inside the main body.
// Small ones near the skin are how a model can be booby-trapped for fatigue failure.
struct ShellReport {
    size_t shells = 0;
    struct Finding { Vec3 centre_mm; double volume_mm3; double size_mm; bool cavity; bool suspicious; bool designed = false; };
    std::vector<Finding> findings;
    bool suspicious() const;
};
ShellReport scan_shells(const Mesh& m, const MassProperties& whole);

// Which setups (+/-X, +/-Y, +/-Z tool approach) can reach the surface. A face sample is
// reachable from a direction if it faces the spindle and a ray toward the spindle never
// re-enters the part (sphere tracing on the exact field).
struct Accessibility {
    double reachable_pct[6] = {};  // area share reachable per direction: +X -X +Y -Y +Z -Z
    double covered_pct = 0;        // reachable from at least one direction
    int setups = 0;                // fewest directions that cover >= 99% of what is reachable
};
Accessibility tool_access(const Mesh& m, const Program& f, size_t samples = 4000);

// Drop-impact screen: energy balance on the part as a cantilever along its longest axis
// (U = m g h = 1/2 k d^2, k = 3EI/L^3), stress from the tip load at the root.
struct ImpactResult { double deflection_mm, stress_mpa; bool fracture; double impact_energy_j; };
ImpactResult impact_screen(const Mesh& m, const Material& mat, double impactor_kg = 5, double drop_m = 0.5);

struct Quote {
    double price_usd, lead_days, stock_kg, machining_h, carbon_kg, fee_usd;
    int setups;
    std::string provider;  // "datum-estimate" until a manufacturing partner API is configured
};
Quote estimate_quote(const MassProperties& mp, const Vec3& size_mm, const Accessibility& acc, const Material& mat, int quantity = 1);

constexpr double kPlatformFeeRate = 0.015;  // applies only to orders placed through a partner

}  // namespace dk
