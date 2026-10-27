#include "places/ReviseLanguage.h"

#include "places/Ontology.h"
#include "places/Planner.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace aige {

using json = nlohmann::json;
namespace fs = std::filesystem;

const json& ReviseLanguage() {
    static const json language = [] {
        std::ifstream in(EnginePlacesDir() / "revise-language.json");
        json j = in ? json::parse(in, nullptr, false) : json();
        return j.is_object() ? j : json{{"params", json::object()}, {"instructions", json::array()}};
    }();
    return language;
}

std::string ReviseStep(const std::string& stage) { return stage == "layout" ? "layout" : "plan"; }

json ReviseInstructionsFor(const std::string& step) {
    json out = json::array();
    for (const auto& i : ReviseLanguage().value("instructions", json::array())) {
        const json steps = i.value("steps", json::array());
        if (std::find(steps.begin(), steps.end(), step) == steps.end()) continue;
        json shown = i;
        shown.erase("steps");
        out.push_back(shown);
    }
    return out;
}

namespace {

const json* Instruction(const std::string& name) {
    for (const auto& i : ReviseLanguage()["instructions"]) {
        if (i.value("do", "") == name) return &i;
    }
    return nullptr;
}

std::string Text(const json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number()) {
        std::string s = std::to_string(std::round(v.get<double>() * 100) / 100);
        s.erase(s.find_last_not_of('0') + 1);
        if (!s.empty() && s.back() == '.') s.pop_back();
        return s;
    }
    if (v.is_array()) {
        std::string s;
        for (const auto& r : v) {
            if (!s.empty()) s += ", ";
            s += r.is_object() ? r.value("type", "") + " " + r.value("to", "") : Text(r);
        }
        return s;
    }
    if (v.is_object()) {
        std::string s;
        for (const auto& [k, x] : v.items()) s += (s.empty() ? "" : ", ") + k + " " + Text(x);
        return s;
    }
    return v.is_null() ? "" : v.dump();
}

// The zones of the plan (its root's children at zone level).
bool IsZone(const Ontology& ontology, const json& spec, const std::string& id) {
    if (!spec.contains("root")) return false;
    for (const auto& c : spec["root"].value("children", json::array())) {
        if (c.value("id", "") != id) continue;
        const ModuleDef* m = ontology.Module(c.value("module", ""));
        return m != nullptr && m->level == PlaceLevel::Zone;
    }
    return false;
}

// Whether a zone's module may hold parcels of `kind`.
bool Holds(const Ontology& ontology, const json& zone, const std::string& kind) {
    const ModuleDef* m = ontology.Module(zone.value("module", ""));
    if (m == nullptr || ontology.Module(kind) == nullptr) return false;
    for (const ChildDef& c : m->children) {
        if (c.module == kind) return true;
        if (c.module.rfind("class:", 0) == 0 && ontology.IsA(kind, c.module.substr(6))) return true;
    }
    return false;
}

int Repeat(const json& child) { return child.value("repeat", json(1)).is_number() ? child.value("repeat", 1) : 1; }

bool LooksLike(const json& child, const json& look) {
    if (!look.is_object()) return true;
    const json quality = child.value("quality", json::object());
    for (const auto& [k, v] : look.items()) {
        if (!quality.contains(k) || quality[k] != v) return false;
    }
    return true;
}

std::string FreeId(const json& children, const std::string& base) {
    auto taken = [&](const std::string& id) {
        return std::any_of(children.begin(), children.end(), [&](const json& c) { return c.value("id", "") == id; });
    };
    if (!taken(base)) return base;
    for (int n = 2;; ++n) {
        if (!taken(base + "-" + std::to_string(n))) return base + "-" + std::to_string(n);
    }
}

double Number(const json& v, double otherwise) { return v.is_number() ? v.get<double>() : otherwise; }

