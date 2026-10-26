#include "editing/PlaceInspect.h"

#include "assets/LayoutImage.h"
#include "assets/TextureCook.h"
#include "editing/PlaceBuild.h"
#include "editing/PlaceScene.h"
#include "editing/ScenePreview.h"
#include "places/Ontology.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <fstream>
#include <map>
#include <set>

namespace aige {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

std::string Rel(const PlaceBuild& build, const std::string& file) { return "places/" + build.Id() + "/" + file; }

std::string BuildTypeOf(const PlaceBuild& build) {
    const json& b = build.Brief();
    return b.is_object() && b.value("buildType", json()).is_string() ? b["buildType"].get<std::string>() : "";
}

json Common(const PlaceBuild& build, const std::string& kind) {
    json brief = build.Brief().is_object() ? build.Brief() : json::object();
    brief.erase("components"); // long, and the zones and items say what was made of it
    return {{"kind", kind}, {"brief", brief}, {"story", build.Story().is_object() ? build.Story() : json::object()}, {"look", LookFor(BuildTypeOf(build))}};
}

// Each cell's regions: the zone it's in and the smallest named part (a room, a plot) around it.
struct Where {
    std::map<std::pair<int, int>, std::string> zone, part;
};

Where WhereOf(const json& solved) {
    Where w;
    std::map<std::pair<int, int>, size_t> partSize;
    for (const auto& r : solved.value("regions", json::array())) {
        std::vector<std::pair<int, int>> cells;
        for (const auto& run : r.value("cells", json::array())) {
            if (!run.is_array() || run.size() < 3) continue;
            for (int x = run[1].get<int>(); x < run[2].get<int>(); ++x) cells.push_back({x, run[0].get<int>()});
        }
        const bool zone = r.value("statement", "") == "site/zones";
        const std::string label = r.value("module", "").empty() ? r.value("id", "") : r.value("module", "");
        for (const auto& c : cells) {
            if (zone) {
                w.zone[c] = r.value("id", "");
            } else if (!label.empty() && (!partSize.contains(c) || cells.size() < partSize[c])) {
                w.part[c] = label;
                partSize[c] = cells.size();
            }
        }
    }
    return w;
}

// A module's placement rules, as the ontology defines them ("near the parent's door, 1-4 cells").
json RulesOf(const ModuleDef& m) {
    json out = json::array();
    for (const auto& r : m.relations) {
        std::string text = r.type;
        for (size_t i = 1; i < r.members.size(); ++i) text += " " + r.members[i];
        if (r.args.is_object() && !r.args.empty()) text += " " + r.args.dump();
        if (r.hard) text += " (must)";
        out.push_back(text);
    }
    return out;
}

// What the ontology says of a kind of thing: what it is, how it's drawn, where it belongs, what it's
// designed against, and the set or module it's part of (its description and placement rules).
json KindFacts(const Ontology& ontology, const std::string& slot, const std::string& module) {
    json facts = json::object();
    const std::string role = slot.substr(0, slot.find('|'));
    if (const Role* r = ontology.FindRole(role)) {
        if (!r->label.empty()) facts["is"] = r->label;
        if (!r->draw.empty()) facts["drawn"] = r->draw;
    }
    for (const char* key : {"at", "near", "dependsOn"}) {
        if (const json v = ontology.RoleLook(role, key); !v.is_null()) facts[key == std::string("at") ? "belongs" : key] = v;
    }
    if (const ModuleDef* m = ontology.Module(module)) {
        facts["partOf"] = m->name + ": " + m->description;
        if (const json rules = RulesOf(*m); !rules.empty()) facts["partOfRules"] = rules;
    }
    return facts;
}

// Each Spec node's module, by id (a thing's node names its set or parcel).
std::map<std::string, std::string> ModulesOf(const json& spec) {
    std::map<std::string, std::string> out;
    std::function<void(const json&)> walk = [&](const json& n) {
        if (!n.is_object()) return;
        if (n.contains("id") && n.contains("module")) out[n.value("id", "")] = n.value("module", "");
        for (const auto& c : n.value("children", json::array())) walk(c);
    };
    if (spec.is_object() && spec.contains("root")) walk(spec["root"]);
    return out;
}

// The kinds of thing in [x0,x1)x[y0,y1): how many of each, and where (zones and parts); with the
// ontology, what each kind is and where it belongs (so its placement is judged against that).
json ItemsIn(const json& solved, const Where& where, int x0, int y0, int x1, int y1, bool positions, const Ontology* ontology = nullptr,
             const json& spec = json()) {
    const auto modules = ontology ? ModulesOf(spec) : std::map<std::string, std::string>{};
    std::map<std::string, std::string> moduleOfKind;
    struct Kind {
        int count = 0;
        std::map<std::string, int> in;
        json at = json::array();
    };
    std::map<std::string, Kind> kinds;
    for (const auto& o : solved.value("objects", json::array())) {
        if (!o.contains("x") || !o.contains("y")) continue;
        const int x = o["x"].get<int>(), y = o["y"].get<int>(), w = o.value("w", 1), h = o.value("h", 1);
        const int cx = x + w / 2, cy = y + h / 2;
        if (cx < x0 || cy < y0 || cx >= x1 || cy >= y1) continue;
        std::string kind = o.value("layer", "") == "building" ? "building" : o.value("slot", "object");
        if (kind.empty()) kind = "object";
        if (!o.value("shows", "").empty() && kind != "building") kind += " (" + o.value("shows", "") + ")";
        Kind& k = kinds[kind];
        ++k.count;
        if (ontology && !moduleOfKind.contains(kind)) {
            // The node's module (a dining set), else its nearest ancestor's (the room).
            for (std::string node = o.value("node", ""); !node.empty(); node = node.find('/') == std::string::npos ? "" : node.substr(0, node.rfind('/'))) {
                if (auto m = modules.find(node); m != modules.end()) {
                    moduleOfKind[kind] = m->second;
                    break;
                }
            }
        }
        std::string in;
        if (auto z = where.zone.find({cx, cy}); z != where.zone.end()) in = z->second;
        if (auto p = where.part.find({cx, cy}); p != where.part.end()) in += (in.empty() ? "" : " / ") + p->second;
        ++k.in[in.empty() ? "open ground" : in];
        if (positions && k.at.size() < 8) k.at.push_back({cx - x0, cy - y0});
    }
    json out = json::array();
    for (const auto& [name, k] : kinds) {
        json in = json::object();
        for (const auto& [where, n] : k.in) in[where] = n;
        json row = {{"type", name}, {"count", k.count}, {"in", in}};
        if (positions) row["cells"] = k.at;
        if (ontology && name != "building") {
            if (const json facts = KindFacts(*ontology, name.substr(0, name.find(' ')), moduleOfKind[name]); !facts.empty()) row["ontology"] = facts;
        }
        out.push_back(row);
    }
    return out;
}

} // namespace

