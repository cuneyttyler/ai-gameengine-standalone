#include "places/PlaceValidate.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace aige {

using json = nlohmann::json;
using places::Interval;
using places::Tri;
using places::Value;

namespace {

const std::vector<std::string> kDensityWords = {"none", "rare", "some", "common", "abundant"};
const std::vector<std::string> kAssetSources = {"auto", "project", "library", "web", "generate"};

// A number, [lo, hi], or {"<key>": either} as an interval.
std::optional<Interval> IntervalOf(const json& value, const std::string& key = {}) {
    if (value.is_number()) return Interval::Of(value.get<double>());
    if (value.is_array() && value.size() == 2 && value[0].is_number() && value[1].is_number()) {
        return Interval{value[0].get<double>(), value[1].get<double>()};
    }
    if (value.is_object() && !key.empty() && value.contains(key)) return IntervalOf(value[key]);
    return std::nullopt;
}

std::string Join(const std::vector<std::string>& items) {
    std::string out;
    for (const auto& item : items) out += (out.empty() ? "" : ", ") + item;
    return out;
}

bool InVocabulary(const Ontology& ontology, const FieldDef& field, const std::string& word) {
    if (!field.values.empty()) return std::find(field.values.begin(), field.values.end(), word) != field.values.end();
    return ontology.FindConcept(field.vocabulary, word) != nullptr;
}

} // namespace

// --- fields -----------------------------------------------------------------------

Value FieldValueOf(const FieldDef& field, const json& given) {
    const json& value = given.is_null() ? field.defaultValue : given;
    if (value.is_null()) return Value::Unknown();
    const std::string& t = field.type;
    if (t == "count" || t == "area" || t == "length" || t == "width" || t == "share" || t == "ratio") {
        if (auto i = IntervalOf(value, t)) return Value::Num(*i);
        return Value::Unknown();
    }
    if (t == "spacing") {
        if (value.is_object() && value.contains("min")) {
            return Value::Num(Interval{value.value("min", 0.0), value.value("max", value.value("min", 0.0))});
        }
        if (auto i = IntervalOf(value)) return Value::Num(*i);
        return Value::Unknown();
    }
    if (t == "size") {
        if (!value.is_object()) return Value::Unknown();
        Value v;
        v.kind = Value::Kind::Object;
        for (const char* side : {"w", "h"}) {
            auto i = value.contains(side) ? IntervalOf(value[side]) : std::nullopt;
            v.object.push_back({side, i ? Value::Num(*i) : Value::Unknown()});
        }
        return v;
    }
    if (t == "density") {
        if (value.is_number()) return Value::Num(value.get<double>());
        if (!value.is_string()) return Value::Unknown();
        auto it = std::find(kDensityWords.begin(), kDensityWords.end(), value.get<std::string>());
        if (it == kDensityWords.end() || field.levels.size() != 5) return Value::Unknown();
        return Value::Num(field.levels[static_cast<size_t>(it - kDensityWords.begin())]);
    }
    if (t == "bool") return value.is_boolean() ? Value::Boolean(value.get<bool>() ? Tri::True : Tri::False) : Value::Unknown();
    if (t == "enum" || t == "description") return value.is_string() ? Value::Str(value.get<std::string>()) : Value::Unknown();
    if (t == "enumSet" || t == "tags") {
        if (!value.is_array()) return Value::Unknown();
        Value list;
        list.kind = Value::Kind::List;
        for (const auto& item : value) {
            if (item.is_string()) list.list.push_back(Value::Str(item.get<std::string>()));
        }
        return list;
    }
    if (t == "mix") {
        if (value.is_string()) {
            Value v;
            v.kind = Value::Kind::Object;
            v.object.push_back({value.get<std::string>(), Value::Num(1.0)});
            return v;
        }
        const json& weights = value.is_object() && value.contains("mix") ? value["mix"] : value;
        if (!weights.is_object()) return Value::Unknown();
        Value v;
        v.kind = Value::Kind::Object;
        for (const auto& [key, w] : weights.items()) v.object.push_back({key, w.is_number() ? Value::Num(w.get<double>()) : Value::Unknown()});
        return v;
    }
    return Value::Unknown();
}