// A quantity of a node: {"plots": {"count": n}} or just {"plots": n}; null when it hasn't one.
json Quantity(const json& node, const char* key, const char* field) {
    const json q = node.is_object() ? node.value("quantity", json()) : json();
    const json v = q.is_object() ? q.value(key, json()) : json();
    return v.is_object() ? v.value(field, json()) : v;
}

// A setting of a node: its size and its count are quantities, the rest qualities.
json SetField(const std::string& node, const std::string& field, const json& value) {
    json op = {{"op", "set"}, {"node", node}};
    if (field == "area") op["quantity"] = {{"area", {{"area", value}}}};
    else if (field == "plots" || field == "count") op["quantity"] = {{"plots", {{"count", value}}}};
    else op["quality"] = {{field, value}};
    return op;
}

} // namespace

std::string ShowInstruction(const json& instruction) {
    const std::string name = instruction.value("do", "");
    const json* def = Instruction(name);
    std::string text = def ? def->value("shown", name) : name;
    json values = instruction;
    if (def) {
        const json defaults = def->value("defaults", json::object()); // a named copy: .items() of a temporary dangles
        for (const auto& [k, v] : defaults.items()) {
            if (!values.contains(k)) values[k] = v;
        }
    }
    // A size asked in cells reads as one.
    if ((name == "grow" || name == "shrink") && values.contains("to") && !instruction.contains("by")) {
        text = (name == "grow" ? "Grow {zone} to {to} cells" : "Shrink {zone} to {to} cells");
    }
    if (name == "remove" && !values.contains("kind")) text = "Remove {node}";
    if (name == "replace" && !values.contains("count")) text = "Replace every {kind} with {with} in {zone}";
    if (name == "place") {
        text = values.contains("into") ? "Move {node} into {into}" : values.contains("near") ? "Move {node} next to {near}" : "Move {node} to the {side} of its zone";
    }
    for (size_t at; (at = text.find('{')) != std::string::npos;) {
        const size_t end = text.find('}', at);
        if (end == std::string::npos) break;
        const std::string key = text.substr(at + 1, end - at - 1);
        text.replace(at, end - at + 1, values.contains(key) ? Text(values[key]) : "");
    }
    return text;
}

