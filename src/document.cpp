#include "dk/document.hpp"

#include "dk/robot.hpp"
#include "dk/meshfield.hpp"
#include "dk/sketch.hpp"

#include <filesystem>

#include <functional>
#include <numbers>
#include <set>
#include <stdexcept>
#include <tuple>

namespace dk {
namespace {

struct Builder {
    Graph& g;
    const json* doc = nullptr;
    std::map<std::string, const json*> by_id;
    std::map<std::tuple<std::string, uint32_t, uint32_t, uint32_t>, Expr> cache;
    std::set<std::string> visiting, evaluated;
    std::vector<std::pair<Expr, double>> lattice_regions;
    std::vector<std::string> warnings;

    [[noreturn]] static void fail(const std::string& id, const std::string& msg) {
        throw std::runtime_error("feature '" + id + "': " + msg);
    }

    static double num(const json& params, const char* key, double fallback) {
        auto it = params.find(key);
        if (it == params.end() || it->is_null()) return fallback;
        if (!it->is_number()) throw std::runtime_error(std::string("parameter '") + key + "' must be a number");
        return it->get<double>();
    }
    static Vec3 vec(const json& params, const char* key, Vec3 fallback) {
        auto it = params.find(key);
        if (it == params.end() || it->is_null()) return fallback;
        if (!it->is_array() || it->size() != 3) throw std::runtime_error(std::string("parameter '") + key + "' must be [x, y, z]");
        return {(*it)[0].get<double>(), (*it)[1].get<double>(), (*it)[2].get<double>()};
    }
    static double positive(const json& params, const char* key, double fallback, const std::string& id) {
        double v = num(params, key, fallback);
        if (!(v > 0)) fail(id, std::string("'") + key + "' must be > 0");
        return v;
    }

    // world point -> feature-local point for center + rotation (X, then Y, then Z)
    static P3 local(const P3& p, const json& params) {
        Vec3 c = vec(params, "center", {0, 0, 0});
        Vec3 r = vec(params, "rotation", {0, 0, 0});
        P3 q = translate(p, c);
        if (r.z != 0) q = rotate_z(q, r.z);
        if (r.y != 0) q = rotate_y(q, r.y);
        if (r.x != 0) q = rotate_x(q, r.x);
        return q;
    }

    std::vector<std::string> inputs_of(const json& f) const {
        std::vector<std::string> out;
        if (auto it = f.find("inputs"); it != f.end())
            for (auto& v : *it) out.push_back(v.get<std::string>());
        return out;
    }
    static bool suppressed(const json& f) { return f.value("suppressed", false); }

    Expr build(const std::string& id, const P3& p) {
        auto key = std::make_tuple(id, p.x.id(), p.y.id(), p.z.id());
        if (auto it = cache.find(key); it != cache.end()) return it->second;
        auto fit = by_id.find(id);
        if (fit == by_id.end()) throw std::runtime_error("unknown feature id '" + id + "'");
        if (visiting.count(id)) fail(id, "is part of a dependency cycle");
        visiting.insert(id);
        Expr e = build_feature(*fit->second, id, p);
        visiting.erase(id);
        evaluated.insert(id);
        cache.emplace(key, e);
        return e;
    }

    // live (non-suppressed) inputs; a suppressed input is skipped, as in feature-based CAD
    std::vector<std::string> live_inputs(const json& f) {
        std::vector<std::string> out;
        for (auto& in : inputs_of(f)) {
            auto it = by_id.find(in);
            if (it == by_id.end()) throw std::runtime_error("feature '" + f.value("id", std::string()) + "' uses unknown input '" + in + "'");
            if (!suppressed(*it->second)) out.push_back(in);
        }
        return out;
    }

    Expr single_input(const json& f, const std::string& id, const P3& p) {
        auto in = live_inputs(f);
        if (in.size() != 1) fail(id, "needs exactly one active input");
        return build(in[0], p);
    }