std::string CheckFieldValue(const Ontology& ontology, const FieldDef& f, const json& value) {
    const std::string& t = f.type;
    auto bounds = [&](Interval i) -> std::string {
        if (i.lo > i.hi) return "a range runs low to high";
        if (f.min && i.lo < *f.min) return "can't be below " + std::to_string(static_cast<long long>(*f.min));
        if (f.max && i.hi > *f.max) return "can't be above " + std::to_string(static_cast<long long>(*f.max));
        return {};
    };
    if (t == "count" || t == "area" || t == "length" || t == "width" || t == "share" || t == "ratio") {
        auto i = IntervalOf(value, t);
        if (!i) return "needs a number or [low, high]" + std::string(t == "count" ? "" : "");
        if (t == "count" && (std::floor(i->lo) != i->lo || std::floor(i->hi) != i->hi)) return "a count is a whole number";
        if (t == "share" && (i->lo < 0.0 || i->hi > 1.0)) return "a share is between 0 and 1";
        return bounds(*i);
    }
    if (t == "spacing") {
        if (value.is_object() && value.contains("min") && value["min"].is_number()) {
            const double lo = value["min"].get<double>();
            const double hi = value.value("max", lo);
            return bounds({lo, hi});
        }
        if (auto i = IntervalOf(value)) return bounds(*i);
        return "needs {\"min\": n, \"max\": n}";
    }
    if (t == "size") {
        if (!value.is_object()) return "needs {\"w\": n or [lo, hi], \"h\": ...}";
        for (const char* side : {"w", "h"}) {
            auto i = value.contains(side) ? IntervalOf(value[side]) : std::nullopt;
            if (!i) return std::string("needs \"") + side + "\" (cells)";
            if (i->lo > i->hi) return "a range runs low to high";
            if (f.minSize.is_object() && f.minSize.contains(side) && i->lo < f.minSize[side].get<double>()) {
                return std::string(side) + " can't be below " + f.minSize[side].dump();
            }
            if (f.maxSize.is_object() && f.maxSize.contains(side) && i->hi > f.maxSize[side].get<double>()) {
                return std::string(side) + " can't be above " + f.maxSize[side].dump();
            }
        }
        return {};
    }
    if (t == "density") {
        if (value.is_string() && std::find(kDensityWords.begin(), kDensityWords.end(), value.get<std::string>()) != kDensityWords.end()) return {};
        if (value.is_number() && value.get<double>() >= 0.0) return {};
        return "is one of none, rare, some, common, abundant";
    }
    if (t == "bool") return value.is_boolean() ? std::string() : "is true or false";
    if (t == "enum") {
        if (!value.is_string()) return "needs one value";
        if (!InVocabulary(ontology, f, value.get<std::string>())) {
            return "'" + value.get<std::string>() + "' isn't one of " +
                   (f.values.empty() ? "the " + f.vocabulary + " vocabulary" : Join(f.values));
        }
        return {};
    }
    if (t == "enumSet") {
        if (!value.is_array()) return "needs a list of values";
        for (const auto& item : value) {
            if (!item.is_string() || !InVocabulary(ontology, f, item.get<std::string>())) {
                return "'" + (item.is_string() ? item.get<std::string>() : item.dump()) + "' isn't in " +
                       (f.values.empty() ? "the " + f.vocabulary + " vocabulary" : Join(f.values));
            }
        }
        return {};
    }
    if (t == "mix") {
        // One value alone is a mix of one ("organic" = {"organic": 1}).
        if (value.is_string()) {
            return InVocabulary(ontology, f, value.get<std::string>()) ? std::string()
                                                                      : "'" + value.get<std::string>() + "' isn't in " + (f.values.empty() ? "the " + f.vocabulary + " vocabulary" : Join(f.values));
        }
        const json& weights = value.is_object() && value.contains("mix") ? value["mix"] : value;
        if (!weights.is_object() || weights.empty()) return "needs {\"value\": weight, ...}";
        double total = 0.0;
        for (const auto& [key, w] : weights.items()) {
            if (!InVocabulary(ontology, f, key)) return "'" + key + "' isn't in " + (f.values.empty() ? "the " + f.vocabulary + " vocabulary" : Join(f.values));
            if (!w.is_number() || w.get<double>() < 0.0) return "weights are numbers ≥ 0";
            total += w.get<double>();
        }
        if (std::abs(total - 1.0) > 0.011) return "weights add up to 1 (they add up to " + std::to_string(total).substr(0, 4) + ")";
        return {};
    }
    if (t == "tags") return value.is_array() ? std::string() : "needs a list of words";
    if (t == "description") return value.is_string() ? std::string() : "needs text";
    if (t == "assetChoice") {
        if (!value.is_object()) return "needs {\"source\": ...}";
        const std::string source = value.value("source", "auto");
        if (std::find(kAssetSources.begin(), kAssetSources.end(), source) == kAssetSources.end()) {
            return "source is one of " + Join(kAssetSources);
        }
        return {};
    }
    return "unknown field type '" + t + "'";
}

// --- the Spec tree ------------------------------------------------------------------

namespace {

struct FlatNode {
    const json* data = nullptr;
    int parent = -1;
    const ModuleDef* module = nullptr;
    Interval perParent{1, 1}; // instances per instance of the parent
    std::vector<int> children;
    std::string id;
};

class SpecScope : public places::Scope {
public:
    SpecScope(const Ontology& ontology, const std::vector<FlatNode>& nodes, const AssetPlanLookup& assets)
        : ontology_(ontology), nodes_(nodes), assets_(assets) {}

