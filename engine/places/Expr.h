#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace aige::places {

// The constraint expression language (docs/places/place-schema.md §4.3): what module
// constraints, `when` conditions and checks are written in.
//
//   size.w * size.h >= 4
//   beds * 2 <= size.w * size.h
//   within(self.buildings, count(descendants(class: BuildingParcel)), tol: 'count')
//   kind in [flower, mixed]
//   fenced implies asset(fence) != 'missing'
//
// Paths (self.x, parent.x, a.b.c), numbers, 'strings', [lists], calls
// (count, sum, min, max, avg, within, children, descendants, asset),
// + - * /, == != < <= > >= in, and or not implies. A bare word that isn't a
// field reads as a string (flower). A '-' between two letters is part of a
// name (house-plot); subtraction between names needs spaces (a - b).
//
// Values can be ranges: a plan's fields may still be [10, 14]. So arithmetic
// is on intervals and a comparison is True (holds for every choice), False
// (for none) or Maybe (for some). A constraint is satisfiable unless False.

enum class Tri { False, Maybe, True };
Tri And(Tri a, Tri b);
Tri Or(Tri a, Tri b);
Tri Not(Tri a);

struct Interval {
    double lo = 0.0, hi = 0.0;
    static Interval Of(double v) { return {v, v}; }
};

struct Value {
    enum class Kind { Unknown, Number, Bool, String, List, Object, Node, Nodes };
    Kind kind = Kind::Unknown;
    Interval number;
    Tri truth = Tri::Maybe;
    std::string text;
    std::vector<Value> list;
    std::vector<Interval> weights; // List made by projecting Nodes: how many instances each item stands for
    std::vector<std::pair<std::string, Value>> object; // size {w, h}, a mix {rose: 0.5, ...}
    int node = -1;                                    // Node: a handle the Scope understands
    std::vector<std::pair<int, Interval>> nodes;      // Nodes: handles, with how many instances each stands for

    static Value Unknown() { return {}; }
    static Value Num(Interval i) { Value v; v.kind = Kind::Number; v.number = i; return v; }
    static Value Num(double d) { return Num(Interval::Of(d)); }
    static Value Boolean(Tri t) { Value v; v.kind = Kind::Bool; v.truth = t; return v; }
    static Value Str(std::string s) { Value v; v.kind = Kind::String; v.text = std::move(s); return v; }
    static Value NodeRef(int handle) { Value v; v.kind = Kind::Node; v.node = handle; return v; }
};

// What an expression can see. Every method may answer Unknown, which makes
// whatever depends on it Maybe.
class Scope {
public:
    virtual ~Scope() = default;
    // A bare name: a field of the node being checked, "self" or "parent".
    // Not found: nullopt (the name then reads as a string).
    virtual std::optional<Value> Name(const std::string& name) = 0;
    // A field of another node.
    virtual Value NodeField(int node, const std::string& field) = 0;
    // children(...) / descendants(...) of a node: `module` a module id, or
    // `cls` a class (from class: X); both empty: all.
    virtual Value Below(int node, bool descendants, const std::string& module, const std::string& cls) = 0;
    virtual int SelfNode() = 0;
    // asset(slot): what the asset plan decided for a slot ('project', 'generate', 'missing', ...).
    virtual Value Asset(const std::string& slot) { (void)slot; return Value::Unknown(); }
    // within(a, b, tol: name): the allowed difference for target `b`.
    virtual double Tolerance(const std::string& name, double target);
};

class Expr {
public:
    struct Node;

    // Parses `text`; nullopt (and `error`) when it isn't a valid expression.
    static std::optional<Expr> Parse(const std::string& text, std::string* error = nullptr);

    Value Eval(Scope& scope) const;
    // The expression as a condition.
    Tri Holds(Scope& scope) const;
    const std::string& Text() const { return text_; }

private:
    std::shared_ptr<const Node> root_;
    std::string text_;
};

// The truth of a value used as a condition.
Tri Truth(const Value& value);

} // namespace aige::places
