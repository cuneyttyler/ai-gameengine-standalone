#include "editing/BuildWindow.h"

#include "places/Expr.h"
#include "places/PlaceValidate.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace aige {

using json = nlohmann::json;

namespace {

// A component's fields for conditions: what the Brief sets, else the defaults.
class ParamScope : public places::Scope {
public:
    ParamScope(const ModuleDef& module, const json& params) : module_(module), params_(params) {}
    std::optional<places::Value> Name(const std::string& name) override {
        const FieldDef* f = module_.Field(name);
        if (f == nullptr) return std::nullopt;
        return FieldValueOf(*f, params_.is_object() && params_.contains(name) ? params_[name] : json());
    }
    places::Value NodeField(int, const std::string&) override { return places::Value::Unknown(); }
    places::Value Below(int, bool, const std::string&, const std::string&) override { return places::Value::Unknown(); }
    int SelfNode() override { return 0; }

private:
    const ModuleDef& module_;
    const json& params_;
};

bool Holds(const std::string& condition, places::Scope& scope) {
    if (condition.empty()) return true;
    auto expr = places::Expr::Parse(condition);
    return !expr || expr->Holds(scope) != places::Tri::False;
}

// The concepts a field's value names: a mix's keys, a set's items, one value.
std::vector<std::string> ConceptsIn(const json& value) {
    std::vector<std::string> out;
    const json& v = value.is_object() && value.contains("mix") ? value["mix"] : value;
    if (v.is_string()) out.push_back(v.get<std::string>());
    if (v.is_array()) {
        for (const auto& item : v) {
            if (item.is_string()) out.push_back(item.get<std::string>());
        }
    }
    if (v.is_object()) {
        for (const auto& [key, weight] : v.items()) out.push_back(key);
    }
    return out;
}

std::string AmountOf(PlaceLevel level) {
    switch (level) {
        case PlaceLevel::Zone:
        case PlaceLevel::Parcel: return "count";
        case PlaceLevel::Feature:
        case PlaceLevel::Dressing: return "density";
        default: return "none";
    }
}

json FieldControl(const Ontology& ontology, const std::string& name, const FieldDef& f, const json& value) {
    json c = {{"name", name}, {"type", f.type}, {"value", value.is_null() ? f.defaultValue : value}};
    if (!f.description.empty()) c["description"] = f.description;
    if (f.min) c["min"] = *f.min;
    if (f.max) c["max"] = *f.max;
    if (!f.unit.empty()) c["unit"] = f.unit;
    json options = json::array();
    for (const auto& v : f.values) options.push_back(v);
    if (!f.vocabulary.empty()) {
        if (const auto* scheme = ontology.Scheme(f.vocabulary)) {
            for (const auto& concept_ : *scheme) options.push_back(concept_.id);
        }
    }
    if (f.type == "density") options = {"none", "rare", "some", "common", "abundant"};
    if (!options.empty()) c["options"] = options;
    if (!f.when.empty()) c["when"] = f.when;
    return c;
}

} // namespace