    int self = 0;

    std::optional<Value> Name(const std::string& name) override {
        if (name == "self") return Value::NodeRef(self);
        if (name == "parent") return nodes_[self].parent >= 0 ? Value::NodeRef(nodes_[self].parent) : Value::Unknown();
        if (nodes_[self].module && nodes_[self].module->Field(name)) return NodeField(self, name);
        return std::nullopt;
    }

    Value NodeField(int node, const std::string& field) override {
        if (node < 0 || node >= static_cast<int>(nodes_.size())) return Value::Unknown();
        const FlatNode& n = nodes_[node];
        if (n.module == nullptr) return Value::Unknown();
        const FieldDef* f = n.module->Field(field);
        if (f == nullptr) return Value::Unknown();
        const json& group = (*n.data).value(f->quantity ? "quantity" : "quality", json::object());
        return FieldValueOf(*f, group.is_object() && group.contains(field) ? group[field] : json());
    }

    Value Below(int node, bool descendants, const std::string& module, const std::string& cls) override {
        Value out;
        out.kind = Value::Kind::Nodes;
        std::vector<std::pair<int, Interval>> todo;
        for (int c : nodes_[node].children) todo.push_back({c, nodes_[c].perParent});
        while (!todo.empty()) {
            auto [c, n] = todo.back();
            todo.pop_back();
            const FlatNode& child = nodes_[c];
            const std::string id = child.module ? child.module->id : "";
            const bool match = (module.empty() || id == module) && (cls.empty() || ontology_.IsA(id, cls));
            if (match) out.nodes.push_back({c, n});
            if (descendants) {
                for (int g : child.children) {
                    const Interval m = nodes_[g].perParent;
                    todo.push_back({g, Interval{n.lo * m.lo, n.hi * m.hi}});
                }
            }
        }
        return out;
    }

    int SelfNode() override { return self; }

    Value Asset(const std::string& slot) override {
        if (!assets_) return Value::Unknown();
        const std::string decided = assets_(nodes_[self].id, slot);
        return decided.empty() ? Value::Unknown() : Value::Str(decided);
    }

    double Tolerance(const std::string& name, double target) override {
        const Tolerances& t = ontology_.Tolerance();
        if (name == "area") return std::abs(target) * t.areaFraction;
        return t.Count(target);
    }

private:
    const Ontology& ontology_;
    const std::vector<FlatNode>& nodes_;
    const AssetPlanLookup& assets_;
};

} // namespace

