// dk_engine: the Datum kernel as a sidecar process for the desktop shell.
//
// Protocol (stdin -> stdout), one request per line:
//   request:  {"id": 7, "method": "evaluate", "params": {...}}\n
//   response: {"id": 7, "result": {...}}\n            or {"id": 7, "error": "message"}\n
// A response that carries binary data adds "binary": <bytes> and the raw bytes follow
// immediately after the newline. stdout carries nothing else; diagnostics go to stderr.
//
// Methods
//   catalog                       feature types + parameter schema, materials
//   example                       starter document (L-bracket)
//   evaluate {document, quality}  rebuild the DAG; returns the contract, mass properties,
//                                 feature list, analyses. quality: "draft" | "fine"
//   scene {}                      brick-pruned bytecode for the GPU viewport (binary)
//   mesh {}                       triangle mesh of the last evaluation (binary DMSH)
//   export_stl {path}             write the last mesh as binary STL
//   quote {material, quantity}    manufacturing estimate for the last evaluation
//   robot_fk {preset | robot, q_deg}  tool pose and joint positions
//   robot_reach {preset | robot, center, samples, threshold}
//                                 reachable workspace + singularity lock zones (binary voxels:
//                                 x, y, z, best dexterity, worst dexterity as float32)
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <numbers>
#include <random>
#include <string>

#include "dk/analysis.hpp"
#include "dk/bricks.hpp"
#include "dk/document.hpp"
#include "dk/mesh.hpp"
#include "dk/meshfield.hpp"
#include "dk/fea.hpp"
#include "dk/robot.hpp"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using namespace dk;

