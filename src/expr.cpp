#include "dk/expr.hpp"

#include <cstring>
#include <sstream>
#include <stdexcept>

namespace dk {

const char* op_name(Op op) {
    static const char* names[] = {"x", "y", "z", "const", "add", "sub", "mul", "div", "neg", "min", "max", "abs", "sqrt", "square", "sin", "cos"};
    return names[static_cast<int>(op)];
}

size_t Graph::Hash::operator()(const Node& n) const {
    uint64_t bits;
    std::memcpy(&bits, &n.c, sizeof bits);
    size_t h = static_cast<size_t>(n.op);
    h = h * 1000003u ^ n.a;
    h = h * 1000003u ^ n.b;
    h = h * 1000003u ^ static_cast<size_t>(bits ^ (bits >> 29));
    return h;
}

Expr Graph::add(Node n) {
    if (n.op == Op::Const && n.c == 0.0) n.c = 0.0;  // fold -0 into +0 so they share a node
    if (auto it = index_.find(n); it != index_.end()) return {this, it->second};
    auto id = static_cast<uint32_t>(nodes_.size());
    nodes_.push_back(n);
    index_.emplace(n, id);
    return {this, id};
}

static bool is_const(const Graph& g, Expr e, double* v = nullptr) {
    const Node& n = g.node(e.id());
    if (n.op != Op::Const) return false;
    if (v) *v = n.c;
    return true;
}

Expr Graph::unary(Op op, Expr a) {
    double v;
    if (is_const(*this, a, &v)) {
        switch (op) {
            case Op::Neg: return constant(-v);
            case Op::Abs: return constant(std::abs(v));
            case Op::Sqrt: return constant(std::sqrt(std::max(v, 0.0)));
            case Op::Square: return constant(v * v);
            case Op::Sin: return constant(std::sin(v));
            case Op::Cos: return constant(std::cos(v));
            default: break;
        }
    }
    if (op == Op::Neg && node(a.id()).op == Op::Neg) return {this, node(a.id()).a};
    return add({op, a.id()});
}

Expr Graph::binary(Op op, Expr a, Expr b) {
    double va, vb;
    bool ca = is_const(*this, a, &va), cb = is_const(*this, b, &vb);
    if (ca && cb) {
        switch (op) {
            case Op::Add: return constant(va + vb);
            case Op::Sub: return constant(va - vb);
            case Op::Mul: return constant(va * vb);
            case Op::Div: return constant(va / vb);
            case Op::Min: return constant(std::min(va, vb));
            case Op::Max: return constant(std::max(va, vb));
            default: break;
        }
    }
    // algebraic identities that keep programs short
    if (op == Op::Add && ca && va == 0) return b;
    if ((op == Op::Add || op == Op::Sub) && cb && vb == 0) return a;
    if (op == Op::Mul && ((ca && va == 1) || (cb && vb == 1))) return ca && va == 1 ? b : a;
    if (op == Op::Mul && ((ca && va == 0) || (cb && vb == 0))) return constant(0);
    if (op == Op::Div && cb && vb == 1) return a;
    if ((op == Op::Min || op == Op::Max) && a.id() == b.id()) return a;
    // canonical operand order for commutative ops improves hash-consing
    if ((op == Op::Add || op == Op::Mul || op == Op::Min || op == Op::Max) && a.id() > b.id()) std::swap(a, b);
    return add({op, a.id(), b.id()});
}

static Graph* graph_of(Expr a, Expr b) {
    if (!a.valid() || !b.valid() || a.graph() != b.graph()) throw std::logic_error("expressions from different graphs");
    return a.graph();
}

Expr operator+(Expr a, Expr b) { return graph_of(a, b)->binary(Op::Add, a, b); }
Expr operator-(Expr a, Expr b) { return graph_of(a, b)->binary(Op::Sub, a, b); }
Expr operator*(Expr a, Expr b) { return graph_of(a, b)->binary(Op::Mul, a, b); }
Expr operator/(Expr a, Expr b) { return graph_of(a, b)->binary(Op::Div, a, b); }
Expr operator-(Expr a) { return a.graph()->unary(Op::Neg, a); }
Expr operator+(Expr a, double b) { return a + a.graph()->constant(b); }
Expr operator-(Expr a, double b) { return a - a.graph()->constant(b); }
Expr operator*(Expr a, double b) { return a * a.graph()->constant(b); }
Expr operator/(Expr a, double b) { return a * a.graph()->constant(1.0 / b); }
Expr operator+(double a, Expr b) { return b.graph()->constant(a) + b; }
Expr operator-(double a, Expr b) { return b.graph()->constant(a) - b; }
Expr operator*(double a, Expr b) { return b.graph()->constant(a) * b; }
Expr min(Expr a, Expr b) { return graph_of(a, b)->binary(Op::Min, a, b); }
Expr max(Expr a, Expr b) { return graph_of(a, b)->binary(Op::Max, a, b); }
Expr min(Expr a, double b) { return min(a, a.graph()->constant(b)); }
Expr max(Expr a, double b) { return max(a, a.graph()->constant(b)); }
Expr abs(Expr a) { return a.graph()->unary(Op::Abs, a); }
Expr sqrt(Expr a) { return a.graph()->unary(Op::Sqrt, a); }
Expr square(Expr a) { return a.graph()->unary(Op::Square, a); }
Expr sin(Expr a) { return a.graph()->unary(Op::Sin, a); }
Expr cos(Expr a) { return a.graph()->unary(Op::Cos, a); }

// ---------------------------------------------------------------- compile / prune

// Emit the subgraph reachable from `root` in topological order. `node_at` maps a
// source index to its Instr; `remap` renumbers operands as they are emitted.
template <class GetInstr>
static Program linearize(uint32_t root, size_t count, GetInstr node_at) {
    std::vector<uint32_t> order;
    std::vector<uint8_t> state(count, 0);  // 0 new, 1 on stack, 2 done
    std::vector<std::pair<uint32_t, bool>> stack{{root, false}};
    while (!stack.empty()) {
        auto [id, expanded] = stack.back();
        stack.pop_back();
        if (state[id] == 2 || (state[id] == 1 && !expanded)) continue;
        if (expanded) {
            state[id] = 2;
            order.push_back(id);
            continue;
        }
        state[id] = 1;
        stack.push_back({id, true});
        Instr in = node_at(id);
        int n = arity(in.op);
        if (n >= 2 && state[in.b] == 0) stack.push_back({in.b, false});
        if (n >= 1 && state[in.a] == 0) stack.push_back({in.a, false});
    }
    std::vector<uint32_t> slot(count, 0);
    Program p;
    p.code.reserve(order.size());
    for (uint32_t id : order) {
        Instr in = node_at(id);
        int n = arity(in.op);
        if (n >= 1) in.a = slot[in.a];
        if (n >= 2) in.b = slot[in.b];
        slot[id] = static_cast<uint32_t>(p.code.size());
        p.code.push_back(in);
    }
    return p;
}

Program compile(Expr root) {
    const Graph& g = *root.graph();
    return linearize(root.id(), g.size(), [&](uint32_t id) {
        const Node& n = g.node(id);
        return Instr{n.op, n.a, n.b, n.c};
    });
}

Program Program::prune(Interval x, Interval y, Interval z) const {
    std::vector<Interval> s;
    eval<Interval>(x, y, z, s);
    // alias[i]: the instruction that instruction i is equivalent to over this box
    std::vector<uint32_t> alias(code.size());
    for (uint32_t i = 0; i < code.size(); ++i) {
        alias[i] = i;
        const Instr& in = code[i];
        if (in.op == Op::Min || in.op == Op::Max) {
            const Interval &a = s[in.a], &b = s[in.b];
            bool a_wins = in.op == Op::Min ? a.hi <= b.lo : a.lo >= b.hi;
            bool b_wins = in.op == Op::Min ? b.hi <= a.lo : b.lo >= a.hi;
            if (a_wins) alias[i] = alias[in.a];
            else if (b_wins) alias[i] = alias[in.b];
        }
    }
    auto root = alias[code.size() - 1];
    return linearize(root, code.size(), [&](uint32_t id) {
        Instr in = code[id];
        int n = arity(in.op);
        if (n >= 1) in.a = alias[in.a];
        if (n >= 2) in.b = alias[in.b];
        return in;
    });
}

std::string Program::disassemble() const {
    std::ostringstream out;
    for (size_t i = 0; i < code.size(); ++i) {
        const Instr& in = code[i];
        out << '%' << i << " = " << op_name(in.op);
        int n = arity(in.op);
        if (in.op == Op::Const) out << ' ' << in.c;
        if (n >= 1) out << " %" << in.a;
        if (n >= 2) out << " %" << in.b;
        out << '\n';
    }
    return out.str();
}

}  // namespace dk