std::vector<AssetNeed> AssetNeeds(const Ontology& ontology, const json& brief) {
    std::vector<AssetNeed> needs;
    std::set<std::string> seen;
    const auto briefComponents = brief.value("components", json::object());
    for (const auto& [component, settings] : briefComponents.items()) {
        if (!settings.is_object() || !settings.value("enabled", true)) continue;
        // The component and what it typically holds, two levels down.
        std::vector<std::pair<const ModuleDef*, int>> todo;
        if (const ModuleDef* m = ontology.Module(component)) todo.push_back({m, 0});
        std::set<std::string> visited;
        while (!todo.empty()) {
            auto [module, depth] = todo.front();
            todo.erase(todo.begin());
            if (!visited.insert(module->id).second) continue;
            // A module the Brief also sets as a component (gardens in the house
            // plots) has the Brief's settings wherever it appears.
            const json own = BriefValue(brief, "components." + module->id);
            if (own.is_object() && !own.value("enabled", true)) continue;
            const json params = own.is_object() ? own.value("params", json::object()) : json::object();
            ParamScope scope(*module, params);
            float minCells = 0.5f, maxCells = 1e9f;
            if (const FieldDef* size = module->Field("size"); size && size->defaultValue.is_object()) {
                const json w = size->defaultValue.value("w", json());
                if (w.is_array() && w.size() == 2) maxCells = w[1].get<float>();
                else if (w.is_number()) maxCells = w.get<float>();
            }
            for (const SlotDef& slot : module->slots) {
                if (!slot.essential && !slot.primary) continue;
                if (!Holds(slot.when, scope)) continue;
                AssetNeed need;
                need.component = component;
                need.module = module->id;
                need.slot = slot.name;
                need.essential = slot.essential;
                std::set<std::string> roles;
                for (const auto& bind : slot.binds) {
                    if (!Holds(bind.when, scope)) continue;
                    roles.insert(bind.role);
                    if (!bind.concepts.empty()) {
                        for (size_t start = 0; start <= bind.concepts.size();) {
                            const size_t plus = bind.concepts.find('+', start);
                            std::string field = bind.concepts.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
                            field.erase(0, field.find_first_not_of(' '));
                            field.erase(field.find_last_not_of(' ') + 1);
                            if (const FieldDef* f = module->Field(field)) {
                                const json value = params.contains(field) ? params[field] : f->defaultValue;
                                for (const auto& c : ConceptsIn(value)) {
                                    if (std::find(need.concepts.begin(), need.concepts.end(), c) == need.concepts.end()) need.concepts.push_back(c);
                                }
                            }
                            if (plus == std::string::npos) break;
                            start = plus + 1;
                        }
                    }
                }
                std::string role;
                for (const auto& r : roles) role += (role.empty() ? "" : "|") + r;
                if (role.empty()) continue;
                need.role = role;
                need.joined = role.find("-piece-set") != std::string::npos;
                if (slot.primary) {
                    need.minCells = minCells;
                    need.maxCells = maxCells;
                }
                std::string key = role;
                for (const auto& c : need.concepts) key += "/" + c;
                if (!seen.insert(key).second) continue;
                needs.push_back(std::move(need));
            }
            if (depth >= 2) continue;
            for (const ChildDef& child : module->children) {
                if (child.weight < 0.5 || child.module.rfind("class:", 0) == 0) continue;
                if (const ModuleDef* c = ontology.Module(child.module); c && Holds(child.when, scope)) todo.push_back({c, depth + 1});
            }
        }
    }
    return needs;
}

json AssetPlan(const Ontology& ontology, const Catalog& catalog, const json& brief, bool isoGrid) {
    json rows = json::array();
    int fromProject = 0, toGenerate = 0;
    for (const AssetNeed& need : AssetNeeds(ontology, brief)) {
        SlotNeed slot;
        slot.role = need.role;
        slot.concepts = need.concepts;
        slot.minCells = need.minCells;
        slot.maxCells = need.maxCells;
        slot.joined = need.joined;
        const Retrieval found = RetrieveForSlot(catalog, ontology, slot, isoGrid);
        json candidates = json::array();
        for (size_t i = 0; i < found.candidates.size() && i < 4; ++i) {
            const Candidate& c = found.candidates[i];
            candidates.push_back({{"id", c.entry->id}, {"path", c.entry->path},
                                  {"rect", {c.entry->rect.x, c.entry->rect.y, c.entry->rect.w, c.entry->rect.h}},
                                  {"name", c.entry->name}, {"fit", std::round(c.fit * 100) / 100}, {"grade", c.grade}});
        }
        const std::string chosen = BriefValue(brief, "components." + need.component + ".assets.source").is_string()
                                       ? BriefValue(brief, "components." + need.component + ".assets.source").get<std::string>()
                                       : "auto";
        std::string source = chosen, reason;
        if (chosen == "auto") {
            if (!found.candidates.empty()) {
                source = "project";
                reason = found.candidates.front().grade == "good" ? "fits well" : "fits, may need adapting";
            } else {
                source = "generate";
                // Why nothing fits: the reason of the closest miss (one that filled the role).
                for (const auto& r : found.rejected) {
                    if (r.reason.rfind("doesn't fill", 0) != 0) {
                        reason = "nothing in the project fits (" + r.reason + ")";
                        break;
                    }
                }
                if (reason.empty()) reason = "nothing in the project fills this role";
            }
        } else if (chosen == "project" && found.candidates.empty()) {
            reason = "nothing in the project fits: the slot will show a placeholder";
        }
        (source == "project" ? fromProject : toGenerate) += 1;
        std::string concepts;
        for (const auto& c : need.concepts) concepts += (concepts.empty() ? "" : ", ") + c;
        rows.push_back({{"component", need.component}, {"module", need.module}, {"slot", need.slot}, {"role", need.role},
                        {"concepts", need.concepts}, {"essential", need.essential}, {"joined", need.joined},
                        {"chosen", chosen}, {"source", source}, {"reason", reason}, {"candidates", candidates},
                        {"label", need.role + (concepts.empty() ? "" : " (" + concepts + ")")}});
    }
    return {{"rows", rows}, {"summary", {{"project", fromProject}, {"generate", toGenerate}}}};
}

