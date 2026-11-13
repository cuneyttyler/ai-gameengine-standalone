// Placements (development_plan.md 34.11, docs/ai/ai-planner.md §11c): where every character and every story prop of the built
// game stands, and where each is as the story goes on. The buildings of the built places are found (a painted place's where
// the vision model saw them, structures.json; an assembled one's where its plan put them), each with its door and, when one
// leads into an interior scene, that interior. Whose home and work each building is, and each character's and prop's spot,
// is decided in one AI call (the rules decide without the AI, and for what it leaves out or gets wrong), kept in
// planner/placements.json (the user's edits locked: Replace keeps them), then ⑪ places everyone by it. Each span of a
// character's whereabouts is a requirement ("whereabouts"): checked against the scenes and the quests' effects, built by a
// stage effect (move_actor) where the rules can, else by the AI panel.
#include "gamebuild/GameBuild.h"

#include "assets/Prefab.h"
#include "assets/Project.h"
#include "assets/SceneIO.h"
#include "gameplay/Behaviour.h"
#include "gameplay/Components.h"
#include "gameplay/Items.h"
#include "gameplay/TileGrid.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "scripting/Components.h"
#include "planner/PlannerDocs.h"
#include "story/StoryOntology.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <deque>
#include <fstream>
#include <map>
#include <optional>
#include <set>

namespace aige {

// GameBuild.cpp's rules, the same ⑪ places by.
std::pair<int, int> PlanDoorOf(const PlannedThing& t);
std::vector<std::string> PlanSlotsFor(const std::string& text);
bool PlanNamesCharacter(const std::string& who, const std::string& id, const std::string& name);
std::optional<std::pair<int, int>> PlanNearestOpen(const TileGrid& grid, int cx, int cy, int radius);
std::optional<std::pair<float, float>> PlanMarkerAt(Registry& registry, const std::string& name);
void PlanAddMarker(Registry& registry, const std::string& name, float x, float y);
std::optional<std::pair<int, int>> PlanOpenMiddle(Registry& registry, const TileGrid& grid);

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string Str(const json& j, const char* key) {
    const auto it = j.is_object() ? j.find(key) : j.end();
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

json ReadJsonFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    // A file not there (yet: the quests before Quests writes them) or unreadable: an empty object, not null.
    const json j = in ? json::parse(in, nullptr, false) : json::object();
    return j.is_discarded() || j.is_null() ? json::object() : j;
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Stem(const std::string& scene) { return fs::path(scene).stem().string(); }

long long Now() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }

const std::set<std::string> kBuildingSlots = {"house", "townhouse", "shop", "workshop", "inn", "temple", "stable"};
const std::set<std::string> kLandmarkSlots = {"centrepiece", "stall", "gateway-gate", "grave", "crop"};

// The words of a text, four letters or more, lowercase.
std::set<std::string> Words(const std::string& text) {
    std::set<std::string> out;
    std::string w;
    for (const unsigned char c : text + " ") {
        if (std::isalpha(c)) {
            w += static_cast<char>(std::tolower(c));
        } else {
            if (w.size() >= 4) out.insert(w);
            w.clear();
        }
    }
    return out;
}

// A spot of the decision as stored: {scene, building?, inside, marker?, near?, note?}; nothing it doesn't say.
json CleanSpot(const json& s, const std::string& scene) {
    json out = {{"scene", scene}, {"inside", false}};
    if (!s.is_object()) return out;
    for (const char* k : {"building", "marker", "near", "note"}) {
        if (!Str(s, k).empty()) out[k] = Str(s, k);
    }
    out["inside"] = s.value("inside", false) && out.contains("building");
    return out;
}

bool SpotSays(const json& s) { return !Str(s, "building").empty() || !Str(s, "marker").empty() || !Str(s, "near").empty(); }

// Which time a whereabouts begins: {"start": true} | {"quest", "stage", "on"} | {"event"}.
json CleanFrom(const json& f) {
    if (!f.is_object() || f.value("start", false)) return {{"start", true}};
    if (!Str(f, "event").empty()) return {{"event", Str(f, "event")}};
    if (!Str(f, "quest").empty()) return {{"quest", Str(f, "quest")}, {"stage", f.value("stage", 0)}, {"on", f.value("on", std::string("enter")) == "complete" ? "complete" : "enter"}};
    return {{"start", true}};
}

std::string FromText(const json& f) {
    if (!f.is_object() || f.empty()) return {};
    if (f.value("start", false)) return "the start";
    if (!Str(f, "event").empty()) return "the event " + Str(f, "event");
    return std::string(f.value("on", std::string("enter")) == "complete" ? "the end of " : "") + Str(f, "quest") + " stage " + std::to_string(f.value("stage", 0));
}

// A whereabouts' begin as one number to order them by (start first; a stage's completion just after it).
long FromKey(const json& f) {
    if (!f.is_object() || f.value("start", false)) return -1;
    if (!Str(f, "event").empty()) return 1L << 30;
    return static_cast<long>(f.value("stage", 0)) * 2 + (f.value("on", std::string("enter")) == "complete" ? 1 : 0);
}

json FindIn(const json& list, const std::string& id) {
    for (const auto& e : list) {
        if (Str(e, "id") == id) return e;
    }
    return json();
}

} // namespace

json ReadPlacements(const fs::path& projectRoot) {
    const json p = ReadJsonFile(projectRoot / "planner" / "placements.json");
    return p.is_object() ? p : json::object();
}

void GameBuild::SavePlacements() const {
    std::ofstream(store_.Dir() / "placements.json", std::ios::binary) << placements_.dump(1);
}

const json* GameBuild::PlacementOf(const std::string& kind, const std::string& id) const {
    const char* list = kind == "prop" ? "props" : "characters";
    if (!placements_.contains(list) || !placements_[list].is_array()) return nullptr;
    for (const auto& e : placements_[list]) {
        if (Str(e, "id") == id) return &e;
    }
    return nullptr;
}

bool GameBuild::PaintedWithoutStructures() {
    for (const auto& row : rows_) {
        if (row.build.empty() || row.status != "built") continue;
        PlaceBuild build(BuildHost(row.place));
        if (build.Open(row.build).empty() && build.FreePaint() && !fs::exists(build.Folder() / "structures.json")) return true;
    }
    return false;
}

// --- The buildings ---------------------------------------------------------------------------------------------------

json GameBuild::PlacementBuildings() {
    const Project project{host_.projectRoot};
    const json settled = ReadJsonFile(store_.Dir() / "gateways.json");
    const json outline = store_.Read("outline").value_or(json::object()).value("places", json::array());
    auto nameOf = [&](const std::string& place) {
        for (const auto& p : outline) {
            if (Str(p, "id") == place && !Str(p, "name").empty()) return Str(p, "name");
        }
        return place.substr(place.rfind('/') + 1);
    };
    json out = json::array();
    std::set<std::string> linked; // the interiors a building leads into
    for (const auto& row : rows_) {
        if ((row.status != "built" && row.status != "placeholder") || row.buildType == "interior" || row.scene.empty()) continue;
        Registry reg;
        if (!LoadScene(reg, project.Resolve(row.scene).string())) continue;
        const TileGrid grid = TileGrid::Build(reg);
        const std::vector<PlannedThing> things = ThingsOf(row);
        // The ways into interiors from this scene: where they were settled (gateways.json), else where their gateway stands.
        std::vector<std::pair<std::string, std::pair<int, int>>> ways;
        for (const auto& r : rows_) {
            if (r.buildType != "interior" || r.scene.empty()) continue;
            const json g = settled.is_object() ? settled.value(row.scene + ">" + r.scene, json()) : json();
            if (g.is_object() && g.contains("cell") && g["cell"].is_array() && g["cell"].size() == 2) {
                ways.push_back({r.scene, {g["cell"][0].get<int>(), g["cell"][1].get<int>()}});
                continue;
            }
            for (auto [e, gw, w] : reg.view<Gateway, WorldTransform2D>().each()) {
                if (gw.scene != r.scene) continue;
                ways.push_back({r.scene, {static_cast<int>(std::floor(w.x)), static_cast<int>(std::floor(w.y))}});
                break;
            }
        }
        std::map<std::string, int> count;
        const size_t first = out.size();
        for (const auto& t : things) {
            const bool building = kBuildingSlots.contains(t.slot);
            if (!building && !kLandmarkSlots.contains(t.slot)) continue;
            int& n = count[t.slot];
            if (!building && n >= 4) continue; // a few of each landmark (the graves, the fields): enough to stand by
            ++n;
            auto [dx, dy] = PlanDoorOf(t);
            if (const auto open = PlanNearestOpen(grid, dx, dy, 4)) dx = open->first, dy = open->second;
            const bool painted = !t.kind.empty();
            const std::string size = painted ? (t.area < 25 ? "small" : t.area < 55 ? "medium" : "large") : (t.area <= 12 ? "small" : t.area <= 36 ? "medium" : "large");
            out.push_back({{"id", Stem(row.scene) + "/" + t.slot + "-" + std::to_string(n)}, {"scene", row.scene}, {"place", row.place},
                           {"kind", t.slot}, {"name", painted ? t.kind : t.slot}, {"ref", t.id}, {"size", size}, {"door", {dx, dy}},
                           {"box", {t.x, t.y, t.x + t.w - 1, t.y + t.h - 1}}, {"building", building}, {"interior", nullptr},
                           {"owner", nullptr}, {"use", nullptr}});
        }
        // Its land areas (a square, a graveyard, a field): where stories happen; their door the open cell in their middle.
        std::map<std::string, int> areas;
        for (const auto& a : AreasOf(row)) {
            const auto mid = PlanNearestOpen(grid, (a.x0 + a.x1) / 2, (a.y0 + a.y1) / 2, 6);
            std::string name = a.module;
            std::replace(name.begin(), name.end(), '-', ' ');
            const int cells = (a.x1 - a.x0 + 1) * (a.y1 - a.y0 + 1);
            out.push_back({{"id", Stem(row.scene) + "/" + a.kind + "-" + std::to_string(++areas[a.kind])}, {"scene", row.scene}, {"place", row.place},
                           {"kind", a.kind}, {"name", name}, {"ref", a.id}, {"module", a.module}, {"area", true}, {"painted", a.painted},
                           {"size", cells <= 36 ? "small" : cells <= 120 ? "medium" : "large"},
                           {"door", mid ? json::array({mid->first, mid->second}) : json::array({(a.x0 + a.x1) / 2, (a.y0 + a.y1) / 2})},
                           {"box", {a.x0, a.y0, a.x1, a.y1}}, {"building", false}, {"interior", nullptr}, {"owner", nullptr}, {"use", nullptr}});
        }
        // Its story anchors (where the story's scenes happen), as they stand in the scene.
        const json desc = store_.Read("places/" + PlanSlug(row.place)).value_or(json::object());
        for (const auto& an : desc.value("storyAnchors", json::array())) {
            if (!an.is_object()) continue;
            const std::string id = an.value("id", "");
            const auto at = PlanMarkerAt(reg, id);
            if (!at) continue;
            const int ax = static_cast<int>(std::floor(at->first)), ay = static_cast<int>(std::floor(at->second));
            const json rel = an.value("relation", json::object());
            std::string about;
            for (const char* k : {"at", "place", "location", "detail"}) {
                if (about.empty() && rel.is_object() && rel.contains(k) && rel[k].is_string()) about = rel[k].get<std::string>();
            }
            out.push_back({{"id", Stem(row.scene) + "/anchor-" + id}, {"scene", row.scene}, {"place", row.place}, {"kind", "anchor"},
                           {"name", about.empty() ? id : id + " (" + about + ")"}, {"marker", id}, {"module", an.value("module", "")},
                           {"size", "small"}, {"door", {ax, ay}}, {"box", {ax, ay, ax, ay}}, {"building", false}, {"anchor", true},
                           {"interior", nullptr}, {"owner", nullptr}, {"use", nullptr}});
        }
        // Each way in: the building whose door is nearest it (within ten cells).
        for (const auto& [scene, cell] : ways) {
            long best = -1, bestD = 0;
            for (size_t i = first; i < out.size(); ++i) {
                if (!out[i].value("building", false) || !out[i]["interior"].is_null()) continue;
                const long dx = out[i]["door"][0].get<int>() - cell.first, dy = out[i]["door"][1].get<int>() - cell.second;
                const long d = dx * dx + dy * dy;
                if (d <= 100 && (best < 0 || d < bestD)) best = static_cast<long>(i), bestD = d;
            }
            if (best < 0) continue;
            const std::string place = RowPlaceOfScene(scene);
            out[best]["interior"] = scene;
            out[best]["interiorPlace"] = place;
            out[best]["door"] = {cell.first, cell.second}; // its door: where its way in is
            out[best]["name"] = out[best].value("name", "") + " (" + nameOf(place) + ")";
            linked.insert(scene);
        }
    }
    // An interior no building outside leads into: itself (its people are inside it all the same).
    for (const auto& r : rows_) {
        if (r.buildType != "interior" || r.scene.empty() || linked.contains(r.scene) || (r.status != "built" && r.status != "placeholder")) continue;
        Registry reg;
        std::pair<int, int> door = {0, 0};
        if (LoadScene(reg, project.Resolve(r.scene).string())) {
            if (const auto a = PlanMarkerAt(reg, "arrive")) door = {static_cast<int>(std::floor(a->first)), static_cast<int>(std::floor(a->second))};
        }
        out.push_back({{"id", "interior/" + Stem(r.scene)}, {"scene", r.scene}, {"place", r.place}, {"kind", "interior"}, {"name", nameOf(r.place)},
                       {"size", "medium"}, {"door", {door.first, door.second}}, {"box", json::array()}, {"building", true}, {"interior", r.scene},
                       {"interiorPlace", r.place}, {"owner", nullptr}, {"use", nullptr}});
    }
    // What each interior is for (its place's purpose and who lives or works there).
    for (auto& b : out) {
        if (Str(b, "interiorPlace").empty()) continue;
        const json d = store_.Read("places/" + PlanSlug(Str(b, "interiorPlace"))).value_or(json::object());
        const json about = d.value("description", json());
        if (about.is_object()) {
            b["purpose"] = Str(about, "purpose");
            b["residents"] = about.value("residents", json::array());
        }
    }
    return out;
}

