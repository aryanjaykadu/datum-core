#include "dk/bricks.hpp"

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <thread>

namespace dk {
namespace {

// Linear-scan register allocation on an SSA program: a value's slot is released after its
// last use, so the GPU interpreter needs only as many slots as values live at once
// (typically ~10) instead of one per instruction (100+), and stays in registers.
size_t allocate_registers(const Program& p, std::vector<Instr>& code, std::vector<uint16_t>& dst) {
    size_t n = p.code.size();
    std::vector<uint32_t> last_use(n, 0);
    for (uint32_t i = 0; i < n; ++i) {
        int k = arity(p.code[i].op);
        if (k >= 1) last_use[p.code[i].a] = i;
        if (k >= 2) last_use[p.code[i].b] = i;
    }
    last_use[n - 1] = uint32_t(n);  // the result stays live
    std::vector<uint16_t> reg(n);
    std::vector<uint16_t> free_slots;
    uint16_t next = 0;
    for (uint32_t i = 0; i < n; ++i) {
        Instr in = p.code[i];
        int k = arity(in.op);
        uint16_t ra = k >= 1 ? reg[in.a] : 0, rb = k >= 2 ? reg[in.b] : 0;
        // operands dying here free their slot first, so the result may overwrite one
        if (k >= 1 && last_use[in.a] == i) free_slots.push_back(ra);
        if (k >= 2 && last_use[in.b] == i && in.b != in.a) free_slots.push_back(rb);
        uint16_t r;
        if (!free_slots.empty()) {
            r = free_slots.back();
            free_slots.pop_back();
        } else {
            r = next++;
        }
        reg[i] = r;
        if (last_use[i] == 0 && i != n - 1) free_slots.push_back(r);  // dead value
        in.a = ra;
        in.b = rb;
        code.push_back(in);
        dst.push_back(r);
    }
    return next;
}

}  // namespace

BrickGrid build_bricks(const Program& f, Vec3 lo, Vec3 hi, int n) {
    BrickGrid g;
    g.lo = lo;
    g.hi = hi;
    g.n = n;
    size_t count = size_t(n) * n * n;
    g.kind.assign(count, 0);
    std::vector<Program> programs(count);
    Vec3 step{(hi.x - lo.x) / n, (hi.y - lo.y) / n, (hi.z - lo.z) / n};

    unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t)
        pool.emplace_back([&, t] {
            for (size_t b = t; b < count; b += threads) {
                int i = int(b % n), j = int((b / n) % n), k = int(b / (size_t(n) * n));
                Interval x{lo.x + i * step.x, lo.x + (i + 1) * step.x};
                Interval y{lo.y + j * step.y, lo.y + (j + 1) * step.y};
                Interval z{lo.z + k * step.z, lo.z + (k + 1) * step.z};
                Interval v = f.bounds(x, y, z);
                if (v.lo > 0) g.kind[b] = 0;
                else if (v.hi < 0) g.kind[b] = 1;
                else {
                    g.kind[b] = 2;
                    programs[b] = f.prune(x, y, z);
                }
            }
        });
    for (auto& t : pool) t.join();

    g.offset.assign(count, 0);
    g.length.assign(count, 0);
    for (size_t b = 0; b < count; ++b) {
        if (g.kind[b] != 2) continue;
        g.offset[b] = uint32_t(g.code.size());
        g.length[b] = uint32_t(programs[b].size());
        g.max_length = std::max(g.max_length, programs[b].size());
        g.max_registers = std::max(g.max_registers, allocate_registers(programs[b], g.code, g.dst));
    }
    return g;
}

std::string to_json(const BrickGrid& g, const std::string& name) {
    std::ostringstream o;
    char buf[64];
    auto num = [&](double v) {
        std::snprintf(buf, sizeof buf, "%.9g", v);
        return std::string(buf);
    };
    o << "{\"name\":\"" << name << "\",\"n\":" << g.n << ",\"max_length\":" << g.max_length << ",\"max_registers\":" << g.max_registers;
    o << ",\"lo\":[" << num(g.lo.x) << ',' << num(g.lo.y) << ',' << num(g.lo.z) << "]";
    o << ",\"hi\":[" << num(g.hi.x) << ',' << num(g.hi.y) << ',' << num(g.hi.z) << "]";
    // bricks: [kind, offset, length] flattened
    o << ",\"bricks\":[";
    for (size_t b = 0; b < g.kind.size(); ++b) o << (b ? "," : "") << int(g.kind[b]) << ',' << g.offset[b] << ',' << g.length[b];
    // code: [op | dst << 8, a, b, c] flattened (a, b, dst are register slots)
    o << "],\"code\":[";
    for (size_t i = 0; i < g.code.size(); ++i) {
        const Instr& in = g.code[i];
        o << (i ? "," : "") << (int(in.op) | (int(g.dst[i]) << 8)) << ',' << in.a << ',' << in.b << ',' << num(in.c);
    }
    o << "]}";
    return o.str();
}

}  // namespace dk
