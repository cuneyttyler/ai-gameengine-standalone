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

// A node's children by reference (pointers into them are kept): never `value()`'s copy.
const json& ChildrenOf(const json& node) {
    static const json none = json::array();
    if (!node.is_object()) return none;
    const auto it = node.find("children");
    return it != node.end() && it->is_array() ? *it : none;
}

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
    for (const char* low : {"flower", "crop", "herb", "grass", "decal"}) {
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
    int width_ = 0, height_ = 0; // a dungeon's or a building's grid (its levels side by side); 0: the square site
    std::vector<std::string> doors_;
    std::vector<std::string> groupAnchors_; // furniture groups' anchor statements, for their check

    void Emit(json s) {
        // Plants grow in patches (a bed, a thicket, a lawn), not one by one over the ground.
        if (s.value("op", "") == "scatter" && !s.contains("cluster") && !s.contains("count")) {
            static const char* plants[] = {"flower", "bush", "shrub", "grass", "herb", "fern", "reed", "weed"};
            const std::string slot = s.value("slot", "");
            for (const char* p : plants) {
                if (slot.find(p) != std::string::npos) {
                    s["cluster"] = {{"size", json::array({5, 18})}, {"fill", 0.8}};
                    s["spacing"] = {{"min", 0}};
                    break;
                }
            }
        }
        const std::string id = s.value("id", "");
        if (id.empty() || !ids_.insert(id).second) return;
        statements_.push_back(std::move(s));
    }
    void Root(const json& root);
    // The other layouts (7.10): levels of rooms and corridors; floors of rooms.
    void DungeonRoot(const json& root, const ModuleDef& m);
    void InteriorRoot(const json& root, const ModuleDef& m);
    // A zone whose parcels stand apart (points of interest, a region's sites), joined by trails or roads.
    void ScatteredZone(const json& zone, const ModuleDef& m, const json& hints);
    // The module's contents hints ({slot, count|density, where, near, layer, mix}), as scatters in `in`.
    void Contents(const json& list, const json& node, const ModuleDef& m, const std::string& self, const std::string& in,
                  const std::string& primaryId);
    // A furniture group: its anchor where the hint says, its members near it.
    void Group(const json& node, const ModuleDef& m, const std::string& self, const std::string& parentInstance);
    // A hint's number: a number, a field's middle, or {field, values} by the field's value.
    std::optional<double> HintNumber(const json& hint, const json& node, const ModuleDef& m) const;
    json HintCount(const json& hint, const json& node, const ModuleDef& m) const;
    void Zone(const json& zone, const ModuleDef& m, int index, const json& layoutMix);
    void Landscape(const std::string& root, const std::string& zonesIn, const std::vector<std::pair<const json*, const ModuleDef*>>& zones);
    void Parcel(const json& templateNode, const ModuleDef& m, const std::string& instance, int index);
    void Feature(const json& node, const ModuleDef& m, const std::string& self, const std::string& parentInstance,
                 const std::string& parentPrimary);
    void Relations(const ModuleDef& m, const std::string& node, const std::string& self, const std::string& parentRegion,
                   const std::string& parentPrimary, const std::string& selfPrimary);
    std::pair<int, int> Footprint(const ModuleDef& m, const SlotDef& slot, const json& parentSize);
    // The share children a parent's instance gets: exactly round(share x n) of n, by hash.
    bool Gets(const std::string& childId, double share, int instance, int instances) const;
    // Which instances get a share child, ranked once per (child, share, instances): a city's plots are many.
    mutable std::map<std::tuple<std::string, double, int>, std::vector<char>> gets_;
    // What instance `index` of a parcel needs beyond its house: the area of the
    // gardens and yards it gets, and whether it has dressing at its front (a doorstep).
    std::pair<int, bool> Extras(const json& parcel, int index, int instances) const;
    // A parcel instance's lot: its size, deepened for what it holds.
    json LotSize(const json& parcel, const ModuleDef& m, int index, int instances) const;
    // The lots of a zone's parcels, added up (cells).
    double ParcelArea(const json& zone) const;
};

json Compiler::LotSize(const json& parcel, const ModuleDef& m, int index, int instances) const {
    json lot = SizeOf(parcel, m, json{{"w", 5}, {"h", 6}});
    const auto [extra, front] = Extras(parcel, index, instances);
    // Deep enough for its house, its front row and the gardens and yards it gets:
    // their area spread over the lot's width, no more than doubling its depth.
    auto low = [](const json& v, int fallback) {
        if (v.is_number()) return v.get<int>();
        if (v.is_array() && !v.empty()) return v[0].get<int>();
        return fallback;
    };
    const int width = std::max(1, low(lot["w"], 5)), depth = std::max(1, low(lot["h"], 6));
    const int more = std::min(depth, (extra + width - 1) / width) + (front ? 1 : 0);
    if (more > 0 && lot["h"].is_number()) lot["h"] = lot["h"].get<int>() + more;
    if (more > 0 && lot["h"].is_array()) lot["h"] = {lot["h"][0].get<int>() + more, lot["h"][1].get<int>() + more};
    return lot;
}

double Compiler::ParcelArea(const json& zone) const {
    auto mid = [](const json& v) {
        if (v.is_number()) return v.get<double>();
        if (v.is_array() && v.size() == 2) return (v[0].get<double>() + v[1].get<double>()) / 2;
        return 5.0;
    };
    double total = 0;
    for (const auto& p : zone.value("children", json::array())) {
        const ModuleDef* pm = ontology_.Module(p.value("module", ""));
        if (pm == nullptr) continue;
        // The zone's own features take land too.
        if (pm->level == PlaceLevel::Feature && !ontology_.IsA(pm->id, "LinearFeature")) {
            const json size = SizeOf(p, *pm, json{{"w", 3}, {"h", 3}});
            total += mid(size["w"]) * mid(size["h"]) * std::max(1, p.value("repeat", 1));
            continue;
        }
        if (pm->level != PlaceLevel::Parcel) continue;
        const int repeat = std::max(1, p.value("repeat", 1));
        for (int k = 0; k < repeat; ++k) {
            const json lot = LotSize(p, *pm, k, repeat);
            total += mid(lot["w"]) * mid(lot["h"]);
        }
    }
    return total;
}

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
        auto lo = [](const json& v, int fallback) {
            if (v.is_number()) return v.get<int>();
            if (v.is_array() && !v.empty()) return v[0].get<int>();
            return fallback;
        };
        int w = lo(size["w"], 3), h = lo(size["h"], 3);
        if (cm->Field("depth") && !cm->Field("size")) {
            w = 4;
            if (const auto d = Range(c, *cm, "depth")) h = static_cast<int>(std::lround(d->first));
        }
        extra += std::clamp(w, 1, 12) * std::clamp(h, 1, 12);
    }
    return {extra, front};
}

