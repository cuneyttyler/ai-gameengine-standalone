#include "places/Expr.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace aige::places {

Tri And(Tri a, Tri b) {
    if (a == Tri::False || b == Tri::False) return Tri::False;
    if (a == Tri::True && b == Tri::True) return Tri::True;
    return Tri::Maybe;
}

Tri Or(Tri a, Tri b) {
    if (a == Tri::True || b == Tri::True) return Tri::True;
    if (a == Tri::False && b == Tri::False) return Tri::False;
    return Tri::Maybe;
}

Tri Not(Tri a) {
    return a == Tri::True ? Tri::False : a == Tri::False ? Tri::True : Tri::Maybe;
}

double Scope::Tolerance(const std::string& name, double target) {
    (void)name;
    return std::abs(target) * 0.05;
}

Tri Truth(const Value& value) {
    switch (value.kind) {
        case Value::Kind::Bool: return value.truth;
        case Value::Kind::Number:
            if (value.number.lo == 0.0 && value.number.hi == 0.0) return Tri::False;
            if (value.number.lo > 0.0 || value.number.hi < 0.0) return Tri::True;
            return Tri::Maybe;
        case Value::Kind::String: return value.text.empty() ? Tri::False : Tri::True;
        case Value::Kind::List: return value.list.empty() ? Tri::False : Tri::True;
        case Value::Kind::Nodes: return value.nodes.empty() ? Tri::False : Tri::True;
        case Value::Kind::Node: return Tri::True;
        default: return Tri::Maybe;
    }
}

// --- syntax ---------------------------------------------------------------------

struct Expr::Node {
    enum class Op { Number, String, Name, Member, List, Call, Unary, Binary };
    Op op;
    double number = 0.0;
    std::string text;                        // String, Name, Member (the member), Call (function), Unary/Binary (operator)
    std::vector<std::shared_ptr<const Node>> args; // operands, list items, call arguments
    std::vector<std::string> argNames;       // Call: "" for positional, "tol" / "class" for named
};

namespace {

using NodePtr = std::shared_ptr<const Expr::Node>;

struct Token {
    enum class Kind { End, Number, String, Name, Symbol } kind = Kind::End;
    std::string text;
    double number = 0.0;
};

class Lexer {
public:
    explicit Lexer(const std::string& text) : s_(text) {}

    bool Run(std::vector<Token>& out, std::string& error) {
        size_t i = 0;
        while (i < s_.size()) {
            const char c = s_[i];
            if (std::isspace(static_cast<unsigned char>(c))) {
                ++i;
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1 < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i + 1])))) {
                size_t end = i;
                while (end < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[end])) || s_[end] == '.')) ++end;
                Token t;
                t.kind = Token::Kind::Number;
                t.text = s_.substr(i, end - i);
                try {
                    t.number = std::stod(t.text);
                } catch (...) {
                    error = "bad number '" + t.text + "'";
                    return false;
                }
                out.push_back(t);
                i = end;
                continue;
            }
            if (c == '\'' || c == '"') {
                size_t end = s_.find(c, i + 1);
                if (end == std::string::npos) {
                    error = "unclosed string";
                    return false;
                }
                out.push_back({Token::Kind::String, s_.substr(i + 1, end - i - 1)});
                i = end + 1;
                continue;
            }
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                size_t end = i;
                while (end < s_.size()) {
                    const char d = s_[end];
                    const bool letter = std::isalnum(static_cast<unsigned char>(d)) || d == '_';
                    // A '-' between two letters is part of the name (house-plot).
                    const bool dash = d == '-' && end > i && end + 1 < s_.size() &&
                                      std::isalpha(static_cast<unsigned char>(s_[end - 1])) &&
                                      std::isalpha(static_cast<unsigned char>(s_[end + 1]));
                    if (!letter && !dash) break;
                    ++end;
                }
                out.push_back({Token::Kind::Name, s_.substr(i, end - i)});
                i = end;
                continue;
            }
            static const char* twoChar[] = {"==", "!=", "<=", ">="};
            bool matched = false;
            for (const char* op : twoChar) {
                if (s_.compare(i, 2, op) == 0) {
                    out.push_back({Token::Kind::Symbol, op});
                    i += 2;
                    matched = true;
                    break;
                }
            }
            if (matched) continue;
            if (std::string("+-*/<>()[],.:").find(c) != std::string::npos) {
                out.push_back({Token::Kind::Symbol, std::string(1, c)});
                ++i;
                continue;
            }
            error = std::string("unexpected '") + c + "'";
            return false;
        }
        out.push_back({});
        return true;
    }

