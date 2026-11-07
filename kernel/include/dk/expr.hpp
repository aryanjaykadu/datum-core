// Field expressions.
//
// Geometry is a scalar field f(x, y, z) in millimetres: f < 0 inside, f > 0 outside.
// Users build it with `Expr` (operator overloading over a hash-consed DAG, so shared
// subexpressions are stored once). `compile()` flattens the reachable DAG into a
// `Program`: a linear SSA instruction list that can be evaluated over any scalar type
// (double, Interval, Dual3) and later lowered to a GPU bytecode VM.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "math.hpp"

namespace dk {

// Opcode values are part of the GPU bytecode: append new ops, never reorder.
enum class Op : uint8_t { X, Y, Z, Const, Add, Sub, Mul, Div, Neg, Min, Max, Abs, Sqrt, Square, Sin, Cos, CopySign, Grid };

constexpr int arity(Op op) {
    switch (op) {
        case Op::X: case Op::Y: case Op::Z: case Op::Const: return 0;
        case Op::Neg: case Op::Abs: case Op::Sqrt: case Op::Square: case Op::Sin: case Op::Cos: return 1;
        case Op::Grid: return 3;  // sample point x, y, z; c = grid id
        default: return 2;
    }
}
const char* op_name(Op op);

struct Node {
    Op op;
    uint32_t a = 0, b = 0;
    double c = 0;
    uint32_t d = 0;  // third operand (Op::Grid only)
    bool operator==(const Node&) const = default;
};
class Graph;

class Expr {
  public:
    Expr() = default;
    Expr(Graph* g, uint32_t id) : g_(g), id_(id) {}
    Graph* graph() const { return g_; }
    uint32_t id() const { return id_; }
    bool valid() const { return g_ != nullptr; }

  private:
    Graph* g_ = nullptr;
    uint32_t id_ = 0;
};

class Graph {
  public:
    Expr x() { return add({Op::X}); }
    Expr y() { return add({Op::Y}); }
    Expr z() { return add({Op::Z}); }
    Expr constant(double v) { return add({Op::Const, 0, 0, v}); }
    Expr unary(Op op, Expr a);
    Expr binary(Op op, Expr a, Expr b);
    Expr grid(int id, Expr x, Expr y, Expr z);
    const Node& node(uint32_t id) const { return nodes_[id]; }
    size_t size() const { return nodes_.size(); }

  private:
    Expr add(Node n);
    struct Hash {
        size_t operator()(const Node& n) const;
    };
    std::vector<Node> nodes_;
    std::unordered_map<Node, uint32_t, Hash> index_;
};

// arithmetic (constant folding happens in Graph::binary / unary)
Expr operator+(Expr a, Expr b);
Expr operator-(Expr a, Expr b);
Expr operator*(Expr a, Expr b);
Expr operator/(Expr a, Expr b);
Expr operator-(Expr a);
Expr operator+(Expr a, double b);
Expr operator-(Expr a, double b);
Expr operator*(Expr a, double b);
Expr operator/(Expr a, double b);
Expr operator+(double a, Expr b);
Expr operator-(double a, Expr b);
Expr operator*(double a, Expr b);
Expr min(Expr a, Expr b);
Expr max(Expr a, Expr b);
Expr min(Expr a, double b);
Expr max(Expr a, double b);
Expr abs(Expr a);
Expr sqrt(Expr a);
Expr square(Expr a);
Expr sin(Expr a);
Expr cos(Expr a);
Expr copysign(Expr magnitude, Expr sign);  // |magnitude| with the sign of `sign`
Expr sample_grid(int id, Expr x, Expr y, Expr z);  // trilinear signed-distance grid

// ---------------------------------------------------------------- Program
struct Instr {
    Op op;
    uint32_t a = 0, b = 0;  // indices of earlier instructions
    double c = 0;
    uint32_t d = 0;         // third operand (Op::Grid)
};

// Sampled signed-distance grids (imported meshes), looked up by id. Defined in meshfield.cpp.
double grid_eval(int id, double x, double y, double z);
Interval grid_eval(int id, Interval x, Interval y, Interval z);
Dual3 grid_eval(int id, Dual3 x, Dual3 y, Dual3 z);

class Program {
  public:
    std::vector<Instr> code;  // the last instruction is the output

    template <class T>
    T eval(const T& x, const T& y, const T& z, std::vector<T>& slots) const;
    template <class T>
    T eval(const T& x, const T& y, const T& z) const {
        std::vector<T> slots;
        return eval(x, y, z, slots);
    }
    double operator()(Vec3 p) const { return eval<double>(p.x, p.y, p.z); }
    Dual3 gradient(Vec3 p) const { return eval<Dual3>(Dual3{p.x, {1, 0, 0}}, Dual3{p.y, {0, 1, 0}}, Dual3{p.z, {0, 0, 1}}); }
    Interval bounds(Interval x, Interval y, Interval z) const { return eval<Interval>(x, y, z); }

    // Specialise the program to a box: every min/max whose operands' intervals do not
    // overlap over the box is replaced by the winning branch, then dead code is removed.
    // The result is exact (not an approximation) for every point inside the box.
    Program prune(Interval x, Interval y, Interval z) const;

    size_t size() const { return code.size(); }
    std::string disassemble() const;
};

Program compile(Expr root);

template <class T>
T Program::eval(const T& x, const T& y, const T& z, std::vector<T>& s) const {
    s.resize(code.size());
    for (size_t i = 0; i < code.size(); ++i) {
        const Instr& in = code[i];
        switch (in.op) {
            case Op::X: s[i] = x; break;
            case Op::Y: s[i] = y; break;
            case Op::Z: s[i] = z; break;
            case Op::Const: s[i] = T(in.c); break;
            case Op::Add: s[i] = s[in.a] + s[in.b]; break;
            case Op::Sub: s[i] = s[in.a] - s[in.b]; break;
            case Op::Mul: s[i] = s[in.a] * s[in.b]; break;
            case Op::Div: s[i] = s[in.a] / s[in.b]; break;
            case Op::Neg: s[i] = -s[in.a]; break;
            case Op::Min: s[i] = min(s[in.a], s[in.b]); break;
            case Op::Max: s[i] = max(s[in.a], s[in.b]); break;
            case Op::Abs: s[i] = abs(s[in.a]); break;
            case Op::Sqrt: s[i] = sqrt(s[in.a]); break;
            case Op::Square: s[i] = square(s[in.a]); break;
            case Op::Sin: s[i] = sin(s[in.a]); break;
            case Op::Cos: s[i] = cos(s[in.a]); break;
            case Op::CopySign: s[i] = copysign(s[in.a], s[in.b]); break;
            case Op::Grid: s[i] = grid_eval(int(in.c), s[in.a], s[in.b], s[in.d]); break;
        }
    }
    return s.back();
}

}  // namespace dk