// --- The decision by rule --------------------------------------------------------------------------------------------

json GameBuild::RulePlacements(const json& buildings) {
    const Project project{host_.projectRoot};
    const StoryGraph g = StoryGraph::Load(StoryGraph::PathIn(host_.projectRoot)).value_or(StoryGraph{});
    const StoryOntology& gso = StoryOntology::Engine();
    auto buildingWithInterior = [&](const std::string& scene) -> json {
        for (const auto& b : buildings) {
            if (Str(b, "interior") == scene) return b;
        }
        return json();
    };
    auto building = [&](const std::string& id) -> json { return FindIn(buildings, id); };
    // The markers of each scene (the story anchors), read once.
    std::map<std::string, std::set<std::string>> markersOf;
    auto hasMarker = [&](const std::string& scene, const std::string& name) {
        auto [it, fresh] = markersOf.try_emplace(scene);
        if (fresh) {
            Registry reg;
            if (LoadScene(reg, project.Resolve(scene).string())) {
                for (auto [e, m] : reg.view<Marker>().each()) it->second.insert(m.name);
            }
        }
        return it->second.contains(name);
    };
    auto anchorsOf = [&](const std::string& place) {
        std::set<std::string> ids;
        for (const auto& a : store_.Read("places/" + PlanSlug(place)).value_or(json::object()).value("storyAnchors", json::array())) {
            if (!Str(a, "id").empty()) ids.insert(Str(a, "id"));
        }
        return ids;
    };
    auto sceneOfPlace = [&](const std::string& place) {
        const Row* r = FindRow(place);
        return r ? r->scene : std::string();
    };
    // A building of one of these kinds in a scene: the least used (a house each while there are enough).
    std::map<std::string, int> used;
    auto pick = [&](const std::string& scene, const std::vector<std::string>& slots, bool own) -> json {
        for (const auto& slot : slots) {
            json best;
            int fewest = 0;
            for (const auto& b : buildings) {
                // A building with an interior is its keeper's (rule 1): not anyone else's home or work.
                if (Str(b, "scene") != scene || Str(b, "kind") != slot || !Str(b, "interior").empty()) continue;
                const int n = used[Str(b, "id")];
                if (best.is_null() || n < fewest) best = b, fewest = n;
            }
            if (!best.is_null() && (!own || fewest == 0)) {
                ++used[Str(best, "id")];
                return best;
            }
        }
        return json();
    };
    auto centreOf = [&](const std::string& scene) -> json {
        for (const auto& b : buildings) {
            if (Str(b, "scene") == scene && Str(b, "kind") == "centrepiece") return b;
        }
        return json();
    };
    json timeline = json::object();
    for (const auto& t : Timeline()) timeline[Str(t, "who")] = t;
    // The planner's quests: their stages' story scenes and the moves they make.
    std::vector<json> designs;
    for (const auto& doc : store_.List("quests")) designs.push_back(store_.Read(doc).value_or(json::object()));
    auto storyCard = [&](const std::string& id) {
        const json s = store_.Read("story/scenes/" + id).value_or(json::object());
        return s.is_object() && s.contains("card") && s["card"].is_object() ? s["card"] : json::object();
    };
    // A spot in a place: inside its interior's building (an interior place), else at its anchor marker, else near.
    auto spotIn = [&](const std::string& place, const std::string& anchor, std::string& scene) -> json {
        const Row* r = FindRow(place);
        if (!r) return json();
        if (r->buildType == "interior") {
            const json b = buildingWithInterior(r->scene);
            scene = r->scene;
            json s = {{"building", b.is_null() ? std::string("interior/") + Stem(r->scene) : Str(b, "id")}, {"inside", true}};
            if (!anchor.empty() && hasMarker(r->scene, anchor)) s["marker"] = anchor;
            return s;
        }
        scene = r->scene;
        if (!anchor.empty() && (hasMarker(r->scene, anchor) || anchorsOf(place).contains(anchor))) return {{"marker", anchor}};
        return json();
    };
    // How many the cast timeline starts at each spot: a crowd (three or more at the square) is where the story gathers
    // them, not where each lives; they're met at home or work, and gather there when the story scene that has them plays.
    std::map<std::string, int> crowd;
    for (const auto& [who, t] : timeline.items()) {
        const json st = t.value("start", json::object());
        if (!Str(st, "spot").empty()) ++crowd[Str(st, "place") + "|" + PlanSlug(Str(st, "spot"))];
    }
    // The first quest stage whose story scene has them in this place: {quest, stage, scene}.
    auto gatheringOf = [&](const std::string& who, const std::string& place) -> json {
        for (const auto& q : designs) {
            for (const auto& st : q.value("stages", json::array())) {
                const json card = storyCard(Str(st, "advancesScene"));
                if (Str(card, "location") != place) continue;
                for (const auto& p : card.value("participants", json::array())) {
                    if (Str(p, "who") == who) return {{"quest", Str(q, "id")}, {"stage", st.value("index", 0)}, {"scene", Str(st, "advancesScene")}};
                }
            }
        }
        return json();
    };
    json characters = json::array();
    for (const auto* n : g.OfClass("Character", gso)) {
        if (n->cls == "PlayerCharacter") continue;
        const std::string id = n->id, slug = PlanSlug(id);
        const json c = store_.Read("characters/" + slug).value_or(json::object());
        const std::string name = Str(c, "name").empty() ? (n->Text("name").empty() ? slug : n->Text("name")) : Str(c, "name");
        const json gameplay = c.value("gameplay", json::object());
        std::string roles;
        bool hostile = false;
        for (const auto& r : gameplay.value("roles", json::array())) {
            const std::string role = r.is_string() ? r.get<std::string>() : std::string();
            roles += role + " ";
            hostile = hostile || role == "boss" || role == "enemy";
        }
        for (const auto* e : g.Out(id, "fills")) {
            const StoryNode* role = g.Find(e->o);
            const std::string r = role ? role->Text("role") : "";
            hostile = hostile || r == "boss" || r == "enemy" || r == "elite";
        }
        const std::string words = Str(gameplay, "class") + " " + roles + " " + Str(c, "line");
        json spot, home = nullptr, work = nullptr;
        std::string scene, why;
        // 1. A shop, workshop, inn or temple whose interior lists them (its residents, who stands in it): they work there,
        // met inside (not a crypt they only visit in the story).
        for (const auto& row : rows_) {
            if (row.buildType != "interior" || row.scene.empty() || !spot.is_null()) continue;
            const json d = store_.Read("places/" + PlanSlug(row.place)).value_or(json::object());
            const json about = d.value("description", json());
            static const std::set<std::string> kept = {"shop", "workshop", "inn", "temple", "stable"};
            const std::vector<std::string> kinds = PlanSlotsFor(about.is_object() ? Str(about, "purpose") : std::string());
            if (kinds.empty() || !kept.contains(kinds.front())) continue;
            std::string anchor;
            bool there = false;
            for (const auto& a : d.value("actors", json::array())) {
                if (a.is_object() && PlanNamesCharacter(Str(a, "character"), id, name)) there = true, anchor = Str(a, "anchor");
            }
            for (const auto& r : about.is_object() ? about.value("residents", json::array()) : json::array()) {
                there = there || (r.is_string() && PlanNamesCharacter(r.get<std::string>(), id, name));
            }
            if (!there) continue;
            spot = spotIn(row.place, anchor, scene);
            if (spot.is_null()) continue;
            work = spot.value("building", json());
            why = "they keep " + (about.is_object() && !Str(about, "purpose").empty() ? std::string("the place: ") + Str(about, "purpose") : row.place) +
                  "; its interior is drawn, so they're met inside it";
        }
        // 2. Where the cast timeline starts them.
        const json tl = timeline.value(id, json::object());
        const json start = tl.value("start", json::object());
        json gather; // {quest, stage, scene, marker}: where the story gathers them, from then
        if (spot.is_null() && !Str(start, "place").empty()) {
            spot = spotIn(Str(start, "place"), PlanSlug(Str(start, "spot")), scene);
            const int many = crowd[Str(start, "place") + "|" + PlanSlug(Str(start, "spot"))];
            if (!spot.is_null() && !hostile && many >= 3 && !Str(spot, "marker").empty() && !spot.value("inside", false)) {
                // Where the story gathers them (a whereabouts from its stage); with no scene that does, the crowd is ambient:
                // they're met at home or work all the same.
                gather = gatheringOf(id, Str(start, "place"));
                if (gather.is_null()) gather = {{"ambient", true}};
                gather["marker"] = Str(spot, "marker");
                gather["many"] = many;
                spot = json();
            }
            if (!spot.is_null()) {
                if (spot.value("inside", false)) work = spot.value("building", json());
                why = "the cast timeline starts them " + (Str(start, "spot").empty() ? std::string("in ") + Str(start, "place") : "at " + Str(start, "spot")) +
                      (Str(start, "does").empty() ? std::string() : ": " + Str(start, "does"));
            } else if (!sceneOfPlace(Str(start, "place")).empty()) {
                scene = sceneOfPlace(Str(start, "place"));
            }
        }
        // 3. The anchor a place's description stands them at.
        if (spot.is_null()) {
            for (const auto& row : rows_) {
                if (!spot.is_null()) break;
                for (const auto& a : store_.Read("places/" + PlanSlug(row.place)).value_or(json::object()).value("actors", json::array())) {
                    if (!a.is_object() || !PlanNamesCharacter(Str(a, "character"), id, name) || Str(a, "anchor").empty()) continue;
                    if (!gather.is_null() && Str(a, "anchor") == Str(gather, "marker")) continue; // the crowd's spot: the story's, not theirs
                    // Their own place first: where they live, else the timeline's.
                    std::string at;
                    json s = spotIn(row.place, Str(a, "anchor"), at);
                    if (s.is_null() || (!scene.empty() && at != scene)) continue;
                    spot = s, scene = at;
                    why = row.place + " stands them at " + Str(a, "anchor");
                    break;
                }
            }
        }
        // Their place: the scene they live in (their home's place, the place a home of theirs is part of).
        if (scene.empty()) {
            std::vector<std::string> refs = {Str(c, "home")};
            for (const auto& p : c.value("behaviour", json::object()).value("packages", json::array())) refs.push_back(Str(p, "place"));
            for (const auto& ref : refs) {
                if (!scene.empty() || ref.empty()) continue;
                if (FindRow(ref)) {
                    scene = sceneOfPlace(ref);
                    continue;
                }
                for (const auto& row : rows_) {
                    if (scene.empty() && PlanSlug(ref).rfind(PlanSlug(row.place) + "-", 0) == 0) scene = row.scene;
                }
            }
        }
        // Where the story first has them (and, with no document of their own, the part of it the scene needs).
        std::string seenAt;
        for (const auto& s : g.SceneOrder()) {
            const auto who = g.Participants(s);
            if (!seenAt.empty() || std::find(who.begin(), who.end(), id) == who.end() || sceneOfPlace(g.LocationOfScene(s)).empty()) continue;
            seenAt = s;
            if (scene.empty()) scene = sceneOfPlace(g.LocationOfScene(s));
        }
        if (c.empty() && spot.is_null() && !seenAt.empty()) {
            for (const auto& p : storyCard(seenAt).value("needsParts", json::array())) {
                if (spot.is_null() && p.is_string() && hasMarker(scene, PlanSlug(p.get<std::string>()))) {
                    spot = {{"marker", PlanSlug(p.get<std::string>())}};
                    why = "the story first has them in " + seenAt + ", at " + PlanSlug(p.get<std::string>());
                }
            }
        }
        const bool undocumented = c.empty();
        if (scene.empty() && !rows_.empty()) scene = rows_.front().scene;
        // Their work and home buildings by what they are (a smith's workshop, a baker's shop; a house each).
        const std::vector<std::string> slots = PlanSlotsFor(words);
        const bool settled = std::any_of(rows_.begin(), rows_.end(), [&](const Row& r) {
            return r.scene == scene && (r.buildType == "settlement" || r.buildType == "world" || r.buildType == "farmland" || r.buildType == "harbour" || r.buildType == "castle");
        });
        if (work.is_null() && !hostile) {
            std::vector<std::string> trades;
            for (const auto& s : slots) {
                if (kBuildingSlots.contains(s) && s != "house" && s != "townhouse") trades.push_back(s);
            }
            if (const json b = pick(scene, trades, false); !b.is_null()) work = Str(b, "id");
        }
        if (!hostile && !undocumented && settled && Str(start, "behaviour") != "hidden") {
            if (const json b = pick(scene, {"house", "townhouse"}, true); !b.is_null()) home = Str(b, "id");
        }
        // 4. Where their kind of work is outdoors (the gate, the graves, the fields, the square), or their work's or home's door.
        if (spot.is_null()) {
            std::vector<std::string> outdoors;
            for (const auto& s : slots) {
                if (kLandmarkSlots.contains(s)) outdoors.push_back(s);
            }
            if (const json b = pick(scene, outdoors, false); !b.is_null()) {
                spot = {{"near", Str(b, "id")}, {"note", "by the " + Str(b, "name")}};
                why = name + " is " + (Str(gameplay, "class").empty() ? std::string("one") : "a " + Str(gameplay, "class")) + ": their place is by the " + Str(b, "name");
            } else if (!work.is_null()) {
                const json b = building(work.get<std::string>());
                spot = {{"building", Str(b, "id")}, {"inside", !Str(b, "interior").empty()}};
                if (!Str(b, "interior").empty()) scene = Str(b, "interior");
                why = "they work at the " + Str(b, "name") + (Str(b, "interior").empty() ? std::string(" (no interior drawn: at its door)") : std::string(", inside"));
            } else if (!home.is_null()) {
                const json b = building(home.get<std::string>());
                spot = {{"building", Str(b, "id")}, {"inside", !Str(b, "interior").empty()}};
                if (!Str(b, "interior").empty()) scene = Str(b, "interior");
                why = "nothing says where they work: at their home, the " + Str(b, "name");
            }
        }
        // 5. Nothing known: the middle of their place (by its centrepiece when it has one), never its way in.
        if (spot.is_null()) {
            const json b = centreOf(scene);
            spot = b.is_null() ? json{{"note", "the middle of the place"}} : json{{"near", Str(b, "id")}, {"note", "in the square"}};
            why = "nothing in the story, the character or the places says where " + name + " stands: " +
                  (b.is_null() ? std::string("the middle of the place") : "by the " + Str(b, "name")) + ", away from its way in";
        }
        if (spot.value("inside", false)) {
            const json b = building(Str(spot, "building"));
            if (!Str(b, "interior").empty()) scene = Str(b, "interior");
        } else if (!Str(spot, "building").empty()) {
            scene = Str(building(Str(spot, "building")), "scene");
        }
        if (!gather.is_null() && gather.value("ambient", false)) {
            why += "; the cast timeline has " + std::to_string(gather.value("many", 0)) + " people waiting at " + Str(gather, "marker") +
                   " and no story scene gathers them there: they're met here instead";
        } else if (!gather.is_null()) {
            why += "; the cast timeline has " + std::to_string(gather.value("many", 0)) + " people at " + Str(gather, "marker") + ": they gather there when " +
                   Str(gather, "scene") + " plays (" + Str(gather, "quest") + " stage " + std::to_string(gather.value("stage", 0)) + ")";
        }
        json entry = {{"id", id}, {"name", name}, {"scene", scene}, {"home", home}, {"work", work}, {"spot", CleanSpot(spot, scene)},
                      {"why", why}, {"by", "rule"}, {"locked", false}, {"hostile", hostile}};
        // Whereabouts: the start, then each move of the cast timeline, then the quests' own moves it doesn't make.
        json where = json::array({{{"from", {{"start", true}}}, {"scene", scene}, {"spot", entry["spot"]}, {"why", "where they start: " + why}}});
        std::set<std::pair<std::string, int>> moved;
        if (!gather.is_null() && !gather.value("ambient", false)) {
            const std::string at = sceneOfPlace(Str(start, "place"));
            where.push_back({{"from", {{"quest", Str(gather, "quest")}, {"stage", gather.value("stage", 0)}, {"on", "enter"}}}, {"scene", at},
                             {"spot", CleanSpot({{"marker", Str(gather, "marker")}}, at)},
                             {"why", Str(gather, "scene") + " gathers them at " + Str(gather, "marker") + " (the cast timeline's start)"}});
            moved.insert({Str(gather, "quest"), gather.value("stage", 0)});
        }
        for (const auto& ch : tl.value("changes", json::array())) {
            if (Str(ch, "place").empty() || !FindRow(Str(ch, "place"))) continue;
            std::string at;
            json s = spotIn(Str(ch, "place"), PlanSlug(Str(ch, "spot")), at);
            if (s.is_null() && Str(ch, "place") == Str(start, "place")) s = entry["spot"], at = scene;
            if (s.is_null()) s = {{"note", Str(ch, "spot").empty() ? std::string("where the story has them") : Str(ch, "spot")}};
            const json from = {{"quest", Str(ch, "quest")}, {"stage", ch.value("stage", 0)}, {"on", ch.value("on", std::string("enter"))}};
            moved.insert({Str(ch, "quest"), ch.value("stage", 0)});
            where.push_back({{"from", from}, {"scene", at}, {"spot", CleanSpot(s, at)}, {"why", Str(ch, "why").empty() ? Str(ch, "does") : Str(ch, "why")}});
        }
        for (const auto& q : designs) {
            for (const auto& st : q.value("stages", json::array())) {
                for (const auto& e : st.value("onEnter", json::array())) {
                    if (Str(e, "op") != "move_actor" || Str(e, "who") != id || moved.contains({Str(q, "id"), st.value("index", 0)})) continue;
                    const std::string to = Str(e, "to").empty() ? Str(e, "place") : Str(e, "to");
                    if (!FindRow(to)) continue;
                    std::string anchor = Str(e, "anchor");
                    // "arrive" is where one comes in: the story scene's own spot instead (what it needs), when it has one.
                    if (anchor.empty() || anchor == "arrive") {
                        anchor.clear();
                        for (const auto& p : storyCard(Str(st, "advancesScene")).value("needsParts", json::array())) {
                            if (anchor.empty() && p.is_string()) anchor = PlanSlug(p.get<std::string>());
                        }
                    }
                    std::string at;
                    json s = spotIn(to, anchor, at);
                    if (s.is_null()) {
                        const json b = centreOf(sceneOfPlace(to));
                        at = sceneOfPlace(to);
                        s = b.is_null() ? json{{"note", "where the story scene happens"}} : json{{"near", Str(b, "id")}, {"note", "where the story scene happens"}};
                    }
                    where.push_back({{"from", {{"quest", Str(q, "id")}, {"stage", st.value("index", 0)}, {"on", "enter"}}}, {"scene", at}, {"spot", CleanSpot(s, at)},
                                     {"why", Str(q, "id") + " stage " + std::to_string(st.value("index", 0)) + " moves them" +
                                                 (Str(st, "advancesScene").empty() ? std::string() : " for " + Str(st, "advancesScene"))}});
                }
            }
        }
        entry["whereabouts"] = where;
        characters.push_back(entry);
    }
    // The props: each quest role that's a thing (an item, a container, a spot used), where the story has it.
    json props = json::array();
    for (const auto& q : designs) {
        const std::string quest = Str(q, "id");
        for (const auto& a : q.value("aliases", json::array())) {
            const std::string kind = Str(a, "kind"), alias = Str(a, "id");
            if (alias.empty() || kind == "character" || kind == "location") continue;
            std::string place = Str(a, "in");
            int usedAt = -1, takenAt = -1;
            for (const auto& st : q.value("stages", json::array())) {
                for (const auto& o : st.value("objectives", json::array())) {
                    if (Str(o, "target") != alias) continue;
                    if (usedAt < 0) {
                        usedAt = st.value("index", 0);
                        if (place.empty()) place = Str(storyCard(Str(st, "advancesScene")), "location");
                    }
                    if ((Str(o, "action") == "collect" || Str(o, "action") == "gather") && takenAt < 0) takenAt = st.value("index", 0);
                }
            }
            if (!FindRow(place)) {
                const std::string giver = q.value("giver", json::object()).value("alias", "");
                for (const auto& x : q.value("aliases", json::array())) {
                    if (Str(x, "id") == giver && !Str(x, "fill").empty()) {
                        for (const auto& c : characters) {
                            if (Str(c, "id") == Str(x, "fill")) place = RowPlaceOfScene(Str(c, "scene"));
                        }
                    }
                }
            }
            if (!FindRow(place)) continue;
            const json where = a.value("where", json::object());
            const std::string anchor = where.is_object() && !Str(where, "anchor").empty() ? Str(where, "anchor") : alias;
            std::string name = Str(a, "query");
            if (name.empty()) {
                name = alias;
                std::replace(name.begin(), name.end(), '-', ' ');
            }
            std::string scene, why;
            json s = spotIn(place, anchor, scene);
            if (!s.is_null() && Str(s, "marker").empty()) {
                // Inside: the anchor there its words name ("altar-bindings": the crypt-altar).
                const auto want = Words(name + " " + alias);
                size_t bestN = 0;
                for (const auto& id : anchorsOf(place)) {
                    size_t n = 0;
                    for (const auto& w : Words(id)) n += want.contains(w);
                    if (n > bestN && hasMarker(scene, id)) s["marker"] = id, bestN = n;
                }
            }
            if (!s.is_null()) {
                why = Str(s, "marker").empty() ? "inside " + place : "the story's anchor " + Str(s, "marker") + " in " + place;
            } else {
                scene = sceneOfPlace(place);
                // Where its words say it is (a seal in a crypt's grave, a rope by the gate), else an anchor its words name.
                std::vector<std::string> outdoors;
                for (const auto& sl : PlanSlotsFor(name + " " + alias)) outdoors.push_back(sl);
                if (const json b = pick(scene, outdoors, false); !b.is_null()) {
                    s = {{"near", Str(b, "id")}, {"note", name}};
                    why = "its words put it by the " + Str(b, "name");
                } else {
                    const auto want = Words(name + " " + alias);
                    std::string best;
                    size_t bestN = 0;
                    for (const auto& id : anchorsOf(place)) {
                        size_t n = 0;
                        for (const auto& w : Words(id)) n += want.contains(w);
                        if (n > bestN) best = id, bestN = n;
                    }
                    if (!best.empty()) {
                        s = {{"marker", best}};
                        why = "the anchor " + best + " is what it names";
                    } else {
                        const json b = centreOf(scene);
                        s = b.is_null() ? json{{"note", "the middle of the place"}} : json{{"near", Str(b, "id")}, {"note", name}};
                        why = "nothing says where in " + place + " it is: its middle";
                    }
                }
            }
            if (s.value("inside", false)) {
                const json b = building(Str(s, "building"));
                if (!Str(b, "interior").empty()) scene = Str(b, "interior");
            }
            json entry = {{"id", quest + "/" + alias}, {"name", name}, {"kind", kind}, {"quest", quest}, {"scene", scene}, {"spot", CleanSpot(s, scene)},
                          {"why", why}, {"by", "rule"}, {"locked", false}};
            if (kind == "item") entry["item"] = PlanSlug(Str(a, "fill").empty() ? alias : Str(a, "fill"));
            json whereabouts = json::array({{{"from", {{"start", true}}}, {"scene", scene}, {"spot", entry["spot"]}, {"why", why}}});
            if (takenAt >= 0) {
                whereabouts.push_back({{"from", {{"quest", quest}, {"stage", takenAt}, {"on", "complete"}}}, {"holder", "player"},
                                       {"why", "the player picks it up in " + quest + " stage " + std::to_string(takenAt)}});
            }
            entry["whereabouts"] = whereabouts;
            props.push_back(entry);
        }
    }
    // The props the requirements name (drawn and placed: a record board, a hidden stone box), where their scene's story has them.
    for (const auto& r : reqs_.value("items", json::array())) {
        if (Str(r, "kind") != "asset") continue;
        const json a = r.value("asset", json::object());
        if (Str(a, "type") != "prop" || !a.value("separate", true) || Str(a, "name").empty()) continue;
        std::string place;
        for (const auto& p : a.value("places", json::array())) {
            if (place.empty() && p.is_string() && FindRow(p.get<std::string>())) place = p.get<std::string>();
        }
        if (place.empty()) continue;
        const std::string name = Str(a, "name"), scene = sceneOfPlace(place);
        json s, card = storyCard(Str(r, "scene"));
        std::string why;
        std::vector<std::string> outdoors = PlanSlotsFor(name + " " + Str(a, "look"));
        if (hasMarker(scene, PlanSlug(name))) {
            s = {{"marker", PlanSlug(name)}};
            why = "the story's anchor " + PlanSlug(name) + " is it";
        } else if (const json b = pick(scene, outdoors, false); !b.is_null()) {
            s = {{"near", Str(b, "id")}, {"note", name}};
            why = "its words put it by the " + Str(b, "name");
        }
        for (const auto& p : card.value("needsParts", json::array())) {
            if (s.is_null() && p.is_string() && hasMarker(scene, PlanSlug(p.get<std::string>()))) {
                s = {{"near", PlanSlug(p.get<std::string>())}, {"note", name}};
                why = "where its story scene " + Str(r, "scene") + " happens";
            }
        }
        if (s.is_null()) {
            const json b = centreOf(scene);
            s = b.is_null() ? json{{"note", "the middle of the place"}} : json{{"near", Str(b, "id")}, {"note", name}};
            why = "nothing says where in " + place + " it stands: its middle";
        }
        props.push_back({{"id", "asset:" + PlanSlug(name)}, {"name", name}, {"kind", "prop"}, {"prefab", Str(a, "prefab")}, {"scene", scene},
                         {"spot", CleanSpot(s, scene)}, {"why", why}, {"by", "rule"}, {"locked", false},
                         {"whereabouts", json::array({{{"from", {{"start", true}}}, {"scene", scene}, {"spot", CleanSpot(s, scene)}, {"why", why}}})}});
    }
    return {{"characters", characters}, {"props", props}};
}