std::vector<PlaceProblem> ValidateSpec(const Ontology& ontology, const json& spec, SpecStage stage,
                                       const std::string& buildType, const AssetPlanLookup& assets) {
    std::vector<PlaceProblem> problems;
    auto problem = [&](const std::string& where, const std::string& message, bool warning = false) {
        problems.push_back({where, message, warning});
    };
    if (!spec.is_object() || spec.value("schema", "") != "aige.spec/1") {
        problem("spec", "not an aige.spec/1 document");
        return problems;
    }
    if (!spec.contains("root") || !spec["root"].is_object()) {
        problem("spec", "needs a \"root\" node");
        return problems;
    }

    // Flatten the tree, checking each node on its own as we go.
    std::vector<FlatNode> nodes;
    std::set<std::string> ids;
    std::vector<std::pair<const json*, int>> todo = {{&spec["root"], -1}};
    while (!todo.empty()) {
        auto [data, parent] = todo.front();
        todo.erase(todo.begin());
        FlatNode n;
        n.data = data;
        n.parent = parent;
        n.id = data->value("id", "");
        const std::string where = n.id.empty() ? "a node" : n.id;
        if (n.id.empty()) problem(where, "every node needs an id");
        else if (!ids.insert(n.id).second) problem(where, "two nodes have this id");
        const std::string moduleId = data->value("module", "");
        n.module = ontology.Module(moduleId);
        if (n.module == nullptr) problem(where, "unknown module '" + moduleId + "'");

        // How many per parent instance: repeat (a count) times share (a fraction).
        Interval per{1, 1};
        if (data->contains("repeat")) {
            if (auto r = IntervalOf((*data)["repeat"]); r && r->lo >= 0 && r->lo <= r->hi) per = *r;
            else problem(where, "repeat is a count or [low, high]");
        }
        if (data->contains("share")) {
            const json& s = (*data)["share"];
            if (s.is_number() && s.get<double>() >= 0.0 && s.get<double>() <= 1.0) {
                per = Interval{per.lo * s.get<double>(), per.hi * s.get<double>()};
            } else {
                problem(where, "share is a fraction between 0 and 1");
            }
        }
        n.perParent = per;

        if (parent < 0 && n.module && !buildType.empty()) {
            const BuildType* build = ontology.Build(buildType);
            if (build && build->root != n.module->id) problem(where, "the root of a " + buildType + " is a " + build->root);
        }
        if (parent >= 0 && n.module && nodes[parent].module) {
            const ModuleDef* p = nodes[parent].module;
            if (ontology.ChildRule(p->id, n.module->id) == nullptr) {
                problem(where, "a " + p->id + " can't contain a " + n.module->id);
            }
            if (static_cast<int>(n.module->level) < static_cast<int>(p->level) && n.module->level != PlaceLevel::Abstract &&
                n.module->level != PlaceLevel::Network) {
                problem(where, std::string("a ") + PlaceLevelName(n.module->level) + " can't sit inside a " + PlaceLevelName(p->level));
            }
        }
        const int index = static_cast<int>(nodes.size());
        if (parent >= 0) nodes[parent].children.push_back(index);
        nodes.push_back(n);
        // Iterate the node itself (not a copy): todo keeps pointers into the document.
        if (data->contains("children") && (*data)["children"].is_array()) {
            for (const auto& child : (*data)["children"]) {
                if (child.is_object()) todo.push_back({&child, index});
                else problem(where, "children are nodes (objects)");
            }
        }
    }

    SpecScope scope(ontology, nodes, assets);
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
        const FlatNode& n = nodes[i];
        if (n.module == nullptr) continue;
        scope.self = i;
        const std::string& where = n.id;

        // Fields: known, valid, and present only when their condition can hold.
        for (const char* group : {"quantity", "quality"}) {
            const json& fields = n.data->value(group, json::object());
            if (!fields.is_object()) {
                problem(where, std::string(group) + " is an object of fields");
                continue;
            }
            for (const auto& [name, value] : fields.items()) {
                const FieldDef* f = n.module->Field(name);
                if (f == nullptr || f->quantity != (std::string(group) == "quantity")) {
                    problem(where + "." + name, "a " + n.module->id + " has no " + group + " field '" + name + "'");
                    continue;
                }
                if (std::string error = CheckFieldValue(ontology, *f, value); !error.empty()) {
                    problem(where + "." + name, error);
                }
                if (!f->when.empty()) {
                    if (auto cond = places::Expr::Parse(f->when); cond && cond->Holds(scope) == Tri::False) {
                        problem(where + "." + name, "only applies when " + f->when);
                    }
                }
            }
        }

        // Children: within each rule's counts (all counts at the final stage).
        for (const ChildDef& rule : n.module->children) {
            Interval total{0, 0};
            for (int c : n.children) {
                const FlatNode& child = nodes[c];
                if (!child.module || ontology.ChildRule(n.module->id, child.module->id) != &rule) continue;
                total.lo += child.perParent.lo;
                total.hi += child.perParent.hi;
            }
            const std::string name = rule.module;
            if (total.lo > rule.max) problem(where, "at most " + std::to_string(rule.max) + " " + name + " per " + n.module->id);
            // A rule tied to a count field is the compiler's to expand from that
            // field; children written out explicitly must agree with it.
            if (stage == SpecStage::Final) {
                if (rule.count.empty() && total.hi < rule.min) problem(where, "needs at least " + std::to_string(rule.min) + " " + name);
                if (!rule.count.empty() && total.hi > 0) {
                    const Value wanted = scope.NodeField(i, rule.count);
                    if (wanted.kind == Value::Kind::Number &&
                        (total.hi < wanted.number.lo || total.lo > wanted.number.hi)) {
                        problem(where, "has " + std::to_string(static_cast<int>(total.lo)) + " " + name + ", but its " +
                                           rule.count + " says " + std::to_string(static_cast<int>(wanted.number.lo)) +
                                           (wanted.number.hi != wanted.number.lo ? "–" + std::to_string(static_cast<int>(wanted.number.hi)) : ""));
                    }
                }
            }
        }

        // Siblings of one module share their parent's instances.
        std::map<std::string, double> shares;
        for (int c : n.children) {
            const json& child = *nodes[c].data;
            if (child.contains("share") && child["share"].is_number() && nodes[c].module) {
                shares[nodes[c].module->id] += child["share"].get<double>();
            }
        }
        for (const auto& [module, total] : shares) {
            if (total > 1.0001) problem(where, "the shares of its " + module + " children add up to more than 1");
        }

        // Aggregate constraints: an error only when no choice within the ranges meets them.
        // Constraints over children wait for the final stage, when the lower levels exist.
        for (const std::string& text : n.module->constraints) {
            if (stage == SpecStage::Draft &&
                (text.find("children(") != std::string::npos || text.find("descendants(") != std::string::npos)) {
                continue;
            }
            std::string error;
            auto expr = places::Expr::Parse(text, &error);
            if (!expr) {
                problem(where, "constraint '" + text + "': " + error);
                continue;
            }
            if (expr->Holds(scope) == Tri::False) problem(where, "can't meet " + text);
        }

        // Relations to other nodes.
        for (const auto& rel : n.data->value("rel", json::array())) {
            if (!rel.is_object()) continue;
            const std::string type = rel.value("type", "");
            if (ontology.Relation(type) == nullptr) problem(where, "unknown relation '" + type + "'");
            for (const char* key : {"of", "to"}) {
                const std::string target = rel.value(key, "");
                if (!target.empty() && !ids.contains(target)) problem(where, type + " " + key + " '" + target + "', which isn't a node here");
            }
        }

        // What it requires or rules out, anywhere in the place.
        auto anywhere = [&](const std::string& target) {
            const std::string cls = target.rfind("class:", 0) == 0 ? target.substr(6) : target;
            return std::any_of(nodes.begin(), nodes.end(), [&](const FlatNode& o) {
                return o.module && (o.module->id == cls || ontology.IsA(o.module->id, cls));
            });
        };
        for (const auto& target : n.module->required) {
            if (!anywhere(target)) problem(where, "a " + n.module->id + " needs a " + target + " in the place", stage == SpecStage::Draft);
        }
        for (const auto& target : n.module->excludes) {
            if (anywhere(target)) problem(where, "a " + n.module->id + " can't be in a place with a " + target);
        }
    }
    return problems;
}