LoweredRevision LowerRevision(const Ontology& ontology, const json& spec, const json& brief, const std::map<std::string, int>& zoneCells,
                              const std::string& step, const json& program) {
    LoweredRevision out;
    json work = spec; // the plan as each instruction leaves it (the next one builds on it)
    std::set<std::string> zonesChanged; // zones whose parcels an instruction changed: one "zone" op each, at the end
    int buildingsAdded = 0;
    auto note = [&](const json& i, const std::string& why) { out.notes.push_back(ShowInstruction(i) + ": " + why); };
    auto zoneNode = [&](const std::string& id) -> json* { return IsZone(ontology, work, id) ? SpecNode(work, id) : nullptr; };
    auto isBuilding = [&](const std::string& kind) { return ontology.IsA(kind, "BuildingParcel"); };
    // `count` more of `kind` in a zone (more of a matching parcel group, or a new one).
    auto addParcels = [&](json& zone, const std::string& kind, int count, const json& look) {
        json& children = zone["children"];
        if (!children.is_array()) children = json::array();
        for (auto& c : children) {
            if (c.value("module", "") == kind && LooksLike(c, look)) {
                c["repeat"] = Repeat(c) + count;
                return;
            }
        }
        json c = {{"id", FreeId(children, zone.value("id", "") + "/" + kind)}, {"module", kind}, {"repeat", count}}; // a parcel's id is zone/parcel
        if (look.is_object() && !look.empty()) c["quality"] = look;
        children.push_back(c);
    };
    // Up to `count` of `kind` out of a zone (every one when count < 0); how many went.
    auto takeParcels = [&](json& zone, const std::string& kind, int count, const json& look) {
        json& children = zone["children"];
        int taken = 0;
        json kept = json::array();
        for (auto& c : children) {
            if (c.value("module", "") != kind || !LooksLike(c, look) || (count >= 0 && taken >= count)) {
                kept.push_back(c);
                continue;
            }
            const int n = Repeat(c);
            const int take = count < 0 ? n : std::min(n, count - taken);
            taken += take;
            if (take < n) {
                c["repeat"] = n - take;
                kept.push_back(c);
            }
        }
        children = kept;
        return taken;
    };

    for (const auto& i : program.is_array() ? program : json::array()) {
        if (!i.is_object()) continue;
        const std::string name = i.value("do", "");
        const json* def = Instruction(name);
        if (def == nullptr) {
            out.notes.push_back("\"" + name + "\" isn't something a revision can do");
            continue;
        }
        const json steps = def->value("steps", json::array());
        if (std::find(steps.begin(), steps.end(), step) == steps.end()) {
            note(i, step == "layout" ? "that's done on the plan, after the layout" : "that's done on the layout: go back to ② Layout");
            continue;
        }
        bool missing = false;
        for (const auto& r : def->value("required", json::array())) missing = missing || !i.contains(r.get<std::string>());
        if (missing) {
            note(i, "something it needs wasn't given");
            continue;
        }
        const std::string zone = i.value("zone", "");
        if (i.contains("zone") && !zoneNode(zone)) {
            note(i, "there's no zone \"" + zone + "\"");
            continue;
        }
        bool ran = true;
        if (name == "swap") {
            if (!zoneNode(i.value("a", "")) || !zoneNode(i.value("b", ""))) {
                note(i, "both must be zones of the place");
                continue;
            }
            out.changes.push_back({{"op", "swap"}, {"a", i["a"]}, {"b", i["b"]}});
        } else if (name == "occupy") {
            if (!zoneNode(i.value("where", ""))) {
                note(i, "there's no zone \"" + i.value("where", "") + "\"");
                continue;
            }
            out.changes.push_back({{"op", "occupy"}, {"zone", zone}, {"where", {{"zone", i["where"]}}}, {"then", i.value("then", "swap")}});
        } else if (name == "shift") {
            const int dx = static_cast<int>(std::lround(Number(i.value("dx", json()), 0))), dy = static_cast<int>(std::lround(Number(i.value("dy", json()), 0)));
            if (dx == 0 && dy == 0) {
                note(i, "it says no distance");
                continue;
            }
            out.changes.push_back({{"op", "shift"}, {"zone", zone}, {"dx", dx}, {"dy", dy}});
        } else if (name == "move") {
            const json rel = NormalizeRelations(i.value("rel", json::array()));
            json kept = json::array();
            for (const auto& r : rel) {
                if (r.value("to", "") != zone) kept.push_back(r); // never relative to itself
            }
            if (kept.empty()) {
                note(i, "it says nowhere to go");
                continue;
            }
            out.changes.push_back({{"op", "move"}, {"node", zone}, {"rel", kept}});
            for (const auto& r : kept) out.expect.push_back({{"node", zone}, {"type", r["type"]}, {"to", r["to"]}});
        } else if (name == "grow" || name == "shrink") {
            const auto it = zoneCells.find(zone);
            const double now = it != zoneCells.end() && it->second > 0 ? it->second : Number(Quantity(*zoneNode(zone), "area", "area"), 0);
            const double by = Number(i.value("by", json()), Number(def->value("defaults", json::object()).value("by", json()), 1.0));
            const double area = i.contains("to") ? Number(i["to"], 0) : now * by;
            if (now <= 0 || area <= 0 || (name == "grow" ? area <= now : area >= now)) {
                note(i, name == "grow" ? "that isn't bigger than it is now" : "that isn't smaller than it is now");
                continue;
            }
            json op = {{"op", "set"}, {"node", zone}, {"quantity", {{"area", {{"area", static_cast<int>(std::lround(area))}}}}}};
            // Smaller: fewer buildings in proportion (else they'd keep it its size).
            const json plots = Quantity(*zoneNode(zone), "plots", "count");
            if (name == "shrink" && plots.is_number() && plots.get<double>() > 0) {
                op["quantity"]["plots"] = {{"count", std::max(1, static_cast<int>(std::floor(plots.get<double>() * area / now)))}};
            }
            out.changes.push_back(op);
            out.inPlace.insert(zone);
        } else if (name == "add-zone") {
            const std::string module = i.value("module", "");
            const ModuleDef* m = ontology.Module(module);
            if (m == nullptr || m->level != PlaceLevel::Zone) {
                note(i, "\"" + module + "\" isn't a kind of zone");
                continue;
            }
            json node = {{"id", FreeId(work["root"].value("children", json::array()), i.value("id", module))}, {"module", module}};
            if (i.contains("rel")) node["rel"] = NormalizeRelations(i["rel"]);
            if (i.contains("to") && i["to"].is_number()) node["quantity"] = {{"area", {{"area", i["to"]}}}};
            out.changes.push_back({{"op", "add"}, {"node", node}});
            for (const auto& r : node.value("rel", json::array())) out.expect.push_back({{"node", node["id"]}, {"type", r["type"]}, {"to", r["to"]}});
            work["root"]["children"].push_back(node);
        } else if (name == "remove-zone") {
            out.changes.push_back({{"op", "remove"}, {"node", zone}});
        } else if (name == "keep") {
            out.keep.insert(zone);
        } else if (name == "set") {
            const std::string node = i.value("node", "");
            if (SpecNode(work, node) == nullptr) {
                note(i, "there's no \"" + node + "\" in the plan");
                continue;
            }
            out.changes.push_back(SetField(node, i.value("field", ""), i["value"]));
        } else if (name == "set-place") {
            out.brief[i.value("path", "")] = i["value"];
        } else if (name == "add") {
            const std::string kind = i.value("kind", "");
            const int count = static_cast<int>(std::lround(Number(i.value("count", json()), 0)));
            json* z = zoneNode(zone);
            if (count < 1) {
                note(i, "it says no number to add");
                continue;
            }
            if (!Holds(ontology, *z, kind)) {
                note(i, "a " + z->value("module", "") + " can't hold \"" + kind + "\"");
                continue;
            }
            addParcels(*z, kind, count, i.value("look", json::object()));
            zonesChanged.insert(zone);
            if (isBuilding(kind)) buildingsAdded += count;
        } else if (name == "remove") {
            if (i.contains("node") && !i.contains("kind")) {
                const std::string node = i.value("node", "");
                if (SpecNode(work, node) == nullptr || node.find('/') == std::string::npos) {
                    note(i, "there's no parcel \"" + node + "\" in the plan");
                    continue;
                }
                out.changes.push_back({{"op", "remove"}, {"node", node}});
            } else {
                json* z = zoneNode(zone);
                if (z == nullptr || !i.contains("kind")) {
                    note(i, "say the zone and the kind");
                    continue;
                }
                const int count = i.contains("count") ? static_cast<int>(std::lround(Number(i["count"], 0))) : -1;
                const int taken = takeParcels(*z, i.value("kind", ""), count, i.value("look", json::object()));
                if (taken == 0) {
                    note(i, "there are none in " + zone);
                    continue;
                }
                if (count > 0 && taken < count) out.notes.push_back(ShowInstruction(i) + ": it had only " + std::to_string(taken));
                zonesChanged.insert(zone);
                if (isBuilding(i.value("kind", ""))) buildingsAdded -= taken;
            }
        } else if (name == "replace") {
            json* z = zoneNode(zone);
            const std::string with = i.value("with", "");
            if (!Holds(ontology, *z, with)) {
                note(i, "a " + z->value("module", "") + " can't hold \"" + with + "\"");
                continue;
            }
            const int count = i.contains("count") ? static_cast<int>(std::lround(Number(i["count"], 0))) : -1;
            const int taken = takeParcels(*z, i.value("kind", ""), count, json::object());
            if (taken == 0) {
                note(i, "there are none in " + zone);
                continue;
            }
            addParcels(*z, with, taken, i.value("look", json::object()));
            zonesChanged.insert(zone);
            buildingsAdded += (isBuilding(with) ? taken : 0) - (isBuilding(i.value("kind", "")) ? taken : 0);
        } else if (name == "place") {
            const std::string node = i.value("node", "");
            json* n = SpecNode(work, node);
            const std::string home = node.substr(0, node.find('/'));
            if (n == nullptr || node.find('/') == std::string::npos) {
                note(i, "there's no parcel \"" + node + "\" in the plan");
                continue;
            }
            if (i.contains("into")) {
                const std::string into = i.value("into", "");
                json* to = zoneNode(into);
                json* from = zoneNode(home);
                const std::string kind = n->value("module", "");
                if (to == nullptr || from == nullptr || !Holds(ontology, *to, kind)) {
                    note(i, to == nullptr ? "there's no zone \"" + into + "\"" : "a " + to->value("module", "") + " can't hold \"" + kind + "\"");
                    continue;
                }
                const json look = n->value("quality", json::object());
                // One of the group goes (the group stays, one fewer), or the parcel itself.
                json kept = json::array();
                for (auto& c : (*from)["children"]) {
                    if (c.value("id", "") == node && Repeat(c) > 1) {
                        c["repeat"] = Repeat(c) - 1;
                        kept.push_back(c);
                    } else if (c.value("id", "") != node) {
                        kept.push_back(c);
                    }
                }
                (*from)["children"] = kept;
                addParcels(*to, kind, 1, look);
                zonesChanged.insert(home);
                zonesChanged.insert(into);
            } else if (i.contains("near")) {
                const std::string near = i.value("near", "");
                if (SpecNode(work, near) == nullptr) {
                    note(i, "there's no \"" + near + "\" in the plan");
                    continue;
                }
                out.changes.push_back({{"op", "set"}, {"node", node}, {"rel", json::array({{{"type", "near"}, {"to", near}}})}});
            } else {
                const std::string side = i.value("side", "");
                if (side == "middle" || side == "centre" || side == "center") {
                    out.changes.push_back({{"op", "set"}, {"node", node}, {"rel", json::array({{{"type", "center"}}})}});
                } else if (side == "north" || side == "south" || side == "east" || side == "west") {
                    out.changes.push_back({{"op", "set"}, {"node", node}, {"rel", json::array({{{"type", "in-" + side}, {"to", home}}})}});
                } else {
                    note(i, "say where: a side (north, south, east, west, middle), near another, or into a zone");
                    continue;
                }
            }
        } else {
            ran = false;
        }
        if (ran) out.shown.push_back(ShowInstruction(i));
    }
    // Each zone whose parcels changed, re-planned with all of them (the kept ones too).
    for (const auto& id : zonesChanged) {
        const json* z = SpecNode(work, id);
        out.changes.push_back({{"op", "zone"}, {"node", id}, {"children", z->value("children", json::array())}});
    }
    // More buildings than the Brief allows: its count raised by as many.
    if (buildingsAdded > 0) {
        const json scale = brief.is_object() ? brief.value("scale", json()) : json();
        const json buildings = scale.is_object() ? scale.value("buildings", json()) : json();
        const json count = buildings.is_object() ? buildings.value("count", json()) : json();
        if (count.is_array() && count.size() == 2 && count[0].is_number() && count[1].is_number()) {
            out.brief["scale.buildings.count"] = json::array({count[0].get<int>() + buildingsAdded, count[1].get<int>() + buildingsAdded});
        } else if (count.is_number()) {
            out.brief["scale.buildings.count"] = count.get<int>() + buildingsAdded;
        }
    }
    return out;
}

} // namespace aige
