// The parametric document: a DAG of features stored as JSON (the .datum format body).
//
//   { "version": 1, "units": "mm", "material": "al6061t6", "root": "body",
//     "features": [ { "id": "base", "type": "box", "name": "Base plate",
//                     "params": { "size": [80, 40, 6], "center": [0, 0, 3] } }, ... ] }
//
// Every feature has `id`, `type`, optional `name`, `params`, `inputs` (ids of other features)
// and `suppressed`. Primitives accept `center` [mm] and `rotation` [deg, applied X then Y
// then Z]. Building is a pure function of the JSON, and the expression graph hash-conses,
// so an edit only creates nodes for what actually changed.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "../../third_party/nlohmann/json.hpp"
#include "robot.hpp"
#include "shapes.hpp"

namespace dk {

using json = nlohmann::json;

struct FeatureInfo {
    std::string id, type, name;
    bool suppressed = false;
    std::vector<std::string> inputs;
};

struct BuildResult {
    Expr root;
    std::vector<FeatureInfo> features;  // in document order
    size_t dag_nodes = 0;               // features evaluated (reachable from root, not suppressed)
    std::vector<std::string> warnings;
    // Regions where voids are intended (the core of each lattice infill): body field + skin.
    // Cavities found inside them are designed, not hidden defects.
    std::vector<std::pair<Expr, double>> designed_void_regions;
};

// Throws std::runtime_error with a user-readable message on invalid documents
// (unknown type, missing input, cycle, bad parameter).
BuildResult build_document(Graph& g, const json& doc);

// Feature types the UI can offer, with their parameter schema (for the property editor).
json feature_catalog();

// A robot definition from JSON. Two formats, lengths in mm and angles in degrees:
//   {"format": "dh", "joints": [{"d", "a", "alpha_deg", "lo_deg", "hi_deg"}, ...]}
//   {"format": "chain", "joints": [{"origins": [{"xyz": [..], "rpy_deg": [..]}, ...],
//                                   "axis": [x, y, z], "lo_deg", "hi_deg"}, ...],
//    "tool": {"origins": [...]}}           (chain = URDF semantics: origin, then rotate)
// plus optional "name", "link_radius", "mount_offset". Throws on invalid definitions.
Robot robot_from_json(const json& def, const std::string& key = "custom");

// A starter document: the demo L-bracket expressed as features.
json example_bracket();

}  // namespace dk