// --- the Brief ------------------------------------------------------------------------

std::vector<PlaceProblem> ValidateBrief(const Ontology& ontology, const json& brief) {
    std::vector<PlaceProblem> problems;
    auto problem = [&](const std::string& where, const std::string& message, bool warning = false) {
        problems.push_back({where, message, warning});
    };
    if (!brief.is_object() || brief.value("schema", "") != "aige.brief/1") {
        problem("brief", "not an aige.brief/1 document");
        return problems;
    }
    const BuildType* build = ontology.Build(brief.value("buildType", ""));
    if (build == nullptr) {
        problem("buildType", "unknown build type '" + brief.value("buildType", "") + "'");
        return problems;
    }
    const json& scale = brief.value("scale", json::object());
    const ScaleClass* cls = build->Scale(scale.value("class", ""));
    if (cls == nullptr) {
        std::vector<std::string> known;
        for (const auto& s : build->scale) known.push_back(s.id);
        problem("scale.class", "a " + build->id + " is one of " + Join(known));
    } else if (scale.contains("buildings")) {
        auto b = IntervalOf(scale["buildings"], "count");
        if (!b || b->lo < 0 || b->lo > b->hi) {
            problem("scale.buildings", "needs a count or [low, high]");
        } else if (b->hi < cls->buildings.first || b->lo > cls->buildings.second) {
            // The numbers win; the class should follow them.
            std::string fits;
            for (const auto& s : build->scale) {
                if (b->lo >= s.buildings.first && b->hi <= s.buildings.second) fits = s.id;
            }
            problem("scale.class", "a " + cls->id + " has " + std::to_string(cls->buildings.first) + "–" +
                                       std::to_string(cls->buildings.second) + " buildings" +
                                       (fits.empty() ? "" : "; these numbers make it a " + fits),
                    true);
        }
    }
    // The layout: one street pattern, or a mix of them ({"mix": {"grid": 0.4, "organic": 0.6}}).
    const ModuleDef* root = ontology.Module(build->root);
    const FieldDef* layoutField = root ? root->Field("layout") : nullptr;
    const json layout = brief.contains("shape") && brief["shape"].is_object() ? brief["shape"].value("layout", json()) : json();
    if (layoutField && !layout.is_null()) {
        if (std::string error = CheckFieldValue(ontology, *layoutField, layout); !error.empty()) problem("shape.layout", error);
    }

    const json& components = brief.value("components", json::object());
    if (!components.is_object()) {
        problem("components", "is an object: module -> its settings");
        return problems;
    }
    for (const auto& [id, c] : components.items()) {
        const std::string where = "components." + id;
        if (std::find(build->components.begin(), build->components.end(), id) == build->components.end()) {
            problem(where, "isn't a component of a " + build->id);
            continue;
        }
        const ModuleDef* module = ontology.Module(id);
        if (!c.is_object()) {
            problem(where, "is an object");
            continue;
        }
        if (!c.value("enabled", true)) {
            if (c.size() > 1) problem(where, "a disabled component has no other settings");
            continue;
        }
        if (c.contains("count")) {
            auto n = IntervalOf(c["count"]);
            if (!n || n->lo < 0 || n->lo > n->hi || std::floor(n->lo) != n->lo || std::floor(n->hi) != n->hi) {
                problem(where + ".count", "is a whole number or [low, high]");
            }
        }
        if (c.contains("density") &&
            (!c["density"].is_string() || std::find(kDensityWords.begin(), kDensityWords.end(), c["density"].get<std::string>()) == kDensityWords.end())) {
            problem(where + ".density", "is one of none, rare, some, common, abundant");
        }
        if (c.contains("assets")) {
            FieldDef choice;
            choice.type = "assetChoice";
            if (std::string error = CheckFieldValue(ontology, choice, c["assets"]); !error.empty()) problem(where + ".assets", error);
        }
        const json& params = c.value("params", json::object());
        if (params.is_object() && module) {
            for (const auto& [name, value] : params.items()) {
                const FieldDef* f = module->Field(name);
                if (f == nullptr) {
                    problem(where + ".params." + name, "a " + id + " has no field '" + name + "'");
                } else if (std::string error = CheckFieldValue(ontology, *f, value); !error.empty()) {
                    problem(where + ".params." + name, error);
                }
            }
        }
    }
    for (const auto& feature : brief.value("features", json::array())) {
        const std::string id = feature.is_object() ? feature.value("module", "") : "";
        const ModuleDef* module = ontology.Module(id);
        if (module == nullptr) {
            problem("features", "unknown feature '" + id + "'");
        } else if (!module->buildTypes.empty() &&
                   std::find(module->buildTypes.begin(), module->buildTypes.end(), build->id) == module->buildTypes.end()) {
            problem("features", "a " + build->id + " can't have a " + id);
        }
    }
    return problems;
}

