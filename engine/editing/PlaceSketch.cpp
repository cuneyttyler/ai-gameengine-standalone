#include "editing/PlaceSketch.h"

#include "editing/PlaceBuild.h"
#include "editing/PlaceInspect.h"
#include "editing/PlaceScene.h"
#include "places/Ontology.h"

#include <map>
#include <set>

namespace aige {

using json = nlohmann::json;

json SketchRequests(const PlaceBuild& build, const json& assetPlan, const Ontology* ontology) {
    const json& solved = build.Solved();
    const json brief = build.Brief();
    const json story = build.Story().is_object() ? build.Story() : json::object();
    const json look = LookFor(brief.value("buildType", ""));
    // Every piece the place needs, by its need's key; and each ground (a patch of it cut from the
    // sketch is that terrain's texture: the painter's ground has the painting's colours).
    std::map<std::string, json> needs;
    for (const auto& n : AssetNeedRows(assetPlan)) {
        const std::string key = n.value("key", "");
        if (key.empty()) continue;
        needs[key] = n;
    }
    const json terrains = assetPlan.value("terrains", json::object()); // named: items() of a temporary dangles
    for (const auto& [t, info] : terrains.items()) {
        if (t == "void" || t.find('#') != std::string::npos) continue;
        needs.emplace("terrain|" + t, json{{"key", "terrain|" + t}, {"layer", "ground"}, {"role", t}, {"title", t + " ground"}, {"description", "open ground of " + t}});
    }
    // How many of each there are, and where (to give each level only its own).
    const json byObject = assetPlan.value("byObject", json::object());
    std::vector<std::pair<std::string, std::pair<int, int>>> placed;
    for (const auto& o : solved.value("objects", json::array())) {
        const std::string key = byObject.value(o.value("id", ""), "");
        if (!key.empty()) placed.push_back({key, {o.value("x", 0), o.value("y", 0)}});
    }
    auto itemsIn = [&](int x0, int y0, int x1, int y1, bool all) {
        std::map<std::string, int> count;
        for (const auto& [key, at] : placed) {
            if (all || (at.first >= x0 && at.second >= y0 && at.first < x1 && at.second < y1)) ++count[key];
        }
        json items = json::array();
        for (const auto& [key, n] : needs) {
            const auto c = count.find(key);
            // Walls, torches, fences and the ground: everywhere.
            const bool part = key.rfind("part|", 0) == 0 || key.rfind("piece|", 0) == 0 || key.rfind("terrain|", 0) == 0;
            if (c == count.end() && !part) continue;
            const std::string role = n.value("role", key);
            json item = {{"key", key}, {"type", n.value("title", role)}, {"count", c == count.end() ? n.value("uses", 1) : c->second}};
            if (const Role* r = ontology ? ontology->FindRole(role.substr(0, role.find('|'))) : nullptr; r && !r->draw.empty()) item["drawn"] = r->draw;
            if (!n.value("description", "").empty()) item["about"] = n.value("description", "");
            items.push_back(item);
        }
        return items;
    };
    json zones = json::array();
    const json root = build.Spec().is_object() && build.Spec().contains("root") ? build.Spec()["root"] : json::object();
    for (const auto& z : root.value("children", json::array())) {
        json row = {{"id", z.value("id", "")}};
        if (const ModuleDef* m = ontology ? ontology->Module(z.value("module", "")) : nullptr) row["is"] = m->name + ": " + m->description;
        if (z.value("placement", json()).is_string()) row["where"] = z["placement"];
        zones.push_back(row);
    }
    auto request = [&](const json& items, const std::string& name, const std::string& level) {
        json context = {{"place", {{"name", brief.value("name", "")}, {"type", brief.value("buildType", "")}, {"style", brief.value("style", json::object())}}},
                        {"look", look},
                        {"story", {{"purpose", story.value("purpose", json())}, {"mood", story.value("mood", json())}, {"logline", story.value("logline", json())}}},
                        {"zones", zones},
                        {"items", items},
                        {"light", look.value("lighting", "sun")}};
        if (!level.empty()) context["level"] = level + " (one level of the place: only it is painted)";
        return json{{"context", context}, {"outDir", "places/" + build.Id() + "/sketch"}, {"name", name}};
    };
    json out = json::array();
    const auto levels = ontology ? PlaceLevelAreas(*ontology, build.Spec(), solved) : std::vector<PlaceLevelArea>{};
    if (levels.empty()) {
        out.push_back(request(itemsIn(0, 0, 0, 0, true), "sketch", ""));
    } else {
        for (const auto& l : levels) out.push_back(request(itemsIn(l.x0, l.y0, l.x1, l.y1, false), "sketch-" + l.id, l.id));
    }
    return out;
}

} // namespace aige