json BriefWindow(const Ontology& ontology, const json& brief, const BriefMerge* merge, const Catalog* catalog, bool isoGrid, int round) {
    const BuildType* build = ontology.Build(brief.value("buildType", ""));
    json window = {{"round", round}, {"buildType", brief.value("buildType", "")}, {"name", brief.value("name", "")}};
    if (build == nullptr) {
        window["problems"] = json::array({{{"where", "buildType"}, {"message", "unknown build type"}}});
        return window;
    }
    window["buildTypeName"] = build->name;

    // Size, with numbers.
    json classes = json::array();
    for (const auto& s : build->scale) {
        classes.push_back({{"id", s.id}, {"buildings", {s.buildings.first, s.buildings.second}}, {"side", {s.side.first, s.side.second}}});
    }
    window["size"] = {{"class", BriefValue(brief, "scale.class")},
                      {"buildings", BriefValue(brief, "scale.buildings.count")},
                      {"area", BriefValue(brief, "scale.area.area")},
                      {"unit", build->unit.label},
                      {"classes", classes}};
    // Layout, walls, features, style: the build type's Brief fields.
    const json& fields = build->briefFields;
    // One pattern or a mix: always shown as shares.
    json shares = json::object();
    const json layout = BriefValue(brief, "shape.layout");
    if (layout.is_string()) shares[layout.get<std::string>()] = 1.0;
    else if (layout.is_object()) shares = layout.contains("mix") ? layout["mix"] : layout;
    window["layout"] = {{"value", shares}, {"options", fields.value("layout", json::array())},
                        {"description", BriefValue(brief, "shape.description")}};
    window["walled"] = {{"value", BriefValue(brief, "walled")}, {"options", fields.value("walled", json::array())}};
    json features = json::array();
    for (const auto& f : brief.value("features", json::array())) {
        if (f.is_object()) features.push_back(f.value("module", ""));
    }
    window["features"] = {{"value", features}, {"options", fields.value("features", json::array())}};
    json style = json::object();
    const auto styleFields = fields.value("style", json::object());
    for (const auto& [key, spec] : styleFields.items()) {
        json options = spec;
        if (spec.is_string()) {
            options = json::array();
            if (const auto* scheme = ontology.Scheme(spec.get<std::string>())) {
                for (const auto& c : *scheme) options.push_back(c.id);
            }
        }
        style[key] = {{"value", BriefValue(brief, "style." + key)}, {"options", options}};
    }
    style["tags"] = {{"value", BriefValue(brief, "style.tags")}};
    window["style"] = style;

    // Components: every one the build type allows, those in the Brief first.
    json components = json::array();
    std::vector<std::string> order;
    const auto given = brief.value("components", json::object());
    for (const auto& [id, c] : given.items()) order.push_back(id);
    for (const auto& id : build->components) {
        if (std::find(order.begin(), order.end(), id) == order.end()) order.push_back(id);
    }
    const auto needs = catalog ? AssetNeeds(ontology, brief) : std::vector<AssetNeed>{};
    for (const auto& id : order) {
        const ModuleDef* m = ontology.Module(id);
        if (m == nullptr) continue;
        const json settings = BriefValue(brief, "components." + id);
        const bool inBrief = settings.is_object();
        const bool enabled = inBrief && settings.value("enabled", true);
        json row = {{"id", id}, {"name", m->name}, {"description", m->description}, {"level", PlaceLevelName(m->level)},
                    {"inBrief", inBrief}, {"enabled", enabled}, {"amount", AmountOf(m->level)},
                    {"count", inBrief ? settings.value("count", json()) : json()},
                    {"density", inBrief ? settings.value("density", json()) : json()},
                    {"source", inBrief ? BriefValue(settings, "assets.source") : json("auto")}};
        // Thumbnails: the best project art for its first need.
        json thumbs = json::array();
        if (catalog && enabled) {
            for (const auto& need : needs) {
                if (need.component != id) continue;
                SlotNeed slot;
                slot.role = need.role;
                slot.concepts = need.concepts;
                slot.minCells = need.minCells;
                slot.maxCells = need.maxCells;
                slot.joined = need.joined;
                const Retrieval found = RetrieveForSlot(*catalog, ontology, slot, isoGrid);
                for (size_t i = 0; i < found.candidates.size() && thumbs.size() < 3; ++i) {
                    const CatalogEntry* e = found.candidates[i].entry;
                    thumbs.push_back({{"id", e->id}, {"path", e->path}, {"rect", {e->rect.x, e->rect.y, e->rect.w, e->rect.h}}});
                }
                if (!thumbs.empty()) break;
            }
        }
        row["thumbs"] = thumbs;
        if (round >= 2 && enabled) {
            json params = json::array();
            const json given = settings.value("params", json::object());
            ParamScope scope(*m, given);
            for (const auto* group : {&m->quantity, &m->quality}) {
                for (const auto& [name, f] : *group) {
                    if (!f.ask || !Holds(f.when, scope)) continue;
                    params.push_back(FieldControl(ontology, name, f, given.value(name, json())));
                }
            }
            row["params"] = params;
        }
        components.push_back(row);
    }
    window["components"] = components;

    // What the drafts disagreed on or guessed.
    json marks = json::object();
    if (merge) {
        for (const auto& f : merge->fields) {
            if (!f.disputed && !f.low()) continue;
            marks[f.path] = {{"disputed", f.disputed}, {"alternatives", f.alternatives}, {"readings", f.readings},
                             {"confidence", f.confidence}};
        }
        window["assumptions"] = merge->assumptions;
        window["questions"] = merge->questions;
    }
    window["marks"] = marks;
    if (round >= 2 && catalog) window["assetPlan"] = AssetPlan(ontology, *catalog, brief, isoGrid);

    json problems = json::array();
    for (const auto& p : ValidateBrief(ontology, brief)) problems.push_back({{"where", p.where}, {"message", p.message}, {"warning", p.warning}});
    window["problems"] = problems;
    return window;
}