// --- What the AI decides from ----------------------------------------------------------------------------------------

json GameBuild::PlacementInput(const json& buildings) {
    const Project project{host_.projectRoot};
    const StoryGraph g = StoryGraph::Load(StoryGraph::PathIn(host_.projectRoot)).value_or(StoryGraph{});
    json scenes = json::array(), places = json::array();
    std::set<std::string> seen;
    auto addScene = [&](const std::string& scene, const std::string& place, const std::string& kind) {
        if (scene.empty() || !seen.insert(scene).second) return;
        Registry reg;
        json markers = json::array();
        if (LoadScene(reg, project.Resolve(scene).string())) {
            for (auto [e, m] : reg.view<Marker>().each()) {
                const std::string& n = m.name;
                if (n == "arrive" || n.rfind("from-", 0) == 0 || n.rfind("home-", 0) == 0 || n.rfind("start-", 0) == 0 || n.rfind("work-", 0) == 0 || n.find('/') != std::string::npos) continue;
                markers.push_back(n);
            }
        }
        scenes.push_back({{"scene", scene}, {"place", place}, {"kind", kind}, {"markers", markers}});
    };
    for (const auto& r : rows_) {
        if (r.status != "built" && r.status != "placeholder") continue;
        addScene(r.scene, r.place, r.buildType);
        const json d = store_.Read("places/" + PlanSlug(r.place)).value_or(json::object());
        const json about = d.value("description", json());
        json anchors = json::array();
        for (const auto& a : d.value("storyAnchors", json::array())) {
            const json rel = a.value("relation", json::object());
            anchors.push_back({{"id", Str(a, "id")}, {"module", Str(a, "module")}, {"about", rel.is_object() ? rel.dump() : std::string()}});
        }
        places.push_back({{"id", r.place}, {"scene", r.scene}, {"kind", r.buildType}, {"purpose", about.is_object() ? Str(about, "purpose") : std::string()},
                          {"residents", about.is_object() ? about.value("residents", json::array()) : json::array()},
                          {"actors", d.value("actors", json::array())}, {"anchors", anchors}});
    }
    json timeline = json::object();
    for (const auto& t : Timeline()) timeline[Str(t, "who")] = t;
    json characters = json::array();
    for (const auto* n : g.OfClass("Character", StoryOntology::Engine())) {
        if (n->cls == "PlayerCharacter") continue;
        const json c = store_.Read("characters/" + PlanSlug(n->id)).value_or(json::object());
        const json gameplay = c.value("gameplay", json::object());
        json packages = json::array();
        for (const auto& p : c.value("behaviour", json::object()).value("packages", json::array())) packages.push_back({{"template", Str(p, "template")}, {"place", Str(p, "place")}});
        std::string bio = c.contains("biography") ? (c["biography"].is_string() ? c["biography"].get<std::string>() : c["biography"].dump()) : std::string();
        if (bio.size() > 400) bio = Utf8Cut(bio, 400) + "…";
        const json tl = timeline.value(n->id, json::object());
        characters.push_back({{"id", n->id}, {"name", Str(c, "name").empty() ? n->Text("name") : Str(c, "name")}, {"line", Str(c, "line")},
                              {"class", Str(gameplay, "class")}, {"roles", gameplay.value("roles", json::array())}, {"home", Str(c, "home")},
                              {"packages", packages}, {"biography", bio}, {"routine", c.value("routine", json())},
                              {"timeline", {{"start", tl.value("start", json::object())}, {"changes", tl.value("changes", json::array())}}}});
    }
    json quests = json::array();
    for (const auto& doc : store_.List("quests")) {
        const json q = store_.Read(doc).value_or(json::object());
        json stages = json::array();
        for (const auto& s : q.value("stages", json::array())) {
            json objectives = json::array();
            for (const auto& o : s.value("objectives", json::array())) objectives.push_back(Str(o, "action") + " " + Str(o, "target"));
            stages.push_back({{"index", s.value("index", 0)}, {"journal", Str(s, "journal")}, {"objectives", objectives}, {"onEnter", s.value("onEnter", json::array())},
                              {"advancesScene", Str(s, "advancesScene")}});
        }
        quests.push_back({{"id", Str(q, "id")}, {"stages", stages}});
    }
    json story = json::array();
    for (const auto& doc : store_.List("story/scenes")) {
        const json s = store_.Read(doc).value_or(json::object());
        const json card = s.value("card", json::object());
        json who = json::array();
        for (const auto& p : card.value("participants", json::array())) who.push_back(Str(p, "who"));
        story.push_back({{"id", doc.substr(doc.rfind('/') + 1)}, {"location", Str(card, "location")}, {"goal", Str(card, "goal")}, {"participants", who},
                         {"needsParts", card.value("needsParts", json::array())}});
    }
    // The draft (the rules' decision) and the props to place.
    const json rule = RulePlacements(buildings);
    json draft = json::array(), props = json::array();
    for (const auto& c : rule.value("characters", json::array())) {
        draft.push_back({{"id", Str(c, "id")}, {"home", c.value("home", json())}, {"work", c.value("work", json())}, {"scene", Str(c, "scene")}, {"spot", c.value("spot", json())}});
    }
    for (const auto& p : rule.value("props", json::array())) {
        props.push_back({{"id", Str(p, "id")}, {"name", Str(p, "name")}, {"kind", Str(p, "kind")}, {"quest", Str(p, "quest")}, {"draft", {{"scene", Str(p, "scene")}, {"spot", p.value("spot", json())}}}});
    }
    json locked = json::array();
    const json old = ReadPlacements(host_.projectRoot);
    for (const char* list : {"characters", "props"}) {
        for (const auto& e : old.value(list, json::array())) {
            if (e.value("locked", false)) locked.push_back({{"id", Str(e, "id")}, {"scene", Str(e, "scene")}, {"spot", e.value("spot", json())}});
        }
    }
    json compact = json::array();
    for (const auto& b : buildings) {
        json one = {{"id", Str(b, "id")}, {"scene", Str(b, "scene")}, {"kind", Str(b, "kind")}, {"name", Str(b, "name")}, {"size", Str(b, "size")},
                    {"door", b.value("door", json())}, {"interior", b.value("interior", json())}};
        if (!Str(b, "purpose").empty()) one["purpose"] = Str(b, "purpose");
        if (!Str(b, "marker").empty()) one["marker"] = Str(b, "marker"); // a story anchor: its spot is that marker
        if (b.value("area", false)) one["area"] = true, one["box"] = b.value("box", json());
        compact.push_back(one);
    }
    return {{"label", "Where everyone stands (placements)"}, {"buildings", compact}, {"scenes", scenes}, {"places", places}, {"characters", characters},
            {"props", props}, {"quests", quests}, {"story", story}, {"draft", draft}, {"locked", locked}};
}