namespace {

struct Session {
    std::unique_ptr<Graph> graph;
    Program program;
    Mesh mesh;
    MassProperties mp;
    Vec3 lo, hi;
    std::string material = "al6061t6";
    json doc;  // the evaluated document (for feature picking)
    FeaGrid fea_grid;   // last stress study
    std::vector<BuildResult::Lightened> lightened;  // lattice zones (modelled as equivalent material)
    FeaResult fea;
    Accessibility access;
    bool ready = false;
};

std::string uuid4() {
    static std::mt19937_64 rng{std::random_device{}()};
    uint64_t a = rng(), b = rng();
    a = (a & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull;  // version 4
    b = (b & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;  // variant 10
    char s[40];
    std::snprintf(s, sizeof s, "%08x-%04x-%04x-%04x-%012llx", unsigned(a >> 32), unsigned((a >> 16) & 0xFFFF), unsigned(a & 0xFFFF),
                  unsigned(b >> 48), (unsigned long long)(b & 0xFFFFFFFFFFFFull));
    return s;
}

void write_response(const json& r, const std::string* binary = nullptr) {
    json out = r;
    if (binary) out["binary"] = binary->size();
    std::string line = out.dump() + "\n";
    std::fwrite(line.data(), 1, line.size(), stdout);
    if (binary) std::fwrite(binary->data(), 1, binary->size(), stdout);
    std::fflush(stdout);
}

// Bounds of the field: interval-search a generous cube, shrink to the occupied bricks.
void field_bounds(const Program& f, Vec3& lo, Vec3& hi) {
    const double R = 2000;  // mm
    BrickGrid probe = build_bricks(f, {-R, -R, -R}, {R, R, R}, 64);
    double step = 2 * R / 64;
    lo = {1e300, 1e300, 1e300};
    hi = {-1e300, -1e300, -1e300};
    for (size_t b = 0; b < probe.kind.size(); ++b) {
        if (probe.kind[b] == 0) continue;
        int i = int(b % 64), j = int((b / 64) % 64), k = int(b / 4096);
        Vec3 bl{-R + i * step, -R + j * step, -R + k * step};
        for (int a = 0; a < 3; ++a) lo[a] = std::min(lo[a], bl[a]), hi[a] = std::max(hi[a], bl[a] + step);
    }
    if (lo.x > hi.x) throw std::runtime_error("the result is empty");
    if (probe.kind.front() != 0 || lo.x <= -R + 1e-9 || hi.x >= R - 1e-9) throw std::runtime_error("the result is unbounded (check booleans and cutting planes)");
    // refine with a finer grid over the found region
    BrickGrid fine = build_bricks(f, lo, hi, 64);
    Vec3 s{(hi.x - lo.x) / 64, (hi.y - lo.y) / 64, (hi.z - lo.z) / 64};
    Vec3 nlo{1e300, 1e300, 1e300}, nhi{-1e300, -1e300, -1e300};
    for (size_t b = 0; b < fine.kind.size(); ++b) {
        if (fine.kind[b] == 0) continue;
        int i = int(b % 64), j = int((b / 64) % 64), k = int(b / 4096);
        Vec3 bl{lo.x + i * s.x, lo.y + j * s.y, lo.z + k * s.z};
        for (int a = 0; a < 3; ++a) nlo[a] = std::min(nlo[a], bl[a]), nhi[a] = std::max(nhi[a], bl[a] + s[a]);
    }
    lo = nlo;
    hi = nhi;
}

std::string encode_mesh(const Mesh& m) {
    std::string out;
    auto put = [&](const void* p, size_t n) { out.append(static_cast<const char*>(p), n); };
    uint32_t header[4] = {0x48534d44u /* "DMSH" */, 1, uint32_t(m.vertices.size()), uint32_t(m.triangles.size() * 3)};
    put(header, sizeof header);
    std::vector<float> pos(m.vertices.size() * 3), nrm(m.vertices.size() * 3, 0.f);
    for (size_t i = 0; i < m.vertices.size(); ++i)
        for (int a = 0; a < 3; ++a) pos[i * 3 + a] = float(m.vertices[i][a]);
    for (auto& t : m.triangles) {
        Vec3 n = cross(m.vertices[t[1]] - m.vertices[t[0]], m.vertices[t[2]] - m.vertices[t[0]]);
        for (int k = 0; k < 3; ++k)
            for (int a = 0; a < 3; ++a) nrm[t[k] * 3 + a] += float(n[a]);
    }
    put(pos.data(), pos.size() * 4);
    put(nrm.data(), nrm.size() * 4);
    for (auto& t : m.triangles) put(t.data(), 12);
    return out;
}

std::string encode_scene(const BrickGrid& g) {
    // header: n, max_registers, surface count, code count, lo[3], hi[3] (float)
    std::string out;
    auto put = [&](const void* p, size_t n) { out.append(static_cast<const char*>(p), n); };
    uint32_t h[4] = {uint32_t(g.n), uint32_t(g.max_registers), uint32_t(g.kind.size()), uint32_t(g.code.size())};
    float box[6] = {float(g.lo.x), float(g.lo.y), float(g.lo.z), float(g.hi.x), float(g.hi.y), float(g.hi.z)};
    put(h, sizeof h);
    put(box, sizeof box);
    for (size_t b = 0; b < g.kind.size(); ++b) {
        uint32_t e[3] = {g.kind[b], g.offset[b], g.length[b]};
        put(e, sizeof e);
    }
    // sampled grids used by the program: global registry id -> local index
    std::vector<int> used;
    for (auto& in : g.code)
        if (in.op == Op::Grid && std::find(used.begin(), used.end(), int(in.c)) == used.end()) used.push_back(int(in.c));
    for (size_t i = 0; i < g.code.size(); ++i) {
        const Instr& in = g.code[i];
        uint32_t word = uint32_t(in.op) | (uint32_t(g.dst[i]) << 8);
        float c = float(in.c);
        if (in.op == Op::Grid) {
            word |= uint32_t(std::find(used.begin(), used.end(), int(in.c)) - used.begin()) << 24;
            c = float(in.d);  // the z operand's register slot
        }
        uint32_t w[3] = {word, in.a, in.b};
        put(w, sizeof w);
        put(&c, 4);
    }
    // grid block (32-bit words): count | per grid: lo.x lo.y lo.z h (f32), nx ny nz offset (u32) | data (f32)
    uint32_t count = uint32_t(used.size());
    put(&count, 4);
    uint32_t offset = 1 + 8 * count;
    for (int id : used) {
        const SdfGrid& s = grid_by_id(id);
        float hdr[4] = {float(s.lo.x), float(s.lo.y), float(s.lo.z), float(s.h)};
        uint32_t dims[4] = {uint32_t(s.nx), uint32_t(s.ny), uint32_t(s.nz), offset};
        put(hdr, sizeof hdr);
        put(dims, sizeof dims);
        offset += uint32_t(s.d.size());
    }
    for (int id : used) {
        const SdfGrid& s = grid_by_id(id);
        put(s.d.data(), s.d.size() * sizeof(float));
    }
    return out;
}

json evaluate(Session& s, const json& params) {
    auto t0 = std::chrono::steady_clock::now();
    auto ms = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); };
    const json& doc = params.at("document");
    bool fine = params.value("quality", std::string("draft")) == "fine";

    auto graph = std::make_unique<Graph>();
    BuildResult built = build_document(*graph, doc);
    s.doc = doc;
    s.lightened = built.lightened;
    Program f = compile(built.root);
    double t_build = ms();

    Vec3 lo, hi;
    field_bounds(f, lo, hi);
    double extent = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z});
    // cell size: ~1/250 of the part in draft, ~1/600 fine, never below 0.05 mm
    double cell = std::max(extent / (fine ? 600.0 : 250.0), 0.05);
    MeshStats st;
    Mesh mesh = dual_contour(f, {lo, hi, cell}, &st);
    double t_mesh = ms();

    MassProperties mp = mass_properties(mesh);
    // Report the box of the meshed result: the interval search above only brackets the part
    // (loosely for imported grids), and the quote sizes its stock from this box.
    if (!mesh.vertices.empty()) {
        lo = {1e300, 1e300, 1e300};
        hi = {-1e300, -1e300, -1e300};
        for (auto& v : mesh.vertices)
            for (int a = 0; a < 3; ++a) lo[a] = std::min(lo[a], v[a]), hi[a] = std::max(hi[a], v[a]);
    }
    Topology topo = topology(mesh);
    s.material = doc.value("material", std::string("al6061t6"));
    const Material& mat = material(s.material);
    double rho = mat.density_kg_m3 * 1e-9;  // kg / mm^3
    ShellReport shells = scan_shells(mesh, mp);
    // voids inside a lattice infill's core are the design, not a hidden defect
    if (!built.designed_void_regions.empty()) {
        std::vector<std::pair<Program, double>> regions;
        for (auto& [body, skin] : built.designed_void_regions) regions.emplace_back(compile(body), skin);
        for (auto& fd : shells.findings)
            for (auto& [region, skin] : regions)
                // designed only if it sits in the lattice core (the infill starts 0.8 x skin deep);
                // a void inside the solid skin is exactly the defect the scan exists to find
                if (region(fd.centre_mm) < -0.8 * skin) {
                    fd.designed = true;
                    fd.suspicious = false;
                }
    }
    Accessibility acc = tool_access(mesh, f, fine ? 6000 : 2000);
    ImpactResult impact = impact_screen(mesh, mat);
    Vec3 size{hi.x - lo.x, hi.y - lo.y, hi.z - lo.z};
    Quote q = estimate_quote(mp, size, acc, mat, 1);
    double t_total = ms();

    json features = json::array();
    for (auto& fi : built.features)
        features.push_back({{"id", fi.id}, {"type", fi.type}, {"name", fi.name}, {"suppressed", fi.suppressed}, {"inputs", fi.inputs}});
    json tensor = json::array();
    for (int r = 0; r < 3; ++r) tensor.push_back({mp.inertia[r][0] * rho * 1e-6, mp.inertia[r][1] * rho * 1e-6, mp.inertia[r][2] * rho * 1e-6});
    json findings = json::array();
    for (auto& fd : shells.findings)
        findings.push_back({{"centre_mm", {fd.centre_mm.x, fd.centre_mm.y, fd.centre_mm.z}}, {"volume_mm3", fd.volume_mm3},
                            {"size_mm", fd.size_mm}, {"cavity", fd.cavity}, {"suspicious", fd.suspicious}, {"designed", fd.designed}});

    // contract (Pillar 6). gpu_vram_usage_mb and itar_tpm_locked are filled in by the shell,
    // which owns the GPU buffers and the file-protection state.
    json contract = {
        {"system_id", uuid4()},
        {"kernel_metrics", {{"dag_nodes_computed", built.dag_nodes}, {"gpu_vram_usage_mb", 0}}},
        {"commerce_and_supply_chain", {{"live_cnc_quote_usd", std::round(q.price_usd * 100) / 100},
                                       {"embodied_carbon_kg", std::round(q.carbon_kg * 1000) / 1000},
                                       {"transaction_fee_revenue_usd", std::round(q.fee_usd * 100) / 100}}},
        {"security_forensics", {{"itar_tpm_locked", false}, {"steganography_detected", shells.suspicious()}}},
        {"advanced_fea_and_cad", {{"mbd_max_deflection_mm", impact.deflection_mm}, {"explicit_fracture_detected", impact.fracture}}},
        {"telemetry_and_4d", {{"mass_kg", mp.volume_mm3 * rho}, {"inertia_tensor_current", tensor}}},
    };

    s.graph = std::move(graph);
    s.program = std::move(f);
    s.mesh = std::move(mesh);
    s.mp = mp;
    s.lo = lo;
    s.hi = hi;
    s.access = acc;
    s.ready = true;

    return {
        {"contract", contract},
        {"features", features},
        {"warnings", built.warnings},
        {"geometry", {{"bbox_min", {lo.x, lo.y, lo.z}}, {"bbox_max", {hi.x, hi.y, hi.z}}, {"volume_mm3", mp.volume_mm3},
                      {"area_mm2", mp.area_mm2}, {"centroid_mm", {mp.centroid.x, mp.centroid.y, mp.centroid.z}},
                      {"triangles", s.mesh.triangles.size()}, {"cell_mm", cell}, {"watertight", topo.watertight()},
                      {"boundary_edges", topo.boundary_edges}, {"nonmanifold_edges", topo.nonmanifold_edges},
                      {"program_instructions", s.program.size()}, {"graph_nodes", s.graph->size()}}},
        {"material", {{"key", mat.key}, {"name", mat.name}, {"density_kg_m3", mat.density_kg_m3}}},
        {"analysis", {{"shells", shells.shells}, {"findings", findings},
                      {"access", {{"reachable_pct", acc.reachable_pct}, {"covered_pct", acc.covered_pct}, {"setups", acc.setups}}},
                      {"impact", {{"deflection_mm", impact.deflection_mm}, {"stress_mpa", impact.stress_mpa}, {"fracture", impact.fracture},
                                  {"energy_j", impact.impact_energy_j}, {"model", "energy balance, cantilever along longest axis"}}}}},
        {"timing_ms", {{"build", t_build}, {"mesh", t_mesh - t_build}, {"total", t_total}}},
    };
}