bool Compiler::Gets(const std::string& childId, double share, int instance, int instances) const {
    if (share >= 0.999) return true;
    const int wanted = static_cast<int>(std::lround(share * instances));
    // Rank the instances by hash: the first `wanted` get it.
    auto& gets = gets_[{childId, share, instances}];
    if (gets.empty()) {
        std::vector<std::pair<uint64_t, int>> ranked;
        for (int k = 0; k < instances; ++k) ranked.push_back({Hash(childId + "#" + std::to_string(k), options_.seed), k});
        std::sort(ranked.begin(), ranked.end());
        gets.assign(instances, 0);
        for (int k = 0; k < wanted && k < instances; ++k) gets[ranked[k].second] = 1;
    }
    return instance >= 0 && instance < instances && gets[instance];
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

std::optional<double> Compiler::HintNumber(const json& hint, const json& node, const ModuleDef& m) const {
    if (hint.is_number()) return hint.get<double>();
    if (hint.is_string()) {
        if (const auto r = Range(node, m, hint.get<std::string>())) return (r->first + r->second) / 2;
        return std::nullopt;
    }
    if (hint.is_object() && hint.contains("field")) {
        const json v = FieldOf(node, m, hint.value("field", ""));
        const json values = hint.value("values", json::object());
        if (v.is_string() && values.contains(v.get<std::string>()) && values[v.get<std::string>()].is_number()) return values[v.get<std::string>()].get<double>();
    }
    return std::nullopt;
}

json Compiler::HintCount(const json& hint, const json& node, const ModuleDef& m) const {
    if (hint.is_number_integer() || hint.is_array()) return hint;
    if (hint.is_string()) {
        if (const auto r = Range(node, m, hint.get<std::string>())) return RangeJson(*r);
    }
    return json();
}

void Compiler::Contents(const json& list, const json& node, const ModuleDef& m, const std::string& self, const std::string& in,
                        const std::string& primaryId) {
    if (!list.is_array()) return;
    const std::string nodeId = node.value("id", "");
    const SlotDef* primary = PrimarySlot(m);
    int k = 0;
    for (const auto& item : list) {
        std::string slotName = item.value("slot", "");
        if (slotName == "primary" && primary) slotName = primary->name;
        const SlotDef* slot = nullptr;
        for (const auto& sd : m.slots) {
            if (sd.name == slotName) slot = &sd;
        }
        if (slot == nullptr) continue;
        const std::string where = item.value("where", "");
        json s = {{"op", "scatter"}, {"id", self + "/more-" + slotName + (k ? "-" + std::to_string(k) : "")}, {"node", nodeId},
                  {"in", in + (where == "border" ? ".border" : where == "inner" ? ".inner" : "")}, {"slot", slotName},
                  {"layer", item.value("layer", LayerForSlot(ontology_, *slot))}};
        const json count = HintCount(item.value("count", json()), node, m);
        const auto density = HintNumber(item.value("density", json()), node, m);
        if (!count.is_null()) s["count"] = count;
        else if (density) s["density"] = *density;
        else s["count"] = 1;
        if (item.value("near", "") == "primary" && !primaryId.empty()) {
            s["near"] = primaryId;
            s["radius"] = item.value("radius", 2);
        }
        if (item.contains("min")) s["min"] = item["min"];
        if (item.value("solid", false) || s["layer"] == "prop") s["spacing"] = {{"min", 1}};
        if (item.contains("mix")) {
            const json v = FieldOf(node, m, item.value("mix", ""));
            if (v.is_object()) s["mix"] = v.contains("mix") ? v["mix"] : v;
        }
        Emit(s);
        ++k;
    }
}

void Compiler::Group(const json& node, const ModuleDef& m, const std::string& self, const std::string& parentInstance) {
    const std::string nodeId = node.value("id", "");
    const SlotDef* anchor = PrimarySlot(m);
    if (anchor == nullptr) return;
    const json hint = m.compile.value("group", json::object());
    const std::string anchorId = self + "/" + anchor->name;
    const auto [fw, fh] = Footprint(m, *anchor, json::object());
    Emit({{"op", "place"}, {"id", anchorId}, {"node", nodeId}, {"in", parentInstance}, {"slot", anchor->name}, {"footprint", {{"w", fw}, {"h", fh}}},
          {"position", hint.value("position", "")}, {"layer", LayerForSlot(ontology_, *anchor)}, {"essential", false}});
    groupAnchors_.push_back(anchorId);
    for (const auto& member : hint.value("members", json::array())) {
        const std::string slotName = member.value("slot", "");
        const SlotDef* slot = nullptr;
        for (const auto& sd : m.slots) {
            if (sd.name == slotName) slot = &sd;
        }
        if (slot == nullptr) continue;
        json count = HintCount(member.value("count", json()), node, m);
        if (count.is_null()) count = 1;
        if ((count.is_number() && count.get<int>() <= 0) || (count.is_array() && count[1].get<int>() <= 0)) continue;
        const std::string memberId = self + "/" + slotName;
        Emit({{"op", "place"}, {"id", memberId}, {"node", nodeId}, {"in", parentInstance}, {"slot", slotName}, {"count", count},
              {"footprint", {{"w", 1}, {"h", 1}}}, {"layer", LayerForSlot(ontology_, *slot)}, {"essential", false}});
        Emit({{"op", "relation"}, {"id", memberId + "/near"}, {"node", nodeId}, {"type", "near"}, {"members", {memberId, anchorId}},
              {"weight", 3.0}, {"args", {{"d0", 1}, {"d1", member.value("near", 1) + 1}}}});
    }
}

void Compiler::ScatteredZone(const json& zone, const ModuleDef& m, const json& hints) {
    const std::string zoneId = zone.value("id", "");
    json members = json::array();
    for (const auto& c : zone.value("children", json::array())) {
        const ModuleDef* cm = ontology_.Module(c.value("module", ""));
        if (cm == nullptr || cm->level != PlaceLevel::Parcel) continue;
        const int repeat = std::max(1, c.value("repeat", 1));
        for (int k = 0; k < repeat; ++k) {
            const std::string instance = repeat == 1 ? c.value("id", "") : c.value("id", "") + "#" + std::to_string(k);
            Emit({{"op", "region"}, {"id", instance}, {"node", c.value("id", "")}, {"in", zoneId}, {"shape", "rect"},
                  {"size", SizeOf(c, *cm, json{{"w", 6}, {"h", 6}})}, {"module", cm->id}, {"spread", hints.value("spread", 12)},
                  {"group", zoneId}, {"essential", true}});
            Relations(*cm, c.value("id", ""), instance, zoneId, "", "");
            Parcel(c, *cm, instance, k);
            members.push_back(instance);
        }
    }
    // The trails (or roads) to them, from the site's entrances.
    const json connect = hints.value("connect", json::object());
    std::string terrain = connect.value("terrain", "dirt");
    for (const auto& c : spec_["root"].value("children", json::array())) {
        const ModuleDef* cm = ontology_.Module(c.value("module", ""));
        if (cm && cm->id == "trail-network") terrain = Text(FieldOf(c, *cm, "surface"), terrain);
    }
    if (!members.empty()) {
        Emit({{"op", "connect"}, {"id", zoneId + "/trails"}, {"node", zoneId}, {"in", zoneId}, {"members", members},
              {"from", json::array({"site/entrance-w", "site/entrance-e"})}, {"class", connect.value("class", "trail")},
              {"width", connect.value("width", 1)}, {"terrain", terrain}});
    }
    Emit({{"op", "check"}, {"id", zoneId + "/placed"}, {"node", zoneId}, {"predicate", "count"},
          {"args", {{"ref", zoneId + "/trails"}, {"range", {static_cast<int>(members.size()), static_cast<int>(members.size())}}}}});
}

void Compiler::Root(const json& root) {
    const ModuleDef* m = ontology_.Module(root.value("module", ""));
    const std::string id = root.value("id", "site");
    if (const BuildType* build = m ? ontology_.BuildOfRoot(m->id) : nullptr) {
        if (build->layout == "dungeon") return DungeonRoot(root, *m);
        if (build->layout == "interior") return InteriorRoot(root, *m);
    }
    // A wall round an inner ward (a castle): its margin, for the moat and the ground outside.
    const json enclosure = m && m->compile.is_object() ? m->compile.value("enclosure", json()) : json();
    const json* wallNode = nullptr;
    const json* moatNode = nullptr;
    if (enclosure.is_object()) {
        for (const auto& c : ChildrenOf(root)) {
            if (c.value("module", "") == enclosure.value("wall", "")) wallNode = &c;
            if (c.value("module", "") == enclosure.value("moat", "")) moatNode = &c;
        }
    }
    const int margin = wallNode ? enclosure.value("margin", 5) : 0;
    if (options_.side > 0) {
        side_ = options_.side;
    } else if (m) {
        const auto area = Range(root, *m, "area");
        side_ = area ? std::max(16, static_cast<int>(std::lround(std::sqrt((area->first + area->second) / 2)))) : 64;
    }
    {
        double required = 0;
        for (const auto& c : root.value("children", json::array())) {
            const ModuleDef* cm = ontology_.Module(c.value("module", ""));
            if (cm == nullptr || cm->level != PlaceLevel::Zone) continue;
            double scale = 1.0;
            if (auto it = options_.zoneScale.find(c.value("id", "")); it != options_.zoneScale.end()) scale = it->second;
            required += ParcelArea(c) * 1.9 * scale;
        }
        if (!options_.fixedSide) side_ = std::max(side_, static_cast<int>(std::ceil(std::sqrt(required / 0.6))) + 2 * margin);
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

    // The enclosure: a moat round the inner ward, the curtain wall on its edge, towers at
    // its corners and the gatehouse where the road comes in. The zones go inside it.
    std::string zonesIn = "site";
    if (wallNode) {
        const int moatWidth = moatNode ? [&] {
            const ModuleDef* mm = ontology_.Module(moatNode->value("module", ""));
            const auto w = mm ? Range(*moatNode, *mm, "width") : std::nullopt;
            return w ? std::max(1, static_cast<int>(std::lround(w->first))) : 2;
        }() : 0;
        const int inner = s - 2 * margin;
        if (moatNode) {
            const std::string moatId = moatNode->value("id", "moat");
            const int at = std::max(1, margin - moatWidth);
            Emit({{"op", "region"}, {"id", "site/moat"}, {"node", moatId}, {"in", "site"}, {"shape", "rect"}, {"at", {at, at}},
                  {"size", {{"w", s - 2 * at}, {"h", s - 2 * at}}}, {"overRoads", true}});
            Emit({{"op", "region"}, {"id", "site/inner"}, {"node", id}, {"in", "site/moat"}, {"shape", "rect"}, {"at", {margin, margin}},
                  {"size", {{"w", inner}, {"h", inner}}}, {"overRoads", true}});
            Emit({{"op", "flood"}, {"id", moatId + "/water"}, {"node", moatId}, {"in", "site/moat"}});
        } else {
            Emit({{"op", "region"}, {"id", "site/inner"}, {"node", id}, {"in", "site"}, {"shape", "rect"}, {"at", {margin, margin}},
                  {"size", {{"w", inner}, {"h", inner}}}, {"overRoads", true}});
        }
        zonesIn = "site/inner";
        const std::string wallId = wallNode->value("id", "curtain-wall");
        const ModuleDef* wm = ontology_.Module(wallNode->value("module", ""));
        json gaps = json::array({"site/main-road"});
        if (ids_.contains("site/cross-road")) gaps.push_back("site/cross-road");
        Emit({{"op", "line"}, {"id", wallId}, {"node", wallId}, {"slot", "wall"}, {"along", "site/inner.edge"}, {"gaps", gaps}, {"closed", true}});
        Emit({{"op", "check"}, {"id", wallId + "/closed"}, {"node", wallId}, {"predicate", "closed"}, {"args", {{"ref", wallId}}}});
        const auto towers = wm ? Range(*wallNode, *wm, "towers") : std::nullopt;
        const int towerCount = towers ? static_cast<int>(std::lround(towers->first)) : 4;
        if (towerCount > 0) {
            Emit({{"op", "place"}, {"id", wallId + "/towers"}, {"node", wallId}, {"in", "site/inner"}, {"slot", "tower"}, {"count", towerCount},
                  {"footprint", {{"w", 2}, {"h", 2}}}, {"position", "corners"}, {"layer", "prop"}, {"essential", false}});
        }
        const auto gatehouses = wm ? Range(*wallNode, *wm, "gatehouses") : std::nullopt;
        Emit({{"op", "place"}, {"id", wallId + "/gatehouse"}, {"node", wallId}, {"in", "site/inner"}, {"slot", "gatehouse"},
              {"count", gatehouses ? static_cast<int>(std::lround(gatehouses->first)) : 1}, {"footprint", {{"w", 3}, {"h", 2}}},
              {"position", "gate"}, {"layer", "prop"}, {"essential", false}});
    }

    // Zones, partitioned by their site preferences.
    double builtNeed = 0;
    json into = json::array();
    std::vector<std::pair<const json*, const ModuleDef*>> zones;
    for (const auto& c : children) {
        const ModuleDef* cm = ontology_.Module(c.value("module", ""));
        if (cm == nullptr || cm->level != PlaceLevel::Zone) continue;
        const auto area = Range(c, *cm, "area");
        json prefers = json::object();
        for (const auto& [criterion, weight] : cm->prefersSite) prefers[criterion] = weight;
        // A zone has room for its parcels: about 1.9 times their lots, with its streets and open land.
        const double needed = ParcelArea(c);
        const double planned = area ? (area->first + area->second) / 2 : 400.0;
        double scale = 1.0;
        if (auto it = options_.zoneScale.find(c.value("id", "")); it != options_.zoneScale.end()) scale = it->second;
        const double need = needed * 1.9 * scale;
        // Without buildings (farmland, a green), a zone takes what the others leave.
        const bool flexible = needed <= 0;
        json zone = {{"id", c.value("id", "")}, {"node", c.value("id", "")}, {"module", cm->id},
                     {"area", static_cast<int>(std::lround(std::max(planned * (flexible ? 1.0 : scale), need)))}, {"prefers", prefers}};
        if (flexible) zone["flexible"] = true;
        else zone["min"] = static_cast<int>(std::lround(need));
        // Where it goes among the others: its relations (near the river, south of the docks).
        json rel = json::array();
        for (const auto& r : c.value("rel", json::array())) {
            if (!r.is_object() || !r.contains("to") || !r["to"].is_string()) continue;
            json one = {{"type", r.value("type", "near")}, {"to", r["to"]}};
            if (r.contains("weight") && r["weight"].is_number()) one["weight"] = r["weight"]; // raised when a check failed
            rel.push_back(one);
        }
        if (!rel.empty()) zone["rel"] = rel;
        if (auto it = options_.keepSeeds.find(c.value("id", "")); it != options_.keepSeeds.end()) {
            zone["seed"] = {it->second.first, it->second.second};
        }
        // Painted by the user (① Confirm's brush): exactly those cells. Else as ① Confirm's
        // layout left it (its kept cells): the plan fills the zone, it doesn't move it.
        if (const json paint = c.value("paint", json()); paint.is_array() && !paint.empty()) zone["paint"] = paint;
        else if (const json kept = c.value("layoutCells", json()); kept.is_array() && !kept.empty()) zone["paint"] = kept;
        // The AI's sketch of it (cells of this site): where it grows, not its border.
        if (const json sketch = c.value("sketchCells", json()); sketch.is_array() && !sketch.empty()) zone["sketch"] = sketch;
        // Drawn by the user (③ Layout's drawing): exactly that land.
        if (const json at = c.value("at", json()); at.is_object()) {
            const int x = at.value("x", 0), y = at.value("y", 0);
            zone["box"] = {x, y, x + std::max(1, at.value("w", 1)), y + std::max(1, at.value("h", 1))};
        }
        builtNeed += need;
        into.push_back(zone);
        zones.push_back({&c, cm});
    }
    if (!into.empty()) {
        Emit({{"op", "partition"}, {"id", "site/zones"}, {"node", id}, {"in", zonesIn}, {"into", into}, {"method", "growth"}});
    }
    const json layout = m ? FieldOf(root, *m, "layout") : json();
    for (size_t i = 0; i < zones.size(); ++i) Zone(*zones[i].first, *zones[i].second, static_cast<int>(i), layout);
    Landscape(id, zonesIn, zones);

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

// The land nothing else covers (development_plan.md 8.3): the wild outside the zones, the
// yards inside them, the verges along the main roads, each with what grows and lies there
// (the landscape module's settings: trees, bushes, flowers, grass, rocks, mounds), so the
// place reads as one landscape. Each is a scatter on the cells its container owns itself.
void Compiler::Landscape(const std::string& root, const std::string& zonesIn, const std::vector<std::pair<const json*, const ModuleDef*>>& zones) {
    const ModuleDef* lm = ontology_.Module("landscape");
    if (lm == nullptr || !lm->compile.is_object()) return;
    const json settings = lm->compile.value("settings", json::object());
    auto scatter = [&](const std::string& in, const std::string& setting, const std::string& tag, const std::string& near) {
        for (const auto& b : settings.value(setting, json::array())) {
            const std::string slotName = b.value("slot", "");
            const SlotDef* slot = nullptr;
            for (const auto& sd : lm->slots) {
                if (sd.name == slotName) slot = &sd;
            }
            const double density = b.value("density", 0.0);
            if (slot == nullptr || density <= 0) continue;
            json st = {{"op", "scatter"}, {"id", in + "/landscape-" + tag + "-" + slotName}, {"node", root}, {"in", in}, {"slot", slotName},
                       {"density", density}, {"own", true}, {"spacing", {{"min", b.value("spacing", 1)}}}, {"layer", LayerForSlot(ontology_, *slot)},
                       {"dressing", "landscape"}};
            if (!near.empty()) {
                st["near"] = near;
                st["radius"] = b.value("radius", 1);
            }
            if (b.contains("cluster")) st["cluster"] = b["cluster"];
            Emit(st);
        }
    };
    scatter("site", "wild", "wild", "");
    if (zonesIn != "site") scatter(zonesIn, "wild", "inner", ""); // inside the walls, between the zones
    for (const auto& [zone, zm] : zones) {
        // A zone that is its own nature (a meadow, the wilds, a forest) or water dresses itself.
        if (ontology_.IsA(zm->id, "NatureArea") || ontology_.IsA(zm->id, "WaterBody")) continue;
        scatter(zone->value("id", ""), "yard", "yard", "");
    }
    int k = 0;
    for (const char* road : {"site/main-road", "site/main-road-2", "site/main-road-3", "site/cross-road"}) {
        if (!ids_.contains(road)) continue;
        scatter("site", "verge", "verge" + std::to_string(++k), road);
    }
}

void Compiler::Zone(const json& zone, const ModuleDef& m, int index, const json& layoutMix) {
    const std::string zoneId = zone.value("id", "");
    const json hints = m.compile.is_object() ? m.compile : json::object();
    if (const json fill = hints.value("fill", json()); fill.is_object()) {
        const json v = FieldOf(zone, m, fill.value("field", ""));
        const json values = fill.value("values", json::object());
        if (v.is_string() && values.contains(v.get<std::string>())) {
            Emit({{"op", "fill"}, {"id", zoneId + "/ground"}, {"node", zoneId}, {"in", zoneId}, {"terrain", values[v.get<std::string>()]}});
        }
    }
    auto background = [&] {
        int k = 0;
        for (const auto& b : hints.value("background", json::array())) {
            const std::string slotName = b.value("slot", "");
            const SlotDef* slot = nullptr;
            for (const auto& sd : m.slots) {
                if (sd.name == slotName) slot = &sd;
            }
            const auto density = HintNumber(b.value("density", json()), zone, m);
            if (slot == nullptr || !density || *density <= 0) continue;
            Emit({{"op", "scatter"}, {"id", zoneId + "/" + slotName + (k++ ? "-" + std::to_string(k) : "")}, {"node", zoneId}, {"in", zoneId},
                  {"slot", slotName}, {"density", *density}, {"own", true}, {"spacing", {{"min", 1}}}, {"layer", LayerForSlot(ontology_, *slot)}});
        }
        return k > 0;
    };
    if (hints.value("placement", "") == "scattered") {
        ScatteredZone(zone, m, hints);
        background();
        for (const auto& c : zone.value("children", json::array())) {
            const ModuleDef* cm = ontology_.Module(c.value("module", ""));
            if (cm == nullptr || cm->level != PlaceLevel::Feature || ontology_.IsA(cm->id, "LinearFeature")) continue;
            const int repeat = std::max(1, c.value("repeat", 1));
            for (int k = 0; k < repeat; ++k) Feature(c, *cm, repeat == 1 ? c.value("id", "") : c.value("id", "") + "#" + std::to_string(k), zoneId, "");
        }
        return;
    }
    if (ontology_.IsA(m.id, "NatureArea") && hints.contains("background")) {
        background();
        return;
    }
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
    std::vector<int> depths;
    for (const auto& c : children) {
        const ModuleDef* cm = ontology_.Module(c.value("module", ""));
        if (cm == nullptr || cm->level != PlaceLevel::Parcel || c.contains("at")) continue;
        const int repeat = std::max(1, c.value("repeat", 1));
        for (int k = 0; k < repeat; ++k) {
            const std::string instance = repeat == 1 ? c.value("id", "") : c.value("id", "") + "#" + std::to_string(k);
            const json lot = LotSize(c, *cm, k, repeat);
            if (lot["h"].is_number()) depths.push_back(lot["h"].get<int>());
            if (lot["h"].is_array()) depths.push_back(lot["h"][1].get<int>());
            into.push_back({{"id", instance}, {"node", c.value("id", "")}, {"module", cm->id}, {"size", lot}});
            parcels.push_back({&c, cm, instance, k});
        }
    }
    // The streets' spacing follows the depth most lots have (not the deepest one: a
    // temple's deep lot would spread a whole ward's streets, and its houses lose their fronts).
    if (!depths.empty()) {
        std::sort(depths.begin(), depths.end());
        depth = std::max(depth, depths[std::min(depths.size() - 1, depths.size() * 4 / 5)]);
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
    const json network = hints.value("network", json::object());
    Emit({{"op", "network"}, {"id", zoneId + "/streets"}, {"node", zoneId}, {"in", zoneId}, {"style", network.value("style", style)},
          {"spacing", std::max(network.value("spacing", 0), 2 * depth + 2)}, {"width", network.value("width", 1)},
          {"class", network.value("class", "street")}, {"terrain", network.value("terrain", "dirt")}});
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
    // Parcels drawn with the module brush: a lot where drawn.
    for (const auto& c : children) {
        const ModuleDef* cm = ontology_.Module(c.value("module", ""));
        const json at = c.value("at", json());
        if (cm == nullptr || cm->level != PlaceLevel::Parcel || !at.is_object()) continue;
        Emit({{"op", "region"}, {"id", c.value("id", "")}, {"node", c.value("id", "")}, {"in", zoneId}, {"shape", "rect"},
              {"size", {{"w", at.value("w", 6)}, {"h", at.value("h", 6)}}}, {"at", {at.value("x", 0), at.value("y", 0)}},
              {"module", cm->id}, {"essential", true}});
        Parcel(c, *cm, c.value("id", ""), 0);
    }
    // The zone's own features (a ward's gardens, a temple ward's graveyard): in
    // its land between the lots.
    for (const auto& c : children) {
        const ModuleDef* cm = ontology_.Module(c.value("module", ""));
        if (cm == nullptr || cm->level != PlaceLevel::Feature || ontology_.IsA(cm->id, "LinearFeature")) continue;
        const int repeat = std::max(1, c.value("repeat", 1));
        for (int k = 0; k < repeat; ++k) {
            const std::string self = repeat == 1 ? c.value("id", "") : c.value("id", "") + "#" + std::to_string(k);
            Feature(c, *cm, self, zoneId, "");
        }
    }
}

void Compiler::Parcel(const json& node, const ModuleDef& m, const std::string& instance, int index) {
    const std::string nodeId = node.value("id", "");
    const int instances = std::max(1, node.value("repeat", 1));
    const json hints = m.compile.is_object() ? m.compile : json::object();
    const SlotDef* primary = hints.value("noPrimary", false) ? nullptr : PrimarySlot(m);
    std::string primaryId;
    const json size = SizeOf(node, m, json{{"w", 5}, {"h", 6}});
    if (const json surface = hints.value("surface", json()); surface.is_string()) {
        Emit({{"op", "fill"}, {"id", instance + "/surface"}, {"node", nodeId}, {"in", instance}, {"terrain", surface}});
    }
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
        // A room's main thing (a dungeon's altar, its stairs) stands in its middle.
        if (hints.value("centre", false) || ontology_.IsA(m.id, "DungeonRoom")) s["position"] = "center";
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
    Contents(hints.value("contents", json()), node, m, instance, instance, primaryId);
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
    const json hints = m.compile.is_object() ? m.compile : json::object();
    if (hints.contains("group")) return Group(node, m, self, parentInstance);
    const SlotDef* primary = hints.value("noPrimary", false) ? nullptr : PrimarySlot(m);
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
    if (!sized && hints.contains("contents")) {
        // What it puts in its room (enemies, loot, lights): there.
        Contents(hints["contents"], node, m, self, parentInstance, parentPrimary);
        return;
    }
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
    json painted = json();
    if (const json at = node.value("at", json()); at.is_object()) {
        painted = json::array({at.value("x", 0), at.value("y", 0)});
        size = {{"w", at.value("w", 3)}, {"h", at.value("h", 3)}};
    }
    if (painted.is_array()) {
        Emit({{"op", "region"}, {"id", self}, {"node", nodeId}, {"in", parentInstance}, {"shape", "rect"}, {"size", size}, {"module", m.id},
              {"at", painted}, {"essential", true}});
    } else
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
    Contents(hints.value("contents", json()), node, m, self, self, primary ? self + "/" + primary->name : "");
    // Anything else: its main object in its middle (a well, a bench, a stable).
    // A ground slot (a yard's path) is its surface, filled above: nothing to place.
    if (primary && LayerForSlot(ontology_, *primary) != "ground") {
        Emit({{"op", "place"}, {"id", self + "/" + primary->name}, {"node", nodeId}, {"in", self}, {"slot", primary->name},
              {"footprint", {{"w", 1}, {"h", 1}}}, {"position", "center"}, {"layer", LayerForSlot(ontology_, *primary)}, {"essential", false}});
    }
}

void Compiler::DungeonRoot(const json& root, const ModuleDef& m) {
    const std::string id = root.value("id", "dungeon");
    struct RoomRef {
        const json* node;
        const ModuleDef* module;
        std::string instance;
        int index;
    };
    struct Level {
        const json* node;
        const ModuleDef* module;
        std::string instance;
        std::vector<RoomRef> rooms;
        double area = 0;
    };
    std::vector<Level> levels;
    for (const auto& c : ChildrenOf(root)) {
        const ModuleDef* lm = ontology_.Module(c.value("module", ""));
        if (lm == nullptr || lm->level != PlaceLevel::Zone) continue;
        const int repeat = std::max(1, c.value("repeat", 1));
        for (int k = 0; k < repeat; ++k) {
            Level level{&c, lm, repeat == 1 ? c.value("id", "") : c.value("id", "") + "#" + std::to_string(k), {}, 0};
            for (const auto& r : ChildrenOf(c)) {
                const ModuleDef* rm = ontology_.Module(r.value("module", ""));
                if (rm == nullptr || rm->level != PlaceLevel::Parcel) continue;
                const int n = std::max(1, r.value("repeat", 1));
                for (int j = 0; j < n; ++j) {
                    std::string instance = n == 1 ? r.value("id", "") : r.value("id", "") + "#" + std::to_string(j);
                    if (repeat > 1) instance += "@" + std::to_string(k); // one level's room, apart from the next's
                    level.rooms.push_back({&r, rm, instance, j});
                    const json size = SizeOf(r, *rm, json{{"w", 5}, {"h", 5}});
                    auto mid = [](const json& v) { return v.is_array() ? (v[0].get<double>() + v[1].get<double>()) / 2 : v.is_number() ? v.get<double>() : 5.0; };
                    level.area += mid(size["w"]) * mid(size["h"]);
                }
            }
            levels.push_back(level);
        }
    }
    // Each level a square big enough for its rooms and the rock and corridors between
    // (more, when a fitting round found it short).
    int side = 24;
    for (const auto& l : levels) {
        double scale = 1.0;
        if (auto it = options_.zoneScale.find(l.node->value("id", "")); it != options_.zoneScale.end()) scale = it->second;
        side = std::max(side, static_cast<int>(std::ceil(std::sqrt(l.area * 3.4 * scale))));
    }
    const int n = std::max<int>(1, static_cast<int>(levels.size()));
    width_ = n * side + 2 * (n - 1) + 2;
    height_ = side + 2;
    side_ = std::max(width_, height_);
    Emit({{"op", "region"}, {"id", "site"}, {"node", id}, {"shape", "rect"}, {"at", {0, 0}}, {"size", {{"w", width_}, {"h", height_}}}});
    Emit({{"op", "fill"}, {"id", "site/rock"}, {"node", id}, {"in", "site"}, {"terrain", "rock-wall"}});
    const std::string theme = Text(FieldOf(root, m, "theme"), "crypt");
    const bool cave = theme == "cave";
    const std::string goalKind = Text(FieldOf(root, m, "goal"), "treasure");
    const bool boss = FieldOf(root, m, "boss").is_boolean() ? FieldOf(root, m, "boss").get<bool>() : false;
    const auto locks = Range(root, m, "locks");
    const auto loops = Range(root, m, "loops");
    const int totalLocks = locks ? static_cast<int>(std::lround((locks->first + locks->second) / 2)) : 0;
    std::string previousGoal;
    std::string goal;
    for (int k = 0; k < static_cast<int>(levels.size()); ++k) {
        const Level& level = levels[k];
        const bool last = k + 1 == static_cast<int>(levels.size());
        auto find = [&](std::initializer_list<const char*> kinds) -> std::string {
            for (const char* kind : kinds) {
                for (const auto& r : level.rooms) {
                    if (r.module->id == kind) return r.instance;
                }
            }
            return {};
        };
        std::string start = find({"entrance-room"});
        if (start.empty() && !level.rooms.empty()) start = level.rooms.front().instance;
        goal = last ? (boss || goalKind == "boss" ? find({"boss-arena", "treasure-room"})
                       : goalKind == "rescue"     ? find({"prison", "treasure-room", "boss-arena"})
                       : goalKind == "escape"     ? find({"stairs-room", "treasure-room"})
                                                  : find({"treasure-room", "boss-arena"}))
                    : find({"stairs-room"});
        if (goal.empty() || goal == start) {
            for (auto it = level.rooms.rbegin(); it != level.rooms.rend(); ++it) {
                if (it->instance != start && !(it->module->compile.is_object() && it->module->compile.value("secret", false))) {
                    goal = it->instance;
                    break;
                }
            }
        }
        const std::string levelId = level.instance;
        const std::string floor = cave ? "cave-floor" : Text(FieldOf(*level.node, *level.module, "floor"), "stone-floor");
        Emit({{"op", "region"}, {"id", levelId}, {"node", level.node->value("id", "")}, {"in", "site"}, {"shape", "rect"},
              {"at", {1 + k * (side + 2), 1}}, {"size", {{"w", side}, {"h", side}}}, {"module", level.module->id}});
        json into = json::array();
        for (const auto& r : level.rooms) {
            const json hints = r.module->compile.is_object() ? r.module->compile : json::object();
            into.push_back({{"id", r.instance}, {"node", r.node->value("id", "")}, {"module", r.module->id},
                            {"size", SizeOf(*r.node, *r.module, json{{"w", 5}, {"h", 5}})}, {"secret", hints.value("secret", false)},
                            {"cave", hints.value("cave", false)}});
        }
        const int levelLocks = totalLocks * (k + 1) / n - totalLocks * k / n;
        json rooms = {{"op", "rooms"}, {"id", levelId + "/rooms"}, {"node", level.node->value("id", "")}, {"in", levelId}, {"into", into},
                      {"style", cave ? "cave" : "rooms"}, {"start", start}, {"goal", goal}, {"locks", levelLocks},
                      {"loops", loops ? static_cast<int>(std::lround(loops->first)) : 1}, {"doorSlot", "door"}, {"lockedSlot", "locked-door"},
                      {"secretSlot", "secret-door"}, {"keySlot", "key"}};
        if (!previousGoal.empty()) rooms["stairsFrom"] = previousGoal;
        Emit(rooms);
        Emit({{"op", "fill"}, {"id", levelId + "/floor"}, {"node", level.node->value("id", "")}, {"in", levelId + "/rooms/corridors"}, {"terrain", floor}});
        for (const auto& r : level.rooms) {
            Emit({{"op", "fill"}, {"id", r.instance + "/floor"}, {"node", r.node->value("id", "")}, {"in", r.instance}, {"terrain", floor}});
            Parcel(*r.node, *r.module, r.instance, r.index);
        }
        Emit({{"op", "check"}, {"id", levelId + "/rooms-placed"}, {"node", level.node->value("id", "")}, {"predicate", "count"},
              {"args", {{"ref", levelId + "/rooms"}, {"range", {static_cast<int>(level.rooms.size()), static_cast<int>(level.rooms.size())}}}}});
        previousGoal = goal;
    }
    Emit({{"op", "check"}, {"id", "site/completable"}, {"node", id}, {"predicate", "completable"}, {"args", {{"goal", goal}}}});
    Emit({{"op", "check"}, {"id", "site/rooms-reachable"}, {"node", id}, {"predicate", "rooms-reachable"}, {"args", {{"class", "Room"}}}});
    Emit({{"op", "check"}, {"id", "site/doors-free"}, {"node", id}, {"predicate", "doors-free"}});
}

void Compiler::InteriorRoot(const json& root, const ModuleDef& m) {
    const std::string id = root.value("id", "interior");
    struct Floor {
        const json* node;
        const ModuleDef* module;
        std::string instance;
        std::string level;
        std::vector<std::tuple<const json*, const ModuleDef*, std::string, int>> rooms;
        double area = 0;
    };
    std::vector<Floor> floors;
    for (const auto& c : ChildrenOf(root)) {
        const ModuleDef* fm = ontology_.Module(c.value("module", ""));
        if (fm == nullptr || fm->level != PlaceLevel::Zone) continue;
        const int repeat = std::max(1, c.value("repeat", 1));
        for (int k = 0; k < repeat; ++k) {
            Floor floor{&c, fm, repeat == 1 ? c.value("id", "") : c.value("id", "") + "#" + std::to_string(k), Text(FieldOf(c, *fm, "level"), "ground"), {}, 0};
            for (const auto& r : ChildrenOf(c)) {
                const ModuleDef* rm = ontology_.Module(r.value("module", ""));
                if (rm == nullptr || rm->level != PlaceLevel::Parcel) continue;
                const int n = std::max(1, r.value("repeat", 1));
                for (int j = 0; j < n; ++j) {
                    std::string instance = n == 1 ? r.value("id", "") : r.value("id", "") + "#" + std::to_string(j);
                    if (repeat > 1) instance += "@" + std::to_string(k);
                    floor.rooms.push_back({&r, rm, instance, j});
                    const json size = SizeOf(r, *rm, json{{"w", 3}, {"h", 3}});
                    auto mid = [](const json& v) { return v.is_array() ? (v[0].get<double>() + v[1].get<double>()) / 2 : v.is_number() ? v.get<double>() : 3.0; };
                    floor.area += mid(size["w"]) * mid(size["h"]);
                }
            }
            floors.push_back(floor);
        }
    }
    // The ground floor first, the floors above, the cellar last.
    std::stable_sort(floors.begin(), floors.end(), [](const Floor& a, const Floor& b) {
        auto rank = [](const std::string& l) { return l == "ground" ? 0 : l == "upper" ? 1 : 2; };
        return rank(a.level) < rank(b.level);
    });
    // The footprint: given, or the busiest floor's rooms with a hall, about 4:3.
    int fw = 0, fh = 0;
    if (root.contains("quantity") && root["quantity"].contains("footprint")) {
        const json size = SizeOf(root, m, json{{"w", 12}, {"h", 9}});
        auto lo = [](const json& v, int f) { return v.is_number() ? v.get<int>() : v.is_array() ? v[0].get<int>() : f; };
        fw = lo(size["w"], 12), fh = lo(size["h"], 9);
    } else {
        double area = 16;
        for (const auto& f : floors) {
            double scale = 1.0;
            if (auto it = options_.zoneScale.find(f.node->value("id", "")); it != options_.zoneScale.end()) scale = it->second;
            area = std::max(area, f.area * (f.rooms.size() >= 3 ? 1.3 : 1.05) * scale);
        }
        fw = std::max(4, static_cast<int>(std::ceil(std::sqrt(area * 4.0 / 3.0))));
        fh = std::max(4, static_cast<int>(std::ceil(area / fw)));
    }
    const int n = std::max<int>(1, static_cast<int>(floors.size()));
    width_ = n * fw + 3 * (n - 1) + 2;
    height_ = fh + 2;
    side_ = std::max(width_, height_);
    Emit({{"op", "region"}, {"id", "site"}, {"node", id}, {"shape", "rect"}, {"at", {0, 0}}, {"size", {{"w", width_}, {"h", height_}}}});
    Emit({{"op", "fill"}, {"id", "site/outside"}, {"node", id}, {"in", "site"}, {"terrain", "void"}});
    const std::string floorSurface = Text(FieldOf(root, m, "floor"), "wood-floor");
    std::string below;
    for (int k = 0; k < static_cast<int>(floors.size()); ++k) {
        const Floor& f = floors[k];
        const std::string floorId = f.instance;
        Emit({{"op", "region"}, {"id", floorId}, {"node", f.node->value("id", "")}, {"in", "site"}, {"shape", "rect"},
              {"at", {1 + k * (fw + 3), 1}}, {"size", {{"w", fw}, {"h", fh}}}, {"module", f.module->id}});
        Emit({{"op", "fill"}, {"id", floorId + "/floor"}, {"node", f.node->value("id", "")}, {"in", floorId}, {"terrain", floorSurface}});
        json into = json::array();
        for (const auto& [node, rm, instance, j] : f.rooms) {
            const json hints = rm->compile.is_object() ? rm->compile : json::object();
            into.push_back({{"id", instance}, {"node", node->value("id", "")}, {"module", rm->id}, {"size", SizeOf(*node, *rm, json{{"w", 3}, {"h", 3}})},
                            {"public", hints.value("public", false)}});
        }
        json plan = {{"op", "floorplan"}, {"id", floorId + "/rooms"}, {"node", f.node->value("id", "")}, {"in", floorId}, {"into", into},
                     {"entrance", k == 0 ? "w" : ""}, {"stairs", n > 1}, {"wallSlot", "wall"}, {"wallNode", id}, {"doorSlot", "door"},
                     {"windowSlot", "window"}, {"stairsSlot", "stairs"}};
        if (!below.empty()) plan["stairsFrom"] = below;
        Emit(plan);
        for (const auto& [node, rm, instance, j] : f.rooms) Parcel(*node, *rm, instance, j);
        Emit({{"op", "check"}, {"id", floorId + "/rooms-placed"}, {"node", f.node->value("id", "")}, {"predicate", "count"},
              {"args", {{"ref", floorId + "/rooms"}, {"range", {static_cast<int>(f.rooms.size()), static_cast<int>(f.rooms.size())}}}}});
        below = floorId + "/rooms";
    }
    Emit({{"op", "check"}, {"id", "site/rooms-reachable"}, {"node", id}, {"predicate", "rooms-reachable"}, {"args", {{"class", "Room"}}}});
    Emit({{"op", "check"}, {"id", "site/groups-reachable"}, {"node", id}, {"predicate", "groups-reachable"}, {"args", {{"anchors", groupAnchors_}}}});
    Emit({{"op", "check"}, {"id", "site/doors-free"}, {"node", id}, {"predicate", "doors-free"}});
}

json Compiler::Run() {
    if (!spec_.contains("root")) return json();
    Root(spec_["root"]);
    return {{"schema", "aige.ir/1"},
            {"place", spec_["root"].value("id", "")},
            {"spec", "spec.json"},
            {"seed", options_.seed},
            {"grid", {{"style", options_.isoGrid ? "isometric" : "topdown"}, {"bounds", {0, 0, width_ ? width_ : side_, height_ ? height_ : side_}}}},
            {"statements", statements_}};
}

} // namespace

json CompileSpec(const Ontology& ontology, const json& spec, const CompileOptions& options) {
    Compiler compiler(ontology, spec, options);
    return compiler.Run();
}

} // namespace aige