// --- PLL --------------------------------------------------------------------------------

const std::vector<std::string>& LayoutOps() {
    static const std::vector<std::string> ops = {
        "fixed", "anchor", "region", "partition", "subdivide", "floorplan", "graph", "path", "network", "connect",
        "place", "line", "rows", "grid", "scatter", "fill", "relation", "objective", "check"};
    return ops;
}

namespace {

const std::vector<std::string> kSelectors = {"cells", "edge", "inner", "border", "center", "front", "back",
                                             "left", "right", "door", "street", "primary"};
const std::vector<std::string> kCheckPredicates = {"count", "density", "area", "footprint-density", "reachable",
                                                   "closed", "score", "filled"};

// The solving step of each statement kind inside its region (docs/layout-ir.md §7).
int StepOf(const json& s) {
    const std::string op = s.value("op", "");
    if (op == "fixed") return 0;
    if (op == "anchor") return s.contains("at") && s["at"].is_array() ? 0 : 4;
    if (op == "path" || op == "network") return 1;
    if (op == "partition" || op == "subdivide" || op == "floorplan" || op == "graph") return 2;
    if (op == "place") return s.value("primary", false) ? 3 : 5;
    if (op == "region" || op == "connect") return 4;
    if (op == "line") return 6;
    if (op == "rows" || op == "grid") return 7;
    if (op == "scatter") return 8;
    if (op == "fill") return 9;
    return 10; // relation, objective, check: evaluated when their members exist
}

// "p[3].house" and "p[*].house" are the same statement.
std::string Generic(std::string id) {
    for (size_t open = id.find('['); open != std::string::npos; open = id.find('[', open + 1)) {
        const size_t close = id.find(']', open);
        if (close == std::string::npos) break;
        id.replace(open + 1, close - open - 1, "*");
    }
    return id;
}

} // namespace