json quote(Session& s, const json& params) {
    if (!s.ready) throw std::runtime_error("evaluate a document first");
    const Material& mat = material(params.value("material", s.material));
    int qty = std::max(1, params.value("quantity", 1));
    Vec3 size{s.hi.x - s.lo.x, s.hi.y - s.lo.y, s.hi.z - s.lo.z};
    Quote q = estimate_quote(s.mp, size, s.access, mat, qty);
    return {{"price_usd", q.price_usd}, {"unit_price_usd", q.price_usd / qty}, {"lead_days", q.lead_days}, {"setups", q.setups},
            {"stock_kg", q.stock_kg}, {"machining_h", q.machining_h}, {"carbon_kg", q.carbon_kg}, {"fee_usd", q.fee_usd},
            {"fee_rate", kPlatformFeeRate}, {"provider", q.provider}, {"material", mat.name}, {"quantity", qty}};
}

json catalog() {
    json mats = json::array();
    for (auto& m : materials())
        mats.push_back({{"key", m.key}, {"name", m.name}, {"density_kg_m3", m.density_kg_m3}, {"yield_mpa", m.yield_mpa}});
    json robots = json::array();
    for (auto& r : robot_presets()) {
        json limits = json::array();
        for (auto& j : r.joints) limits.push_back({j.lo * 180 / std::numbers::pi, j.hi * 180 / std::numbers::pi});
        robots.push_back({{"key", r.key}, {"name", r.name}, {"dof", r.joints.size()}, {"reach_mm", r.reach()}, {"limits_deg", limits},
                          {"link_radius", r.link_radius}});
    }
    return {{"features", feature_catalog()}, {"materials", mats}, {"robots", robots}};
}

}  // namespace