    Expr build_feature(const json& f, const std::string& id, const P3& p) {
        const std::string type = f.value("type", std::string());
        const json params = f.value("params", json::object());

        if (type == "box") {
            Vec3 s = vec(params, "size", {10, 10, 10});
            if (s.x <= 0 || s.y <= 0 || s.z <= 0) fail(id, "size must be positive");
            double r = num(params, "radius", 0);
            if (r < 0 || 2 * r >= std::min({s.x, s.y, s.z})) fail(id, "edge radius must be < half the smallest side");
            P3 q = local(p, params);
            return r > 0 ? rounded_box(q, s, r) : box(q, s);
        }
        if (type == "sphere") return sphere(local(p, params), positive(params, "radius", 5, id));
        if (type == "cylinder") {
            P3 q = local(p, params);
            std::string axis = params.value("axis", std::string("z"));
            if (axis == "x") q = rotate_y(q, -90);
            else if (axis == "y") q = rotate_x(q, 90);
            else if (axis != "z") fail(id, "axis must be x, y or z");
            return cylinder_z(q, positive(params, "radius", 5, id), positive(params, "height", 10, id));
        }
        if (type == "torus") {
            double R = positive(params, "major", 10, id), r = positive(params, "minor", 2, id);
            if (r >= R) fail(id, "minor radius must be smaller than major radius");
            return torus_z(local(p, params), R, r);
        }
        if (type == "half_space") return half_space(local(p, params), vec(params, "normal", {0, 0, 1}), num(params, "offset", 0));

        if (type == "union" || type == "intersect" || type == "subtract") {
            auto in = live_inputs(f);
            if (in.empty()) fail(id, "needs at least one active input");
            double k = num(params, "blend", 0);
            if (k < 0) fail(id, "blend must be >= 0");
            Expr acc = build(in[0], p);
            for (size_t i = 1; i < in.size(); ++i) {
                Expr b = build(in[i], p);
                if (type == "union") acc = k > 0 ? smooth_unite(acc, b, k) : unite(acc, b);
                else if (type == "intersect") acc = k > 0 ? -smooth_unite(-acc, -b, k) : intersect(acc, b);
                else acc = k > 0 ? smooth_subtract(acc, b, k) : subtract(acc, b);
            }
            return acc;
        }
        if (type == "shell") {
            Expr body = single_input(f, id, p);
            double t = positive(params, "thickness", 1, id);
            return params.value("mode", std::string("inward")) == "centered" ? shell(body, t) : shell_inward(body, t);
        }
        if (type == "offset") return offset(single_input(f, id, p), num(params, "distance", 0));
        if (type == "transform") {
            P3 q = local(p, params);  // same center/rotation convention as primitives
            return single_input(f, id, q);
        }
        if (type == "mirror") {
            std::string plane = params.value("plane", std::string("yz"));
            P3 m = p;
            if (plane == "yz") m.x = -p.x;
            else if (plane == "xz") m.y = -p.y;
            else if (plane == "xy") m.z = -p.z;
            else fail(id, "plane must be yz, xz or xy");
            return unite(single_input(f, id, p), single_input(f, id, m));
        }
        if (type == "pattern_linear") {
            int n = int(num(params, "count", 2));
            if (n < 1 || n > 64) fail(id, "count must be 1..64");
            Vec3 step = vec(params, "step", {10, 0, 0});
            Expr acc = single_input(f, id, p);
            for (int i = 1; i < n; ++i) acc = unite(acc, single_input(f, id, translate(p, step * double(i))));
            return acc;
        }
        if (type == "pattern_circular") {
            int n = int(num(params, "count", 6));
            if (n < 1 || n > 128) fail(id, "count must be 1..128");
            double total = num(params, "angle", 360);
            double step = (std::abs(total - 360) < 1e-9) ? 360.0 / n : (n > 1 ? total / (n - 1) : 0);
            Expr acc = single_input(f, id, p);
            for (int i = 1; i < n; ++i) acc = unite(acc, single_input(f, id, rotate_z(p, step * i)));
            return acc;
        }
        if (type == "import_mesh") {
            std::string path = params.value("path", std::string());
            if (path.empty()) fail(id, "no mesh file");
            int cells = int(num(params, "resolution", 128));
            if (cells < 16 || cells > 400) fail(id, "resolution must be 16..400 cells");
            std::error_code ec;
            auto stamp = std::filesystem::last_write_time(std::filesystem::path(path), ec);
            if (ec) fail(id, "mesh file not found: " + path);
            // bake once per (file, version, resolution) for the life of the engine
            static std::map<std::string, int> baked;
            std::string key = path + "|" + std::to_string(static_cast<long long>(stamp.time_since_epoch().count())) + "|" + std::to_string(cells);
            auto it = baked.find(key);
            if (it == baked.end()) {
                try {
                    auto grid = std::make_shared<const SdfGrid>(bake_sdf(load_mesh(path), cells));
                    it = baked.emplace(key, register_grid(grid)).first;
                } catch (const std::exception& e) {
                    fail(id, e.what());
                }
            }
            P3 q = local(p, params);
            return sample_grid(it->second, q.x, q.y, q.z);
        }
        if (type == "extrude" || type == "revolve") {
            std::vector<ProfilePoint> prof;
            auto it = params.find("profile");
            if (it == params.end() || !it->is_array()) fail(id, "needs a profile (list of [x, y] or [x, y, fillet] points)");
            for (auto& pt : *it) {
                if (!pt.is_array() || pt.size() < 2) fail(id, "profile points must be [x, y] or [x, y, fillet]");
                double r = pt.size() > 2 ? pt[2].get<double>() : 0.0;
                if (r < 0) fail(id, "fillet radii must be >= 0");
                prof.push_back({pt[0].get<double>(), pt[1].get<double>(), r});
            }
            Polygon2 poly;
            try {
                poly = tessellate_profile(prof);
            } catch (const std::exception& e) {
                fail(id, e.what());
            }
            P3 q = local(p, params);
            if (type == "extrude") {
                double dist = positive(params, "distance", 10, id);
                return extrude_profile(q, poly, dist, params.value("direction", std::string("one side")) == "symmetric");
            }
            for (auto& v : poly)
                if (v[0] < -1e-9) fail(id, "a revolve profile must stay on one side of the axis (x >= 0)");
            double angle = num(params, "angle", 360);
            if (!(angle > 0 && angle <= 360)) fail(id, "revolve angle must be in (0, 360]");
            return revolve_profile(q, poly, angle);
        }
        if (type == "robot_arm") {
            std::string key = params.value("preset", std::string("ur5"));
            const json* custom = nullptr;
            if (doc && doc->contains("robots") && (*doc)["robots"].contains(key)) custom = &(*doc)["robots"][key];
            Robot robot = custom ? robot_from_json(*custom, key) : robot_preset(key);
            const Robot& r = robot;
            robot.link_radius = num(params, "link_radius", r.link_radius);
            if (robot.link_radius <= 0) fail(id, "link radius must be > 0");
            Vec3 qa = vec(params, "joints_a", {0, -90, 0}), qb = vec(params, "joints_b", {-90, 0, 0});
            const double k = std::numbers::pi / 180.0;
            std::vector<double> q = {qa.x * k, qa.y * k, qa.z * k, qb.x * k, qb.y * k, qb.z * k};
            // arms with more than 6 joints take the rest from "joints_more" (degrees)
            if (auto it = params.find("joints_more"); it != params.end() && it->is_array())
                for (auto& v : *it) q.push_back(v.get<double>() * k);
            q.resize(robot.joints.size(), 0.0);
            for (size_t i = 0; i < robot.joints.size(); ++i)
                if (q[i] < robot.joints[i].lo - 1e-9 || q[i] > robot.joints[i].hi + 1e-9) fail(id, "joint " + std::to_string(i + 1) + " is outside its limits");
            Vec3 base = vec(params, "center", {0, 0, 0});
            Collision hit = self_collision(robot, q, base, params.value("floor", true));
            if (hit.hit) {
                std::string who = f.value("name", id);
                warnings.push_back(hit.floor ? who + ": link " + std::to_string(hit.link_a) + " goes below the mounting surface"
                                             : who + ": links " + std::to_string(hit.link_a) + " and " + std::to_string(hit.link_b) + " collide in this pose");
            }
            return robot_geometry(p, robot, q, base);
        }
        if (type == "gyroid_infill") {
            Expr body = single_input(f, id, p);
            double cell = positive(params, "cell", 10, id), wall = positive(params, "wall", 1, id), skin = positive(params, "skin", 1.2, id);
            Expr core = intersect(gyroid(p, cell, wall), offset(body, -skin * 0.8));
            lattice_regions.emplace_back(body, skin);
            return unite(shell_inward(body, skin), core);
        }
        fail(id, "unknown feature type '" + type + "'");
    }
};

}  // namespace

BuildResult build_document(Graph& g, const json& doc) {
    if (!doc.is_object()) throw std::runtime_error("document must be a JSON object");
    if (doc.value("version", 1) != 1) throw std::runtime_error("unsupported document version");
    const json& features = doc.at("features");
    Builder b{g, &doc, {}, {}, {}, {}, {}, {}};
    BuildResult out;
    for (auto& f : features) {
        std::string id = f.value("id", std::string());
        if (id.empty()) throw std::runtime_error("every feature needs an id");
        if (!b.by_id.emplace(id, &f).second) throw std::runtime_error("duplicate feature id '" + id + "'");
        FeatureInfo info{id, f.value("type", std::string()), f.value("name", id), f.value("suppressed", false), b.inputs_of(f)};
        out.features.push_back(info);
    }
    std::string root = doc.value("root", out.features.empty() ? std::string() : out.features.back().id);
    if (root.empty()) throw std::runtime_error("document has no features");
    if (Builder::suppressed(*b.by_id.at(root))) throw std::runtime_error("the root feature is suppressed");
    out.root = b.build(root, coords(g));
    out.dag_nodes = b.evaluated.size();
    out.designed_void_regions = b.lattice_regions;
    out.warnings = b.warnings;
    for (auto& f : out.features)
        if (!f.suppressed && !b.evaluated.count(f.id)) out.warnings.push_back("feature '" + f.name + "' is not used by the result");
    return out;
}

json feature_catalog() {
    auto vec3 = [](const char* label, json def) { return json{{"kind", "vec3"}, {"label", label}, {"default", def}}; };
    auto number = [](const char* label, double def, double min, const char* unit = "mm") {
        return json{{"kind", "number"}, {"label", label}, {"default", def}, {"min", min}, {"unit", unit}};
    };
    auto choice = [](const char* label, std::vector<std::string> options) {
        return json{{"kind", "choice"}, {"label", label}, {"options", options}, {"default", options[0]}};
    };
    json placement = {{"center", vec3("Center", {0, 0, 0})}, {"rotation", vec3("Rotation (deg)", {0, 0, 0})}};
    auto with_placement = [&](json p) {
        p.update(placement);
        return p;
    };
    return json{
        {"box", {{"label", "Box"}, {"group", "Primitive"}, {"inputs", 0},
                 {"params", with_placement({{"size", vec3("Size", {20, 20, 20})}, {"radius", number("Edge radius", 0, 0)}})}}},
        {"cylinder", {{"label", "Cylinder"}, {"group", "Primitive"}, {"inputs", 0},
                      {"params", with_placement({{"radius", number("Radius", 5, 0.001)}, {"height", number("Height", 20, 0.001)},
                                                 {"axis", choice("Axis", {"z", "x", "y"})}})}}},
        {"sphere", {{"label", "Sphere"}, {"group", "Primitive"}, {"inputs", 0}, {"params", with_placement({{"radius", number("Radius", 10, 0.001)}})}}},
        {"torus", {{"label", "Torus"}, {"group", "Primitive"}, {"inputs", 0},
                   {"params", with_placement({{"major", number("Major radius", 10, 0.001)}, {"minor", number("Minor radius", 2, 0.001)}})}}},
        {"half_space", {{"label", "Cutting plane"}, {"group", "Primitive"}, {"inputs", 0},
                        {"params", with_placement({{"normal", vec3("Normal", {0, 0, 1})}, {"offset", number("Offset", 0, -1e9)}})}}},
        {"union", {{"label", "Union"}, {"group", "Boolean"}, {"inputs", "2+"}, {"params", {{"blend", number("Blend radius", 0, 0)}}}}},
        {"subtract", {{"label", "Subtract"}, {"group", "Boolean"}, {"inputs", "2+"}, {"params", {{"blend", number("Edge blend", 0, 0)}}}}},
        {"intersect", {{"label", "Intersect"}, {"group", "Boolean"}, {"inputs", "2+"}, {"params", {{"blend", number("Blend radius", 0, 0)}}}}},
        {"shell", {{"label", "Shell"}, {"group", "Modify"}, {"inputs", 1},
                   {"params", {{"thickness", number("Thickness", 1, 0.001)}, {"mode", choice("Mode", {"inward", "centered"})}}}}},
        {"offset", {{"label", "Offset"}, {"group", "Modify"}, {"inputs", 1}, {"params", {{"distance", number("Distance", 1, -1e9)}}}}},
        {"transform", {{"label", "Move / rotate"}, {"group", "Modify"}, {"inputs", 1}, {"params", placement}}},
        {"mirror", {{"label", "Mirror"}, {"group", "Pattern"}, {"inputs", 1}, {"params", {{"plane", choice("Plane", {"yz", "xz", "xy"})}}}}},
        {"pattern_linear", {{"label", "Linear pattern"}, {"group", "Pattern"}, {"inputs", 1},
                            {"params", {{"count", number("Count", 3, 1, "")}, {"step", vec3("Step", {20, 0, 0})}}}}},
        {"pattern_circular", {{"label", "Circular pattern"}, {"group", "Pattern"}, {"inputs", 1},
                              {"params", {{"count", number("Count", 6, 1, "")}, {"angle", number("Total angle", 360, 0, "deg")}}}}},
        {"extrude", {{"label", "Extrude sketch"}, {"group", "Sketch"}, {"inputs", 0},
                     {"params", with_placement({{"profile", json{{"kind", "profile"}, {"label", "Profile"},
                                                                 {"default", json::array({{0, 0, 0}, {40, 0, 0}, {40, 25, 0}, {0, 25, 0}})}}},
                                                {"distance", number("Distance", 10, 0.001)},
                                                {"direction", choice("Direction", {"one side", "symmetric"})}})}}},
        {"revolve", {{"label", "Revolve sketch"}, {"group", "Sketch"}, {"inputs", 0},
                     {"params", with_placement({{"profile", json{{"kind", "profile"}, {"label", "Profile (x = radius)"},
                                                                 {"default", json::array({{0, 0, 0}, {20, 0, 2}, {20, 30, 2}, {0, 30, 0}})}}},
                                                {"angle", number("Angle", 360, 0.1, "deg")}})}}},
        {"import_mesh", {{"label", "Imported part"}, {"group", "Import"}, {"inputs", 0}, {"hidden", true},
                         {"params", with_placement({{"path", json{{"kind", "file"}, {"label", "Mesh file"}, {"default", ""}}},
                                                    {"source", json{{"kind", "file"}, {"label", "Imported from"}, {"default", ""}}},
                                                    {"resolution", number("Resolution (cells)", 128, 16, "")}})}}},
        {"robot_arm", {{"label", "Robot arm"}, {"group", "Robotics"}, {"inputs", 0},
                       {"params", {{"preset", choice("Model", {"ur5", "puma560", "planar3r"})}, {"center", vec3("Base position", {0, 0, 0})},
                                   {"joints_a", vec3("Joints 1-3 (deg)", {0, -90, 0})}, {"joints_b", vec3("Joints 4-6 (deg)", {-90, 0, 0})},
                                   {"link_radius", number("Link radius", 42, 1)}}}}},
        {"gyroid_infill", {{"label", "Gyroid infill"}, {"group", "Lattice"}, {"inputs", 1},
                           {"params", {{"cell", number("Cell size", 10, 0.1)}, {"wall", number("Wall", 0.8, 0.05)}, {"skin", number("Skin", 1.2, 0.05)}}}}},
    };
}

Robot robot_from_json(const json& def, const std::string& key) {
    const double k = std::numbers::pi / 180.0;
    auto num = [](const json& o, const char* name, double fallback) {
        auto it = o.find(name);
        if (it == o.end() || it->is_null()) return fallback;
        if (!it->is_number()) throw std::runtime_error(std::string("robot: '") + name + "' must be a number");
        double v = it->get<double>();
        if (!std::isfinite(v)) throw std::runtime_error(std::string("robot: '") + name + "' is not finite");
        return v;
    };
    auto vec3 = [](const json& o, const char* name, Vec3 fallback) {
        auto it = o.find(name);
        if (it == o.end()) return fallback;
        if (!it->is_array() || it->size() != 3) throw std::runtime_error(std::string("robot: '") + name + "' must be [x, y, z]");
        return Vec3{(*it)[0].get<double>(), (*it)[1].get<double>(), (*it)[2].get<double>()};
    };
    auto origins = [&](const json& o, std::vector<Step>& out) {
        auto it = o.find("origins");
        if (it == o.end()) return;
        for (auto& org : *it) {
            Vec3 xyz = vec3(org, "xyz", {0, 0, 0}), rpy = vec3(org, "rpy_deg", {0, 0, 0});
            if (length(xyz) > 0) out.push_back({false, xyz, 0});
            // URDF rpy = fixed-axis roll, pitch, yaw = intrinsic Z(yaw) Y(pitch) X(roll)
            if (rpy.z != 0) out.push_back({true, {0, 0, 1}, rpy.z * k});
            if (rpy.y != 0) out.push_back({true, {0, 1, 0}, rpy.y * k});
            if (rpy.x != 0) out.push_back({true, {1, 0, 0}, rpy.x * k});
        }
    };
    Robot r;
    r.key = key;
    r.name = def.value("name", std::string("Custom arm"));
    r.link_radius = num(def, "link_radius", 35);
    r.mount_offset = num(def, "mount_offset", 0);
    if (r.link_radius <= 0) throw std::runtime_error("robot: link_radius must be > 0");
    std::string format = def.value("format", std::string("dh"));
    const json& joints = def.at("joints");
    if (!joints.is_array() || joints.empty() || joints.size() > 12) throw std::runtime_error("robot: needs 1 to 12 joints");
    for (auto& j : joints) {
        double lo = num(j, "lo_deg", -180) * k, hi = num(j, "hi_deg", 180) * k;
        if (!(lo < hi)) throw std::runtime_error("robot: joint lower limit must be below the upper limit");
        if (format == "dh") {
            r.joints.push_back(dh_joint(num(j, "d", 0), num(j, "a", 0), num(j, "alpha_deg", 0) * k, lo, hi));
        } else if (format == "chain") {
            Joint jt;
            origins(j, jt.pre);
            jt.axis = vec3(j, "axis", {0, 0, 1});
            if (length(jt.axis) < 1e-9) throw std::runtime_error("robot: joint axis must be non-zero");
            jt.lo = lo;
            jt.hi = hi;
            r.joints.push_back(jt);
        } else {
            throw std::runtime_error("robot: format must be 'dh' or 'chain'");
        }
    }
    if (format == "chain" && def.contains("tool")) origins(def["tool"], r.joints.back().post);
    if (r.reach() < 1e-6) throw std::runtime_error("robot: the chain has no length");
    return r;
}

json example_bracket() {
    // L-profile = envelope minus a notch whose corner carries the exact inner fillet
    const double L = 80, W = 40, T = 6, H = 50, R = 6, big = 4 * (L + H);
    const double xi = -L / 2 + T, gx = xi, span = 34;
    json features = json::array({
        {{"id", "envelope"}, {"type", "box"}, {"name", "Envelope"}, {"params", {{"size", {L, W, H}}, {"center", {0, 0, H / 2}}}}},
        {{"id", "notch"}, {"type", "box"}, {"name", "Inner notch (fillet R6)"},
         {"params", {{"size", {big, 4 * big, big}}, {"center", {xi + big / 2, 0, T + big / 2}}, {"radius", R}}}},
        {{"id", "profile"}, {"type", "subtract"}, {"name", "L profile"}, {"inputs", {"envelope", "notch"}}},
        {{"id", "gusset_box"}, {"type", "box"}, {"name", "Gusset block"}, {"params", {{"size", {span, 10, span}}, {"center", {gx + span / 2, 0, T + span / 2}}}}},
        {{"id", "gusset_cut"}, {"type", "half_space"}, {"name", "Gusset diagonal"}, {"params", {{"normal", {1, 0, 1}}, {"center", {gx, 0, T + span}}}}},
        {{"id", "gusset"}, {"type", "intersect"}, {"name", "Gusset"}, {"inputs", {"gusset_box", "gusset_cut"}}},
        {{"id", "gusset_lattice"}, {"type", "gyroid_infill"}, {"name", "Gusset lattice"}, {"inputs", {"gusset"}},
         {"params", {{"cell", 14}, {"wall", 0.8}, {"skin", 1.2}}}},
        {{"id", "solid"}, {"type", "union"}, {"name", "Body"}, {"inputs", {"profile", "gusset_lattice"}}, {"params", {{"blend", 2}}}},
        {{"id", "hole"}, {"type", "cylinder"}, {"name", "Base hole"}, {"params", {{"radius", 3.3}, {"height", 40}, {"center", {10, -12, 0}}}}},
        {{"id", "base_holes_x"}, {"type", "pattern_linear"}, {"name", "Hole row"}, {"inputs", {"hole"}}, {"params", {{"count", 2}, {"step", {20, 0, 0}}}}},
        {{"id", "base_holes"}, {"type", "pattern_linear"}, {"name", "Hole grid"}, {"inputs", {"base_holes_x"}}, {"params", {{"count", 2}, {"step", {0, 24, 0}}}}},
        {{"id", "wall_hole"}, {"type", "cylinder"}, {"name", "Wall hole"}, {"params", {{"radius", 3.3}, {"height", 40}, {"axis", "x"}, {"center", {-L / 2, 0, 30}}}}},
        {{"id", "wall_holes"}, {"type", "pattern_linear"}, {"name", "Wall holes"}, {"inputs", {"wall_hole"}}, {"params", {{"count", 2}, {"step", {0, 0, 12}}}}},
        {{"id", "body"}, {"type", "subtract"}, {"name", "Drilled bracket"}, {"inputs", {"solid", "base_holes", "wall_holes"}}, {"params", {{"blend", 1}}}},
    });
    return json{{"version", 1}, {"units", "mm"}, {"material", "al6061t6"}, {"name", "L-bracket"}, {"root", "body"}, {"features", features}};
}

}  // namespace dk