// --- The decision merged and checked ----------------------------------------------------------------------------------

json GameBuild::MergePlacements(const json& decided, const json& rule, const json& old, const json& buildings, std::vector<std::string>& notes) const {
    std::set<std::string> scenes;
    for (const auto& r : rows_) {
        if (!r.scene.empty()) scenes.insert(r.scene);
    }
    for (const auto& b : buildings) {
        if (!Str(b, "interior").empty()) scenes.insert(Str(b, "interior"));
    }
    std::set<std::pair<std::string, int>> stages;
    for (const auto& doc : store_.List("quests")) {
        const json q = store_.Read(doc).value_or(json::object());
        for (const auto& s : q.value("stages", json::array())) stages.insert({Str(q, "id"), s.value("index", 0)});
    }
    auto building = [&](const std::string& id) { return FindIn(buildings, id); };
    // A spot of the AI's made real: its building's scene (inside: its interior's), else the scene said; "" when it isn't real.
    auto realSpot = [&](const json& s, std::string scene, std::string& why) -> json {
        if (!s.is_object() || !SpotSays(s)) {
            why = "it says no spot";
            return json();
        }
        if (!Str(s, "building").empty()) {
            const json b = building(Str(s, "building"));
            if (b.is_null()) {
                why = "no building " + Str(s, "building");
                return json();
            }
            const bool inside = s.value("inside", false) && !Str(b, "interior").empty();
            scene = inside ? Str(b, "interior") : Str(b, "scene");
            json out = CleanSpot(s, scene);
            out["inside"] = inside;
            return out;
        }
        if (!Str(s, "near").empty() && !building(Str(s, "near")).is_null()) scene = Str(building(Str(s, "near")), "scene");
        if (!scenes.contains(scene)) {
            why = "no scene " + scene;
            return json();
        }
        return CleanSpot(s, scene);
    };
    auto fromReal = [&](const json& f) {
        const json c = CleanFrom(f);
        return !c.contains("quest") || stages.contains({Str(c, "quest"), c.value("stage", 0)});
    };
    auto merge = [&](const char* list, const char* prefix) {
        json out = json::array();
        std::map<std::string, json> byAi, byOld;
        for (const auto& e : decided.value(list, json::array())) byAi[Str(e, "id")] = e;
        for (const auto& e : old.value(list, json::array())) byOld[Str(e, "id")] = e;
        for (json e : rule.value(list, json::array())) {
            const std::string id = Str(e, "id");
            if (const auto ai = byAi.find(id); ai != byAi.end()) {
                const json& d = ai->second;
                std::string why;
                const json spot = realSpot(d.value("spot", json()), Str(d, "scene"), why);
                bool ok = !spot.is_null();
                for (const char* k : {"home", "work"}) {
                    if (!Str(d, k).empty() && building(Str(d, k)).is_null()) ok = false, why = "no building " + Str(d, k);
                }
                if (ok) {
                    if (std::string(list) == "characters") {
                        e["home"] = Str(d, "home").empty() ? json() : json(Str(d, "home"));
                        e["work"] = Str(d, "work").empty() ? json() : json(Str(d, "work"));
                    }
                    e["spot"] = spot;
                    e["scene"] = Str(spot, "scene");
                    if (!Str(d, "why").empty()) e["why"] = Str(d, "why");
                    if (!Str(d, "faces").empty()) e["faces"] = Str(d, "faces"); // a prop's front: what it looks to
                    e["by"] = "ai";
                    // Its whereabouts: each that's real, the start first (its spot).
                    json where = json::array({{{"from", {{"start", true}}}, {"scene", e["scene"]}, {"spot", spot}, {"why", e["why"]}}});
                    for (const auto& w : d.value("whereabouts", json::array())) {
                        if (!w.is_object() || CleanFrom(w.value("from", json())).value("start", false) || !fromReal(w.value("from", json()))) continue;
                        json one = {{"from", CleanFrom(w.value("from", json()))}, {"why", Str(w, "why")}};
                        if (w.contains("until") && w["until"].is_object() && (!Str(w["until"], "quest").empty() || !Str(w["until"], "event").empty())) {
                            one["until"] = CleanFrom(w["until"]);
                        }
                        if (!Str(w, "holder").empty()) {
                            one["holder"] = Str(w, "holder");
                        } else {
                            std::string wwhy;
                            const json ws = realSpot(w.value("spot", json()), Str(w, "scene"), wwhy);
                            if (ws.is_null()) continue;
                            one["scene"] = Str(ws, "scene");
                            one["spot"] = ws;
                        }
                        where.push_back(one);
                    }
                    if (where.size() > 1 || e.value("whereabouts", json::array()).size() <= 1) e["whereabouts"] = where;
                    else {
                        // The AI gave none past the start: the rules' moves kept after its start.
                        json kept = json::array({where[0]});
                        for (size_t k = 1; k < e["whereabouts"].size(); ++k) kept.push_back(e["whereabouts"][k]);
                        e["whereabouts"] = kept;
                    }
                } else {
                    notes.push_back(id + ": the AI's decision isn't real (" + why + "): the rules' kept");
                }
            } else if (!decided.empty()) {
                notes.push_back(id + ": the AI left it out: the rules'");
            }
            // The user's edits stay as they made them.
            if (const auto o = byOld.find(id); o != byOld.end() && o->second.value("locked", false)) {
                e = o->second;
                notes.push_back(id + ": kept as you set it (locked)");
            }
            // Each whereabouts' id, and what it had built (its requirement) while it's the same.
            const json was = byOld.contains(id) ? byOld[id].value("whereabouts", json::array()) : json::array();
            json& where = e["whereabouts"];
            if (!where.is_array()) where = json::array();
            std::stable_sort(where.begin(), where.end(), [](const json& a, const json& b) { return FromKey(a.value("from", json())) < FromKey(b.value("from", json())); });
            for (size_t k = 0; k < where.size(); ++k) {
                json& w = where[k];
                w["id"] = std::string(prefix) + PlanSlug(id) + "-" + std::to_string(k);
                if (!w.contains("until") && k + 1 < where.size()) w["until"] = where[k + 1].value("from", json());
                if (!w.contains("until")) w["until"] = nullptr;
                w["req"] = nullptr;
                w["status"] = "todo";
                for (const auto& o : was) {
                    if (o.value("from", json()) == w.value("from", json()) && Str(o, "scene") == Str(w, "scene") && o.value("spot", json()) == w.value("spot", json())) {
                        w["req"] = o.value("req", json());
                        w["status"] = o.value("status", std::string("todo"));
                    }
                }
            }
            if (!e.contains("locked")) e["locked"] = false;
            out.push_back(e);
        }
        return out;
    };
    json result = {{"schema", "aige.placements/1"}, {"updated", Now()}, {"decided", decided.empty() ? "rule" : "ai"}, {"buildings", buildings}};
    result["characters"] = merge("characters", "w-");
    result["props"] = merge("props", "wp-");
    // Whose each building is.
    for (auto& b : result["buildings"]) {
        for (const auto& c : result["characters"]) {
            for (const char* use : {"home", "work"}) {
                if (Str(c, use) == Str(b, "id") && b["owner"].is_null()) b["owner"] = Str(c, "id"), b["use"] = use;
            }
        }
    }
    return result;
}

// --- Into the scenes ------------------------------------------------------------------------------------------------

