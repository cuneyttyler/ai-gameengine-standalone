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
    // Relations name the final ids, and only ones that exist.
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
            json child = {{"id", ""}, {"module", cm->id}, {"quantity", json::object()}, {"quality", json::object()}};
            if (rule.min >= 1) {
                if (rule.min >= 2) child["repeat"] = rule.min; // the minimum, at least
            } else if (!rule.count.empty()) {
                continue; // tied to a field: the compiler makes as many as it says
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