private:
    const std::string& s_;
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : t_(std::move(tokens)) {}

    NodePtr Parse(std::string& error) {
        NodePtr node = Implies();
        if (!error_.empty()) {
            error = error_;
            return nullptr;
        }
        if (Peek().kind != Token::Kind::End) {
            error = "unexpected '" + Peek().text + "'";
            return nullptr;
        }
        return node;
    }

private:
    const Token& Peek() const { return t_[std::min(pos_, t_.size() - 1)]; }
    bool IsSymbol(const char* s) const { return Peek().kind == Token::Kind::Symbol && Peek().text == s; }
    bool IsWord(const char* s) const { return Peek().kind == Token::Kind::Name && Peek().text == s; }
    void Fail(const std::string& message) {
        if (error_.empty()) error_ = message;
    }
    bool Expect(const char* s) {
        if (IsSymbol(s)) {
            ++pos_;
            return true;
        }
        Fail(std::string("expected '") + s + "'");
        return false;
    }

    static NodePtr Make(Expr::Node::Op op, std::string text, std::vector<NodePtr> args = {}) {
        auto node = std::make_shared<Expr::Node>();
        node->op = op;
        node->text = std::move(text);
        node->args = std::move(args);
        return node;
    }

    NodePtr Implies() {
        NodePtr left = OrExpr();
        while (IsWord("implies")) {
            ++pos_;
            left = Make(Expr::Node::Op::Binary, "implies", {left, OrExpr()});
        }
        return left;
    }
    NodePtr OrExpr() {
        NodePtr left = AndExpr();
        while (IsWord("or")) {
            ++pos_;
            left = Make(Expr::Node::Op::Binary, "or", {left, AndExpr()});
        }
        return left;
    }
    NodePtr AndExpr() {
        NodePtr left = NotExpr();
        while (IsWord("and")) {
            ++pos_;
            left = Make(Expr::Node::Op::Binary, "and", {left, NotExpr()});
        }
        return left;
    }
    NodePtr NotExpr() {
        if (IsWord("not")) {
            ++pos_;
            return Make(Expr::Node::Op::Unary, "not", {NotExpr()});
        }
        return Compare();
    }
    NodePtr Compare() {
        NodePtr left = Add();
        for (const char* op : {"==", "!=", "<=", ">=", "<", ">"}) {
            if (IsSymbol(op)) {
                ++pos_;
                return Make(Expr::Node::Op::Binary, op, {left, Add()});
            }
        }
        if (IsWord("in")) {
            ++pos_;
            return Make(Expr::Node::Op::Binary, "in", {left, Add()});
        }
        return left;
    }
    NodePtr Add() {
        NodePtr left = Mul();
        while (IsSymbol("+") || IsSymbol("-")) {
            std::string op = Peek().text;
            ++pos_;
            left = Make(Expr::Node::Op::Binary, op, {left, Mul()});
        }
        return left;
    }
    NodePtr Mul() {
        NodePtr left = Unary();
        while (IsSymbol("*") || IsSymbol("/")) {
            std::string op = Peek().text;
            ++pos_;
            left = Make(Expr::Node::Op::Binary, op, {left, Unary()});
        }
        return left;
    }
    NodePtr Unary() {
        if (IsSymbol("-")) {
            ++pos_;
            return Make(Expr::Node::Op::Unary, "-", {Unary()});
        }
        return Postfix();
    }
    NodePtr Postfix() {
        NodePtr node = Primary();
        while (node && IsSymbol(".")) {
            ++pos_;
            if (Peek().kind != Token::Kind::Name) {
                Fail("expected a name after '.'");
                return node;
            }
            node = Make(Expr::Node::Op::Member, Peek().text, {node});
            ++pos_;
        }
        return node;
    }
    NodePtr Primary() {
        const Token token = Peek();
        if (token.kind == Token::Kind::Number) {
            ++pos_;
            auto node = std::make_shared<Expr::Node>();
            node->op = Expr::Node::Op::Number;
            node->number = token.number;
            return node;
        }
        if (token.kind == Token::Kind::String) {
            ++pos_;
            return Make(Expr::Node::Op::String, token.text);
        }
        if (IsSymbol("(")) {
            ++pos_;
            NodePtr inner = Implies();
            Expect(")");
            return inner;
        }
        if (IsSymbol("[")) {
            ++pos_;
            std::vector<NodePtr> items;
            if (!IsSymbol("]")) {
                items.push_back(Implies());
                while (IsSymbol(",")) {
                    ++pos_;
                    items.push_back(Implies());
                }
            }
            Expect("]");
            return Make(Expr::Node::Op::List, "", items);
        }
        if (token.kind == Token::Kind::Name) {
            ++pos_;
            if (IsSymbol("(")) {
                ++pos_;
                auto call = std::make_shared<Expr::Node>();
                call->op = Expr::Node::Op::Call;
                call->text = token.text;
                if (!IsSymbol(")")) {
                    do {
                        if (call->args.size() > 0) ++pos_; // the ','
                        std::string name;
                        if (Peek().kind == Token::Kind::Name && t_[std::min(pos_ + 1, t_.size() - 1)].text == ":") {
                            name = Peek().text;
                            pos_ += 2;
                        }
                        call->args.push_back(Implies());
                        call->argNames.push_back(name);
                    } while (IsSymbol(","));
                }
                Expect(")");
                return call;
            }
            return Make(Expr::Node::Op::Name, token.text);
        }
        Fail(token.kind == Token::Kind::End ? "unexpected end" : "unexpected '" + token.text + "'");
        ++pos_;
        return Make(Expr::Node::Op::Number, "");
    }

    std::vector<Token> t_;
    size_t pos_ = 0;
    std::string error_;
};