void ApplyWindowEdit(const Ontology& ontology, json& brief, const std::string& path, const json& value) {
    if (path == "shape.layout" && value.is_object()) {
        // Shares from the window: one pattern is stored as just its name.
        const json& shares = value.contains("mix") ? value["mix"] : value;
        json kept = json::object();
        for (const auto& [pattern, share] : shares.items()) {
            if (share.is_number() && share.get<double>() > 0) kept[pattern] = share;
        }
        if (kept.size() == 1) SetBriefValue(brief, path, kept.begin().key());
        else if (!kept.empty()) SetBriefValue(brief, path, json{{"mix", kept}});
        return;
    }
    if (path == "features") {
        json list = json::array();
        if (value.is_array()) {
            for (const auto& f : value) {
                if (f.is_string()) list.push_back({{"module", f}});
            }
        }
        brief["features"] = list;
        return;
    }
    SetBriefValue(brief, path, value);
    if (path.rfind("scale.", 0) == 0) FollowScale(ontology, brief, path == "scale.class" ? path : "scale.buildings");
    // components.<id>.enabled
    const std::string prefix = "components.";
    if (path.rfind(prefix, 0) == 0 && path.size() > 8 && path.substr(path.size() - 8) == ".enabled") {
        const std::string id = path.substr(prefix.size(), path.size() - prefix.size() - 8);
        if (value.is_boolean() && !value.get<bool>()) {
            brief["components"][id] = {{"enabled", false}};
        } else {
            json& c = brief["components"][id];
            if (!c.contains("assets")) c["assets"] = {{"source", "auto"}};
        }
    }
}

} // namespace aige