int main() {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    Session session;
    std::string line;
    std::fprintf(stderr, "dk_engine ready\n");
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        json req, id = nullptr;
        try {
            req = json::parse(line);
            id = req.value("id", json(nullptr));
            const std::string method = req.at("method");
            const json params = req.value("params", json::object());
            if (method == "evaluate") write_response({{"id", id}, {"result", evaluate(session, params)}});
            else if (method == "catalog") write_response({{"id", id}, {"result", catalog()}});
            else if (method == "example") write_response({{"id", id}, {"result", example_bracket()}});
            else if (method == "quote") write_response({{"id", id}, {"result", quote(session, params)}});
            else if (method == "stress") {
                // linear-elastic study of the current part: per-vertex von Mises (f32) + summary
                if (!session.ready) throw std::runtime_error("evaluate a document first");
                auto spheres = [&](const char* key, bool with_force) {
                    std::vector<FeaSphere> out;
                    for (auto& s : params.value(key, json::array())) {
                        json c = s.at("center");
                        FeaSphere f{{c[0].get<double>(), c[1].get<double>(), c[2].get<double>()}, s.at("radius").get<double>()};
                        if (with_force) {
                            json F = s.at("force");
                            f.force = {F[0].get<double>(), F[1].get<double>(), F[2].get<double>()};
                        }
                        out.push_back(f);
                    }
                    return out;
                };
                auto fixtures = spheres("fixtures", false), loads = spheres("loads", true);
                if (fixtures.empty()) throw std::runtime_error("add at least one fixture (where the part is held)");
                if (loads.empty()) throw std::runtime_error("add at least one load (where force is applied)");
                double target = std::clamp(params.value("voxels", 40000.0), 2000.0, 400000.0);
                double h = voxel_size_for(session.lo, session.hi, target);
                Vec3 pad{h, h, h};
                const Material& m = material(session.material);
                bool homogenised = !session.lightened.empty();
                if (!homogenised) {
                    session.fea_grid = voxelize(session.program, session.lo - pad, session.hi + pad, h);
                    session.fea = solve_elastic(session.fea_grid, m.youngs_gpa * 1000.0, 0.33, fixtures, loads);
                } else {
                    // walls thinner than a voxel: solve on the solid before lightening, with the lattice zone as an
                    // equivalent material (stiffness rho^1.5) and its stress raised to wall stress (/ rho)
                    const auto& L = session.lightened.back();
                    Program body = compile(L.body);
                    session.fea_grid = voxelize(body, session.lo - pad, session.hi + pad, h);
                    const FeaGrid& gr = session.fea_grid;
                    std::vector<float> stiff(gr.solid.size(), 1.f), amp(gr.solid.size(), 1.f);
                    for (int k = 0; k < gr.nz; ++k)
                        for (int j = 0; j < gr.ny; ++j)
                            for (int i = 0; i < gr.nx; ++i) {
                                size_t v = size_t(i) + size_t(gr.nx) * (j + size_t(gr.ny) * k);
                                if (!gr.solid[v]) continue;
                                Vec3 c{gr.lo.x + (i + 0.5) * h, gr.lo.y + (j + 0.5) * h, gr.lo.z + (k + 0.5) * h};
                                if (body(c) > -L.skin) continue;  // solid skin
                                double rho = gyroid_density(grid_eval(L.grid_id, c.x, c.y, c.z), L.cell);
                                stiff[v] = float(gyroid_stiffness(rho));
                                amp[v] = float(1.0 / rho);
                            }
                    session.fea = solve_elastic(session.fea_grid, m.youngs_gpa * 1000.0, 0.33, fixtures, loads, 6000, 1e-6, &stiff, &amp);
                }
                std::string bin;
                bin.reserve(session.mesh.vertices.size() * 4);
                for (auto& v : session.mesh.vertices) {
                    float s = float(sample_node_field(session.fea_grid, session.fea.node_vm, v));
                    bin.append(reinterpret_cast<const char*>(&s), 4);
                }
                // peak away from clamp and load spots (those are singular in any linear FEA)
                std::vector<float> sorted;
                for (float v : session.fea.von_mises) if (v > 0) sorted.push_back(v);
                std::sort(sorted.begin(), sorted.end());
                double p99 = sorted.empty() ? 0 : sorted[size_t(0.99 * (sorted.size() - 1))];
                write_response({{"id", id}, {"result", {
                    {"max_mpa", session.fea.max_vm}, {"p99_mpa", p99}, {"max_displacement_mm", session.fea.max_displacement},
                    {"yield_mpa", m.yield_mpa}, {"safety_factor", m.yield_mpa / std::max(p99, 1e-9)},
                    {"iterations", session.fea.iterations}, {"converged", session.fea.converged}, {"seconds", session.fea.seconds},
                    {"voxel_mm", h}, {"voxels", session.fea_grid.solid_count()},
                    {"fixed_nodes", session.fea.fixed_nodes}, {"loaded_nodes", session.fea.loaded_nodes}, {"material", m.name}, {"homogenised", homogenised}}}}, &bin);
            } else if (method == "feature_at") {
                if (!session.ready) throw std::runtime_error("evaluate a document first");
                json pt = params.at("point");
                double span = std::max({session.hi.x - session.lo.x, session.hi.y - session.lo.y, session.hi.z - session.lo.z});
                std::string fid = feature_at(session.doc, {pt[0].get<double>(), pt[1].get<double>(), pt[2].get<double>()}, std::max(0.05, 0.004 * span));
                write_response({{"id", id}, {"result", {{"feature", fid}}}});
            } else if (method == "scene") {
                if (!session.ready) throw std::runtime_error("evaluate a document first");
                Vec3 c = (session.lo + session.hi) * 0.5;
                double half = 0.5 * std::max({session.hi.x - session.lo.x, session.hi.y - session.lo.y, session.hi.z - session.lo.z}) * 1.04;
                BrickGrid g = build_bricks(session.program, c - Vec3{half, half, half}, c + Vec3{half, half, half}, params.value("bricks", 24));
                std::string bin = encode_scene(g);
                write_response({{"id", id}, {"result", {{"bytes", bin.size()}, {"max_registers", g.max_registers}}}}, &bin);
            } else if (method == "mesh") {
                if (!session.ready) throw std::runtime_error("evaluate a document first");
                std::string bin = encode_mesh(session.mesh);
                write_response({{"id", id}, {"result", {{"bytes", bin.size()}}}}, &bin);
            } else if (method == "export_stl") {
                if (!session.ready) throw std::runtime_error("evaluate a document first");
                std::string path = params.at("path");
                if (!write_stl(session.mesh, path, "Datum Core export")) throw std::runtime_error("could not write " + path);
                write_response({{"id", id}, {"result", {{"path", path}, {"triangles", session.mesh.triangles.size()}}}});
            } else if (method == "robot_reach") {
                Robot r = params.contains("robot") ? robot_from_json(params["robot"]) : robot_preset(params.value("preset", std::string("ur5")));
                json c = params.value("center", json::array({0, 0, 0}));
                size_t samples = std::clamp<size_t>(params.value("samples", size_t(150000)), 1000, 2000000);
                double threshold = std::clamp(params.value("threshold", 0.05), 0.001, 0.5);
                auto t0 = std::chrono::steady_clock::now();
                ReachResult rr = reachability(r, {c[0].get<double>(), c[1].get<double>(), c[2].get<double>()}, samples, threshold, 1,
                                              params.value("collisions", true), params.value("floor", true));
                double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                std::string bin(reinterpret_cast<const char*>(rr.voxels.data()), rr.voxels.size() * sizeof(ReachVoxel));
                write_response({{"id", id}, {"result", {{"robot", r.name}, {"voxels", rr.voxels.size()}, {"voxel_mm", rr.voxel_mm},
                    {"reach_mm", rr.reach_mm}, {"volume_m3", rr.volume_m3}, {"singular_pct", rr.singular_pct}, {"dexterous_pct", rr.dexterous_pct},
                    {"samples", rr.samples}, {"near_singular_samples", rr.near_singular_samples}, {"colliding_samples", rr.colliding_samples}, {"threshold", threshold}, {"ms", ms}}}}, &bin);
            } else if (method == "robot_fk") {
                // tool flange pose and joint positions for joint angles in degrees
                Robot r = params.contains("robot") ? robot_from_json(params["robot"]) : robot_preset(params.value("preset", std::string("ur5")));
                std::vector<double> q;
                for (auto& v : params.value("q_deg", json::array())) q.push_back(v.get<double>() * std::numbers::pi / 180.0);
                q.resize(r.joints.size(), 0.0);
                auto frames = forward_kinematics(r, q);
                json joints = json::array();
                for (size_t i = 0; i + 1 < frames.size(); ++i) joints.push_back({frames[i].o.x, frames[i].o.y, frames[i].o.z});
                const Frame& t = frames.back();
                double L = std::max(r.reach(), 1.0);
                auto J = jacobian(r, frames, L);
                J.resize(3 * r.joints.size());
                write_response({{"id", id}, {"result", {{"tool", {t.o.x, t.o.y, t.o.z}}, {"tool_z", {t.z.x, t.z.y, t.z.z}}, {"joints", joints},
                    {"dof", r.joints.size()}, {"reach_mm", r.reach()}, {"dexterity", inverse_condition(J, 3, int(r.joints.size()), 0)}}}});
            } else if (method == "robot_ik") {
                // joint angles (deg) that bring the flange to a world point, from the current pose
                Robot r = params.contains("robot") ? robot_from_json(params["robot"]) : robot_preset(params.value("preset", std::string("ur5")));
                std::vector<double> q;
                for (auto& v : params.value("q_deg", json::array())) q.push_back(v.get<double>() * std::numbers::pi / 180.0);
                json t = params.at("target"), b = params.value("base", json::array({0, 0, 0}));
                Vec3 target{t[0].get<double>() - b[0].get<double>(), t[1].get<double>() - b[1].get<double>(), t[2].get<double>() - b[2].get<double>()};
                IkResult ik = inverse_kinematics(r, q, target, 0.5);
                json deg = json::array();
                for (double v : ik.q) deg.push_back(v * 180.0 / std::numbers::pi);
                write_response({{"id", id}, {"result", {{"q_deg", deg}, {"error_mm", ik.error_mm}, {"reached", ik.reached}, {"iterations", ik.iterations}}}});
            } else if (method == "ping") write_response({{"id", id}, {"result", "pong"}});
            else throw std::runtime_error("unknown method '" + method + "'");
        } catch (const std::exception& e) {
            write_response({{"id", id}, {"error", e.what()}});
        }
    }
    return 0;
}