// --- evaluation -------------------------------------------------------------------

Interval Combine(Interval a, Interval b, char op) {
    switch (op) {
        case '+': return {a.lo + b.lo, a.hi + b.hi};
        case '-': return {a.lo - b.hi, a.hi - b.lo};
        case '*': {
            const double p[] = {a.lo * b.lo, a.lo * b.hi, a.hi * b.lo, a.hi * b.hi};
            return {*std::min_element(p, p + 4), *std::max_element(p, p + 4)};
        }
        default: { // '/'
            if (b.lo <= 0.0 && b.hi >= 0.0) {
                return {-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
            }
            const double p[] = {a.lo / b.lo, a.lo / b.hi, a.hi / b.lo, a.hi / b.hi};
            return {*std::min_element(p, p + 4), *std::max_element(p, p + 4)};
        }
    }
}

Tri CompareIntervals(Interval a, Interval b, const std::string& op) {
    if (op == "<") return a.hi < b.lo ? Tri::True : a.lo >= b.hi ? Tri::False : Tri::Maybe;
    if (op == "<=") return a.hi <= b.lo ? Tri::True : a.lo > b.hi ? Tri::False : Tri::Maybe;
    if (op == ">") return CompareIntervals(b, a, "<");
    if (op == ">=") return CompareIntervals(b, a, "<=");
    const bool same = a.lo == a.hi && b.lo == b.hi && a.lo == b.lo;
    const bool apart = a.hi < b.lo || b.hi < a.lo;
    const Tri equal = same ? Tri::True : apart ? Tri::False : Tri::Maybe;
    return op == "==" ? equal : Not(equal);
}

Tri Equal(const Value& a, const Value& b) {
    if (a.kind == Value::Kind::Unknown || b.kind == Value::Kind::Unknown) return Tri::Maybe;
    if (a.kind == Value::Kind::Number && b.kind == Value::Kind::Number) return CompareIntervals(a.number, b.number, "==");
    if (a.kind == Value::Kind::Bool && b.kind == Value::Kind::Bool) {
        if (a.truth == Tri::Maybe || b.truth == Tri::Maybe) return Tri::Maybe;
        return a.truth == b.truth ? Tri::True : Tri::False;
    }
    if (a.kind == Value::Kind::String && b.kind == Value::Kind::String) return a.text == b.text ? Tri::True : Tri::False;
    return Tri::False;
}

// A list's items, or a single value as a one-item list.
std::vector<Value> Items(const Value& v) {
    if (v.kind == Value::Kind::List) return v.list;
    return {v};
}

Value Aggregate(const std::string& fn, const Value& v) {
    if (v.kind == Value::Kind::Unknown) return Value::Unknown();
    if (fn == "count") {
        if (v.kind == Value::Kind::Nodes) {
            Interval total{0, 0};
            for (const auto& [node, n] : v.nodes) total = Combine(total, n, '+');
            return Value::Num(total);
        }
        if (v.kind == Value::Kind::List) {
            if (!v.weights.empty()) {
                Interval total{0, 0};
                for (const auto& w : v.weights) total = Combine(total, w, '+');
                return Value::Num(total);
            }
            return Value::Num(static_cast<double>(v.list.size()));
        }
        return Value::Num(1.0);
    }
    std::vector<Value> items = Items(v);
    std::vector<Interval> numbers;
    std::vector<Interval> weights;
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].kind == Value::Kind::Unknown) return Value::Unknown();
        if (items[i].kind != Value::Kind::Number) continue;
        numbers.push_back(items[i].number);
        weights.push_back(i < v.weights.size() ? v.weights[i] : Interval::Of(1.0));
    }
    if (fn == "sum") {
        Interval total{0, 0};
        for (size_t i = 0; i < numbers.size(); ++i) total = Combine(total, Combine(numbers[i], weights[i], '*'), '+');
        return Value::Num(total);
    }
    if (numbers.empty()) return Value::Unknown();
    if (fn == "min" || fn == "max") {
        Interval result = numbers[0];
        for (const auto& n : numbers) {
            result = fn == "min" ? Interval{std::min(result.lo, n.lo), std::min(result.hi, n.hi)}
                                 : Interval{std::max(result.lo, n.lo), std::max(result.hi, n.hi)};
        }
        return Value::Num(result);
    }
    if (fn == "avg") {
        Interval total{0, 0};
        for (const auto& n : numbers) total = Combine(total, n, '+');
        return Value::Num(Combine(total, Interval::Of(static_cast<double>(numbers.size())), '/'));
    }
    return Value::Unknown();
}

