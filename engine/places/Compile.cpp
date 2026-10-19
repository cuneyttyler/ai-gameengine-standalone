#include "places/Compile.h"

#include "places/Expr.h"
#include "places/PlaceValidate.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace aige {

using json = nlohmann::json;

namespace {

uint64_t Hash(const std::string& text, uint64_t seed) {
    uint64_t h = 1469598103934665603ull ^ (seed * 0x9E3779B97F4A7C15ull);
    for (const unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

json FieldOf(const json& node, const ModuleDef& m, const std::string& name) {
    const FieldDef* f = m.Field(name);
    if (f == nullptr) return nullptr;
    const json group = node.value(f->quantity ? "quantity" : "quality", json::object());
    if (group.is_object() && group.contains(name)) return group[name];
    return f->defaultValue;
}

// A number field (count, length, width...) as [lo, hi], or nothing.
std::optional<std::pair<double, double>> Range(const json& node, const ModuleDef& m, const std::string& name) {
    const FieldDef* f = m.Field(name);
    if (f == nullptr) return std::nullopt;
    const places::Value v = FieldValueOf(*f, FieldOf(node, m, name));
    if (v.kind != places::Value::Kind::Number) return std::nullopt;
    return std::pair{v.number.lo, v.number.hi};
}

json RangeJson(const std::pair<double, double>& r) {
    const int lo = static_cast<int>(std::lround(r.first)), hi = static_cast<int>(std::lround(r.second));
    return lo == hi ? json(lo) : json::array({lo, hi});
}

// The size field as {"w": .., "h": ..}, or a fallback.
json SizeOf(const json& node, const ModuleDef& m, json fallback) {
    const json size = FieldOf(node, m, "size");
    if (size.is_object() && size.contains("w") && size.contains("h")) return {{"w", size["w"]}, {"h", size["h"]}};
    return fallback;
}

std::string Text(const json& v, const std::string& fallback) { return v.is_string() ? v.get<std::string>() : fallback; }

// Whether a module's condition holds for a node's fields (unknown: yes).
class NodeFields : public places::Scope {
public:
    NodeFields(const ModuleDef& m, const json& node) : m_(m), node_(node) {}
    std::optional<places::Value> Name(const std::string& name) override {
        const FieldDef* f = m_.Field(name);
        if (f == nullptr) return std::nullopt;
        return FieldValueOf(*f, FieldOf(node_, m_, name));
    }
    places::Value NodeField(int, const std::string&) override { return places::Value::Unknown(); }
    places::Value Below(int, bool, const std::string&, const std::string&) override { return places::Value::Unknown(); }
    int SelfNode() override { return 0; }

private:
    const ModuleDef& m_;
    const json& node_;
};

bool RuleHolds(const std::string& condition, const ModuleDef& m, const json& node) {
    if (condition.empty()) return true;
    auto expr = places::Expr::Parse(condition);
    NodeFields scope(m, node);
    return !expr || expr->Holds(scope) != places::Tri::False;
}

const SlotDef* PrimarySlot(const ModuleDef& m) {
    for (const auto& s : m.slots) {
        if (s.primary) return &s;
    }
    for (const auto& s : m.slots) {
        if (s.essential) return &s;
    }
    return m.slots.empty() ? nullptr : &m.slots.front();
}

bool RoleIs(const Ontology& ontology, std::string role, const std::string& target) {
    role = role.substr(0, role.find('|'));
    for (int guard = 0; guard < 16 && !role.empty(); ++guard) {
        if (role == target) return true;
        const Role* r = ontology.FindRole(role);
        role = r ? r->broader : "";
    }
    return false;
}

std::string LayerForSlot(const Ontology& ontology, const SlotDef& slot) {
    const std::string role = slot.binds.empty() ? "" : slot.binds.front().role;
    if (RoleIs(ontology, role, "building")) return "building";
    if (RoleIs(ontology, role, "terrain")) return "ground";
    for (const char* low : {"flower", "crop", "herb", "grass"}) {
        if (RoleIs(ontology, role, low)) return "decal";
    }
    return "prop";
}

class Compiler {
public:
    Compiler(const Ontology& ontology, const json& spec, const CompileOptions& options)
        : ontology_(ontology), spec_(spec), options_(options) {}

    json Run();

private:
    const Ontology& ontology_;
    const json& spec_;
    const CompileOptions& options_;
    json statements_ = json::array();
    std::set<std::string> ids_;
    int side_ = 64;
    std::vector<std::string> doors_;

    void Emit(json s) {
        const std::string id = s.value("id", "");
        if (id.empty() || !ids_.insert(id).second) return;
        statements_.push_back(std::move(s));
    }
    void Root(const json& root);
    void Zone(const json& zone, const ModuleDef& m, int index, const json& layoutMix);
    void Parcel(const json& templateNode, const ModuleDef& m, const std::string& instance, int index);
    void Feature(const json& node, const ModuleDef& m, const std::string& self, const std::string& parentInstance,
                 const std::string& parentPrimary);
    void Relations(const ModuleDef& m, const std::string& node, const std::string& self, const std::string& parentRegion,
                   const std::string& parentPrimary, const std::string& selfPrimary);
    std::pair<int, int> Footprint(const ModuleDef& m, const SlotDef& slot, const json& parentSize);
    // The share children a parent's instance gets: exactly round(share x n) of n, by hash.
    bool Gets(const std::string& childId, double share, int instance, int instances) const;
    // What instance `index` of a parcel needs beyond its house: the depth of the
    // garden or yard it gets, and whether it has dressing at its front (a doorstep).
    std::pair<int, bool> Extras(const json& parcel, int index, int instances) const;
};

std::pair<int, bool> Compiler::Extras(const json& parcel, int index, int instances) const {
    int extra = 0;
    bool front = false;
    for (const auto& c : parcel.value("children", json::array())) {
        const ModuleDef* cm = ontology_.Module(c.value("module", ""));
        if (cm == nullptr || cm->level != PlaceLevel::Feature || ontology_.IsA(cm->id, "LinearFeature")) continue;
        const double share = c.contains("share") && c["share"].is_number() ? c["share"].get<double>() : 1.0;
        if (!Gets(c.value("id", ""), share, index, instances)) continue;
        if (cm->Field("size") == nullptr && cm->Field("depth") == nullptr) {
            front = true;
            continue;
        }
        const json size = SizeOf(c, *cm, json{{"w", 3}, {"h", 3}});
        int h = 3;
        if (size["h"].is_number()) h = size["h"].get<int>();
        if (size["h"].is_array()) h = (size["h"][0].get<int>() + size["h"][1].get<int>() + 1) / 2;
        if (cm->Field("depth") && !cm->Field("size")) {
            if (const auto d = Range(c, *cm, "depth")) h = static_cast<int>(std::lround(d->first));
        }
        extra += std::clamp(h, 1, 8);
    }
    return {std::min(extra, 12), front};
}

bool Compiler::Gets(const std::string& childId, double share, int instance, int instances) const {
    if (share >= 0.999) return true;
    const int wanted = static_cast<int>(std::lround(share * instances));
    // Rank this instance among all, by hash: the first `wanted` get it.
    const uint64_t mine = Hash(childId + "#" + std::to_string(instance), options_.seed);
    int below = 0;
    for (int k = 0; k < instances; ++k) {
        const uint64_t other = Hash(childId + "#" + std::to_string(k), options_.seed);
        if (other < mine || (other == mine && k < instance)) ++below;
    }
    return below < wanted;
}

std::pair<int, int> Compiler::Footprint(const ModuleDef& m, const SlotDef& slot, const json& parentSize) {
    if (options_.footprint) {
        if (auto fp = options_.footprint(m.id, slot.name)) return *fp;
    }
    if (LayerForSlot(ontology_, slot) == "building") {
        auto lo = [](const json& v, int fallback) {
            if (v.is_number()) return v.get<int>();
            if (v.is_array() && !v.empty() && v[0].is_number()) return v[0].get<int>();
            return fallback;
        };
        const int w = lo(parentSize.value("w", json()), 5), h = lo(parentSize.value("h", json()), 6);
        return {std::max(2, static_cast<int>(std::lround(w * 0.6))), std::max(2, static_cast<int>(std::lround(h * 0.45)))};
    }
    return {1, 1};
}

void Compiler::Relations(const ModuleDef& m, const std::string& node, const std::string& self, const std::string& parentRegion,
                         const std::string& parentPrimary, const std::string& selfPrimary) {
    int k = 0;
    for (const auto& r : m.relations) {
        json members = json::array();
        bool ok = true;
        for (const auto& member : r.members) {
            std::string ref;
            if (member == "self") ref = self;
            else if (member == "self.primary") ref = selfPrimary;
            else if (member.rfind("self.", 0) == 0) {
                // A slot of this instance: only if it was placed (an adaptable slot may not be).
                const std::string placed = self + "/" + member.substr(5);
                if (ids_.contains(placed)) ref = placed;
            }
            else if (member == "parent") ref = parentRegion;
            else if (member == "parent.primary") ref = parentPrimary;
            else if (member.rfind("parent.primary.", 0) == 0 && !parentPrimary.empty()) ref = parentPrimary + member.substr(14);
            else if (member == "parent.street") ref = parentRegion + ".street";
            else if (member == "parent.door" && !parentPrimary.empty()) ref = parentPrimary + ".door";
            else if (member == "parent.road" || member == "parent.main-road") ref = "site/main-road";
            if (ref.empty()) ok = false;
            members.push_back(ref);
        }
        if (!ok) continue;
        json s = {{"op", "relation"}, {"id", self + "/rel" + std::to_string(k++)}, {"node", node}, {"type", r.type},
                  {"members", members}, {"weight", r.weight}};
        if (r.hard) s["hard"] = true;
        if (r.args.is_object() && !r.args.empty()) s["args"] = r.args;
        Emit(s);
    }
}

void Compiler::Root(const json& root) {
    const ModuleDef* m = ontology_.Module(root.value("module", ""));
    const std::string id = root.value("id", "site");
    if (options_.side > 0) {
        side_ = options_.side;
    } else if (m) {
        const auto area = Range(root, *m, "area");
        side_ = area ? std::max(16, static_cast<int>(std::lround(std::sqrt((area->first + area->second) / 2)))) : 64;
    }
    const int s = side_;
    Emit({{"op", "region"}, {"id", "site"}, {"node", id}, {"shape", "rect"}, {"at", {0, 0}}, {"size", {{"w", s}, {"h", s}}}});
    const json children = root.value("children", json::array());
    std::string ground = "grass";
    std::string groundNode = id;
    for (const auto& c : children) {
        if (c.value("module", "") == "ground") {
            const ModuleDef* gm = ontology_.Module("ground");
            ground = Text(gm ? FieldOf(c, *gm, "terrain") : json(), "grass");
            groundNode = c.value("id", id);
        }
    }
    Emit({{"op", "fill"}, {"id", "site/ground"}, {"node", groundNode}, {"in", "site"}, {"terrain", ground}});
    Emit({{"op", "anchor"}, {"id", "site/entrance-w"}, {"node", id}, {"at", {0, s / 2}}, {"kind", "entrance"}});
    Emit({{"op", "anchor"}, {"id", "site/entrance-e"}, {"node", id}, {"at", {s - 1, s / 2 + s / 10}}, {"kind", "entrance"}});

    // Networks first: the river, the main road(s).
    int mainRoads = 0;
    const ModuleDef* rootModule = m;
    const auto buildings = rootModule ? Range(root, *rootModule, "buildings") : std::nullopt;
    for (const auto& c : children) {
        const std::string module = c.value("module", "");
        const ModuleDef* cm = ontology_.Module(module);
        if (cm == nullptr) continue;
        const std::string node = c.value("id", module);
        if (module == "river") {
            Emit({{"op", "anchor"}, {"id", node + "/source"}, {"node", node}, {"at", {s * 3 / 10, 0}}, {"kind", "marker"}});
            Emit({{"op", "anchor"}, {"id", node + "/mouth"}, {"node", node}, {"at", {s * 7 / 10, s - 1}}, {"kind", "marker"}});
            const auto width = Range(c, *cm, "width");
            Emit({{"op", "path"}, {"id", node}, {"node", node}, {"in", "site"}, {"from", node + "/source"}, {"to", node + "/mouth"},
                  {"class", "river"}, {"width", width ? static_cast<int>(std::lround(width->first)) : 3}, {"terrain", "water"}});
        } else if (module == "road") {
            const std::string cls = Text(FieldOf(c, *cm, "class"), "main");
            if (cls != "main" && mainRoads > 0) continue; // streets come from each zone's network
            const auto width = Range(c, *cm, "width");
            const std::string surface = Text(FieldOf(c, *cm, "surface"), "dirt");
            const std::string pathId = mainRoads == 0 ? "site/main-road" : "site/main-road-" + std::to_string(mainRoads + 1);
            Emit({{"op", "path"}, {"id", pathId}, {"node", node}, {"in", "site"}, {"from", "site/entrance-w"}, {"to", "site/entrance-e"},
                  {"via", json::array({"site.center"})}, {"class", "main"}, {"width", width ? static_cast<int>(std::lround(width->first)) : 2},
                  {"terrain", surface}, {"wiggle", 0.5}});
            ++mainRoads;
            // A town or bigger gets a crossroads.
            if (buildings && buildings->second >= 150 && mainRoads == 1) {
                Emit({{"op", "anchor"}, {"id", "site/entrance-n"}, {"node", id}, {"at", {s / 2 + s / 12, 0}}, {"kind", "entrance"}});
                Emit({{"op", "anchor"}, {"id", "site/entrance-s"}, {"node", id}, {"at", {s / 2 - s / 12, s - 1}}, {"kind", "entrance"}});
                Emit({{"op", "path"}, {"id", "site/cross-road"}, {"node", node}, {"in", "site"}, {"from", "site/entrance-n"},
                      {"to", "site/entrance-s"}, {"via", json::array({"site.center"})}, {"class", "main"}, {"width", 2},
                      {"terrain", surface}, {"wiggle", 0.5}});
            }
        }
    }
    if (mainRoads == 0) {
        Emit({{"op", "path"}, {"id", "site/main-road"}, {"node", id}, {"in", "site"}, {"from", "site/entrance-w"}, {"to", "site/entrance-e"},
              {"via", json::array({"site.center"})}, {"class", "main"}, {"width", 2}, {"terrain", "dirt"}});
    }

    // Zones, partitioned by their site preferences.
    json into = json::array();
    std::vector<std::pair<const json*, const ModuleDef*>> zones;
    for (const auto& c : children) {
        const ModuleDef* cm = ontology_.Module(c.value("module", ""));
        if (cm == nullptr || cm->level != PlaceLevel::Zone) continue;
        const auto area = Range(c, *cm, "area");
        json prefers = json::object();
        for (const auto& [criterion, weight] : cm->prefersSite) prefers[criterion] = weight;
        // A zone has room for its parcels: about 1.9 times their area, with its streets and open land.
        double needed = 0;
        for (const auto& p : c.value("children", json::array())) {
            const ModuleDef* pm = ontology_.Module(p.value("module", ""));
            if (pm == nullptr || pm->level != PlaceLevel::Parcel) continue;
            const json size = SizeOf(p, *pm, json{{"w", 5}, {"h", 6}});
            auto mid = [](const json& v) {
                if (v.is_number()) return v.get<double>();
                if (v.is_array() && v.size() == 2) return (v[0].get<double>() + v[1].get<double>()) / 2;
                return 5.0;
            };
            needed += mid(size["w"]) * mid(size["h"]) * std::max(1, p.value("repeat", 1));
        }
        const double planned = area ? (area->first + area->second) / 2 : 400.0;
        into.push_back({{"id", c.value("id", "")}, {"node", c.value("id", "")}, {"module", cm->id},
                        {"area", static_cast<int>(std::lround(std::max(planned, needed * 1.9)))}, {"prefers", prefers}});
        zones.push_back({&c, cm});
    }
    if (!into.empty()) {
        Emit({{"op", "partition"}, {"id", "site/zones"}, {"node", id}, {"in", "site"}, {"into", into}, {"method", "growth"}});
    }
    const json layout = m ? FieldOf(root, *m, "layout") : json();
    for (size_t i = 0; i < zones.size(); ++i) Zone(*zones[i].first, *zones[i].second, static_cast<int>(i), layout);

    // Walls round the site, with gates where the roads cross.
    for (const auto& c : children) {
        if (c.value("module", "") != "wall") continue;
        const std::string node = c.value("id", "wall");
        json gaps = json::array({"site/main-road"});
        if (ids_.contains("site/cross-road")) gaps.push_back("site/cross-road");
        Emit({{"op", "line"}, {"id", node}, {"node", node}, {"slot", "wall"}, {"along", "site.edge"}, {"gaps", gaps}, {"closed", true}});
        Emit({{"op", "check"}, {"id", node + "/closed"}, {"node", node}, {"predicate", "closed"}, {"args", {{"ref", node}}}});
    }
    Emit({{"op", "check"}, {"id", "site/doors-reachable"}, {"node", id}, {"predicate", "reachable"}, {"args", {{"from", "entrances"}, {"to", "doors"}}}});
}

void Compiler::Zone(const json& zone, const ModuleDef& m, int index, const json& layoutMix) {
    const std::string zoneId = zone.value("id", "");
    // Nature zones: their plants, scattered.
    if (ontology_.IsA(m.id, "NatureArea")) {
        const SlotDef* slot = PrimarySlot(m);
        double density = 0.15;
        for (const char* field : {"trees", "flowers", "rocks"}) {
            if (const auto r = Range(zone, m, field)) density = (r->first + r->second) / 2;
        }
        if (slot) {
            Emit({{"op", "scatter"}, {"id", zoneId + "/" + slot->name}, {"node", zoneId}, {"in", zoneId}, {"slot", slot->name},
                  {"density", density}, {"spacing", {{"min", 1}}}, {"layer", LayerForSlot(ontology_, *slot)}});
        }
        return;
    }
    const json children = zone.value("children", json::array());
    if (children.empty()) return;
    // Its parcels, as instances.
    json into = json::array();
    std::vector<std::tuple<const json*, const ModuleDef*, std::string, int>> parcels;
    int depth = 6;
    for (const auto& c : children) {
        const ModuleDef* cm = ontology_.Module(c.value("module", ""));
        if (cm == nullptr || cm->level != PlaceLevel::Parcel) continue;
        const int repeat = std::max(1, c.value("repeat", 1));
        const json size = SizeOf(c, *cm, json{{"w", 5}, {"h", 6}});
        for (int k = 0; k < repeat; ++k) {
            const std::string instance = repeat == 1 ? c.value("id", "") : c.value("id", "") + "#" + std::to_string(k);
            // Deep enough for its house, its front row and the garden or yard it gets.
            json lot = size;
            const auto [extra, front] = Extras(c, k, repeat);
            const int more = extra + (front ? 1 : 0);
            if (more > 0 && lot["h"].is_number()) lot["h"] = lot["h"].get<int>() + more;
            if (more > 0 && lot["h"].is_array()) lot["h"] = {lot["h"][0].get<int>() + more, lot["h"][1].get<int>() + more};
            if (lot["h"].is_number()) depth = std::max(depth, lot["h"].get<int>());
            if (lot["h"].is_array()) depth = std::max(depth, lot["h"][1].get<int>());
            into.push_back({{"id", instance}, {"node", c.value("id", "")}, {"module", cm->id}, {"size", lot}});
            parcels.push_back({&c, cm, instance, k});
        }
    }
    // Mix the kinds of parcel along the streets, by a seeded shuffle (bigger ones first).
    std::vector<size_t> order(into.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const bool bigA = !ontology_.IsA(into[a]["module"], "Dwelling"), bigB = !ontology_.IsA(into[b]["module"], "Dwelling");
        if (bigA != bigB) return bigA;
        // The largest first (the square, the temple), while the streets still have room.
        auto area = [&](size_t i) {
            const json& sz = into[i]["size"];
            auto mid = [](const json& v) { return v.is_array() ? (v[0].get<double>() + v[1].get<double>()) / 2 : v.is_number() ? v.get<double>() : 0.0; };
            return mid(sz.value("w", json())) * mid(sz.value("h", json()));
        };
        if (bigA && area(a) != area(b)) return area(a) > area(b);
        return Hash(into[a]["id"].get<std::string>(), options_.seed) < Hash(into[b]["id"].get<std::string>(), options_.seed);
    });
    json ordered = json::array();
    for (size_t i : order) ordered.push_back(into[i]);

    // The street style: the zone's, else the settlement's mix, by zone.
    std::string style = Text(FieldOf(zone, m, "street-style"), "");
    if (style.empty()) {
        const json mix = layoutMix.is_object() && layoutMix.contains("mix") ? layoutMix["mix"] : layoutMix;
        if (mix.is_string()) style = mix.get<std::string>();
        else if (mix.is_object() && !mix.empty()) {
            const double pick = (Hash(zoneId, options_.seed) % 1000) / 1000.0;
            double run = 0;
            for (const auto& [pattern, share] : mix.items()) {
                run += share.is_number() ? share.get<double>() : 0;
                if (pick <= run) {
                    style = pattern;
                    break;
                }
            }
            if (style.empty()) style = mix.begin().key();
        }
    }
    if (style.empty()) style = index == 0 ? "organic" : "organic";
    Emit({{"op", "network"}, {"id", zoneId + "/streets"}, {"node", zoneId}, {"in", zoneId}, {"style", style},
          {"spacing", 2 * depth + 2}, {"width", 1}, {"class", "street"}, {"terrain", "dirt"}});
    Emit({{"op", "subdivide"}, {"id", zoneId + "/plots"}, {"node", zoneId}, {"in", zoneId}, {"style", "frontage"},
          {"access", "street"}, {"into", ordered}});
    Emit({{"op", "check"}, {"id", zoneId + "/plots-placed"}, {"node", zoneId}, {"predicate", "count"},
          {"args", {{"ref", zoneId + "/plots"}, {"range", {static_cast<int>(ordered.size() * 9 / 10), static_cast<int>(ordered.size())}}}}});
    if (const auto band = Range(zone, m, "footprint-density")) {
        // Built cells over the ward's cells: the band is the plan's, widened (roads and open land count too).
        Emit({{"op", "check"}, {"id", zoneId + "/density"}, {"node", zoneId}, {"predicate", "footprint-density"},
              {"args", {{"ref", zoneId}, {"band", {band->first * 0.3, band->second}}}}});
    }
    for (const auto& [node, cm, instance, k] : parcels) Parcel(*node, *cm, instance, k);
}

void Compiler::Parcel(const json& node, const ModuleDef& m, const std::string& instance, int index) {
    const std::string nodeId = node.value("id", "");
    const int instances = std::max(1, node.value("repeat", 1));
    const SlotDef* primary = PrimarySlot(m);
    std::string primaryId;
    const json size = SizeOf(node, m, json{{"w", 5}, {"h", 6}});
    if (primary) {
        primaryId = instance + "/" + primary->name;
        const auto [fw, fh] = Footprint(m, *primary, size);
        const std::string layer = LayerForSlot(ontology_, *primary);
        json s = {{"op", "place"}, {"id", primaryId}, {"node", nodeId}, {"in", instance}, {"slot", primary->name}, {"module", m.id},
                  {"primary", true}, {"footprint", {{"w", fw}, {"h", fh}}}, {"facing", "street"}, {"layer", layer}};
        int setback = 1;
        if (const auto r = Range(node, m, "setback")) setback = static_cast<int>(std::lround(r->first));
        if (Extras(node, index, instances).second) setback = std::max(1, setback); // its doorstep, sign or stall goes there
        s["setback"] = setback;
        if (layer == "ground") {
            // A square: its terrain is the primary "object".
            Emit({{"op", "fill"}, {"id", primaryId}, {"node", nodeId}, {"in", instance}, {"terrain", Text(FieldOf(node, m, "surface"), "cobble")}});
            primaryId.clear();
        } else {
            Emit(s);
            if (layer == "building") doors_.push_back(primaryId);
        }
    }
    // A square's centrepiece and stalls.
    if (m.id == "market-square") {
        const std::string centre = Text(FieldOf(node, m, "centrepiece"), "well");
        if (centre != "none") {
            Emit({{"op", "place"}, {"id", instance + "/centrepiece"}, {"node", nodeId}, {"in", instance}, {"slot", "centrepiece"},
                  {"footprint", {{"w", 2}, {"h", 2}}}, {"position", "center"}, {"layer", "prop"}});
        }
        if (const auto stalls = Range(node, m, "stalls")) {
            Emit({{"op", "rows"}, {"id", instance + "/stalls"}, {"node", nodeId}, {"in", instance}, {"slot", "stall"},
                  {"count", RangeJson({std::max(1.0, stalls->first / 4), std::max(1.0, stalls->second / 4)})}, {"layer", "prop"}, {"margin", 1}});
        }
    }
    Relations(m, nodeId, instance, instance.substr(0, instance.find('/')), "", primaryId);
    // Its features, as this instance's share of them.
    for (const auto& c : node.value("children", json::array())) {
        const ModuleDef* cm = ontology_.Module(c.value("module", ""));
        if (cm == nullptr) continue;
        const double share = c.contains("share") && c["share"].is_number() ? c["share"].get<double>() : 1.0;
        if (!Gets(c.value("id", ""), share, index, instances)) continue;
        const std::string self = instance + "/" + cm->id;
        Feature(c, *cm, self, instance, primaryId);
    }
}

void Compiler::Feature(const json& node, const ModuleDef& m, const std::string& self, const std::string& parentInstance,
                       const std::string& parentPrimary) {
    const std::string nodeId = node.value("id", "");
    const SlotDef* primary = PrimarySlot(m);
    // Lines: along the parent's edge, a gate at its front.
    if (ontology_.IsA(m.id, "LinearFeature")) {
        if (primary) {
            Emit({{"op", "line"}, {"id", self}, {"node", nodeId}, {"slot", primary->name}, {"along", parentInstance + ".edge"},
                  {"gaps", json::array({"front"})}, {"closed", true}});
            Emit({{"op", "check"}, {"id", self + "/closed"}, {"node", nodeId}, {"predicate", "closed"}, {"args", {{"ref", self}}}});
        }
        return;
    }
    // Dressing-like features without a size (a doorstep, a sign): by the door.
    const bool sized = m.Field("size") != nullptr || m.Field("depth") != nullptr;
    if (!sized) {
        if (primary && !parentPrimary.empty()) {
            double density = 0.5;
            if (const auto r = Range(node, m, "items")) density = std::clamp((r->first + r->second) / 6.0, 0.1, 1.0);
            Emit({{"op", "scatter"}, {"id", self}, {"node", nodeId}, {"in", parentPrimary + ".front"}, {"slot", primary->name},
                  {"density", density}, {"layer", LayerForSlot(ontology_, *primary)}});
        } else if (primary) {
            Emit({{"op", "place"}, {"id", self}, {"node", nodeId}, {"in", parentInstance}, {"slot", primary->name},
                  {"footprint", {{"w", 1}, {"h", 1}}}, {"layer", LayerForSlot(ontology_, *primary)}, {"essential", false}});
        }
        return;
    }
    // A region of its own, related to its parent's main object.
    json size = SizeOf(node, m, json{{"w", 3}, {"h", 3}});
    if (const auto depth = Range(node, m, "depth"); depth && !m.Field("size")) {
        size = {{"w", 4}, {"h", std::max(1, static_cast<int>(std::lround(depth->first)))}};
    }
    Emit({{"op", "region"}, {"id", self}, {"node", nodeId}, {"in", parentInstance}, {"shape", "rect"}, {"size", size}, {"module", m.id},
          {"essential", false}});
    Relations(m, nodeId, self, parentInstance, parentPrimary, "");
    if (const json surface = FieldOf(node, m, "surface"); surface.is_string()) {
        Emit({{"op", "fill"}, {"id", self + "/surface"}, {"node", nodeId}, {"in", self}, {"terrain", surface}});
    }
    // Its contents.
    std::string mixField;
    const json plantMix = [&] {
        for (const char* field : {"flower-mix", "crop-mix", "fruit-mix", "livestock-mix"}) {
            const json v = FieldOf(node, m, field);
            if (v.is_object()) {
                mixField = field;
                return v.contains("mix") ? v["mix"] : v;
            }
        }
        return json::object();
    }();
    // What it's filled with: the slot whose concepts the mix names, else its main one.
    const SlotDef* contents = primary;
    for (const auto& slot : m.slots) {
        for (const auto& b : slot.binds) {
            if (!mixField.empty() && b.concepts == mixField) contents = &slot;
        }
    }
    bool fenceLine = false;
    if (ontology_.IsA(m.id, "PlantedArea") || m.id == "pasture" || m.id == "graveyard") {
        if (const json fenced = FieldOf(node, m, "fenced"); fenced.is_boolean() && fenced.get<bool>()) {
            if (const SlotDef* fence = [&]() -> const SlotDef* {
                    for (const auto& s : m.slots) {
                        if (s.name == "fence") return &s;
                    }
                    return nullptr;
                }()) {
                Emit({{"op", "line"}, {"id", self + "/fence"}, {"node", nodeId}, {"slot", fence->name}, {"along", self + ".edge"},
                      {"gaps", json::array({parentPrimary.empty() ? "front" : "toward:" + parentPrimary})}, {"closed", true}});
                fenceLine = true;
            }
        }
        bool beds = false;
        // Beds are counted by the garden's `beds` field: the compiler writes them out when
        // the module's rule allows them here (a vegetable or mixed garden), or the plan lists them.
        for (const auto& c : node.value("children", json::array())) beds = beds || c.value("module", "") == "vegetable-bed";
        for (const auto& rule : m.children) {
            if (rule.module == "vegetable-bed" && !rule.count.empty() && RuleHolds(rule.when, m, node)) beds = true;
        }
        const auto bedCount = Range(node, m, "beds");
        if (beds && bedCount) {
            Emit({{"op", "rows"}, {"id", self + "/beds"}, {"node", nodeId}, {"in", self}, {"regions", true}, {"module", "vegetable-bed"},
                  {"count", RangeJson(*bedCount)}, {"margin", 1}});
            // One statement for every bed the rows make (PLL instance statements, layout-ir.md §4.2).
            double density = 0.7;
            if (const ModuleDef* bed = ontology_.Module("vegetable-bed")) {
                if (const auto plants = Range(json::object(), *bed, "plants")) density = (plants->first + plants->second) / 2;
            }
            Emit({{"op", "scatter"}, {"id", self + "/beds[*]/crops"}, {"node", nodeId}, {"in", self + "/beds[*]"}, {"slot", "crop"},
                  {"density", density}, {"layer", "decal"}, {"mix", plantMix}});
        }
        if (m.id == "orchard") {
            Emit({{"op", "grid"}, {"id", self + "/trees"}, {"node", nodeId}, {"in", self}, {"slot", "tree"},
                  {"spacing", {{"x", 2}, {"y", 2}}}, {"count", Range(node, m, "trees") ? RangeJson(*Range(node, m, "trees")) : json(9)},
                  {"layer", "prop"}});
        } else if (m.id == "graveyard") {
            Emit({{"op", "grid"}, {"id", self + "/graves"}, {"node", nodeId}, {"in", self}, {"slot", "grave"},
                  {"spacing", {{"x", 2}, {"y", 2}}}, {"count", Range(node, m, "graves") ? RangeJson(*Range(node, m, "graves")) : json(8)},
                  {"layer", "prop"}});
        } else if (m.id == "field") {
            Emit({{"op", "rows"}, {"id", self + "/rows"}, {"node", nodeId}, {"in", self}, {"slot", "crop"},
                  {"count", Range(node, m, "rows") ? RangeJson(*Range(node, m, "rows")) : json(6)}, {"layer", "decal"}, {"margin", 0}});
        } else if (contents) {
            double density = 0.3;
            for (const char* field : {"flowers", "plants", "trees"}) {
                if (const auto r = Range(node, m, field)) density = (r->first + r->second) / 2;
            }
            // The whole area but its fence (a small garden has hardly an inside).
            std::string in = fenceLine ? self + " - " + self + "/fence" : self;
            if (beds) in += " - " + self + "/beds";
            json scatter = {{"op", "scatter"}, {"id", self + "/" + contents->name}, {"node", nodeId}, {"in", in}, {"slot", contents->name},
                            {"density", density}, {"layer", LayerForSlot(ontology_, *contents)}};
            // Counted contents (a pasture's animals) are a count, not a density.
            if (const auto n = Range(node, m, "animals")) scatter["count"] = RangeJson(*n);
            if (!plantMix.empty()) scatter["mix"] = plantMix;
            Emit(scatter);
        }
        return;
    }
    // Anything else: its main object in its middle (a well, a bench, a stable).
    // A ground slot (a yard's path) is its surface, filled above: nothing to place.
    if (primary && LayerForSlot(ontology_, *primary) != "ground") {
        Emit({{"op", "place"}, {"id", self + "/" + primary->name}, {"node", nodeId}, {"in", self}, {"slot", primary->name},
              {"footprint", {{"w", 1}, {"h", 1}}}, {"position", "center"}, {"layer", LayerForSlot(ontology_, *primary)}, {"essential", false}});
    }
}

json Compiler::Run() {
    if (!spec_.contains("root")) return json();
    Root(spec_["root"]);
    return {{"schema", "aige.ir/1"},
            {"place", spec_["root"].value("id", "")},
            {"spec", "spec.json"},
            {"seed", options_.seed},
            {"grid", {{"style", options_.isoGrid ? "isometric" : "topdown"}, {"bounds", {0, 0, side_, side_}}}},
            {"statements", statements_}};
}

} // namespace

json CompileSpec(const Ontology& ontology, const json& spec, const CompileOptions& options) {
    Compiler compiler(ontology, spec, options);
    return compiler.Run();
}

} // namespace aige
