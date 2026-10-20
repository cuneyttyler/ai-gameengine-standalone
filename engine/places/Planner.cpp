#include "places/Planner.h"

#include "places/Brief.h"
#include "places/Expr.h"
#include "places/PlaceValidate.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <set>

namespace aige {

using json = nlohmann::json;

namespace {

std::string Slug(const std::string& text) {
    std::string out;
    for (const char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else if ((c == '-' || c == ' ' || c == '_') && !out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

double Mid(const json& v, double fallback) {
    const json& x = v.is_object() && v.size() == 1 ? v.begin().value() : v; // {"count": ...}, {"area": ...}
    if (x.is_number()) return x.get<double>();
    if (x.is_array() && x.size() == 2 && x[0].is_number() && x[1].is_number()) return (x[0].get<double>() + x[1].get<double>()) / 2.0;
    return fallback;
}

std::pair<double, double> Bounds(const json& v) {
    const json& x = v.is_object() && v.size() == 1 ? v.begin().value() : v;
    if (x.is_number()) return {x.get<double>(), x.get<double>()};
    if (x.is_array() && x.size() == 2 && x[0].is_number() && x[1].is_number()) return {x[0].get<double>(), x[1].get<double>()};
    return {-1, -1};
}

// A node's own fields for conditions.
class NodeScope : public places::Scope {
public:
    NodeScope(const ModuleDef& module, const json& node) : module_(module), node_(node) {}
    std::optional<places::Value> Name(const std::string& name) override {
        const FieldDef* f = module_.Field(name);
        if (f == nullptr) return std::nullopt;
        const json group = node_.value(f->quantity ? "quantity" : "quality", json::object());
        return FieldValueOf(*f, group.is_object() && group.contains(name) ? group[name] : json());
    }
    places::Value NodeField(int, const std::string&) override { return places::Value::Unknown(); }
    places::Value Below(int, bool, const std::string&, const std::string&) override { return places::Value::Unknown(); }
    int SelfNode() override { return 0; }

private:
    const ModuleDef& module_;
    const json& node_;
};

bool Holds(const std::string& condition, places::Scope& scope) {
    if (condition.empty()) return true;
    auto expr = places::Expr::Parse(condition);
    return !expr || expr->Holds(scope) != places::Tri::False;
}

// The Brief's settings for a module, into a node's fields (the Brief wins).
void ApplyBriefSettings(const Ontology& ontology, const json& brief, json& node) {
    const ModuleDef* m = ontology.Module(node.value("module", ""));
    if (m == nullptr) return;
    const json settings = BriefValue(brief, "components." + m->id);
    if (!settings.is_object()) return;
    const json params = settings.value("params", json::object());
    for (const auto& [name, value] : params.items()) {
        const FieldDef* f = m->Field(name);
        if (f == nullptr) continue;
        node[f->quantity ? "quantity" : "quality"][name] = value;
    }
}

void EnsureGroups(json& node) {
    for (const char* key : {"quantity", "quality"}) {
        if (!node.contains(key) || !node[key].is_object()) node[key] = json::object();
    }
}

// Keeps only fields the module has.
void KeepKnownFields(const ModuleDef& m, json& node) {
    for (const char* key : {"quantity", "quality"}) {
        json kept = json::object();
        for (const auto& [name, value] : node[key].items()) {
            const FieldDef* f = m.Field(name);
            if (f && f->quantity == (std::string(key) == "quantity")) kept[name] = value;
        }
        node[key] = kept;
    }
}

std::string UniqueId(std::string id, std::set<std::string>& used) {
    if (id.empty()) id = "node";
    std::string out = id;
    for (int n = 2; used.contains(out); ++n) out = id + "-" + std::to_string(n);
    used.insert(out);
    return out;
}

void CollectIds(const json& node, std::set<std::string>& ids) {
    if (node.contains("id") && node["id"].is_string()) ids.insert(node["id"].get<std::string>());
    if (node.contains("children") && node["children"].is_array()) {
        for (const auto& c : node["children"]) CollectIds(c, ids);
    }
}

json SubgraphWithRequest(const Ontology& ontology, const json& brief, const std::string& id) {
    json sub = ontology.Subgraph(id);
    if (sub.is_null()) return sub;
    const json settings = BriefValue(brief, "components." + id);
    if (settings.is_object()) sub["requested"] = settings;
    // Trim what the planner doesn't need, to keep prompts small.
    sub.erase("concepts");
    return sub;
}

// The modules a child rule allows: the module, or every module of a class.
std::vector<std::string> RuleModules(const Ontology& ontology, const ChildDef& rule) {
    std::vector<std::string> out;
    if (rule.module.rfind("class:", 0) == 0) {
        const std::string cls = rule.module.substr(6);
        for (const auto& [id, m] : ontology.Modules()) {
            if (ontology.IsA(id, cls)) out.push_back(id);
        }
    } else if (ontology.Module(rule.module)) {
        out.push_back(rule.module);
    }
    return out;
}

bool Disabled(const json& brief, const std::string& module) {
    const json c = BriefValue(brief, "components." + module);
    return c.is_object() && !c.value("enabled", true);
}

} // namespace

std::vector<PlaceProblem> TidySpec(const Ontology& ontology, json& spec) {
    std::vector<PlaceProblem> changed;
    auto note = [&](const std::string& where, const std::string& message) { changed.push_back({where, message, true}); };
    // A value of a field's vocabulary or list, as given or by a synonym.
    auto known = [&](const FieldDef& f, const std::string& value) -> std::string {
        if (!f.values.empty()) return std::find(f.values.begin(), f.values.end(), value) != f.values.end() ? value : "";
        if (f.vocabulary.empty()) return value;
        if (ontology.FindConcept(f.vocabulary, value)) return value;
        for (const Concept* c : ontology.ConceptsNamed(value)) {
            if (c->scheme == f.vocabulary) return c->id;
        }
        // Plurals: "onions" -> onion.
        if (value.size() > 3 && value.back() == 's' && ontology.FindConcept(f.vocabulary, value.substr(0, value.size() - 1))) {
            return value.substr(0, value.size() - 1);
        }
        return "";
    };
    std::function<void(json&)> walk = [&](json& node) {
        const ModuleDef* m = ontology.Module(node.value("module", ""));
        const std::string id = node.value("id", "");
        if (m) {
            for (const char* group : {"quantity", "quality"}) {
                if (!node.contains(group) || !node[group].is_object()) continue;
                std::vector<std::string> drop;
                for (auto& [name, value] : node[group].items()) {
                    const FieldDef* f = m->Field(name);
                    if (f == nullptr) continue;
                    const std::string where = id + "." + name;
                    if (f->type == "mix" && !value.is_string()) {
                        json& weights = value.is_object() && value.contains("mix") ? value["mix"] : value;
                        if (!weights.is_object()) continue;
                        json kept = json::object();
                        double total = 0;
                        for (const auto& [key, w] : weights.items()) {
                            const std::string to = known(*f, key);
                            if (to.empty() || !w.is_number() || w.get<double>() <= 0) {
                                note(where, "left out '" + key + "': not a known " + (f->vocabulary.empty() ? "value" : f->vocabulary));
                                continue;
                            }
                            if (to != key) note(where, "read '" + key + "' as " + to);
                            kept[to] = kept.value(to, 0.0) + w.get<double>();
                            total += w.get<double>();
                        }
                        if (kept.empty()) {
                            drop.push_back(name);
                            continue;
                        }
                        if (std::abs(total - 1.0) > 0.011) {
                            for (auto& [key, w] : kept.items()) w = std::round(w.get<double>() / total * 100) / 100;
                            note(where, "weights scaled to add up to 1");
                        }
                        weights = kept;
                    } else if (f->type == "enumSet" && value.is_array()) {
                        json kept = json::array();
                        for (const auto& item : value) {
                            const std::string to = item.is_string() ? known(*f, item.get<std::string>()) : "";
                            if (to.empty()) note(where, "left out " + item.dump() + ": not a known value");
                            else kept.push_back(to);
                        }
                        value = kept;
                    } else if ((f->type == "enum" || (f->type == "mix" && value.is_string())) && value.is_string()) {
                        const std::string to = known(*f, value.get<std::string>());
                        if (to.empty()) {
                            note(where, "'" + value.get<std::string>() + "' isn't a known value: the default is used");
                            drop.push_back(name);
                        } else if (to != value.get<std::string>()) {
                            note(where, "read '" + value.get<std::string>() + "' as " + to);
                            value = to;
                        }
                    }
                }
                for (const auto& name : drop) node[group].erase(name);
            }
            // Children counted by a field: as many as it says.
            for (const ChildDef& rule : m->children) {
                if (rule.count.empty() || !node.contains("children")) continue;
                std::vector<json*> matching;
                for (auto& c : node["children"]) {
                    if (c.value("module", "") == rule.module && !c.contains("share")) matching.push_back(&c);
                }
                const FieldDef* f = m->Field(rule.count);
                if (matching.size() != 1 || f == nullptr) continue;
                const json given = (f->quantity ? node["quantity"] : node["quality"]).value(rule.count, json());
                const places::Value v = FieldValueOf(*f, given);
                if (v.kind != places::Value::Kind::Number) continue;
                const int want = std::clamp(static_cast<int>(std::lround((v.number.lo + v.number.hi) / 2)), rule.min, rule.max);
                const int has = matching[0]->value("repeat", 1);
                if (has == want) continue;
                if (want >= 2) (*matching[0])["repeat"] = want;
                else matching[0]->erase("repeat");
                note(id, "made " + std::to_string(want) + " " + rule.module + (want == 1 ? "" : "s") + ", as its " + rule.count + " says");
            }
        }
        if (node.contains("children") && node["children"].is_array()) {
            for (auto& c : node["children"]) walk(c);
        }
    };
    if (spec.contains("root")) walk(spec["root"]);
    return changed;
}

namespace {

// A number field's value clamped into [min, max] (and whole, for a count), in the form it was given.
json ClampNumber(const FieldDef& f, const json& value) {
    auto clamp = [&](double v) {
        if (f.min) v = std::max(v, *f.min);
        if (f.max) v = std::min(v, *f.max);
        if (f.type == "share") v = std::clamp(v, 0.0, 1.0);
        if (f.type == "count") v = std::round(v);
        return v;
    };
    auto one = [&](const json& v) -> json {
        if (v.is_number()) {
            const double c = clamp(v.get<double>());
            return f.type == "count" ? json(static_cast<int>(c)) : json(c);
        }
        if (v.is_array() && v.size() == 2 && v[0].is_number() && v[1].is_number()) {
            double lo = clamp(v[0].get<double>()), hi = clamp(v[1].get<double>());
            if (lo > hi) std::swap(lo, hi);
            if (f.type == "count") return json::array({static_cast<int>(lo), static_cast<int>(hi)});
            return json::array({lo, hi});
        }
        return v;
    };
    if (value.is_object() && value.contains(f.type)) return {{f.type, one(value[f.type])}};
    return one(value);
}

json ClampSize(const FieldDef& f, json value) {
    if (!value.is_object()) return value;
    for (const char* side : {"w", "h"}) {
        if (!value.contains(side)) continue;
        const double lo = f.minSize.is_object() && f.minSize.contains(side) ? f.minSize[side].get<double>() : 1.0;
        const double hi = f.maxSize.is_object() && f.maxSize.contains(side) ? f.maxSize[side].get<double>() : 1e9;
        json& v = value[side];
        auto c = [&](double x) { return std::clamp(std::round(x), lo, hi); };
        if (v.is_number()) v = static_cast<int>(c(v.get<double>()));
        else if (v.is_array() && v.size() == 2 && v[0].is_number() && v[1].is_number()) {
            double a = c(v[0].get<double>()), b = c(v[1].get<double>());
            if (a > b) std::swap(a, b);
            v = json::array({static_cast<int>(a), static_cast<int>(b)});
        }
    }
    return value;
}

std::vector<std::string> NamesIn(const std::string& expression) {
    std::vector<std::string> names;
    std::string word;
    for (const char c : expression + " ") {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') word += c;
        else {
            if (!word.empty()) names.push_back(word);
            word.clear();
        }
    }
    return names;
}

} // namespace

std::vector<PlaceProblem> ResolveSpec(const Ontology& ontology, const json& brief, json& spec) {
    std::vector<PlaceProblem> notes;
    auto note = [&](const std::string& where, const std::string& message) { notes.push_back({where, message, true}); };
    if (!spec.contains("root")) return notes;

    // Fields and children, node by node.
    std::function<void(json&)> fix = [&](json& node) {
        const ModuleDef* m = ontology.Module(node.value("module", ""));
        if (m == nullptr) return;
        const std::string id = node.value("id", "");
        EnsureGroups(node);
        for (const char* group : {"quantity", "quality"}) {
            std::vector<std::string> drop;
            for (auto& [name, value] : node[group].items()) {
                const FieldDef* f = m->Field(name);
                if (f == nullptr || f->quantity != (std::string(group) == "quantity")) {
                    note(id + "." + name, "left out: a " + m->id + " has no such field");
                    drop.push_back(name);
                    continue;
                }
                if (CheckFieldValue(ontology, *f, value).empty()) continue;
                const json fixed = f->type == "size" ? ClampSize(*f, value) : ClampNumber(*f, value);
                if (fixed != value && CheckFieldValue(ontology, *f, fixed).empty()) {
                    note(id + "." + name, "brought into range: " + fixed.dump());
                    value = fixed;
                } else {
                    note(id + "." + name, "couldn't be read (" + value.dump().substr(0, 60) + "): the default is used");
                    drop.push_back(name);
                }
            }
            for (const auto& name : drop) node[group].erase(name);
        }
        // Fields that don't apply here.
        NodeScope scope(*m, node);
        for (const char* group : {"quantity", "quality"}) {
            std::vector<std::string> drop;
            for (const auto& [name, value] : node[group].items()) {
                const FieldDef* f = m->Field(name);
                if (f && !f->when.empty()) {
                    if (auto cond = places::Expr::Parse(f->when); cond && cond->Holds(scope) == places::Tri::False) drop.push_back(name);
                }
            }
            for (const auto& name : drop) {
                node[group].erase(name);
                note(id + "." + name, "left out: it only applies when " + m->Field(name)->when);
            }
        }
        // A count of parts the Brief turned off (a farmland ring's farmsteads with
        // farmsteads off): none can be made, so the plan says none.
        for (const ChildDef& rule : m->children) {
            if (rule.count.empty() || !Disabled(brief, rule.module)) continue;
            const FieldDef* f = m->Field(rule.count);
            if (f == nullptr) continue;
            json& group = node[f->quantity ? "quantity" : "quality"];
            if (!group.contains(rule.count) || Mid(group[rule.count], 0) <= 0) continue;
            group[rule.count] = 0;
            note(id + "." + rule.count, "set to 0: " + rule.module + " is turned off in the Brief");
        }
        if (!node.contains("children") || !node["children"].is_array()) return;
        // Children it can't hold.
        json kept = json::array();
        for (auto& c : node["children"]) {
            const ModuleDef* cm = ontology.Module(c.value("module", ""));
            const bool outward = cm && static_cast<int>(cm->level) < static_cast<int>(m->level) && cm->level != PlaceLevel::Abstract &&
                                 cm->level != PlaceLevel::Network;
            if (cm == nullptr || !ontology.ChildRule(m->id, cm->id) || outward) {
                note(c.value("id", "?"), "left out: a " + m->id + " can't hold a " + c.value("module", "?"));
                continue;
            }
            kept.push_back(c);
        }
        node["children"] = kept;
        // Too many of a rule; shares over 1.
        for (const ChildDef& rule : m->children) {
            double total = 0;
            std::vector<json*> mine;
            for (auto& c : node["children"]) {
                if (ontology.ChildRule(m->id, c.value("module", "")) != &rule) continue;
                mine.push_back(&c);
                total += Mid(c.value("repeat", json()), 1) * (c.contains("share") ? Mid(c["share"], 1) : 1);
            }
            if (total > rule.max && total > 0) {
                const double factor = rule.max / total;
                for (json* c : mine) {
                    const int repeat = std::max(1, static_cast<int>(std::floor(Mid(c->value("repeat", json()), 1) * factor)));
                    if (repeat >= 2) (*c)["repeat"] = repeat;
                    else c->erase("repeat");
                }
                note(id, "at most " + std::to_string(rule.max) + " " + rule.module + ": scaled down");
            }
        }
        std::map<std::string, double> shares;
        for (const auto& c : node["children"]) {
            if (c.contains("share") && c["share"].is_number()) shares[c.value("module", "")] += c["share"].get<double>();
        }
        for (const auto& [module, sum] : shares) {
            if (sum <= 1.0001) continue;
            for (auto& c : node["children"]) {
                if (c.value("module", "") == module && c.contains("share")) c["share"] = std::round(c["share"].get<double>() / sum * 100) / 100;
            }
            note(id, "the shares of its " + module + " children added up to more than 1: scaled to 1");
        }
        for (auto& c : node["children"]) fix(c);
    };
    fix(spec["root"]);

    // A ward left without parcels (its step gave none): its module's typical ones.
    std::set<std::string> used;
    CollectIds(spec["root"], used);
    for (auto& zone : spec["root"]["children"]) {
        const ModuleDef* zm = ontology.Module(zone.value("module", ""));
        if (zm == nullptr || zm->level != PlaceLevel::Zone || zm->Field("plots") == nullptr) continue;
        if (!zone.contains("children") || !zone["children"].is_array()) zone["children"] = json::array();
        const bool hasParcels = std::any_of(zone["children"].begin(), zone["children"].end(), [&](const json& c) {
            return ontology.IsA(c.value("module", ""), "BuildingParcel");
        });
        if (hasParcels) continue;
        const json fallback = FallbackZoneAnswer(ontology, zone);
        int added = 0;
        for (const auto& c : fallback["children"]) {
            const std::string module = c.value("module", "");
            if (Disabled(brief, module)) continue;
            const bool present = std::any_of(zone["children"].begin(), zone["children"].end(),
                                             [&](const json& k) { return k.value("module", "") == module; });
            if (present) continue;
            json node = {{"id", UniqueId(zone.value("id", "") + "/" + module, used)}, {"module", module},
                         {"quantity", json::object()}, {"quality", json::object()}, {"rationale", c.value("rationale", "")}};
            if (c.contains("repeat")) node["repeat"] = c["repeat"];
            ApplyBriefSettings(ontology, brief, node);
            zone["children"].push_back(node);
            added += ontology.IsA(module, "BuildingParcel") ? static_cast<int>(Mid(c.value("repeat", json()), 1)) : 0;
        }
        if (added > 0) note(zone.value("id", ""), "had no parcels: given " + std::to_string(added) + " from its module's typical ones");
    }

    // What a part needs, or rules out, anywhere in the place.
    for (int round = 0; round < 4; ++round) {
        std::vector<std::string> modules;
        std::function<void(const json&)> collect = [&](const json& n) {
            modules.push_back(n.value("module", ""));
            for (const auto& c : n.value("children", json::array())) collect(c);
        };
        collect(spec["root"]);
        auto anywhere = [&](const std::string& target, const std::string& except) {
            const std::string cls = target.rfind("class:", 0) == 0 ? target.substr(6) : target;
            return std::any_of(modules.begin(), modules.end(), [&](const std::string& m) {
                return m != except && (m == cls || ontology.IsA(m, cls));
            });
        };
        bool removed = false;
        std::function<void(json&)> prune = [&](json& n) {
            if (!n.contains("children")) return;
            json kept = json::array();
            for (auto& c : n["children"]) {
                const ModuleDef* cm = ontology.Module(c.value("module", ""));
                std::string why;
                if (cm) {
                    for (const auto& t : cm->required) {
                        if (!anywhere(t, "")) why = "a " + cm->id + " needs a " + (t.rfind("class:", 0) == 0 ? t.substr(6) : t) + ", which the place doesn't have";
                    }
                    for (const auto& t : cm->excludes) {
                        if (anywhere(t, cm->id)) why = "a " + cm->id + " can't be in a place with a " + t;
                    }
                }
                if (!why.empty()) {
                    note(c.value("id", "?"), "left out: " + why);
                    removed = true;
                    continue;
                }
                prune(c);
                kept.push_back(c);
            }
            n["children"] = kept;
        };
        prune(spec["root"]);
        if (!removed) break;
    }

    // Constraints a node breaks: the fields they name go back to their defaults.
    for (const auto& p : ValidateSpec(ontology, spec, SpecStage::Final, brief.value("buildType", ""))) {
        if (p.warning || p.message.rfind("can't meet ", 0) != 0) continue;
        json* node = SpecNode(spec, p.where);
        const ModuleDef* m = node ? ontology.Module(node->value("module", "")) : nullptr;
        if (m == nullptr) continue;
        std::vector<std::string> reset;
        for (const auto& name : NamesIn(p.message.substr(11))) {
            const FieldDef* f = m->Field(name);
            if (f == nullptr) continue;
            const char* group = f->quantity ? "quantity" : "quality";
            if ((*node)[group].contains(name)) {
                (*node)[group].erase(name);
                reset.push_back(name);
            }
        }
        if (!reset.empty()) {
            std::string list;
            for (const auto& r : reset) list += (list.empty() ? "" : ", ") + r;
            note(p.where, "broke " + p.message.substr(11) + ": " + list + " back to the module's defaults");
        }
    }
    return notes;
}

json FallbackRootAnswer(const Ontology& ontology, const json& brief) {
    const BuildType* build = ontology.Build(brief.value("buildType", ""));
    json children = json::array();
    if (build == nullptr) return {{"children", children}};
    struct Zone {
        std::string module;
        int count;
        double weight;
    };
    std::vector<Zone> zones;
    const json components = brief.value("components", json::object());
    for (const auto& [id, c] : components.items()) {
        const ModuleDef* m = ontology.Module(id);
        if (m == nullptr || m->level != PlaceLevel::Zone || !c.is_object() || !c.value("enabled", true) || !ontology.ChildRule(build->root, id)) continue;
        const int count = std::max(1, static_cast<int>(std::lround(Mid(c.value("count", json()), 1))));
        const double weight = ontology.IsA(id, "NatureArea") ? 0.0 : id == "residential-ward" ? 3.0 : id == "craftsmen-ward" ? 1.5 : 1.0;
        zones.push_back({id, count, weight});
    }
    if (zones.empty()) {
        if (ontology.ChildRule(build->root, "residential-ward")) zones.push_back({"residential-ward", 1, 3.0});
        if (ontology.ChildRule(build->root, "market-ward")) zones.push_back({"market-ward", 1, 1.0});
    }
    const double buildings = Mid(BriefValue(brief, "scale.buildings"), 30);
    const double area = Mid(BriefValue(brief, "scale.area"), 6400);
    double totalWeight = 0;
    for (const auto& z : zones) totalWeight += z.weight * z.count;
    for (const auto& z : zones) {
        for (int k = 0; k < z.count; ++k) {
            const double share = totalWeight > 0 ? z.weight / totalWeight : 0;
            const int plots = static_cast<int>(std::lround(buildings * share));
            json node = {{"id", z.module + (z.count > 1 ? "-" + std::to_string(k + 1) : "")}, {"module", z.module},
                         {"quantity", {{"area", {{"area", static_cast<int>(std::lround(share > 0 ? area * 0.7 * share : area * 0.08))}}}}},
                         {"rationale", "planned from the Brief alone"}};
            if (plots > 0 && ontology.Module(z.module)->Field("plots")) node["quantity"]["plots"] = {{"count", plots}};
            children.push_back(node);
        }
    }
    return {{"children", children}, {"rationale", "Planned from the Brief alone: the AI couldn't plan this place."}};
}

json FallbackZoneAnswer(const Ontology& ontology, const json& zone) {
    const ModuleDef* m = ontology.Module(zone.value("module", ""));
    json children = json::array();
    if (m == nullptr) return {{"children", children}};
    const json plotsField = zone.contains("quantity") ? zone["quantity"].value("plots", json()) : json();
    int plots = static_cast<int>(std::lround(Mid(plotsField, -1)));
    if (plots < 0) {
        const FieldDef* f = m->Field("plots");
        plots = f ? static_cast<int>(std::lround(Mid(f->defaultValue, 10))) : 0;
    }
    // Building parcels share the plots by their weights (largest remainder); required others once.
    std::vector<std::pair<std::string, double>> weighted;
    for (const ChildDef& rule : m->children) {
        if (rule.module.rfind("class:", 0) == 0 || !ontology.Module(rule.module)) continue;
        if (ontology.IsA(rule.module, "BuildingParcel")) {
            weighted.push_back({rule.module, rule.weight});
        } else if (rule.min >= 1) {
            children.push_back({{"id", zone.value("id", "") + "/" + rule.module}, {"module", rule.module}, {"rationale", "the zone needs one"}});
        }
    }
    double total = 0;
    for (const auto& [module, w] : weighted) total += w;
    std::vector<std::pair<int, double>> counts;
    int given = 0;
    for (const auto& [module, w] : weighted) {
        const double exact = total > 0 ? plots * w / total : 0;
        counts.push_back({static_cast<int>(std::floor(exact)), exact - std::floor(exact)});
        given += static_cast<int>(std::floor(exact));
    }
    for (int left = plots - given; left > 0 && !counts.empty(); --left) {
        auto best = std::max_element(counts.begin(), counts.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
        best->first += 1;
        best->second = -1;
    }
    for (size_t i = 0; i < weighted.size(); ++i) {
        if (counts[i].first <= 0) continue;
        json child = {{"id", zone.value("id", "") + "/" + weighted[i].first}, {"module", weighted[i].first}, {"rationale", "from the zone's typical parcels"}};
        if (counts[i].first >= 2) child["repeat"] = counts[i].first;
        children.push_back(child);
    }
    return {{"children", children}, {"rationale", "Planned from the module rules: the AI couldn't plan this zone."}};
}

double DensityShare(const std::string& word) {
    if (word == "none") return 0.0;
    if (word == "rare") return 0.15;
    if (word == "some") return 0.35;
    if (word == "common") return 0.6;
    if (word == "abundant") return 0.85;
    return -1.0;
}

json PlanRootContext(const Ontology& ontology, const json& brief) {
    const BuildType* build = ontology.Build(brief.value("buildType", ""));
    if (build == nullptr) return json::object();
    json modules = json::array();
    std::set<std::string> seen;
    if (const ModuleDef* root = ontology.Module(build->root)) {
        for (const ChildDef& rule : root->children) {
            for (const auto& id : RuleModules(ontology, rule)) {
                if (Disabled(brief, id) || !seen.insert(id).second) continue;
                // Zones the build type offers, and what the root needs.
                const bool component = std::find(build->components.begin(), build->components.end(), id) != build->components.end();
                if (!component && rule.min == 0 && rule.module.rfind("class:", 0) != 0) {
                    json brief_ = BriefValue(brief, "components." + id);
                    if (!brief_.is_object()) continue;
                }
                modules.push_back(SubgraphWithRequest(ontology, brief, id));
            }
        }
    }
    const ScaleClass* scale = build->Scale(BriefValue(brief, "scale.class").is_string() ? BriefValue(brief, "scale.class").get<std::string>() : "");
    json context = {{"root", ontology.Subgraph(build->root)}, {"modules", modules}};
    if (scale) context["scale"] = {{"class", scale->id}, {"zones", {scale->zones.first, scale->zones.second}}};
    return context;
}

json PlanZoneContext(const Ontology& ontology, const json& brief, const std::string& zoneModule) {
    const ModuleDef* zone = ontology.Module(zoneModule);
    if (zone == nullptr) return json::object();
    json modules = json::array();
    std::set<std::string> seen;
    for (const ChildDef& rule : zone->children) {
        for (const auto& id : RuleModules(ontology, rule)) {
            if (Disabled(brief, id) || !seen.insert(id).second) continue;
            modules.push_back(SubgraphWithRequest(ontology, brief, id));
        }
    }
    return {{"zone", SubgraphWithRequest(ontology, brief, zoneModule)}, {"modules", modules}};
}

json* SpecNode(json& spec, const std::string& id) {
    std::function<json*(json&)> find = [&](json& node) -> json* {
        if (node.value("id", "") == id) return &node;
        if (node.contains("children") && node["children"].is_array()) {
            for (auto& c : node["children"]) {
                if (json* hit = find(c)) return hit;
            }
        }
        return nullptr;
    };
    return spec.contains("root") ? find(spec["root"]) : nullptr;
}

json SpecFromRoot(const Ontology& ontology, const json& brief, const json& answer, const std::string& placeId) {
    const BuildType* build = ontology.Build(brief.value("buildType", ""));
    json spec = {{"schema", "aige.spec/1"}, {"brief", "brief.json"}};
    if (build == nullptr) return spec;
    const ModuleDef* rootModule = ontology.Module(build->root);
    json root = {{"id", placeId}, {"module", build->root}};
    root["quantity"] = answer.value("quantity", json::object());
    root["quality"] = answer.value("quality", json::object());
    EnsureGroups(root);
    // The Brief's numbers and choices win.
    const auto [lo, hi] = Bounds(BriefValue(brief, "scale.buildings"));
    if (lo >= 0) {
        const double given = Mid(root["quantity"].value("buildings", json()), -1);
        const double buildings = given >= lo && given <= hi ? given : std::round((lo + hi) / 2);
        root["quantity"]["buildings"] = {{"count", static_cast<int>(buildings)}};
    }
    if (BriefValue(brief, "scale.area").is_object()) {
        root["quantity"]["area"] = {{"area", static_cast<int>(Mid(BriefValue(brief, "scale.area"), 3600))}};
    }
    if (const json layout = BriefValue(brief, "shape.layout"); !layout.is_null()) root["quality"]["layout"] = layout;
    if (const json walled = BriefValue(brief, "walled"); walled.is_string()) root["quality"]["walled"] = walled;
    if (const json condition = BriefValue(brief, "style.condition"); condition.is_string()) root["quality"]["condition"] = condition;
    if (rootModule) KeepKnownFields(*rootModule, root);
    root["rationale"] = answer.value("rationale", "");

    std::set<std::string> used = {placeId};
    json children = json::array();
    std::map<std::string, std::string> renamed;
    for (const auto& c : answer.value("children", json::array())) {
        const std::string module = c.value("module", "");
        const ModuleDef* m = ontology.Module(module);
        if (m == nullptr || Disabled(brief, module) || !ontology.ChildRule(build->root, module)) continue;
        json node = {{"id", ""}, {"module", module}, {"quantity", c.value("quantity", json::object())},
                     {"quality", c.value("quality", json::object())}, {"rationale", c.value("rationale", "")}};
        EnsureGroups(node);
        const std::string wanted = Slug(c.value("id", module));
        node["id"] = UniqueId(wanted.empty() ? module : wanted, used);
        renamed[c.value("id", "")] = node["id"];
        ApplyBriefSettings(ontology, brief, node);
        KeepKnownFields(*m, node);
        if (c.contains("rel") && c["rel"].is_array()) node["rel"] = c["rel"];
        children.push_back(node);
    }
    // What the Brief asks for and the answer left out, and what the root needs.
    auto has = [&](const std::string& module) {
        return std::any_of(children.begin(), children.end(), [&](const json& n) { return n["module"] == module; });
    };
    auto add = [&](const std::string& module) {
        json node = {{"id", UniqueId(module, used)}, {"module", module}, {"quantity", json::object()},
                     {"quality", json::object()}, {"rationale", "asked for in the Brief"}};
        ApplyBriefSettings(ontology, brief, node);
        children.push_back(node);
    };
    const auto briefComponents = brief.value("components", json::object());
    for (const auto& [id, c] : briefComponents.items()) {
        if (!c.is_object() || !c.value("enabled", true) || has(id) || !ontology.ChildRule(build->root, id)) continue;
        add(id);
    }
    if (const json walled = BriefValue(brief, "walled"); walled.is_string() && walled != "none" && !has("wall")) add("wall");
    for (const auto& f : brief.value("features", json::array())) {
        const std::string id = f.is_object() ? f.value("module", "") : "";
        if (ontology.Module(id) && ontology.ChildRule(build->root, id) && !has(id)) add(id);
    }
    if (rootModule) {
        for (const ChildDef& rule : rootModule->children) {
            if (rule.min < 1 || rule.module.rfind("class:", 0) == 0) continue;
            if (!has(rule.module)) add(rule.module);
        }
    }
    // Relations name the final ids, and only ones that exist: a part the
    // answer named, or one added above by its module ("along the river").
    for (const auto& node : children) renamed.emplace(node.value("module", ""), node.value("id", ""));
    for (auto& node : children) {
        if (!node.contains("rel")) continue;
        json kept = json::array();
        for (auto rel : node["rel"]) {
            if (!rel.is_object() || !ontology.Relation(rel.value("type", ""))) continue;
            bool ok = true;
            for (const char* key : {"to", "of"}) {
                if (!rel.contains(key)) continue;
                auto it = renamed.find(rel.value(key, ""));
                if (it == renamed.end()) ok = false;
                else rel[key] = it->second;
            }
            if (ok) kept.push_back(rel);
        }
        node["rel"] = kept;
    }
    root["children"] = children;
    spec["root"] = root;
    return spec;
}

std::vector<std::string> ZoneIds(const Ontology& ontology, const json& spec) {
    std::vector<std::string> out;
    if (!spec.contains("root")) return out;
    for (const auto& c : spec["root"].value("children", json::array())) {
        const ModuleDef* m = ontology.Module(c.value("module", ""));
        if (m && m->level == PlaceLevel::Zone && !m->children.empty()) out.push_back(c.value("id", ""));
    }
    return out;
}

void ApplyZoneAnswer(const Ontology& ontology, const json& brief, json& spec, const std::string& zoneId, const json& answer) {
    json* zone = SpecNode(spec, zoneId);
    if (zone == nullptr) return;
    const ModuleDef* zm = ontology.Module(zone->value("module", ""));
    if (zm == nullptr) return;
    EnsureGroups(*zone);
    for (const char* key : {"quantity", "quality"}) {
        const auto fields = answer.value(key, json::object());
        for (const auto& [name, value] : fields.items()) (*zone)[key][name] = value;
    }
    ApplyBriefSettings(ontology, brief, *zone);
    KeepKnownFields(*zm, *zone);
    if (answer.contains("rationale")) (*zone)["rationale"] = answer["rationale"];
    // Its parcels are replaced: their ids are free again (a kept parcel keeps its id).
    (*zone)["children"] = json::array();
    std::set<std::string> used;
    CollectIds(spec["root"], used);
    json children = json::array();
    for (const auto& c : answer.value("children", json::array())) {
        const std::string module = c.value("module", "");
        const ModuleDef* m = ontology.Module(module);
        if (m == nullptr || Disabled(brief, module) || !ontology.ChildRule(zm->id, module)) continue;
        std::string name = c.value("id", module);
        if (name.rfind(zoneId + "/", 0) == 0) name = name.substr(zoneId.size() + 1);
        json node = {{"id", UniqueId(zoneId + "/" + Slug(name.empty() ? module : name), used)}, {"module", module},
                     {"quantity", c.value("quantity", json::object())}, {"quality", c.value("quality", json::object())},
                     {"rationale", c.value("rationale", "")}};
        EnsureGroups(node);
        const double repeat = Mid(c.value("repeat", json()), 1);
        if (repeat >= 2) node["repeat"] = static_cast<int>(std::round(repeat));
        if (c.contains("share") && c["share"].is_number()) node["share"] = std::clamp(c["share"].get<double>(), 0.0, 1.0);
        ApplyBriefSettings(ontology, brief, node);
        KeepKnownFields(*m, node);
        children.push_back(node);
    }
    (*zone)["children"] = children;
}

void ExpandSpec(const Ontology& ontology, const json& brief, json& spec) {
    std::set<std::string> used;
    CollectIds(spec["root"], used);
    // depthBelowParcel: 0 at a parcel; expansion stops two levels below it.
    std::function<void(json&, int)> expand = [&](json& node, int depth) {
        const ModuleDef* m = ontology.Module(node.value("module", ""));
        if (m == nullptr) return;
        EnsureGroups(node);
        if (!node.contains("children") || !node["children"].is_array()) node["children"] = json::array();
        json& children = node["children"];
        const bool belowZone = m->level == PlaceLevel::Parcel || m->level == PlaceLevel::Feature;
        NodeScope scope(*m, node);
        std::set<std::string> present;
        for (const auto& c : children) present.insert(c.value("module", ""));
        for (const ChildDef& rule : m->children) {
            if (rule.module.rfind("class:", 0) == 0) continue;
            const ModuleDef* cm = ontology.Module(rule.module);
            if (cm == nullptr || present.contains(cm->id) || Disabled(brief, cm->id) || !Holds(rule.when, scope)) continue;
            if (cm->level == PlaceLevel::Dressing) continue; // dressing comes with the compile step
            // Nor a part that can't be made: one that needs something the Brief turned
            // off (a pasture needs its fence, and fences are off).
            const bool needsDisabled = std::any_of(cm->children.begin(), cm->children.end(), [&](const ChildDef& r) {
                return r.min >= 1 && r.when.empty() && r.module.rfind("class:", 0) != 0 && Disabled(brief, r.module);
            });
            if (needsDisabled) continue;
            json child = {{"id", ""}, {"module", cm->id}, {"quantity", json::object()}, {"quality", json::object()}};
            if (rule.min >= 1 && !rule.count.empty()) {
                // As many as the parent's field says (a docks' piers), within the rule.
                const FieldDef* f = m->Field(rule.count);
                const places::Value v = f ? FieldValueOf(*f, (f->quantity ? node["quantity"] : node["quality"]).value(rule.count, json()))
                                          : places::Value::Unknown();
                int n = rule.min;
                if (v.kind == places::Value::Kind::Number) n = static_cast<int>(std::lround((v.number.lo + v.number.hi) / 2));
                n = std::clamp(n, rule.min, rule.max);
                if (n >= 2) child["repeat"] = n;
            } else if (rule.min >= 1) {
                if (rule.min >= 2) child["repeat"] = rule.min; // the minimum, at least
            } else if (!rule.count.empty()) {
                // Tied to a field: a zone's (a farmland ring's farmsteads and fields)
                // are made here, as many as it says; below a zone the compiler
                // writes them (a garden's beds, as rows).
                if (m->level != PlaceLevel::Zone) continue;
                const FieldDef* f = m->Field(rule.count);
                const places::Value v = f ? FieldValueOf(*f, (f->quantity ? node["quantity"] : node["quality"]).value(rule.count, json()))
                                          : places::Value::Unknown();
                if (v.kind != places::Value::Kind::Number) continue;
                const int n = std::clamp(static_cast<int>(std::lround((v.number.lo + v.number.hi) / 2)), 0, rule.max);
                if (n <= 0) continue;
                if (n >= 2) child["repeat"] = n;
            } else if (belowZone && depth < 2) {
                // Typical children: as often as the rule's weight, or the Brief's density for the component.
                const json density = BriefValue(brief, "components." + cm->id + ".density");
                double share = density.is_string() ? DensityShare(density.get<std::string>()) : rule.weight;
                if (share < 0) share = rule.weight;
                if (share < 0.05) continue;
                if (share < 0.999) child["share"] = std::round(share * 100) / 100;
            } else {
                continue;
            }
            child["id"] = UniqueId(node.value("id", "") + "/" + cm->id, used);
            child["rationale"] = "expansion rule: a " + m->id + " typically has a " + cm->id;
            ApplyBriefSettings(ontology, brief, child);
            children.push_back(child);
            present.insert(cm->id);
        }
        for (auto& c : children) {
            const ModuleDef* cm = ontology.Module(c.value("module", ""));
            const bool parcel = cm && cm->level == PlaceLevel::Parcel;
            expand(c, parcel ? 0 : (belowZone ? depth + 1 : depth));
        }
    };
    expand(spec["root"], 0);
}

int CountBuildings(const Ontology& ontology, const json& spec) {
    double total = 0;
    std::function<void(const json&, double)> walk = [&](const json& node, double times) {
        double here = times * Mid(node.value("repeat", json()), 1) * (node.contains("share") ? Mid(node["share"], 1) : 1);
        if (ontology.IsA(node.value("module", ""), "BuildingParcel")) total += here;
        for (const auto& c : node.value("children", json::array())) walk(c, here);
    };
    if (spec.contains("root")) walk(spec["root"], 1);
    return static_cast<int>(std::lround(total));
}

int RebalanceBuildings(const Ontology& ontology, const json& brief, json& spec) {
    int count = CountBuildings(ontology, spec);
    const auto [lo, hi] = Bounds(BriefValue(brief, "scale.buildings"));
    if (lo >= 0 && count > 0 && (count < lo || count > hi)) {
        const double factor = ((lo + hi) / 2.0) / count;
        for (auto& zone : spec["root"]["children"]) {
            for (auto& c : zone["children"]) {
                if (!ontology.IsA(c.value("module", ""), "BuildingParcel")) continue;
                const double repeat = Mid(c.value("repeat", json()), 1);
                const int scaled = std::max(1, static_cast<int>(std::lround(repeat * factor)));
                if (scaled >= 2) c["repeat"] = scaled;
                else c.erase("repeat");
            }
        }
        count = CountBuildings(ontology, spec);
    }
    // The zones' plots and the root's buildings say what's there.
    for (auto& zone : spec["root"]["children"]) {
        const ModuleDef* zm = ontology.Module(zone.value("module", ""));
        if (zm == nullptr || zm->Field("plots") == nullptr || !zone.contains("children")) continue;
        double plots = 0;
        for (const auto& c : zone["children"]) {
            if (ontology.IsA(c.value("module", ""), "BuildingParcel")) {
                plots += Mid(c.value("repeat", json()), 1) * (c.contains("share") ? Mid(c["share"], 1) : 1);
            }
        }
        if (plots > 0) zone["quantity"]["plots"] = {{"count", static_cast<int>(std::lround(plots))}};
    }
    spec["root"]["quantity"]["buildings"] = {{"count", count}};
    return count;
}

json PlanSummary(const Ontology& ontology, const json& spec) {
    json rows = json::array();
    int zones = 0;
    static const std::vector<std::string> keyFields = {"street-style", "wealth", "density", "trades", "grandeur", "kind",
                                                       "class", "surface", "material", "faith", "terrain"};
    if (!spec.contains("root")) return {{"rows", rows}};
    for (const auto& c : spec["root"].value("children", json::array())) {
        const ModuleDef* m = ontology.Module(c.value("module", ""));
        if (m == nullptr) continue;
        if (m->level == PlaceLevel::Zone) ++zones;
        json one = {{"schema", "aige.spec/1"}, {"root", c}};
        json qualities = json::object();
        for (const auto& f : keyFields) {
            if (c.contains("quality") && c["quality"].contains(f)) qualities[f] = c["quality"][f];
        }
        json parcels = json::array();
        for (const auto& p : c.value("children", json::array())) {
            const ModuleDef* pm = ontology.Module(p.value("module", ""));
            if (pm == nullptr || pm->level != PlaceLevel::Parcel) continue;
            parcels.push_back({{"id", p.value("id", "")}, {"module", pm->id}, {"name", pm->name},
                               {"count", static_cast<int>(Mid(p.value("repeat", json()), 1))}});
        }
        rows.push_back({{"id", c.value("id", "")}, {"module", m->id}, {"name", m->name}, {"level", PlaceLevelName(m->level)},
                        {"plots", c.contains("quantity") ? c["quantity"].value("plots", json()) : json()},
                        {"buildings", CountBuildings(ontology, one)},
                        {"area", c.contains("quantity") ? c["quantity"].value("area", json()) : json()},
                        {"qualities", qualities}, {"parcels", parcels}, {"rationale", c.value("rationale", "")}});
    }
    return {{"rows", rows},
            {"totals", {{"buildings", CountBuildings(ontology, spec)}, {"zones", zones},
                        {"area", spec["root"].contains("quantity") ? spec["root"]["quantity"].value("area", json()) : json()}}},
            {"rationale", spec["root"].value("rationale", "")}};
}

json PlanSketch(const Ontology& ontology, const json& spec, int side) {
    struct Item {
        std::string id, module, name;
        double area;
    };
    std::vector<Item> items;
    if (spec.contains("root")) {
        for (const auto& c : spec["root"].value("children", json::array())) {
            const ModuleDef* m = ontology.Module(c.value("module", ""));
            if (m == nullptr || m->level != PlaceLevel::Zone) continue;
            double area = Mid(c.contains("quantity") ? c["quantity"].value("area", json()) : json(), -1);
            if (area <= 0) area = Mid(c.contains("quantity") ? c["quantity"].value("plots", json()) : json(), 10) * 60;
            items.push_back({c.value("id", ""), m->id, m->name, area});
        }
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.area > b.area; });
    json rects = json::array();
    double total = 0;
    for (const auto& i : items) total += i.area;
    if (total <= 0 || side <= 0) return {{"side", side}, {"rects", rects}};
    // Squarified treemap (Bruls et al. 2000) of the zones, scaled to 85% of the site.
    const double scale = 0.85 * side * side / total;
    double x = 0, y = 0, w = side, h = side;
    size_t i = 0;
    while (i < items.size()) {
        const bool horizontal = w >= h; // lay the row along the shorter side
        const double length = horizontal ? h : w;
        std::vector<size_t> row;
        double rowArea = 0;
        auto worst = [&](const std::vector<size_t>& r, double sum) {
            double maxA = 0, minA = 1e18;
            for (size_t k : r) {
                maxA = std::max(maxA, items[k].area * scale);
                minA = std::min(minA, items[k].area * scale);
            }
            const double s2 = sum * sum, l2 = length * length;
            return std::max(l2 * maxA / s2, s2 / (l2 * minA));
        };
        while (i < items.size()) {
            std::vector<size_t> next = row;
            next.push_back(i);
            const double nextArea = rowArea + items[i].area * scale;
            if (!row.empty() && worst(next, nextArea) > worst(row, rowArea)) break;
            row = next;
            rowArea = nextArea;
            ++i;
        }
        const double thickness = rowArea / length;
        double offset = 0;
        for (size_t k : row) {
            const double along = items[k].area * scale / thickness;
            const double rx = horizontal ? x : x + offset, ry = horizontal ? y + offset : y;
            const double rw = horizontal ? thickness : along, rh = horizontal ? along : thickness;
            rects.push_back({{"id", items[k].id}, {"module", items[k].module}, {"name", items[k].name},
                             // Edges rounded, not sizes, so rectangles tile the site exactly.
                             {"x", std::round(rx)}, {"y", std::round(ry)}, {"w", std::round(rx + rw) - std::round(rx)},
                             {"h", std::round(ry + rh) - std::round(ry)}});
            offset += along;
        }
        if (horizontal) {
            x += thickness;
            w -= thickness;
        } else {
            y += thickness;
            h -= thickness;
        }
        if (w <= 0 || h <= 0) break;
    }
    return {{"side", side}, {"rects", rects}};
}

} // namespace aige

namespace aige {

json RevisionInput(const Ontology& ontology, const json& spec, const json& solved) {
    json zones = json::array();
    if (spec.contains("root")) {
        for (const auto& c : spec["root"].value("children", json::array())) {
            json zone = {{"id", c.value("id", "")}, {"module", c.value("module", "")}, {"quantity", c.value("quantity", json::object())},
                         {"quality", c.value("quality", json::object())}};
            if (c.contains("rel")) zone["rel"] = c["rel"];
            json parcels = json::array();
            for (const auto& p : c.value("children", json::array())) {
                json one = {{"id", p.value("id", "")}, {"module", p.value("module", "")}};
                for (const char* key : {"repeat", "share", "quantity", "quality"}) {
                    if (p.contains(key) && !(p[key].is_object() && p[key].empty())) one[key] = p[key];
                }
                parcels.push_back(one);
            }
            if (!parcels.empty()) zone["parcels"] = parcels;
            zones.push_back(zone);
        }
    }
    json result = json::object();
    if (solved.is_object()) {
        std::map<std::string, int> buildings;
        for (const auto& o : solved.value("objects", json::array())) {
            if (o.value("layer", "") != "building") continue;
            const std::string node = o.value("node", "");
            buildings[node.substr(0, node.find('/'))]++;
        }
        json problems = json::array();
        for (const auto& p : solved.value("problems", json::array())) {
            problems.push_back(p.value("node", "") + ": " + p.value("message", ""));
        }
        result = {{"buildings", buildings}, {"problems", problems}};
    }
    return {{"plan", {{"zones", zones}, {"buildings", CountBuildings(ontology, spec)}}}, {"result", result}};
}

json RevisionContext(const Ontology& ontology, const json& brief, const json& spec) {
    std::set<std::string> modules;
    if (spec.contains("root")) {
        for (const auto& c : spec["root"].value("children", json::array())) modules.insert(c.value("module", ""));
    }
    if (const BuildType* build = ontology.Build(brief.value("buildType", ""))) {
        if (const ModuleDef* root = ontology.Module(build->root)) {
            for (const ChildDef& rule : root->children) {
                for (const auto& id : RuleModules(ontology, rule)) modules.insert(id);
            }
        }
    }
    json zones = json::object();
    for (const auto& id : modules) {
        const ModuleDef* m = ontology.Module(id);
        if (m == nullptr || m->level != PlaceLevel::Zone) continue;
        json holds = json::array();
        for (const ChildDef& rule : m->children) {
            for (const auto& c : RuleModules(ontology, rule)) {
                const ModuleDef* cm = ontology.Module(c);
                if (cm && cm->level != PlaceLevel::Dressing && !Disabled(brief, c)) holds.push_back(c);
            }
        }
        zones[id] = holds;
    }
    return {{"root", PlanRootContext(ontology, brief)}, {"what each zone can hold", zones}};
}

std::vector<PlaceProblem> ApplyRevision(const Ontology& ontology, const json& brief, json& spec, const json& changes) {
    std::vector<PlaceProblem> notes;
    auto note = [&](const std::string& where, const std::string& message) { notes.push_back({where, message, true}); };
    if (!spec.contains("root") || !changes.is_array()) return notes;
    const BuildType* build = ontology.Build(brief.value("buildType", ""));
    // A relation may name a part by its id or, at the top, by its module ("the river").
    auto relations = [&](const json& rel) {
        json kept = json::array();
        if (!rel.is_array()) return kept;
        for (auto r : rel) {
            if (!r.is_object() || !ontology.Relation(r.value("type", ""))) continue;
            bool ok = true;
            for (const char* key : {"to", "of"}) {
                if (!r.contains(key) || !r[key].is_string()) continue;
                const std::string to = r[key];
                if (SpecNode(spec, to)) continue;
                ok = false;
                for (const auto& c : spec["root"]["children"]) {
                    if (c.value("module", "") == to) {
                        r[key] = c.value("id", "");
                        ok = true;
                        break;
                    }
                }
            }
            if (ok) kept.push_back(r);
        }
        return kept;
    };
    for (const auto& change : changes) {
        if (!change.is_object()) continue;
        const std::string op = change.value("op", "");
        const std::string id = change.contains("node") && change["node"].is_string() ? change["node"].get<std::string>() : "";
        if (op == "move") {
            // Where it goes, nothing else: its relations replaced.
            json* node = SpecNode(spec, id);
            if (node == nullptr) {
                note(id, "not moved: the plan has no such part");
                continue;
            }
            (*node)["rel"] = relations(change.value("rel", json::array()));
            if ((*node)["rel"].empty()) note(id, "not moved: the relations name nothing in the plan");
        } else if (op == "set") {
            json* node = SpecNode(spec, id);
            if (node == nullptr) {
                note(id, "not changed: the plan has no such part");
                continue;
            }
            EnsureGroups(*node);
            for (const char* key : {"quantity", "quality"}) {
                const json fields = change.value(key, json::object());
                if (!fields.is_object()) continue;
                for (const auto& [name, value] : fields.items()) {
                    if (value.is_null()) (*node)[key].erase(name);
                    else (*node)[key][name] = value;
                }
            }
            if (change.contains("repeat")) {
                const double repeat = Mid(change["repeat"], 1);
                if (repeat >= 2) (*node)["repeat"] = static_cast<int>(std::round(repeat));
                else node->erase("repeat");
            }
            if (change.contains("rel")) (*node)["rel"] = relations(change["rel"]);
        } else if (op == "zone") {
            json* node = SpecNode(spec, id);
            const ModuleDef* m = node ? ontology.Module(node->value("module", "")) : nullptr;
            if (m == nullptr || m->level != PlaceLevel::Zone) {
                note(id, "not re-planned: it isn't a zone of the plan");
                continue;
            }
            ApplyZoneAnswer(ontology, brief, spec, id,
                            {{"quantity", change.value("quantity", json::object())}, {"quality", change.value("quality", json::object())},
                             {"children", change.value("children", json::array())}});
            if (change.contains("rel")) {
                if (json* zone = SpecNode(spec, id)) (*zone)["rel"] = relations(change["rel"]);
            }
        } else if (op == "add") {
            const json given = change.value("node", json::object());
            const std::string module = given.is_object() ? given.value("module", "") : "";
            const ModuleDef* m = ontology.Module(module);
            if (m == nullptr || build == nullptr || !ontology.ChildRule(build->root, module)) {
                note(module.empty() ? "?" : module, "not added: a " + (build ? build->root : std::string("place")) + " can't hold it");
                continue;
            }
            if (Disabled(brief, module)) {
                note(module, "not added: it's turned off in the Brief");
                continue;
            }
            std::set<std::string> used;
            CollectIds(spec["root"], used);
            const std::string wanted = Slug(given.value("id", module));
            json node = {{"id", UniqueId(wanted.empty() ? module : wanted, used)}, {"module", module},
                         {"quantity", given.value("quantity", json::object())}, {"quality", given.value("quality", json::object())},
                         {"rationale", given.value("rationale", "added at the user's request")}, {"children", json::array()}};
            EnsureGroups(node);
            ApplyBriefSettings(ontology, brief, node);
            KeepKnownFields(*m, node);
            if (given.contains("rel")) node["rel"] = relations(given["rel"]);
            const std::string newId = node["id"];
            spec["root"]["children"].push_back(node);
            if (m->level == PlaceLevel::Zone && given.contains("children")) {
                ApplyZoneAnswer(ontology, brief, spec, newId, {{"children", given["children"]}});
            }
        } else if (op == "remove") {
            bool removed = false;
            std::function<void(json&)> drop = [&](json& n) {
                if (!n.contains("children") || !n["children"].is_array()) return;
                json& kids = n["children"];
                for (size_t i = 0; i < kids.size() && !removed; ++i) {
                    if (kids[i].value("id", "") == id) {
                        kids.erase(i);
                        removed = true;
                        return;
                    }
                    drop(kids[i]);
                }
            };
            if (id != spec["root"].value("id", "")) drop(spec["root"]);
            if (!removed) note(id, "not removed: the plan has no such part");
        }
    }
    return notes;
}

} // namespace aige

namespace aige {

namespace {

// How many of `module` the Spec stands for (repeat x share down the tree).
double CountModule(const json& spec, const std::string& module) {
    double total = 0;
    std::function<void(const json&, double)> walk = [&](const json& node, double times) {
        const double here = times * Mid(node.value("repeat", json()), 1) * (node.contains("share") ? Mid(node["share"], 1) : 1);
        if (node.value("module", "") == module) total += here;
        for (const auto& c : node.value("children", json::array())) walk(c, here);
    };
    if (spec.contains("root")) walk(spec["root"], 1);
    return total;
}

std::string Status(double planned, int built) {
    if (planned >= 0.5 && built == 0) return "missing";
    if (built < 0.8 * planned - 0.01) return "short";
    return "ok";
}

} // namespace

json VerifyLayout(const Ontology& ontology, const json& brief, const json& spec, const json& solved) {
    json rows = json::array();
    if (!spec.contains("root") || !solved.is_object()) return rows;
    const json objects = solved.value("objects", json::array());
    const json regions = solved.value("regions", json::array());
    auto builtUnder = [&](const std::string& prefix) {
        int n = 0;
        for (const auto& o : objects) {
            const std::string node = o.value("node", "");
            if (o.value("layer", "") == "building" && o.value("primary", false) && (node == prefix || node.rfind(prefix + "/", 0) == 0)) ++n;
        }
        return n;
    };
    auto builtOf = [&](const std::string& module) {
        int n = 0;
        for (const auto& r : regions) n += r.value("module", "") == module;
        if (n > 0) return n;
        for (const auto& o : objects) n += o.value("shows", "") == module;
        return n;
    };
    // The whole place: the Brief's buildings.
    {
        const auto [lo, hi] = Bounds(BriefValue(brief, "scale.buildings"));
        const int planned = CountBuildings(ontology, spec);
        const int built = builtUnder(spec["root"].value("id", ""));
        int total = 0;
        for (const auto& o : objects) total += o.value("layer", "") == "building" && o.value("primary", false);
        json row = {{"id", "buildings"}, {"what", "Buildings"}, {"planned", planned}, {"built", total}};
        if (lo >= 0) row["asked"] = lo == hi ? std::to_string(static_cast<int>(lo))
                                             : std::to_string(static_cast<int>(lo)) + "–" + std::to_string(static_cast<int>(hi));
        row["status"] = total < lo * 0.9 ? "short" : Status(planned, total);
        (void)built;
        rows.push_back(row);
    }
    // The site: within the Brief's area, unless its zones needed more.
    {
        const auto [lo, hi] = Bounds(BriefValue(brief, "scale.area"));
        const json b = solved.contains("grid") ? solved["grid"].value("bounds", json()) : json();
        if (lo >= 0 && b.is_array() && b.size() == 4) {
            const int area = b[2].get<int>() * b[3].get<int>();
            json row = {{"id", "area"}, {"what", "Site area (cells)"}, {"built", area},
                        {"asked", std::to_string(static_cast<int>(lo)) + "–" + std::to_string(static_cast<int>(hi))}};
            row["status"] = area <= hi * 1.1 ? "ok" : "short";
            if (area > hi * 1.1) row["note"] = "larger than the Brief asks: its zones needed the room";
            rows.push_back(row);
        }
    }
    // Each zone, and its parcels: a ward without its temple shows here.
    for (const auto& zone : spec["root"].value("children", json::array())) {
        const ModuleDef* zm = ontology.Module(zone.value("module", ""));
        if (zm == nullptr || zm->level != PlaceLevel::Zone) continue;
        const std::string zoneId = zone.value("id", "");
        json one = {{"spec", "x"}, {"root", zone}};
        const int planned = CountBuildings(ontology, one);
        const int built = builtUnder(zoneId);
        json parts = json::array();
        for (const auto& p : zone.value("children", json::array())) {
            const ModuleDef* pm = ontology.Module(p.value("module", ""));
            if (pm == nullptr || !ontology.IsA(pm->id, "BuildingParcel")) continue;
            const double want = Mid(p.value("repeat", json()), 1);
            const int got = builtUnder(p.value("id", ""));
            parts.push_back({{"id", p.value("id", "")}, {"what", pm->name}, {"planned", static_cast<int>(std::lround(want))},
                             {"built", got}, {"status", Status(want, got)}});
        }
        json row = {{"id", zoneId}, {"what", zm->name}, {"zone", true}, {"planned", planned}, {"built", built}, {"parts", parts}};
        std::string status = planned == 0 && zm->Field("plots") ? "missing" : Status(planned, built);
        for (const auto& p : parts) {
            if (status == "ok" && p["status"] != "ok") status = "short";
        }
        if (planned == 0 && zm->Field("plots")) row["note"] = "no buildings planned";
        row["status"] = status;
        rows.push_back(row);
    }
    // What else the Brief asked for: each enabled component that isn't a zone.
    const json components = brief.value("components", json::object());
    for (const auto& [id, c] : components.items()) {
        const ModuleDef* m = ontology.Module(id);
        if (m == nullptr || !c.is_object() || !c.value("enabled", true)) continue;
        if (m->level == PlaceLevel::Zone || m->level == PlaceLevel::Network || m->level == PlaceLevel::Dressing ||
            ontology.IsA(id, "BuildingParcel") || ontology.IsA(id, "LinearFeature")) {
            continue;
        }
        const double planned = CountModule(spec, id);
        const int built = builtOf(id);
        json row = {{"id", "components." + id}, {"what", m->name}, {"asked", c.contains("density") ? c["density"] : json("yes")},
                    {"planned", static_cast<int>(std::lround(planned))}, {"built", built}};
        row["status"] = planned < 0.5 ? "missing" : Status(planned, built);
        if (planned < 0.5) row["note"] = "not in the plan";
        rows.push_back(row);
    }
    // What the solver couldn't place, by name.
    for (const auto& p : solved.value("problems", json::array())) {
        if (p.value("code", "") != "S-UNSAT") continue;
        rows.push_back({{"id", "problem." + p.value("statement", "")}, {"what", p.value("node", "")}, {"note", p.value("message", "")},
                        {"status", "missing"}});
    }
    return rows;
}

} // namespace aige
