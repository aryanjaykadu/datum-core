#include <cstring>
#include <filesystem>
#include <functional>
#include <numbers>
#include <set>
#include <stdexcept>
#include <tuple>

#include "dk/document.hpp"
#include "dk/analysis.hpp"
#include "dk/fea.hpp"
#include "dk/meshfield.hpp"
#include "dk/parts.hpp"
#include "dk/robot.hpp"
#include "dk/sketch.hpp"

namespace dk {
namespace {

struct Builder {
    Graph& g;
    const json* doc = nullptr;
    std::map<std::string, const json*> by_id;
    std::map<std::tuple<std::string, uint32_t, uint32_t, uint32_t>, Expr> cache;
    std::set<std::string> visiting, evaluated;
    std::vector<std::pair<Expr, double>> lattice_regions;
    std::vector<BuildResult::Lightened> lightened;
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
        if (type == "gear") {
            P3 q = local(p, params);
            try {
                return spur_gear(q, positive(params, "module", 2, id), int(num(params, "teeth", 20)), num(params, "pressure", 20),
                                 positive(params, "width", 10, id), num(params, "bore", 0));
            } catch (const std::runtime_error& e) {
                fail(id, e.what());
            }
        }
        if (type == "wing" || type == "nose_cone" || type == "spring" || type == "bearing" || type == "beam" || type == "brake_disc" || type == "wheel") {
            P3 q = local(p, params);
            auto str = [&](const char* key, const char* fallback) { return params.value(key, std::string(fallback)); };
            try {
                if (type == "wing")
                    return wing(q, str("naca", "2412"), positive(params, "root_chord", 180, id), num(params, "taper", 0.6),
                                positive(params, "span", 500, id), num(params, "sweep", 15), num(params, "dihedral", 3));
                if (type == "nose_cone") return nose_cone(q, str("shape", "ogive"), positive(params, "diameter", 60, id), positive(params, "length", 150, id));
                if (type == "spring")
                    return coil_spring(q, positive(params, "mean_d", 20, id), positive(params, "wire_d", 2, id),
                                       positive(params, "length", 40, id), positive(params, "turns", 7, id));
                if (type == "bearing") {
                    std::string size = str("size", "608");
                    BearingSize s = size == "custom" ? BearingSize{positive(params, "bore", 8, id), positive(params, "od", 22, id), positive(params, "width", 7, id)}
                                                     : bearing_size(size);
                    return ball_bearing(q, s);
                }
                if (type == "beam") {
                    std::string kind = str("kind", "I-beam"), size = str("size", "custom");
                    BeamSize s = size == "custom" ? BeamSize{positive(params, "height", 100, id), positive(params, "width", 55, id),
                                                             positive(params, "web", 4, id), positive(params, "flange", 6, id)}
                                                  : ipe_size(size);
                    return beam(q, size == "custom" ? kind : "I-beam", s, positive(params, "length", 300, id));
                }
                if (type == "brake_disc")
                    return brake_disc(q, positive(params, "diameter", 280, id), positive(params, "thickness", 22, id), positive(params, "bore", 65, id),
                                      int(num(params, "bolts", 5)), positive(params, "pcd", 112, id), int(num(params, "drilled", 24)));
                return wheel(q, positive(params, "diameter", 381, id), positive(params, "width", 165, id), int(num(params, "spokes", 5)),
                             int(num(params, "bolts", 5)), positive(params, "pcd", 112, id), positive(params, "bore", 57.1, id));
            } catch (const std::runtime_error& e) {
                fail(id, e.what());
            }
        }
        if (type == "thread") {
            static const std::map<std::string, double> coarse = {{"M3", 0.5}, {"M4", 0.7}, {"M5", 0.8}, {"M6", 1.0}, {"M8", 1.25},
                                                                 {"M10", 1.5}, {"M12", 1.75}, {"M16", 2.0}, {"M20", 2.5}};
            std::string size = params.value("size", std::string("M8"));
            auto it = coarse.find(size);
            if (it == coarse.end()) fail(id, "unknown thread size " + size);
            double pitch = num(params, "pitch", 0);
            if (pitch <= 0) pitch = it->second;
            return threaded_rod(local(p, params), std::stod(size.substr(1)), pitch, positive(params, "length", 20, id));
        }
        if (type == "hole") {
            Expr body = single_input(f, id, p);
            HoleSpec h;
            try {
                h = metric_hole(params.value("size", std::string("M6")));
            } catch (const std::runtime_error& e) {
                fail(id, e.what());
            }
            double depth = positive(params, "depth", 10, id);
            Expr cut;
            try {
                cut = hole_cutter(local(p, params), h, depth, params.value("kind", std::string("simple")));
            } catch (const std::runtime_error& e) {
                fail(id, e.what());
            }
            return subtract(body, cut);
        }
        if (type == "texture") {
            Expr body = single_input(f, id, p);
            try {
                return textured(body, p, params.value("pattern", std::string("knurl")), positive(params, "cell", 3, id),
                                positive(params, "depth", 0.4, id));
            } catch (const std::runtime_error& e) {
                fail(id, e.what());
            }
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
        if (type == "loft") {
            auto read = [&](const char* key) {
                std::vector<ProfilePoint> prof;
                auto it = params.find(key);
                if (it == params.end() || !it->is_array()) fail(id, std::string("needs a profile '") + key + "'");
                for (auto& pt : *it) {
                    if (!pt.is_array() || pt.size() < 2) fail(id, "profile points must be [x, y] or [x, y, fillet]");
                    prof.push_back({pt[0].get<double>(), pt[1].get<double>(), pt.size() > 2 ? std::max(pt[2].get<double>(), 0.0) : 0.0});
                }
                try {
                    return tessellate_profile(prof);
                } catch (const std::exception& e) {
                    fail(id, std::string(key) + ": " + e.what());
                }
            };
            Polygon2 a = read("profile"), b = read("profile_top");
            return loft_profiles(local(p, params), a, b, positive(params, "height", 30, id));
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
        if (type == "lighten") {
            Expr body = single_input(f, id, p);
            LightenOptions opt;
            opt.target = num(params, "target", 40) / 100.0;
            opt.cell = num(params, "cell", 0);
            opt.skin = positive(params, "skin", 1.6, id);
            opt.min_wall = positive(params, "min_wall", 0.6, id);
            LightenStudy study;
            if (doc && doc->contains("study")) {
                const json& st = (*doc)["study"];
                auto read = [&](const char* key, bool force, std::vector<FeaSphere>& out) {
                    for (auto& s : st.value(key, json::array())) {
                        json c = s.at("center");
                        FeaSphere sp{{c[0].get<double>(), c[1].get<double>(), c[2].get<double>()}, s.at("radius").get<double>()};
                        if (force) {
                            json F = s.at("force");
                            sp.force = {F[0].get<double>(), F[1].get<double>(), F[2].get<double>()};
                        }
                        out.push_back(sp);
                    }
                };
                read("fixtures", false, study.fixtures);
                read("loads", true, study.loads);
            }
            study.youngs_mpa = material(doc ? doc->value("material", std::string("al6061t6")) : std::string("al6061t6")).youngs_gpa * 1000;
            // the stress solve and the search take seconds: cache by the input's program and the settings
            Program prog = compile(body);
            uint64_t hsh = 1469598103934665603ull;
            auto mix = [&](uint64_t v) { hsh = (hsh ^ v) * 1099511628211ull; };
            for (auto& in : prog.code) {
                mix(uint64_t(in.op)); mix(in.a); mix(in.b); mix(in.d);
                double c = in.c;
                uint64_t bits;
                std::memcpy(&bits, &c, 8);
                mix(bits);
            }
            std::string key = std::to_string(hsh) + "|" + params.dump() + "|" + (doc && doc->contains("study") ? (*doc)["study"].dump() : "") + "|" + std::to_string(study.youngs_mpa);
            static std::map<std::string, std::tuple<int, double, double, double, double, bool>> cache;  // grid, removed, wall min/max, cell, graded
            LightenResult r;
            auto it = cache.find(key);
            try {
                if (it == cache.end()) {
                    r = lighten(p, body, study, opt);
                    cache[key] = {r.grid_id, r.removed, r.wall_min, r.wall_max, r.cell, r.graded};
                } else {
                    // rebuild the expression around the cached thickness grid (cheap)
                    auto [gid, removed, wmin, wmax, cellc, graded] = it->second;
                    const double kk = 2 * std::numbers::pi / cellc, gscale = 1.0 / (std::numbers::sqrt2 * kk);
                    Expr x = p.x * kk, y = p.y * kk, z = p.z * kk;
                    Expr g = sin(x) * cos(y) + sin(y) * cos(z) + sin(z) * cos(x);
                    Expr sheet = (abs(g) * gscale - sample_grid(gid, p.x, p.y, p.z) * 0.5) * (1.0 / 1.6);
                    r.shape = unite(shell_inward(body, opt.skin), intersect(sheet, offset(body, -opt.skin)));
                    r.removed = removed, r.wall_min = wmin, r.wall_max = wmax, r.cell = cellc, r.graded = graded, r.grid_id = gid;
                }
            } catch (const std::runtime_error& e) {
                fail(id, e.what());
            }
            lattice_regions.emplace_back(body, opt.skin);
            lightened.push_back({body, r.grid_id, r.cell, opt.skin});
            char note[200];
            std::snprintf(note, sizeof note, "%s: %.0f %% lighter, %.1f mm cells, walls %.1f-%.1f mm%s", f.value("name", id).c_str(), r.removed * 100,
                          r.cell, r.wall_min, r.wall_max, r.graded ? ", thicker where the stress study is high" : " (uniform: add a strength study to grade it)");
            warnings.push_back(note);
            return r.shape;
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
    Builder b{g, &doc, {}, {}, {}, {}, {}, {}, {}};
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
    out.lightened = b.lightened;
    out.warnings = b.warnings;
    for (auto& f : out.features)
        if (!f.suppressed && !b.evaluated.count(f.id)) out.warnings.push_back("feature '" + f.name + "' is not used by the result");
    return out;
}

std::string feature_at(const json& doc, Vec3 p, double tol) {
    Graph g;
    BuildResult built = build_document(g, doc);
    // depth = number of features upstream (transitively), from the inputs lists
    std::map<std::string, std::vector<std::string>> inputs;
    for (auto& f : built.features) inputs[f.id] = f.inputs;
    std::map<std::string, size_t> depth;
    std::function<size_t(const std::string&)> up = [&](const std::string& id) -> size_t {
        if (auto it = depth.find(id); it != depth.end()) return it->second;
        depth[id] = 0;  // guards cycles (build_document already rejects them)
        size_t d = 0;
        for (auto& i : inputs[id]) d += 1 + up(i);
        return depth[id] = d;
    };
    Builder b{g, &doc, {}, {}, {}, {}, {}, {}, {}};
    for (auto& f : doc.at("features")) b.by_id.emplace(f.value("id", std::string()), &f);
    b.build(doc.value("root", built.features.back().id), coords(g));
    std::string best;
    size_t best_depth = SIZE_MAX;
    double best_d = 1e300;
    for (auto& f : built.features) {
        if (f.suppressed || !b.evaluated.count(f.id)) continue;
        double d = std::abs(compile(b.build(f.id, coords(g)))(p));
        if (d > tol) continue;
        size_t k = up(f.id);
        if (k < best_depth || (k == best_depth && d < best_d)) best = f.id, best_depth = k, best_d = d;
    }
    return best;
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
        {"hole", {{"label", "Hole (ISO metric)"}, {"group", "Modify"}, {"inputs", 1},
                  {"params", with_placement({{"size", choice("Size", {"M6", "M3", "M4", "M5", "M8", "M10", "M12", "M16", "M20"})},
                                             {"kind", choice("Type", {"simple", "counterbore", "countersink"})},
                                             {"depth", number("Depth", 10, 0.1)}})}}},
        {"texture", {{"label", "Surface texture"}, {"group", "Modify"}, {"inputs", 1},
                     {"params", {{"pattern", choice("Pattern", {"knurl", "bumps", "ribs"})}, {"cell", number("Cell size", 3, 0.2)},
                                 {"depth", number("Depth", 0.4, 0.01)}}}}},
        {"gear", {{"label", "Spur gear"}, {"group", "Parts"}, {"inputs", 0},
                  {"params", with_placement({{"module", number("Module", 2, 0.1)}, {"teeth", number("Teeth", 20, 6, "")},
                                             {"pressure", number("Pressure angle", 20, 14.5, "deg")}, {"width", number("Face width", 10, 0.1)},
                                             {"bore", number("Bore diameter", 8, 0)}})}}},
        {"thread", {{"label", "Threaded rod"}, {"group", "Parts"}, {"inputs", 0},
                    {"params", with_placement({{"size", choice("Size", {"M8", "M3", "M4", "M5", "M6", "M10", "M12", "M16", "M20"})},
                                               {"length", number("Length", 30, 0.5)}, {"pitch", number("Pitch (0 = coarse)", 0, 0)}})}}},
        {"spring", {{"label", "Coil spring"}, {"group", "Parts"}, {"inputs", 0},
                    {"params", with_placement({{"mean_d", number("Coil diameter (mean)", 20, 0.5)}, {"wire_d", number("Wire diameter", 2, 0.1)},
                                               {"length", number("Free length", 40, 0.5)}, {"turns", number("Turns", 7, 1, "")}})}}},
        {"bearing", {{"label", "Ball bearing"}, {"group", "Parts"}, {"inputs", 0},
                     {"params", with_placement({{"size", choice("Size", {"608", "6000", "6200", "6201", "6202", "6203", "6204", "6205", "custom"})},
                                                {"bore", number("Bore (custom)", 8, 1)}, {"od", number("Outer diameter (custom)", 22, 2)},
                                                {"width", number("Width (custom)", 7, 1)}})}}},
        {"beam", {{"label", "Structural beam"}, {"group", "Structural"}, {"inputs", 0},
                  {"params", with_placement({{"size", choice("Standard size", {"custom", "IPE 80", "IPE 100", "IPE 120", "IPE 160", "IPE 200"})},
                                             {"kind", choice("Section (custom)", {"I-beam", "C-channel", "L-angle", "T-bar", "Rect tube"})},
                                             {"height", number("Height (custom)", 100, 1)}, {"width", number("Width (custom)", 55, 1)},
                                             {"web", number("Web / wall (custom)", 4, 0.2)}, {"flange", number("Flange (custom)", 6, 0.2)},
                                             {"length", number("Length", 300, 1)}})}}},
        {"wing", {{"label", "Wing (NACA)"}, {"group", "Aerospace"}, {"inputs", 0},
                  {"params", with_placement({{"naca", choice("NACA section", {"2412", "0012", "0009", "4412", "4415", "6409"})},
                                             {"root_chord", number("Root chord", 180, 5)}, {"taper", number("Taper ratio", 0.6, 0.1, "")},
                                             {"span", number("Half span", 500, 5)}, {"sweep", number("Sweep", 15, -60, "deg")},
                                             {"dihedral", number("Dihedral", 3, -30, "deg")}})}}},
        {"nose_cone", {{"label", "Nose cone"}, {"group", "Aerospace"}, {"inputs", 0},
                       {"params", with_placement({{"shape", choice("Shape", {"ogive", "conical", "elliptical", "parabolic"})},
                                                  {"diameter", number("Base diameter", 60, 1)}, {"length", number("Length", 150, 1)}})}}},
        {"brake_disc", {{"label", "Brake disc"}, {"group", "Automotive"}, {"inputs", 0},
                        {"params", with_placement({{"diameter", number("Outer diameter", 280, 50)}, {"thickness", number("Thickness", 22, 2)},
                                                   {"bore", number("Centre bore", 65, 5)}, {"bolts", number("Wheel bolts", 5, 3, "")},
                                                   {"pcd", number("Bolt circle (PCD)", 112, 30)}, {"drilled", number("Drilled holes per ring", 24, 0, "")}})}}},
        {"wheel", {{"label", "Wheel"}, {"group", "Automotive"}, {"inputs", 0},
                   {"params", with_placement({{"diameter", number("Rim diameter", 381, 100)}, {"width", number("Rim width", 165, 30)},
                                              {"spokes", number("Spokes", 5, 3, "")}, {"bolts", number("Wheel bolts", 5, 3, "")},
                                              {"pcd", number("Bolt circle (PCD)", 112, 50)}, {"bore", number("Centre bore", 57.1, 10)}})}}},
        {"extrude", {{"label", "Extrude sketch"}, {"group", "Sketch"}, {"inputs", 0},
                     {"params", with_placement({{"profile", json{{"kind", "profile"}, {"label", "Profile"},
                                                                 {"default", json::array({{0, 0, 0}, {40, 0, 0}, {40, 25, 0}, {0, 25, 0}})}}},
                                                {"distance", number("Distance", 10, 0.001)},
                                                {"direction", choice("Direction", {"one side", "symmetric"})}})}}},
        {"loft", {{"label", "Loft (blend two sketches)"}, {"group", "Sketch"}, {"inputs", 0},
                  {"params", with_placement({{"profile", json{{"kind", "profile"}, {"label", "Bottom profile (z = 0)"},
                                                              {"default", json::array({{-20, -20, 0}, {20, -20, 0}, {20, 20, 0}, {-20, 20, 0}})}}},
                                             {"profile_top", json{{"kind", "profile"}, {"label", "Top profile"},
                                                                  {"default", json::array({{-8, -8, 4}, {8, -8, 4}, {8, 8, 4}, {-8, 8, 4}})}}},
                                             {"height", number("Height", 40, 0.01)}})}}},
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
        {"lighten", {{"label", "Make it lighter"}, {"group", "Lattice"}, {"inputs", 1},
                     {"params", {{"target", number("Lighter by", 40, 5, "%")}, {"skin", number("Solid skin", 1.6, 0.4)},
                                 {"cell", number("Cell size (0 = auto)", 0, 0)}, {"min_wall", number("Thinnest wall", 0.6, 0.3)}}}}},
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