json LookFor(const std::string& buildType) {
    static const json types = [] {
        std::ifstream in(EnginePlacesDir() / "buildtypes.json");
        json j = json::parse(in, nullptr, false);
        return j.is_discarded() ? json::array() : j;
    }();
    for (const auto& t : types) {
        if (t.is_object() && t.value("id", "") == buildType) return t.value("look", json::object());
    }
    return json::object();
}

json InspectLayoutRequest(const PlaceBuild& build, const Ontology* ontology) {
    json body = Common(build, "layout");
    // The zones alone, over the ground (the things in them are the plan's check): drawn from the
    // solved layout, whichever step it's at.
    json zones = build.Solved().is_object() ? build.Solved() : json::object();
    zones.erase("objects"); // its walls (lines) stay: an interior's rooms are its walls
    LayoutImageOptions options;
    options.targetSize = 1600;
    std::set<std::string> zoneIds;
    const json& specRoot = build.Spec().is_object() && build.Spec().contains("root") ? build.Spec()["root"] : json::object();
    for (const auto& z : specRoot.value("children", json::array())) zoneIds.insert(z.value("id", ""));
    for (const auto& r : zones.value("regions", json::array())) {
        const std::string id = r.value("id", "");
        if (r.value("statement", "") == "site/zones") options.names[id] = id;
        else if (zoneIds.contains(id)) options.partNames[id] = id;                 // a zone drawn another way (a floor)
        else if (!r.value("module", "").empty() && id.find('/') != std::string::npos) {
            const std::string tail = id.substr(id.rfind('/') + 1);               // a room or plot, by its own name
            if (tail.find('#') == std::string::npos && tail.size() <= 24) options.partNames[id] = tail;
        }
    }
    WritePng(build.Folder() / "inspect-layout.png", RenderLayoutImage(zones, options));
    body["images"] = json::array({Rel(build, "inspect-layout.png")});
    json planned = json::array();
    const json& spec = build.Spec();
    for (const auto& z : spec.is_object() && spec.contains("root") ? spec["root"].value("children", json::array()) : json::array()) {
        json row = {{"id", z.value("id", "")}, {"module", z.value("module", "")}};
        if (const ModuleDef* m = ontology ? ontology->Module(z.value("module", "")) : nullptr) {
            row["is"] = m->name + ": " + m->description;
            if (const json rules = RulesOf(*m); !rules.empty()) row["rules"] = rules; // where the ontology says it goes
        }
        for (const char* key : {"placement", "rationale"}) {
            if (z.value(key, json()).is_string() && !z[key].get<std::string>().empty()) row[key] = z[key];
        }
        row["parts"] = z.value("children", json::array()).size();
        planned.push_back(row);
    }
    body["expected"] = {{"zones", planned}};
    return body;
}