Value Eval(const Expr::Node& node, Scope& scope);

// The name of an argument written as a bare word (children(garden), class: X).
std::string WordOf(const Expr::Node& node) {
    if (node.op == Expr::Node::Op::Name || node.op == Expr::Node::Op::String) return node.text;
    return {};
}

Value Member(const Value& base, const std::string& name, Scope& scope) {
    switch (base.kind) {
        case Value::Kind::Node: return scope.NodeField(base.node, name);
        case Value::Kind::Nodes: {
            // Projection: each node's field, standing for that node's instances.
            Value list;
            list.kind = Value::Kind::List;
            for (const auto& [handle, n] : base.nodes) {
                list.list.push_back(scope.NodeField(handle, name));
                list.weights.push_back(n);
            }
            return list;
        }
        case Value::Kind::Object:
            for (const auto& [key, value] : base.object) {
                if (key == name) return value;
            }
            return Value::Unknown();
        default: return Value::Unknown();
    }
}

Value Eval(const Expr::Node& node, Scope& scope) {
    using Op = Expr::Node::Op;
    switch (node.op) {
        case Op::Number: return Value::Num(node.number);
        case Op::String: return Value::Str(node.text);
        case Op::Name: {
            if (auto v = scope.Name(node.text)) return *v;
            if (node.text == "true") return Value::Boolean(Tri::True);
            if (node.text == "false") return Value::Boolean(Tri::False);
            return Value::Str(node.text); // a bare word: flower, mixed
        }
        case Op::Member: return Member(Eval(*node.args[0], scope), node.text, scope);
        case Op::List: {
            Value list;
            list.kind = Value::Kind::List;
            for (const auto& item : node.args) list.list.push_back(Eval(*item, scope));
            return list;
        }
        case Op::Unary: {
            Value v = Eval(*node.args[0], scope);
            if (node.text == "not") return Value::Boolean(Not(Truth(v)));
            if (v.kind != Value::Kind::Number) return Value::Unknown();
            return Value::Num(Interval{-v.number.hi, -v.number.lo});
        }
        case Op::Call: {
            const std::string& fn = node.text;
            auto named = [&](const char* name) -> const Expr::Node* {
                for (size_t i = 0; i < node.args.size(); ++i) {
                    if (node.argNames[i] == name) return node.args[i].get();
                }
                return nullptr;
            };
            auto positional = [&](size_t index) -> const Expr::Node* {
                size_t seen = 0;
                for (size_t i = 0; i < node.args.size(); ++i) {
                    if (!node.argNames[i].empty()) continue;
                    if (seen++ == index) return node.args[i].get();
                }
                return nullptr;
            };
            if (fn == "children" || fn == "descendants") {
                const Expr::Node* cls = named("class");
                const Expr::Node* module = positional(0);
                return scope.Below(scope.SelfNode(), fn == "descendants", module ? WordOf(*module) : "",
                                   cls ? WordOf(*cls) : "");
            }
            if (fn == "asset") {
                const Expr::Node* slot = positional(0);
                return scope.Asset(slot ? WordOf(*slot) : "");
            }
            if (fn == "within") {
                const Expr::Node* a = positional(0);
                const Expr::Node* b = positional(1);
                const Expr::Node* tol = named("tol");
                if (!a || !b) return Value::Unknown();
                Value va = Eval(*a, scope), vb = Eval(*b, scope);
                if (va.kind != Value::Kind::Number || vb.kind != Value::Kind::Number) return Value::Boolean(Tri::Maybe);
                const std::string tolName = tol ? WordOf(*tol) : "count";
                const double slackLo = scope.Tolerance(tolName, vb.number.lo);
                const double slackHi = scope.Tolerance(tolName, vb.number.hi);
                const Interval allowed{vb.number.lo - slackLo, vb.number.hi + slackHi};
                // Holds for every choice when va sits inside; for none when apart.
                if (va.number.lo >= allowed.lo && va.number.hi <= allowed.hi && vb.number.lo == vb.number.hi) {
                    return Value::Boolean(Tri::True);
                }
                if (va.number.hi < allowed.lo || va.number.lo > allowed.hi) return Value::Boolean(Tri::False);
                return Value::Boolean(Tri::Maybe);
            }
            if (fn == "count" || fn == "sum" || fn == "min" || fn == "max" || fn == "avg") {
                const Expr::Node* arg = positional(0);
                return arg ? Aggregate(fn, Eval(*arg, scope)) : Value::Unknown();
            }
            return Value::Unknown();
        }
        case Op::Binary: {
            const std::string& op = node.text;
            if (op == "and" || op == "or" || op == "implies") {
                const Tri a = Truth(Eval(*node.args[0], scope));
                if (op == "and" && a == Tri::False) return Value::Boolean(Tri::False);
                if (op == "or" && a == Tri::True) return Value::Boolean(Tri::True);
                if (op == "implies" && a == Tri::False) return Value::Boolean(Tri::True);
                const Tri b = Truth(Eval(*node.args[1], scope));
                if (op == "and") return Value::Boolean(And(a, b));
                if (op == "or") return Value::Boolean(Or(a, b));
                return Value::Boolean(Or(Not(a), b));
            }
            Value a = Eval(*node.args[0], scope);
            Value b = Eval(*node.args[1], scope);
            if (op == "in") {
                Tri any = Tri::False;
                for (const Value& item : Items(b)) any = Or(any, Equal(a, item));
                if (b.kind == Value::Kind::Unknown) return Value::Boolean(Tri::Maybe);
                return Value::Boolean(any);
            }
            if (op == "==" || op == "!=") {
                if (a.kind == Value::Kind::Number && b.kind == Value::Kind::Number) {
                    return Value::Boolean(CompareIntervals(a.number, b.number, op));
                }
                const Tri equal = Equal(a, b);
                return Value::Boolean(op == "==" ? equal : Not(equal));
            }
            if (a.kind != Value::Kind::Number || b.kind != Value::Kind::Number) {
                return (op == "<" || op == "<=" || op == ">" || op == ">=") ? Value::Boolean(Tri::Maybe) : Value::Unknown();
            }
            if (op == "<" || op == "<=" || op == ">" || op == ">=") return Value::Boolean(CompareIntervals(a.number, b.number, op));
            return Value::Num(Combine(a.number, b.number, op[0]));
        }
    }
    return Value::Unknown();
}

} // namespace

std::optional<Expr> Expr::Parse(const std::string& text, std::string* error) {
    std::vector<Token> tokens;
    std::string problem;
    if (!Lexer(text).Run(tokens, problem)) {
        if (error) *error = problem;
        return std::nullopt;
    }
    NodePtr root = Parser(std::move(tokens)).Parse(problem);
    if (!root) {
        if (error) *error = problem;
        return std::nullopt;
    }
    Expr expr;
    expr.root_ = root;
    expr.text_ = text;
    return expr;
}

Value Expr::Eval(Scope& scope) const {
    return root_ ? aige::places::Eval(*root_, scope) : Value::Unknown();
}

Tri Expr::Holds(Scope& scope) const {
    return Truth(Eval(scope));
}

} // namespace aige::places