void GameBuild::ResolvePlacements(const std::function<Registry*(const std::string& scene)>& sceneOf) {
    std::map<Registry*, std::unique_ptr<TileGrid>> grids;
    auto gridOf = [&](Registry* reg) -> const TileGrid& {
        auto& g = grids[reg];
        if (!g) g = std::make_unique<TileGrid>(TileGrid::Build(*reg));
        return *g;
    };
    const json buildings = placements_.value("buildings", json::array());
    auto actorAnchor = [&](const std::string& place, const std::string& who, const std::string& name) {
        const json d = store_.Read("places/" + PlanSlug(place)).value_or(json::object());
        for (const auto& a : d.value("actors", json::array())) {
            if (a.is_object() && PlanNamesCharacter(Str(a, "character"), who, name)) return Str(a, "anchor");
        }
        const json anchors = d.value("storyAnchors", json::array());
        return anchors.empty() ? std::string() : Str(anchors[0], "id");
    };
    // The cells taken in each scene: by the characters placed, and by the props' own markers (a character stands beside
    // the record table they keep, not on it); counted once the anchors are where they go.
    std::map<std::string, std::set<std::pair<int, int>>> taken;
    // The cells hidden behind a painted place's buildings, in each scene.
    std::map<std::string, std::set<std::pair<int, int>>> hiddenIn;
    auto hiddenOf = [&](const std::string& scene) -> const std::set<std::pair<int, int>>& {
        auto [it, fresh] = hiddenIn.try_emplace(scene);
        if (fresh) {
            for (const auto& r : rows_) {
                if (r.scene == scene && r.buildType != "interior") it->second = HiddenCells(r);
            }
        }
        return it->second;
    };
    // The cells one can walk to from where one arrives (else from its first gateway's arrival), in each scene.
    std::map<Registry*, std::set<std::pair<int, int>>> reaches;
    auto reachOf = [&](Registry* reg, const TileGrid& grid) -> const std::set<std::pair<int, int>>& {
        auto [it, fresh] = reaches.try_emplace(reg);
        if (!fresh) return it->second;
        std::optional<std::pair<float, float>> from = PlanMarkerAt(*reg, "arrive");
        if (!from) {
            for (auto [e, m, w] : reg->view<Marker, WorldTransform2D>().each()) {
                if (!from && m.name.rfind("from-", 0) == 0) from = std::make_pair(w.x, w.y);
            }
        }
        if (!from) return it->second;
        std::deque<std::pair<int, int>> open = {{static_cast<int>(std::floor(from->first)), static_cast<int>(std::floor(from->second))}};
        if (grid.IsSolid(open[0].first, open[0].second)) return it->second;
        it->second.insert(open[0]);
        while (!open.empty() && it->second.size() < 200000) {
            const auto [x, y] = open.front();
            open.pop_front();
            for (const auto [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                const std::pair<int, int> n = {x + dx, y + dy};
                if (it->second.contains(n) || grid.IsSolid(n.first, n.second)) continue;
                it->second.insert(n);
                open.push_back(n);
            }
        }
        return it->second;
    };
    // A cell for a spot in its scene: false when its scene can't be read.
    auto resolve = [&](json& spot, const std::string& who, const std::string& name) -> bool {
        Registry* reg = sceneOf(Str(spot, "scene"));
        if (!reg) return false;
        const TileGrid& grid = gridOf(reg);
        std::optional<std::pair<int, int>> at;
        auto cellOf = [](const std::pair<float, float>& p) { return std::make_pair(static_cast<int>(std::floor(p.first)), static_cast<int>(std::floor(p.second))); };
        const json b = Str(spot, "building").empty() ? json() : FindIn(buildings, Str(spot, "building"));
        if (!Str(spot, "marker").empty()) {
            if (const auto m = PlanMarkerAt(*reg, Str(spot, "marker"))) at = cellOf(*m);
        }
        if (!at && !b.is_null() && spot.value("inside", false)) {
            // Inside: at their anchor there (the counter, the forge), else the room's middle.
            const std::string anchor = actorAnchor(Str(b, "interiorPlace"), who, name);
            if (const auto m = anchor.empty() ? std::nullopt : PlanMarkerAt(*reg, anchor)) at = cellOf(*m);
            if (!at) at = PlanOpenMiddle(*reg, grid);
        } else if (!at && !b.is_null() && b.contains("door") && b["door"].is_array() && b["door"].size() == 2) {
            at = std::make_pair(b["door"][0].get<int>(), b["door"][1].get<int>());
        }
        if (!at && !Str(spot, "near").empty()) {
            const json nb = FindIn(buildings, Str(spot, "near"));
            if (!nb.is_null() && nb["door"].is_array() && nb["door"].size() == 2) at = std::make_pair(nb["door"][0].get<int>() + 1, nb["door"][1].get<int>());
            else if (const auto m = PlanMarkerAt(*reg, Str(spot, "near"))) at = std::make_pair(cellOf(*m).first + 1, cellOf(*m).second);
        }
        if (!at) {
            // The middle of the place (its centrepiece when it has one), never its way in.
            for (const auto& c : buildings) {
                if (!at && Str(c, "scene") == Str(spot, "scene") && Str(c, "kind") == "centrepiece") at = std::make_pair(c["door"][0].get<int>(), c["door"][1].get<int>() + 1);
            }
            if (!at) at = PlanOpenMiddle(*reg, grid);
            if (!Str(spot, "marker").empty()) spot["missing"] = Str(spot, "marker") + " isn't a marker of " + Str(spot, "scene") + ": its middle";
        }
        if (!at) return false;
        // On a cell one can stand on and walk from: not solid, not a gateway's, not in a building's or a thing's box (the
        // painting's stalls, wells and gates aren't blocked by its walk marks), and joined to where one arrives.
        std::set<std::pair<int, int>> ways;
        for (auto [e, gw, w] : reg->view<Gateway, WorldTransform2D>().each()) ways.insert(cellOf({w.x, w.y}));
        const std::string scene = Str(spot, "scene");
        auto inThing = [&](int x, int y) {
            for (const auto& b : buildings) {
                if (Str(b, "scene") != scene || b.value("area", false) || b.value("anchor", false)) continue;
                const json box = b.value("box", json());
                if (!box.is_array() || box.size() != 4 || !box[0].is_number()) continue;
                if (x >= box[0].get<int>() && x <= box[2].get<int>() && y >= box[1].get<int>() && y <= box[3].get<int>()) return true;
            }
            return false;
        };
        const std::set<std::pair<int, int>>& reach = reachOf(reg, grid);
        const std::set<std::pair<int, int>>& hidden = hiddenOf(scene);
        for (int r = 0; r <= 12; ++r) {
            bool found = false;
            for (int dy = -r; dy <= r && !found; ++dy) {
                for (int dx = -r; dx <= r && !found; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                    const int x = at->first + dx, y = at->second + dy;
                    if (grid.IsSolid(x, y) || ways.contains({x, y}) || inThing(x, y)) continue;
                    if (hidden.contains({x, y})) continue; // behind a building, out of sight
                    if (!reach.empty() && !reach.contains({x, y})) continue; // a pocket nobody walks to
                    if (!who.empty() && taken[Str(spot, "scene")].contains({x, y})) continue; // someone, or a prop, is there
                    at = std::make_pair(x, y);
                    found = true;
                }
            }
            if (found) break;
        }
        spot["x"] = at->first;
        spot["y"] = at->second;
        if (!who.empty()) taken[Str(spot, "scene")].insert(*at);
        return true;
    };
    // A story anchor at someone's house ("record-table" at "lysa-house"): at the door of the house that is their home in
    // that scene (the plan's house isn't where the painter put it), its marker and its entry moved there.
    for (auto& an : placements_["buildings"]) {
        if (!an.value("anchor", false) || Str(an, "marker").empty()) continue;
        const std::string scene = Str(an, "scene");
        const json d = store_.Read("places/" + PlanSlug(RowPlaceOfScene(scene))).value_or(json::object());
        std::string at;
        for (const auto& a : d.value("storyAnchors", json::array())) {
            if (a.is_object() && Str(a, "id") == Str(an, "marker")) at = Lower(Str(a.value("relation", json::object()), "at"));
        }
        if (at.empty() || (at.find("house") == std::string::npos && at.find("home") == std::string::npos && at.find("shop") == std::string::npos &&
                           at.find("forge") == std::string::npos && at.find("cottage") == std::string::npos)) {
            continue;
        }
        std::string home;
        for (const auto& c : placements_.value("characters", json::array())) {
            const std::string slug = PlanSlug(Str(c, "id")), first = slug.substr(0, slug.find('-'));
            const json b = Str(c, "home").empty() ? json() : FindIn(buildings, Str(c, "home"));
            if (b.is_null() || Str(b, "scene") != scene || first.size() < 3) continue;
            if (home.empty() && (at.find(first) != std::string::npos || at.find(Lower(Str(c, "name"))) != std::string::npos)) home = Str(c, "home");
        }
        if (home.empty()) continue;
        json spot = {{"scene", scene}, {"building", home}, {"inside", false}};
        if (!resolve(spot, std::string(), std::string())) continue;
        const int x = spot.value("x", 0), y = spot.value("y", 0);
        Registry* reg = sceneOf(scene);
        bool found = false;
        for (auto [e, m, w] : reg->view<Marker, WorldTransform2D>().each()) {
            if (m.name == Str(an, "marker")) w.x = x + 0.5f, w.y = y + 0.5f, found = true;
        }
        if (!found) PlanAddMarker(*reg, Str(an, "marker"), x + 0.5f, y + 0.5f);
        an["box"] = {x, y, x, y};
        an["door"] = {x, y};
        an["near"] = home;
    }
    for (const auto& p : placements_.value("props", json::array())) {
        const json s = p.value("spot", json());
        if (!s.is_object() || Str(s, "marker").empty()) continue;
        if (Registry* reg = sceneOf(Str(s, "scene"))) {
            if (const auto m = PlanMarkerAt(*reg, Str(s, "marker"))) taken[Str(s, "scene")].insert({static_cast<int>(std::floor(m->first)), static_cast<int>(std::floor(m->second))});
        }
    }
    for (auto& c : placements_["characters"]) {
        const std::string id = Str(c, "id"), slug = PlanSlug(id), name = Str(c, "name");
        if (!c["spot"].is_object()) continue;
        if (!resolve(c["spot"], id, name)) c["spot"]["missing"] = "its scene " + Str(c["spot"], "scene") + " can't be read";
        c["scene"] = Str(c["spot"], "scene");
        // Their bed (their home's door, or inside it when they're met inside it) and their workplace, as markers of their
        // scene: where their sleep and work packages take them (not the spot they stand at).
        c.erase("bed");
        c.erase("workAt");
        for (const char* role : {"home", "work"}) {
            const std::string bid = Str(c, role);
            const json b = bid.empty() ? json() : FindIn(buildings, bid);
            if (b.is_null()) continue;
            const std::string scene = Str(c, "scene");
            json s = {{"scene", scene}, {"building", bid}};
            if (Str(b, "scene") == scene) s["inside"] = false;
            else if (Str(b, "interior") == scene) s["inside"] = true;
            else continue;
            if (!resolve(s, id, name)) continue;
            Registry* reg = sceneOf(scene);
            const std::string marker = std::string(std::string(role) == "home" ? "home-" : "work-") + slug;
            bool moved = false;
            for (auto [e, m, w] : reg->view<Marker, WorldTransform2D>().each()) {
                if (m.name == marker) w.x = s.value("x", 0) + 0.5f, w.y = s.value("y", 0) + 0.5f, moved = true;
            }
            if (!moved) PlanAddMarker(*reg, marker, s.value("x", 0) + 0.5f, s.value("y", 0) + 0.5f);
            const std::string what = Str(b, "name").empty() ? Str(b, "kind") : Str(b, "name");
            c[std::string(role) == "home" ? "bed" : "workAt"] = {{"scene", scene}, {"marker", marker}, {"x", s["x"]}, {"y", s["y"]},
                {"label", std::string(std::string(role) == "home" ? "home: " : "work: ") + (s.value("inside", false) ? "inside the " : "the door of the ") + what + " (" + bid + ")"}};
        }
        int k = 0;
        for (auto& w : c["whereabouts"]) {
            ++k;
            if (w.value("from", json()).value("start", false)) {
                w["scene"] = c["scene"];
                w["spot"] = c["spot"];
                w["marker"] = "start-" + slug;
                continue;
            }
            if (!w.contains("spot") || !w["spot"].is_object()) continue;
            if (!resolve(w["spot"], id, name)) continue;
            w["scene"] = Str(w["spot"], "scene");
            // The marker it goes to: the one it names, else one made for it there.
            Registry* reg = sceneOf(Str(w, "scene"));
            if (!Str(w["spot"], "marker").empty() && reg && PlanMarkerAt(*reg, Str(w["spot"], "marker"))) {
                w["marker"] = Str(w["spot"], "marker");
            } else if (reg) {
                const std::string made = "where/" + slug + "/" + std::to_string(k - 1);
                PlanAddMarker(*reg, made, w["spot"].value("x", 0) + 0.5f, w["spot"].value("y", 0) + 0.5f);
                w["marker"] = made;
            }
        }
    }
    for (auto& p : placements_["props"]) {
        if (!p["spot"].is_object()) continue;
        if (!resolve(p["spot"], std::string(), Str(p, "name"))) p["spot"]["missing"] = "its scene " + Str(p["spot"], "scene") + " can't be read";
        p["scene"] = Str(p["spot"], "scene");
        for (auto& w : p["whereabouts"]) {
            if (w.value("from", json()).value("start", false)) w["scene"] = p["scene"], w["spot"] = p["spot"];
        }
        // A prop made from the assets (the record table) already standing in its scene goes where it's placed now (its
        // marker moved with its anchor): Implement only stands one where there's none. Its name as a slug (Record Table
        // is record-table); one copy kept, the plain others (no script, no action) taken out.
        if (Str(p, "id").rfind("asset:", 0) == 0 && p["spot"].contains("x") && !p["spot"].contains("missing")) {
            if (Registry* reg = sceneOf(Str(p, "scene"))) {
                auto slugOf = [](const std::string& text) {
                    std::string out;
                    for (const unsigned char ch : text) {
                        if (std::isalnum(ch)) out += static_cast<char>(std::tolower(ch));
                        else if (!out.empty() && out.back() != '-') out += '-';
                    }
                    while (!out.empty() && out.back() == '-') out.pop_back();
                    return out;
                };
                const std::string name = slugOf(Str(p, "name"));
                const float x = p["spot"].value("x", 0) + 0.5f, y = p["spot"].value("y", 0) + 0.5f;
                std::vector<Entity> copies;
                for (auto [e, n, w] : reg->view<Name, WorldTransform2D>().each()) {
                    if (!reg->all_of<Marker>(e) && !reg->all_of<Actor>(e) && slugOf(n.value) == name) w.x = x, w.y = y, copies.push_back(e);
                }
                // The one kept: one with a script or an action, else one with the asset's own picture, else the first.
                auto rank = [&](Entity e) {
                    if (reg->all_of<LuaScript>(e) || reg->all_of<Action>(e)) return 0;
                    const Sprite* s = reg->try_get<Sprite>(e);
                    return s && s->texture.find("placeholders") == std::string::npos ? 1 : 2;
                };
                std::stable_sort(copies.begin(), copies.end(), [&](Entity a, Entity b) { return rank(a) < rank(b); });
                for (size_t i = 1; i < copies.size(); ++i) {
                    if (rank(copies[i]) > 0) reg->destroy(copies[i]);
                }
                // Its asset (⑥ Assets: its picture and size): one stood from it where there's none, a placeholder's
                // picture replaced by it; and it's a prefab (prefabs/props/<name>.prefab), its copy an instance of it.
                json asset;
                if (reqs_.empty()) LoadRequirements();
                for (const auto& r : reqs_.value("items", json::array())) {
                    const json a = r.value("asset", json());
                    if (Str(r, "kind") == "asset" && a.is_object() && slugOf(Str(a, "id")) == slugOf(Str(p, "id").substr(6))) asset = a;
                }
                const std::string art = asset.is_object() ? Str(asset, "art") : std::string();
                const bool hasArt = !art.empty() && fs::exists(host_.projectRoot / art);
                const std::string size = asset.is_object() ? Str(asset, "size") : std::string();
                const float height = size == "tiny" ? 24.0f : size == "small" ? 40.0f : size == "large" ? 112.0f : size == "huge" ? 160.0f : 72.0f;
                auto dress = [&](Entity e) {
                    auto& s = reg->get_or_emplace<Sprite>(e);
                    s = Sprite{};
                    s.texture = art;
                    s.height = height;
                    s.width = height;
                    // As wide as its picture is for that height (a PNG's size in its header).
                    std::ifstream in(host_.projectRoot / art, std::ios::binary);
                    unsigned char h[24] = {};
                    if (in.read(reinterpret_cast<char*>(h), 24) && h[1] == 'P' && h[2] == 'N' && h[3] == 'G') {
                        const int pw = (h[16] << 24) | (h[17] << 16) | (h[18] << 8) | h[19], ph = (h[20] << 24) | (h[21] << 16) | (h[22] << 8) | h[23];
                        if (pw > 0 && ph > 0) s.width = height * static_cast<float>(pw) / static_cast<float>(ph);
                    }
                };
                Entity keep = copies.empty() ? Entity{entt::null} : copies.front();
                if (keep == entt::null && hasArt) {
                    keep = reg->create();
                    reg->emplace<Name>(keep, asset.is_object() && !Str(asset, "name").empty() ? Str(asset, "name") : Str(p, "name"));
                    reg->emplace<WorldTransform2D>(keep, x, y);
                    dress(keep);
                } else if (keep != entt::null && hasArt && rank(keep) == 2) {
                    reg->remove<PrefabInstance>(keep); // the placeholder's: its own prefab instead
                    dress(keep);
                }
                // Turned to what it faces (a building, a marker, a character, or n, e, s, w): its picture's front looks to
                // the lower left (+y); mirrored, to the lower right (+x). Whichever is nearer the way to it.
                if (keep != entt::null && reg->valid(keep) && !Str(p, "faces").empty()) {
                    const std::string faces = Str(p, "faces");
                    std::optional<std::pair<float, float>> toward;
                    if (faces == "n") toward = std::make_pair(x, y - 1.0f);
                    else if (faces == "s") toward = std::make_pair(x, y + 1.0f);
                    else if (faces == "e") toward = std::make_pair(x + 1.0f, y);
                    else if (faces == "w") toward = std::make_pair(x - 1.0f, y);
                    if (!toward) {
                        if (const json b = FindIn(buildings, faces); !b.is_null() && b["door"].is_array() && b["door"].size() == 2) {
                            toward = std::make_pair(b["door"][0].get<float>() + 0.5f, b["door"][1].get<float>() + 0.5f);
                        }
                    }
                    if (!toward) toward = PlanMarkerAt(*reg, faces);
                    if (!toward) {
                        for (const auto& c : placements_.value("characters", json::array())) {
                            if (Str(c, "id") == faces && c["spot"].is_object() && c["spot"].contains("x") && Str(c, "scene") == Str(p, "scene")) {
                                toward = std::make_pair(c["spot"].value("x", 0) + 0.5f, c["spot"].value("y", 0) + 0.5f);
                            }
                        }
                    }
                    if (toward) {
                        const float dx = toward->first - x, dy = toward->second - y;
                        const bool mirrored = dx > dy; // more toward +x (the lower right) than +y (the lower left)
                        if (auto* s = reg->try_get<Sprite>(keep); s && (s->u0 > s->u1) != mirrored) std::swap(s->u0, s->u1);
                        p["facing"] = mirrored ? "lower right" : "lower left";
                    } else {
                        p["facing"] = faces + " isn't a building, a marker or a character here";
                    }
                }
                if (keep != entt::null && reg->valid(keep) && !reg->all_of<PrefabInstance>(keep)) {
                    PrefabLibrary prefabs(Project{host_.projectRoot});
                    std::string why;
                    const std::string prefab = "prefabs/props/" + name + ".prefab";
                    if (prefabs.SaveAsPrefab(*reg, keep, prefab, &why)) p["prefab"] = prefab;
                    else p["prefabError"] = why;
                } else if (keep != entt::null && reg->valid(keep)) {
                    p["prefab"] = reg->get<PrefabInstance>(keep).path;
                }
            }
        }
    }
}

json GameBuild::WhereaboutsAt(const std::string& who, const std::string& quest, int stage) const {
    const json p = placements_.contains("characters") ? placements_ : ReadPlacements(host_.projectRoot);
    for (const auto& c : p.value("characters", json::array())) {
        if (Str(c, "id") != who) continue;
        json best;
        long bestKey = -2;
        for (const auto& w : c.value("whereabouts", json::array())) {
            const json f = w.value("from", json());
            long key = -1;
            if (!f.value("start", false)) {
                if (Str(f, "quest") != quest) continue;
                const int at = f.value("stage", 0);
                const bool on = f.value("on", std::string("enter")) == "complete" ? at < stage : at <= stage;
                if (!on) continue;
                key = FromKey(f);
            }
            if (!w.contains("holder") && key > bestKey) best = w, bestKey = key;
        }
        return best;
    }
    return json();
}

// --- Asked, answered, applied ----------------------------------------------------------------------------------------

std::string GameBuild::RequestPlacements(const std::string& why) {
    if (stage_.empty() || stage_ == "build") return "the places aren't all built yet";
    // Already under way: once. A step left over from a build that was stopped (or closed) meanwhile starts again.
    if (!placeStep_.empty() && status_ == "working") return {};
    placeWhy_ = why;
    placeStep_ = "decide";
    placeStatus_ = status_ == "working" ? std::string("working") : status_;
    status_ = "working";
    message_ = "⑪ Placements: finding the buildings and deciding where everyone stands (" + why + ")…";
    placeNews_ = {{"progress", "Finding the buildings and who lives and works in each…"}};
    Record("placements", "characters", "asked again", why);
    Save();
    return {};
}

std::string GameBuild::ReplaceCharacters(const std::string& why) { return RequestPlacements(why); }

bool GameBuild::DrivePlacements() {
    if (placeStep_.empty()) return false;
    if (placeStep_ == "decide") {
        // The roads first (their ways out marked): the AI puts the gateways between outdoor places on them.
        const json laid = LayRoads();
        const json buildings = PlacementBuildings();
        json input = PlacementInput(buildings);
        input["exits"] = laid["exits"];
        input["ways"] = OutdoorWays();
        // The requirements of where people and things are: the placements are how they're met.
        json wanted = json::array();
        static const std::set<std::string> kWhere = {"presence", "character-state", "arrival", "movement", "prop", "item"};
        for (const auto& r : reqs_.value("items", json::array())) {
            if (!kWhere.contains(Str(r, "kind")) || wanted.size() >= 240) continue;
            wanted.push_back({{"id", Str(r, "id")}, {"kind", Str(r, "kind")}, {"text", Str(r, "text")}, {"when", r.value("when", json::object())},
                              {"where", r.value("where", json::object())}, {"who", r.value("who", json::array())}});
        }
        if (!wanted.empty()) input["requirements"] = wanted;
        const int id = host_.request ? host_.request("/planner/placements", input, 600) : 0;
        if (id != 0) {
            pending_[id] = "place:";
            placeStep_ = "waiting";
            message_ = "⑪ Placements: the AI decides whose each of the " + std::to_string(buildings.size()) + " buildings is and where everyone stands…";
            placeNews_ = {{"progress", "The AI decides where everyone stands (" + std::to_string(buildings.size()) + " buildings)…"}};
            return true;
        }
        placeAnswers_.push_back({{"ok", false}, {"errors", json::array({"the AI service isn't running: the rules decide"})}});
        placeStep_ = "apply";
    }
    if (placeStep_ == "waiting") {
        if (placeAnswers_.empty()) return false;
        placeStep_ = "apply";
    }
    const json answer = placeAnswers_.empty() ? json::object() : placeAnswers_.back();
    placeAnswers_.clear();
    placeNews_ = {{"progress", "Placing everyone…"}};
    SettleAreaAnchors();
    const json buildings = PlacementBuildings();
    const json rule = RulePlacements(buildings);
    std::vector<std::string> notes;
    std::string error;
    json decided = json::object();
    if (answer.value("ok", false) && answer.contains("placements") && answer["placements"].is_object()) decided = answer["placements"];
    else if (answer.contains("errors")) error = answer["errors"].dump();
    if (answer.contains("problems")) notes.push_back("the AI's answer still had problems: " + answer["problems"].dump());
    // The roads and their ways out, and each gateway between outdoor places on the way out the AI chose.
    const json laid = LayRoads();
    const json gates = SettleGateways(decided.value("gateways", json::array()), laid["exits"], ReadPlacements(host_.projectRoot).value("gateways", json::array()));
    placements_ = MergePlacements(decided, rule, ReadPlacements(host_.projectRoot), buildings, notes);
    placements_["roads"] = laid["roads"];
    placements_["exits"] = laid["exits"];
    placements_["gateways"] = gates;
    placements_["notes"] = notes;
    placements_["why"] = placeWhy_;
    SavePlacements();
    std::string why;
    if (stage_ != "populate") {
        // Everyone placed again by it (Populate resolves and records the spots), then its requirements.
        why = PlaceCharactersAgain([](const std::string&) {});
        RefreshPlacementRequirements();
        CheckPlacementRequirements();
    }
    placeStep_.clear();
    if (placeStatus_ != "working") status_ = placeStatus_.empty() ? std::string("done") : placeStatus_;
    message_ = why.empty() ? "⑪ Placements: " + std::to_string(placements_.value("characters", json::array()).size()) + " characters and " +
                                 std::to_string(placements_.value("props", json::array()).size()) + " props placed (" + placements_.value("decided", std::string("rule")) + ")"
                           : "⑪ Placements: " + why;
    Record("placements", "characters", placements_.value("decided", std::string("rule")), placeWhy_ + (error.empty() ? "" : " (AI: " + error + ")"));
    placeNews_ = {{"done", true}, {"placements", ReadPlacements(host_.projectRoot)}};
    if (!why.empty() || !error.empty()) placeNews_["error"] = !why.empty() ? why : "the AI didn't decide (" + error + "): the rules did";
    Save();
    return true;
}

std::string GameBuild::ReplaceCharactersNow(const std::function<void(const std::string&)>& say) {
    if (stage_.empty() || stage_ == "build") return "the places aren't all built yet";
    SettleAreaAnchors();
    const json buildings = PlacementBuildings();
    say(std::to_string(buildings.size()) + " buildings found");
    std::vector<std::string> notes;
    const json laid = LayRoads();
    const json gates = ReadPlacements(host_.projectRoot).value("gateways", json::array());
    placements_ = MergePlacements(json::object(), RulePlacements(buildings), ReadPlacements(host_.projectRoot), buildings, notes);
    placements_["roads"] = laid["roads"];
    placements_["exits"] = laid["exits"];
    placements_["gateways"] = gates;
    placements_["notes"] = notes;
    placements_["why"] = "build_scenes --replace-characters";
    SavePlacements();
    for (const auto& n : notes) say("  " + n);
    if (const std::string why = PlaceCharactersAgain(say); !why.empty()) return why;
    RefreshPlacementRequirements();
    CheckPlacementRequirements();
    Save();
    return {};
}

json GameBuild::TakePlacementNews() {
    json out = std::move(placeNews_);
    placeNews_ = json();
    return out;
}

void GameBuild::PlacementMoved(const std::string& character, const std::string& scene) {
    for (auto& a : bindings_["actors"]) {
        if (Str(a, "character") == character) a["scene"] = scene;
    }
    std::ofstream(store_.Dir() / "bindings.json", std::ios::binary) << bindings_.dump(2);
    Save();
}

// --- Where their packages run ------------------------------------------------------------------------------------------

json GameBuild::PackageSpot(const json& placed, const std::string& tmpl, const std::string& spot,
                            const std::function<bool(const std::string&)>& hasMarker) const {
    const std::string slug = PlanSlug(Str(placed, "id"));
    const json bed = placed.value("bed", json()), work = placed.value("workAt", json());
    const json own = placed.value("spot", json::object());
    const std::string ownText = !Str(own, "marker").empty() ? "at " + Str(own, "marker") : !Str(own, "building").empty() ? "at " + Str(own, "building") : "where they stand";
    auto of = [](const json& m) { return json{{"marker", Str(m, "marker")}, {"label", Str(m, "label")}}; };
    if (tmpl == "sleep" && bed.is_object()) return of(bed);
    // A spot the timeline names that is a marker of the scene (village-square, the record board).
    if (!spot.empty()) {
        for (const std::string m : {spot, PlanSlug(spot)}) {
            if (!m.empty() && hasMarker(m)) return {{"marker", m}, {"label", "at " + m}};
        }
        const std::string words = Lower(spot);
        if (bed.is_object() && (words.find("home") != std::string::npos || words.find("house") != std::string::npos || words.find("bed") != std::string::npos ||
                                words.find("cottage") != std::string::npos)) {
            return of(bed);
        }
    }
    static const std::set<std::string> working = {"work", "vendor", "tend", "use-part", "use-object", "guard"};
    if (working.contains(tmpl) && work.is_object()) return of(work);
    if ((tmpl == "stay" || tmpl == "home") && bed.is_object() && !working.contains(tmpl) && Str(own, "marker").empty() && Str(own, "building").empty()) return of(bed);
    return {{"marker", "start-" + slug}, {"label", ownText}};
}

void GameBuild::AnnotateCastPackages() {
    if (!reqs_.contains("items") || !reqs_["items"].is_array()) return;
    const json p = ReadPlacements(host_.projectRoot);
    std::map<std::string, json> byWho;
    for (const auto& c : p.value("characters", json::array())) byWho[Str(c, "id")] = c;
    // The markers each scene has that a package could name: the anchors, the spots, the beds and workplaces.
    std::map<std::string, std::set<std::string>> markers;
    for (const auto& b : p.value("buildings", json::array())) {
        if (!Str(b, "marker").empty()) markers[Str(b, "scene")].insert(Str(b, "marker"));
    }
    for (const auto& c : p.value("characters", json::array())) {
        for (const auto& w : c.value("whereabouts", json::array())) {
            if (!Str(w, "marker").empty()) markers[Str(w, "scene")].insert(Str(w, "marker"));
        }
    }
    bool changed = false;
    for (auto& r : reqs_["items"]) {
        if (Str(r, "kind") != "character-state" || !r.contains("state") || !r["state"].is_object()) continue;
        json& state = r["state"];
        if (!state.contains("packages") || !state["packages"].is_array()) continue;
        const std::string who = r.value("who", json::array()).empty() ? std::string() : r["who"][0].get<std::string>();
        const auto it = byWho.find(who);
        if (it == byWho.end()) continue;
        const std::string scene = Str(it->second, "scene");
        for (auto& pk : state["packages"]) {
            if (!pk.is_object()) continue;
            // In their own scene (the package's place, else the state's, else where they are): a marker there.
            const std::string place = !Str(pk, "place").empty() ? Str(pk, "place") : Str(state, "place");
            if (!place.empty() && RowPlaceOfScene(scene) != place) {
                pk.erase("at");
                continue;
            }
            const json at = PackageSpot(it->second, Str(pk, "template"), Str(state, "spot"),
                                        [&](const std::string& m) { return markers[scene].contains(m); });
            if (pk.value("at", json()) != json(Str(at, "label"))) pk["at"] = Str(at, "label"), pk["marker"] = Str(at, "marker"), changed = true;
        }
    }
    if (changed) SaveRequirements();
}

// --- Its requirements ------------------------------------------------------------------------------------------------

json GameBuild::PlacementRequirements() const {
    json out = json::array();
    const json p = ReadPlacements(host_.projectRoot);
    const json buildings = p.value("buildings", json::array());
    const json outline = store_.Read("outline").value_or(json::object()).value("places", json::array());
    auto placeName = [&](const std::string& scene) {
        std::string place;
        for (const auto& r : rows_) {
            if (r.scene == scene) place = r.place;
        }
        for (const auto& o : outline) {
            if (Str(o, "id") == place && !Str(o, "name").empty()) return Str(o, "name");
        }
        return place.empty() ? Stem(scene) : place.substr(place.rfind('/') + 1);
    };
    auto spotText = [&](const json& s, const std::string& marker) {
        const json b = Str(s, "building").empty() ? json() : FindIn(buildings, Str(s, "building"));
        if (!b.is_null()) return (s.value("inside", false) ? "inside the " : "at the door of the ") + Str(b, "name");
        if (!Str(s, "marker").empty()) return "at " + Str(s, "marker");
        if (!Str(s, "near").empty()) return "near " + Str(s, "near") + (Str(s, "note").empty() ? std::string() : " (" + Str(s, "note") + ")");
        return "at " + marker;
    };
    // A stage's story scene (the requirement is shown under it).
    std::map<std::pair<std::string, int>, std::string> storyOf;
    for (const auto& doc : store_.List("quests")) {
        const json q = store_.Read(doc).value_or(json::object());
        for (const auto& s : q.value("stages", json::array())) storyOf[{Str(q, "id"), s.value("index", 0)}] = Str(s, "advancesScene");
    }
    for (const auto& c : p.value("characters", json::array())) {
        const std::string who = Str(c, "id"), name = Str(c, "name");
        for (const auto& w : c.value("whereabouts", json::array())) {
            const std::string scene = Str(w, "scene"), marker = Str(w, "marker");
            if (scene.empty() || marker.empty() || w.contains("holder")) continue;
            const json from = w.value("from", json());
            const json until = w.value("until", json());
            json when = from;
            json check = {{"check", "at"}, {"who", who}, {"scene", scene}, {"marker", marker}};
            if (w.value("spot", json()).contains("x")) check["x"] = w["spot"]["x"], check["y"] = w["spot"]["y"];
            for (const char* k : {"start", "quest", "stage", "on", "event"}) {
                if (from.contains(k)) check[k] = from[k];
            }
            const std::string text = name + " is " + spotText(w.value("spot", json()), marker) + " in " + placeName(scene) + " from " + FromText(from) +
                                     (until.is_object() && !until.empty() ? " until " + FromText(until) : std::string()) +
                                     " (marker " + marker + ")" + (Str(w, "why").empty() ? std::string() : " — " + Str(w, "why"));
            out.push_back({{"id", Str(w, "id")}, {"scene", from.contains("quest") ? storyOf[{Str(from, "quest"), from.value("stage", 0)}] : std::string()},
                           {"act", ""}, {"kind", "whereabouts"}, {"by", "rule"}, {"status", "todo"}, {"text", text}, {"when", when}, {"until", until},
                           {"where", {{"place", RowPlaceOfScene(scene)}, {"scene", scene}, {"marker", marker}}}, {"who", json::array({who})},
                           {"source", {{"doc", "placements"}, {"who", who}, {"whereabouts", Str(w, "id")}}}, {"accept", json::array({check})}});
        }
    }
    for (const auto& pr : p.value("props", json::array())) {
        const json ws = pr.value("whereabouts", json::array());
        if (ws.empty() || Str(pr, "scene").empty()) continue;
        const json& start = ws[0];
        std::string until;
        for (const auto& w : ws) {
            if (w.contains("holder")) until = FromText(w.value("from", json())) + " (" + (Str(w, "holder") == "player" ? std::string("the player picks it up") : "given to " + Str(w, "holder")) + ")";
        }
        const json spot = pr.value("spot", json());
        json check = {{"check", "prop-at"}, {"id", Str(pr, "id")}, {"scene", Str(pr, "scene")}, {"name", Str(pr, "name")}};
        if (!Str(pr, "item").empty()) check["item"] = Str(pr, "item");
        if (!Str(spot, "own").empty()) check["marker"] = Str(spot, "own");
        if (spot.contains("x")) check["x"] = spot["x"], check["y"] = spot["y"];
        out.push_back({{"id", Str(start, "id")}, {"scene", ""}, {"act", ""}, {"kind", "whereabouts"}, {"by", "rule"}, {"status", "todo"},
                       {"text", "The " + Str(pr, "name") + " is " + spotText(spot, Str(spot, "own")) + " in " + placeName(Str(pr, "scene")) + " from the start" +
                                    (until.empty() ? std::string() : " until " + until) + (Str(pr, "why").empty() ? std::string() : " — " + Str(pr, "why"))},
                       {"when", {{"start", true}}}, {"where", {{"place", RowPlaceOfScene(Str(pr, "scene"))}, {"scene", Str(pr, "scene")}, {"marker", Str(spot, "own")}}},
                       {"who", json::array()}, {"source", {{"doc", "placements"}, {"prop", Str(pr, "id")}, {"whereabouts", Str(start, "id")}}},
                       {"accept", json::array({check})}});
    }
    return out;
}

namespace {

// What builds a whereabouts requirement (its implementation items): {how, does, by}.
json WhereaboutsItems(const json& r) {
    const json a = r.value("accept", json::array()).empty() ? json::object() : r["accept"][0];
    const json when = r.value("when", json::object());
    const std::string at = Str(a, "marker") + " in " + Str(a, "scene");
    if (Str(a, "check") == "prop-at") return json::array({{{"how", "prefab"}, {"does", "⑪ places it at " + at + " (planner/placements.json)"}, {"by", "rule"}}});
    if (when.value("start", false)) return json::array({{{"how", "quest-effect"}, {"does", "⑪ places them at " + at + " (planner/placements.json)"}, {"by", "rule"}}});
    if (!Str(when, "event").empty()) {
        return json::array({{{"how", "script"}, {"does", "on the event " + Str(when, "event") + " they go to " + at + " (a rule on the event, a package, or a script)"}, {"by", "ai-panel"}}});
    }
    return json::array({{{"how", "quest-effect"},
                         {"does", std::string(Str(when, "on") == "complete" ? "once " : "entering ") + Str(when, "quest") + " stage " + std::to_string(when.value("stage", 0)) +
                                      (Str(when, "on") == "complete" ? " is done" : "") + ", a move_actor takes them to " + at},
                         {"by", "rule"}}});
}

} // namespace

void GameBuild::RefreshPlacementRequirements() {
    const json fresh = PlacementRequirements();
    // The statuses back onto the placements' whereabouts (built, failed...).
    auto syncStatuses = [&] {
        std::map<std::string, json> byWhere;
        for (const auto& r : reqs_.value("items", json::array())) {
            if (Str(r, "kind") == "whereabouts") byWhere[Str(r, "id")] = r;
        }
        json p = ReadPlacements(host_.projectRoot);
        for (const char* list : {"characters", "props"}) {
            if (!p.contains(list)) continue;
            for (auto& e : p[list]) {
                for (auto& w : e["whereabouts"]) {
                    const auto it = byWhere.find(Str(w, "id"));
                    if (it == byWhere.end()) continue;
                    w["req"] = Str(w, "id");
                    w["status"] = it->second.value("status", std::string("todo"));
                }
            }
        }
        placements_ = p;
        SavePlacements();
    };
    if (!reqs_.contains("items") || !reqs_["items"].is_array() || reqs_["items"].empty()) return; // made with the rest (MakeRequirements)
    std::map<std::string, json> old;
    json items = json::array();
    for (const auto& r : reqs_["items"]) {
        if (Str(r, "kind") == "whereabouts") old[Str(r, "id")] = r;
        else items.push_back(r);
    }
    std::set<std::string> changed;
    for (const auto& f : fresh) {
        const auto it = old.find(Str(f, "id"));
        if (it != old.end() && Str(it->second, "text") == Str(f, "text")) {
            items.push_back(it->second);
            old.erase(it);
            continue;
        }
        changed.insert(Str(f, "id"));
        items.push_back(f);
    }
    for (const auto& [id, r] : old) changed.insert(id); // gone
    reqs_["items"] = items;
    json impl = json::array();
    for (const auto& i : reqs_.value("implementations", json::array())) {
        if (!changed.contains(Str(i, "req"))) impl.push_back(i);
    }
    for (const auto& f : fresh) {
        if (!changed.contains(Str(f, "id"))) continue;
        int k = 0;
        for (const auto& w : WhereaboutsItems(f)) {
            impl.push_back({{"id", "iw-" + Str(f, "id") + "-" + std::to_string(++k)}, {"req", Str(f, "id")}, {"how", Str(w, "how")}, {"does", Str(w, "does")},
                            {"by", Str(w, "by")}, {"after", json::array()}, {"status", "todo"}, {"made", json::array()}});
        }
    }
    reqs_["implementations"] = impl;
    SaveRequirements();
    syncStatuses();
    AnnotateCastPackages();
}

// The whereabouts that already hold once everyone is placed (where they start, and never moved after): verified now, as
// Implement's check would find them; the rest stay to Implement (not failed: their quests' moves aren't made yet).
void GameBuild::CheckPlacementRequirements() {
    if (!reqs_.contains("items") || !reqs_["items"].is_array()) return;
    std::set<std::string> verified;
    for (auto& r : reqs_["items"]) {
        if (Str(r, "kind") != "whereabouts") continue;
        const json checks = StaticChecks(r);
        bool all = !checks.empty();
        for (const auto& c : checks) all = all && c.value("ok", json()) == json(true);
        r["checks"] = checks;
        if (all) {
            r["status"] = "verified";
            r.erase("why");
            verified.insert(Str(r, "id"));
        } else if (Str(r, "status") == "verified") {
            r["status"] = "todo";
        }
    }
    for (auto& i : reqs_["implementations"]) {
        if (verified.contains(Str(i, "req")) && Str(i, "status") != "built") i["status"] = "built", i["note"] = "the scenes have it";
    }
    SaveRequirements();
    json p = ReadPlacements(host_.projectRoot);
    for (const char* list : {"characters", "props"}) {
        if (!p.contains(list)) continue;
        for (auto& e : p[list]) {
            for (auto& w : e["whereabouts"]) {
                if (verified.contains(Str(w, "id"))) w["status"] = "verified";
            }
        }
    }
    placements_ = p;
    SavePlacements();
}

json GameBuild::PlacementImplementations(const json& r) const { return WhereaboutsItems(r); }

// --- Its checks ------------------------------------------------------------------------------------------------------

json GameBuild::PlacementStatic(const json& a) {
    const Project project{host_.projectRoot};
    const std::string check = Str(a, "check"), who = Str(a, "who"), scene = Str(a, "scene"), marker = Str(a, "marker");
    json r = {{"check", check}, {"ok", nullptr}};
    auto fail = [&](const std::string& why) { r["ok"] = false, r["why"] = why; };
    Registry reg;
    if (scene.empty() || !LoadScene(reg, project.Resolve(scene).string())) {
        fail("its scene " + scene + " can't be read");
        return r;
    }
    const auto at = marker.empty() ? std::nullopt : PlanMarkerAt(reg, marker);
    auto closeTo = [&](float x, float y, float d) { return at && std::hypot(at->first - x, at->second - y) <= d; };
    if (check == "prop-at") {
        // The thing lying there (an item), else something by its name, else its marker.
        const float px = at ? at->first : a.value("x", 0) + 0.5f, py = at ? at->second : a.value("y", 0) + 0.5f;
        bool there = false;
        if (!Str(a, "item").empty()) {
            for (auto [e, pick, w] : reg.view<ItemPickup, WorldTransform2D>().each()) there = there || (pick.item == Str(a, "item") && std::hypot(w.x - px, w.y - py) <= 3.0f);
            if (there) r["ok"] = true;
            else fail("the " + Str(a, "name") + " (" + Str(a, "item") + ") isn't lying at its spot in " + scene);
            return r;
        }
        const auto want = Words(Str(a, "name"));
        for (auto [e, n, w] : reg.view<Name, WorldTransform2D>().each()) {
            if (reg.all_of<Marker>(e) || std::hypot(w.x - px, w.y - py) > 4.0f) continue;
            const auto has = Words(n.value);
            there = there || std::any_of(want.begin(), want.end(), [&](const std::string& x) { return has.contains(x); });
        }
        if (there || at) r["ok"] = true;
        else fail("nothing called " + Str(a, "name") + " stands at its spot in " + scene);
        return r;
    }
    if (!at) {
        fail("there's no marker " + marker + " in " + scene);
        return r;
    }
    const std::string name = Str(store_.Read("characters/" + PlanSlug(who)).value_or(json::object()), "name");
    auto names = [&](const std::string& said) {
        return !said.empty() && (Lower(said) == Lower(who) || Lower(said) == Lower(name) || Lower(who).rfind(Lower(said) + "-", 0) == 0);
    };
    // Where ⑪ placed them.
    std::string home;
    for (const auto& act : bindings_.value("actors", json::array())) {
        if (Str(act, "character") == who) home = Str(act, "scene");
    }
    auto standsThere = [&] {
        if (home != scene) return false;
        for (auto [e, actor, w] : reg.view<Actor, WorldTransform2D>().each()) {
            if (actor.key == who) return closeTo(w.x, w.y, 3.0f);
        }
        return false;
    };
    if (a.value("start", false)) {
        if (standsThere()) r["ok"] = true;
        else fail((name.empty() ? who : name) + " doesn't stand at " + marker + " in " + scene + " at the start (" + (home.empty() ? std::string("not placed") : "placed in " + home) + ")");
        return r;
    }
    if (!Str(a, "event").empty()) return r; // an event's move: the test tells
    // The quest's stage effects up to then: the last that moves them.
    const std::string quest = Str(a, "quest");
    const json q = ReadJsonFile(project.Resolve("quests/" + quest + ".json"));
    std::vector<std::pair<int, json>> stages;
    for (const auto& s : q.value("stages", json::array())) {
        if (s.contains("index") && s["index"].is_number()) stages.push_back({s["index"].get<int>(), s});
    }
    std::sort(stages.begin(), stages.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    // Up to the stage it begins at (on a stage's completion: as the next one begins).
    int upto = a.value("stage", 0);
    if (Str(a, "on") == "complete") {
        for (const auto& [index, s] : stages) {
            if (index > a.value("stage", 0)) {
                upto = index;
                break;
            }
        }
    }
    std::string movedScene, movedMarker;
    for (const auto& [index, s] : stages) {
        if (index > upto) break;
        for (const auto& e : s.value("onEnter", json::array())) {
            if (Str(e, "op") == "move_actor" && names(Str(e, "who"))) movedScene = Str(e, "scene"), movedMarker = Str(e, "marker");
        }
    }
    if (!movedScene.empty()) {
        if (movedScene == scene && (movedMarker == marker || (PlanMarkerAt(reg, movedMarker) && closeTo(PlanMarkerAt(reg, movedMarker)->first, PlanMarkerAt(reg, movedMarker)->second, 3.0f)))) {
            r["ok"] = true;
        } else {
            fail((name.empty() ? who : name) + " is moved to " + movedMarker + " in " + movedScene + " by then, not " + marker + " in " + scene);
        }
        return r;
    }
    if (standsThere()) {
        r["ok"] = true; // never moved: where ⑪ placed them
        return r;
    }
    // A package of theirs that goes there while the quest is at that stage.
    std::error_code ec;
    for (const auto& f : fs::directory_iterator(project.root / "records" / "package", ec)) {
        if (f.path().filename().string().rfind(PlanSlug(who), 0) != 0) continue;
        const json pk = ReadJsonFile(f.path());
        const json loc = pk.value("location", json::object());
        if (Str(loc, "scene") == scene && Str(loc, "marker") == marker && pk.dump().find(quest) != std::string::npos) {
            r["has"] = f.path().filename().string();
            return r; // there: whether it runs from that stage, a test tells
        }
    }
    fail("nothing brings " + (name.empty() ? who : name) + " to " + marker + " in " + scene + " by " + quest + " stage " + std::to_string(a.value("stage", 0)) +
         ": no stage effect (move_actor) or package does");
    return r;
}

void GameBuild::PlacementPlayed(const json& a, const json& record, json& r) {
    if (Str(a, "check") != "at" || a.value("start", false) || Str(a, "quest").empty()) return;
    for (const auto& s : record.value("stageStates", json::array())) {
        if (Str(s, "quest") != Str(a, "quest") || s.value("stage", -1) < a.value("stage", 0)) continue;
        const json cast = s.value("cast", json::object());
        if (!cast.contains(Str(a, "who"))) return;
        const json me = cast[Str(a, "who")];
        bool ok = Str(me, "scene") == Str(a, "scene");
        std::string why = "in the test they were in " + Str(me, "scene") + " as stage " + std::to_string(s.value("stage", 0)) + " began";
        if (ok && me.contains("x") && a.contains("x")) {
            const double d = std::hypot(me.value("x", 0.0) - (a.value("x", 0) + 0.5), me.value("y", 0.0) - (a.value("y", 0) + 0.5));
            ok = d <= 4.0;
            why = "in the test they stood " + std::to_string(static_cast<int>(d)) + " cells from " + Str(a, "marker");
        }
        r["ok"] = ok;
        r["played"] = true;
        if (ok) r.erase("why");
        else r["why"] = why;
        return;
    }
}

// --- Built by rule -----------------------------------------------------------------------------------------------------

void GameBuild::ImplementWhereabouts(json& item, json& req) {
    const json a = req.value("accept", json::array()).empty() ? json::object() : req["accept"][0];
    const json c = PlacementStatic(a);
    if (c.value("ok", json()) == json(true)) {
        item["status"] = "built", item["note"] = "the scenes and quests have it";
        return;
    }
    const json when = req.value("when", json::object());
    const std::string quest = Str(when, "quest");
    if (Str(a, "check") == "at" && !quest.empty() && Str(when, "event").empty()) {
        // The stage's effect: a move_actor to its marker (on a stage's completion: as the next one begins).
        const Project project{host_.projectRoot};
        const fs::path file = project.Resolve("quests/" + quest + ".json");
        json q = ReadJsonFile(file);
        Registry reg;
        if (q.is_object() && LoadScene(reg, project.Resolve(Str(a, "scene")).string()) && PlanMarkerAt(reg, Str(a, "marker"))) {
            std::vector<int> order;
            for (const auto& s : q.value("stages", json::array())) order.push_back(s.value("index", 0));
            std::sort(order.begin(), order.end());
            int at = when.value("stage", 0);
            if (Str(when, "on") == "complete") {
                const auto next = std::upper_bound(order.begin(), order.end(), at);
                at = next == order.end() ? -1 : *next;
            }
            for (auto& s : q["stages"]) {
                if (at < 0 || s.value("index", -1) != at) continue;
                json& ops = s["onEnter"];
                if (!ops.is_array()) ops = json::array();
                ops.erase(std::remove_if(ops.begin(), ops.end(), [&](const json& e) { return Str(e, "op") == "move_actor" && Str(e, "who") == Str(a, "who"); }), ops.end());
                ops.push_back({{"op", "move_actor"}, {"who", Str(a, "who")}, {"scene", Str(a, "scene")}, {"marker", Str(a, "marker")}});
                std::ofstream(file, std::ios::binary) << q.dump(2);
                files_["quests/" + quest + ".json"] = {{"planner", "quests/" + quest}, {"decision", "placements"}};
                item["status"] = "built";
                item["note"] = quest + " stage " + std::to_string(at) + " moves them to " + Str(a, "marker") + " in " + Str(a, "scene");
                item["made"] = json::array({"quests/" + quest + ".json"});
                return;
            }
        }
    }
    if (c.value("ok", json()).is_null() && c.contains("has")) {
        item["status"] = "built", item["note"] = "a package takes them there (" + Str(c, "has") + "): whether it runs then, a test tells";
        return;
    }
    ToPanel(item, req, c.value("why", std::string()));
}

// --- The user's edit ------------------------------------------------------------------------------------------------

std::string EditPlacement(const fs::path& root, const std::string& kind, const std::string& id, const json& patch, Registry* open,
                          const std::string& openScene, std::string* movedScene) {
    json p = ReadPlacements(root);
    const char* list = kind == "prop" ? "props" : "characters";
    if (!p.contains(list) || !p[list].is_array()) return "there are no placements yet: Replace Characters first";
    json* e = nullptr;
    for (auto& x : p[list]) {
        if (Str(x, "id") == id) e = &x;
    }
    if (!e) return "no placement for " + id;
    const json buildings = p.value("buildings", json::array());
    bool changed = false;
    for (const char* k : {"home", "work"}) {
        if (!patch.contains(k)) continue;
        if (patch[k].is_string() && !patch[k].get<std::string>().empty() && FindIn(buildings, patch[k].get<std::string>()).is_null()) return "no building " + patch[k].get<std::string>();
        (*e)[k] = patch[k].is_string() && !patch[k].get<std::string>().empty() ? patch[k] : json();
        changed = true;
    }
    if (patch.contains("whereabouts") && patch["whereabouts"].is_array()) (*e)["whereabouts"] = patch["whereabouts"], changed = true;
    if (patch.contains("spot") && patch["spot"].is_object()) {
        const json s = patch["spot"];
        json spot = {{"scene", Str(s, "scene").empty() ? Str((*e)["spot"], "scene") : Str(s, "scene")}, {"inside", false}};
        // Its scene, and a cell in it.
        if (!Str(s, "building").empty()) {
            const json b = FindIn(buildings, Str(s, "building"));
            if (b.is_null()) return "no building " + Str(s, "building");
            const bool inside = s.value("inside", false) && !Str(b, "interior").empty();
            spot["building"] = Str(b, "id");
            spot["inside"] = inside;
            spot["scene"] = inside ? Str(b, "interior") : Str(b, "scene");
            if (!inside && b["door"].is_array() && b["door"].size() == 2) spot["x"] = b["door"][0], spot["y"] = b["door"][1];
        }
        const std::string scene = Str(spot, "scene");
        Registry fileReg;
        Registry* reg = open && scene == openScene ? open : &fileReg;
        if (reg == &fileReg && !LoadScene(fileReg, Project{root}.Resolve(scene).string())) return "can't read " + scene;
        const TileGrid grid = TileGrid::Build(*reg);
        if (!Str(s, "marker").empty()) {
            const auto m = PlanMarkerAt(*reg, Str(s, "marker"));
            if (!m) return "no marker " + Str(s, "marker") + " in " + scene;
            spot["marker"] = Str(s, "marker");
            spot["x"] = static_cast<int>(std::floor(m->first)), spot["y"] = static_cast<int>(std::floor(m->second));
        }
        if (s.contains("x") && s.contains("y") && s["x"].is_number() && s["y"].is_number()) {
            spot["x"] = static_cast<int>(std::floor(s["x"].get<double>())), spot["y"] = static_cast<int>(std::floor(s["y"].get<double>()));
        }
        if (!spot.contains("x")) {
            const auto mid = PlanOpenMiddle(*reg, grid);
            if (!mid) return scene + " has nowhere to stand";
            spot["x"] = mid->first, spot["y"] = mid->second;
        }
        if (const auto c = PlanNearestOpen(grid, spot["x"].get<int>(), spot["y"].get<int>(), 6)) spot["x"] = c->first, spot["y"] = c->second;
        const float x = spot["x"].get<int>() + 0.5f, y = spot["y"].get<int>() + 0.5f;
        const std::string oldScene = Str((*e)["spot"], "scene");
        if (kind == "character") {
            const std::string slug = PlanSlug(id), marker = "start-" + slug;
            bool moved = false;
            for (auto [ent, actor, w] : reg->view<Actor, WorldTransform2D>().each()) {
                if (actor.key == id) w.x = x, w.y = y, moved = true;
            }
            if (!moved) {
                // From another scene: taken out of it, made here from its prefab.
                if (!oldScene.empty() && oldScene != scene) {
                    Registry was;
                    Registry* from = open && oldScene == openScene ? open : &was;
                    if (from == open || LoadScene(was, Project{root}.Resolve(oldScene).string())) {
                        std::vector<Entity> gone;
                        for (auto [ent, actor] : from->view<Actor>().each()) {
                            if (actor.key == id) gone.push_back(ent);
                        }
                        for (auto [ent, m] : from->view<Marker>().each()) {
                            if (m.name == marker) gone.push_back(ent);
                        }
                        for (Entity g : gone) from->destroy(g);
                        if (from == &was) SaveScene(was, Project{root}.Resolve(oldScene).string());
                        else if (movedScene) *movedScene = oldScene;
                    }
                }
                PrefabLibrary prefabs{Project{root}};
                if (!prefabs.Instantiate(*reg, "prefabs/characters/" + slug + ".prefab", x, y)) return "prefabs/characters/" + slug + ".prefab can't be placed";
                // The bindings: the scene they live in now.
                json b = ReadJsonFile(root / "planner" / "bindings.json");
                if (b.is_object() && b.contains("actors")) {
                    for (auto& act : b["actors"]) {
                        if (Str(act, "character") == id) act["scene"] = scene;
                    }
                    std::ofstream(root / "planner" / "bindings.json", std::ios::binary) << b.dump(2);
                }
            }
            PlanAddMarker(*reg, marker, x, y);
            spot["own"] = marker;
            // Their start is where they stand now.
            if ((*e)["whereabouts"].is_array() && !(*e)["whereabouts"].empty()) {
                json& w0 = (*e)["whereabouts"][0];
                w0["scene"] = scene, w0["spot"] = spot, w0["marker"] = marker;
            }
        } else {
            // The prop's marker, and what stands at it (the item lying there, the thing by its name).
            const std::string marker = Str((*e)["spot"], "own").empty() ? id : Str((*e)["spot"], "own");
            const auto was = PlanMarkerAt(*reg, marker);
            if (oldScene == scene && was) {
                for (auto [ent, n, w] : reg->view<Name, WorldTransform2D>().each()) {
                    if (std::hypot(w.x - was->first, w.y - was->second) > 1.5f || reg->all_of<Marker>(ent)) continue;
                    const bool ours = (reg->all_of<ItemPickup>(ent) && reg->get<ItemPickup>(ent).item == Str(*e, "item")) || Lower(n.value) == Lower(Str(*e, "name")) ||
                                      reg->all_of<Interactable>(ent);
                    if (ours) w.x = x, w.y = y;
                }
            } else if (!Str(*e, "item").empty()) {
                const Entity it = reg->create();
                reg->emplace<WorldTransform2D>(it, x, y);
                reg->emplace<ItemPickup>(it, Str(*e, "item"), 1, false);
                reg->emplace<Name>(it, Str(*e, "item"));
            }
            PlanAddMarker(*reg, marker, x, y);
            spot["own"] = marker;
        }
        if (reg == &fileReg) SaveScene(fileReg, Project{root}.Resolve(scene).string());
        else if (movedScene) *movedScene = scene;
        (*e)["spot"] = spot;
        (*e)["scene"] = scene;
        changed = true;
    }
    (*e)["locked"] = patch.contains("locked") ? patch.value("locked", true) : (changed ? true : e->value("locked", false));
    if (changed) (*e)["by"] = "user";
    p["updated"] = Now();
    std::ofstream(root / "planner" / "placements.json", std::ios::binary) << p.dump(1);
    return {};
}

} // namespace aige