json InspectPlanRequest(const PlaceBuild& build, const Ontology* ontology) {
    json body = Common(build, "plan");
    const json& solved = build.Solved();
    std::map<std::string, std::string> codes;
    LayoutImageOptions options;
    options.codes = true;
    options.codesOut = &codes;
    options.targetSize = 1600;
    for (const auto& r : solved.value("regions", json::array())) {
        if (r.value("statement", "") == "site/zones") options.names[r.value("id", "")] = r.value("id", "");
    }
    WritePng(build.Folder() / "inspect-plan.png", RenderLayoutImage(solved, options));
    body["images"] = json::array({Rel(build, "inspect-plan.png")});
    const json bounds = solved.value("grid", json::object()).value("bounds", json::array({0, 0, 64, 64}));
    json legend = json::object();
    for (const auto& [code, kind] : codes) legend[code] = kind;
    body["expected"] = {{"items", ItemsIn(solved, WhereOf(solved), 0, 0, bounds[2].get<int>(), bounds[3].get<int>(), false, ontology, build.Spec())},
                        {"legend", legend}};
    return body;
}

json InspectBuildRequest(Registry& registry, const Project& project, const IsoParams& iso, const PlaceBuild& build, int originX, int originY,
                         int window, const Ontology* ontology) {
    json body = Common(build, "build");
    const json& solved = build.Solved();
    const json bounds = solved.value("grid", json::object()).value("bounds", json::array({0, 0, 64, 64}));
    const int W = bounds[2].get<int>(), H = bounds[3].get<int>();
    // The areas looked at: each level (its own scene in the game), else the whole place; each cut in
    // windows of about `window` cells, all the same size, one when it's small.
    std::vector<std::tuple<std::string, int, int, int, int>> areas;
    if (ontology) {
        for (const auto& l : PlaceLevelAreas(*ontology, build.Spec(), solved)) areas.push_back({l.id, l.x0, l.y0, l.x1, l.y1});
    }
    if (areas.empty()) {
        const auto [ox0, oy0, ox1, oy1] = OccupiedBounds(solved); // the place, not the empty site round it
        areas.push_back({"", ox0, oy0, ox1, oy1});
    }
    (void)W, (void)H;
    const Where where = WhereOf(solved);
    json images = json::array(), windows = json::array();
    int shot = 0;
    for (const auto& [level, ax0, ay0, ax1, ay1] : areas) {
    const int aw = ax1 - ax0, ah = ay1 - ay0;
    const int nx = std::max(1, static_cast<int>(std::ceil(aw / static_cast<double>(window))));
    const int ny = std::max(1, static_cast<int>(std::ceil(ah / static_cast<double>(window))));
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            const int x0 = ax0 + aw * i / nx, x1 = ax0 + aw * (i + 1) / nx, y0 = ay0 + ah * j / ny, y1 = ay0 + ah * (j + 1) / ny;
            ++shot;
            const std::string file = areas.size() == 1 && nx * ny == 1 ? "inspect-build.png" : "inspect-build-" + std::to_string(shot) + ".png";
            WritePng(build.Folder() / file, RenderScenePreview(registry, project, iso, originX + x0, originY + y0, originX + x1, originY + y1, 1600));
            images.push_back(Rel(build, file));
            std::string label = nx * ny == 1 ? (level.empty() ? "the whole place" : "the whole of " + level)
                                             : std::to_string(j * nx + i + 1) + " of " + std::to_string(nx * ny) + " (column " + std::to_string(i + 1) +
                                                   ", row " + std::to_string(j + 1) + "; cut in " + std::to_string(nx) + " x " + std::to_string(ny) + ")";
            if (!level.empty() && nx * ny > 1) label = level + ", window " + label;
            if (!level.empty()) label += " (a level of the place: its own scene in the game)";
            // What the layout (PLL) put there, with each kind's cells in the window: the vision model
            // names what it sees by them.
            windows.push_back({{"label", label}, {"items", ItemsIn(solved, where, x0, y0, x1, y1, true, ontology, build.Spec())},
                               {"cells", {{"from", {x0, y0}}, {"to", {x1, y1}}, {"note", "cell (0,0) of the window is its top corner; x runs down-right, y down-left"}}}});
        }
    }
    }
    body["images"] = images;
    body["windows"] = windows;
    return body;
}

std::string InspectSummary(const json& answer) {
    if (!answer.is_object() || !answer.value("ok", false)) {
        std::string why;
        for (const auto& e : answer.is_object() ? answer.value("errors", json::array()) : json::array()) why += (why.empty() ? "" : "; ") + e.dump();
        return "failed: " + why;
    }
    int errors = 0, warnings = 0;
    for (const auto& p : answer.value("problems", json::array())) (p.value("severity", "") == "error" ? errors : warnings)++;
    std::string out;
    const json scores = answer.value("scores", json::object()); // kept: items() of a temporary dangles
    for (const auto& [k, v] : scores.items()) {
        char text[32];
        std::snprintf(text, sizeof text, "%s %.1f", k.c_str(), v.is_number() ? v.get<double>() : 0.0);
        out += (out.empty() ? "" : ", ") + std::string(text);
    }
    return out + " | " + std::to_string(errors) + " errors, " + std::to_string(warnings) + " warnings";
}

} // namespace aige