std::vector<PlaceProblem> ValidateLayout(const Ontology& ontology, const json& ir) {
    std::vector<PlaceProblem> problems;
    auto problem = [&](const std::string& where, const std::string& message) { problems.push_back({where, message, false}); };
    if (!ir.is_object() || ir.value("schema", "") != "aige.ir/1") {
        problem("layout", "not an aige.ir/1 document");
        return problems;
    }
    const json& statements = ir.value("statements", json::array());
    if (!statements.is_array()) {
        problem("layout", "needs a \"statements\" list");
        return problems;
    }

    // Every statement's id, and the ids its areas produce (zones, parcels, rooms, graph nodes).
    std::map<std::string, const json*> byId;
    std::set<std::string> areas;
    for (const auto& s : statements) {
        if (!s.is_object()) continue;
        const std::string id = Generic(s.value("id", ""));
        if (id.empty()) continue;
        if (byId.contains(id)) problem("statement " + id, "two statements have this id");
        byId[id] = &s;
        const std::string op = s.value("op", "");
        if (op == "region" || op == "subdivide") areas.insert(id);
        for (const char* list : {"into", "rooms", "nodes"}) {
            for (const auto& part : s.value(list, json::array())) {
                const std::string partId = part.is_object() ? part.value("id", "") : part.is_string() ? part.get<std::string>() : "";
                if (!partId.empty()) {
                    areas.insert(partId);
                    byId.emplace(partId, &s);
                }
            }
        }
    }
    // The statement a reference names (the longest prefix that is an id), and the selectors after it.
    auto resolve = [&](const std::string& ref, std::string& rest) -> const json* {
        const std::string generic = Generic(ref);
        std::string base = generic;
        while (true) {
            if (auto it = byId.find(base); it != byId.end()) {
                rest = generic.substr(base.size());
                return it->second;
            }
            const size_t dot = base.find_last_of(".");
            const size_t bracket = base.find_last_of("[");
            const size_t cut = dot == std::string::npos ? bracket : bracket == std::string::npos ? dot : std::max(dot, bracket);
            if (cut == std::string::npos || cut == 0) return nullptr;
            base = base.substr(0, cut);
        }
    };
    // The region a statement is in, and how deep that region sits. A statement
    // without "in" lives where what it runs along (or from, or refers to) is.
    auto containerOf = [](const json& s) -> std::string {
        for (const char* key : {"in", "along", "from", "at", "ref"}) {
            if (s.contains(key) && s[key].is_string()) return s[key].get<std::string>();
        }
        return {};
    };
    auto depthOf = [&](const json& s) {
        int depth = 0;
        std::string in = containerOf(s);
        std::set<std::string> seen;
        while (!in.empty() && depth < 64) {
            std::string rest;
            const json* parent = resolve(in.substr(0, in.find_first_of(" |&")), rest);
            if (parent == nullptr || !seen.insert(in).second) break;
            ++depth;
            in = containerOf(*parent);
        }
        return depth;
    };

    for (const auto& s : statements) {
        if (!s.is_object()) {
            problem("layout", "statements are objects");
            continue;
        }
        const std::string op = s.value("op", "");
        const std::string id = s.value("id", "");
        const std::string where = "statement " + (id.empty() ? op : id);
        if (std::find(LayoutOps().begin(), LayoutOps().end(), op) == LayoutOps().end()) {
            problem(where, "unknown statement '" + op + "'");
            continue;
        }
        if (s.value("node", "").empty()) problem(where, "names no Spec node (\"node\")");

        // Required fields.
        auto need = [&](std::initializer_list<const char*> fields, bool any = false) {
            bool some = false;
            std::vector<std::string> missing;
            for (const char* f : fields) {
                if (s.contains(f)) some = true;
                else missing.push_back(f);
            }
            if (any ? !some : !missing.empty()) problem(where, std::string("needs ") + (any ? "one of " : "") + Join(any ? std::vector<std::string>(fields.begin(), fields.end()) : missing));
        };
        if (op == "fixed") need({"cells", "entity", "ref"}, true);
        if (op == "anchor") need({"at"});
        if (op == "partition") need({"in", "into"});
        if (op == "subdivide") need({"in", "style"});
        if (op == "floorplan") need({"in", "rooms"});
        if (op == "graph") need({"in", "nodes", "edges"});
        if (op == "path") need({"from", "to"});
        if (op == "network") need({"in", "style"});
        if (op == "connect") need({"between", "kind"});
        if (op == "place") {
            need({"in"});
            need({"slot", "asset"}, true);
        }
        if (op == "line") need({"slot", "along"});
        if (op == "rows" || op == "grid") {
            need({"in"});
            if (!s.value("regions", false)) need({"slot", "mix"}, true); // rows of regions (beds) hold their own contents
        }
        if (op == "scatter") {
            need({"in", "density"});
            need({"slot", "mix"}, true);
        }
        if (op == "fill") {
            need({"in", "cells"}, true);
            need({"terrain"});
        }
        if (op == "relation") need({"type", "members"});
        if (op == "objective") need({"in"});
        if (op == "check") need({"predicate"});

        // References: each resolves, its selectors are known, and (relations
        // and checks aside) it names something solved before this.
        const int myDepth = depthOf(s);
        const int myStep = StepOf(s);
        auto checkRef = [&](const std::string& text, bool ordered) {
            // Region expressions: a | b, a & b, a - b.
            std::vector<std::string> parts;
            std::string current;
            for (size_t i = 0; i < text.size(); ++i) {
                const char c = text[i];
                const bool sep = (c == '|' || c == '&' || (c == '-' && i > 0 && text[i - 1] == ' ' && i + 1 < text.size() && text[i + 1] == ' '));
                if (sep) {
                    parts.push_back(current);
                    current.clear();
                } else {
                    current += c;
                }
            }
            parts.push_back(current);
            for (std::string part : parts) {
                part.erase(0, part.find_first_not_of(' '));
                part.erase(part.find_last_not_of(' ') + 1);
                if (part.empty() || part == "parent") continue;
                std::string rest;
                const json* target = resolve(part.substr(0, part.find('(')), rest);
                if (target == nullptr) {
                    problem(where, "refers to '" + part + "', which isn't a statement");
                    continue;
                }
                // Selectors: .name or .name(args)
                for (size_t dot = rest.find('.'); dot != std::string::npos; dot = rest.find('.', dot + 1)) {
                    const size_t end = rest.find_first_of(".(", dot + 1);
                    const std::string selector = rest.substr(dot + 1, end == std::string::npos ? std::string::npos : end - dot - 1);
                    const bool known = std::find(kSelectors.begin(), kSelectors.end(), selector) != kSelectors.end() ||
                                       selector == "side" || selector == "corner" || selector.rfind("anchor:", 0) == 0;
                    if (!known) problem(where, "'" + part + "': unknown selector '" + selector + "'");
                    if (end != std::string::npos && rest[end] == '(') dot = rest.find(')', end);
                    if (dot == std::string::npos) break;
                }
                if (ordered && target != &s) {
                    const int depth = depthOf(*target);
                    const int step = StepOf(*target);
                    const bool partOfArea = target->contains("into") || target->contains("rooms") || target->contains("nodes");
                    if (!partOfArea && (depth > myDepth || (depth == myDepth && step > myStep))) {
                        problem(where, "refers to '" + part + "', which is solved after it");
                    }
                }
            }
        };
        const bool ordered = op != "relation" && op != "check" && op != "objective";
        for (const char* key : {"in", "along", "from", "to", "ref"}) {
            if (s.contains(key) && s[key].is_string()) checkRef(s[key].get<std::string>(), ordered && std::string(key) != "in" ? true : ordered);
        }
        if (s.contains("at") && s["at"].is_string()) checkRef(s["at"].get<std::string>(), ordered);
        for (const char* key : {"gaps", "via", "between", "members", "avoid", "connect"}) {
            for (const auto& item : s.value(key, json::array())) {
                if (!item.is_string()) continue;
                std::string ref = item.get<std::string>();
                // Gaps may also name a side of the line's region: front, side:<s>, toward:<ref>.
                if (std::string(key) == "gaps") {
                    if (ref == "front" || ref.rfind("side:", 0) == 0) continue;
                    if (ref.rfind("toward:", 0) == 0) ref = ref.substr(7);
                }
                checkRef(ref, ordered);
            }
        }
        if (s.contains("in") && s["in"].is_string()) {
            std::string in = s["in"].get<std::string>();
            in = in.substr(0, in.find_first_of(" |&"));
            std::string rest;
            if (const json* target = resolve(in, rest); target && rest.empty() && !areas.contains(Generic(in))) {
                const std::string top = target->value("op", "");
                if (top != "region" && top != "subdivide" && top != "place" && top != "fixed") problem(where, "is in '" + in + "', which isn't an area");
            }
        }

        if (op == "relation") {
            const std::string type = s.value("type", "");
            const RelationType* rt = ontology.Relation(type);
            const int members = static_cast<int>(s.value("members", json::array()).size());
            if (rt == nullptr) problem(where, "unknown relation '" + type + "'");
            else if (members < rt->minMembers || (rt->maxMembers >= 0 && members > rt->maxMembers)) {
                problem(where, type + " takes " + std::to_string(rt->minMembers) +
                                   (rt->maxMembers == rt->minMembers ? "" : rt->maxMembers < 0 ? " or more" : "–" + std::to_string(rt->maxMembers)) +
                                   " members");
            }
            if (s.contains("weight") && (!s["weight"].is_number() || s["weight"].get<double>() <= 0.0)) problem(where, "a weight is a number above 0");
        }
        if (op == "check") {
            const std::string predicate = s.value("predicate", "");
            if (std::find(kCheckPredicates.begin(), kCheckPredicates.end(), predicate) == kCheckPredicates.end()) {
                problem(where, "unknown check '" + predicate + "'");
            }
            const json& args = s.value("args", json::object());
            if (args.is_object() && args.contains("ref") && args["ref"].is_string()) checkRef(args["ref"].get<std::string>(), false);
        }
        if (op == "scatter" && s.contains("density") && (!s["density"].is_number() || s["density"].get<double>() < 0.0 || s["density"].get<double>() > 1.0)) {
            problem(where, "density is objects per free cell, between 0 and 1");
        }
    }

    // Sub-areas of fixed size that can't fit their fixed-size parent.
    std::map<std::string, double> childArea;
    auto areaOf = [](const json& s) -> std::optional<double> {
        const json& size = s.value("size", json());
        if (size.is_object() && size.contains("w") && size.contains("h") && size["w"].is_number() && size["h"].is_number()) {
            return size["w"].get<double>() * size["h"].get<double>();
        }
        if (s.contains("area") && s["area"].is_number()) return s["area"].get<double>();
        return std::nullopt;
    };
    for (const auto& s : statements) {
        if (!s.is_object() || s.value("op", "") != "region" || !s.contains("in") || !s["in"].is_string()) continue;
        if (auto a = areaOf(s)) childArea[Generic(s["in"].get<std::string>())] += *a;
    }
    for (const auto& [parent, total] : childArea) {
        auto it = byId.find(parent);
        if (it == byId.end()) continue;
        if (auto a = areaOf(*it->second); a && total > *a) {
            problem("statement " + parent, "its regions need " + std::to_string(static_cast<int>(total)) + " cells but it has " +
                                               std::to_string(static_cast<int>(*a)));
        }
    }
    return problems;
}

} // namespace aige
