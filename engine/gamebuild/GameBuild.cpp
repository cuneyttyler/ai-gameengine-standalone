#include "gamebuild/GameBuild.h"
#include "gamebuild/PlanCast.h"
#include "gamebuild/PlanKeys.h"

#include "assets/AssetDatabase.h"
#include "assets/Catalog.h"
#include "assets/Prefab.h"
#include "assets/Project.h"
#include "assets/SceneFiles.h"
#include "assets/SceneIO.h"
#include "assets/LayoutImage.h"
#include "assets/PackImport.h"
#include "assets/Sourcing.h"
#include "assets/TextureCook.h"
#include "editing/PaintedPlace.h"
#include "editing/Commands.h"
#include "editing/PlaceScene.h"
#include "gameplay/Animation.h"
#include "gameplay/Archetypes.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/GameSession.h"
#include "gameplay/Behaviour.h"
#include "gameplay/Components.h"
#include "gameplay/Cutscene.h"
#include "gameplay/Interiors.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/Items.h"
#include "gameplay/Pathfinding.h"
#include "gameplay/Paths.h"
#include "gameplay/Settings.h"
#include "gameplay/Story.h"
#include "gameplay/TileGrid.h"
#include "gameplay/TileLayer.h"
#include "physics2d/Components.h"
#include "places/Ontology.h"
#include "planner/PlannerDocs.h"
#include "records/Records.h"
#include "scripting/Components.h"
#include "story/StoryOntology.h"
#include "ui/Documents.h"
#include "ui/UiDesign.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <fstream>
#include <set>

namespace aige {

namespace fs = std::filesystem;
using json = nlohmann::json;

// Where a build stage sits in the tabs' order: Continue goes from the furthest one reached.
int BuildStageOrder(const std::string& stage) {
    static const std::vector<std::string> order = {"", "requirements", "build", "characters", "objects", "quests",
        "placements", "behaviour", "scenes", "systems", "visuals", "finish", "features", "playtest", "result", "done"};
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i] == stage) return static_cast<int>(i);
    }
    return -1;
}

namespace {

std::string Text(const json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::optional<std::pair<int, int>> NearestOpen(const TileGrid& grid, int cx, int cy, int radius = 40) {
    for (int r = 0; r <= radius; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                if (!grid.IsSolid(cx + dx, cy + dy)) return std::make_pair(cx + dx, cy + dy);
            }
        }
    }
    return std::nullopt;
}

// Every cell one can walk to from `from` (a step at a time, across open cells, within `x0..x1`, `y0..y1`).
std::set<std::pair<int, int>> ReachableFrom(const TileGrid& grid, std::pair<int, int> from, int x0, int y0, int x1, int y1) {
    std::set<std::pair<int, int>> seen = {from};
    std::vector<std::pair<int, int>> todo = {from};
    while (!todo.empty()) {
        const auto [x, y] = todo.back();
        todo.pop_back();
        for (const auto [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
            const std::pair<int, int> n = {x + dx, y + dy};
            if (n.first < x0 || n.second < y0 || n.first > x1 || n.second > y1 || grid.IsSolid(n.first, n.second) || !seen.insert(n).second) continue;
            todo.push_back(n);
        }
    }
    return seen;
}

// Of `cells`, the one nearest (x, y).
std::optional<std::pair<int, int>> NearestOf(const std::set<std::pair<int, int>>& cells, int x, int y) {
    std::optional<std::pair<int, int>> best;
    long bestD = 0;
    for (const auto& c : cells) {
        const long d = static_cast<long>(c.first - x) * (c.first - x) + static_cast<long>(c.second - y) * (c.second - y);
        if (!best || d < bestD) best = c, bestD = d;
    }
    return best;
}

// The words of a name or text, three letters or more, lowercase, without the ones every room has.
std::set<std::string> NameWords(const std::string& text) {
    static const std::set<std::string> common = {"room", "rooms", "level", "area", "the", "and", "with", "near", "place"};
    std::set<std::string> out;
    std::string w;
    for (const unsigned char c : text + " ") {
        if (std::isalpha(c)) {
            w += static_cast<char>(std::tolower(c));
        } else {
            if (w.size() >= 3 && !common.contains(w)) out.insert(w);
            w.clear();
        }
    }
    return out;
}

// The room of a built place (a dungeon's, a building's) a story anchor is in: the one whose name holds all the anchor's
// words, when only one does ("crypt-gate": crypt-gate-approach); else the one the anchor's module names
// ("entrance-room": entrance); else the one its words say most of. Its middle, in the scene's cells.
std::optional<std::pair<int, int>> RoomOfAnchor(const json& anchor, const json& solved, int originX, int originY) {
    struct Room {
        std::set<std::string> words;
        int x, y;
    };
    std::vector<Room> rooms;
    for (const auto& r : solved.value("regions", json::array())) {
        if (!r.is_object() || r.value("parent", json()).is_null()) continue;
        const json b = r.value("bounds", json());
        if (!b.is_array() || b.size() != 4) continue;
        const std::string id = r.value("id", "");
        rooms.push_back({NameWords(id.substr(id.rfind('/') + 1)), b[0].get<int>() + b[2].get<int>() / 2 + originX, b[1].get<int>() + b[3].get<int>() / 2 + originY});
    }
    if (rooms.empty()) return std::nullopt;
    const auto own = NameWords(anchor.value("id", "")), module = NameWords(anchor.value("module", ""));
    const json relation = anchor.value("relation", json::object());
    const auto text = NameWords(relation.is_object() ? relation.value("place", "") + " " + relation.value("at", "") : std::string());
    auto overlap = [](const std::set<std::string>& a, const std::set<std::string>& b) {
        int n = 0;
        for (const auto& w : a) n += b.contains(w);
        return n;
    };
    // All its own words in one room's name, and only one.
    const Room* only = nullptr;
    int holding = 0;
    for (const auto& r : rooms) {
        if (!own.empty() && overlap(own, r.words) == static_cast<int>(own.size())) only = &r, ++holding;
    }
    if (holding == 1) return std::make_pair(only->x, only->y);
    for (const auto* words : {&module, &text}) {
        const Room* best = nullptr;
        int bestN = 0;
        for (const auto& r : rooms) {
            if (const int n = overlap(*words, r.words); n > bestN) best = &r, bestN = n;
        }
        if (best) return std::make_pair(best->x, best->y);
    }
    return std::nullopt;
}

// The open cells' bounds and middle.
struct Extent {
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    bool any = false;
};

Extent OpenExtent(Registry& registry, const TileGrid& grid) {
    Extent e;
    auto add = [&](int x, int y) {
        if (grid.IsSolid(x, y)) return;
        if (!e.any) e = {x, y, x, y, true};
        e.minX = std::min(e.minX, x);
        e.minY = std::min(e.minY, y);
        e.maxX = std::max(e.maxX, x);
        e.maxY = std::max(e.maxY, y);
    };
    for (auto [entity, tile, at] : registry.view<Tile, WorldTransform2D>().each()) {
        add(static_cast<int>(std::floor(at.x)), static_cast<int>(std::floor(at.y)));
    }
    // A built place's ground is its tile layers (a painted place's: its walkable cells), not tile entities.
    if (const TileLayers* layers = FindLayers(registry)) {
        for (const auto& [name, layer] : layers->layers) layer.ForEach([&](int gx, int gy, uint16_t) { add(gx, gy); });
    }
    return e;
}

Entity AddMarker(Registry& registry, const std::string& name, float x, float y) {
    for (auto [e, m] : registry.view<Marker>().each()) {
        if (m.name == name) {
            registry.get_or_emplace<WorldTransform2D>(e) = {x, y};
            return e;
        }
    }
    const Entity e = registry.create();
    registry.emplace<WorldTransform2D>(e, x, y);
    registry.emplace<Marker>(e, name);
    registry.emplace<Name>(e, "Marker " + name);
    return e;
}

std::optional<std::pair<float, float>> MarkerAt(Registry& registry, const std::string& name) {
    for (auto [e, m, at] : registry.view<Marker, WorldTransform2D>().each()) {
        if (m.name == name) return std::make_pair(at.x, at.y);
    }
    return std::nullopt;
}

// A built place's buildings and things as its plan placed them (layout.solved.json's objects, in scene cells): where a
// character lives, works or keeps watch, and the door one goes in by. A painted place has nothing else that says so.
} // namespace

namespace {

// The kind of plan thing a structure the vision model named is ("a timber house": house; "market stall": stall).
static std::string SlotOfKind(std::string kind) {
    std::transform(kind.begin(), kind.end(), kind.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    static const std::vector<std::pair<std::vector<std::string>, std::string>> kinds = {
        {{"stable"}, "stable"},
        {{"stall", "booth"}, "stall"},
        {{"inn", "tavern"}, "inn"},
        {{"shop", "store", "bakery"}, "shop"},
        {{"smithy", "forge", "workshop", "mill", "barn", "shed"}, "workshop"},
        {{"church", "chapel", "temple", "tower", "shrine"}, "temple"},
        {{"well", "fountain"}, "centrepiece"},
        {{"gate", "arch"}, "gateway-gate"},
        {{"house", "cottage", "home", "hut", "hall"}, "house"},
    };
    for (const auto& [words, slot] : kinds) {
        for (const auto& w : words) {
            if (kind.find(w) != std::string::npos) return slot;
        }
    }
    return {};
}

static std::vector<PlannedThing> PlannedThings(const json& solved, int ox, int oy) {
    std::vector<PlannedThing> things;
    for (const auto& o : solved.value("objects", json::array())) {
        if (!o.is_object() || o.value("slot", "").empty()) continue;
        PlannedThing t{o.value("id", ""), o.value("slot", ""), o.value("facing", "s"), o.value("x", 0) + ox, o.value("y", 0) + oy,
                       std::max(1, o.value("w", 1)), std::max(1, o.value("h", 1))};
        t.area = t.w * t.h;
        things.push_back(t);
    }
    return things;
}

// The cell in front of it, on the side it faces (its door).
static std::pair<int, int> DoorOf(const PlannedThing& t) {
    if (t.hasDoor) return {t.doorX, t.doorY};
    if (t.facing == "n") return {t.x + t.w / 2, t.y - 1};
    if (t.facing == "e") return {t.x + t.w, t.y + t.h / 2};
    if (t.facing == "w") return {t.x - 1, t.y + t.h / 2};
    return {t.x + t.w / 2, t.y + t.h};
}

// A step further out from its door (where one arrives coming out of it).
static std::pair<int, int> OutsideDoor(const PlannedThing& t) {
    auto [x, y] = DoorOf(t);
    if (t.facing == "n") return {x, y - 1};
    if (t.facing == "e") return {x + 1, y};
    if (t.facing == "w") return {x - 1, y};
    return {x, y + 1};
}

// A place's way out as its plan has it: an interior's front door (the gateway just inside it, one arrives a step further
// in, away from the side it faces), else a dungeon's entrance (its stairs, else the middle of its entrance room; one
// arrives a step into the room). {gateway's cell, arrival's cell}; none when the plan has neither.
using WayOut = std::pair<std::pair<int, int>, std::pair<int, int>>;
static std::optional<WayOut> PlannedWayOut(const std::vector<PlannedThing>& things, const TileGrid& grid) {
    auto apart = [&](std::pair<int, int> at, int dx, int dy) -> std::optional<WayOut> {
        for (int step = 2; step <= 4; ++step) {
            if (const auto in = NearestOpen(grid, at.first + dx * step, at.second + dy * step); in && *in != at) return WayOut{at, *in};
        }
        return std::nullopt;
    };
    for (const auto& t : things) {
        if (t.slot != "door" || t.id.find("front-door") == std::string::npos) continue;
        const int dx = t.facing == "w" ? 1 : t.facing == "e" ? -1 : 0, dy = t.facing == "n" ? 1 : t.facing == "s" ? -1 : 0;
        const auto at = NearestOpen(grid, t.x + dx, t.y + dy, 4);
        if (at) return apart(*at, dx, dy);
    }
    const PlannedThing* stairs = nullptr;
    long sx = 0, sy = 0, n = 0;
    for (const auto& t : things) {
        if (t.id.find("entrance/") == std::string::npos) continue;
        if (t.slot == "stairs" && !stairs) stairs = &t;
        sx += t.x, sy += t.y, ++n;
    }
    if (n == 0) return std::nullopt;
    const int mx = static_cast<int>(sx / n), my = static_cast<int>(sy / n);
    const auto at = stairs ? NearestOpen(grid, stairs->x, stairs->y, 6) : NearestOpen(grid, mx, my);
    if (!at) return std::nullopt;
    const int dx = mx > at->first ? 1 : mx < at->first ? -1 : 0, dy = my > at->second ? 1 : my < at->second ? -1 : 0;
    return apart(*at, dx, dy == 0 && dx == 0 ? 1 : dy);
}

// The kinds of building (or thing) some words say one belongs at: the rule of the word said first ("a child who heard a
// bell in the graveyard" lives in a house; "a gravedigger" works at the graves). Empty: they say none.
static std::vector<std::string> SlotsFor(const std::string& text) {
    static const std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>> rules = {
        {{"stable", "groom", "ostler"}, {"stable", "inn"}},
        {{"inn", "tavern", "innkeeper", "barkeep", "taproom", "alehouse"}, {"inn"}},
        {{"forge", "smithy", "blacksmith", "carpenter", "workshop", "mill", "miller", "weaver", "tanner", "potter", "cooper"}, {"workshop", "shop"}},
        {{"shop", "store", "apothecary", "alchemist", "bakery", "baker", "merchant", "vendor", "trader", "weapon", "weaponsmith", "armor",
          "armour", "arms", "potion", "healer", "herbalist", "butcher"},
         {"shop", "workshop"}},
        {{"temple", "chapel", "church", "priest", "shrine", "monastery", "cleric"}, {"temple"}},
        {{"grave", "gravedigger", "graveyard", "cemetery", "crypt", "tomb", "burial"}, {"grave"}},
        {{"farm", "farmer", "field", "crop", "orchard"}, {"crop"}},
        {{"well", "square", "market", "record", "board", "elder", "fountain", "plaza"}, {"centrepiece", "stall"}},
        {{"gate", "boundary", "guard", "watch", "captain", "wall", "fence", "sentry"}, {"gateway-gate"}},
        {{"home", "house", "cottage", "hut", "child", "villager", "widow"}, {"house", "townhouse"}},
    };
    auto ruleOf = [&](const std::string& w) -> const std::vector<std::string>* {
        for (const auto& [words, slots] : rules) {
            for (const auto& k : words) {
                if (w == k || w == k + "s" || (k.size() >= 5 && w.find(k) != std::string::npos)) return &slots;
            }
        }
        return nullptr;
    };
    std::string word;
    for (size_t i = 0; i <= text.size(); ++i) {
        const char c = i < text.size() ? static_cast<char>(std::tolower(static_cast<unsigned char>(text[i]))) : ' ';
        if (c >= 'a' && c <= 'z') {
            word += c;
            continue;
        }
        if (!word.empty()) {
            if (const auto* slots = ruleOf(word)) return *slots;
        }
        word.clear();
    }
    return {};
}

// planner/gateways.json (development_plan.md 32.12): each way through as it was settled, by "<scene>><scene it goes to>":
// its cell, where one arrives coming back, and what shows it (a painted door or gate the vision model found: "seen"; art
// drawn for it: "art"). Kept when the scenes are made again (characters placed again, ⑩ again).
static json LoadGateways(const fs::path& plannerDir) {
    std::ifstream in(plannerDir / "gateways.json", std::ios::binary);
    const json g = in ? json::parse(in, nullptr, false) : json();
    return g.is_object() ? g : json::object();
}

static void SaveGateways(const fs::path& plannerDir, const json& g) {
    std::ofstream(plannerDir / "gateways.json", std::ios::binary) << g.dump(1);
}

static std::string GatewayKey(const std::string& from, const std::string& to) { return from + ">" + to; }

// A PNG's size from its header; {0, 0} when it isn't one.
static std::pair<int, int> PngSize(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    unsigned char h[24] = {};
    if (!in.read(reinterpret_cast<char*>(h), 24) || h[1] != 'P' || h[2] != 'N' || h[3] != 'G') return {0, 0};
    auto be = [&](int i) { return (h[i] << 24) | (h[i + 1] << 16) | (h[i + 2] << 8) | h[i + 3]; };
    return {be(16), be(20)};
}

// What shows a gateway: its art standing on its cell; a painted door or gate (a faint glow on the cell before it, so
// it's seen to be a way in); else the placeholder's blue.
static void DressGateway(Registry& registry, Entity e, const json& entry, const fs::path& root) {
    auto& s = registry.get_or_emplace<Sprite>(e);
    const std::string art = entry.value("art", "");
    if (!art.empty()) {
        const auto [w, h] = PngSize(root / art);
        const float height = entry.value("height", 96.0f);
        s = Sprite{};
        s.texture = art;
        s.height = height;
        s.width = h > 0 ? height * static_cast<float>(w) / static_cast<float>(h) : height;
    } else if (!entry.value("seen", "").empty()) {
        s.a = 0.35f;
    }
}

// Whether a place's "who" (an actor's character, a resident: "character/bera", "Bera", a full id) is this character.
static bool NamesCharacter(const std::string& who, const std::string& id, const std::string& name) {
    if (who.empty()) return false;
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    const std::string w = lower(who), i = lower(id), n = lower(name);
    if (w == i || w == n) return true;
    if (i.rfind(w + "-", 0) == 0) return true; // character/bera for character/bera-ash
    const std::string first = n.substr(0, n.find(' '));
    return !first.empty() && (w == first || w == "character/" + first);
}

// Objective actions as the engine's quest objective types.
std::string ObjectiveType(const std::string& action) {
    static const std::map<std::string, std::string> types = {
        {"talk", "talk"},       {"goto", "reach"},       {"reach", "reach"},     {"kill", "defeat"},     {"defeat-count", "defeat"},
        {"gather", "collect"},  {"collect", "collect"},  {"deliver", "deliver"}, {"use", "use"},         {"interact", "use"},
        {"escort", "escort"},   {"survive", "survive"},  {"examine", "reach"},   {"spy", "reach"},       {"stealth", "reach"},
        {"defend", "survive"},  {"capture", "defeat"},   {"free", "use"},        {"craft", "have"},      {"wait", "survive"},
        {"choose", "talk"}};
    auto it = types.find(action);
    return it == types.end() ? "reach" : it->second;
}

} // namespace

// For Placements.cpp: the same rules ⑪ places by.
std::pair<int, int> PlanDoorOf(const PlannedThing& t) { return DoorOf(t); }
std::vector<std::string> PlanSlotsFor(const std::string& text) { return SlotsFor(text); }
bool PlanNamesCharacter(const std::string& who, const std::string& id, const std::string& name) { return NamesCharacter(who, id, name); }
std::optional<std::pair<int, int>> PlanNearestOpen(const TileGrid& grid, int cx, int cy, int radius) { return NearestOpen(grid, cx, cy, radius); }
std::optional<std::pair<float, float>> PlanMarkerAt(Registry& registry, const std::string& name) { return MarkerAt(registry, name); }
void PlanAddMarker(Registry& registry, const std::string& name, float x, float y) { AddMarker(registry, name, x, y); }
std::optional<std::pair<int, int>> PlanOpenMiddle(Registry& registry, const TileGrid& grid) {
    const Extent e = OpenExtent(registry, grid);
    if (!e.any) return std::nullopt;
    return NearestOpen(grid, (e.minX + e.maxX) / 2, (e.minY + e.maxY) / 2);
}

json EngineQuestFromDesign(const json& design, const json& bindings, const std::function<std::string(const std::string&)>& sceneOf,
                           const json& staging) {
    json quest = {{"name", design.value("name", design.value("id", ""))}, {"description", design.value("rationale", "")}};
    std::map<std::string, std::string> fills; // alias -> what the game knows it by
    std::map<std::string, json> boundOf;      // alias -> its binding (⑪): its marker, entity, item, thing
    std::map<std::string, std::string> kindOf;
    for (const auto& a : design.value("aliases", json::array())) {
        std::string fill = a.value("fill", "");
        kindOf[a.value("id", "")] = a.value("kind", "");
        for (const auto& b : bindings.value("aliases", json::array())) {
            if (b.value("quest", "") == design.value("id", "") && b.value("alias", "") == a.value("id", "")) {
                boundOf[a.value("id", "")] = b;
                if (!b.value("marker", "").empty()) fill = b.value("marker", "");
                else if (!b.value("entity", "").empty()) fill = b.value("entity", "");
            }
        }
        fills[a.value("id", "")] = fill;
    }
    json aliases = json::array();
    for (const auto& a : design.value("aliases", json::array())) {
        const std::string kind = a.value("kind", "character");
        aliases.push_back({{"id", a.value("id", "")},
                           {"kind", kind == "spot" || kind == "container" ? "entity" : kind},
                           {"fill", fills[a.value("id", "")]}});
    }
    quest["aliases"] = aliases;
    const std::string giverAlias = design.value("giver", json::object()).value("alias", "");
    if (!giverAlias.empty() && fills.count(giverAlias)) quest["giver"] = fills[giverAlias];
    json stages = json::array();
    for (const auto& s : design.value("stages", json::array())) {
        json stage = {{"index", s.value("index", 10)}, {"journal", s.value("journal", "")}};
        json objectives = json::array();
        int k = 0;
        const json designed = s.value("objectives", json::array());
        for (const auto& o : designed) {
            std::string type = ObjectiveType(o.value("action", "reach"));
            const std::string target = o.value("target", "");
            const json b = boundOf.count(target) ? boundOf[target] : json::object();
            json obj = {{"id", o.value("id", "o" + std::to_string(s.value("index", 10)) + "-" + std::to_string(++k))},
                        {"text", o.value("text", s.value("journal", ""))}};
            if (!target.empty()) obj["target"] = "@" + target;
            // What the game counts, from what ⑪ made for the role: an item is had (picked up, it's in the bag; "collect"
            // counts prefabs picked up), handed over to whom the stage talks to (else the giver); a thing is used by its name.
            const std::string item = b.value("item", "");
            if (!item.empty() && (type == "collect" || type == "have")) {
                type = "have";
                obj["target"] = item;
            } else if (type == "deliver" && kindOf[target] == "item") {
                std::string to = o.value("to", "");
                for (const auto& x : designed) {
                    if (to.empty() && x.value("action", "") == "talk") to = x.value("target", "");
                }
                if (to.empty()) to = design.value("giver", json::object()).value("alias", "");
                obj["target"] = "@" + to;
                obj["item"] = item.empty() ? PlanSlug(target) : item;
            } else if ((type == "use" || type == "examine") && !b.value("thing", "").empty()) {
                obj["target"] = b.value("thing", "");
            } else if (type == "defeat" && !b.value("entity", "").empty() && !b.value("marker", "").empty()) {
                obj["target"] = b.value("entity", ""); // one made for it: by its key, not where it stands
            }
            obj["type"] = type;
            if (o.value("count", 1) > 1) obj["count"] = o.value("count", 1);
            objectives.push_back(obj);
        }
        if (!objectives.empty()) stage["objectives"] = objectives;
        // What happens as the stage begins (development_plan.md 32.5): characters moved, shown, hidden, turned;
        // enemies spawned; items given. Aliases become what the game knows them by.
        json onEnter = json::array();
        auto rawFill = [&](const std::string& alias) -> std::string {
            for (const auto& a : design.value("aliases", json::array())) {
                if (a.value("id", "") == alias) return a.value("fill", "").empty() ? a.value("in", "") : a.value("fill", "");
            }
            return alias;
        };
        for (const auto& e : s.value("onEnter", json::array())) {
            if (!e.is_object() || e.value("op", "").empty()) continue;
            json out = e;
            if (e.contains("who") && e["who"].is_string()) out["who"] = rawFill(e.value("who", "")); // a character's key
            const std::string op = e.value("op", "");
            if (op == "move_actor") {
                const std::string place = rawFill(e.value("to", ""));
                if (sceneOf && !place.empty()) out["scene"] = sceneOf(place);
                out["marker"] = e.value("anchor", std::string("arrive"));
                out.erase("to");
                out.erase("anchor");
            } else if (op == "spawn") {
                // {class, at: a spot alias or anchor, count}: the class's prefab at the spot's marker.
                if (!e.value("class", "").empty()) out["prefab"] = "prefabs/enemies/" + e.value("class", "") + ".prefab";
                const std::string at = e.value("at", "");
                out["marker"] = fills.count(at) ? fills[at] : at;
                out.erase("class");
                out.erase("at");
            } else if (op == "give_to") {
                out["item"] = PlanSlug(e.value("item", ""));
            }
            onEnter.push_back(out);
        }
        // The story's staging (GameBuild::StoryStaging): the scene's cast brought to where it happens, and sent home after.
        for (const auto& op : staging.is_object() ? staging.value(std::to_string(s.value("index", 10)), json::array()) : json::array()) {
            if (std::find(onEnter.begin(), onEnter.end(), op) == onEnter.end()) onEnter.push_back(op);
        }
        if (!onEnter.empty()) stage["onEnter"] = onEnter;
        const std::string terminal = s.value("terminal", "");
        if (terminal == "completed" || terminal == "complete") stage["ends"] = "complete";
        if (terminal == "failed" || terminal == "fail") stage["ends"] = "fail";
        stages.push_back(stage);
    }
    quest["stages"] = stages;
    json rewards = json::object();
    std::string items;
    for (const auto& r : design.value("rewards", json::array())) {
        if (r.contains("xp")) rewards["experience"] = r["xp"];
        if (r.contains("gold")) rewards["gold"] = r["gold"];
        if (r.contains("item")) items += (items.empty() ? "" : ", ") + PlanSlug(r.value("item", ""));
    }
    if (!items.empty()) rewards["items"] = items;
    if (!rewards.empty()) quest["rewards"] = rewards;
    return quest;
}

// --- The build ------------------------------------------------------------------------------------

GameBuild::GameBuild(Host host) : host_(std::move(host)), store_(host_.projectRoot) {}
GameBuild::~GameBuild() = default;

void GameBuild::Record(const std::string& kind, const std::string& subject, const std::string& chosen, const std::string& reason) {
    PlanDecision d;
    d.stage = stage_;
    d.actor = "planner";
    d.kind = kind;
    d.subject = subject;
    d.chosen = chosen;
    d.reason = reason;
    store_.Decide(d);
}

std::string GameBuild::Start(const std::string& mode) {
    const json state = store_.State();
    if (state.value("baseline", json()).is_null()) return "the design has no baseline yet: validate it first (⑨)";
    const json brief = store_.Read("brief").value_or(json::object());
    mode_ = mode.empty() ? BriefGet(brief, "plan.building").is_string() ? BriefGet(brief, "plan.building").get<std::string>() : "automatic" : mode;
    if (mode_ == "confirm-per-place" || mode_ == "ask-per-place") mode_ = "manual";
    rows_.clear();
    jobs_.clear();
    builds_.clear();
    pending_.clear();
    bindings_ = {{"schema", "aige.bindings/1"}, {"aliases", json::array()}, {"actors", json::array()}};
    files_ = json::object();
    playtest_ = json::object();
    check_ = json::object(); // validated again once it's built
    SaveCheck();
    reqs_ = json::object(); // made again, first
    SaveRequirements();
    rebuildOnly_ = false;
    stage_ = "requirements";
    reached_ = "requirements"; // a fresh build: Continue starts at the top
    subStep_.clear();
    status_ = "working";
    message_ = "Requirements: reading the plan…";
    MakeBuildPlan();
    Record("build-plan", "buildplan", std::to_string(rows_.size()) + " places, " + mode_);
    Save();
    return {};
}

void GameBuild::Stop() {
    for (auto& r : rows_) {
        if (r.status == "building") r.status = "waiting";
        r.inEditor = false;
    }
    builds_.clear();
    pending_.clear();
    // The requirements (or a tab's items) being made are stopped too: resumable, not left "making", which
    // would keep the Requirements tab's buttons disabled and stop them being made again.
    if (reqs_.value("status", "") == "making" || reqs_.value("implementing", false)) {
        reqs_.erase("implementing");
        reqs_["status"] = "stopped";
        for (const char* list : {"scenes", "implScenes"}) {
            if (!reqs_.contains(list) || !reqs_[list].is_array()) continue;
            for (auto& s : reqs_[list]) {
                if (s.value("status", "") == "asking") s["status"] = "queued";
            }
        }
        if (reqs_.contains("implementations") && reqs_["implementations"].is_array()) {
            for (auto& i : reqs_["implementations"]) {
                if (i.value("status", "") == "building") i["status"] = "todo";
            }
        }
        reqsOnly_ = false; // Continue goes back to the build's own progress, not a requirements-only run
        SaveRequirements();
    }
    status_ = "stopped";
    message_ = "Stopped: Continue building goes on from the step it was at; Restart builds every place again.";
    Save();
}

std::string GameBuild::Open() {
    if (!fs::exists(store_.Dir() / "buildstate.json")) return "nothing is being built";
    Load();
    pending_.clear();
    // What was waiting on the AI when the editor closed: ready to go again from where it was.
    ResetInFlight();
    // A build that was waiting on the AI reopens where it was (PlaceBuild::Open).
    for (auto& r : rows_) {
        if (r.status == "building" && !r.build.empty()) {
            // Past its plan (its art being found, its scene being built) when the editor closed: the
            // Build panel finishes it, from where it was.
            std::ifstream sessionIn(host_.projectRoot / "places" / r.build / "session.json", std::ios::binary);
            const json session = json::parse(sessionIn, nullptr, false);
            const std::string stage = session.is_object() && session.value("stage", json()).is_string() ? session["stage"].get<std::string>() : "";
            // Built by then: it's built (its scene as the Build panel saved it), not handed over again.
            std::error_code ec;
            if (stage == "built" && fs::exists(Project{host_.projectRoot}.Resolve(r.scene), ec)) {
                try {
                    FinishInEditor(r);
                    continue;
                } catch (const std::exception&) {
                    // its scene doesn't hold it: finished again in the Build panel below
                }
            }
            const bool pastPlan = stage == "planned" || stage == "sourcing" || stage == "sourced" || stage == "building" || stage == "built";
            if (pastPlan && host_.buildInEditor) {
                r.resumeInEditor = true;
                continue;
            }
            auto build = std::make_unique<PlaceBuild>(BuildHost(r.place));
            if (build->Open(r.build).empty()) builds_[r.place] = std::move(build);
            else r.status = "waiting";
        }
    }
    // Opened, it waits for you: a build that was going when the editor closed is stopped, and Continue building goes on
    // from where it was.
    if (status_ == "working") {
        status_ = "stopped";
        message_ = "Stopped when the editor closed: Continue building goes on from where it was.";
    }
    // The requirements left "making" by a stop: resumable, so the Requirements tab's buttons stay usable.
    if (reqs_.value("status", "") == "making" && status_ != "working") {
        reqs_.erase("implementing");
        reqs_["status"] = "stopped";
        reqsOnly_ = false;
        SaveRequirements();
    }
    Save();
    return {};
}

void GameBuild::ResetInFlight() {
    for (auto& j : jobs_) {
        if (j.status == "running") j.status = "queued";
    }
    // A gameplay step the AI panel was carrying out: its answer was lost, so it goes again.
    for (auto& f : features_) {
        if (f.value("status", "") == "running") f["status"] = "queued";
    }
    if (reqs_.contains("implementations") && reqs_["implementations"].is_array()) {
        for (auto& i : reqs_["implementations"]) {
            if (i.value("status", "") == "building") i["status"] = "todo";
        }
    }
    // A scene the AI was reading for the requirements: read again; the assets' merge asked again.
    for (const char* list : {"scenes", "implScenes"}) {
        if (reqs_.contains(list) && reqs_[list].is_array()) {
            for (auto& s : reqs_[list]) {
                if (s.value("status", "") == "asking") s["status"] = "queued";
            }
        }
    }
    if (reqs_.value("phase", "") == "assets") reqs_["phase"] = "scenes";
    SaveRequirements();
    // A drawing (a clip of it) half-way: drawn again from the clip it was on.
    for (auto& a : art_) {
        if (a.value("status", "") == "drawing") a["status"] = "queued";
        a["inFlight"] = false;
        if (a.value("status", "") == "queued") a.erase("doing");
    }
    // A scene being validated or its test judged: asked again.
    bool checkChanged = false;
    if (check_.contains("scenes") && check_["scenes"].is_array()) {
        for (auto& r : check_["scenes"]) {
            if (r.value("status", "") == "asking") r["status"] = "queued", checkChanged = true;
        }
    }
    if (check_.contains("tests") && check_["tests"].is_object()) {
        for (auto& [scene, t] : check_["tests"].items()) {
            if (t.value("status", "") == "judging") t["status"] = "queued", checkChanged = true; // (one played is stopped with the editor: LoadCheck)
        }
    }
    if (checkChanged) SaveCheck();
    // Visuals' panels half-made (an image being drawn, the document being written): paused, gone on with on Continue
    // (the images made are kept).
    json visuals = VisualsState();
    bool visualsChanged = false;
    if (visuals.contains("panels") && visuals["panels"].is_object()) {
        for (auto& [id, p] : visuals["panels"].items()) {
            const std::string st = p.value("status", "");
            if (st == "images" || st == "writing") {
                p["status"] = "paused", p["doing"] = "paused: Continue building goes on from here";
                visualsChanged = true;
            }
        }
    }
    if (visualsChanged) SaveVisualsState(visuals);
    // A prop whose states or animations were being drawn: paused, made again on Continue.
    json world = WorldState();
    bool worldChanged = false;
    if (world.contains("objects") && world["objects"].is_object()) {
        for (auto& [id, m] : world["objects"].items()) {
            if (m.is_object() && (m.value("status", "") == "drawing" || m.value("status", "") == "building")) m["status"] = "paused", worldChanged = true;
        }
    }
    if (worldChanged) SaveWorldState(world);
    // A tab's revision asked for: its answer is lost; said again by the user.
    {
        std::ifstream in(store_.Dir() / "build-revisions.json", std::ios::binary);
        json history = in ? json::parse(in, nullptr, false) : json();
        in.close();
        bool changed = false;
        if (history.is_object()) {
            for (auto& [tab, list] : history.items()) {
                if (!list.is_array()) continue;
                for (auto& r : list) {
                    if (r.value("status", "") == "running") r["status"] = "failed", r["error"] = "stopped before its answer came: ask again", changed = true;
                }
            }
        }
        if (changed) std::ofstream(store_.Dir() / "build-revisions.json", std::ios::binary) << history.dump(1);
    }
    // The placements being decided: decided again once everyone is placed.
    if (placeStep_ == "decide") placeStep_.clear(), placeStatus_.clear(), placeAskedForPopulate_ = false;
}

void GameBuild::RestartPaused() {
    if (!host_.request) return;
    const json visuals = VisualsState().value("panels", json::object());
    for (const auto& [id, p] : visuals.items()) {
        if (p.value("status", "") == "paused") NextVisualStep(id);
    }
    const json objects = WorldState().value("objects", json::object());
    for (const auto& [id, m] : objects.items()) {
        if (m.is_object() && m.value("status", "") == "paused") BuildObjects(id);
    }
}

void GameBuild::ServiceLost() {
    // Everything else asked is lost too: dropped, its answers (when they come) ignored.
    pending_.clear();
    // A place being built here: let go of, reopened from where it was (its session) on Continue.
    for (auto& r : rows_) {
        if (r.status == "building" && !r.inEditor && builds_.contains(r.place)) {
            builds_.erase(r.place);
            r.status = "waiting";
        }
    }
    ResetInFlight();
    if (status_ == "working") {
        status_ = "stopped";
        message_ = "The AI service stopped: start it, then Continue building goes on from where it was.";
        Record("stop", "ai-service", "lost", "the AI service stopped answering");
    }
    Save();
}

std::string GameBuild::Resume() {
    if (rows_.empty()) return "nothing to continue: start the build";
    if (status_ == "working" && !reqsOnly_) return {};
    reqsOnly_ = false; // Continue: the build goes on (requirements still being made are waited for)
    // Continue goes from the furthest step reached: a step run again while stopped (the requirements, a tab's
    // items) never moves the resume point back. A single place being rebuilt is the exception: it finishes here.
    if (!rebuildOnly_ && BuildStageOrder(reached_) > BuildStageOrder(stage_)) {
        stage_ = reached_;
        subStep_.clear();
    }
    // What a stop left half-way (its answer dropped with it): asked again, not waited on for ever.
    if (pending_.empty()) {
        ResetInFlight();
        RestartPaused();
    }
    // Stopped while the AI was deciding the placements before everyone was placed: placed first, then asked again.
    if (stage_ == "placements" && subStep_.empty() && placeStep_ == "decide") placeStep_.clear(), placeStatus_.clear(), placeAskedForPopulate_ = false;
    // Quest roles not found (⑪): the characters placed and the roles bound again, before anything else goes on.
    const json aliases = bindings_.value("aliases", json::array());
    static const std::set<std::string> kBeforePlaced = {"", "requirements", "build", "characters", "objects", "quests", "placements"};
    if (!kBeforePlaced.contains(stage_) &&
        std::any_of(aliases.begin(), aliases.end(), [](const json& a) { return a.value("how", "") == "failed"; })) {
        placeAgain_ = true;
    }
    // The ways through not shown yet (a game built before them): onto the paintings' doors, art for the others.
    if (!kBeforePlaced.contains(stage_)) {
        if (const int added = QueueGateways(); added > 0) {
            artApproved_ = true; // continuing is asking for them
            Record("art", "gateways", std::to_string(added) + " ways through to show", "continue");
        }
    }
    // Gameplay steps that failed go again (from ⑫ on: the playtest and result after them are made again too).
    int retried = 0;
    for (auto& f : features_) {
        if (f.value("status", "") != "failed") continue;
        f["status"] = "queued";
        f["error"] = "";
        f["tries"] = f.value("tries", 0) + 1;
        ++retried;
    }
    if ((retried > 0 || placeAgain_) && (stage_ == "playtest" || stage_ == "result" || stage_ == "done")) stage_ = retried > 0 ? "features" : "playtest";
    if (retried > 0 || placeAgain_) {
        status_ = "working";
        message_ = placeAgain_ ? std::string("⑪ Placing the characters and finding the quests' roles again…")
                               : "⑫ Trying " + std::to_string(retried) + " gameplay step" + (retried == 1 ? "" : "s") + " again…";
        Record("retry", "gameplay", std::to_string(retried) + " failed steps");
        Save();
        return {};
    }
    const bool drawing = std::any_of(art_.begin(), art_.end(), [](const json& a) { return a.value("status", "") == "queued"; });
    if (stage_ == "done" && drawing) {
        status_ = "working";
        message_ = "⑩ Finding the buildings' doors and drawing the ways through…";
        Save();
        return {};
    }
    if (stage_ == "done") return "the game is built: Restart builds to build it again";
    // Waiting after the places: on to connecting them.
    if (stage_ == "build" && std::all_of(rows_.begin(), rows_.end(), [](const Row& r) { return r.status == "built" || r.status == "placeholder"; })) {
        placesApproved_ = true;
    }
    status_ = "working";
    message_ = stage_ == "build" ? "⑩ Carrying on with the places…" : "Carrying on…";
    Save();
    return {};
}

void GameBuild::Save() {
    json rows = json::array();
    for (const auto& r : rows_) {
        rows.push_back({{"place", r.place}, {"scene", r.scene}, {"buildType", r.buildType}, {"prompt", r.prompt}, {"mode", r.mode},
                        {"status", r.status}, {"build", r.build}, {"error", r.error}, {"dependsOn", r.dependsOn}, {"brief", r.brief},
                        {"checks", r.checks}, {"sections", r.sections}, {"paintApproved", r.paintApproved}});
    }
    json jobs = json::array();
    for (const auto& j : jobs_) {
        jobs.push_back({{"id", j.id}, {"kind", j.kind}, {"subject", j.subject}, {"status", j.status}, {"error", j.error}, {"attempts", j.attempts}});
    }
    std::ofstream(store_.Dir() / "buildplan.json", std::ios::binary) << json{{"schema", "aige.build-plan/1"}, {"mode", mode_}, {"rows", rows}}.dump(2);
    std::ofstream(store_.Dir() / "buildstate.json", std::ios::binary)
        << json{{"stage", stage_}, {"status", status_}, {"message", message_}, {"mode", mode_}, {"jobs", jobs}, {"nextJob", nextJob_},
                {"reached", reached_},
                {"bindings", bindings_}, {"files", files_}, {"playtest", playtest_}, {"features", features_}, {"art", art_}, {"keepArt", keepArt_}, {"requirementsOnly", reqsOnly_}, {"rebuildOnly", rebuildOnly_}, {"subStep", subStep_},
                {"artApproved", artApproved_}, {"placesApproved", placesApproved_}, {"placeStep", placeStep_}, {"placeStatus", placeStatus_},
                {"placeWhy", placeWhy_}}
               .dump(2);
}

void GameBuild::Load() {
    std::ifstream in(store_.Dir() / "buildstate.json", std::ios::binary);
    const json s = json::parse(in, nullptr, false);
    std::ifstream planIn(store_.Dir() / "buildplan.json", std::ios::binary);
    const json plan = json::parse(planIn, nullptr, false);
    if (s.is_discarded()) return;
    stage_ = s.value("stage", "");
    status_ = s.value("status", "");
    message_ = s.value("message", "");
    mode_ = s.value("mode", "automatic");
    // A build from before the furthest-stage marker: its stage is the furthest reached.
    reached_ = s.value("reached", stage_);
    nextJob_ = s.value("nextJob", 1);
    bindings_ = s.value("bindings", json::object());
    files_ = s.value("files", json::object());
    playtest_ = s.value("playtest", json::object());
    features_ = s.value("features", json::array());
    art_ = s.value("art", json::array());
    artApproved_ = s.value("artApproved", false);
    keepArt_ = s.value("keepArt", false);
    reqsOnly_ = s.value("requirementsOnly", false);
    rebuildOnly_ = s.value("rebuildOnly", false);
    subStep_ = s.value("subStep", "");
    // A build from before the Build tabs' order: its stage by its tab's name now.
    static const std::map<std::string, std::string> renamed = {{"art", "characters"}, {"populate", "placements"}, {"write", "behaviour"},
                                                              {"implement", "finish"}};
    if (const auto it = renamed.find(stage_); it != renamed.end()) {
        stage_ = it->second;
        if (stage_ == "characters") subStep_ = "drawing"; // its art is queued already
        if (stage_ == "behaviour" && !s.value("jobs", json::array()).empty()) subStep_ = "writing";
    }
    placesApproved_ = s.value("placesApproved", false);
    // Placements being decided when the editor closed: asked again (the AI's answer was lost with it).
    placeStep_ = s.value("placeStep", "");
    if (placeStep_ == "waiting" || placeStep_ == "apply") placeStep_ = "decide";
    placeStatus_ = s.value("placeStatus", "");
    placeWhy_ = s.value("placeWhy", "");
    for (auto& a : art_) {
        if (a.value("inFlight", false)) a["inFlight"] = false; // its answer was lost with the editor: asked again
    }
    jobs_.clear();
    for (const auto& j : s.value("jobs", json::array())) {
        Job job;
        job.id = j.value("id", "");
        job.kind = j.value("kind", "");
        job.subject = j.value("subject", "");
        job.status = j.value("status", "queued");
        job.error = j.value("error", "");
        job.attempts = j.value("attempts", 0);
        jobs_.push_back(job);
    }
    rows_.clear();
    if (!plan.is_discarded()) {
        for (const auto& r : plan.value("rows", json::array())) {
            Row row;
            row.place = r.value("place", "");
            row.scene = r.value("scene", "");
            row.buildType = r.value("buildType", "");
            row.prompt = r.value("prompt", "");
            row.mode = r.value("mode", "automatic");
            row.status = r.value("status", "waiting");
            row.build = r.value("build", "");
            row.error = r.value("error", "");
            row.brief = r.value("brief", json::object());
            row.checks = r.value("checks", json::object());
            for (const auto& d : r.value("dependsOn", json::array())) row.dependsOn.push_back(d.get<std::string>());
            for (const auto& d : r.value("sections", json::array())) row.sections.push_back(d.get<std::string>());
            row.paintApproved = r.value("paintApproved", false);
            rows_.push_back(std::move(row));
        }
    }
    LoadCheck();
    LoadRequirements();
    // (Validating, testing or making the requirements when the editor closed: Continue building goes on with it.)
}

GameBuild::Row* GameBuild::FindRow(const std::string& place) {
    for (auto& r : rows_) {
        if (r.place == place) return &r;
    }
    // An open world's section: built in the world's scene.
    for (auto& r : rows_) {
        if (std::find(r.sections.begin(), r.sections.end(), place) != r.sections.end()) return &r;
    }
    return nullptr;
}

std::string GameBuild::StartScene() const {
    for (const auto& r : rows_) {
        const StoryGraph g = StoryGraph::Load(StoryGraph::PathIn(host_.projectRoot)).value_or(StoryGraph{});
        if (const StoryNode* n = g.Find(r.place); n && n->Flag("start")) return r.scene;
        // A world starts where its starting section is.
        for (const auto& s : r.sections) {
            if (const StoryNode* n = g.Find(s); n && n->Flag("start")) return r.scene;
        }
    }
    return rows_.empty() ? std::string() : rows_.front().scene;
}

bool GameBuild::Owns(int requestId) const { return pending_.contains(requestId); }

void GameBuild::OnResponse(int requestId, const json& body) {
    auto it = pending_.find(requestId);
    if (it == pending_.end()) return;
    const std::string who = it->second;
    pending_.erase(it);
    if (body.is_object() && body.value("unreachable", false)) {
        ServiceLost();
        return;
    }
    // Kept for Pump (which may be heavy): the build's answers and the jobs'.
    if (who.rfind("build:", 0) == 0) buildAnswers_[who.substr(6)].push_back({requestId, body});
    else if (who.rfind("job:", 0) == 0) jobAnswers_.push_back({who.substr(4), body});
    else if (who.rfind("art:", 0) == 0) artAnswers_.push_back({std::atoi(who.c_str() + 4), body});
    else if (who.rfind("check:", 0) == 0) checkAnswers_.push_back({std::atoi(who.c_str() + 6), body});
    else if (who.rfind("test:", 0) == 0) testAnswers_.push_back({who.substr(5), body});
    else if (who.rfind("req:", 0) == 0) reqAnswers_.push_back({who.substr(4), body});
    else if (who.rfind("impl:", 0) == 0) implAnswers_.push_back({who.substr(5), body});
    else if (who == "place:") placeAnswers_.push_back(body); // the placements decided (Placements.cpp)
    else if (who.rfind("rev:", 0) == 0) reviseAnswers_.push_back({who.substr(4), body}); // a Build tab's revision
    else if (who.rfind("vis:", 0) == 0) visualAnswers_.push_back({who.substr(4), body}); // Build ▸ Visuals
    else if (who.rfind("obj:", 0) == 0) objectAnswers_.push_back({who.substr(4), body}); // Build ▸ Props & objects: a state's picture
}

PlaceBuild::Host GameBuild::BuildHost(const std::string& place) {
    PlaceBuild::Host h;
    h.ontology = host_.places;
    const fs::path root = host_.projectRoot;
    auto places = host_.places;
    h.catalog = [root, places] {
        AssetDatabase assets{Project{root}};
        assets.Refresh();
        return BuildCatalog(assets, &places(), {});
    };
    h.isoGrid = [] { return ActiveSettings().grid.style != GridStyle::TopDown; };
    h.request = [this, place](const std::string& path, const json& body, int timeout) {
        const int id = host_.request ? host_.request(path, body, timeout) : 0;
        if (id != 0) pending_[id] = "build:" + place;
        return id;
    };
    h.projectRoot = root;
    h.layoutStep = false; // the planner confirms the zones with the rest
    // The scene it's built into says how, when it has a type (its menu's Type, docs/places/scene-types.md); else the plan's.
    std::string scene;
    if (const Row* r = FindRow(place)) scene = r->scene;
    h.buildMethod = [root, scene, method = host_.buildMethod]() -> std::string {
        if (!scene.empty()) {
            std::error_code ec;
            const fs::path file = Project{root}.Resolve(scene);
            if (fs::exists(file, ec)) {
                const std::string type = SceneType(file);
                if (type == "Paint") {
                    // A painted scene: painted as the plan says (Free or by Plan), Free when the plan's is Assembled.
                    const std::string planned = method ? NormalBuildMethod(method()) : std::string("Paint");
                    return IsPaintMethod(planned) ? planned : std::string("Paint");
                }
                if (!type.empty()) return std::string("Assembled");
            }
        }
        return NormalBuildMethod(method ? method() : std::string("Assembled"));
    };
    h.paintModel = host_.paintModel;
    return h;
}

// What the story needs of a place, for its build: the game's logline and synopsis, the scenes set
// there (in order), who is there, what the quests need in it, and its purpose and mood.
json GameBuild::StoryAnchors(const std::string& place) const {
    const json desc = store_.Read("places/" + PlanSlug(place)).value_or(json::object());
    json out = json::array();
    for (const auto& a : desc.value("storyAnchors", json::array())) {
        if (!a.is_object() || a.value("id", "").empty()) continue;
        std::string about = a.value("about", a.value("description", ""));
        if (about.empty()) about = a.value("module", "");
        out.push_back({{"id", a.value("id", "")}, {"about", about}});
    }
    return out;
}

json GameBuild::StoryFor(const std::string& place) const {
    const StoryGraph g = StoryGraph::Load(StoryGraph::PathIn(host_.projectRoot)).value_or(StoryGraph{});
    auto text = [](const json& j, const char* key) {
        const auto it = j.is_object() ? j.find(key) : j.end();
        return j.is_object() && it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    const json outline = store_.Read("outline").value_or(json::object());
    const json storyPart = outline.is_object() ? outline.value("story", json::object()) : json::object();
    std::string synopsis = text(storyPart, "synopsis");
    if (synopsis.size() > 900) synopsis = Utf8Cut(synopsis, 900) + "…";
    json scenes = json::array();
    for (const auto& id : g.SceneOrder()) {
        if (g.LocationOfScene(id) != place) continue;
        const json s = store_.Read("story/scenes/" + id).value_or(json::object());
        const json card = s.is_object() ? s.value("card", json::object()) : json::object();
        json scene = {{"id", id}, {"goal", text(card, "goal")}, {"summary", text(s, "summary")}};
        if (card.is_object() && card.contains("needsParts")) scene["needs"] = card["needsParts"];
        scenes.push_back(scene);
    }
    json people = json::array();
    for (const auto& e : g.edges) {
        if (e.p != "locatedAt" || e.o != place) continue;
        const StoryNode* n = g.Find(e.s);
        if (n) people.push_back({{"id", n->id}, {"name", n->Text("name")}, {"line", n->Text("summary")}});
    }
    json needs = json::array();
    for (const auto& doc : store_.List("quests")) {
        const json q = store_.Read(doc).value_or(json::object());
        if (!q.is_object()) continue;
        for (const auto& a : q.value("aliases", json::array())) {
            if (!a.is_object() || text(a, "in") != place) continue;
            needs.push_back({{"quest", text(q, "name")}, {"alias", text(a, "id")}, {"kind", text(a, "kind")},
                             {"where", a.value("where", json())}});
        }
    }
    const json desc = store_.Read("places/" + PlanSlug(place)).value_or(json::object());
    const json d = desc.is_object() ? desc.value("description", json::object()) : json::object();
    json out = {{"logline", text(storyPart, "logline")}, {"synopsis", synopsis}, {"scenesHere", scenes},
                {"charactersHere", people}, {"questNeeds", needs}, {"purpose", text(d, "purpose")}, {"mood", text(d, "mood")}};
    return out;
}

PlaceBuild* GameBuild::BuildOf(const std::string& place) {
    auto it = builds_.find(place);
    return it == builds_.end() ? nullptr : it->second.get();
}

// --- ⑩ The build plan ----------------------------------------------------------------------------------

void GameBuild::MakeBuildPlan() {
    const StoryGraph g = StoryGraph::Load(StoryGraph::PathIn(host_.projectRoot)).value_or(StoryGraph{});
    std::vector<Row> rows;
    for (const auto& doc : store_.List("places")) {
        const json p = store_.Read(doc).value_or(json::object());
        Row row;
        row.place = p.value("id", "location/" + PlanSlug(doc));
        row.scene = "scenes/" + PlanSlug(row.place) + ".aige";
        row.brief = p.value("brief", json::object());
        row.buildType = row.brief.value("buildType", p.value("kind", ""));
        const StoryNode* n = g.Find(row.place);
        const std::string name = n && !n->Text("name").empty() ? n->Text("name") : PlanSlug(row.place);
        row.prompt = name + ": " + p.value("description", json::object()).value("purpose", row.buildType);
        if (!row.brief.contains("name")) row.brief["name"] = name;
        for (const auto* e : g.Out(row.place, "inside")) row.dependsOn.push_back(e->o); // the outside before the inside
        row.mode = mode_ == "manual" ? "manual" : "automatic";
        rows.push_back(std::move(row));
    }
    // A place's gateways into the places entered from it (a crypt in the village, a cave by the camp): in its own Brief
    // (an open world's are the world's: WorldRow).
    for (const auto& e : g.edges) {
        const json gw = e.fields.value("gateway", json());
        if (e.p != "connects" || !gw.is_object() || gw.value("kind", "") == "building-door") continue;
        // An interior entered from outdoors: by its building's door (ConnectScenes); from a dungeon or another interior
        // (a cellar under the inn, stairs down): a gateway inside.
        const StoryNode* fromNode = g.Find(e.s);
        const bool fromInside = fromNode && (fromNode->Text("buildType") == "interior" || fromNode->Text("buildType") == "dungeon");
        if (const StoryNode* to = g.Find(e.o); to && to->Text("buildType") == "interior" && !fromInside) continue;
        for (auto& r : rows) {
            if (r.place != e.s) continue;
            if (!r.brief.contains("gateways")) r.brief["gateways"] = json::array();
            r.brief["gateways"].push_back({{"to", e.o}, {"kind", gw.value("kind", "")}, {"at", gw.value("at", "")}});
        }
    }
    WorldRow(g, rows);
    // Parents first.
    std::set<std::string> placed;
    while (!rows.empty()) {
        bool moved = false;
        for (auto it = rows.begin(); it != rows.end();) {
            const bool ready = std::all_of(it->dependsOn.begin(), it->dependsOn.end(), [&](const std::string& d) {
                return placed.contains(d) || std::none_of(rows.begin(), rows.end(), [&](const Row& r) { return r.place == d; });
            });
            if (ready) {
                placed.insert(it->place);
                rows_.push_back(std::move(*it));
                it = rows.erase(it);
                moved = true;
            } else {
                ++it;
            }
        }
        if (!moved) { // a cycle: the rest in order
            for (auto& r : rows) rows_.push_back(std::move(r));
            rows.clear();
        }
    }
}

// An open world (the outline's "world"): its sections built as one place, the world (docs/places/tiling.md), its Brief
// made from the plan: each section a place of it (its kind, size, where, and its own parts from its description), the
// world's spacing, size and landform, and the gateways into what's entered from it (a dungeon, a crypt, an interior).
// What was inside a section waits for the world.
void GameBuild::WorldRow(const StoryGraph& g, std::vector<Row>& rows) {
    const json outline = store_.Read("outline").value_or(json::object());
    const json world = outline.value("world", json());
    if (!world.is_object() || world.empty() || !host_.places) return;
    const Ontology& places = host_.places();
    std::string worldType;
    for (const auto& [id, b] : places.Builds()) {
        if (b.scope == "world" && worldType.empty()) worldType = id;
    }
    if (worldType.empty()) return;
    const std::string name = world.value("name", std::string("The world"));
    Row row;
    row.place = "location/world-" + PlanSlug(name);
    row.scene = "scenes/" + PlanSlug(row.place) + ".aige";
    row.buildType = worldType;
    row.prompt = name + ": an open world";
    row.mode = mode_ == "manual" ? "manual" : "automatic";
    json brief = {{"schema", "aige.brief/1"}, {"buildType", worldType}, {"name", name}, {"places", json::object()},
                  {"root", json::object()}, {"style", json::object()}};
    if (world.contains("spacing") && world["spacing"].is_number()) brief["root"]["spacing"] = world["spacing"];
    if (world.contains("settlementSize") && world["settlementSize"].is_string()) brief["root"]["settlementSize"] = world["settlementSize"];
    if (world.contains("landform") && world["landform"].is_string()) brief["style"]["landform"] = world["landform"];
    brief["scale"] = {{"class", "land"}}; // set from its count of places below
    std::set<std::string> sections;
    auto keyOf = [](const std::string& place) { return PlanSlug(place); };
    for (const auto& r : rows) {
        const StoryNode* n = g.Find(r.place);
        if (!n || !n->Flag("section")) continue;
        sections.insert(r.place);
        json p = {{"name", r.brief.value("name", n->Text("name"))}, {"type", r.buildType}};
        if (!n->Text("size").empty()) p["size"] = n->Text("size");
        if (const json where = n->fields.value("where", json()); where.is_object()) {
            json w = where;
            if (const StoryNode* other = g.Find(w.value("to", "")); other && other->Flag("section")) w["to"] = keyOf(other->id);
            p["where"] = w;
        }
        if (r.brief.contains("components")) p["components"] = r.brief["components"];
        // The outline's size is the world's word for it (the Confirm page's); without one, its class as ⑥ described it.
        if (const json cls = r.brief.value("scale", json::object()).value("class", json()); cls.is_string() && !p.contains("size")) p["class"] = cls;
        brief["places"][keyOf(r.place)] = p;
    }
    if (sections.empty()) return;
    brief["scale"]["class"] = sections.size() <= 3 ? "island" : sections.size() <= 8 ? "land" : "continent";
    // The gateways into the places entered from a section (each its kind, the section, and where, in words).
    json gateways = json::array();
    for (const auto& c : g.edges) {
        if (c.p != "connects") continue;
        const json gw = c.fields.value("gateway", json());
        if (!gw.is_object()) continue;
        const std::string from = sections.contains(c.s) ? c.s : sections.contains(c.o) ? c.o : "";
        const std::string to = from == c.s ? c.o : c.s;
        if (from.empty() || gw.value("kind", "") == "building-door") continue;
        if (const StoryNode* target = g.Find(to); target && target->Text("buildType") == "interior") continue; // through its door
        gateways.push_back({{"to", to}, {"from", keyOf(from)}, {"kind", gw.value("kind", "")}, {"at", gw.value("at", "")}});
    }
    if (!gateways.empty()) brief["gateways"] = gateways;
    row.brief = brief;
    row.sections.assign(sections.begin(), sections.end());
    // The sections' own rows go (the world builds them); what was inside one waits for the world.
    std::vector<Row> kept;
    for (auto& r : rows) {
        if (sections.contains(r.place)) continue;
        for (auto& d : r.dependsOn) {
            if (sections.contains(d)) d = row.place;
        }
        kept.push_back(std::move(r));
    }
    kept.insert(kept.begin(), std::move(row));
    rows = std::move(kept);
}

std::string GameBuild::SetMode(const std::string& place, const std::string& mode) {
    Row* r = FindRow(place);
    if (!r) return "no " + place + " in the build plan";
    r->mode = mode == "manual" ? "manual" : "automatic";
    Record("set-mode", place, r->mode);
    Save();
    return {};
}

std::string GameBuild::Retry(const std::string& place) {
    Row* r = FindRow(place);
    if (!r) return "no " + place + " in the build plan";
    r->status = "waiting";
    r->error.clear();
    r->steps = 0;
    builds_.erase(place);
    if (stage_ != "build") {
        stage_ = "build"; // the rest follows again
        status_ = "working";
    }
    Record("retry", place, "");
    Save();
    return {};
}

std::string GameBuild::Rebuild(const std::string& place) {
    Row* r = FindRow(place);
    if (!r) return "no " + place + " in the build plan";
    if (r->status == "building" && status_ == "working") return "it's being built now";
    r->status = "waiting";
    r->error.clear();
    r->steps = 0;
    r->build.clear(); // a new build of it, not the old one carried on
    r->inEditor = false;
    r->resumeInEditor = false;
    r->mode = "manual"; // built again by the user in the Build panel: it waits at ① Confirm, never plans or paints by itself
    builds_.erase(place);
    rebuildOnly_ = BuildStageOrder(reached_) > BuildStageOrder("build"); // only this place, then back to where it was
    stage_ = "build"; // the place is built here
    status_ = "working";
    message_ = "⑩ Building " + PlanSlug(place) + " again…";
    Record("rebuild", place, "the Build panel's, from ① Confirm");
    Save();
    return {};
}

std::string GameBuild::HandToEditor(const std::string& place, const std::string& buildId) {
    // The user opened this place's build in the Build panel: the automatic build stops driving it (no plan-root,
    // plan-zone or paint behind the panel's window), and the row waits for the user's steps there.
    auto stem = [](const std::string& s) {
        const size_t at = s.find_last_of('/');
        return at == std::string::npos ? s : s.substr(at + 1);
    };
    bool any = false;
    for (auto& row : rows_) {
        const bool match = (!buildId.empty() && row.build == buildId) ||
                           (!place.empty() && (row.place == place || stem(row.place) == stem(place)));
        if (!match) continue;
        builds_.erase(row.place); // its copy stops acting: the panel's is the one now
        buildAnswers_.erase(row.place);
        row.inEditor = false;
        row.resumeInEditor = false;
        row.awaitingApproval = false;
        if (row.status == "building" || row.status == "waiting") row.status = "needs-you";
        Record("hand-to-editor", row.place, "the Build panel has it");
        any = true;
    }
    if (any) Save();
    return {};
}

std::string GameBuild::InstallBuilt(const std::string& place, const std::string& buildId) {
    if (fs::exists(store_.Dir() / "buildstate.json")) Load();
    Row* r = FindRow(place);
    if (!r) return "no " + place + " in the build plan";
    r->build = buildId;
    r->error.clear();
    r->inEditor = r->resumeInEditor = false;
    builds_.erase(place);
    try {
        FinishInEditor(*r);
    } catch (const std::exception& e) {
        return e.what();
    }
    Save();
    return {};
}

std::string GameBuild::UsePlaceholder(const std::string& place) {
    Row* r = FindRow(place);
    if (!r) return "no " + place + " in the build plan";
    const std::string why = PlaceholderScene(*r);
    if (!why.empty()) return why;
    Record("placeholder", place, r->scene, "the user chose a placeholder");
    Save();
    return {};
}

std::string GameBuild::ManualDone(const std::string& place) {
    Row* r = FindRow(place);
    PlaceBuild* build = BuildOf(place);
    if (!r || !build) return "no build for " + place;
    if (build->Stage() != "planned" && build->Stage() != "built") return "its build isn't laid out yet";
    const std::string why = BuildScene(*r, *build);
    if (!why.empty()) return why;
    Record("build-step", place, "built", "the user finished it in the Build panel");
    Save();
    return {};
}

bool GameBuild::DriveBuilds() {
    bool did = false;
    auto depsDone = [&](const Row& row) {
        return std::all_of(row.dependsOn.begin(), row.dependsOn.end(), [&](const std::string& d) {
            const Row* dep = FindRow(d);
            return !dep || dep->status == "built" || dep->status == "placeholder";
        });
    };
    for (auto& row : rows_) {
        if (row.status == "building") {
            did = DriveRow(row) || did;
            return did; // one automatic build at a time
        }
    }
    for (auto& row : rows_) {
        if (row.status != "waiting" || !depsDone(row)) continue;
        // A place that already has a build (stopped, or tried before): carried on from where it is, not started again.
        // Built by then (the user finished it in the Build panel): it's built. Past its plan: the Build panel finishes
        // it. Before: reopened here. (Rebuild clears the build for a new one; one that can't be reopened starts anew.)
        if (!row.build.empty() && row.mode == "automatic") {
            std::ifstream sessionIn(host_.projectRoot / "places" / row.build / "session.json", std::ios::binary);
            const json session = sessionIn ? json::parse(sessionIn, nullptr, false) : json();
            const std::string stage = session.is_object() && session.value("stage", json()).is_string() ? session["stage"].get<std::string>() : "";
            std::error_code ec;
            if (stage == "built" && fs::exists(Project{host_.projectRoot}.Resolve(row.scene), ec)) {
                try {
                    FinishInEditor(row);
                    Record("built", row.place, row.scene, "its build was finished in the Build panel");
                    return true;
                } catch (const std::exception& e) {
                    row.error = e.what(); // then built again below
                }
            } else if ((stage == "planned" || stage == "sourcing" || stage == "sourced" || stage == "building") && host_.buildInEditor) {
                row.status = "building";
                row.resumeInEditor = true;
                Record("build-step", row.place, stage + " → the Build panel", "carried on from where it was");
                return DriveRow(row) || true;
            } else if (!stage.empty()) {
                auto reopened = std::make_unique<PlaceBuild>(BuildHost(row.place));
                if (reopened->Open(row.build).empty()) {
                    builds_[row.place] = std::move(reopened);
                    row.status = "building";
                    Record("build-step", row.place, "reopened at " + stage, "carried on from where it was");
                    return DriveRow(row) || true;
                }
            }
        }
        auto build = std::make_unique<PlaceBuild>(BuildHost(row.place));
        const std::string why = build->StartFromBrief(row.prompt, row.brief);
        if (why.empty()) build->SetStory(StoryFor(row.place));
        if (!why.empty()) {
            row.error = why;
            row.status = "failed";
        } else {
            row.build = build->Id();
            builds_[row.place] = std::move(build);
            row.status = row.mode == "manual" ? "needs-you" : "building";
        }
        did = true;
        if (row.status == "building") return DriveRow(row) || did;
        if (row.status == "failed" && row.mode == "automatic") {
            PlaceholderScene(row);
            Record("placeholder", row.place, row.scene, "its build couldn't start: " + row.error);
        }
    }
    return did;
}

bool GameBuild::DriveRow(Row& row) {
    if (row.resumeInEditor) {
        // Reopened past its plan: the Build panel picks it up again. Painted and not painted yet: its painting's calls
        // approved first (unless building is automatic), as a build carried on here is.
        if (!row.paintApproved && !AutoApprove()) {
            std::ifstream sessionIn(host_.projectRoot / "places" / row.build / "session.json", std::ios::binary);
            const json session = sessionIn ? json::parse(sessionIn, nullptr, false) : json();
            if (session.is_object() && session.value("stage", json()) == "planned" && IsPaintMethod(session.value("method", std::string())) &&
                !session.value("fromPicture", false) && !fs::exists(host_.projectRoot / "places" / row.build / "painting.png")) {
                if (!row.awaitingApproval) {
                    row.awaitingApproval = true;
                    message_ = "⑩ " + row.place + " is planned: approve painting it (its tiles and calls in the table)";
                    return true;
                }
                return false;
            }
        }
        row.resumeInEditor = false;
        const std::string why = host_.buildInEditor ? host_.buildInEditor(row.build, row.scene) : "the editor isn't there";
        if (!why.empty()) throw std::runtime_error(why);
        row.inEditor = true;
        return true;
    }
    if (row.inEditor) {
        // The Build panel is building it (art, scene, checks): the next place waits.
        const std::string status = host_.editorBuildStatus ? host_.editorBuildStatus(row.build) : "failed: the editor isn't there";
        if (status == "working") return false;
        row.inEditor = false;
        if (status != "built") throw std::runtime_error(status.rfind("failed: ", 0) == 0 ? status.substr(8) : status);
        FinishInEditor(row);
        return true;
    }
    PlaceBuild* build = BuildOf(row.place);
    if (!build) {
        row.status = "waiting";
        return false;
    }
    bool did = false;
    try {
        auto answers = std::move(buildAnswers_[row.place]);
        buildAnswers_.erase(row.place);
        for (auto& [id, body] : answers) {
            build->OnResponse(id, body);
            did = true;
        }
        for (int step = 0; step < 12; ++step) {
            const std::string stage = build->Stage();
            std::string why;
            // The request is confirmed first (it moves on to the assets), then the build.
            if (stage == "request") why = build->Step("assets");
            else if (stage == "assets") why = build->Confirm();
            else if (stage == "layout") {
                // The zones laid out first (as pressing "Lay out the zones" would), then checked by the vision model
                // (its errors revised), then what's inside them.
                if (!build->HasLayout()) why = build->ReplanLayout();
                else if (build->LayoutChecked() || !build->CheckLayout().empty()) why = build->PlanFromLayout();
            }
            else if (stage == "plan") {
                // Its zones checked by the vision model first (built without ② Layout); then approved.
                if (build->LayoutChecked() || !build->CheckLayout().empty()) why = build->Approve();
            }
            else if (stage == "planned" && build->Scope() == "world" && !build->SectionsPlanned()) {
                // An open world: its sections planned first (their AI answers come through this build), then it's one place.
                if (build->State().value("sections", json::array()).empty()) {
                    json stories = json::object(); // each section's story (its scenes, people, anchors)
                    for (const auto& s : row.sections) stories[PlanSlug(s)] = StoryFor(s);
                    build->SetSectionStories(stories);
                    why = build->PlanSections();
                }
                else {
                    build->DriveSections();
                    if (!build->SectionsPlanned()) return did;
                }
                if (why.empty() && !build->SectionsPlanned()) return true;
            }
            else if ((stage == "planned" || stage == "built") && host_.buildInEditor) {
                // Painted: the painting's calls approved first (unless building is automatic). The build's own method
                // (a scene's type may differ from the plan's), and not when it's painted already (an earlier try's).
                if (stage == "planned" && IsPaintMethod(build->Method()) && !build->Painted() && !build->FromPicture() &&
                    !row.paintApproved && !AutoApprove()) {
                    if (!row.awaitingApproval) {
                        row.awaitingApproval = true;
                        message_ = "⑩ " + row.place + " is planned: approve painting it (its tiles and calls in the table)";
                        return true;
                    }
                    return false;
                }
                // Built by the Build panel itself, as the user would: its art found or made, the scene
                // built, checked and saved; this build lets go of it meanwhile.
                const std::string id = build->Id();
                row.build = id;
                builds_.erase(row.place);
                const std::string why = host_.buildInEditor(id, row.scene);
                if (!why.empty()) throw std::runtime_error(why);
                row.inEditor = true;
                Record("build-step", row.place, "planned → the Build panel", "automatic: built in the editor");
                return true;
            } else if (stage == "planned" || stage == "built") {
                const std::string built = BuildScene(row, *build);
                if (!built.empty()) throw std::runtime_error(built);
                return true;
            } else {
                // Waiting on the AI (or stuck: nothing asked, nothing to press).
                const bool asking = std::any_of(pending_.begin(), pending_.end(), [&](const auto& p) { return p.second == "build:" + row.place; });
                if (!asking && ++row.steps > 3) throw std::runtime_error("the build stopped at '" + stage + "': " + build->State().value("status", ""));
                return did;
            }
            if (!why.empty()) throw std::runtime_error(stage + ": " + why);
            Record("build-step", row.place, stage + " → " + build->Stage(), "automatic: checks passed");
            row.steps = 0;
            did = true;
        }
    } catch (const std::exception& e) {
        row.error = e.what();
        row.status = "failed";
        builds_.erase(row.place);
        if (row.mode == "automatic") {
            // So binding and the playtest can go on; rebuilt later with Retry.
            PlaceholderScene(row);
            Record("placeholder", row.place, row.scene, "the build failed: " + row.error);
        }
        return true;
    }
    return did;
}

std::string GameBuild::BuildScene(Row& row, PlaceBuild& build) {
    const Project project{host_.projectRoot};
    const Ontology& places = host_.places();
    PrefabLibrary prefabs(project);
    AssetDatabase assets(project);
    assets.Refresh();
    PlaceSceneOptions options;
    options.place = build.Id();
    options.isoGrid = ActiveSettings().grid.style != GridStyle::TopDown;
    const TerrainSet terrains = ProjectTerrainSet(prefabs, project);
    const json plan = PlanPlaceAssets(places, build.Spec(), build.Solved(), BuildCatalog(assets, &places, {}), prefabs, assets, terrains, options, nullptr);
    Registry registry;
    json report;
    BuildPlaceScene(registry, prefabs, build.Solved(), plan, terrains, options, &report);
    const int made = AddStoryMarkers(registry, row, build.Solved(), options.originX, options.originY);
    std::error_code ec;
    fs::create_directories(project.Resolve(row.scene).parent_path(), ec);
    SaveScene(registry, project.Resolve(row.scene).string());
    int objects = 0;
    const json counts = report.value("objects", json::object());
    for (const auto& [layer, n] : counts.items()) objects += n.is_number() ? n.get<int>() : 0;
    row.checks = {{"objects", objects}, {"pieces", report.value("pieces", 0)}, {"anchorsMade", made}};
    row.build = build.Id();
    row.status = "built";
    files_[row.scene] = {{"planner", "places/" + PlanSlug(row.place)}, {"build", build.Id()}};
    return {};
}

Entity GameBuild::CreatePlanPlayer(Registry& registry, float x, float y) {
    // One there already (placed again: its drawing and kit kept): moved to where it starts.
    for (auto [e, team, w] : registry.view<Faction, WorldTransform2D>().each()) {
        if (team.team != Team::Player) continue;
        w.x = x, w.y = y;
        return e;
    }
    // Interact is the default action; the player fights only when the plan has combat (its brief's systems.combat).
    const Entity player = CreatePlayer(registry, x, y);
    const json brief = store_.Read("brief").value_or(json::object());
    const json on = BriefGet(brief, "systems.combat.on"), style = BriefGet(brief, "systems.combat.style");
    const bool fights = on.is_boolean() ? on.get<bool>() : style.is_string() && style.get<std::string>() != "none";
    if (fights) registry.emplace_or_replace<MeleeAttack>(player, 3.0f, 0.7f, 0.5f, 0.35f, 0.5f);
    return player;
}

int GameBuild::AddStoryMarkers(Registry& registry, const Row& row, const json& solved, int originX, int originY) {
    // Its story anchors as markers: on the part that shows the anchor's module, else in the middle.
    const TileGrid grid = TileGrid::Build(registry);
    const Extent extent = OpenExtent(registry, grid);
    const int midX = extent.any ? (extent.minX + extent.maxX) / 2 : 0, midY = extent.any ? (extent.minY + extent.maxY) / 2 : 0;
    int made = 0;
    // A painted place's markers are already where its painting shows them (PlacePaint): kept.
    std::map<std::string, std::pair<float, float>> placed;
    for (auto [e, m, w] : registry.view<Marker, WorldTransform2D>().each()) placed[m.name] = {w.x, w.y};
    // Its own description's anchors; a world's are its sections' (each in its section: its objects are
    // "heartland-<key>/...", the middle of them where none shows the anchor).
    struct Part {
        std::string place, prefix;
        int midX, midY;
    };
    std::vector<Part> parts = {{row.place, "", midX, midY}};
    std::string startSection;
    const StoryGraph graph = StoryGraph::Load(StoryGraph::PathIn(host_.projectRoot)).value_or(StoryGraph{});
    for (const auto& s : row.sections) {
        Part part{s, "heartland-" + PlanSlug(s) + "/", midX, midY};
        long sx = 0, sy = 0, n = 0;
        for (const auto& o : solved.value("objects", json::array())) {
            if (!o.is_object() || o.value("id", "").rfind(part.prefix, 0) != 0) continue;
            sx += o.value("x", 0), sy += o.value("y", 0), ++n;
        }
        if (n > 0) part.midX = static_cast<int>(sx / n) + originX, part.midY = static_cast<int>(sy / n) + originY;
        if (const StoryNode* node = graph.Find(s); node && node->Flag("start")) startSection = s;
        parts.push_back(part);
    }
    for (const Part& part : parts) {
        const json desc = store_.Read("places/" + PlanSlug(part.place)).value_or(json::object());
        for (const auto& a : desc.value("storyAnchors", json::array())) {
            if (!a.is_object()) continue;
            const std::string id = a.value("id", ""), module = a.value("module", "");
            if (placed.contains(id)) continue;
            std::optional<std::pair<int, int>> at;
            for (const auto& o : solved.value("objects", json::array())) {
                if (!o.is_object() || (!part.prefix.empty() && o.value("id", "").rfind(part.prefix, 0) != 0)) continue;
                const std::string node = o.value("node", "");
                if (o.value("shows", "") == module || o.value("slot", "") == id || node.size() >= id.size() + 1 && node.compare(node.size() - id.size(), id.size(), id) == 0) {
                    at = NearestOpen(grid, o.value("x", 0) + originX, o.value("y", 0) + originY);
                    break;
                }
            }
            if (!at) {
                // The room it's in (a dungeon's rooms are named for themselves, not the anchors' modules).
                if (const auto room = RoomOfAnchor(a, solved, originX, originY)) at = NearestOpen(grid, room->first, room->second);
            }
            if (!at) {
                // The module's part of the plan (market-ward/market-square), else what the anchor's words name.
                const std::vector<PlannedThing> things = ThingsOf(row);
                const json relation = a.value("relation", json::object());
                const std::string at_ = relation.is_object() ? relation.value("at", "") : "";
                const std::vector<std::string> slots = SlotsFor(id + " " + at_ + " " + module);
                for (int pass = 0; pass < 2 && !at; ++pass) {
                    for (const auto& t : things) {
                        if (!part.prefix.empty() && t.id.rfind(part.prefix, 0) != 0) continue;
                        const bool ofModule = !module.empty() && (t.id.find("/" + module) != std::string::npos || t.id.rfind(module, 0) == 0);
                        if (pass == 0 ? !ofModule : std::find(slots.begin(), slots.end(), t.slot) == slots.end()) continue;
                        const auto [dx, dy] = DoorOf(t);
                        at = NearestOpen(grid, dx, dy);
                        break;
                    }
                }
            }
            if (!at) {
                at = NearestOpen(grid, part.midX + made, part.midY);
                ++made;
            }
            if (at) AddMarker(registry, id, at->first + 0.5f, at->second + 0.5f);
        }
    }
    // A world's arrival is in its starting section (else its first).
    int arriveX = midX, arriveY = midY;
    for (const Part& part : parts) {
        if (!part.prefix.empty() && (part.place == startSection || (startSection.empty() && arriveX == midX && arriveY == midY))) {
            arriveX = part.midX, arriveY = part.midY;
        }
    }
    if (const auto at = placed.find("arrive"); at != placed.end()) {
        if (row.scene == StartScene()) CreatePlanPlayer(registry, at->second.first, at->second.second);
    } else if (auto arrive = NearestOpen(grid, arriveX, arriveY)) {
        AddMarker(registry, "arrive", arrive->first + 0.5f, arrive->second + 0.5f);
        if (row.scene == StartScene()) CreatePlanPlayer(registry, arrive->first + 0.5f, arrive->second + 0.5f);
    }
    return made;
}

using LevelArea = PlaceLevelArea;

// Only what stands in [x0,x1)x[y0,y1) (a cell round it for the walls) is kept: the rest goes.
static void KeepOnly(Registry& registry, int x0, int y0, int x1, int y1, const std::string& level) {
    x0 -= 1, y0 -= 1, x1 += 1, y1 += 1;
    auto inside = [&](float x, float y) { return x >= x0 && y >= y0 && x < x1 && y < y1; };
    std::vector<Entity> gone;
    for (auto e : registry.view<entt::entity>()) {
        // A painted level's backdrop stands on the screen, not a cell: kept with its own level only.
        if (const auto* n = registry.try_get<PlaceNode>(e); n && n->statement.rfind("backdrop#", 0) == 0) {
            if (n->node != level) gone.push_back(e);
            continue;
        }
        if (const auto* t = registry.try_get<Tile>(e)) {
            if (!inside(static_cast<float>(t->gx), static_cast<float>(t->gy))) gone.push_back(e);
        } else if (const auto* w = registry.try_get<WorldTransform2D>(e)) {
            if (!inside(w->x, w->y)) gone.push_back(e);
        }
    }
    for (auto e : gone) registry.destroy(e);
    for (auto& [name, layer] : LayersOf(registry).layers) {
        std::vector<std::pair<int, int>> out;
        layer.ForEach([&](int gx, int gy, uint16_t) {
            if (!inside(static_cast<float>(gx), static_cast<float>(gy))) out.push_back({gx, gy});
        });
        for (const auto& [gx, gy] : out) layer.Erase(gx, gy);
    }
}

void GameBuild::FinishInEditor(Row& row) {
    const Project project{host_.projectRoot};
    const std::string path = project.Resolve(row.scene).string();
    Registry registry;
    if (!LoadScene(registry, path)) throw std::runtime_error("the scene the Build panel saved can't be read: " + row.scene);
    // Only a scene that holds the place: one without it (emptied, or another's) isn't marked up and saved over.
    {
        bool holds = false;
        for (auto [e, node] : registry.view<PlaceNode>().each()) holds = holds || node.place == row.build;
        if (!holds) throw std::runtime_error(row.scene + " doesn't hold " + row.build + " (it isn't built into it)");
    }
    PlaceBuild build(BuildHost(row.place));
    int made = 0;
    std::vector<LevelArea> levels;
    int ox = 0, oy = 0;
    if (build.Open(row.build).empty()) {
        std::tie(ox, oy) = build.Origin();
        made = AddStoryMarkers(registry, row, build.Solved(), ox, oy);
        if (host_.places) levels = PlaceLevelAreas(host_.places(), build.Spec(), build.Solved());
    }
    files_[row.scene] = {{"planner", "places/" + PlanSlug(row.place)}, {"build", row.build}};
    if (levels.empty()) {
        SaveScene(registry, path);
    } else {
        // Each level its own scene (docs/build-fixes.md): the first at the row's scene, the others
        // beside it; stairs lead down to the next level and back up (a gateway on each, arriving by
        // the other level's stairs).
        const std::string whole = SerializeScene(registry);
        const fs::path base = fs::path(row.scene);
        auto sceneOf = [&](size_t i) {
            return i == 0 ? row.scene : (base.parent_path() / (base.stem().string() + "-" + PlanSlug(levels[i].id) + base.extension().string())).generic_string();
        };
        auto stairsOf = [&](const LevelArea& l, bool last) -> std::pair<float, float> {
            if (!l.stairs.empty()) {
                const auto& s = last ? l.stairs.back() : l.stairs.front();
                return {ox + s.first + 0.5f, oy + s.second + 0.5f};
            }
            return {ox + (l.x0 + l.x1) / 2.0f, oy + (l.y0 + l.y1) / 2.0f};
        };
        json sceneList = json::array();
        for (size_t i = 0; i < levels.size(); ++i) {
            Registry level;
            DeserializeScene(level, whole);
            KeepOnly(level, ox + levels[i].x0, oy + levels[i].y0, ox + levels[i].x1, oy + levels[i].y1, levels[i].id);
            auto gateway = [&](float x, float y, const std::string& scene, const std::string& marker, const std::string& label) {
                const Entity g = level.create();
                level.emplace<WorldTransform2D>(g, x, y);
                level.emplace<Gateway>(g, scene, marker);
                level.emplace<Name>(g, label);
            };
            if (i + 1 < levels.size()) {
                const auto [sx, sy] = stairsOf(levels[i], true);
                gateway(sx, sy, sceneOf(i + 1), "from-" + PlanSlug(levels[i].id), "Stairs down to " + levels[i + 1].id);
                AddMarker(level, "from-" + PlanSlug(levels[i + 1].id), sx + 1.0f, sy);
            }
            if (i > 0) {
                const auto [sx, sy] = stairsOf(levels[i], false);
                gateway(sx, sy, sceneOf(i - 1), "from-" + PlanSlug(levels[i].id), "Stairs up to " + levels[i - 1].id);
                AddMarker(level, "from-" + PlanSlug(levels[i - 1].id), sx + 1.0f, sy);
            }
            const std::string file = project.Resolve(sceneOf(i)).string();
            std::error_code ec;
            fs::create_directories(fs::path(file).parent_path(), ec);
            SaveScene(level, file);
            files_[sceneOf(i)] = {{"planner", "places/" + PlanSlug(row.place)}, {"build", row.build}, {"level", levels[i].id}};
            sceneList.push_back(sceneOf(i));
        }
        row.checks["levels"] = sceneList;
    }
    row.checks["anchorsMade"] = made;
    row.checks["builtIn"] = "the Build panel";
    row.status = "built";
    Record("built", row.place, row.scene, "built by the Build panel: art found, scene built and checked");
}

std::string GameBuild::PlaceholderScene(Row& row) {
    // A plain room of floor in walls, big enough to walk and hold its anchors.
    const Project project{host_.projectRoot};
    Registry registry;
    const int w = row.buildType == "settlement" || row.buildType == "wilderness" || row.buildType == "region" ? 24 : 12;
    for (int y = 0; y < w; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool edge = x == 0 || y == 0 || x == w - 1 || y == w - 1;
            CreateTile(registry, x, y, edge ? TileKind::Wall : TileKind::Dirt);
        }
    }
    const json desc = store_.Read("places/" + PlanSlug(row.place)).value_or(json::object());
    int k = 0;
    for (const auto& a : desc.value("storyAnchors", json::array())) {
        AddMarker(registry, a.value("id", ""), 2.5f + static_cast<float>((k * 3) % (w - 4)), 2.5f + static_cast<float>(((k * 3) / (w - 4)) * 2));
        ++k;
    }
    AddMarker(registry, "arrive", w / 2 + 0.5f, w / 2 + 0.5f);
    if (row.scene == StartScene()) CreatePlanPlayer(registry, w / 2 + 0.5f, w / 2 + 0.5f);
    std::error_code ec;
    fs::create_directories(project.Resolve(row.scene).parent_path(), ec);
    SaveScene(registry, project.Resolve(row.scene).string());
    row.status = "placeholder";
    row.checks = {{"placeholder", true}};
    files_[row.scene] = {{"planner", "places/" + PlanSlug(row.place)}, {"placeholder", true}};
    return {};
}

void GameBuild::ConnectScenes() {
    const Project project{host_.projectRoot};
    const StoryGraph g = StoryGraph::Load(StoryGraph::PathIn(host_.projectRoot)).value_or(StoryGraph{});
    std::vector<std::pair<std::string, std::string>> links;
    for (const auto& e : g.edges) {
        if (!((e.p == "connects" || e.p == "inside") && FindRow(e.s) && FindRow(e.o) && FindRow(e.s) != FindRow(e.o))) continue;
        // Once a pair, whichever way the story said it (a connects b, b connects a): the gateways go both ways anyway.
        const bool known = std::any_of(links.begin(), links.end(), [&](const auto& l) {
            return (FindRow(l.first) == FindRow(e.s) && FindRow(l.second) == FindRow(e.o)) || (FindRow(l.first) == FindRow(e.o) && FindRow(l.second) == FindRow(e.s));
        });
        if (!known) links.push_back({e.s, e.o});
    }
    // Each scene's gateways go round its edge: east, west, south, north, then inward.
    std::map<std::string, int> used;
    // The place a place is in (the outline's parent): its way back out leads there.
    const json outlined = store_.Read("outline").value_or(json::object()).value("places", json::array());
    auto parentOf = [&](const std::string& place) {
        for (const auto& p : outlined) {
            if (p.is_object() && p.value("id", json()) == json(place)) return p.contains("parent") && p["parent"].is_string() ? p["parent"].get<std::string>() : std::string();
        }
        return std::string();
    };
    const json settled = LoadGateways(store_.Dir());
    std::map<std::string, std::vector<PlannedThing>> planned;
    auto PlannedFor = [&](const Row& row) -> const std::vector<PlannedThing>& {
        auto [it, fresh] = planned.try_emplace(row.place);
        if (fresh) it->second = ThingsOf(row);
        return it->second;
    };
    // An interior place inside this one (docs/places/interiors.md §2b): entered through a building's door. Its
    // building: one whose interior is a scene not yet linked, its role named in the interior's name or prompt (an inn
    // for "the Rusty Anchor inn"), else the biggest such building, else any building. False: none (the edge then).
    auto throughDoor = [&](Registry& registry, const Row& outside, const Row& inside) {
        if (inside.buildType != "interior" || outside.buildType == "interior") return false;
        std::string words = inside.place + " " + inside.prompt;
        std::transform(words.begin(), words.end(), words.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::optional<Entity> best;
        int bestScore = -1;
        for (auto [e, obstacle] : registry.view<Obstacle>().each()) {
            const auto* interior = registry.try_get<Interior>(e);
            if (interior && interior->kind == "scene" && !interior->scene.empty()) continue; // taken
            std::string role;
            if (const auto* node = registry.try_get<PlaceNode>(e)) role = node->statement.substr(node->statement.rfind('.') + 1);
            const int size = (obstacle.left + obstacle.right + 1) * (obstacle.back + 1);
            const bool named = !role.empty() && words.find(role) != std::string::npos;
            const int score = (named ? 100000 : 0) + (interior && interior->kind == "scene" ? 10000 : 0) + size;
            if (score > bestScore) bestScore = score, best = e;
        }
        if (best) {
            LinkInteriorOutside(registry, *best, inside.scene, "from-" + PlanSlug(outside.place), "from-" + PlanSlug(inside.place));
            return true;
        }
        // A painted place has no building entities: its plan's buildings, the one its words name (the forge for "Ash
        // Forge", a shop for "Pike Arms"), else a shop, else a house; one whose door isn't some other way in already.
        const std::vector<PlannedThing> things = PlannedFor(outside);
        if (things.empty()) return false;
        const json desc = store_.Read("places/" + PlanSlug(inside.place)).value_or(json::object());
        const json about = desc.value("description", json());
        const std::string purpose = about.is_object() ? about.value("purpose", "") : "";
        const std::vector<std::string> named = SlotsFor(words + " " + purpose);
        const TileGrid grid = TileGrid::Build(registry);
        const PlannedThing* door = nullptr;
        int doorScore = -1;
        std::vector<std::string> buildings = {"shop", "workshop", "inn", "temple", "house", "townhouse"};
        if (!named.empty() && std::find(buildings.begin(), buildings.end(), named.front()) == buildings.end()) {
            // Not a building (a crypt: among the graves; a mine: none here): only what it names, else the edge.
            buildings = named;
        }
        for (const auto& t : things) {
            const auto kind = std::find(buildings.begin(), buildings.end(), t.slot);
            if (kind == buildings.end()) continue;
            const auto [dx, dy] = DoorOf(t);
            bool taken = false;
            for (auto [e, gw, w] : registry.view<Gateway, WorldTransform2D>().each()) {
                taken = taken || (std::abs(w.x - dx - 0.5f) < 2.0f && std::abs(w.y - dy - 0.5f) < 2.0f);
            }
            if (taken) continue;
            const auto rank = std::find(named.begin(), named.end(), t.slot);
            const int score = (rank != named.end() ? 1000 - static_cast<int>(rank - named.begin()) * 100 : 0) + (10 - static_cast<int>(kind - buildings.begin()));
            if (score > doorScore) doorScore = score, door = &t;
        }
        if (!door) return false;
        const auto [dx, dy] = DoorOf(*door);
        const auto at = NearestOpen(grid, dx, dy);
        if (!at) return false;
        const Entity gw = CreateGateway(registry, at->first + 0.5f, at->second + 0.5f, inside.scene, "from-" + PlanSlug(outside.place));
        registry.emplace_or_replace<Name>(gw, "Gateway to " + inside.brief.value("name", PlanSlug(inside.place)) + " (door of " + door->id + ")");
        const auto [ox, oy] = OutsideDoor(*door);
        if (const auto out = NearestOpen(grid, ox, oy)) AddMarker(registry, "from-" + PlanSlug(inside.place), out->first + 0.5f, out->second + 0.5f);
        return true;
    };
    auto place = [&](const std::string& from, const std::string& to) {
        Row* a = FindRow(from);
        Row* b = FindRow(to);
        Registry registry;
        if (!LoadScene(registry, project.Resolve(a->scene).string())) return;
        // Connected already (an earlier build, characters placed again): its way there stays, nothing added.
        for (auto [e, gateway] : registry.view<Gateway>().each()) {
            if (gateway.scene == b->scene) return;
        }
        for (auto [e, interior] : registry.view<Interior>().each()) {
            if (interior.kind == "scene" && interior.scene == b->scene) return;
        }
        // Settled before (a door the vision model found, a gateway with its art): where it was, as it was. An interior's or a
        // dungeon's way back out only as ApplyExit found it ("exit"), not a door or wall the ways-in look took for it.
        const bool settledHere = a->buildType == "settlement" || a->buildType == "world";
        const bool wayBack = !settledHere && (FindRow(parentOf(a->place)) ? FindRow(parentOf(a->place)) == b : b->buildType == "settlement" || b->buildType == "world");
        if (const json s = settled.value(GatewayKey(a->scene, b->scene), json());
            s.is_object() && s.contains("cell") && s["cell"].is_array() && (!wayBack || s.value("exit", false))) {
            const Entity gw = CreateGateway(registry, s["cell"][0].get<float>() + 0.5f, s["cell"][1].get<float>() + 0.5f, b->scene, "from-" + PlanSlug(from));
            registry.emplace_or_replace<Name>(gw, "Gateway to " + b->brief.value("name", PlanSlug(to)));
            DressGateway(registry, gw, s, project.root);
            if (s.contains("arrive") && s["arrive"].is_array()) {
                AddMarker(registry, "from-" + PlanSlug(to), s["arrive"][0].get<float>() + 0.5f, s["arrive"][1].get<float>() + 0.5f);
            }
            SaveScene(registry, project.Resolve(a->scene).string());
            return;
        }
        if (throughDoor(registry, *a, *b)) {
            SaveScene(registry, project.Resolve(a->scene).string());
            return;
        }
        // A gateway planned for it (a cave mouth, crypt door, mine shaft the plan placed: its id "…gateway-<place>"):
        // walking into it goes there; one arrives in front of it coming back.
        {
            const std::string wanted = "gateway-" + PlanSlug(to);
            std::optional<std::pair<float, float>> at;
            for (auto [e, node, t] : registry.view<PlaceNode, WorldTransform2D>().each()) {
                const std::string& id = node.statement;
                if (id.size() < wanted.size() || id.compare(id.size() - wanted.size(), wanted.size(), wanted) != 0) continue;
                if (!registry.all_of<Gateway>(e)) registry.emplace<Gateway>(e, b->scene, "from-" + PlanSlug(from));
                at = std::pair{t.x, t.y};
                break;
            }
            if (at) {
                const TileGrid grid = TileGrid::Build(registry);
                if (const auto in = NearestOpen(grid, static_cast<int>(at->first), static_cast<int>(at->second) + 2)) {
                    AddMarker(registry, "from-" + PlanSlug(to), in->first + 0.5f, in->second + 0.5f);
                }
                SaveScene(registry, project.Resolve(a->scene).string());
                return;
            }
            // A painted place's planned gateway is in its plan only (the gate it painted): a gateway on it.
            for (const auto& t : PlannedFor(*a)) {
                if (t.id.size() < wanted.size() || t.id.compare(t.id.size() - wanted.size(), wanted.size(), wanted) != 0) continue;
                const TileGrid grid = TileGrid::Build(registry);
                const auto on = NearestOpen(grid, t.x + t.w / 2, t.y + t.h / 2);
                if (!on) break;
                const Entity gate = CreateGateway(registry, on->first + 0.5f, on->second + 0.5f, b->scene, "from-" + PlanSlug(from));
                registry.emplace_or_replace<Name>(gate, "Gateway to " + b->brief.value("name", PlanSlug(to)));
                const auto [ox, oy] = OutsideDoor(t);
                if (const auto in = NearestOpen(grid, ox, oy)) AddMarker(registry, "from-" + PlanSlug(to), in->first + 0.5f, in->second + 0.5f);
                SaveScene(registry, project.Resolve(a->scene).string());
                return;
            }
        }
        const TileGrid grid = TileGrid::Build(registry);
        const Extent e = OpenExtent(registry, grid);
        const int k = used[from]++;
        const int midX = (e.minX + e.maxX) / 2, midY = (e.minY + e.maxY) / 2;
        // Past the four sides: along the side, a few cells further each time round (not on the first one's cell).
        const int round = k / 4, along = (round % 2 ? 1 : -1) * ((round + 1) / 2) * 6;
        const int targets[4][2] = {{e.maxX, midY + along}, {e.minX, midY + along}, {midX + along, e.maxY}, {midX + along, e.minY}};
        std::optional<std::pair<int, int>> cell;
        // A dungeon's (or a building's) way out is in its entrance (development_plan.md 32.8): the middle of what stands in
        // its entrance zone or hall, not its edge. The way back out only (to the place it's in: the outline's parent, else
        // its first link); others round the edge.
        const std::string parent = parentOf(a->place);
        const bool outward = FindRow(parent) ? FindRow(parent) == b : k == 0;
        if (outward && a->buildType != "settlement" && a->buildType != "world") {
            // As its plan has it (a painted place has nothing else that says so): an interior's front door, the gateway on
            // it and one arrives a step inside; a dungeon's entrance (its stairs, else the middle of its entrance room).
            if (const auto way = PlannedWayOut(PlannedFor(*a), grid)) {
                const Entity gw = CreateGateway(registry, way->first.first + 0.5f, way->first.second + 0.5f, b->scene, "from-" + PlanSlug(from));
                registry.emplace_or_replace<Name>(gw, "Gateway to " + b->brief.value("name", PlanSlug(to)));
                AddMarker(registry, "from-" + PlanSlug(to), way->second.first + 0.5f, way->second.second + 0.5f);
                SaveScene(registry, project.Resolve(a->scene).string());
                return;
            }
            double sx = 0, sy = 0;
            int n = 0;
            for (auto [ent, node, t] : registry.view<PlaceNode, WorldTransform2D>().each()) {
                if (node.node.find("entrance") == std::string::npos && node.statement.find("entrance") == std::string::npos) continue;
                sx += t.x, sy += t.y, ++n;
            }
            if (n > 0) cell = NearestOpen(grid, static_cast<int>(sx / n), static_cast<int>(sy / n));
        }
        if (!cell) cell = NearestOpen(grid, targets[k % 4][0], targets[k % 4][1]);
        if (!cell) return;
        const Entity edge = CreateGateway(registry, cell->first + 0.5f, cell->second + 0.5f, b->scene, "from-" + PlanSlug(from));
        registry.emplace_or_replace<Name>(edge, "Gateway to " + b->brief.value("name", PlanSlug(to)));
        // Where you arrive coming back: a step inside from the gateway.
        const int ix = cell->first + (cell->first > midX ? -2 : cell->first < midX ? 2 : 0);
        const int iy = cell->second + (cell->second > midY ? -2 : cell->second < midY ? 2 : 0);
        const auto in = NearestOpen(grid, ix, iy);
        if (in) AddMarker(registry, "from-" + PlanSlug(to), in->first + 0.5f, in->second + 0.5f);
        SaveScene(registry, project.Resolve(a->scene).string());
    };
    for (const auto& [a, b] : links) {
        place(a, b);
        place(b, a);
        Record("connect", a + " ↔ " + b, "gateways both ways");
    }
}

// --- ⑪ Populate and bind -------------------------------------------------------------------------------

// A behaviour plan's entries as a character's rules (Responses): its package names made its own.
static json BehaviourRules(const json& plan, const std::string& character) {
    json rules = json::array();
    for (const auto& entry : plan.value("behaviour", json::array())) {
        if (!entry.is_object()) continue;
        const json when = entry.value("when", json::object());
        std::string event = when.value("event", std::string());
        if (when.contains("quest")) event = "quest:" + when.value("quest", std::string()) + ":" + std::to_string(when.value("stage", 0));
        else if (when.contains("playerAction")) event = "action:" + when.value("playerAction", std::string());
        if (event.empty()) continue;
        const json does = entry.value("does", json::object());
        json rule = {{"event", event}};
        if (entry.contains("if")) rule["if"] = entry["if"];
        if (does.contains("clip")) rule["clip"] = does["clip"];
        if (does.contains("says")) rule["says"] = does["says"];
        if (when.contains("range")) rule["range"] = when["range"];
        if (entry.value("once", false)) rule["once"] = true;
        json effects = does.value("do", json::array());
        if (!effects.is_array()) effects = json::array();
        if (const std::string package = does.value("package", std::string()); !package.empty()) effects.push_back({{"op", "push_package"}, {"package", character + "-" + package}});
        if (const std::string done = does.value("stops", std::string()); !done.empty()) effects.push_back({{"op", "pop_package"}, {"package", character + "-" + done}});
        if (const std::string sends = does.value("sends", std::string()); !sends.empty()) effects.push_back({{"op", "send_event"}, {"event", sends}});
        if (!effects.empty()) rule["do"] = effects;
        rules.push_back(rule);
    }
    return rules;
}

// A prefab drawn already (⑪'s art: its sheet and animator) keeps its drawing when it's made again (characters placed
// again): its picture, animator and facing onto the new one before it's saved over.
static void KeepDrawing(PrefabLibrary& prefabs, const std::string& path, Registry& registry, Entity e) {
    if (!prefabs.Find(path)) return;
    prefabs.VisitTemplate(path, [&](Registry& from, Entity t) {
        if (!from.all_of<Animator>(t)) return;
        registry.emplace_or_replace<Animator>(e, from.get<Animator>(t));
        if (const auto* sprite = from.try_get<Sprite>(t)) registry.emplace_or_replace<Sprite>(e, *sprite);
        if (const auto* facing = from.try_get<Facing>(t)) registry.emplace_or_replace<Facing>(e, *facing);
    });
}

std::string GameBuild::PlaceCharactersAgain(const std::function<void(const std::string&)>& say) {
    if (stage_.empty() || stage_ == "build") return "the places aren't all built yet";
    const Project project{host_.projectRoot};
    // What ⑪ put in each scene goes: its characters (their Actor keys), the markers it and the story made (all but the
    // gateways' arrivals), the quests' items and the encounters' spawners. The places and their gateways stay.
    for (auto& row : rows_) {
        if (row.status != "built") continue;
        Registry reg;
        const std::string path = project.Resolve(row.scene).string();
        if (!LoadScene(reg, path)) continue;
        std::vector<Entity> gone;
        for (auto [e, actor] : reg.view<Actor>().each()) {
            // Not the player: it stays as it is (its drawing, kit and scripts), moved to where it starts.
            if (const auto* team = reg.try_get<Faction>(e); team && team->team == Team::Player) continue;
            if (actor.key.rfind("character/", 0) == 0) gone.push_back(e);
        }
        // (Every marker: the gateways' arrivals too, with the gateways ⑩ made: connected again below.)
        for (auto [e, marker] : reg.view<Marker>().each()) gone.push_back(e);
        for (auto [e, gateway] : reg.view<Gateway>(entt::exclude<PlaceNode>).each()) gone.push_back(e);
        for (auto [e, pickup] : reg.view<ItemPickup>(entt::exclude<PlaceNode>).each()) gone.push_back(e);
        for (auto [e, spawner] : reg.view<Spawner>().each()) {
            if (spawner.encounter.find("-encounter-") != std::string::npos) gone.push_back(e);
        }
        std::sort(gone.begin(), gone.end());
        gone.erase(std::unique(gone.begin(), gone.end()), gone.end());
        for (Entity e : gone) {
            if (reg.valid(e)) reg.destroy(e);
        }
        SaveScene(reg, path);
        // Its story's markers again (where one arrives, the anchors), the player moved to where it starts.
        try {
            FinishInEditor(row);
        } catch (const std::exception& e) {
            say(row.place + ": its story markers weren't added (" + e.what() + ")");
        }
        say(row.place + ": " + std::to_string(gone.size()) + " things ⑩–⑪ had placed taken out");
    }
    // The story markers came back where the plan has them: a painted square's anchors where the painting has the square,
    // the roads' ways out marked again, and the anchors by a gateway Placements moved, moved with it.
    SettleAreaAnchors();
    LayRoads();
    SettleGateAnchors();
    try {
        ConnectScenes(); // the gateways and where one arrives through them, again
    } catch (const std::exception& e) {
        return std::string("⑩ connecting the scenes: ") + e.what();
    }
    try {
        Populate();
    } catch (const std::exception& e) {
        return std::string("⑪ placing the characters (") + populateAt_ + "): " + e.what();
    }
    WriteFiles(); // the quests again, with their roles as they're bound now
    int bound = 0, failed = 0;
    for (const auto& a : bindings_.value("aliases", json::array())) (a.value("how", "") == "failed" ? failed : bound) += 1;
    for (const auto& a : bindings_.value("actors", json::array())) {
        say("  " + a.value("character", "") + " in " + a.value("scene", "") + (a.value("anchor", "").empty() ? "" : " at " + a.value("anchor", "")));
    }
    say(std::to_string(bound) + " quest roles found, " + std::to_string(failed) + " not");
    Record("populate", "characters", "placed again", "the user");
    Save();
    return {};
}

void GameBuild::Populate() {
    const Project project{host_.projectRoot};
    const StoryGraph g = StoryGraph::Load(StoryGraph::PathIn(host_.projectRoot)).value_or(StoryGraph{});
    const StoryOntology& gso = StoryOntology::Engine();
    std::map<std::string, std::unique_ptr<Registry>> scenes;
    auto sceneOf = [&](const std::string& place) -> Registry* {
        Row* r = FindRow(place);
        if (!r) return nullptr;
        auto& reg = scenes[r->scene];
        if (!reg) {
            reg = std::make_unique<Registry>();
            if (!LoadScene(*reg, project.Resolve(r->scene).string())) return nullptr;
        }
        return reg.get();
    };
    // A free open cell near a point, a cell clear of every other one taken in that scene (no one on anyone's cell).
    std::map<Registry*, std::set<std::pair<int, int>>> taken;
    auto freeNear = [&](Registry& reg, int cx, int cy) -> std::pair<float, float> {
        const TileGrid grid = TileGrid::Build(reg);
        auto [slot, fresh] = taken.try_emplace(&reg);
        auto& used = slot->second;
        if (fresh) {
            for (auto [e, gw, w] : reg.view<Gateway, WorldTransform2D>().each()) used.insert({static_cast<int>(std::floor(w.x)), static_cast<int>(std::floor(w.y))});
        }
        auto clear = [&](int x, int y) {
            if (grid.IsSolid(x, y)) return false;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (used.contains({x + dx, y + dy})) return false;
                }
            }
            return true;
        };
        for (int r = 0; r <= 40; ++r) {
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r || !clear(cx + dx, cy + dy)) continue;
                    used.insert({cx + dx, cy + dy});
                    return {cx + dx + 0.5f, cy + dy + 0.5f};
                }
            }
        }
        const auto c = NearestOpen(grid, cx, cy);
        return c ? std::make_pair(c->first + 0.5f, c->second + 0.5f) : std::make_pair(cx + 0.5f, cy + 0.5f);
    };
    auto spot = [&](Registry& reg, const std::string& marker, int) -> std::pair<float, float> {
        if (auto m = MarkerAt(reg, marker)) return *m;
        const auto arrive = MarkerAt(reg, "arrive").value_or(std::make_pair(5.5f, 5.5f));
        return freeNear(reg, static_cast<int>(arrive.first), static_cast<int>(arrive.second) + 2);
    };
    std::map<std::string, std::vector<PlannedThing>> planned;
    auto plannedIn = [&](const std::string& place) -> const std::vector<PlannedThing>& {
        auto [it, fresh] = planned.try_emplace(place);
        const Row* row = FindRow(place);
        if (fresh && row) it->second = ThingsOf(*row);
        return it->second;
    };
    // The placements (development_plan.md 34.11, Placements.cpp): where each character and story prop stands, decided by the
    // AI with Replace Characters (or once a painted place's buildings are found), by rule now when nothing is decided yet;
    // their spots found in the scenes as they are (their markers made), so everyone below is placed by them.
    populateAt_ = "the placements";
    placements_ = ReadPlacements(host_.projectRoot);
    if (placements_.value("characters", json::array()).empty() && placements_.value("props", json::array()).empty()) {
        const json buildings = PlacementBuildings();
        std::vector<std::string> notes;
        placements_ = MergePlacements(json::object(), RulePlacements(buildings), json::object(), buildings, notes);
        placements_["notes"] = notes;
        placements_["why"] = "⑪ placing the characters (by rule: nothing was decided yet)";
    }
    ResolvePlacements([&](const std::string& scene) -> Registry* {
        if (scene.empty()) return nullptr;
        auto& reg = scenes[scene];
        if (!reg) {
            reg = std::make_unique<Registry>();
            if (!LoadScene(*reg, project.Resolve(scene).string())) {
                reg.reset();
                return nullptr;
            }
        }
        return reg.get();
    });
    // What ⑪ did with each (the spot it stands on, its own marker), recorded in the placements.
    auto notePlaced = [&](const char* list, const std::string& id, const std::string& scene, std::pair<float, float> at, const std::string& marker) {
        if (!placements_.contains(list)) return;
        for (auto& e : placements_[list]) {
            if (e.value("id", "") != id || !e["spot"].is_object()) continue;
            e["spot"]["scene"] = scene;
            e["spot"]["x"] = static_cast<int>(std::floor(at.first));
            e["spot"]["y"] = static_cast<int>(std::floor(at.second));
            e["spot"]["own"] = marker;
            e["scene"] = scene;
            if (e["whereabouts"].is_array() && !e["whereabouts"].empty() && e["whereabouts"][0].value("from", json::object()).value("start", false)) {
                e["whereabouts"][0]["scene"] = scene;
                e["whereabouts"][0]["spot"] = e["spot"];
                e["whereabouts"][0]["marker"] = marker;
            }
        }
    };
    populateAt_.clear();
    // The places a character's document names: its home, then where its day takes it.
    auto refsOf = [](const json& c) {
        std::vector<std::string> refs;
        if (!c.value("home", "").empty()) refs.push_back(c.value("home", ""));
        const json behaviour = c.value("behaviour", json::object());
        for (const auto& p : behaviour.is_object() ? behaviour.value("packages", json::array()) : json::array()) {
            if (p.is_object() && !p.value("place", "").empty()) refs.push_back(p.value("place", ""));
        }
        return refs;
    };
    auto homeOf = [&](const std::string& character) -> std::string {
        // The shop, workshop, inn or temple it keeps (an interior whose purpose names it and has it among its residents
        // or actors: "Bera sells armor and works at her village smithy"; not a crypt it only visits in the story), else
        // its own home (its character's document), else the built place a home it names is in ("bellwether-bakery" in
        // Bellwether), else where the story has it with no time on it (a visit in a scene isn't where it lives), else
        // where it's first seen. The placements say it first (the scene its spot is in).
        if (const json* p = PlacementOf("character", character)) {
            const std::string place = RowPlaceOfScene(p->value("spot", json::object()).value("scene", ""));
            if (!place.empty()) return place;
        }
        const json c = store_.Read("characters/" + PlanSlug(character)).value_or(json::object());
        const std::string name = c.value("name", "");
        for (const auto& row : rows_) {
            if (row.buildType != "interior" || name.empty()) continue;
            const json d = store_.Read("places/" + PlanSlug(row.place)).value_or(json::object());
            const json about = d.value("description", json());
            const std::string purpose = about.is_object() ? about.value("purpose", "") : "";
            const std::vector<std::string> kind = SlotsFor(purpose);
            static const std::set<std::string> kept = {"shop", "workshop", "inn", "temple", "stable"};
            if (kind.empty() || !kept.contains(kind.front()) || purpose.find(name.substr(0, name.find(' '))) == std::string::npos) continue;
            bool there = false;
            for (const auto& a : d.value("actors", json::array())) there = there || (a.is_object() && NamesCharacter(a.value("character", ""), character, name));
            for (const auto& r : about.is_object() ? about.value("residents", json::array()) : json::array()) {
                there = there || (r.is_string() && NamesCharacter(r.get<std::string>(), character, name));
            }
            if (there) return row.place;
        }
        if (FindRow(c.value("home", ""))) return c.value("home", "");
        for (const auto& ref : refsOf(c)) {
            const std::string slug = PlanSlug(ref);
            for (const auto& row : rows_) {
                if (slug.rfind(PlanSlug(row.place) + "-", 0) == 0) return row.place;
            }
        }
        for (const auto* e : g.Out(character, "locatedAt")) {
            if (e->from.empty() && e->until.empty() && FindRow(e->o)) return e->o;
        }
        // Where it's seen first.
        for (const auto& s : g.SceneOrder()) {
            const auto who = g.Participants(s);
            if (std::find(who.begin(), who.end(), character) != who.end() && FindRow(g.LocationOfScene(s))) return g.LocationOfScene(s);
        }
        return rows_.empty() ? std::string() : rows_.front().place;
    };
    json actors = json::array(), aliases = json::array();
    std::map<std::string, std::string> entityOf; // character -> entity name
    int offset = 0;
    // ⑦'s records: the classes characters are made from, the merchants' stock (development_plan.md 32.2).
    const PlanRecords records(store_.Read("systems").value_or(json::object()));
    PrefabLibrary prefabs(project);
    packages_ = json::object();
    populateAt_ = "the enemy classes' prefabs";
    // A prefab for every kind of enemy (its class), for what the story and spawners make (a quest's ambush).
    for (const auto& id : records.Ids("character-class")) {
        const json& k = *records.Find("character-class", id);
        if (k.value("role", "") == "civilian" || k.value("combat", "") == "none") continue;
        Registry temp;
        CastSpec spec;
        spec.kind = k.value("role", "") == "boss" ? CastKind::Boss : CastKind::Enemy;
        spec.cls = id;
        const Entity e = CreateEnemy(temp, 0.0f, 0.0f, entt::null);
        temp.emplace<Perception>(e);
        ApplyCast(temp, e, spec, records);
        temp.emplace_or_replace<Name>(e, k.value("name", id));
        const std::string path = "prefabs/enemies/" + id + ".prefab";
        KeepDrawing(prefabs, path, temp, e);
        if (prefabs.SaveAsPrefab(temp, e, path)) files_[path] = {{"planner", "systems"}};
    }
    // A character's anchor in a place (the place's description: actors [{character, anchor}]).
    auto anchorIn = [&](const std::string& place, const std::string& character) -> std::string {
        const json d = store_.Read("places/" + PlanSlug(place)).value_or(json::object());
        const std::string name = store_.Read("characters/" + PlanSlug(character)).value_or(json::object()).value("name", "");
        for (const auto& a : d.value("actors", json::array())) {
            if (a.is_object() && NamesCharacter(a.value("character", ""), character, name)) return a.value("anchor", "");
        }
        return {};
    };
    // Where it belongs in a place by what its document says (its home, where it works, its class, roles and line): the
    // door of a house of its own, its shop, the inn, the graves, the gate; each house and shop one's own while there
    // are enough. Its door's cell, or none (the place's plan has nothing it names).
    std::map<std::string, int> housed; // place + thing -> how many there
    auto belongsAt = [&](Registry& reg, const std::string& place, const std::string& character) -> std::optional<std::pair<int, int>> {
        const auto& things = plannedIn(place);
        if (things.empty()) return std::nullopt;
        auto aDoorway = [&](const PlannedThing& t) {
            const auto [dx, dy] = DoorOf(t);
            for (auto [e, gw, w] : reg.view<Gateway, WorldTransform2D>().each()) {
                if (std::abs(w.x - dx - 0.5f) < 2.0f && std::abs(w.y - dy - 0.5f) < 2.0f && t.slot != "gateway-gate") return true;
            }
            return false;
        };
        const json c = store_.Read("characters/" + PlanSlug(character)).value_or(json::object());
        std::vector<std::string> texts;
        for (const auto& ref : refsOf(c)) {
            std::string words = PlanSlug(ref);
            std::replace(words.begin(), words.end(), '-', ' ');
            texts.push_back(words);
        }
        const json gameplay = c.value("gameplay", json::object());
        if (gameplay.is_object()) {
            texts.push_back(gameplay.value("class", ""));
            std::string roles;
            for (const auto& r : gameplay.value("roles", json::array())) roles += (r.is_string() ? r.get<std::string>() : "") + " ";
            texts.push_back(roles);
        }
        texts.push_back(c.value("line", ""));
        texts.push_back("home");
        for (const auto& text : texts) {
            const std::vector<std::string> slots = SlotsFor(text);
            const PlannedThing* best = nullptr;
            int fewest = 0;
            for (const auto& slot : slots) {
                for (const auto& t : things) {
                    if (t.slot != slot || aDoorway(t)) continue;
                    const int n = housed[place + "|" + t.id];
                    if (!best || n < fewest) best = &t, fewest = n;
                }
                if (best && fewest == 0) break; // a free one of the kind it's named first
            }
            if (!best) continue;
            ++housed[place + "|" + best->id];
            return DoorOf(*best);
        }
        return std::nullopt;
    };
    // Where it stands in a place, as a marker: beside its anchor there, else beside the way in.
    auto standAt = [&](Registry& reg, const std::string& place, const std::string& character, const std::string& marker) {
        if (auto m = MarkerAt(reg, marker)) return *m;
        // Its spot in the placements (a building's door, inside its interior, a marker, near one), when it's in this scene.
        if (marker == "start-" + PlanSlug(character)) {
            const Row* r = FindRow(place);
            const json* p = PlacementOf("character", character);
            const json s = p ? p->value("spot", json::object()) : json::object();
            if (r && s.contains("x") && s.value("scene", "") == r->scene) {
                const auto at = freeNear(reg, s.value("x", 0), s.value("y", 0));
                AddMarker(reg, marker, at.first, at.second);
                return at;
            }
        }
        const std::string anchor = anchorIn(place, character);
        std::pair<float, float> at;
        if (auto a = anchor.empty() ? std::nullopt : MarkerAt(reg, anchor)) {
            at = freeNear(reg, static_cast<int>(a->first), static_cast<int>(a->second) + 1);
        } else if (const auto door = belongsAt(reg, place, character)) {
            at = freeNear(reg, door->first, door->second);
        } else {
            at = spot(reg, std::string(), offset++ % 5);
        }
        AddMarker(reg, marker, at.first, at.second);
        return at;
    };
    // A package record from a plan's {template, place, schedule, target}: where it's done (its home, work or bed
    // when in its home place; else a marker made in that place's scene) and when.
    auto packageRecord = [&](const std::string& character, const std::string& home, const json& p, const std::string& description) {
        const std::string slug = PlanSlug(character), tmpl = p.value("template", "");
        json package = {{"template", tmpl}, {"description", description}};
        const std::string place = p.value("place", "").empty() ? home : p.value("place", "");
        if (place == home) {
            static const std::set<std::string> work = {"work", "vendor", "tend", "use-part", "use-object"};
            package["location"] = {{"alias", tmpl == "sleep" ? "bed" : work.contains(tmpl) ? "work" : "home"}};
        } else if (Registry* there = sceneOf(place)) {
            const std::string marker = slug + "-" + tmpl;
            standAt(*there, place, character, marker);
            package["location"] = {{"scene", FindRow(place)->scene}, {"marker", marker}};
        }
        if (const json when = p.value("schedule", json()); when.is_object()) {
            json schedule = json::object();
            for (const char* k : {"from", "to"}) {
                if (when.contains(k) && when[k].is_string()) schedule[k] = when[k];
            }
            if (const json days = when.value("days", json()); days.is_string()) schedule["days"] = days;
            else if (days.is_array()) {
                std::string d;
                for (const auto& v : days) d += (d.empty() ? "" : ",") + (v.is_number() ? std::to_string(v.get<int>()) : v.is_string() ? v.get<std::string>() : "");
                schedule["days"] = d;
            }
            if (!schedule.empty()) package["schedule"] = schedule;
        }
        if (const std::string target = p.value("target", ""); !target.empty()) package["target"] = target;
        return package;
    };
    // The characters, each at its anchor in its home, made from its class and level, as itself in every scene
    // (an Actor with its own prefab, so its day and the story can take it elsewhere).
    for (const auto* n : g.OfClass("Character", gso)) {
        if (n->cls == "PlayerCharacter") continue;
        const std::string home = homeOf(n->id);
        Registry* reg = sceneOf(home);
        if (!reg) continue;
        const std::string slug = PlanSlug(n->id);
        populateAt_ = "the character " + n->id;
        const std::string name = n->Text("name").empty() ? slug : n->Text("name");
        const json c = store_.Read("characters/" + slug).value_or(json::object());
        const json gameplay = c.value("gameplay", json::object());
        CastSpec spec;
        spec.kind = CastKind::Civilian;
        for (const auto* e : g.Out(n->id, "fills")) {
            const StoryNode* role = g.Find(e->o);
            const std::string r = role ? role->Text("role") : "";
            if (r == "boss") spec.kind = CastKind::Boss;
            else if ((r == "enemy" || r == "elite") && spec.kind != CastKind::Boss) spec.kind = CastKind::Enemy;
        }
        for (const auto& r : gameplay.value("roles", json::array())) {
            if (r == "boss") spec.kind = CastKind::Boss;
            else if (r == "enemy" && spec.kind == CastKind::Civilian) spec.kind = CastKind::Enemy;
        }
        const bool hostile = spec.kind != CastKind::Civilian;
        // Where they stand when the story starts: their start-<slug> marker (home-<slug> is their home's).
        const auto at = standAt(*reg, home, n->id, "start-" + slug);
        notePlaced("characters", n->id, FindRow(home)->scene, at, "start-" + slug);
        // One of each: a copy already in the scene (⑪ run again; ⑫ may have given it scripts) is kept where it stands,
        // the one with a script else the first, and any other copies go.
        Entity e = entt::null;
        std::vector<Entity> copies;
        for (auto [x, actor] : reg->view<Actor>().each()) {
            if (actor.key == n->id) copies.push_back(x);
        }
        for (Entity x : copies) {
            if (e == entt::null || (!reg->all_of<LuaScript>(e) && reg->all_of<LuaScript>(x))) e = x;
        }
        for (Entity x : copies) {
            if (x != e) reg->destroy(x);
        }
        if (copies.size() > 1) Record("populate", n->id, "1 kept", std::to_string(copies.size() - 1) + " other copies taken out");
        if (e == entt::null) {
            e = hostile ? CreateEnemy(*reg, at.first, at.second, entt::null) : CreateNpc(*reg, at.first, at.second);
        }
        if (!hostile) reg->emplace_or_replace<Talk>(e, "dialogue/" + slug + ".json", n->id, 1.5f);
        // Its day: every one has a Behaviour (so a quest can give it a package later), its places as markers.
        auto& behaviour = reg->get_or_emplace<Behaviour>(e);
        behaviour.home = behaviour.work = behaviour.bed = "start-" + slug;
        // Their bed and workplace (Placements: their home's door or inside it, their workplace's), when they have them.
        json placed = json::object();
        for (const auto& pc : placements_.value("characters", json::array())) {
            if (pc.value("id", "") == n->id) placed = pc;
        }
        if (const json b = placed.value("bed", json()); b.is_object() && MarkerAt(*reg, b.value("marker", ""))) behaviour.home = behaviour.bed = b.value("marker", "");
        if (const json w = placed.value("workAt", json()); w.is_object() && MarkerAt(*reg, w.value("marker", ""))) behaviour.work = w.value("marker", "");
        std::string packages;
        for (const auto& p : c.value("behaviour", json::object()).value("packages", json::array())) {
            if (!p.is_object() || p.value("template", "").empty()) continue;
            const std::string id = slug + "-" + p.value("template", "");
            packages_[id] = packageRecord(n->id, home, p, name + ": " + p.value("template", ""));
            packages += (packages.empty() ? "" : ", ") + id;
        }
        // The cast timeline (⑥b): each quest's packages for the stages it holds them, ranked (story, then the main quest,
        // then the side quests), before their own day: the first that holds runs, so where they are and what they do is the
        // highest-ranked quest's while it's on, the next one's after, their own day when none is.
        std::string stacked;
        for (const auto& t : TimelinePackagePlan(n->id)) {
            json plan = t["plan"];
            if (plan.value("place", "").empty() || !FindRow(plan.value("place", ""))) continue;
            const std::string id = slug + "-" + PlanSlug(t.value("id", ""));
            json record;
            if (FindRow(plan.value("place", ""))->scene == FindRow(home)->scene && placed.is_object() && placed.contains("id")) {
                // In their own scene: where Placements says (their bed, their workplace, a marker the timeline names).
                const json at = PackageSpot(placed, plan.value("template", ""), t.value("spot", ""), [&](const std::string& m) { return MarkerAt(*reg, m).has_value(); });
                record = packageRecord(n->id, plan.value("place", ""), plan, name + ": " + plan.value("template", "") + " " + at.value("label", "") + " (" + t.value("quest", "") + ")");
                record["location"] = {{"scene", FindRow(home)->scene}, {"marker", at.value("marker", "")}};
            } else {
                record = packageRecord(n->id, std::string(), plan, name + ": " + plan.value("template", "") + " (" + t.value("quest", "") + ")");
            }
            json conditions = json::array();
            if (!t.value("stages", json::array()).empty()) conditions.push_back({{"quest", t.value("quest", "")}, {"questState", "active"}, {"stages", t["stages"]}});
            record["conditions"] = conditions;
            packages_[id] = record;
            stacked += (stacked.empty() ? "" : ", ") + id;
            if (t.value("last", false)) {
                // After the quest is done, they stay as its last stage left them.
                json after = record;
                after["conditions"] = json::array({{{"quest", t.value("quest", "")}, {"questState", "completed"}}});
                packages_[id + "-after"] = after;
                stacked += ", " + id + "-after";
            }
        }
        if (!stacked.empty()) packages = stacked + (packages.empty() ? "" : ", " + packages);
        if (hostile && packages.empty()) reg->remove<Behaviour>(e); // an enemy with no day: it guards where it stands (Follow)
        else behaviour.packages = packages;
        // Packages its behaviour plan pushes (on a quest's stage, an event).
        const json plan = store_.Read("behaviours/" + slug).value_or(json::object());
        for (const auto& p : plan.value("packages", json::array())) {
            if (!p.is_object()) continue;
            const std::string id = slug + "-" + p.value("id", p.value("template", ""));
            if (!packages_.contains(id)) packages_[id] = packageRecord(n->id, home, p, plan.value("character", name) + ": " + p.value("id", ""));
        }
        // Its numbers, fighting, kit and what killing it gives: from its class and level.
        spec.level = gameplay.value("level", 1);
        spec.cls = ClassFor(records, gameplay.value("class", ""), spec.kind);
        spec.gameplay = gameplay;
        if (records.Find("loot", "stock-" + slug)) spec.stock = "stock-" + slug;
        else if (const std::string stock = gameplay.value("stock", ""); records.Find("loot", stock)) spec.stock = stock;
        const json cast = ApplyCast(*reg, e, spec, records);
        // What it does when things happen (⑦): its rules, from its behaviour plan.
        if (const json rules = BehaviourRules(plan, slug); !rules.empty()) reg->emplace_or_replace<Responses>(e, Responses{rules.dump()});
        reg->emplace_or_replace<Name>(e, name);
        const std::string prefab = "prefabs/characters/" + slug + ".prefab";
        reg->emplace_or_replace<Actor>(e, n->id, prefab);
        std::string why;
        KeepDrawing(prefabs, prefab, *reg, e);
        if (prefabs.SaveAsPrefab(*reg, e, prefab, &why)) files_[prefab] = {{"planner", "characters/" + slug}};
        entityOf[n->id] = name;
        actors.push_back({{"character", n->id}, {"scene", FindRow(home)->scene}, {"entity", name}, {"boss", hostile},
                          {"marker", "start-" + slug}, {"anchor", anchorIn(home, n->id)}, {"cast", cast}, {"packages", packages}});
    }
    populateAt_ = "the player";
    // The player, from its own document: its class, level, abilities and kit (it stands in the start scene).
    for (const auto* n : g.OfClass("Character", gso)) {
        if (n->cls != "PlayerCharacter") continue;
        Registry* reg = nullptr;
        for (const auto& r : rows_) {
            if (r.scene == StartScene()) reg = sceneOf(r.place);
        }
        if (!reg) break;
        // A player that lost its prefab (made bare again where it starts): its prefab back in its place (its drawing,
        // kit, dialogue and scripts).
        {
            Entity bare = entt::null;
            for (auto [e, team] : reg->view<Faction>().each()) {
                if (team.team == Team::Player && bare == entt::null) bare = e;
            }
            const std::string prefab = "prefabs/characters/" + PlanSlug(n->id) + ".prefab";
            if (bare != entt::null && !reg->all_of<PrefabInstance>(bare) && prefabs.Find(prefab)) {
                const auto* w = reg->try_get<WorldTransform2D>(bare);
                const float x = w ? w->x : 0.0f, y = w ? w->y : 0.0f;
                if (prefabs.Instantiate(*reg, prefab, x, y)) {
                    reg->destroy(bare);
                    Record("populate", n->id, "prefab back", "the player had lost its prefab: " + prefab);
                }
            }
        }
        for (auto [e, team] : reg->view<Faction>().each()) {
            if (team.team != Team::Player) continue;
            const json c = store_.Read("characters/" + PlanSlug(n->id)).value_or(json::object());
            const json brief = store_.Read("brief").value_or(json::object());
            const json on = BriefGet(brief, "systems.combat.on"), style = BriefGet(brief, "systems.combat.style");
            CastSpec spec;
            spec.kind = CastKind::Player;
            spec.gameplay = c.value("gameplay", json::object());
            spec.level = spec.gameplay.value("level", 1);
            spec.cls = ClassFor(records, spec.gameplay.value("class", ""), CastKind::Player);
            spec.fights = on.is_boolean() ? on.get<bool>() : style.is_string() && style.get<std::string>() != "none";
            const json cast = ApplyCast(*reg, e, spec, records);
            if (!n->Text("name").empty()) reg->emplace_or_replace<Name>(e, n->Text("name"));
            actors.push_back({{"character", n->id}, {"player", true}, {"scene", StartScene()}, {"cast", cast}});
            break;
        }
        break;
    }
    populateAt_ = "the places' enemies and chests";
    // The places' own enemies and chests (development_plan.md 32.3, 32.4): what the plan scattered in its enemy
    // and boss slots fights, made from the enemy class it shows (a skeleton: the skeleton class) at the place's
    // level; its chests and loot caches hold a loot table's things.
    std::map<std::string, int> levelOf; // place -> the level of the quests played there
    for (const auto& doc : store_.List("quests")) {
        const json q = store_.Read(doc).value_or(json::object());
        const json lv = q.value("level", json());
        const int low = lv.is_array() && !lv.empty() && lv[0].is_number() ? lv[0].get<int>() : lv.is_number() ? lv.get<int>() : 0;
        if (low <= 0) continue;
        for (const auto& a : q.value("aliases", json::array())) {
            const std::string in = a.value("in", "");
            if (!in.empty()) levelOf[in] = std::max(levelOf[in], low);
        }
    }
    json placed = json::array();
    for (const auto& r : rows_) {
        if (r.build.empty() || r.status != "built") continue;
        std::ifstream in(host_.projectRoot / "places" / r.build / "layout.solved.json", std::ios::binary);
        const json solved = in ? json::parse(in, nullptr, false) : json();
        if (!solved.is_object()) continue;
        std::map<std::string, const json*> bySlot; // statement id -> its object (pointing into `objects`, kept alive here)
        const json objects = solved.value("objects", json::array());
        for (const auto& o : objects) {
            const std::string slot = o.value("slot", "");
            if (slot == "enemy" || slot == "boss" || slot == "chest" || slot == "loot-cache") bySlot[o.value("id", "")] = &o;
        }
        if (bySlot.empty()) continue;
        Registry* reg = sceneOf(r.place);
        if (!reg) continue;
        const int level = std::max(1, levelOf.contains(r.place) ? levelOf[r.place] : 1);
        int enemies = 0, chests = 0, spawners = 0;
        // Where enemies come back (a dungeon, a camp, the wilds; not a town or a house): each encounter zone's enemies
        // are a Spawner's instead, the same number of the same classes, back a while after they die (32.3).
        static const std::set<std::string> kSettled = {"settlement", "interior", "harbour", "castle", "farmland", "world"};
        const bool returns = !kSettled.contains(r.buildType);
        struct Zone {
            double x = 0, y = 0;
            std::vector<std::pair<float, float>> at;
            std::map<std::string, int> classes;
        };
        std::map<std::string, Zone> zones; // its node -> its enemies
        std::vector<Entity> spawned;       // the placed ones the spawners stand for
        for (auto [e, node] : reg->view<PlaceNode>().each()) {
            const auto it = bySlot.find(node.statement);
            if (it == bySlot.end()) continue;
            const json& o = *it->second;
            const std::string slot = o.value("slot", "");
            if (slot == "chest" || slot == "loot-cache") {
                // A loot table for it: the place's own, else one for chests.
                std::string loot;
                for (const std::string& id : {PlanSlug(r.place) + "-chest", r.buildType + "-chest", std::string("chest")}) {
                    if (records.Find("loot", id)) {
                        loot = id;
                        break;
                    }
                }
                if (loot.empty()) {
                    for (const auto& id : records.Ids("loot")) {
                        if (id.find("chest") != std::string::npos || id.find("treasure") != std::string::npos) loot = id;
                    }
                }
                auto& box = reg->get_or_emplace<Container>(e);
                if (box.loot.empty()) box.loot = loot;
                box.gold = std::max(box.gold, 5 * level);
                ++chests;
                continue;
            }
            // An enemy where it stands: fights, perceives, drops its loot.
            CastSpec spec;
            spec.kind = slot == "boss" ? CastKind::Boss : CastKind::Enemy;
            spec.level = level;
            spec.cls = ClassFor(records, o.value("shows", "") + " " + o.value("purpose", ""), spec.kind);
            if (returns && slot == "enemy" && !spec.cls.empty() && reg->all_of<WorldTransform2D>(e)) {
                const auto& w = reg->get<WorldTransform2D>(e);
                Zone& z = zones[o.value("node", node.node)];
                z.at.push_back({w.x, w.y});
                z.x += w.x, z.y += w.y;
                ++z.classes[spec.cls];
                spawned.push_back(e);
                continue;
            }
            reg->emplace_or_replace<Faction>(e, Team::Enemy);
            reg->get_or_emplace<Hurtbox>(e);
            if (!reg->all_of<Collider2D>(e)) reg->emplace<Collider2D>(e, 0.3f, 0.3f);
            if (!reg->all_of<Follow>(e)) reg->emplace<Follow>(e, entt::null, 2.0f, 0.8f, 6.0f);
            reg->emplace_or_replace<Perception>(e);
            const json cast = ApplyCast(*reg, e, spec, records);
            if (!reg->all_of<Name>(e)) reg->emplace<Name>(e, o.value("shows", "enemy"));
            placed.push_back({{"scene", r.scene}, {"object", o.value("id", "")}, {"cast", cast}});
            ++enemies;
        }
        // The spawners: an encounter record each (its classes' prefabs, weighted by how many), at the middle of its
        // zone's enemies, as far round as they stood.
        if (!zones.empty()) {
            RecordDatabase encounterRecords;
            encounterRecords.Open(EngineReflection(), host_.projectRoot);
            int n = 0;
            for (auto& [zoneNode, z] : zones) {
                const int count = static_cast<int>(z.at.size());
                const float cx = static_cast<float>(z.x / count), cy = static_cast<float>(z.y / count);
                float radius = 2.0f;
                for (const auto& [x, y] : z.at) radius = std::max(radius, std::hypot(x - cx, y - cy) + 1.0f);
                json entries = json::array();
                for (const auto& [cls, k] : z.classes) {
                    entries.push_back({{"prefab", "prefabs/enemies/" + cls + ".prefab"}, {"weight", k}, {"min", 1}, {"max", 1}});
                }
                const std::string id = PlanSlug(r.place) + "-encounter-" + std::to_string(++n);
                if (const auto why = encounterRecords.Write("encounter", id, {{"name", r.place + ": " + zoneNode}, {"entries", entries}})) {
                    Record("refused", "records/encounter/" + id, *why);
                    continue;
                }
                files_["records/encounter/" + id + ".json"] = {{"planner", "places/" + PlanSlug(r.place)}};
                const Entity sp = reg->create();
                reg->emplace<WorldTransform2D>(sp, cx, cy);
                reg->emplace<Name>(sp, id);
                Spawner spawner;
                spawner.encounter = id;
                spawner.radius = std::min(radius, 12.0f);
                spawner.maxAlive = count;
                spawner.respawnSeconds = 300.0f; // five minutes after one dies, another
                spawner.level = level;
                reg->emplace<Spawner>(sp, spawner);
                placed.push_back({{"scene", r.scene}, {"spawner", id}, {"count", count}, {"cast", {{"combat", "melee"}}}});
                enemies += count;
                ++spawners;
            }
            for (Entity e : spawned) {
                if (reg->valid(e)) reg->destroy(e);
            }
        }
        Record("populate", r.place, std::to_string(enemies) + " enemies (" + std::to_string(spawners) + " spawners), " +
               std::to_string(chests) + " chests", "level " + std::to_string(level));
    }
    populateAt_ = "the story's scenes";
    // The story's scenes (development_plan.md 33.6): each its place and cast. A quest's role is made where the scene that
    // uses it happens, and a stage brings the scene's cast there (StoryStaging), each to a marker of its own beside where
    // the scene happens (its first part's anchor, else where one arrives).
    auto str = [](const json& j, const char* key) {
        const auto it = j.is_object() ? j.find(key) : j.end();
        return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    auto sceneCard = [&](const std::string& id) {
        const json s = store_.Read("story/scenes/" + id).value_or(json::object());
        const auto it = s.is_object() ? s.find("card") : s.end();
        return it != s.end() && it->is_object() ? *it : json::object();
    };
    // The scenes of the stages that use a role (an objective's target, an effect's spot), in the quest's order.
    auto storyScenesOf = [&](const json& q, const std::string& alias) {
        std::vector<std::string> ids;
        for (const auto& s : q.value("stages", json::array())) {
            bool uses = false;
            for (const auto& o : s.value("objectives", json::array())) uses = uses || str(o, "target") == alias;
            for (const auto& e : s.value("onEnter", json::array())) uses = uses || str(e, "at") == alias;
            if (uses && !str(s, "advancesScene").empty()) ids.push_back(str(s, "advancesScene"));
        }
        return ids;
    };
    // Where the story has a role: the place of the first scene that uses it, else of a scene that needs it as a part.
    auto storyPlaceOf = [&](const json& q, const std::string& alias) -> std::string {
        for (const auto& id : storyScenesOf(q, alias)) {
            if (const std::string loc = str(sceneCard(id), "location"); FindRow(loc)) return loc;
        }
        for (const auto& doc : store_.List("story/scenes")) {
            const json card = sceneCard(doc.substr(doc.rfind('/') + 1));
            for (const auto& p : card.value("needsParts", json::array())) {
                if (p.is_string() && PlanSlug(p.get<std::string>()) == PlanSlug(alias) && FindRow(str(card, "location"))) return str(card, "location");
            }
        }
        return {};
    };
    std::set<std::string> fighters; // the characters placed who fight
    for (const auto& act : actors) {
        if (act.value("boss", false)) fighters.insert(act.value("character", ""));
    }
    {
        std::set<std::string> players;
        for (const auto& act : actors) {
            if (act.value("player", false)) players.insert(act.value("character", ""));
        }
        // Each story scene's quest stage (the stage that plays it): where its cast is then (their whereabouts).
        std::map<std::string, std::pair<std::string, int>> stageOfScene;
        for (const auto& doc : store_.List("quests")) {
            const json q = store_.Read(doc).value_or(json::object());
            for (const auto& s : q.value("stages", json::array())) {
                if (!str(s, "advancesScene").empty() && !stageOfScene.contains(str(s, "advancesScene"))) stageOfScene[str(s, "advancesScene")] = {str(q, "id"), s.value("index", 0)};
            }
        }
        for (const auto& doc : store_.List("story/scenes")) {
            const std::string id = doc.substr(doc.rfind('/') + 1);
            const json card = sceneCard(id);
            const Row* r = FindRow(str(card, "location"));
            Registry* reg = r ? sceneOf(r->place) : nullptr;
            if (!reg) continue;
            // Where the scene happens: the part it needs, else the place's middle (its centrepiece), else where one arrives.
            std::optional<std::pair<float, float>> focus;
            for (const auto& p : card.value("needsParts", json::array())) {
                if (!focus && p.is_string()) focus = MarkerAt(*reg, PlanSlug(p.get<std::string>()));
            }
            for (const auto& b : placements_.value("buildings", json::array())) {
                if (!focus && b.value("scene", "") == r->scene && b.value("kind", "") == "centrepiece" && b["door"].is_array() && b["door"].size() == 2) {
                    focus = std::make_pair(b["door"][0].get<int>() + 0.5f, b["door"][1].get<int>() + 1.5f);
                }
            }
            if (!focus) focus = MarkerAt(*reg, "arrive");
            if (!focus) continue;
            const auto when = stageOfScene.find(id);
            for (const auto& p : card.value("participants", json::array())) {
                const std::string who = str(p, "who");
                if (who.empty() || players.contains(who)) continue;
                const std::string marker = "story/" + id + "/" + PlanSlug(who);
                if (MarkerAt(*reg, marker)) continue;
                std::string home;
                for (const auto& act : actors) {
                    if (act.value("character", "") == who) home = act.value("scene", "");
                }
                if (!home.empty() && home != r->scene && MetAt(who, str(card, "location")) != str(card, "location")) continue; // met in their own shop
                // Where their whereabouts have them as the scene's stage begins, when that's here; else where it happens.
                std::pair<float, float> toward = *focus;
                if (when != stageOfScene.end()) {
                    const json w = WhereaboutsAt(who, when->second.first, when->second.second);
                    const json s = w.is_object() ? w.value("spot", json::object()) : json::object();
                    const bool moved = w.is_object() && !w.value("from", json::object()).value("start", false); // a move of the story's, not where they start
                    if (moved && w.value("scene", "") == r->scene && s.contains("x")) toward = {s.value("x", 0) + 0.5f, s.value("y", 0) + 0.5f};
                }
                const auto at = freeNear(*reg, static_cast<int>(toward.first), static_cast<int>(toward.second) + (toward == *focus ? 1 : 0));
                AddMarker(*reg, marker, at.first, at.second);
            }
        }
    }
    populateAt_ = "the quests' aliases";
    // The quests' aliases.
    for (const auto& doc : store_.List("quests")) {
        const json q = store_.Read(doc).value_or(json::object());
        for (const auto& a : q.value("aliases", json::array())) {
            json b = {{"quest", q.value("id", "")}, {"alias", a.value("id", "")}};
            const std::string kind = a.value("kind", ""), fill = a.value("fill", ""), in = a.value("in", "");
            if (kind == "character" && fill.empty() && !a.value("query", "").empty()) {
                // Whoever fits a description (no one in particular): an enemy's ("a skeleton rising from a grave") is one
                // of the story's cast who fights in the scene that uses it, else one made for it where the story has it;
                // a person's ("villagers who saw the attacks") is a character who lives where it is (the alias's place,
                // else the quest giver's), not already one of the quest's.
                std::string query = a.value("query", "");
                std::transform(query.begin(), query.end(), query.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                static const char* const kFoes[] = {"skeleton", "ghost", "zombie", "spirit", "undead", "wraith", "ghoul", "bandit", "wolf",
                                                    "monster", "enemy", "enemies", "beast", "spider", "rat", "goblin", "orc", "cultist", "raider"};
                bool foe = false;
                for (const char* w : kFoes) foe = foe || query.find(w) != std::string::npos;
                const std::string cls = ClassFor(records, query, CastKind::Enemy);
                if (foe || !cls.empty()) {
                    // One of the scene's cast who fights, whose name or class the description names.
                    std::set<std::string> want;
                    for (const auto& w : NameWords(query)) {
                        if (w.size() > 3) want.insert(w);
                    }
                    std::string pick;
                    for (const auto& sid : storyScenesOf(q, a.value("id", ""))) {
                        for (const auto& p : sceneCard(sid).value("participants", json::array())) {
                            const std::string who = str(p, "who");
                            if (!fighters.contains(who) || !pick.empty()) continue;
                            const json c = store_.Read("characters/" + PlanSlug(who)).value_or(json::object());
                            const auto has = NameWords(c.value("name", "") + " " + c.value("gameplay", json::object()).value("class", "") + " " + PlanSlug(who));
                            if (std::any_of(want.begin(), want.end(), [&](const std::string& w) { return has.contains(w); })) pick = who;
                        }
                    }
                    std::string place = storyPlaceOf(q, a.value("id", ""));
                    if (place.empty()) place = in;
                    Registry* reg = pick.empty() ? sceneOf(place) : nullptr;
                    std::string useCls = cls;
                    for (const auto& id : records.Ids("character-class")) {
                        const json& k = *records.Find("character-class", id);
                        if (useCls.empty() && k.value("role", "") != "civilian" && k.value("combat", "") != "none") useCls = id;
                    }
                    if (!pick.empty()) {
                        b["how"] = "story";
                        b["entity"] = pick;
                        b["reason"] = "the one of the scene's cast who fights that it names";
                        for (const auto& act : actors) {
                            if (act.value("character", "") == pick) b["scene"] = act.value("scene", "");
                        }
                    } else if (reg && !useCls.empty()) {
                        // Made for it, where the story has it (its anchor there): the quest's own, by its key.
                        const std::string key = q.value("id", "") + "/" + a.value("id", "");
                        const std::string anchor = a.value("where", json::object()).is_object() ? a.value("where", json::object()).value("anchor", a.value("id", "")) : a.value("id", "");
                        bool there = false;
                        for (auto [e, act] : reg->view<Actor>().each()) there = there || act.key == key;
                        const auto at = MarkerAt(*reg, key).value_or(spot(*reg, anchor, offset++ % 5));
                        if (!there) {
                            const json& k = *records.Find("character-class", useCls);
                            const Entity e = CreateEnemy(*reg, at.first, at.second, entt::null);
                            reg->emplace<Perception>(e);
                            CastSpec spec;
                            spec.kind = k.value("role", "") == "boss" ? CastKind::Boss : CastKind::Enemy;
                            spec.cls = useCls;
                            spec.level = std::max(1, levelOf.contains(place) ? levelOf[place] : 1);
                            ApplyCast(*reg, e, spec, records);
                            reg->emplace_or_replace<Name>(e, k.value("name", useCls));
                            reg->emplace_or_replace<Actor>(e, key, "prefabs/enemies/" + useCls + ".prefab");
                        }
                        if (!MarkerAt(*reg, key)) AddMarker(*reg, key, at.first, at.second);
                        b["how"] = "made";
                        b["entity"] = key; // its Actor key: defeating it counts, not any enemy
                        b["marker"] = key;
                        b["scene"] = FindRow(place)->scene;
                        b["reason"] = "a " + useCls + " made where the story has it";
                    } else {
                        b["how"] = "query";
                        b["entity"] = "Enemy"; // any enemy: OnDefeated counts the team
                        b["reason"] = "any enemy" + (cls.empty() ? std::string() : " (" + cls + ")") + ": no place to make one";
                        if (Row* r = FindRow(in)) b["scene"] = r->scene;
                    }
                } else {
                    std::string place = in;
                    if (place.empty()) {
                        const std::string giver = q.value("giver", json::object()).value("alias", "");
                        for (const auto& g : q.value("aliases", json::array())) {
                            if (g.value("id", "") == giver) place = homeOf(g.value("fill", ""));
                        }
                    }
                    std::set<std::string> taken;
                    for (const auto& g : q.value("aliases", json::array())) taken.insert(g.value("fill", ""));
                    std::string pick;
                    for (const auto& act : actors) {
                        const std::string who = act.value("character", "");
                        if (act.value("player", false) || act.value("boss", false) || taken.contains(who)) continue;
                        const Row* r = FindRow(place);
                        if (r && act.value("scene", "") != r->scene) continue;
                        pick = who;
                        b["scene"] = act.value("scene", "");
                        break;
                    }
                    if (!pick.empty()) {
                        b["how"] = "query";
                        b["entity"] = pick;
                    } else {
                        b["how"] = "failed";
                        b["reason"] = "no one fits \"" + a.value("query", "") + "\" in " + (place.empty() ? std::string("the game") : place);
                    }
                }
            } else if (kind == "character") {
                if (entityOf.count(fill)) {
                    b["how"] = "fixed";
                    b["entity"] = fill; // talk targets are World Bible keys
                    for (const auto& act : actors) {
                        if (act.value("character", "") == fill) b["scene"] = act.value("scene", "");
                    }
                } else {
                    b["how"] = "failed";
                    b["reason"] = fill + " isn't placed anywhere";
                }
            } else if (kind == "location") {
                const std::string place = fill.empty() ? in : fill;
                if (Row* r = FindRow(place)) {
                    b["how"] = "fixed";
                    b["scene"] = r->scene;
                    b["marker"] = "arrive";
                } else {
                    b["how"] = "failed";
                    b["reason"] = place + " isn't built";
                }
            } else {
                // A spot, an item or a container: where the story has it (the place of the first scene that uses it; the
                // plan's place when that's an interior there, a shop or a crypt), else its place, else its keeper's home,
                // else the quest giver's; at its story anchor there, else a marker made.
                const std::string told = storyPlaceOf(q, a.value("id", ""));
                std::string place = !in.empty() ? in : !fill.empty() && kind != "item" ? homeOf(fill) : std::string();
                const Row* inRow = FindRow(place);
                if (!told.empty() && told != place && !(inRow && inRow->buildType == "interior" && !in.empty())) {
                    if (!place.empty()) Record("bind", q.value("id", "") + "/" + a.value("id", ""), told, "where the story uses it (the plan had " + place + ")");
                    place = told;
                }
                if (place.empty()) {
                    const std::string giver = q.value("giver", json::object()).value("alias", "");
                    for (const auto& g : q.value("aliases", json::array())) {
                        if (g.value("id", "") == giver && !g.value("fill", "").empty()) place = homeOf(g.value("fill", ""));
                    }
                }
                if (place.empty()) place = homeOf(fill);
                // The placements say where it is (34.11): its scene, and its spot there.
                const std::string propId = q.value("id", "") + "/" + a.value("id", "");
                const json* placedProp = PlacementOf("prop", propId);
                const json propSpot = placedProp ? placedProp->value("spot", json::object()) : json::object();
                if (const std::string there = RowPlaceOfScene(propSpot.value("scene", "")); !there.empty() && propSpot.contains("x")) {
                    if (there != place) Record("bind", propId, there, "where the placements put it");
                    place = there;
                }
                Registry* reg = sceneOf(place);
                const json where = a.value("where", json::object());
                const std::string anchor = where.is_object() ? where.value("anchor", a.value("id", "")) : a.value("id", "");
                if (!reg) {
                    b["how"] = "failed";
                    b["reason"] = "no scene for " + place;
                } else {
                    std::vector<std::string> candidates;
                    for (auto [e, m] : reg->view<Marker>().each()) candidates.push_back(m.name);
                    const std::string marker = q.value("id", "") + "/" + a.value("id", "");
                    std::pair<float, float> at;
                    if (const auto made = MarkerAt(*reg, marker)) {
                        at = *made; // Populate again: where it was made
                        b["how"] = "made";
                    } else if (propSpot.contains("x") && propSpot.value("scene", "") == FindRow(place)->scene) {
                        at = {propSpot.value("x", 0) + 0.5f, propSpot.value("y", 0) + 0.5f};
                        b["how"] = "placements";
                        b["reason"] = placedProp->value("why", "");
                    } else if (std::find(candidates.begin(), candidates.end(), anchor) != candidates.end()) {
                        at = *MarkerAt(*reg, anchor);
                        b["how"] = "story-anchor";
                    } else {
                        at = spot(*reg, anchor, offset++ % 5);
                        b["how"] = "made";
                    }
                    b["candidates"] = candidates.size();
                    if (!MarkerAt(*reg, marker)) AddMarker(*reg, marker, at.first, at.second);
                    notePlaced("props", propId, FindRow(place)->scene, at, marker);
                    b["scene"] = FindRow(place)->scene;
                    b["marker"] = marker;
                    if (kind == "item") {
                        // The item itself, lying there (its record made if the systems didn't: a quest item); the quest has it
                        // by its id (EngineQuestFromDesign: collect is "have" it).
                        const std::string itemId = PlanSlug(fill.empty() ? a.value("id", "") : fill);
                        RecordDatabase itemRecords;
                        itemRecords.Open(EngineReflection(), host_.projectRoot);
                        if (!fs::exists(host_.projectRoot / "records" / "item" / (itemId + ".json"))) {
                            std::string name = itemId;
                            std::replace(name.begin(), name.end(), '-', ' ');
                            for (size_t i = 0; i < name.size(); ++i) {
                                if (i == 0 || name[i - 1] == ' ') name[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[i])));
                            }
                            const json rec = {{"name", name}, {"kind", "quest"}, {"description", a.value("query", "")}, {"stack", 1},
                                              {"unique", true}, {"value", 0}, {"weight", 0.2}};
                            if (const auto why = itemRecords.Write("item", itemId, rec)) Record("refused", "records/item/" + itemId, *why);
                            else files_["records/item/" + itemId + ".json"] = {{"planner", doc}};
                        }
                        bool lying = false;
                        for (auto [e, p] : reg->view<ItemPickup>().each()) lying = lying || p.item == itemId;
                        if (!lying) {
                            const Entity item = reg->create();
                            reg->emplace<WorldTransform2D>(item, at.first, at.second);
                            reg->emplace<ItemPickup>(item, itemId, 1, false);
                            reg->emplace<Name>(item, itemId);
                        }
                        b["item"] = itemId;
                    }
                    // A thing the player uses (interact, free, use): what's there by that name, else one made at its marker.
                    static const std::set<std::string> used = {"interact", "use", "free"};
                    bool usedThing = false;
                    for (const auto& s : q.value("stages", json::array())) {
                        for (const auto& o : s.value("objectives", json::array())) {
                            usedThing = usedThing || (str(o, "target") == a.value("id", "") && used.contains(str(o, "action")));
                        }
                    }
                    if (usedThing && kind != "item") {
                        std::string display = a.value("id", "");
                        std::replace(display.begin(), display.end(), '-', ' ');
                        for (size_t i = 0; i < display.size(); ++i) {
                            if (i == 0 || display[i - 1] == ' ') display[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(display[i])));
                        }
                        const auto want = NameWords(display);
                        std::string thing;
                        for (auto [e, use, w] : reg->view<Interactable, WorldTransform2D>().each()) {
                            if (!thing.empty() || std::hypot(w.x - at.first, w.y - at.second) > 3.0f) continue;
                            const std::string called = !use.name.empty() ? use.name : reg->all_of<Name>(e) ? reg->get<Name>(e).value : std::string();
                            const auto has = NameWords(called);
                            if (!called.empty() && std::all_of(want.begin(), want.end(), [&](const std::string& x) { return has.contains(x); })) thing = called;
                        }
                        if (thing.empty()) {
                            const Entity e = reg->create();
                            reg->emplace<WorldTransform2D>(e, at.first, at.second);
                            reg->emplace<Name>(e, display);
                            Interactable use;
                            use.name = display;
                            use.once = true;
                            use.event = q.value("id", "") + ":" + a.value("id", "");
                            reg->emplace<Interactable>(e, use);
                            thing = display;
                        }
                        b["thing"] = thing;
                    }
                }
            }
            aliases.push_back(b);
        }
    }
    for (auto& [scene, reg] : scenes) {
        if (reg) SaveScene(*reg, project.Resolve(scene).string());
    }
    placements_["updated"] = static_cast<long long>(std::time(nullptr));
    SavePlacements(); // as placed: each one's cell and own marker (the AI Planner's Placements tab shows them)
    bindings_ = {{"schema", "aige.bindings/1"}, {"aliases", aliases}, {"actors", actors}, {"enemies", placed}};
    std::ofstream(store_.Dir() / "bindings.json", std::ios::binary) << bindings_.dump(2);
    int failed = 0;
    for (const auto& b : aliases) failed += b.value("how", "") == "failed";
    Record("bind", "bindings", std::to_string(aliases.size() - failed) + " of " + std::to_string(aliases.size()) + " aliases bound");
    KeepOneOfEach(); // the copies an earlier run or the AI panel left in other scenes
}

int GameBuild::KeepOneOfEach(const std::string& only) {
    const Project project{host_.projectRoot};
    // Where ⑪ put each character (the player: the start scene). Anywhere else, the story brings them (move_actor, their
    // packages: the roster spawns them there); a copy standing there would be taken for them wherever it's met first.
    std::map<std::string, std::string> homeScene;
    for (const auto& act : bindings_.value("actors", json::array())) {
        if (!act.value("character", "").empty() && !act.value("scene", "").empty()) homeScene[act.value("character", "")] = act.value("scene", "");
    }
    int gone = 0;
    std::set<std::string> seen;
    for (const auto& row : rows_) {
        if (row.status != "built" || row.scene.empty() || seen.contains(row.scene) || (!only.empty() && row.scene != only)) continue;
        seen.insert(row.scene);
        Registry reg;
        const std::string path = project.Resolve(row.scene).string();
        if (!LoadScene(reg, path)) continue;
        std::vector<Entity> out;
        std::map<std::string, std::vector<Entity>> byKey;
        for (auto [e, actor] : reg.view<Actor>().each()) {
            if (actor.key.empty()) continue;
            if (const auto home = homeScene.find(actor.key); home != homeScene.end() && home->second != row.scene) out.push_back(e);
            else byKey[actor.key].push_back(e);
        }
        // One of each here: the one with a script (⑫ gave it one), else the first.
        std::set<Entity> keep;
        for (auto& [key, copies] : byKey) {
            Entity best = entt::null;
            for (Entity x : copies) {
                if (best == entt::null || (!reg.all_of<LuaScript>(best) && reg.all_of<LuaScript>(x))) best = x;
            }
            keep.insert(best);
            for (Entity x : copies) {
                if (x != best) out.push_back(x);
            }
        }
        // One player: the one kept above, else the first.
        Entity player = entt::null;
        for (auto [e, team] : reg.view<Faction>().each()) {
            if (team.team != Team::Player || std::find(out.begin(), out.end(), e) != out.end()) continue;
            if (player == entt::null || (keep.contains(e) && !keep.contains(player))) player = e;
        }
        for (auto [e, team] : reg.view<Faction>().each()) {
            if (team.team == Team::Player && e != player && std::find(out.begin(), out.end(), e) == out.end()) out.push_back(e);
        }
        if (out.empty()) continue;
        for (Entity e : out) {
            if (reg.valid(e)) reg.destroy(e);
        }
        SaveScene(reg, path);
        gone += static_cast<int>(out.size());
        Record("populate", row.scene, std::to_string(out.size()) + " copies taken out",
               "one of each character, and each only in the scene ⑪ put it in (the story brings it elsewhere)");
    }
    return gone;
}

// --- ⑪b The characters' art (development_plan.md 32.6) ---------------------------------------------------------

// Who is drawn (the Brief's plan.art.characters: none, main — the player, major characters and the enemy classes —
// or all) and what each is animated doing, from its part: everyone walks; a fighter swings its weapon (held, so
// the hand's points are kept for the weapon drawn over it), is hurt and dies; an archer shoots; a caster casts.
// An item's type for its picture (one drawn per type, development_plan.md 32.6): weapons by their class, armour by
// where it's worn, potions and food by what they restore, scrolls and books, else its kind. {type, what's drawn}.
std::pair<std::string, std::string> ItemArtType(const json& item) {
    const std::string kind = item.value("kind", "misc"), slot = item.value("slot", "none");
    if (kind == "weapon" || slot == "weapon") {
        std::string cls = item.value("class", std::string("sword"));
        if (cls.empty()) cls = "sword";
        return {"weapon-" + cls, "A " + cls};
    }
    if (kind == "armor" || (slot != "none" && slot != "weapon")) {
        static const std::map<std::string, std::string> worn = {{"head", "A helmet"}, {"body", "Body armour"}, {"hands", "A pair of gloves"},
                                                               {"feet", "A pair of boots"}, {"offhand", "A shield"}, {"ring", "A ring"},
                                                               {"amulet", "An amulet"}};
        return {"armor-" + slot, worn.contains(slot) ? worn.at(slot) : "A piece of armour"};
    }
    const json use = item.value("use", json::object());
    if (!use.value("learn", "").empty() || kind == "book") return {"book", "A book"};
    if (!use.value("ability", "").empty()) return {"scroll", "A rolled scroll"};
    if (kind == "consumable") {
        if (use.contains("mana")) return {"potion-mana", "A blue mana potion in a glass bottle"};
        if (use.contains("stamina")) return {"potion-stamina", "A green stamina potion in a glass bottle"};
        if (use.contains("heal")) return {"potion-health", "A red healing potion in a glass bottle"};
        return {"consumable", "A potion in a glass bottle"};
    }
    static const std::map<std::string, std::string> kinds = {{"key", "A key"}, {"material", "A bundle of crafting materials"},
                                                            {"currency", "A pile of coins"}, {"quest", "A precious relic"},
                                                            {"misc", "A small item"}};
    return {kind, kinds.contains(kind) ? kinds.at(kind) : "A " + kind};
}

// Whether a character fights: the player; a boss or an enemy by the roles it fills, or by its gameplay's.
static bool Fights(const StoryGraph& g, const StoryNode& n, const json& gameplay) {
    if (n.cls == "PlayerCharacter") return true;
    for (const auto* e : g.Out(n.id, "fills")) {
        const StoryNode* role = g.Find(e->o);
        const std::string r = role ? role->Text("role") : "";
        if (r == "boss" || r == "enemy" || r == "elite") return true;
    }
    for (const auto& r : gameplay.value("roles", json::array())) {
        if (r == "boss" || r == "enemy") return true;
    }
    return false;
}

// What a class's character is animated doing: walking; a fighter its attack (its weapon's), getting hurt, falling dead.
static json ActionsFor(const PlanRecords& records, const json& klass, bool fights) {
    auto weaponOf = [&]() -> std::string {
        for (const auto& e : klass.value("equipment", json::array())) {
            if (e.value("slot", "") != "weapon") continue;
            if (const json* item = records.Find("item", e.value("item", ""))) return item->value("class", std::string("sword"));
        }
        return "sword";
    };
    json actions = json::array({{{"action", "walking"}}});
    const std::string combat = klass.value("combat", std::string(fights ? "melee" : "none"));
    if (combat == "melee" || (combat == "none" && fights)) {
        const std::string weapon = weaponOf();
        actions.push_back({{"action", "swinging a " + weapon + " to attack"}, {"hold", true}, {"weaponClass", weapon}});
    } else if (combat == "ranged") {
        actions.push_back({{"action", "shooting a bow"}, {"hold", true}, {"weaponClass", "bow"}});
    } else if (combat == "caster") {
        actions.push_back({{"action", "casting a spell"}});
    }
    if (combat != "none" || fights) {
        actions.push_back({{"action", "getting hurt"}});
        actions.push_back({{"action", "falling down dead"}});
    }
    return actions;
}

json GameBuild::PlanActions(const std::string& who) const {
    const PlanRecords records(store_.Read("systems").value_or(json::object()));
    const StoryGraph g = StoryGraph::Load(StoryGraph::PathIn(host_.projectRoot)).value_or(StoryGraph{});
    const StoryNode* n = g.Find(who);
    const json c = store_.Read("characters/" + PlanSlug(who)).value_or(json::object());
    const json gameplay = c.value("gameplay", json::object());
    const bool player = n && n->cls == "PlayerCharacter";
    const json* klass = records.Find("character-class", ClassFor(records, gameplay.value("class", ""), player ? CastKind::Player : CastKind::Civilian));
    return ActionsFor(records, klass ? *klass : json::object(), n ? Fights(g, *n, gameplay) : false);
}

void GameBuild::ApplyDrawnArt() {
    const Project project{host_.projectRoot};
    PrefabLibrary prefabs(project);
    for (const auto& a : art_) {
        const std::string target = a.value("target", ""), sheet = a.value("sheet", "");
        if (a.value("status", "") != "done" || sheet.empty() || target.empty() || !fs::exists(project.Resolve(sheet))) continue;
        std::string why;
        if (target == "player") {
            Registry reg;
            const std::string scene = StartScene();
            if (scene.empty() || !LoadScene(reg, project.Resolve(scene).string())) continue;
            for (auto [e, team] : reg.view<Faction>().each()) {
                if (team.team == Team::Player) why = ApplyCharacterSheet(reg, e, project, sheet);
            }
            if (why.empty()) SaveScene(reg, project.Resolve(scene).string());
        } else if (prefabs.Find(target)) {
            if (const auto failed = prefabs.EditTemplate(target, [&](Registry& reg, Entity e) { why = ApplyCharacterSheet(reg, e, project, sheet); })) why = *failed;
        }
        if (!why.empty()) Record("art", a.value("subject", ""), "not applied", why);
    }
}

void GameBuild::MarkMetRequirements() {
    if (!reqs_.contains("items") || !reqs_["items"].is_array()) return;
    checkScenes_.clear();
    int met = 0;
    for (auto& r : reqs_["items"]) {
        if (r.value("status", std::string("todo")) == "verified") continue;
        const json checks = StaticChecks(r);
        bool all = !checks.empty();
        for (const auto& c : checks) all = all && c.value("ok", json()) == json(true);
        if (!all) continue;
        r["checks"] = checks;
        r["status"] = "verified";
        r.erase("why");
        ++met;
    }
    checkScenes_.clear();
    if (met > 0) SaveRequirements();
}

void GameBuild::QueueArt(bool others, const std::string& part) {
    const bool characters = others || part == "characters";
    // What was drawn before (an earlier build of this project, its pictures still there): reused, not drawn again.
    const json previous = art_;
    auto drawnBefore = [&](const std::string& subject) -> json {
        for (const auto& p : previous) {
            if (p.value("subject", "") != subject) continue;
            const std::string file = !p.value("sheet", "").empty() ? p.value("sheet", "") : p.value("icon", "");
            if (!file.empty() && fs::exists(host_.projectRoot / file)) return p;
        }
        return json();
    };
    if (!others && characters) {
        // The characters queued again; the rest (the items, the ways through, the interface) is their own steps'.
        art_ = json::array();
        for (const auto& p : previous) {
            const std::string subject = p.value("subject", "");
            if (subject.rfind("character/", 0) != 0 && subject.rfind("class:", 0) != 0) art_.push_back(p);
        }
        artApproved_ = false;
    }
    const json brief = store_.Read("brief").value_or(json::object());
    const json chosen = BriefGet(brief, "plan.art.characters");
    // Every character by default (main: the player, the major ones and the enemies' kinds; none).
    const std::string which = others ? std::string("all") : chosen.is_string() ? chosen.get<std::string>() : "all";
    if (!host_.request) return;
    const PlanRecords records(store_.Read("systems").value_or(json::object()));
    const bool iso = ActiveSettings().grid.style != GridStyle::TopDown;
    auto add = [&](const std::string& subject, const std::string& name, const std::string& description, const std::string& target,
                   const json& actions) {
        if (description.empty() || which == "none") return;
        for (const auto& a : art_) {
            if (a.value("subject", "") == subject) return; // drawn (or being drawn) already
        }
        json entry = {{"subject", subject}, {"name", name}, {"description", description}, {"target", target},
                      {"directions", iso ? 8 : 4}, {"isometric", iso}, {"actions", actions}, {"sheet", ""}, {"next", 0},
                      {"status", "queued"}, {"error", ""}};
        // Drawn before: its sheet, and the clips it has first (they're there); only the clips it hasn't are drawn. Queued
        // still, so its sheet goes onto its prefab (or the player) again.
        if (const json was = drawnBefore(subject); was.is_object() && !was.value("sheet", "").empty()) {
            std::set<std::string> drawn;
            const json had = was.value("actions", json::array());
            for (int k = 0; k < was.value("next", 0) && k < static_cast<int>(had.size()); ++k) drawn.insert(had[k].value("action", ""));
            json mine = json::array(), rest = json::array();
            for (const auto& a : actions) (drawn.contains(a.value("action", "")) ? mine : rest).push_back(a);
            if (keepArt_) {
                // Kept as drawn: the clips its sheet has, whatever it would be given now.
                mine = json::array(), rest = json::array();
                for (int k = 0; k < was.value("next", 0) && k < static_cast<int>(had.size()); ++k) mine.push_back(had[k]);
            }
            entry["next"] = static_cast<int>(mine.size());
            for (const auto& a : rest) mine.push_back(a);
            entry["actions"] = mine;
            entry["sheet"] = was["sheet"];
            entry["reused"] = true;
        } else if (keepArt_) {
            entry["status"] = "failed", entry["error"] = "kept as drawn (keepArt): not drawn, its placeholder stays";
        }
        art_.push_back(entry);
    };
    const StoryGraph g = StoryGraph::Load(StoryGraph::PathIn(host_.projectRoot)).value_or(StoryGraph{});
    for (const auto* n : characters ? g.OfClass("Character", StoryOntology::Engine()) : std::vector<const StoryNode*>{}) {
        const std::string slug = PlanSlug(n->id);
        const json c = store_.Read("characters/" + slug).value_or(json::object());
        const json gameplay = c.value("gameplay", json::object());
        const bool player = n->cls == "PlayerCharacter";
        const std::string importance = c.value("importance", n->Text("importance"));
        if (!player && which != "all" && importance != "major") continue;
        // What it's animated doing: the plan's, and whatever else the requirements ask of it ("is animated ...").
        json actions = PlanActions(n->id);
        for (const auto& r : reqs_.value("items", json::array())) {
            if (r.value("kind", "") != "animation" || !r.contains("animation") || !r["animation"].is_object() ||
                r["animation"].value("who", "") != n->id) continue;
            const std::string act = r["animation"].value("action", "");
            const bool has = std::any_of(actions.begin(), actions.end(), [&](const json& x) { return x.value("action", "") == act; });
            if (!act.empty() && !has) actions.push_back({{"action", act}});
        }
        const std::string look = c.value("appearance", json::object()).value("description", "");
        add(n->id, slug, look.empty() ? "" : c.value("name", slug) + ": " + look, player ? "player" : "prefabs/characters/" + slug + ".prefab",
            actions);
    }
    for (const auto& id : records.Ids("character-class")) {
        const json& k = *records.Find("character-class", id);
        if (!characters || k.value("role", "") == "civilian" || k.value("combat", "") == "none") continue;
        add("class:" + id, "enemy-" + id, k.value("name", id) + (k.value("description", "").empty() ? "" : ": " + k.value("description", "")),
            "prefabs/enemies/" + id + ".prefab", ActionsFor(records, k, true));
    }
    // The items' pictures (plan.art.items, all by default), one per type of item, shared by every item of it: a
    // sword (every sword: the iron one, the storm blade), an axe, a bow; a helmet, body armour, boots, a shield; a
    // healing potion, a mana potion; a scroll, a book, a key... Each item's inventory icon, and for what's wielded or
    // worn the picture drawn on the character too (Equipment's attachments).
    const json itemsChosen = BriefGet(brief, "plan.art.items");
    // One already there (drawn, or queued by a step before) is kept as it is.
    auto queued = [&](const std::string& subject) {
        return std::any_of(art_.begin(), art_.end(), [&](const json& a) { return a.value("subject", "") == subject; });
    };
    if (!others && part == "objects" && !(itemsChosen.is_string() && itemsChosen.get<std::string>() == "none")) {
        std::map<std::string, json> types; // its type -> {what's drawn, its items}
        for (const auto& id : records.Ids("item")) {
            const json& item = *records.Find("item", id);
            const auto [type, drawn] = ItemArtType(item);
            if (!types.contains(type)) types[type] = {{"drawn", drawn}, {"items", json::array()}};
            types[type]["items"].push_back(id);
        }
        for (const auto& [type, t] : types) {
            if (queued("items:" + type)) continue;
            json entry = {{"subject", "items:" + type}, {"name", "item-" + type}, {"kind", "item"}, {"items", t["items"]},
                          {"description", t.value("drawn", type) + ", alone, whole, on a plain background, as a game item icon"},
                          {"actions", json::array()}, {"sheet", ""}, {"next", 0}, {"status", "queued"}, {"error", ""}};
            if (const json was = drawnBefore("items:" + type); was.is_object() && !was.value("icon", "").empty()) {
                entry["icon"] = was["icon"], entry["status"] = "done", entry["reused"] = true; // its picture, again
            } else if (keepArt_) {
                entry["status"] = "failed", entry["error"] = "kept as drawn (keepArt): not drawn";
            }
            art_.push_back(entry);
        }
    }
    // The interface's look (plan.art.ui, on by default): its windows' panel, in the game's style and theme (⑧).
    const json uiChosen = BriefGet(brief, "plan.art.ui");
    if (!others && part == "interface" && !queued("ui:panel") && !(uiChosen.is_string() && uiChosen.get<std::string>() == "none")) {
        const json ui = store_.Read("ui").value_or(json::object());
        const json style = BriefGet(brief, "artStyle.style");
        const std::string look = (style.is_string() ? style.get<std::string>() + " " : std::string()) + brief.value("genre", std::string("game"));
        json entry = {{"subject", "ui:panel"}, {"name", "ui-panel"}, {"kind", "item"}, {"items", json::array()}, {"height", 256},
                      {"description", "An empty decorative window panel for a " + look + " game's menus, " + ui.value("theme", std::string("stone")) +
                                          " look: an ornamented frame round a plain, flat, even centre, seen straight on, nothing inside it, no text"},
                      {"actions", json::array()}, {"sheet", ""}, {"next", 0}, {"status", "queued"}, {"error", ""}};
        if (const json was = drawnBefore("ui:panel"); was.is_object() && !was.value("icon", "").empty()) {
            entry["icon"] = was["icon"], entry["status"] = "done", entry["reused"] = true;
        }
        art_.push_back(entry);
    }
    // The ways through: onto the doors the paintings show, art for the others.
    if (!others && part == "objects") QueueGateways();
    int calls = 0, reused = 0;
    for (const auto& a : art_) {
        if (a.value("status", "") == "done") continue;
        calls += (a.value("sheet", "").empty() ? 1 : 0) + static_cast<int>(a.value("actions", json::array()).size()) - a.value("next", 0);
        reused += a.value("reused", false);
    }
    for (const auto& a : art_) reused += a.value("reused", false) && a.value("status", "") == "done";
    Record("art", others ? std::string("characters") : part, std::to_string(art_.size()) + " to draw, " + std::to_string(calls) + " calls",
           which + (reused ? ", " + std::to_string(reused) + " drawn before (reused)" : ""));
}

bool GameBuild::AutoApprove() const {
    if (host_.autoApprove && host_.autoApprove()) return true;
    const json v = BriefGet(store_.Read("brief").value_or(json::object()), "plan.building");
    return v.is_string() && v.get<std::string>() == "automatic";
}

std::vector<std::string> GameBuild::RebuildScenes(const std::vector<std::string>& only, const std::function<void(const std::string&)>& say) {
    std::vector<std::string> built;
    if (!host_.places) return built;
    const Project project{host_.projectRoot};
    const Ontology& places = host_.places();
    PrefabLibrary prefabs(project);
    AssetDatabase assets(project);
    assets.Refresh();
    const TerrainSet terrains = ProjectTerrainSet(prefabs, project);
    const IsoParams iso = BootstrapIso();
    CatalogOptions catalogOptions;
    catalogOptions.isoGrid = ActiveSettings().grid.style != GridStyle::TopDown;
    catalogOptions.pixelsPerCell = [&](const std::string& folder) {
        const PackScale scale = PackScaleFor(project, assets, folder, iso);
        return scale.source == "default" ? 0.0f : scale.pixelsPerCell;
    };
    const Catalog catalog = BuildCatalog(assets, &places, catalogOptions);
    for (auto& row : rows_) {
        if (row.build.empty()) continue;
        if (!only.empty() && std::find(only.begin(), only.end(), row.place) == only.end() &&
            std::find(only.begin(), only.end(), PlanSlug(row.place)) == only.end()) {
            continue;
        }
        PlaceBuild build(BuildHost(row.place));
        if (const std::string why = build.Open(row.build); !why.empty()) {
            say(row.place + ": its build can't be opened (" + why + ")");
            continue;
        }
        const std::string stage = build.Stage();
        if (stage != "built" && stage != "sourced" && stage != "building") {
            say(row.place + ": not ready to build (" + stage + "): finish it in the Build panel first");
            continue;
        }
        // The options the Build panel builds with (EditorApp::BuildIntoScene, ApplyBackground).
        PlaceSceneOptions options;
        options.place = build.Id();
        std::tie(options.originX, options.originY) = build.Origin();
        options.iso = iso;
        options.isoGrid = ActiveSettings().grid.style != GridStyle::TopDown;
        options.hills = HillsFor(build.Brief());
        if (build.Painted()) {
            options.paintedFolder = build.Folder().string();
            options.paintedIds = PaintedObjectIds(&places, build.Solved());
            if (build.FreePaint()) {
                for (const auto& o : build.Solved().value("objects", json::array())) options.paintedIds.insert(o.value("id", ""));
            }
        } else if (const json bg = build.Background(); bg.is_object() && bg.value("cellPx", 0) > 0) {
            options.groundPicture = bg.value("path", "");
            options.groundPictureCellPx = bg.value("cellPx", 0);
        }
        SetMoodGrade(options, build.Brief(), build.Story());
        // The art its Assets found and made (nothing fetched or made now: what's missing is a placeholder).
        AssetSourcing sourcing;
        sourcing.brief = build.Brief();
        sourcing.settings = ReadSourcingSettings(project.root);
        sourcing.canGenerate = false;
        sourcing.strategyRules = build.FromPlanner();
        sourcing.tried = build.TriedSources();
        sourcing.library = LoadAssetLibrary(project.root);
        {
            std::ifstream in(build.Folder() / "assets.json");
            const json saved = in ? json::parse(in, nullptr, false) : json();
            if (saved.is_object()) sourcing.overrides = saved.value("overrides", json::object());
        }
        sourcing.interiors = LoadInteriorChoices(build.Folder());
        const json plan = PlanPlaceAssets(places, build.Spec(), build.Solved(), catalog, prefabs, assets, terrains, options, &sourcing);
        // Into the scene as it is (another place in it stays; this one's old build goes, as the panel's Build does).
        Registry registry;
        const std::string path = project.Resolve(row.scene).string();
        std::error_code ec;
        fs::create_directories(fs::path(path).parent_path(), ec);
        if (fs::exists(path, ec)) LoadScene(registry, path);
        json report;
        BuildPlaceScene(registry, prefabs, build.Solved(), plan, terrains, options, &report);
        json verify = VerifyPlaceScene(registry, build.Solved(), plan, options);
        int failing = 0;
        for (const auto& c : verify) failing += !c.value("ok", true) && !c.value("warning", false);
        json repairs = json::array();
        if (failing) {
            repairs = RepairPlaceScene(registry, prefabs, build.Solved(), plan, verify, options);
            verify = VerifyPlaceScene(registry, build.Solved(), plan, options);
        }
        UpdateWorldToScreen(registry, iso);
        SaveScene(registry, path);
        // Its story markers and levels, as when the Build panel finishes it for the plan.
        try {
            FinishInEditor(row);
        } catch (const std::exception& e) {
            say(row.place + ": built, but its story markers weren't added (" + e.what() + ")");
        }
        int occluders = 0;
        if (const json painting = report.value("painting", json()); painting.is_object()) occluders = painting.value("occluders", 0);
        int stillFailing = 0;
        for (const auto& c : verify) stillFailing += !c.value("ok", true) && !c.value("warning", false);
        say(row.place + " → " + row.scene + ": " + std::to_string(report.value("painted", 0)) + " painted parts, " + std::to_string(occluders) +
            " occluders, " + std::to_string(repairs.size()) + " repairs" + (stillFailing ? ", " + std::to_string(stillFailing) + " checks still failing" : ""));
        built.push_back(row.place);
    }
    Save();
    return built;
}

std::string GameBuild::ApprovePaint(const std::string& place) {
    Row* r = FindRow(place);
    if (!r) return "no " + place + " in the build plan";
    r->paintApproved = true;
    r->awaitingApproval = false;
    Record("approve", place, "painting approved");
    Save();
    return {};
}

int GameBuild::QueueGateways() {
    if (!host_.request) return 0;
    const Project project{host_.projectRoot};
    json settled = LoadGateways(store_.Dir());
    auto queued = [&](const std::string& subject) {
        return std::any_of(art_.begin(), art_.end(), [&](const json& a) {
            return a.value("subject", "") == subject && a.value("status", "") != "failed";
        });
    };
    // An interior's way out is its own door (the room's exit): nothing to find in its painting, nothing to draw. What was
    // asked or settled for one is let go, its gateways back to the placeholder's blue.
    bool unsettled = false;
    for (const auto& row : rows_) {
        if (row.buildType != "interior") continue;
        for (auto& a : art_) {
            const std::string kind = a.value("kind", "");
            if ((kind == "doors" || kind == "gateway") && a.value("scene", "") == row.scene && a.value("status", "") != "done") {
                a["status"] = "done", a["doing"] = "", a["inFlight"] = false, a["error"] = "", a["note"] = "an interior: its way out is its own door";
            }
        }
        bool touched = false;
        for (auto it = settled.begin(); it != settled.end();) {
            // (Its way out found in its painting is kept: ApplyExit.)
            if (it.key().rfind(row.scene + ">", 0) == 0 && !(it.value().is_object() && it.value().value("exit", false))) {
                it = settled.erase(it), touched = true;
            } else {
                ++it;
            }
        }
        if (!touched) continue;
        unsettled = true;
        Registry reg;
        const std::string path = project.Resolve(row.scene).string();
        if (!LoadScene(reg, path)) continue;
        for (auto [g, gw] : reg.view<Gateway>(entt::exclude<PlaceNode>).each()) {
            reg.emplace_or_replace<Sprite>(g, Sprite{64.0f, 32.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.3f, 0.6f, 1.0f, 0.8f});
            reg.get<Sprite>(g).flat = true;
            const std::string toPlace = RowPlaceOfScene(gw.scene);
            if (const Row* to = toPlace.empty() ? nullptr : FindRow(toPlace)) reg.emplace_or_replace<Name>(g, "Gateway to " + to->brief.value("name", PlanSlug(to->place)));
        }
        SaveScene(reg, path);
    }
    if (unsettled) SaveGateways(store_.Dir(), settled);
    const size_t before = art_.size();
    for (const auto& row : rows_) {
        if (row.status != "built" || row.buildType == "interior") continue;
        Registry reg;
        if (!LoadScene(reg, project.Resolve(row.scene).string())) continue;
        std::string folder;
        {
            PlaceBuild build(BuildHost(row.place));
            if (!row.build.empty() && build.Open(row.build).empty() && build.Painted()) {
                folder = fs::relative(build.Folder(), project.root).generic_string();
            }
        }
        json places = json::array();
        // A dungeon's (a place one goes out to) way back out is its entrance, found by ApplyExit: not a door to find here.
        std::vector<std::string> targets;
        for (auto [e, gw] : reg.view<Gateway>(entt::exclude<PlaceNode>).each()) targets.push_back(gw.scene);
        const bool outPlace = row.buildType != "settlement" && row.buildType != "world";
        const std::string wayOut = outPlace && !folder.empty() ? WayOutOf(row.scene, targets) : std::string();
        for (auto [e, gw] : reg.view<Gateway>(entt::exclude<PlaceNode>).each()) {
            if (!wayOut.empty() && gw.scene == wayOut) continue;
            const json s = settled.value(GatewayKey(row.scene, gw.scene), json());
            if (s.is_object() && (!s.value("seen", "").empty() || !s.value("art", "").empty())) continue; // shown already
            const Row* to = nullptr;
            for (const auto& r : rows_) to = to ? to : (r.scene == gw.scene ? &r : nullptr);
            if (!to) continue;
            if (folder.empty()) {
                if (!queued("gateway:" + GatewayKey(row.scene, to->scene))) QueueGatewayArt(row, *to);
                continue;
            }
            const json d = store_.Read("places/" + PlanSlug(to->place)).value_or(json::object());
            const json about = d.value("description", json());
            std::string what = to->buildType == "interior" ? "a building one goes into" : "a place one goes out to (" + to->buildType + ")";
            if (about.is_object() && !about.value("purpose", "").empty()) what += ": " + about.value("purpose", "");
            places.push_back({{"id", to->scene}, {"name", to->brief.value("name", PlanSlug(to->place))}, {"about", what}, {"out", to->buildType != "interior"}});
        }
        if (!places.empty() && !queued("doors:" + row.scene)) {
            art_.push_back({{"subject", "doors:" + row.scene}, {"name", row.brief.value("name", PlanSlug(row.place)) + ": its ways in"}, {"kind", "doors"},
                            {"dir", folder}, {"scene", row.scene}, {"places", places}, {"actions", json::array()}, {"status", "queued"}});
        }
    }
    // An interior's or a dungeon's way back out: where its painting shows one comes in (the vision model looks once; what it
    // found is kept in gateways.json, "exit").
    for (const auto& row : rows_) {
        if (row.status != "built" || row.buildType == "settlement" || row.buildType == "world" || queued("exit:" + row.scene)) continue;
        std::string folder;
        {
            PlaceBuild build(BuildHost(row.place));
            if (!row.build.empty() && build.Open(row.build).empty() && build.Painted()) folder = fs::relative(build.Folder(), project.root).generic_string();
        }
        if (folder.empty()) continue;
        Registry reg;
        if (!LoadScene(reg, project.Resolve(row.scene).string())) continue;
        std::vector<std::string> targets;
        for (auto [e, gw] : reg.view<Gateway>(entt::exclude<PlaceNode>).each()) targets.push_back(gw.scene);
        const std::string to = WayOutOf(row.scene, targets);
        if (to.empty() || settled.value(GatewayKey(row.scene, to), json::object()).value("exit", false)) continue;
        const std::string name = row.brief.value("name", PlanSlug(row.place));
        const json d = store_.Read("places/" + PlanSlug(row.place)).value_or(json::object());
        const json about = d.value("description", json());
        std::string what = row.buildType == "interior" ? "the inside of " + name + ", a building" : name + ", a " + row.buildType;
        if (about.is_object() && about.value("purpose", json()).is_string() && !about.value("purpose", "").empty()) what += " (" + about.value("purpose", "") + ")";
        const std::string outside = RowPlaceOfScene(to).empty() ? std::string("outside") : FindRow(RowPlaceOfScene(to))->brief.value("name", PlanSlug(RowPlaceOfScene(to)));
        art_.push_back({{"subject", "exit:" + row.scene}, {"name", name + ": its way out"}, {"kind", "exit"}, {"dir", folder}, {"scene", row.scene},
                        {"to", to}, {"about", what}, {"outside", outside}, {"actions", json::array()}, {"status", "queued"}});
    }
    return static_cast<int>(art_.size() - before);
}

void GameBuild::QueueGatewayArt(const Row& from, const Row& to) {
    const std::string name = to.brief.value("name", PlanSlug(to.place));
    const json d = store_.Read("places/" + PlanSlug(to.place)).value_or(json::object());
    const json about = d.value("description", json());
    const std::string purpose = about.is_object() ? about.value("purpose", "") : "";
    const std::vector<std::string> kind = SlotsFor(name + " " + purpose);
    std::string look;
    if (!kind.empty() && kind.front() == "grave") {
        look = "a small stone crypt entrance: a heavy iron-bound door under a carved stone lintel, worn steps going down";
    } else if (to.buildType == "interior") {
        look = "a doorway: a wooden door in a short stretch of stone wall with a step and a lantern";
    } else {
        look = "a weathered stone archway with an open iron gate and a wooden signpost beside it";
    }
    art_.push_back({{"subject", "gateway:" + GatewayKey(from.scene, to.scene)}, {"name", "Way to " + name}, {"kind", "gateway"},
                    {"scene", from.scene}, {"to", to.scene}, {"height", 96}, {"actions", json::array()},
                    {"description", "The way from " + from.brief.value("name", PlanSlug(from.place)) + " to " + name + (purpose.empty() ? "" : " (" + purpose + ")") +
                                        ", plain to see as the way there: " + look + ". One thing standing alone on the ground."},
                    {"status", "queued"}});
}

std::string GameBuild::DrawMissing(const std::string& part) {
    if (stage_.empty() || stage_ == "requirements" || stage_ == "build") return "the places aren't built yet: the art is drawn once they are";
    if (!host_.request) return "the AI service isn't running: start it, then draw what's missing";
    auto inPart = [&](const json& a) {
        const std::string subject = a.value("subject", "");
        const bool character = subject.rfind("character/", 0) == 0 || subject.rfind("class:", 0) == 0;
        const bool ui = subject.rfind("ui:", 0) == 0;
        if (part == "characters") return character;
        if (part == "interface") return ui;
        if (part == "objects") return !character && !ui;
        return true;
    };
    int again = 0;
    for (auto& a : art_) {
        const std::string st = a.value("status", "");
        if (!inPart(a) || st == "done" || (st == "drawing" && a.value("inFlight", false))) continue;
        a["status"] = "queued", a["error"] = "", a["inFlight"] = false;
        a.erase("doing");
        ++again;
    }
    if (again == 0) return "nothing is missing: every one is drawn";
    artApproved_ = true; // asked for: drawn now
    if (status_ != "working") status_ = "working";
    message_ = "Drawing what's missing: " + std::to_string(again) + "…";
    Record("art", part.empty() ? std::string("art") : part, std::to_string(again) + " to draw again", "the user: draw what's missing");
    Save();
    return {};
}

std::string GameBuild::UseLibraryArt(const std::string& subject, const std::string& path) {
    if (path.empty()) return "it couldn't be copied into the project";
    json* a = nullptr;
    for (auto& x : art_) {
        if (x.value("subject", "") == subject) a = &x;
    }
    if (!a) return "nothing here is called " + subject;
    if (a->value("inFlight", false)) return "it's being drawn now: wait for it, or stop the build first";
    const bool anim = path.size() > 10 && path.compare(path.size() - 10, 10, ".anim.json") == 0;
    const std::string subjectKind = subject.rfind("character/", 0) == 0 || subject.rfind("class:", 0) == 0 ? "character" : "thing";
    if (subjectKind == "character" && !anim) return "a character takes a character from the library (a sheet with its animations)";
    if (subjectKind != "character" && anim) return "a character can't be a prop's picture";
    if (anim) {
        // Its clips as the sheet has them: nothing more to draw for it.
        std::ifstream in(host_.projectRoot / path, std::ios::binary);
        const json sheet = json::parse(in, nullptr, false);
        json actions = json::array();
        if (sheet.is_object() && sheet.contains("clips") && sheet["clips"].is_object()) {
            for (const auto& [clip, c] : sheet["clips"].items()) {
                if (clip != "idle") actions.push_back({{"action", clip}, {"clip", clip}});
            }
        }
        (*a)["sheet"] = path;
        (*a)["actions"] = actions;
        (*a)["next"] = static_cast<int>(actions.size());
    } else if (a->value("kind", "") == "item") {
        (*a)["icon"] = path;
    } else {
        (*a)["art"] = path;
    }
    (*a)["status"] = "done", (*a)["error"] = "", (*a)["fromLibrary"] = true, (*a)["inFlight"] = false;
    a->erase("doing");
    files_[anim ? path : path] = {{"planner", subject}, {"library", true}};
    ApplyDrawnArt(); // onto its prefab (or the player), where they're made already
    Record("art", subject, path, "the user: from the library");
    Save();
    return {};
}

std::string GameBuild::DrawOthers() {
    if (stage_.empty() || stage_ == "requirements" || stage_ == "build") return "the places aren't built yet: the characters are drawn once they are";
    if (!host_.request) return "the AI isn't there";
    const size_t before = art_.size();
    QueueArt(true);
    if (art_.size() == before) return "every character is drawn already";
    artApproved_ = true; // asked for: drawn now
    if (status_ != "working") status_ = "working";
    message_ = "⑪ Drawing " + std::to_string(art_.size() - before) + " more characters…";
    Record("art", "characters", std::to_string(art_.size() - before) + " more characters to draw", "the user: draw the others too");
    Save();
    return {};
}

std::string GameBuild::ApproveArt(bool draw) {
    if (stage_ != "characters" && stage_ != "objects" && stage_ != "visuals") return "nothing is waiting to be drawn";
    artApproved_ = true;
    if (!draw) {
        for (auto& a : art_) {
            if (a.value("status", "") == "queued") a["status"] = "failed", a["error"] = "skipped";
        }
    }
    Record("approve", "art", draw ? "the characters' art approved" : "the characters' art skipped");
    Save();
    return {};
}

// A painted thing's ground (its picture box [x, y, w, h] in the painting's pixels): the diamond under its box (as wide as
// the box, half as tall, its bottom corner at the box's foot) as a square of the painting's cells: {x0, y0, x1, y1}.
static std::array<int, 4> PaintedGround(const PaintView& view, const json& box) {
    const double bx = box[0].get<double>(), bw = box[2].get<double>(), foot = box[1].get<double>() + box[3].get<double>();
    const double mid = bx + bw / 2, e = view.th * 0.25;
    int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
    for (const auto& [px, py] : {std::pair{mid, foot - bw / 2 + e}, std::pair{mid, foot - e}, std::pair{bx + e, foot - bw / 4}, std::pair{bx + bw - e, foot - bw / 4}}) {
        const auto [gx, gy] = view.Cell(px, py);
        x0 = std::min(x0, static_cast<int>(std::floor(gx))), x1 = std::max(x1, static_cast<int>(std::floor(gx)));
        y0 = std::min(y0, static_cast<int>(std::floor(gy))), y1 = std::max(y1, static_cast<int>(std::floor(gy)));
    }
    return {x0, y0, x1, y1};
}

std::set<std::pair<int, int>> GameBuild::HiddenCells(const Row& row) {
    std::set<std::pair<int, int>> out;
    if (row.build.empty()) return out;
    PlaceBuild build(BuildHost(row.place));
    if (!build.Open(row.build).empty() || !build.FreePaint()) return out;
    const auto [ox, oy] = build.Origin();
    std::ifstream sin(build.Folder() / "structures.json", std::ios::binary);
    const json found = sin ? json::parse(sin, nullptr, false) : json();
    std::ifstream pin(build.Folder() / "painting.json", std::ios::binary);
    const json painting = pin ? json::parse(pin, nullptr, false) : json();
    if (!found.is_object() || !painting.is_object() || !painting.contains("view")) return out;
    const PaintView view = PaintView::From(painting["view"]);
    static const std::set<std::string> tall = {"house", "townhouse", "shop", "workshop", "inn", "temple", "stable"};
    for (const auto& s : found.value("structures", json::array())) {
        const json box = s.value("box", json());
        if (!tall.contains(SlotOfKind(s.value("kind", ""))) || !box.is_array() || box.size() != 4) continue;
        const auto [x0, y0, x1, y1] = PaintedGround(view, box);
        const double bx = box[0].get<double>(), by = box[1].get<double>(), bw = box[2].get<double>(), bh = box[3].get<double>();
        // The cells behind its ground (not in front of it on either side) whose middle its picture covers: its walls rise
        // from its ground, so across no further than its ground's left and right corners (the vision's box is looser).
        const int reach = static_cast<int>(std::ceil(bh / std::max(1.0, view.th) * 2.0)) + 2;
        const double left = std::max(bx, view.Pixel(x0, y1 + 1.0).first), right = std::min(bx + bw, view.Pixel(x1 + 1.0, y0).first);
        for (int y = y0 - reach; y <= y1; ++y) {
            for (int x = x0 - reach; x <= x1; ++x) {
                if (x >= x0 && y >= y0) continue; // its ground: blocked anyway
                const auto [px, py] = view.Pixel(x + 0.5, y + 0.5);
                if (px >= left && px <= right && py >= by && py <= by + bh) out.insert({x + ox, y + oy});
            }
        }
    }
    return out;
}

std::vector<PlannedThing> GameBuild::ThingsOf(const Row& row) {
    std::vector<PlannedThing> things;
    if (row.build.empty()) return things;
    PlaceBuild build(BuildHost(row.place));
    if (!build.Open(row.build).empty()) return things;
    const auto [ox, oy] = build.Origin();
    things = PlannedThings(build.Solved(), ox, oy);
    if (!build.FreePaint()) return things;
    // Painted freely: the painter put its buildings where it liked, so they're where the vision model found them in the
    // painting (their feet: the bottom of each one's box); the plan's buildings aren't there. Its land (fields, graves,
    // the gate's place) is where the plan has it.
    std::ifstream sin(build.Folder() / "structures.json", std::ios::binary);
    const json found = sin ? json::parse(sin, nullptr, false) : json();
    std::ifstream pin(build.Folder() / "painting.json", std::ios::binary);
    const json painting = pin ? json::parse(pin, nullptr, false) : json();
    const json structures = found.is_object() ? found.value("structures", json::array()) : json::array();
    if (structures.empty() || !painting.is_object() || !painting.contains("view")) return things;
    static const std::set<std::string> standing = {"house", "townhouse", "shop", "workshop", "inn", "temple", "stable", "centrepiece", "stall"};
    things.erase(std::remove_if(things.begin(), things.end(), [](const PlannedThing& t) { return standing.contains(t.slot); }), things.end());
    const PaintView view = PaintView::From(painting["view"]);
    int i = 0;
    for (const auto& s : structures) {
        const std::string kind = s.value("kind", ""), slot = SlotOfKind(kind);
        const json box = s.value("box", json());
        ++i;
        if (slot.empty() || !box.is_array() || box.size() != 4) continue;
        // Its ground (a square of cells under its box); its door the cell just in front of its foot (towards the viewer).
        const double bx = box[0].get<double>(), bw = box[2].get<double>(), foot = box[1].get<double>() + box[3].get<double>();
        const double mid = bx + bw / 2, e = view.th * 0.25;
        const auto [x0, y0, x1, y1] = PaintedGround(view, box);
        const auto [dx, dy] = view.Cell(mid, foot + e);
        PlannedThing t{"painted/" + kind + "#" + std::to_string(i), slot, "s", x0 + ox, y0 + oy, x1 - x0 + 1, y1 - y0 + 1};
        t.hasDoor = true;
        t.doorX = static_cast<int>(std::floor(dx)) + ox, t.doorY = static_cast<int>(std::floor(dy)) + oy;
        if (t.doorX >= t.x && t.doorX < t.x + t.w && t.doorY >= t.y && t.doorY < t.y + t.h) t.doorY = t.y + t.h; // never inside it
        t.kind = kind;
        t.area = static_cast<int>(box[2].get<double>() * box[3].get<double>() / 1000.0);
        things.push_back(t);
    }
    // The plan's land the painter built over (crop beds under a painted house) isn't there to stand at.
    std::vector<const PlannedThing*> built;
    for (const auto& t : things) {
        if (t.id.rfind("painted/", 0) == 0 && standing.contains(t.slot)) built.push_back(&t);
    }
    std::vector<PlannedThing> kept;
    for (const auto& t : things) {
        const bool under = t.id.rfind("painted/", 0) != 0 && std::any_of(built.begin(), built.end(), [&](const PlannedThing* b) {
            return t.x < b->x + b->w && b->x < t.x + t.w && t.y < b->y + b->h && b->y < t.y + t.h;
        });
        if (!under) kept.push_back(t);
    }
    return kept;
}

// What kind of land area a plan module is: square, graveyard, field; empty when none stories happen at.
static std::string KindOfArea(const std::string& module) {
    for (const char* w : {"square", "plaza", "green"}) {
        if (module.find(w) != std::string::npos) return "square";
    }
    for (const char* w : {"graveyard", "cemetery"}) {
        if (module.find(w) != std::string::npos) return "graveyard";
    }
    for (const char* w : {"field", "farm", "pasture", "meadow", "orchard", "paddock", "vineyard"}) {
        if (module.find(w) != std::string::npos) return "field";
    }
    return {};
}

std::vector<PlannedArea> GameBuild::AreasOf(const Row& row) {
    std::vector<PlannedArea> out;
    if (row.build.empty()) return out;
    PlaceBuild build(BuildHost(row.place));
    if (!build.Open(row.build).empty()) return out;
    const auto [ox, oy] = build.Origin();
    for (const auto& r : build.Solved().value("regions", json::array())) {
        if (!r.is_object()) continue;
        const std::string module = r.value("module", ""), kind = KindOfArea(module);
        const json b = r.value("bounds", json());
        if (kind.empty() || !b.is_array() || b.size() != 4) continue;
        const int x = b[0].get<int>() + ox, y = b[1].get<int>() + oy;
        out.push_back({r.value("id", module), module, kind, x, y, x + std::max(1, b[2].get<int>()) - 1, y + std::max(1, b[3].get<int>()) - 1, false});
    }
    if (!build.FreePaint()) return out;
    // Painted freely: a square is where the painter put the stalls and wells near it (within 15 cells of the plan's), the
    // ones close together (6 cells apart at most), a cell round them.
    std::vector<PlannedThing> stood;
    for (const auto& t : ThingsOf(row)) {
        if (t.id.rfind("painted/", 0) == 0 && (t.slot == "stall" || t.slot == "centrepiece")) stood.push_back(t);
    }
    auto gap = [](const PlannedThing& t, int x0, int y0, int x1, int y1) {
        const int dx = std::max({0, x0 - (t.x + t.w - 1), t.x - x1}), dy = std::max({0, y0 - (t.y + t.h - 1), t.y - y1});
        return std::max(dx, dy);
    };
    for (auto& a : out) {
        if (a.kind != "square") continue;
        const double cx = (a.x0 + a.x1) / 2.0, cy = (a.y0 + a.y1) / 2.0;
        int seed = -1;
        double best = 15.0;
        for (size_t i = 0; i < stood.size(); ++i) {
            const double d = std::hypot(stood[i].x + stood[i].w / 2.0 - cx, stood[i].y + stood[i].h / 2.0 - cy);
            if (d <= best) best = d, seed = static_cast<int>(i);
        }
        if (seed < 0) continue;
        std::vector<bool> in(stood.size(), false);
        in[seed] = true;
        int x0 = stood[seed].x, y0 = stood[seed].y, x1 = x0 + stood[seed].w - 1, y1 = y0 + stood[seed].h - 1;
        for (bool grew = true; grew;) {
            grew = false;
            for (size_t i = 0; i < stood.size(); ++i) {
                if (in[i] || gap(stood[i], x0, y0, x1, y1) > 6) continue;
                in[i] = true;
                grew = true;
                x0 = std::min(x0, stood[i].x), y0 = std::min(y0, stood[i].y);
                x1 = std::max(x1, stood[i].x + stood[i].w - 1), y1 = std::max(y1, stood[i].y + stood[i].h - 1);
            }
        }
        a.x0 = x0 - 1, a.y0 = y0 - 1, a.x1 = x1 + 1, a.y1 = y1 + 1;
        a.painted = true;
    }
    return out;
}

void GameBuild::SettleAreaAnchors() {
    const Project project{host_.projectRoot};
    for (const auto& row : rows_) {
        if ((row.status != "built" && row.status != "placeholder") || row.buildType == "interior" || row.scene.empty()) continue;
        std::vector<PlannedArea> painted;
        for (const auto& a : AreasOf(row)) {
            if (a.painted) painted.push_back(a);
        }
        if (painted.empty()) continue;
        const json desc = store_.Read("places/" + PlanSlug(row.place)).value_or(json::object());
        Registry reg;
        const std::string path = project.Resolve(row.scene).string();
        if (!LoadScene(reg, path)) continue;
        const TileGrid grid = TileGrid::Build(reg);
        bool moved = false;
        for (const auto& a : painted) {
            const int cx = (a.x0 + a.x1) / 2, cy = (a.y0 + a.y1) / 2;
            std::set<std::pair<int, int>> taken;
            int k = 0;
            for (const auto& an : desc.value("storyAnchors", json::array())) {
                if (!an.is_object() || an.value("module", "") != a.module) continue;
                // The first in its middle, the next ones two cells either side of it.
                std::optional<std::pair<int, int>> at;
                for (int tries = 0; tries < 8 && (!at || taken.contains(*at)); ++tries, ++k) {
                    const int off = (k + 1) / 2 * 2 * (k % 2 ? 1 : -1);
                    at = NearestOpen(grid, cx + off, cy);
                }
                if (!at) continue;
                taken.insert(*at);
                const std::string id = an.value("id", "");
                bool found = false;
                for (auto [e, m, w] : reg.view<Marker, WorldTransform2D>().each()) {
                    if (m.name != id) continue;
                    found = true;
                    if (static_cast<int>(std::floor(w.x)) != at->first || static_cast<int>(std::floor(w.y)) != at->second) {
                        w.x = at->first + 0.5f, w.y = at->second + 0.5f;
                        moved = true;
                    }
                }
                if (!found) {
                    AddMarker(reg, id, at->first + 0.5f, at->second + 0.5f);
                    moved = true;
                }
            }
        }
        if (moved) {
            SaveScene(reg, path);
            Record("placements", row.place, "anchors moved", "its square's story anchors put where the painting has the square");
        }
    }
}

// --- Roads, the ways out along them, and the gateways on them (34.11 Placements ▸ Paths) -----------------------------

// A built outdoor place's roads as paths characters can walk (docs/ai/ai-paths.md): each road of its plan a path named
// road-<its id> (a painted place: the roads its painting keeps, its main roads and streets), its points at its corners;
// and where a road leaves the scene, a marker "to-the-<side>" on it (x 0 is west, the last column east, y 0 north, the
// last row south, as the plan names its entrances). Made again each time (the markers go with Replace Characters).
json GameBuild::LayRoads() {
    const Project project{host_.projectRoot};
    json roads = json::array(), exits = json::array();
    for (const auto& row : rows_) {
        if ((row.status != "built" && row.status != "placeholder") || row.buildType == "interior" || row.scene.empty() || row.build.empty()) continue;
        PlaceBuild build(BuildHost(row.place));
        if (!build.Open(row.build).empty()) continue;
        const auto [ox, oy] = build.Origin();
        const json solved = build.Solved();
        const json b = solved.value("grid", json::object()).value("bounds", json::array());
        if (!b.is_array() || b.size() != 4) continue;
        const int W = b[2].get<int>(), H = b[3].get<int>();
        const bool painted = build.FreePaint();
        Registry reg;
        const std::string path = project.Resolve(row.scene).string();
        if (!LoadScene(reg, path)) continue;
        std::vector<Entity> gone;
        for (auto [e, p] : reg.view<Path>().each()) {
            if (p.name.rfind("road-", 0) == 0) gone.push_back(e);
        }
        for (auto [e, m] : reg.view<Marker>().each()) {
            if (m.name.rfind("to-the-", 0) == 0) gone.push_back(e);
        }
        for (Entity e : gone) reg.destroy(e);
        const TileGrid grid = TileGrid::Build(reg);
        std::map<std::string, int> sides;
        for (const auto& p : solved.value("paths", json::array())) {
            const std::string cls = p.value("class", ""), id = p.value("id", "");
            if (painted && cls != "main" && cls != "street" && id.find("/site/") == std::string::npos) continue;
            std::vector<std::pair<int, int>> cells;
            for (const auto& c : p.value("cells", json::array())) {
                if (c.is_array() && c.size() >= 2) cells.push_back({c[0].get<int>(), c[1].get<int>()});
            }
            if (cells.size() < 2) continue;
            // Its corners: the first cell, each where it turns, the last.
            std::vector<std::pair<int, int>> corners = {cells.front()};
            for (size_t i = 1; i + 1 < cells.size(); ++i) {
                const int ax = cells[i].first - cells[i - 1].first, ay = cells[i].second - cells[i - 1].second;
                const int bx = cells[i + 1].first - cells[i].first, by = cells[i + 1].second - cells[i].second;
                if (ax != bx || ay != by) corners.push_back(cells[i]);
            }
            corners.push_back(cells.back());
            std::string name = "road-";
            for (const char ch : (id.rfind("site/", 0) == 0 ? id.substr(5) : id)) name += (ch == '/' ? '-' : ch);
            json points = json::array(), shown = json::array();
            for (const auto& [x, y] : corners) {
                points.push_back({{"x", x + ox + 0.5f}, {"y", y + oy + 0.5f}});
                shown.push_back({x + ox, y + oy});
            }
            const Entity e = reg.create();
            reg.emplace<Name>(e, "Road " + name.substr(5));
            reg.emplace<WorldTransform2D>(e, 0.0f, 0.0f);
            Path road;
            road.name = name;
            road.points = points.dump();
            road.end = "back";
            reg.emplace<Path>(e, road);
            json out = {{"scene", row.scene}, {"name", name}, {"class", cls}, {"points", shown}, {"cells", static_cast<int>(cells.size())}};
            // Its ends on the scene's edge: a way out.
            json ways = json::array();
            for (const auto& end : {cells.front(), cells.back()}) {
                const auto [x, y] = end;
                const std::string side = x <= 0 ? "west" : x >= W - 1 ? "east" : y <= 0 ? "north" : y >= H - 1 ? "south" : "";
                if (side.empty()) continue;
                const int n = ++sides[side];
                const std::string marker = "to-the-" + side + (n > 1 ? "-" + std::to_string(n) : "");
                const auto at = NearestOpen(grid, x + ox, y + oy, 6);
                const int mx = at ? at->first : x + ox, my = at ? at->second : y + oy;
                AddMarker(reg, marker, mx + 0.5f, my + 0.5f);
                exits.push_back({{"scene", row.scene}, {"marker", marker}, {"side", side}, {"x", mx}, {"y", my}, {"road", name}});
                ways.push_back(marker);
            }
            out["exits"] = ways;
            roads.push_back(out);
        }
        SaveScene(reg, path);
    }
    placements_["roads"] = roads;
    placements_["exits"] = exits;
    return {{"roads", roads}, {"exits", exits}};
}

json GameBuild::OutdoorWays() const {
    // The gateways between two outdoor places (not a door into a building): where each is now, and where it leads.
    const Project project{host_.projectRoot};
    json out = json::array();
    const json outlined = store_.Read("outline").value_or(json::object()).value("places", json::array());
    auto lineOf = [&](const std::string& place) {
        for (const auto& p : outlined) {
            if (p.is_object() && p.value("id", json()) == json(place)) return p.value("line", std::string());
        }
        return std::string();
    };
    for (const auto& row : rows_) {
        if (row.status != "built" || row.buildType == "interior" || row.scene.empty()) continue;
        Registry reg;
        if (!LoadScene(reg, project.Resolve(row.scene).string())) continue;
        for (auto [e, gw, w] : reg.view<Gateway, WorldTransform2D>().each()) {
            const Row* to = nullptr;
            for (const auto& r : rows_) {
                if (r.scene == gw.scene) to = &r;
            }
            if (!to || to->buildType == "interior") continue;
            out.push_back({{"scene", row.scene}, {"to", to->scene}, {"toPlace", to->place}, {"toName", to->brief.value("name", PlanSlug(to->place))},
                           {"toLine", lineOf(to->place)}, {"x", static_cast<int>(std::floor(w.x))}, {"y", static_cast<int>(std::floor(w.y))}});
        }
    }
    return out;
}

json GameBuild::SettleGateways(const json& decided, const json& exits, const json& old) {
    // Each gateway the AI put on a way out: on that marker's cell, one arriving two cells in along the road, kept in
    // gateways.json (⑩ puts it there each time it connects the scenes); "was": where it stood before it was first moved
    // (the story anchors near it there go with it). The ones not decided again stay as they were.
    const Project project{host_.projectRoot};
    json settled = LoadGateways(store_.Dir());
    const json ways = OutdoorWays();
    json out = json::array();
    std::set<std::string> done;
    for (const auto& d : decided.is_array() ? decided : json::array()) {
        if (!d.is_object()) continue;
        const std::string scene = d.value("scene", ""), to = d.value("to", ""), exit = d.value("exit", "");
        json at;
        for (const auto& x : exits) {
            if (x.value("scene", "") == scene && x.value("marker", "") == exit) at = x;
        }
        if (at.is_null() || to.empty() || done.contains(scene + ">" + to)) continue;
        json was;
        for (const auto& o : old.is_array() ? old : json::array()) {
            if (o.value("scene", "") == scene && o.value("to", "") == to && o.contains("was")) was = o["was"];
        }
        for (const auto& w : ways) {
            if (was.is_null() && w.value("scene", "") == scene && w.value("to", "") == to) was = json::array({w["x"], w["y"]});
        }
        if (was.is_null()) continue; // no gateway there to move
        const int x = at.value("x", 0), y = at.value("y", 0);
        const std::string side = at.value("side", "");
        const int ix = x + (side == "west" ? 2 : side == "east" ? -2 : 0), iy = y + (side == "north" ? 2 : side == "south" ? -2 : 0);
        Registry reg;
        std::pair<int, int> arrive = {ix, iy};
        if (LoadScene(reg, project.Resolve(scene).string())) {
            if (const auto in = NearestOpen(TileGrid::Build(reg), ix, iy, 6)) arrive = *in;
        }
        json& s = settled[GatewayKey(scene, to)];
        if (!s.is_object()) s = json::object();
        s["cell"] = {x, y};
        s["arrive"] = {arrive.first, arrive.second};
        s["exit"] = true;
        s["by"] = "placements";
        done.insert(scene + ">" + to);
        out.push_back({{"scene", scene}, {"to", to}, {"exit", exit}, {"x", x}, {"y", y}, {"arrive", {arrive.first, arrive.second}}, {"was", was},
                       {"why", d.value("why", "")}});
    }
    for (const auto& o : old.is_array() ? old : json::array()) {
        if (o.is_object() && !done.contains(o.value("scene", "") + ">" + o.value("to", ""))) out.push_back(o);
    }
    SaveGateways(store_.Dir(), settled);
    return out;
}

void GameBuild::SettleGateAnchors() {
    // The story anchors the plan put by a gateway (the failing boundary by the graveyard's gate) go with it: by as much
    // as it moved, on the nearest open cell.
    const Project project{host_.projectRoot};
    std::map<std::string, std::vector<json>> moved;
    for (const auto& g : placements_.value("gateways", json::array())) {
        if (g.contains("was") && g["was"].is_array() && g["was"].size() == 2) moved[g.value("scene", "")].push_back(g);
    }
    for (const auto& [scene, gates] : moved) {
        Registry reg;
        const std::string path = project.Resolve(scene).string();
        if (!LoadScene(reg, path)) continue;
        const json desc = store_.Read("places/" + PlanSlug(RowPlaceOfScene(scene))).value_or(json::object());
        std::set<std::string> anchors;
        for (const auto& a : desc.value("storyAnchors", json::array())) {
            if (a.is_object() && !a.value("id", "").empty()) anchors.insert(a.value("id", ""));
        }
        const TileGrid grid = TileGrid::Build(reg);
        bool changed = false;
        for (auto [e, m, w] : reg.view<Marker, WorldTransform2D>().each()) {
            if (!anchors.contains(m.name)) continue;
            for (const auto& g : gates) {
                const int wx = g["was"][0].get<int>(), wy = g["was"][1].get<int>();
                const int mx = static_cast<int>(std::floor(w.x)), my = static_cast<int>(std::floor(w.y));
                if (std::max(std::abs(mx - wx), std::abs(my - wy)) > 8) continue;
                const int dx = g.value("x", wx) - wx, dy = g.value("y", wy) - wy;
                if (dx == 0 && dy == 0) continue;
                if (const auto at = NearestOpen(grid, std::max(0, mx + dx), std::max(0, my + dy), 8)) {
                    w.x = at->first + 0.5f, w.y = at->second + 0.5f;
                    changed = true;
                }
                break;
            }
        }
        if (changed) SaveScene(reg, path);
    }
}

std::string GameBuild::RowPlaceOfScene(const std::string& scene) const {
    for (const auto& r : rows_) {
        if (r.scene == scene) return r.place;
    }
    return {};
}

void GameBuild::ApplyEntrances(json& a, const json& body) {
    const Project project{host_.projectRoot};
    a["doing"] = "";
    const std::string sceneOf = a.value("scene", "");
    const Row* row = nullptr;
    for (const auto& r : rows_) row = row ? row : (r.scene == sceneOf ? &r : nullptr);
    if (row && row->buildType == "interior") { // its way out is its own door
        a["status"] = "done", a["note"] = "an interior: its way out is its own door";
        return;
    }
    Registry reg;
    const std::string path = project.Resolve(sceneOf).string();
    PlaceBuild build(BuildHost(row ? row->place : std::string()));
    if (!row || !build.Open(row->build).empty() || !LoadScene(reg, path)) {
        a["status"] = "failed", a["error"] = "its scene or build can't be read";
        return;
    }
    const auto [ox, oy] = build.Origin();
    std::ifstream in(build.Folder() / "painting.json", std::ios::binary);
    const json painting = in ? json::parse(in, nullptr, false) : json();
    if (!painting.is_object() || !painting.contains("view")) {
        a["status"] = "failed", a["error"] = "its painting has no view";
        return;
    }
    const PaintView view = PaintView::From(painting["view"]);
    const TileGrid grid = TileGrid::Build(reg);
    json settled = LoadGateways(store_.Dir());
    int found = 0;
    std::vector<std::string> unseen;
    for (const auto& e : body.value("entrances", json::array())) {
        const std::string to = e.value("id", "");
        const json door = e.value("door", json()), box = e.value("box", json());
        const json& at = door.is_array() && door.size() == 4 ? door : box;
        if (!e.value("found", false) || !at.is_array() || at.size() != 4) {
            unseen.push_back(to);
            continue;
        }
        // Its foot (the bottom of the door, or of the building): the cell there, or the nearest open one before it; one
        // arrives a cell further out (lower in the picture: towards the viewer).
        const double px = at[0].get<double>() + at[2].get<double>() / 2, py = at[1].get<double>() + at[3].get<double>();
        const auto [cx, cy] = view.Cell(px, py);
        const auto [ax, ay] = view.Cell(px, py + view.th);
        const auto cell = NearestOpen(grid, static_cast<int>(std::floor(cx)) + ox, static_cast<int>(std::floor(cy)) + oy);
        if (!cell) {
            unseen.push_back(to);
            continue;
        }
        auto arrive = NearestOpen(grid, static_cast<int>(std::floor(ax)) + ox, static_cast<int>(std::floor(ay)) + oy);
        if (arrive && *arrive == *cell) arrive = NearestOpen(grid, cell->first + 1, cell->second + 1);
        json entry = {{"cell", {cell->first, cell->second}}, {"seen", e.value("kind", std::string("door"))}};
        if (arrive) entry["arrive"] = {arrive->first, arrive->second};
        settled[GatewayKey(sceneOf, to)] = entry;
        ++found;
        const std::string marker = "from-" + PlanSlug(RowPlaceOfScene(to));
        for (auto [g, gw, w] : reg.view<Gateway, WorldTransform2D>().each()) {
            if (gw.scene != to) continue;
            w.x = cell->first + 0.5f, w.y = cell->second + 0.5f;
            reg.emplace_or_replace<Sprite>(g, Sprite{64.0f, 32.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.3f, 0.6f, 1.0f, 0.8f});
            reg.get<Sprite>(g).flat = true;
            DressGateway(reg, g, entry, project.root);
            reg.emplace_or_replace<Name>(g, "Gateway to " + e.value("name", RowPlaceOfScene(to).substr(RowPlaceOfScene(to).rfind('/') + 1)) + " (its " +
                                                e.value("kind", std::string("door")) + " in the painting)");
        }
        for (auto [m, mk, w] : reg.view<Marker, WorldTransform2D>().each()) {
            if (mk.name == marker && arrive) w.x = arrive->first + 0.5f, w.y = arrive->second + 0.5f;
        }
    }
    SaveGateways(store_.Dir(), settled);
    SaveScene(reg, path);
    // Painted freely, its buildings are known now (structures.json): where everyone stands decided again (Placements.cpp),
    // and the characters placed again at them.
    if (build.FreePaint()) RequestPlacements("the buildings of " + row->place + " were found in its painting");
    // What the painting doesn't show: art drawn for it.
    for (const auto& to : unseen) {
        const Row* other = nullptr;
        for (const auto& r : rows_) other = other ? other : (r.scene == to ? &r : nullptr);
        const std::string subject = other ? "gateway:" + GatewayKey(row->scene, other->scene) : std::string();
        if (other && std::none_of(art_.begin(), art_.end(), [&](const json& x) { return x.value("subject", "") == subject && x.value("status", "") != "failed"; })) {
            QueueGatewayArt(*row, *other);
        }
    }
    a["status"] = "done";
    a["found"] = found;
    Record("art", a.value("subject", ""), std::to_string(found) + " ways in found in the painting", std::to_string(unseen.size()) + " to draw");
}

std::string GameBuild::WayOutOf(const std::string& scene, const std::vector<std::string>& gateways) const {
    if (gateways.empty()) return {};
    // To the place it's in (the outline's parent) when a gateway leads there, else its first.
    const std::string place = RowPlaceOfScene(scene);
    std::string parent;
    for (const auto& p : store_.Read("outline").value_or(json::object()).value("places", json::array())) {
        if (p.is_object() && p.value("id", json()) == json(place) && p.contains("parent") && p["parent"].is_string()) parent = p["parent"].get<std::string>();
    }
    for (const auto& r : rows_) {
        if (!parent.empty() && r.place == parent && std::find(gateways.begin(), gateways.end(), r.scene) != gateways.end()) return r.scene;
    }
    return gateways.front();
}

void GameBuild::ApplyExit(json& a, const json& body) {
    const Project project{host_.projectRoot};
    a["doing"] = "";
    const std::string sceneOf = a.value("scene", ""), to = a.value("to", "");
    const Row* row = nullptr;
    for (const auto& r : rows_) row = row ? row : (r.scene == sceneOf ? &r : nullptr);
    Registry reg;
    const std::string path = project.Resolve(sceneOf).string();
    PlaceBuild build(BuildHost(row ? row->place : std::string()));
    if (!row || to.empty() || !build.Open(row->build).empty() || !LoadScene(reg, path)) {
        a["status"] = "failed", a["error"] = "its scene or build can't be read";
        return;
    }
    const auto [ox, oy] = build.Origin();
    std::ifstream in(build.Folder() / "painting.json", std::ios::binary);
    const json painting = in ? json::parse(in, nullptr, false) : json();
    if (!painting.is_object() || !painting.contains("view")) {
        a["status"] = "failed", a["error"] = "its painting has no view";
        return;
    }
    const PaintView view = PaintView::From(painting["view"]);
    const TileGrid grid = TileGrid::Build(reg);
    const Extent ext = OpenExtent(reg, grid);
    // Where one can walk: from where one arrives (else the middle of its open ground).
    std::optional<std::pair<int, int>> start;
    if (const auto m = MarkerAt(reg, "arrive")) start = NearestOpen(grid, static_cast<int>(std::floor(m->first)), static_cast<int>(std::floor(m->second)), 3);
    if (!start && ext.any) start = NearestOpen(grid, (ext.minX + ext.maxX) / 2, (ext.minY + ext.maxY) / 2);
    if (!start) {
        a["status"] = "failed", a["error"] = "it has no open ground";
        return;
    }
    const auto reach = ReachableFrom(grid, *start, ext.minX - 2, ext.minY - 2, ext.maxX + 2, ext.maxY + 2);
    // The plan's way out, to break a tie between doors (its front door, its entrance).
    const auto planned = PlannedWayOut(ThingsOf(*row), grid);
    // Each exit the vision model found: the walkable cell at its foot. One that can't be walked to (more than a few cells
    // from any open ground one reaches, or blocked, it says) loses to one that can.
    std::optional<std::pair<int, int>> best;
    double bestScore = 0.0;
    std::string kind;
    int seen = 0;
    for (const auto& e : body.value("exits", json::array())) {
        const json box = e.value("box", json());
        if (!box.is_array() || box.size() != 4) continue;
        ++seen;
        const double px = box[0].get<double>() + box[2].get<double>() / 2, py = box[1].get<double>() + box[3].get<double>();
        const auto [cx, cy] = view.Cell(px, py);
        const int x = static_cast<int>(std::floor(cx)) + ox, y = static_cast<int>(std::floor(cy)) + oy;
        const auto walkTo = NearestOf(reach, x, y);
        if (!walkTo) continue;
        const double off = std::hypot(walkTo->first - x, walkTo->second - y);
        if (off > 3.0) continue; // walled off: not a way one can take
        double score = off + (e.value("reachable", true) ? 0.0 : 6.0);
        if (planned) score += 0.25 * std::hypot(walkTo->first - planned->first.first, walkTo->second - planned->first.second);
        if (!best || score < bestScore) best = walkTo, bestScore = score, kind = e.value("kind", std::string("door"));
    }
    if (!best) {
        // None that can be walked to: the gateway stays where the plan (or the edge) put it.
        a["status"] = "done", a["note"] = std::to_string(seen) + " ways out seen in the painting, none that can be walked to: kept where it was";
        Record("art", a.value("subject", ""), "kept", a.value("note", ""));
        return;
    }
    // One arrives a couple of steps in from it (towards the middle of the ground one reaches), never on it.
    double mx = 0, my = 0;
    for (const auto& c : reach) mx += c.first, my += c.second;
    mx /= static_cast<double>(reach.size()), my /= static_cast<double>(reach.size());
    const int dx = mx > best->first + 0.5 ? 1 : mx < best->first - 0.5 ? -1 : 0, dy = my > best->second + 0.5 ? 1 : my < best->second - 0.5 ? -1 : 0;
    std::optional<std::pair<int, int>> arrive;
    for (int step = 2; step >= 1 && !arrive; --step) {
        const auto c = NearestOf(reach, best->first + dx * step, best->second + dy * step);
        if (c && *c != *best) arrive = c;
    }
    if (!arrive) {
        for (const auto [nx, ny] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
            if (!arrive && reach.contains({best->first + nx, best->second + ny})) arrive = std::make_pair(best->first + nx, best->second + ny);
        }
    }
    json settled = LoadGateways(store_.Dir());
    json entry = {{"cell", {best->first, best->second}}, {"seen", kind.empty() ? std::string("door") : kind}, {"exit", true}};
    if (arrive) entry["arrive"] = {arrive->first, arrive->second};
    settled[GatewayKey(sceneOf, to)] = entry;
    SaveGateways(store_.Dir(), settled);
    bool moved = false;
    for (auto [g, gw, w] : reg.view<Gateway, WorldTransform2D>(entt::exclude<PlaceNode>).each()) {
        if (gw.scene != to) continue;
        w.x = best->first + 0.5f, w.y = best->second + 0.5f;
        DressGateway(reg, g, entry, project.root);
        moved = true;
    }
    if (!moved) {
        const std::string toPlace = RowPlaceOfScene(to);
        const Entity g = CreateGateway(reg, best->first + 0.5f, best->second + 0.5f, to, "from-" + PlanSlug(row->place));
        reg.emplace_or_replace<Name>(g, "Gateway to " + (toPlace.empty() ? to : FindRow(toPlace)->brief.value("name", PlanSlug(toPlace))));
        DressGateway(reg, g, entry, project.root);
    }
    if (arrive) AddMarker(reg, "from-" + PlanSlug(RowPlaceOfScene(to)), arrive->first + 0.5f, arrive->second + 0.5f);
    SaveScene(reg, path);
    a["status"] = "done";
    a["found"] = seen;
    Record("art", a.value("subject", ""), std::to_string(best->first) + "," + std::to_string(best->second),
           "its way out (" + entry.value("seen", "") + ") as the painting shows it, of " + std::to_string(seen) + " seen; one arrives beside it");
}

bool GameBuild::DriveArt() {
    bool did = false;
    const Project project{host_.projectRoot};
    // The answers: a sheet drawn, a clip added (the sheet grows), or why not.
    bool clipsDrawn = false;
    for (auto& [index, body] : std::exchange(artAnswers_, {})) {
        if (index < 0 || index >= static_cast<int>(art_.size())) continue;
        json& a = art_[index];
        did = true;
        a["inFlight"] = false;
        if (!body.value("ok", false)) {
            const std::string why = body.contains("errors") ? body["errors"].dump() : "no answer";
            if (a.value("sheet", "").empty()) {
                a["status"] = "failed", a["error"] = why; // it keeps the placeholder
            } else {
                a["next"] = a.value("next", 0) + 1; // that clip goes; the others still come
                a["error"] = why;
            }
            continue;
        }
        if (a.value("kind", "") == "doors") {
            ApplyEntrances(a, body);
            continue;
        }
        if (a.value("kind", "") == "exit") {
            ApplyExit(a, body);
            continue;
        }
        if (a.value("kind", "") == "gateway") {
            const json assets = body.value("assets", json::array());
            const std::string art = assets.empty() ? "" : assets[0].value("path", "");
            a["status"] = art.empty() ? "failed" : "done";
            a["doing"] = "";
            if (art.empty()) {
                a["error"] = "no picture came back";
                continue;
            }
            files_[art] = {{"planner", a.value("subject", "")}};
            if (const Row* from = FindRow(RowPlaceOfScene(a.value("scene", ""))); from && from->buildType == "interior") continue; // its own door
            // Stood on its gateway, where it is; kept for when the scenes are made again.
            Registry reg;
            const std::string path = project.Resolve(a.value("scene", "")).string();
            if (!LoadScene(reg, path)) continue;
            json settled = LoadGateways(store_.Dir());
            const std::string key = GatewayKey(a.value("scene", ""), a.value("to", ""));
            json entry = settled.value(key, json::object());
            entry["art"] = art;
            entry["height"] = a.value("height", 96);
            for (auto [e, gw, w] : reg.view<Gateway, WorldTransform2D>().each()) {
                if (gw.scene != a.value("to", "")) continue;
                if (!entry.contains("cell")) entry["cell"] = {static_cast<int>(std::floor(w.x)), static_cast<int>(std::floor(w.y))};
                if (const auto m = MarkerAt(reg, "from-" + PlanSlug(RowPlaceOfScene(a.value("to", "")))); m && !entry.contains("arrive")) {
                    entry["arrive"] = {static_cast<int>(std::floor(m->first)), static_cast<int>(std::floor(m->second))};
                }
                DressGateway(reg, e, entry, project.root);
            }
            settled[key] = entry;
            SaveGateways(store_.Dir(), settled);
            SaveScene(reg, path);
            Record("art", a.value("subject", ""), art, "stood on its gateway");
            continue;
        }
        if (a.value("kind", "") == "asset") {
            // An asset of the requirements (⑫ Implement): its picture, placed by its prefab item.
            const json assets = body.value("assets", json::array());
            a["art"] = assets.empty() ? "" : assets[0].value("path", "");
            a["status"] = a.value("art", "").empty() ? "failed" : "done";
            a["doing"] = "";
            if (a.value("art", "").empty()) a["error"] = "no picture came back";
            else files_[a.value("art", "")] = {{"planner", a.value("subject", "")}};
            continue;
        }
        if (a.value("kind", "") == "item") {
            // Its picture: written onto its record with the rest (⑫).
            const json assets = body.value("assets", json::array());
            a["icon"] = assets.empty() ? "" : assets[0].value("path", "");
            a["status"] = a.value("icon", "").empty() ? "failed" : "done";
            if (!a.value("icon", "").empty()) files_[a.value("icon", "")] = {{"planner", a.value("subject", "")}};
            continue;
        }
        if (a.value("sheet", "").empty()) {
            a["sheet"] = body.value("sheet", "");
            if (a.value("sheet", "").empty()) a["status"] = "failed", a["error"] = "no sheet came back";
        } else {
            // The clip it was added as (the animator names it from the action's words).
            const int k = a.value("next", 0);
            if (a["actions"].is_array() && k >= 0 && k < static_cast<int>(a["actions"].size())) a["actions"][k]["clip"] = body.value("clip", "");
            a["next"] = k + 1;
            clipsDrawn = true;
        }
    }
    if (clipsDrawn) ReconcileClips();
    // Two in flight at most (the art model's rate), each one request at a time (its clips grow one sheet).
    int flying = 0;
    for (const auto& a : art_) flying += a.value("inFlight", false);
    for (int i = 0; i < static_cast<int>(art_.size()) && flying < 2; ++i) {
        json& a = art_[i];
        if (a.value("inFlight", false) || a.value("status", "") == "done" || a.value("status", "") == "failed") continue;
        const json actions = a.value("actions", json::array());
        const int next = a.value("next", 0);
        int id = 0;
        // Its asset's name, as the service takes it: lowercase letters, digits and - (up to 48), "way-to-haunted-graveyard".
        auto artName = [&](const char* otherwise) {
            std::string out;
            for (const unsigned char c : a.value("name", std::string())) {
                if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
                else if (!out.empty() && out.back() != '-') out += '-';
            }
            out = out.substr(0, 48);
            while (!out.empty() && out.back() == '-') out.pop_back();
            return out.empty() ? std::string(otherwise) : out;
        };
        if (a.value("kind", "") == "doors") {
            a["doing"] = "finding its buildings and their doors in the painting";
            const std::string place = a.value("name", "").substr(0, a.value("name", "").find(':'));
            message_ = "⑩ " + place + ": finding its doors and gates in the painting, for its gateways…";
            id = host_.request("/places/find-entrances", {{"dir", a.value("dir", "")}, {"painting", "painting.png"}, {"places", a.value("places", json::array())}}, 900);
        } else if (a.value("kind", "") == "exit") {
            a["doing"] = "finding where one comes in and goes out in the painting";
            message_ = "⑩ " + a.value("name", "") + ": finding it in the painting, for the gateway back out…";
            id = host_.request("/places/find-way-out", {{"dir", a.value("dir", "")}, {"painting", "painting.png"}, {"about", a.value("about", "")},
                                                         {"outside", a.value("outside", "")}}, 300);
        } else if (a.value("kind", "") == "gateway") {
            a["doing"] = "drawing it";
            message_ = "⑩ Drawing the " + a.value("name", "") + "…";
            id = host_.request("/assets/generate", {{"name", artName("gateway")}, {"description", a.value("description", "")},
                                                     {"kind", "object"}, {"height", a.value("height", 96)}}, 300);
        } else if (a.value("kind", "") == "asset") {
            a["doing"] = "drawing it";
            message_ = "⑫ Drawing " + a.value("name", "") + "…";
            id = host_.request("/assets/generate", {{"name", artName("asset")}, {"description", a.value("description", "")},
                                                     {"kind", "object"}, {"height", a.value("height", 72)}}, 300);
        } else if (a.value("kind", "") == "item") {
            a["doing"] = "drawing its picture";
            message_ = "⑪ Drawing " + a.value("name", "") + "…";
            id = host_.request("/assets/generate", {{"name", artName("item")}, {"description", a.value("description", "")},
                                                     {"kind", "object"}, {"height", a.value("height", 32)}}, 300);
        } else if (a.value("sheet", "").empty()) {
            a["doing"] = "drawing it from " + std::to_string(a.value("directions", 4)) + " sides";
            message_ = "⑪ Drawing " + a.value("name", "") + "…";
            id = host_.request("/characters/generate", {{"name", a.value("name", "")}, {"description", a.value("description", "")},
                                                         {"directions", a.value("directions", 4)}, {"size", 64},
                                                         {"isometric", a.value("isometric", false)}}, 900);
        } else if (next < static_cast<int>(actions.size())) {
            const json& act = actions[next];
            a["doing"] = "animating: " + act.value("action", "") + " (" + std::to_string(next + 1) + " of " + std::to_string(actions.size()) + ")";
            message_ = "⑪ Animating " + a.value("name", "") + ": " + act.value("action", "") + "…";
            json body = {{"sheet", a.value("sheet", "")}, {"action", act.value("action", "")}, {"description", a.value("description", "")}};
            if (act.value("hold", false)) body["hold"] = true, body["weaponClass"] = act.value("weaponClass", "");
            if (host_.animateModel) body["provider"] = host_.animateModel();
            id = host_.request("/characters/animate", body, 900);
        } else {
            // Drawn and animated: onto its prefab (its copies take it when the game loads), or the player in the start scene.
            std::string why;
            const std::string target = a.value("target", ""), sheet = a.value("sheet", "");
            if (target.empty()) {
                // A creature of the requirements: its sheet, for the prefab its item makes.
            } else if (target == "player") {
                Registry reg;
                const std::string scene = StartScene();
                if (!scene.empty() && LoadScene(reg, project.Resolve(scene).string())) {
                    for (auto [e, team] : reg.view<Faction>().each()) {
                        if (team.team == Team::Player) why = ApplyCharacterSheet(reg, e, project, sheet);
                    }
                    if (why.empty()) SaveScene(reg, project.Resolve(scene).string());
                }
            } else {
                PrefabLibrary prefabs(project);
                // Its prefab not made yet (the art comes before ⑪ places anyone): put on it once it is (ApplyDrawnArt).
                if (prefabs.Find(target)) {
                    if (const auto failed = prefabs.EditTemplate(target, [&](Registry& reg, Entity e) { why = ApplyCharacterSheet(reg, e, project, sheet); })) why = *failed;
                }
            }
            a["status"] = why.empty() ? "done" : "failed";
            a["error"] = why;
            a["doing"] = "";
            if (why.empty()) files_[sheet] = {{"planner", a.value("subject", "")}};
            Record("art", a.value("subject", ""), why.empty() ? sheet : "not applied", why);
            did = true;
            continue;
        }
        if (id == 0) {
            a["status"] = "failed", a["error"] = "the AI service isn't running";
            did = true;
            continue;
        }
        pending_[id] = "art:" + std::to_string(i);
        a["inFlight"] = true;
        a["status"] = "drawing";
        ++flying;
        did = true;
    }
    return did;
}

// --- ⑫ Writing the game ----------------------------------------------------------------------------------

void GameBuild::QueueWriting() {
    // A dialogue for every character someone can talk to.
    for (const auto& a : bindings_.value("actors", json::array())) {
        if (a.value("boss", false)) continue;
        Job job;
        job.id = "w-" + std::to_string(nextJob_++);
        job.kind = "dialogue";
        job.subject = a.value("character", "");
        jobs_.push_back(job);
    }
}

bool GameBuild::DriveJobs() {
    bool did = false;
    auto answers = std::move(jobAnswers_);
    jobAnswers_.clear();
    for (auto& [id, body] : answers) {
        for (auto& job : jobs_) {
            if (job.id != id) continue;
            did = true;
            if (!body.value("ok", false)) {
                job.status = job.attempts < 2 ? "queued" : "failed";
                job.error = body.contains("errors") ? body["errors"].dump() : "no answer";
                if (job.status == "failed") WriteDialogue(job.subject, FallbackDialogue(job.subject), "fallback");
                continue;
            }
            const auto problems = HandleJob(job, body);
            if (problems.empty()) {
                job.status = "done";
            } else if (job.attempts < 2) {
                job.problems = json(problems);
                job.status = "queued";
            } else {
                job.status = "failed";
                job.error = problems.front();
                WriteDialogue(job.subject, FallbackDialogue(job.subject), "fallback");
            }
        }
    }
    for (auto& job : jobs_) {
        if (job.status != "queued") continue;
        const StoryGraph g = StoryGraph::Load(StoryGraph::PathIn(host_.projectRoot)).value_or(StoryGraph{});
        const json c = store_.Read("characters/" + PlanSlug(job.subject)).value_or(json::object());
        json gives = json::array();
        StoryLibrary story(host_.projectRoot);
        for (const auto& doc : store_.List("quests")) {
            const json q = store_.Read(doc).value_or(json::object());
            for (const auto& a : q.value("aliases", json::array())) {
                if (a.value("id", "") == q.value("giver", json::object()).value("alias", "") && a.value("fill", "") == job.subject) {
                    gives.push_back({{"id", q.value("id", "")}, {"name", q.value("name", "")}, {"stages", q.value("stages", json::array())}});
                }
            }
        }
        json knows = json::array();
        for (const auto* e : g.Out(job.subject, "knows")) knows.push_back(e->o.empty() ? e->Text("about") : e->o);
        json scenes = json::array();
        for (const auto& s : g.SceneOrder()) {
            const auto who = g.Participants(s);
            if (std::find(who.begin(), who.end(), job.subject) == who.end()) continue;
            const json sd = store_.Read("story/scenes/" + s).value_or(json::object());
            if (sd.value("card", json::object()).value("mode", "") == "dialogue") scenes.push_back({{"id", s}, {"summary", sd.value("summary", "")}});
        }
        json body = {{"character", c}, {"key", job.subject}, {"gives", gives}, {"knows", knows}, {"scenes", scenes},
                     {"brief", store_.Read("brief").value_or(json::object()).value("logline", "")}};
        if (!job.problems.empty()) body["problems"] = job.problems;
        const int id = host_.request ? host_.request("/planner/dialogue", body, 300) : 0;
        if (id == 0) {
            // Without the AI: a greeting, and the quests it gives.
            WriteDialogue(job.subject, FallbackDialogue(job.subject), "fallback");
            job.status = "done";
            did = true;
            continue;
        }
        pending_[id] = "job:" + job.id;
        job.status = "running";
        ++job.attempts;
        did = true;
    }
    return did;
}

std::vector<std::string> GameBuild::HandleJob(Job& job, const json& answer) {
    if (job.kind != "dialogue") return {};
    json dialogue = answer.value("dialogue", json::object());
    dialogue["speaker"] = job.subject;
    std::string error;
    const std::string path = "dialogue/" + PlanSlug(job.subject) + ".json";
    auto parsed = DialogueFromJson(dialogue, path, &error);
    if (!parsed) return {error};
    // The quests it names must exist: the ones this plan writes.
    std::set<std::string> quests;
    for (const auto& doc : store_.List("quests")) quests.insert(store_.Read(doc).value_or(json::object()).value("id", ""));
    std::vector<std::string> problems;
    for (const auto& node : parsed->nodes) {
        for (const auto& choice : node.choices) {
            for (const auto& effect : choice.effects) {
                if ((effect.op == "start_quest" || effect.op == "complete_quest") && !quests.contains(effect.name)) {
                    problems.push_back("node " + node.id + ": no quest '" + effect.name + "'");
                }
            }
            if (!choice.next.empty() && !parsed->Node(choice.next)) problems.push_back("node " + node.id + ": no node '" + choice.next + "'");
        }
        if (!node.next.empty() && !parsed->Node(node.next)) problems.push_back("node " + node.id + ": no node '" + node.next + "'");
    }
    if (!parsed->Node(parsed->start)) problems.push_back("no start node '" + parsed->start + "'");
    if (!problems.empty()) return problems;
    PlanDecision d;
    d.stage = "write";
    d.actor = "ai";
    d.kind = "write-dialogue";
    d.subject = path;
    d.rationale = answer.value("rationale", "");
    WriteDialogue(job.subject, DialogueToJson(*parsed), store_.Decide(d));
    return {};
}

json GameBuild::FallbackDialogue(const std::string& character) const {
    const json c = store_.Read("characters/" + PlanSlug(character)).value_or(json::object());
    json choices = json::array();
    for (const auto& doc : store_.List("quests")) {
        const json q = store_.Read(doc).value_or(json::object());
        for (const auto& a : q.value("aliases", json::array())) {
            if (a.value("id", "") == q.value("giver", json::object()).value("alias", "") && a.value("fill", "") == character) {
                choices.push_back({{"text", "Can I help? (" + q.value("name", q.value("id", "")) + ")"},
                                   {"effects", {{{"op", "start_quest"}, {"name", q.value("id", "")}}}},
                                   {"conditions", {{{"quest", q.value("id", "")}, {"is", "inactive"}}}}});
            }
        }
    }
    choices.push_back({{"text", "Farewell."}});
    std::string line = c.value("voice", json::object()).value("sample", "");
    if (line.empty()) line = "Well met, traveller.";
    return {{"speaker", character}, {"start", "greet"}, {"nodes", {{{"id", "greet"}, {"text", line}, {"choices", choices}}}}};
}

void GameBuild::WriteDialogue(const std::string& character, const json& dialogue, const std::string& madeBy) {
    json d = dialogue;
    d["speaker"] = character;
    if (!WriteStoryFile(host_.projectRoot, "dialogue", PlanSlug(character), d)) {
        files_["dialogue/" + PlanSlug(character) + ".json"] = {{"planner", "characters/" + PlanSlug(character)}, {"decision", madeBy}};
    }
}

void GameBuild::WriteQuests() {
    for (const auto& doc : store_.List("quests")) {
        const json design = store_.Read(doc).value_or(json::object());
        const std::string id = design.value("id", PlanSlug(doc));
        const auto sceneOf = [this](const std::string& place) {
            const Row* r = FindRow(place);
            return r ? r->scene : std::string();
        };
        json quest = EngineQuestFromDesign(design, bindings_, sceneOf, StoryStaging(design));
        // Started with the game (Project ▸ Story ▸ Quests): as its file has it (the user's choice kept); a main quest
        // nothing starts (no dialogue, script or cutscene has start_quest for it) starts with the game.
        std::ifstream was(host_.projectRoot / "quests" / (id + ".json"), std::ios::binary);
        const json old = was ? json::parse(was, nullptr, false) : json();
        if (old.is_object() && old.contains("atStart")) {
            if (old.value("atStart", false)) quest["atStart"] = true;
        } else if (id == "q-main" || design.value("type", "") == "main") {
            bool started = false;
            for (const char* folder : {"dialogue", "scripts", "cutscenes"}) {
                std::error_code ec;
                for (const auto& f : fs::recursive_directory_iterator(host_.projectRoot / folder, ec)) {
                    if (started || !f.is_regular_file()) continue;
                    std::ifstream in(f.path(), std::ios::binary);
                    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                    const size_t at = text.find("start_quest");
                    started = at != std::string::npos && text.find("\"" + id + "\"", at > 200 ? at - 200 : 0) != std::string::npos;
                }
            }
            if (!started) quest["atStart"] = true;
        }
        if (!WriteStoryFile(host_.projectRoot, "quests", id, quest)) {
            files_["quests/" + id + ".json"] = {{"planner", doc}, {"decision", design.value("madeBy", "")}};
        }
    }
}

std::string GameBuild::MetAt(const std::string& who, const std::string& location) const {
    auto str = [](const json& j, const char* key) {
        const auto it = j.is_object() ? j.find(key) : j.end();
        return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    const json places = store_.Read("outline").value_or(json::object()).value("places", json::array());
    auto parentOf = [&](const std::string& place) {
        for (const auto& p : places) {
            if (str(p, "id") == place) return str(p, "parent");
        }
        return std::string();
    };
    auto inside = [&](std::string place) {
        for (int i = 0; i < 8 && !place.empty(); ++i) {
            place = parentOf(place);
            if (place == location) return true;
        }
        return false;
    };
    // The places that list them (where they stand or work), inside the scene's place.
    for (const auto& p : places) {
        const std::string id = str(p, "id");
        if (id == location || !inside(id)) continue;
        for (const auto& a : store_.Read("places/" + PlanSlug(id)).value_or(json::object()).value("actors", json::array())) {
            const std::string named = str(a, "character");
            if (!named.empty() && (named == who || who.rfind(named + "-", 0) == 0)) return id;
        }
    }
    // Their home inside it, when they sell there.
    const json c = store_.Read("characters/" + PlanSlug(who)).value_or(json::object());
    const std::string home = str(c, "home");
    if (!home.empty() && home != location && inside(home)) {
        for (const auto& r : c.value("gameplay", json::object()).value("roles", json::array())) {
            const std::string role = r.is_string() ? r.get<std::string>() : std::string();
            if (role == "vendor" || role == "merchant" || role == "healer" || role == "innkeeper" || role == "shopkeeper" || role == "smith") return home;
        }
    }
    return location;
}

json GameBuild::StoryStaging(const json& design) const {
    // Each stage's scene (advancesScene): those of its cast (but the player) who live in another scene are brought to
    // where it happens as it begins, each to the marker ⑪ made for them there (story/<scene>/<who>). Their day takes them
    // home once the player leaves (ActorRoster::Leave).
    // The plan's cast timeline says it (⑥b): its moves and behaviours, stage by stage.
    if (!Timeline().empty()) return TimelineStaging(design);
    json out = json::object();
    std::map<std::string, std::string> homeScene;
    for (const auto& act : bindings_.value("actors", json::array())) {
        if (!act.value("player", false)) homeScene[act.value("character", "")] = act.value("scene", "");
    }
    auto str = [](const json& j, const char* key) {
        const auto it = j.is_object() ? j.find(key) : j.end();
        return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    for (const auto& s : design.value("stages", json::array())) {
        const std::string id = str(s, "advancesScene");
        if (id.empty()) continue;
        const json scene = store_.Read("story/scenes/" + id).value_or(json::object());
        const json card = scene.is_object() && scene.contains("card") && scene["card"].is_object() ? scene["card"] : json::object();
        std::string there;
        for (const auto& r : rows_) {
            if (r.place == str(card, "location")) there = r.scene;
        }
        if (there.empty()) continue;
        json ops = json::array();
        for (const auto& p : card.value("participants", json::array())) {
            const std::string who = str(p, "who");
            const auto home = homeScene.find(who);
            if (home == homeScene.end() || home->second == there) continue;
            if (MetAt(who, str(card, "location")) != str(card, "location")) continue; // met in their own shop there: not moved out of it
            bool moved = false; // the design brings them itself
            for (const auto& e : s.value("onEnter", json::array())) moved = moved || (str(e, "op") == "move_actor" && str(e, "who") == who);
            if (!moved) ops.push_back({{"op", "move_actor"}, {"who", who}, {"scene", there}, {"marker", "story/" + id + "/" + PlanSlug(who)}});
        }
        if (!ops.empty()) out[std::to_string(s.value("index", 10))] = ops;
    }
    return out;
}

std::set<std::string> GameBuild::MarkersOutOfReach(const std::string& scene) const {
    std::set<std::string> out;
    Registry reg;
    if (scene.empty() || !LoadScene(reg, Project{host_.projectRoot}.Resolve(scene).string())) return out;
    const auto from = MarkerAt(reg, "arrive");
    if (!from) return out;
    const TileGrid grid = TileGrid::Build(reg);
    const Extent e = OpenExtent(reg, grid);
    const auto reach = ReachableFrom(grid, {static_cast<int>(std::floor(from->first)), static_cast<int>(std::floor(from->second))},
                                     e.minX - 2, e.minY - 2, e.maxX + 2, e.maxY + 2);
    for (auto [m, mk, w] : reg.view<Marker, WorldTransform2D>().each()) {
        if (!reach.contains({static_cast<int>(std::floor(w.x)), static_cast<int>(std::floor(w.y))})) out.insert(mk.name);
    }
    return out;
}

std::string GameBuild::PlaceAnchorsAgain(const std::string& scene, const std::set<std::string>& ids) {
    const Row* row = nullptr;
    for (const auto& r : rows_) row = row ? row : (r.scene == scene ? &r : nullptr);
    if (!row) return "no place is built into " + scene;
    const Project project{host_.projectRoot};
    Registry reg;
    const std::string path = project.Resolve(scene).string();
    if (!LoadScene(reg, path)) return "can't read " + scene;
    json solved = json::object();
    int ox = 0, oy = 0;
    {
        PlaceBuild build(BuildHost(row->place));
        if (!row->build.empty() && build.Open(row->build).empty()) {
            solved = build.Solved();
            std::tie(ox, oy) = build.Origin();
        }
    }
    const auto from = MarkerAt(reg, "arrive");
    if (!from) return scene + " has no arrive marker";
    const TileGrid grid = TileGrid::Build(reg);
    const Extent e = OpenExtent(reg, grid);
    const auto reach = ReachableFrom(grid, {static_cast<int>(std::floor(from->first)), static_cast<int>(std::floor(from->second))},
                                     e.minX - 2, e.minY - 2, e.maxX + 2, e.maxY + 2);
    const json desc = store_.Read("places/" + PlanSlug(row->place)).value_or(json::object());
    std::string why;
    for (const auto& id : ids) {
        json anchor = {{"id", id}};
        for (const auto& a : desc.value("storyAnchors", json::array())) {
            if (a.value("id", "") == id) anchor = a;
        }
        // Where it was: its marker (or the quest's copy of it).
        std::optional<std::pair<float, float>> was;
        for (auto [m, mk, w] : reg.view<Marker, WorldTransform2D>().each()) {
            if (mk.name == id || (mk.name.size() > id.size() && mk.name.ends_with("/" + id))) was = std::make_pair(w.x, w.y);
        }
        // In its room, else where it was: on a cell one can walk to from where one arrives.
        std::optional<std::pair<int, int>> want = RoomOfAnchor(anchor, solved, ox, oy);
        if (!want && was) want = std::make_pair(static_cast<int>(std::floor(was->first)), static_cast<int>(std::floor(was->second)));
        if (!want) {
            why += (why.empty() ? "" : "; ") + id + ": no room and no marker for it";
            continue;
        }
        const auto at = NearestOf(reach, want->first, want->second);
        if (!at) continue;
        bool moved = false;
        for (auto [m, mk, w] : reg.view<Marker, WorldTransform2D>().each()) {
            if (mk.name == id || (mk.name.size() > id.size() && mk.name.ends_with("/" + id))) w.x = at->first + 0.5f, w.y = at->second + 0.5f, moved = true;
        }
        if (!moved) AddMarker(reg, id, at->first + 0.5f, at->second + 0.5f);
        Record("markers", scene + " " + id, std::to_string(at->first) + "," + std::to_string(at->second), "where one can walk to it");
    }
    SaveScene(reg, path);
    return why;
}

void GameBuild::MergeQuests(const std::map<std::string, QuestMerge>& docs) {
    const Project project{host_.projectRoot};
    for (const auto& [doc, merge] : docs) {
        const json design = store_.Read(doc).value_or(json::object());
        const std::string id = design.value("id", PlanSlug(doc));
        const auto sceneOf = [this](const std::string& place) {
            const Row* r = FindRow(place);
            return r ? r->scene : std::string();
        };
        const json staging = StoryStaging(design);
        const json made = EngineQuestFromDesign(design, bindings_, sceneOf, staging);
        std::ifstream in(project.Resolve("quests/" + id + ".json"), std::ios::binary);
        json quest = in ? json::parse(in, nullptr, false) : json();
        in.close();
        if (!quest.is_object()) {
            quest = made; // none yet: as the design has it
        } else {
            // Into the quest as it is (⑫'s steps may have rewritten it): the roles the fix is about (their fills, and
            // their objectives where the stage lacks them), and the effects of the stages it changed.
            std::map<std::string, std::string> fillOf;
            for (const auto& a : made.value("aliases", json::array())) fillOf[a.value("id", "")] = a.value("fill", "");
            const bool hasAliases = quest.value("aliases", json::array()).is_array() && !quest.value("aliases", json::array()).empty();
            if (hasAliases) {
                for (auto& old : quest["aliases"]) {
                    if (merge.aliases.contains(old.value("id", "")) && fillOf.count(old.value("id", ""))) old["fill"] = fillOf[old.value("id", "")];
                }
                for (const auto& a : made.value("aliases", json::array())) {
                    const bool there = std::any_of(quest["aliases"].begin(), quest["aliases"].end(), [&](const json& o) { return o.value("id", "") == a.value("id", ""); });
                    if (!there && merge.aliases.contains(a.value("id", ""))) quest["aliases"].push_back(a);
                }
            }
            json& stages = quest["stages"];
            if (!stages.is_array()) stages = json::array();
            const json madeStages = made.value("stages", json::array());
            for (size_t k = 0; k < madeStages.size(); ++k) {
                const json& s = madeStages[k];
                for (auto& old : stages) {
                    if (old.value("index", -1) != s.value("index", -2)) continue;
                    // Its objectives for the roles the fix binds: a quest without aliases names its targets outright.
                    json& objectives = old["objectives"];
                    if (!objectives.is_array()) objectives = json::array();
                    for (json o : s.value("objectives", json::array())) {
                        const std::string target = o.value("target", "");
                        if (target.size() < 2 || target[0] != '@' || !merge.aliases.contains(target.substr(1))) continue;
                        std::string resolved = hasAliases ? target : fillOf[target.substr(1)];
                        // A place, outright, is its arrival marker, which every scene has: the quest gets a marker of its own
                        // there ("q-main/bellwether"), in the place's scene, where one arrives.
                        if (!hasAliases && resolved == "arrive") {
                            resolved.clear();
                            for (const auto& a : design.value("aliases", json::array())) {
                                if (a.value("id", "") != target.substr(1)) continue;
                                const Row* r = FindRow(a.value("fill", a.value("in", "")));
                                Registry reg;
                                const std::string scenePath = r ? project.Resolve(r->scene).string() : std::string();
                                if (!r || !LoadScene(reg, scenePath)) continue;
                                const std::string name = id + "/" + target.substr(1);
                                if (!MarkerAt(reg, name)) {
                                    const auto at = MarkerAt(reg, "arrive");
                                    if (!at) continue;
                                    AddMarker(reg, name, at->first, at->second);
                                    SaveScene(reg, scenePath);
                                }
                                resolved = name;
                            }
                        }
                        // What the journal says: its verb and whom or what ("Defeat Grey Lament", "Talk to Elder Sella").
                        std::string who = target.substr(1);
                        for (const auto& a : design.value("aliases", json::array())) {
                            if (a.value("id", "") != who) continue;
                            if (a.value("fill", "").rfind("character/", 0) == 0) {
                                who = store_.Read("characters/" + PlanSlug(a.value("fill", ""))).value_or(json::object()).value("name", who);
                            } else if (!a.value("query", "").empty()) {
                                who = a.value("query", "");
                                if (!who.empty()) who[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(who[0])));
                                who = "a " + who;
                            } else {
                                std::replace(who.begin(), who.end(), '-', ' ');
                            }
                        }
                        // Already in the stage: its target, or an objective whose target or text names the same one ("Deliver the
                        // burial ledger to Lysa" for deliver burial-ledger; "Defeat Odran Vale" for the prefab or the key).
                        static const std::set<std::string> generic = {"main", "side", "character", "characters", "prefab", "prefabs", "enemy", "arrive", "quest"};
                        std::set<std::string> names;
                        for (const auto& w : NameWords(target.substr(1) + " " + resolved + " " + who)) {
                            if (!generic.contains(w)) names.insert(w);
                        }
                        // A character: every word of their name ("Bera Ash" isn't "Ash Carter").
                        const bool person = resolved.rfind("character/", 0) == 0;
                        const auto personWords = NameWords(who);
                        const bool there = std::any_of(objectives.begin(), objectives.end(), [&](const json& x) {
                            if (x.value("target", "") == target || (!resolved.empty() && x.value("target", "") == resolved)) return true;
                            const auto words = NameWords(x.value("target", "") + " " + x.value("text", ""));
                            if (person) {
                                return !personWords.empty() &&
                                       std::all_of(personWords.begin(), personWords.end(), [&](const std::string& w) { return words.contains(w); });
                            }
                            int shared = 0;
                            for (const auto& w : words) shared += names.contains(w);
                            return shared >= 2 || (shared >= 1 && x.value("type", "") == o.value("type", ""));
                        });
                        if (there || resolved.empty()) continue;
                        static const std::map<std::string, std::string> verbs = {{"defeat", "Defeat"}, {"talk", "Talk to"}, {"reach", "Go to"},
                                                                                 {"use", "Use"}, {"deliver", "Deliver"}, {"have", "Find"}, {"escort", "Escort"}};
                        const auto verb = verbs.find(o.value("type", ""));
                        o["text"] = (verb != verbs.end() ? verb->second : std::string("Find")) + " " + who;
                        o["target"] = resolved;
                        // An id of its own in the stage.
                        std::string oid = o.value("id", "o");
                        for (int n = 2; std::any_of(objectives.begin(), objectives.end(), [&](const json& x) { return x.value("id", "") == oid; }); ++n) {
                            oid = o.value("id", "o") + "-" + std::to_string(n);
                        }
                        o["id"] = oid;
                        objectives.push_back(o);
                    }
                    // The effects of a stage the fix changed, those it hasn't got; the story's staging, always.
                    {
                        json& ops = old["onEnter"];
                        if (!ops.is_array()) ops = json::array();
                        const json mine = merge.stages.contains(static_cast<int>(k)) ? s.value("onEnter", json::array())
                                                                                     : staging.value(std::to_string(s.value("index", 10)), json::array());
                        for (const auto& op : mine) {
                            if (std::find(ops.begin(), ops.end(), op) == ops.end()) ops.push_back(op);
                        }
                        if (ops.empty()) old.erase("onEnter");
                    }
                }
            }
        }
        if (!WriteStoryFile(host_.projectRoot, "quests", id, quest)) {
            files_["quests/" + id + ".json"] = {{"planner", doc}, {"decision", "validate"}};
        }
    }
}

void GameBuild::WriteFiles() {
    const Project project{host_.projectRoot};
    WriteQuests();
    // Cutscenes from the cutscene scenes: their beats as subtitles between fades.
    for (const auto& doc : store_.List("story/scenes")) {
        const json s = store_.Read(doc).value_or(json::object());
        if (s.value("card", json::object()).value("mode", "") != "cutscene" || s.value("status", "") != "written") continue;
        json lines = json::array();
        double t = 0.5;
        for (const auto& beat : s.value("beats", json::array())) {
            if (beat.value("text", "").empty()) continue;
            lines.push_back({{"at", t}, {"text", beat.value("text", "")}, {"seconds", 3.0}});
            t += 3.5;
        }
        if (lines.empty()) continue;
        // One ⑫ Implement wrote from the scene (its cast at their marks, its beats played) is kept.
        {
            std::ifstream had(host_.projectRoot / "cutscenes" / (PlanSlug(doc) + ".json"), std::ios::binary);
            const json was = had ? json::parse(had, nullptr, false) : json();
            if (was.is_object() && was.value("madeBy", "") == "implement") continue;
        }
        const json cutscene = {{"name", s.value("summary", PlanSlug(doc))},
                               {"tracks", {{{"type", "screen"}, {"keys", {{{"at", 0.0}, {"op", "fade_in"}, {"seconds", 0.5}},
                                                                         {{"at", t}, {"op", "fade_out"}, {"seconds", 0.5}}}}},
                                           {{"type", "dialogue"}, {"keys", lines}}}}};
        std::string error;
        if (auto parsed = CutsceneFromJson(cutscene, PlanSlug(doc), &error); parsed && ValidateCutscene(*parsed).empty()) {
            if (!WriteCutsceneFile(host_.projectRoot, PlanSlug(doc), cutscene)) {
                files_["cutscenes/" + PlanSlug(doc) + ".json"] = {{"planner", doc}, {"decision", s.value("madeBy", "")}};
            }
        }
    }
    // Records, and the characters' packages.
    RecordDatabase records;
    records.Open(EngineReflection(), host_.projectRoot);
    const json systems = store_.Read("systems").value_or(json::object());
    // The items' pictures (⑪b): the inventory's icon; a weapon's or armour's also drawn on its wearer.
    std::map<std::string, std::string> iconOf;
    for (const auto& a : art_) {
        if (a.value("kind", "") != "item" || a.value("icon", "").empty()) continue;
        for (const auto& id : a.value("items", json::array())) iconOf[id.get<std::string>()] = a.value("icon", ""); // every item of its type
    }
    for (json r : systems.value("records", json::array())) {
        if (r.value("type", "") == "item" && iconOf.contains(r.value("id", "")) && r["record"].is_object()) {
            json& item = r["record"];
            const std::string icon = iconOf[r.value("id", "")];
            if (!item.contains("icon")) item["icon"] = icon;
            const std::string slot = item.value("slot", "none");
            if (slot != "none" && !item.contains("sprite")) {
                item["sprite"] = icon;
                if (slot == "weapon" || slot == "offhand") {
                    item["grip"] = json::array({0.5, 0.85}); // held at the foot of its picture (a hilt, a haft)
                    item["pointing"] = -90.0;               // drawn upright
                }
            }
        }
        if (const auto why = records.Write(r.value("type", ""), r.value("id", ""), r.value("record", json::object()))) {
            Record("refused", "records/" + r.value("type", "") + "/" + r.value("id", ""), *why);
        } else {
            files_["records/" + r.value("type", "") + "/" + r.value("id", "") + ".json"] = {{"planner", "systems"}, {"decision", systems.value("madeBy", "")}};
        }
    }
    // The characters' packages, as ⑪ Populate made them (where each is done, as a marker or the character's
    // home, work or bed; when). A record the engine refuses is reported, not dropped.
    for (const auto& [id, package] : packages_.items()) {
        if (const auto why = records.Write("package", id, package)) Record("refused", "records/package/" + id, *why);
        else files_["records/package/" + id + ".json"] = {{"planner", "characters"}};
    }
    // The features' scripts (32.10): what ⑦ wrote for what the engine can't do without code, each written and put on
    // what it's for: a character's or an enemy class's prefab (every copy has it), a thing in a place, or the game
    // (an entity of its own in every scene).
    {
        const Project project{host_.projectRoot};
        PrefabLibrary prefabs(project);
        for (const auto& doc : store_.List("systemdesigns")) {
            for (const auto& sc : store_.Read(doc).value_or(json::object()).value("scripts", json::array())) {
                const std::string path = sc.value("path", ""), source = sc.value("source", "");
                if (path.rfind("scripts/", 0) != 0 || path.find("..") != std::string::npos || source.empty()) continue;
                std::error_code ec;
                fs::create_directories((host_.projectRoot / path).parent_path(), ec);
                std::ofstream(host_.projectRoot / path, std::ios::binary) << source;
                files_[path] = {{"planner", doc}};
                const json on = sc.value("on", json::object());
                const std::string kind = on.value("kind", ""), target = on.value("target", "");
                std::string where;
                auto onPrefab = [&](const std::string& prefab) {
                    const auto why = prefabs.EditTemplate(prefab, [&](Registry& reg, Entity e) { reg.emplace_or_replace<LuaScript>(e).path = path; });
                    where = why ? "not on " + prefab + ": " + *why : prefab;
                };
                if (kind == "character") onPrefab("prefabs/characters/" + PlanSlug(target) + ".prefab");
                else if (kind == "enemy") onPrefab("prefabs/enemies/" + PlanSlug(target) + ".prefab");
                else {
                    for (const auto& r : rows_) {
                        if (kind == "thing" && r.place != on.value("place", "") && PlanSlug(r.place) != PlanSlug(on.value("place", ""))) continue;
                        Registry reg;
                        if (!LoadScene(reg, project.Resolve(r.scene).string())) continue;
                        bool put = false;
                        if (kind == "game") {
                            const Entity e = reg.create();
                            reg.emplace<Name>(e, "game: " + fs::path(path).stem().string());
                            reg.emplace<WorldTransform2D>(e, 0.0f, 0.0f);
                            reg.emplace<LuaScript>(e).path = path;
                            put = true;
                        } else {
                            // The thing by its name, else the first whose plan statement names it.
                            for (auto [e, name] : reg.view<Name>().each()) {
                                if (!put && PlanSlug(name.value) == PlanSlug(target)) reg.emplace_or_replace<LuaScript>(e).path = path, put = true;
                            }
                            for (auto [e, node] : reg.view<PlaceNode>().each()) {
                                if (!put && node.statement.find(PlanSlug(target)) != std::string::npos) reg.emplace_or_replace<LuaScript>(e).path = path, put = true;
                            }
                        }
                        if (put) {
                            SaveScene(reg, project.Resolve(r.scene).string());
                            where += (where.empty() ? "" : ", ") + r.scene;
                        }
                    }
                    if (where.empty()) where = "nothing named " + target + " was found";
                }
                Record("script", path, where, sc.value("purpose", ""));
            }
        }
    }
    // The keys the features' designs ask for (32.7): added to the game's input map, clashes moved to free keys.
    {
        json planned = json::array();
        for (const auto& doc : store_.List("systemdesigns")) {
            for (const auto& a : store_.Read(doc).value_or(json::object()).value("actions", json::array())) planned.push_back(a);
        }
        const json designed = store_.Read("input").value_or(json::object());
        if (!planned.empty() || designed.value("actions", json()).is_array()) {
            InputMap map = LoadInputMap(host_.projectRoot);
            for (const auto& line : ApplyPlannedActions(map, planned)) Record("key", "input.json", line);
            // ⑦ Systems ▸ Input: the game's controls as designed, exactly (their keys, contexts, the screens they open).
            for (const auto& line : ApplyDesignedInput(map, designed)) Record("key", "input.json", line);
            if (SaveInputMap(host_.projectRoot, map)) files_["input.json"] = {{"planner", designed.empty() ? "systemdesigns" : "input"}};
        }
    }
    // The interface's theme, and the game's own file.
    const json ui = store_.Read("ui").value_or(json::object());
    const json brief = store_.Read("brief").value_or(json::object());
    const json game = {{"schema", "aige.game/1"},
                       {"title", brief.value("title", "")},
                       {"logline", brief.value("logline", "")},
                       {"genre", brief.value("genre", "")},
                       {"startScene", StartScene()},
                       {"theme", ui.value("theme", "")},
                       {"screens", ui.value("screens", json::array())},
                       {"hud", ui.value("hud", json::array())},
                       {"provenance", {{"planner", "brief"}, {"decision", brief.value("madeBy", "")}}}};
    std::ofstream(host_.projectRoot / "game.json", std::ios::binary) << game.dump(2) << "\n";
    // The interface's theme, for the game's documents (ui/theme.json).
    if (!ui.value("theme", "").empty()) {
        std::error_code themeEc;
        fs::create_directories(host_.projectRoot / "ui", themeEc);
        // Its own look on top (32.7): the panel the art stage drew, the font ⑧ chose from the project's.
        json tokens = json::object(), fonts = json::array();
        for (const auto& a : art_) {
            if (a.value("subject", "") != "ui:panel" || a.value("icon", "").empty()) continue;
            fs::create_directories(host_.projectRoot / "ui" / "art", themeEc);
            fs::copy_file(host_.projectRoot / a.value("icon", ""), host_.projectRoot / "ui" / "art" / "panel.png",
                          fs::copy_options::overwrite_existing, themeEc);
            if (!themeEc) {
                tokens["panel-decorator"] = "image(../art/panel.png)"; // relative to the styles (ui/styles/)
                files_["ui/art/panel.png"] = {{"planner", "ui"}};
            }
        }
        if (const std::string font = ui.value("font", ""); !font.empty() && font.find_first_of("/\\.") == std::string::npos) {
            // The project's own (assets/fonts), else the engine's (data/ui/fonts, found beside its documents).
            fonts.push_back(fs::exists(host_.projectRoot / "assets" / "fonts" / (font + ".ttf"), themeEc) ? "assets/fonts/" + font + ".ttf"
                                                                                                          : "fonts/" + font + ".ttf");
            tokens["font-family"] = font;
        }
        std::ofstream(host_.projectRoot / "ui" / "theme.json", std::ios::binary)
            << json{{"base", ui.value("theme", "")}, {"tokens", tokens}, {"fonts", fonts}}.dump(2);
        files_["ui/theme.json"] = {{"planner", "ui"}};
    }
    // Each place's map (the map window, M): its painting, else its plan drawn from above, as ui/maps/<scene>.png, and
    // beside it how a scene's position lands on it (ui/maps/<scene>.json), so the map shows where the player is.
    for (const auto& r : rows_) {
        if (r.build.empty()) continue;
        const fs::path folder = host_.projectRoot / "places" / r.build;
        std::ifstream solvedIn(folder / "layout.solved.json", std::ios::binary);
        const json solved = solvedIn ? json::parse(solvedIn, nullptr, false) : json();
        if (!solved.is_object() || !solved.contains("grid")) continue;
        std::error_code mapEc;
        fs::create_directories(host_.projectRoot / "ui" / "maps", mapEc);
        const std::string stem = fs::path(r.scene).stem().string();
        json meta;
        const json painting = [&] {
            std::ifstream in(folder / "painting.json", std::ios::binary);
            return in ? json::parse(in, nullptr, false) : json();
        }();
        if (fs::exists(folder / "painting.png", mapEc) && painting.is_object() && painting.contains("view")) {
            fs::copy_file(folder / "painting.png", host_.projectRoot / "ui" / "maps" / (stem + ".png"), fs::copy_options::overwrite_existing, mapEc);
            const PaintView v = PaintView::From(painting["view"]);
            const double span = (v.x1 - v.x0) + (v.y1 - v.y0);
            meta = {{"kind", "iso"}, {"view", painting["view"]}, {"width", span * v.tw / 2}, {"height", span * v.th / 2 + v.top}};
        } else {
            // From above, no legend or title: a cell is `cell` px from the bounds' corner.
            LayoutImageOptions options;
            options.legend = false;
            const ImageData picture = RenderLayoutImage(solved, options);
            if (picture.width == 0 || !WritePng(host_.projectRoot / "ui" / "maps" / (stem + ".png"), picture)) continue;
            const json b = solved["grid"].value("bounds", json::array({0, 0, 64, 64}));
            meta = {{"kind", "grid"}, {"x0", b[0]}, {"y0", b[1]}, {"w", b[2]}, {"h", b[3]}};
        }
        // The scene's cells against the plan's: the middle of what's built where the plan put it (a building's footprint
        // against its entity), the most common difference.
        {
            Registry reg;
            std::map<std::pair<int, int>, int> votes;
            if (LoadScene(reg, Project{host_.projectRoot}.Resolve(r.scene).string())) {
                std::map<std::string, const json*> byId;
                const json objects = solved.value("objects", json::array()); // (byId points into it)
                for (const auto& o : objects) byId[o.value("id", "")] = &o;
                for (auto [e, node, w] : reg.view<PlaceNode, WorldTransform2D>().each()) {
                    const auto it = byId.find(node.statement);
                    if (it == byId.end()) continue;
                    const json& o = *it->second;
                    const double cx = o.value("x", 0) + o.value("w", 1) / 2.0, cy = o.value("y", 0) + o.value("h", 1) / 2.0;
                    ++votes[{static_cast<int>(std::lround(w.x - cx)), static_cast<int>(std::lround(w.y - cy))}];
                }
            }
            std::pair<int, int> offset{0, 0};
            int best = 0;
            for (const auto& [d, n] : votes) {
                if (n > best) best = n, offset = d;
            }
            meta["offset"] = {offset.first, offset.second};
        }
        std::ofstream(host_.projectRoot / "ui" / "maps" / (stem + ".json"), std::ios::binary) << meta.dump(2);
        files_["ui/maps/" + stem + ".png"] = {{"planner", "places/" + PlanSlug(r.place)}};
    }
    // The game starts in the plan's first place (Project Settings ▸ Game ▸ start scene).
    ActiveSettings().game.startScene = StartScene();
    SaveSettings(EngineReflection(), project.SettingsPath().string());
    files_["game.json"] = {{"planner", "brief"}};
    std::ofstream(store_.Dir() / "files.json", std::ios::binary) << json{{"schema", "aige.plan-files/1"}, {"files", files_}}.dump(2);
    Record("write", "files", std::to_string(files_.size()) + " files");
}

// --- ⑫ The playtest ---------------------------------------------------------------------------------------

void GameBuild::Playtest() {
    const Project project{host_.projectRoot};
    json checks = json::array();
    int passed = 0, total = 0;
    auto check = [&](const std::string& what, bool ok, const std::string& detail = {}) {
        ++total;
        passed += ok;
        checks.push_back({{"check", what}, {"ok", ok}, {"detail", detail}});
    };
    // No two actions of one context share a key unless grouped (32.7).
    {
        const auto clashes = KeyClashes(LoadInputMap(host_.projectRoot));
        check("no two actions share a key", clashes.empty(), clashes.empty() ? "" : clashes.front());
    }
    // Every enemy in a place can be fought (32.3).
    for (const auto& e : bindings_.value("enemies", json::array())) {
        const json cast = e.value("cast", json::object());
        if (cast.value("combat", "") == "none") check("enemy " + e.value("object", "") + " fights", false, "its class doesn't fight");
    }
    // Every scene loads, with its gateways leading somewhere.
    std::map<std::string, std::unique_ptr<Registry>> scenes;
    for (const auto& r : rows_) {
        auto reg = std::make_unique<Registry>();
        const bool loaded = LoadScene(*reg, project.Resolve(r.scene).string());
        check("scene " + r.scene + " loads", loaded && reg->view<WorldTransform2D>().size() > 0);
        if (loaded) scenes[r.scene] = std::move(reg);
    }
    const auto gatewayProblems = GatewayProblems(project);
    check("every gateway leads to a scene and marker", gatewayProblems.empty(), gatewayProblems.empty() ? "" : gatewayProblems.front());
    // The scenes reachable from the start through gateways.
    std::set<std::string> reachable;
    std::vector<std::string> todo = {StartScene()};
    while (!todo.empty()) {
        const std::string s = todo.back();
        todo.pop_back();
        if (!reachable.insert(s).second || !scenes.count(s)) continue;
        for (auto [e, gw] : scenes[s]->view<Gateway>().each()) todo.push_back(gw.scene);
        for (auto [e, interior] : scenes[s]->view<Interior>().each()) {
            if (interior.kind == "scene" && !interior.scene.empty()) todo.push_back(interior.scene); // through a building's door
        }
    }
    for (const auto& r : rows_) check("scene " + r.scene + " can be reached from the start", reachable.contains(r.scene));
    // Can someone walk there in its scene from where they arrive?
    auto walkable = [&](Registry& reg, float x, float y) {
        const TileGrid grid = TileGrid::Build(reg);
        auto from = MarkerAt(reg, "arrive");
        if (!from) return true;
        // To it, or beside it (a thing one uses stands in its own cell: an altar, a lever).
        const int tx = static_cast<int>(std::floor(x)), ty = static_cast<int>(std::floor(y));
        for (const auto [dx, dy] : {std::pair{0, 0}, std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1},
                                    std::pair{1, 1}, std::pair{-1, -1}, std::pair{1, -1}, std::pair{-1, 1}}) {
            if ((dx != 0 || dy != 0) && grid.IsSolid(tx + dx, ty + dy)) continue;
            const auto path = FindPath([&](int gx, int gy) { return grid.IsSolid(gx, gy); },
                                       {static_cast<int>(std::floor(from->first)), static_cast<int>(std::floor(from->second))}, {tx + dx, ty + dy});
            if (path.has_value()) return true;
        }
        return false;
    };
    // Quests and dialogues.
    StoryLibrary story(host_.projectRoot);
    story.Refresh();
    json quests = json::array();
    for (const auto& doc : store_.List("quests")) {
        const std::string id = store_.Read(doc).value_or(json::object()).value("id", PlanSlug(doc));
        const Quest* quest = story.FindQuest(id);
        json result = {{"id", id}};
        if (!quest) {
            result["result"] = "skipped";
            result["reason"] = "its file didn't load";
            quests.push_back(result);
            check("quest " + id + " loads", false);
            continue;
        }
        const auto problems = ValidateQuest(*quest, [&](const std::string& q) { return story.FindQuest(q); });
        check("quest " + id + " is valid", problems.empty(), problems.empty() ? "" : problems.front());
        std::string stuck;
        for (const auto& stage : quest->stages) {
            for (const auto& o : stage.objectives) {
                const std::string target = quest->Resolve(o.target);
                bool found = target.empty() || o.type == "survive" || o.type == "have" || o.type == "collect";
                for (auto& [scene, reg] : scenes) {
                    if (found || !reachable.contains(scene)) continue;
                    if (o.type == "talk" || o.type == "deliver" || o.type == "escort") {
                        for (auto [e, talk, at] : reg->view<Talk, WorldTransform2D>().each()) {
                            if (talk.character == target && walkable(*reg, at.x, at.y)) found = true;
                        }
                    } else if (o.type == "defeat") {
                        // Someone that can be fought (health and a hurtbox), named or keyed as the target (a plan's
                        // boss by its character key), or any enemy for "Enemy".
                        for (auto [e, at] : reg->view<WorldTransform2D>().each()) {
                            if (!reg->all_of<Health, Hurtbox>(e) || !walkable(*reg, at.x, at.y)) continue;
                            const auto* name = reg->try_get<Name>(e);
                            const auto* actor = reg->try_get<Actor>(e);
                            const auto* team = reg->try_get<Faction>(e);
                            const auto* copy = reg->try_get<PrefabInstance>(e);
                            if ((name && name->value == target) || (actor && (actor->key == target || actor->prefab == target)) ||
                                (copy && copy->path == target) || (target == "Enemy" && team && team->team == Team::Enemy)) found = true;
                        }
                        // Or a spawner makes them (a dungeon's enemies come back): any enemy, or its classes' prefabs.
                        for (auto [e, sp] : reg->view<Spawner>().each()) {
                            if (target == "Enemy" || target.rfind("prefabs/enemies/", 0) == 0) found = true;
                        }
                    } else { // reach, use
                        if (auto m = MarkerAt(*reg, target)) found = walkable(*reg, m->first, m->second);
                        if (!found) {
                            for (auto [e, name, at] : reg->view<Name, WorldTransform2D>().each()) {
                                if (name.value == target) found = walkable(*reg, at.x, at.y);
                            }
                        }
                    }
                }
                if (!found && stuck.empty()) stuck = "stage " + std::to_string(stage.index) + ": nowhere to " + o.type + " '" + target + "'";
            }
        }
        result["result"] = stuck.empty() ? "completed" : "stuck";
        if (!stuck.empty()) result["reason"] = stuck;
        check("quest " + id + " can be walked to the end", stuck.empty(), stuck);
        quests.push_back(result);
    }
    for (const auto& d : story.Dialogues()) {
        const auto problems = ValidateDialogue(d, [&](const std::string& q) { return story.FindQuest(q); });
        check("dialogue " + d.path + " is valid", problems.empty(), problems.empty() ? "" : problems.front());
    }
    playtest_ = {{"schema", "aige.plan-report/1"}, {"kind", "playtest"}, {"quests", quests}, {"checks", checks},
                 {"summary", {{"passed", passed}, {"total", total}}},
                 {"note", "A walk through the game's logic: scenes, gateways, quests and dialogue (not simulated combat)."}};
    std::ofstream(store_.Dir() / "playtest.json", std::ios::binary) << playtest_.dump(2);
    Record("playtest", "playtest", std::to_string(passed) + "/" + std::to_string(total) + " checks passed");
}

// --- ⑬ The result --------------------------------------------------------------------------------------------

void GameBuild::WriteResult() {
    const json brief = store_.Read("brief").value_or(json::object());
    const StoryGraph g = StoryGraph::Load(StoryGraph::PathIn(host_.projectRoot)).value_or(StoryGraph{});
    int built = 0, placeholders = 0;
    json places = json::array();
    for (const auto& r : rows_) {
        built += r.status == "built";
        placeholders += r.status == "placeholder";
        places.push_back({{"place", r.place}, {"scene", r.scene}, {"build", r.build}, {"status", r.status}, {"checks", r.checks}, {"error", r.error}});
    }
    int completed = 0;
    for (const auto& q : playtest_.value("quests", json::array())) completed += q.value("result", "") == "completed";
    const size_t quests = store_.List("quests").size();
    auto range = [&](const char* path) {
        const json v = BriefGet(brief, path);
        return v.is_array() && v.size() == 2 ? v[0].dump() + "–" + v[1].dump() : v.dump();
    };
    json asked = json::array();
    asked.push_back({{"what", "places"}, {"asked", range("scope.places")}, {"made", rows_.size()}});
    asked.push_back({{"what", "story scenes"}, {"asked", range("scope.scenes")}, {"made", g.SceneOrder().size()}});
    asked.push_back({{"what", "side quests"}, {"asked", range("scope.sideQuests")}, {"made", quests > 0 ? quests - 1 : 0}});
    asked.push_back({{"what", "characters"}, {"asked", BriefGet(brief, "cast").dump()}, {"made", g.OfClass("Character", StoryOntology::Engine()).size()}});
    const std::string headline = "Planned and built " + brief.value("title", "the game") + ": " + std::to_string(g.SceneOrder().size()) + " scenes, " +
                                 std::to_string(g.OfClass("Character", StoryOntology::Engine()).size()) + " characters, " + std::to_string(quests) +
                                 " quests, " + std::to_string(rows_.size()) + " places (" + std::to_string(placeholders) + " placeholders); " +
                                 std::to_string(completed) + " of " + std::to_string(quests) + " quests can be finished.";
    const json result = {{"schema", "aige.plan-result/1"},
                         {"headline", headline},
                         {"features", features_},
                         {"askedVsMade", asked},
                         {"places", places},
                         {"quests", playtest_.value("quests", json::array())},
                         {"checks", playtest_.value("summary", json::object())},
                         {"placeholders", placeholders},
                         {"files", files_.size()},
                         {"startScene", StartScene()},
                         {"whatNext", {"Play from the start", "Open a place's build to refine it", "Rebuild a placeholder place", "Add a side quest"}}};
    std::ofstream(store_.Dir() / "result.json", std::ios::binary) << result.dump(2);
    message_ = headline;
    Record("result", "result", headline);
}

void GameBuild::Next(const std::string& stage) {
    stage_ = stage;
    // The furthest stage reached (Continue resumes here; an earlier step run again doesn't move it back).
    if (BuildStageOrder(stage) > BuildStageOrder(reached_)) reached_ = stage;
    subStep_.clear();
    static const std::map<std::string, std::string> labels = {
        {"requirements", "Requirements: reading the plan…"}, {"build", "⑩ Building the places…"},
        {"characters", "Characters: drawing and animating them…"}, {"objects", "Props & objects: drawing and making them…"},
        {"quests", "Quests: writing them…"}, {"placements", "Placements: placing everyone and everything…"},
        {"behaviour", "Behaviour: packages, routines and dialogue…"}, {"scenes", "Scenes: triggers, cutscenes and small scenes…"},
        {"systems", "Systems: the gameplay's scripts…"}, {"visuals", "Visuals: the interface…"},
        {"finish", "Building what's left…"}, {"features", "⑫ Setting up the gameplay…"}, {"playtest", "Playtesting…"},
        {"result", "⑬ The result…"}};
    if (labels.count(stage)) message_ = labels.at(stage);
}

json GameBuild::QuestAsks() const {
    json out = json::object();
    const Project project{host_.projectRoot};
    for (const auto& doc : store_.List("quests")) {
        const std::string id = store_.Read(doc).value_or(json::object()).value("id", PlanSlug(doc));
        std::ifstream in(project.Resolve("quests/" + id + ".json"), std::ios::binary);
        const json q = in ? json::parse(in, nullptr, false) : json();
        if (!q.is_object()) continue;
        json stages = json::object();
        for (const auto& s : q.value("stages", json::array())) {
            if (s.is_object() && s.contains("index") && s["index"].is_number()) stages[std::to_string(s["index"].get<int>())] = s.value("objectives", json::array());
        }
        out[id] = stages;
    }
    return out;
}

void GameBuild::KeepQuestAsks(const json& before, const std::string& by) {
    // The stages ask of the player what the story does (⑪ made them from it): what a step of ⑫ added is optional (it
    // doesn't hold the stage back), what it took out or changed is put back. Its stages' effects are its own.
    const Project project{host_.projectRoot};
    for (auto entry = before.begin(); entry != before.end(); ++entry) {
        const std::string id = entry.key();
        const json& was = entry.value();
        const fs::path path = project.Resolve("quests/" + id + ".json");
        std::ifstream in(path, std::ios::binary);
        json q = in ? json::parse(in, nullptr, false) : json();
        in.close();
        if (!q.is_object() || !q.contains("stages") || !q["stages"].is_array()) continue;
        bool changed = false;
        for (auto& s : q["stages"]) {
            if (!s.is_object() || !s.contains("index") || !s["index"].is_number()) continue;
            const std::string key = std::to_string(s["index"].get<int>());
            if (!was.contains(key)) {
                Record("kept", "quests/" + id, "stage " + key + " added by " + by, "the quest's stages are the story's: Validate reports it");
                continue;
            }
            const json old = was[key];
            json now = s.value("objectives", json::array());
            if (now == old) continue;
            json kept = json::array();
            auto findIn = [](const json& list, const std::string& oid) -> const json* {
                for (const auto& o : list) {
                    if (o.value("id", "") == oid) return &o;
                }
                return nullptr;
            };
            for (const auto& o : old) kept.push_back(o); // what the story asks, as it was
            for (auto o : now) {
                if (findIn(old, o.value("id", ""))) continue;
                if (!o.value("optional", false)) Record("kept", "quests/" + id, "stage " + key + ": \"" + o.value("text", o.value("id", "")) + "\" optional", by + " added it");
                o["optional"] = true;
                kept.push_back(o);
            }
            for (const auto& o : old) {
                const json* there = findIn(now, o.value("id", ""));
                if (!there) Record("kept", "quests/" + id, "stage " + key + ": \"" + o.value("text", o.value("id", "")) + "\" put back", by + " took it out");
                else if (*there != o) Record("kept", "quests/" + id, "stage " + key + ": \"" + o.value("text", o.value("id", "")) + "\" as it was", by + " changed it");
            }
            s["objectives"] = kept;
            changed = true;
        }
        if (changed) WriteStoryFile(host_.projectRoot, "quests", id, q);
    }
}

bool GameBuild::DriveFeatures() {
    for (auto& f : features_) {
        const std::string status = f.value("status", "");
        if (status == "running") {
            const std::string now = host_.featureStatus ? host_.featureStatus() : std::string("failed: no AI panel");
            if (now == "working") return false; // its answer pumps again
            f["status"] = now == "done" ? "done" : "failed";
            if (now != "done") f["error"] = now.rfind("failed: ", 0) == 0 ? now.substr(8) : now;
            Record("feature", f.value("id", ""), f.value("status", ""), f.value("error", ""));
            // A character it placed again (one already in its scene, or one it put in a scene that isn't theirs): taken out.
            KeepOneOfEach(f.value("scene", ""));
            // A feature's step keeps the quests as the story made them (a Validate fix may change them: that's its job).
            if (f.contains("asksBefore")) {
                KeepQuestAsks(f["asksBefore"], f.value("name", f.value("id", "")));
                f.erase("asksBefore");
            }
            return true;
        }
        if (status != "queued") continue;
        // One that failed is reported on the result page; the game goes on being built.
        const std::string request = f.value("request", "");
        if (!host_.startFeature || request.empty()) {
            f["status"] = "skipped";
            f["error"] = request.empty() ? "it has no build request" : "no AI panel here";
            return true;
        }
        const bool fix = f.value("id", "").rfind("fix-", 0) == 0;
        if (!fix) f["asksBefore"] = QuestAsks();
        if (const std::string why = host_.startFeature(request, f.value("scene", "")); !why.empty()) {
            f["status"] = "failed";
            f["error"] = why;
            f.erase("asksBefore");
            return true;
        }
        f["status"] = "running";
        int done = 0;
        for (const auto& g : features_) done += g.value("status", "") != "queued" && g.value("status", "") != "running";
        // Said as the Build tab it's for ("Behaviour: the AI panel builds ..."); a gameplay feature (a build before the
        // requirements) as Systems'.
        static const std::map<std::string, std::string> tabNames = {
            {"characters", "Characters"}, {"objects", "Props & objects"}, {"quests", "Quests"}, {"placements", "Placements"},
            {"behaviour", "Behaviour"}, {"scenes", "Scenes"}, {"systems", "Systems"}, {"visuals", "Visuals"}};
        const auto tabName = tabNames.find(f.value("tab", std::string("systems")));
        message_ = (tabName != tabNames.end() ? tabName->second : std::string("Systems")) + ": the AI panel builds " + f.value("name", "a feature") +
                   " (task " + std::to_string(done + 1) + " of " + std::to_string(features_.size()) + ")…";
        return true;
    }
    return false;
}

bool GameBuild::Pump() {
    // A Build tab's revision answered: applied whatever the build is doing.
    bool revised = false;
    while (!reviseAnswers_.empty()) {
        const auto [key, body] = reviseAnswers_.front();
        reviseAnswers_.erase(reviseAnswers_.begin());
        ApplyTabRevision(key, body);
        revised = true;
    }
    while (!objectAnswers_.empty()) {
        const auto [key, body] = objectAnswers_.front();
        objectAnswers_.erase(objectAnswers_.begin());
        ApplyObjectState(key, body);
        revised = true;
    }
    while (!visualAnswers_.empty()) {
        const auto [key, body] = visualAnswers_.front();
        visualAnswers_.erase(visualAnswers_.begin());
        ApplyVisualAnswer(key, body);
        revised = true;
    }
    if (revised) Save();
    if (status_ != "working") return revised;
    bool did = revised;
    // Placements (34.11): decided (the AI's one call, else the rules) and everyone placed by them; nothing else meanwhile.
    if (!placeStep_.empty()) {
        did = DrivePlacements();
        if (!placeStep_.empty() || status_ != "working") {
            if (did) Save();
            return did;
        }
    }
    if (placeAgain_) {
        placeAgain_ = false;
        if (const std::string why = PlaceCharactersAgain([](const std::string&) {}); !why.empty()) throw std::runtime_error(why);
        message_ = "⑪ The characters placed and the quests' roles found again";
        did = true;
    }
    // ⑬ Validate: the AI's look at each scene against what was built.
    const bool implementing = reqs_.value("implementing", false);
    if (check_.value("status", "") == "checking" || TestsPending() || reqs_.value("status", "") == "making" || implementing) {
        did = DriveCheck() || did;
        did = DriveTests() || did;
        if (reqs_.value("status", "") == "making") did = DriveRequirements() || did;
        if (implementing) {
            // ⑫ Implement (or a requirement fixed once built): its items, their art, the AI panel's tasks.
            if (stage_ == "done" && artApproved_) did = DriveArt() || did;
            if (stage_ != "features") did = DriveFeatures() || did;
            did = DriveImplement() || did;
        }
        if (stage_ == "done" && !did && check_.value("status", "") != "checking" && !TestsPending() && reqs_.value("status", "") != "making" &&
            !reqs_.value("implementing", false)) {
            status_ = "done";
        }
        if (stage_ == "done") {
            if (did) Save();
            return did;
        }
    }
    // The requirements made again (or a tab's items built) while the build was stopped: only they; it stops again once
    // they're done.
    if (reqsOnly_) {
        if (reqs_.value("status", "") != "making" && !reqs_.value("implementing", false)) {
            reqsOnly_ = false;
            status_ = "stopped";
            message_ += " Continue goes on with the build.";
            did = true;
        }
        if (did) Save();
        return did;
    }
    // A game built before its ways through were shown (development_plan.md 32.12): its doors found, art for the rest.
    if (!gatewaysChecked_ && stage_ != "build" && stage_ != "requirements" && stage_ != "characters" && stage_ != "objects" &&
        stage_ != "quests" && stage_ != "placements") {
        gatewaysChecked_ = true;
        if (const int added = QueueGateways(); added > 0) {
            artApproved_ = true;
            message_ = "⑩ Finding the buildings' doors and drawing the ways through…";
            Record("art", "gateways", std::to_string(added) + " ways through to show", "the game built before them");
            did = true;
        }
    }
    if (stage_ == "build") {
        did = DriveBuilds() || did;
        const bool done = std::all_of(rows_.begin(), rows_.end(), [](const Row& r) {
            return r.status == "built" || r.status == "placeholder" || (r.status == "failed" && r.mode == "manual");
        });
        const bool waitingOnYou = std::any_of(rows_.begin(), rows_.end(), [](const Row& r) { return r.status == "needs-you"; });
        if (waitingOnYou && !did) message_ = "⑩ A place waits for you in the Build panel.";
        // Every place built: the user looks at them first, and Continue goes on (connecting them, the characters...).
        if (done && !placesApproved_ && !AutoApprove()) {
            const std::string waiting = "⑩ All " + std::to_string(rows_.size()) +
                                        " places are built: look at them, then Continue to connect them and draw the characters";
            if (status_ != "waiting" || message_ != waiting) {
                status_ = "waiting";
                message_ = waiting;
                Save();
            }
            return false;
        }
        if (done) {
            try {
                ConnectScenes();
            } catch (const std::exception& e) {
                throw std::runtime_error(std::string("⑩ connecting the scenes: ") + e.what());
            }
            if (rebuildOnly_ && BuildStageOrder(reached_) > BuildStageOrder("build")) {
                // One place rebuilt from the start: the rest of the build stays where it was (its later steps
                // aren't run again), so Continue still goes on from the furthest step reached.
                rebuildOnly_ = false;
                stage_ = reached_;
                status_ = "stopped";
                message_ = "⑩ The place is rebuilt. Continue goes on with the build.";
            } else {
                rebuildOnly_ = false;
                Next("characters");
            }
            did = true;
        }
    }
    // The Build page, in its tabs' order: each tab builds its own part, to the requirements (docs/ai/ai-planner.md).
    // Requirements → Places → Characters → Props & objects → Quests → Placements → Behaviour → Scenes → Systems →
    // Visuals; then every item left (the last pass), the playtest and the result. `subStep_` is how far a tab's step
    // has gone (it's "" as the step begins).
    if (stage_ == "requirements") {
        if (reqs_.value("status", "") == "making") {
            // being made (driven above)
        } else if (reqs_.value("items", json::array()).empty()) {
            if (const std::string why = MakeRequirements(); !why.empty()) throw std::runtime_error("the requirements: " + why);
            did = true;
        } else {
            Next("build");
            did = true;
        }
    }
    const bool artLeft = std::any_of(art_.begin(), art_.end(), [](const json& a) {
        return a.value("status", "") == "queued" || a.value("status", "") == "drawing";
    });
    int artCalls = 0; // the paid calls left: a picture not drawn, each clip not drawn (what was drawn before is reused)
    for (const auto& a : art_) {
        if (a.value("status", "") != "queued" && a.value("status", "") != "drawing") continue;
        artCalls += (a.value("sheet", "").empty() && a.value("icon", "").empty() ? 1 : 0) +
                    std::max(0, static_cast<int>(a.value("actions", json::array()).size()) - a.value("next", 0));
    }
    // Art queued by a step before (the others drawn, the ways through): drawn on in the steps that don't draw their own.
    static const std::set<std::string> kDrawing = {"requirements", "build", "characters", "objects", "visuals"};
    if (!kDrawing.contains(stage_) && artLeft && artApproved_) {
        did = DriveArt() || did;
        if (stage_ == "done") {
            const bool still = std::any_of(art_.begin(), art_.end(), [](const json& a) {
                return a.value("status", "") == "queued" || a.value("status", "") == "drawing";
            });
            if (!still) status_ = "done", message_ = "The others are drawn too.";
            if (did) Save();
            return did;
        }
    }
    // A drawing step's art: approved first when it costs calls (unless the Brief's plan.building is automatic).
    auto drawArt = [&](const std::string& what) {
        if (artLeft && !artApproved_ && !AutoApprove() && artCalls > 0) {
            const std::string waiting = what + ": " + std::to_string(artCalls) + " calls to draw: approve, or skip them (they keep their placeholders)";
            if (message_ != waiting) message_ = waiting, did = true;
            return false;
        }
        did = DriveArt() || did;
        return std::none_of(art_.begin(), art_.end(), [](const json& a) { return a.value("status", "") == "queued" || a.value("status", "") == "drawing"; });
    };
    if (stage_ == "characters") {
        // Character art and animations: each character drawn once, then animated for what the requirements ask.
        if (subStep_.empty()) {
            try {
                QueueArt(false, "characters");
            } catch (const std::exception& e) {
                throw std::runtime_error(std::string("Characters: choosing what's drawn: ") + e.what());
            }
            subStep_ = "drawing";
            did = true;
        } else if (drawArt("Characters") && !RunTab("characters")) {
            MarkMetRequirements();
            Next("objects");
            did = true;
        }
    }
    if (stage_ == "objects") {
        // Props and objects: the items' icons and the ways through drawn; each described thing made in its scene (its
        // SmartObject, its Reactions, each state's picture and animation).
        if (subStep_.empty()) {
            try {
                QueueArt(false, "objects");
            } catch (const std::exception& e) {
                throw std::runtime_error(std::string("Props & objects: choosing what's drawn: ") + e.what());
            }
            if (!store_.Read("objects").value_or(json::object()).value("objects", json::array()).empty()) BuildObjects("all");
            subStep_ = "making";
            did = true;
        } else {
            const json world = WorldState();
            bool making = !objectAnswers_.empty();
            // (Kept, not iterated as a temporary: items() of a temporary is gone before the loop reads it.)
            const json objects = world.value("objects", json::object());
            for (const auto& [id, m] : objects.items()) {
                making = making || (m.is_object() && (m.value("status", "") == "building" || m.value("status", "") == "paused"));
            }
            if (drawArt("Props & objects") && !making && !RunTab("objects")) {
                MarkMetRequirements();
                Next("quests");
                did = true;
            }
        }
    }
    if (stage_ == "quests") {
        // The game's quests written (their stages, objectives, stage effects, journal and markers); bound to who and what
        // is placed in Placements, and written again then.
        if (subStep_.empty()) {
            WriteQuests();
            subStep_ = "written";
            did = true;
        } else if (!RunTab("quests")) {
            MarkMetRequirements();
            Next("placements");
            did = true;
        }
    }
    if (stage_ == "placements") {
        if (subStep_.empty()) {
            // Everyone and everything placed by the rules, the quests' roles bound; then where exactly decided by the AI
            // (one call, given the requirements), unless a painted place's buildings aren't found yet (ApplyEntrances).
            // Nothing else goes on while it decides (driven first, above): everyone is placed again by it.
            try {
                Populate();
            } catch (const std::exception& e) {
                throw std::runtime_error(std::string("Placements: placing the characters and binding the quests (") + populateAt_ + "): " + e.what());
            }
            ApplyDrawnArt(); // their drawings on the characters it made
            if (!placeAskedForPopulate_ && host_.request && ReadPlacements(host_.projectRoot).value("decided", "") != "ai" && !PaintedWithoutStructures()) {
                placeAskedForPopulate_ = true;
                RequestPlacements("Placements: placing the characters, props and objects");
            }
            subStep_ = "placed";
            did = true;
        } else if (subStep_ == "placed") {
            WriteFiles(); // the quests again with their roles bound, and the rest of the game's files
            MarkMetRequirements();
            subStep_ = "written";
            did = true;
        } else if (!RunTab("placements")) {
            Next("behaviour");
            did = true;
        }
    }
    if (stage_ == "behaviour") {
        // Each character's AI packages, routine and changes by quest stage (the cast timeline), and their dialogue.
        if (subStep_.empty()) {
            QueueWriting();
            subStep_ = "writing";
            did = true;
        } else {
            did = DriveJobs() || did;
            const bool idle = std::none_of(jobs_.begin(), jobs_.end(), [](const Job& j) { return j.status == "queued" || j.status == "running"; });
            if (idle && legacyFeatures_) {
            // The features, from the plan: each its id, name, request and how it went.
            features_ = json::array();
            for (const auto& f : store_.Read("gameplay").value_or(json::object()).value("features", json::array())) {
                // Its steps, each where it runs; a feature without any: its one request, for the whole project.
                json steps = store_.Read("systemdesigns/" + PlanSlug(f.value("id", ""))).value_or(json::object()).value("steps", json::array());
                if (steps.empty() && !f.value("buildRequest", "").empty()) steps.push_back({{"place", ""}, {"request", f.value("buildRequest", "")}});
                for (const auto& step : steps) {
                    const std::string place = step.value("place", "");
                    // Its scripts are written and on what they're for (WriteFiles): the request says so, so they aren't again.
                    std::string request = step.value("request", "");
                    for (const auto& sc : store_.Read("systemdesigns/" + PlanSlug(f.value("id", ""))).value_or(json::object()).value("scripts", json::array())) {
                        request += " (Already done: " + sc.value("path", "") + " is written and attached to " +
                                   sc.value("on", json::object()).value("target", std::string("the game")) + "; don't write it again.)";
                    }
                    // The quests are built from the story (⑪): the step works with them as they are.
                    request += " (The quests in quests/ are built from the story and already have their stages, objectives and "
                               "targets: don't create quests, add or change their stages or objectives, or set their stage; react to "
                               "their stages and events, and give what the story's scenes need.)";
                    json row = {{"id", f.value("id", "")}, {"name", f.value("name", "")}, {"place", place}, {"scene", ""}, {"request", request},
                                {"needsScript", f.value("needsScript", false)}, {"status", "queued"}, {"error", ""}};
                    if (!place.empty()) {
                        const Row* built = FindRow(place);
                        if (!built) {
                            for (const auto& r : rows_) {
                                if (PlanSlug(r.place) == PlanSlug(place)) built = &r;
                            }
                        }
                        if (built) row["scene"] = built->scene;
                        else row["status"] = "failed", row["error"] = "no scene was built for " + place;
                    }
                    features_.push_back(row);
                }
            }
            Next("features");
                did = true;
            } else if (idle && !RunTab("behaviour")) {
                MarkMetRequirements();
                Next("scenes");
                did = true;
            }
        }
    }
    if (stage_ == "scenes") {
        // The story scenes' triggers, the cutscenes, and the small scenes (each an ambient cutscene near the player).
        if (subStep_.empty()) {
            if (!store_.Read("smallscenes").value_or(json::object()).value("scenes", json::array()).empty()) BuildSmallScenes("all");
            subStep_ = "made";
            did = true;
        } else if (!RunTab("scenes")) {
            MarkMetRequirements();
            Next("systems");
            did = true;
        }
    }
    if (stage_ == "systems") {
        // The gameplay systems' scripts (⑦'s steps), by the AI panel.
        if (!RunTab("systems")) {
            MarkMetRequirements();
            Next("visuals");
            did = true;
        }
    }
    if (stage_ == "visuals") {
        // ⑧'s panels: their images and documents; the interface's own picture.
        if (subStep_.empty()) {
            QueueArt(false, "interface");
            if (host_.request && !store_.Read("ui").value_or(json::object()).value("panels", json::array()).empty()) BuildVisuals("all");
            subStep_ = "building";
            did = true;
        } else {
            bool building = !visualAnswers_.empty();
            const json panels = VisualsState().value("panels", json::object());
            for (const auto& [id, p] : panels.items()) {
                building = building || p.value("status", "") == "images" || p.value("status", "") == "writing" || p.value("status", "") == "paused";
            }
            if (drawArt("Visuals") && !building && !RunTab("visuals")) {
                MarkMetRequirements();
                Next("finish");
                did = true;
            }
        }
    }
    if (stage_ == "finish") {
        // Every item left (one waiting on a later step's, one a stop left), then all the requirements checked.
        if (!RunTab("")) {
            if (host_.featuresDone) host_.featuresDone();
            Next("playtest");
            did = true;
        }
    }
    if (stage_ == "features") {
        did = DriveFeatures() || did;
        const bool idle = std::none_of(features_.begin(), features_.end(), [](const json& f) {
            return f.value("status", "") == "queued" || f.value("status", "") == "running";
        });
        if (idle) {
            if (host_.featuresDone) host_.featuresDone();
            Next("playtest");
            did = true;
        }
    }
    if (stage_ == "playtest") {
        Playtest();
        Next("result");
        did = true;
    }
    if (stage_ == "result") {
        WriteResult();
        stage_ = "done";
        status_ = "done";
        check_["trace"] = TraceScenes(); // the story's scenes and what's built for them now (Validate again to check them)
        SaveCheck();
        did = true;
    }
    if (did) Save();
    return did;
}

// --- Views ------------------------------------------------------------------------------------------------------

json GameBuild::State() const {
    json rows = json::array();
    for (const auto& r : rows_) {
        // The place builder's own step for the place being built (request, assets, plan, layout...).
        std::string step, phase;
        if (r.status == "building") {
            const auto b = builds_.find(r.place);
            if (b != builds_.end() && b->second) {
                step = b->second->Stage();
                const json s = b->second->State();
                if (s.is_object() && s.value("planPhase", json()).is_string()) phase = s["planPhase"].get<std::string>();
            }
        }
        // Its build's own method (a painted place has no ③ Plan, and its steps are numbered by its method), whether
        // its zones are laid out yet (① Confirm until then), and whether it's painted already.
        std::string method = host_.buildMethod ? host_.buildMethod() : std::string("Assembled");
        bool layoutMade = false, paintedAlready = false;
        if (!r.build.empty()) {
            const fs::path folder = host_.projectRoot / "places" / r.build;
            std::ifstream sessionIn(folder / "session.json", std::ios::binary);
            const json session = sessionIn ? json::parse(sessionIn, nullptr, false) : json();
            if (session.is_object() && session.value("method", json()).is_string()) method = session["method"].get<std::string>();
            layoutMade = fs::exists(folder / "zones.solved.json");
            paintedAlready = fs::exists(folder / "painting.png");
        }
        json row = {{"place", r.place}, {"scene", r.scene}, {"buildType", r.buildType}, {"mode", r.mode}, {"status", r.status},
                    {"step", step}, {"planPhase", phase}, {"build", r.build}, {"error", r.error}, {"checks", r.checks}, {"dependsOn", r.dependsOn},
                    {"sections", r.sections}, {"awaitingApproval", r.awaitingApproval},
                    {"method", method}, {"layoutMade", layoutMade}, {"painted", paintedAlready}};
        // Painted (the Paint method): its tiles and calls, once its plan is laid out (before it's painted).
        if (!r.build.empty()) {
            const fs::path folder = host_.projectRoot / "places" / r.build;
            std::ifstream sessionIn(folder / "session.json"), solvedIn(folder / "layout.solved.json");
            const json session = sessionIn ? json::parse(sessionIn, nullptr, false) : json();
            if (session.is_object() && IsPaintMethod(session.value("method", std::string())) && solvedIn) {
                const json solved = json::parse(solvedIn, nullptr, false);
                if (solved.is_object() && solved.contains("grid")) {
                    const auto box = OccupiedBounds(solved);
                    const FreePaintPlan plan = PlanFreePaint(box[2] - box[0], box[3] - box[1], session.value("paintCellPx", kFreePaintCell));
                    // (klein composes in two calls; a dungeon is always FLUX-3-Image's, as PlaceBuild::PaintModel.)
                    const bool klein = r.buildType != "dungeon" && host_.paintModel && host_.paintModel() == "flux-2-klein-4b";
                    row["paint"] = {{"tiles", plan.tiles}, {"calls", plan.calls + (klein ? 1 : 0)}, {"cellPx", session.value("paintCellPx", kFreePaintCell)},
                                    {"painted", fs::exists(folder / "painting.png")}};
                }
            }
        }
        rows.push_back(row);
    }
    std::ifstream in(store_.Dir() / "result.json", std::ios::binary);
    const json result = json::parse(in, nullptr, false);
    return {{"active", Active()},  {"stage", stage_},           {"status", status_},  {"message", message_}, {"mode", mode_},
            {"reached", reached_},
            {"rows", rows},        {"bindings", bindings_},     {"playtest", playtest_}, {"files", files_.size()},
            {"features", [&] {
                 // ⑫ The gameplay set up: each feature's steps (the AI panel's tasks), how each went.
                 json out = json::array();
                 for (const auto& f : features_) {
                     std::string request = f.value("request", "");
                     if (request.size() > 160) request = Utf8Cut(request, 157) + "…";
                     out.push_back({{"id", f.value("id", "")}, {"name", f.value("name", "")}, {"place", f.value("place", "")},
                                    {"status", f.value("status", "")}, {"error", f.value("error", "")}, {"request", request}});
                 }
                 return out;
             }()},
            {"art", [&] {
                 // Who is drawn, how far along, and the calls: one to draw, one per action.
                 json out = json::array();
                 for (const auto& a : art_) {
                     out.push_back({{"subject", a.value("subject", "")}, {"name", a.value("name", "")}, {"status", a.value("status", "")},
                                    {"done", a.value("next", 0) + (a.value("sheet", "").empty() ? 0 : 1)},
                                    {"calls", 1 + a.value("actions", json::array()).size()}, {"error", a.value("error", "")},
                                    {"doing", a.value("inFlight", false) ? a.value("doing", "") : std::string()}});
                 }
                 return out;
             }()},
            {"result", result.is_discarded() ? json() : result},
            {"check", CheckView()},
            {"requirements", reqs_},
            {"visuals", VisualsState()},
            {"world", WorldState()},
            {"revisions", [&] {
                 std::ifstream in(store_.Dir() / "build-revisions.json", std::ios::binary);
                 const json j = in ? json::parse(in, nullptr, false) : json();
                 return j.is_object() ? j : json::object();
             }()}};
}

// --- Build ▸ Props & objects and Small scenes (World) -------------------------------------------------------------------

json GameBuild::WorldState() const {
    std::ifstream in(store_.Dir() / "world-build.json", std::ios::binary);
    const json j = in ? json::parse(in, nullptr, false) : json();
    return j.is_object() ? j : json{{"objects", json::object()}, {"scenes", json::object()}};
}

void GameBuild::SaveWorldState(const json& state) const {
    std::ofstream(store_.Dir() / "world-build.json", std::ios::binary) << state.dump(1);
}

namespace {
std::string WorldSlug(const std::string& text) {
    std::string out;
    for (const unsigned char ch : text) {
        if (std::isalnum(ch)) out += static_cast<char>(std::tolower(ch));
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}
std::string WorldLower(std::string text) {
    for (auto& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return text;
}
// A story effects or conditions list as the engine's JSON text ("" when there's none).
std::string JsonText(const json& j) { return j.is_array() && !j.empty() ? j.dump() : std::string(); }
} // namespace

std::string GameBuild::BuildObjects(const std::string& which) {
    const json objects = store_.Read("objects").value_or(json::object()).value("objects", json::array());
    if (!objects.is_array() || objects.empty()) return "World ▸ Props & objects hasn't described them yet";
    const Project project{host_.projectRoot};
    PrefabLibrary prefabs(project);
    json world = WorldState();
    int made = 0;
    for (const auto& o : objects) {
        const std::string oid = o.value("id", "");
        if (oid.empty() || (which != "all" && which != oid)) continue;
        json& m = world["objects"][oid];
        m = {{"status", "building"}, {"notes", json::array()}};
        auto note = [&](const std::string& n) { m["notes"].push_back(n); };
        Row* row = FindRow(o.value("place", ""));
        if (!row || row->scene.empty()) {
            m["status"] = "failed", m["error"] = o.value("place", "") + " isn't built";
            continue;
        }
        Registry reg;
        const std::string path = project.Resolve(row->scene).string();
        if (!LoadScene(reg, path)) {
            m["status"] = "failed", m["error"] = "can't read " + row->scene;
            continue;
        }
        const std::string name = o.value("name", oid), slug = WorldSlug(name);
        // Its entity: the one there by its name (a story prop Placements stood), else one stood where it's said to be.
        Entity e = entt::null;
        for (auto [ent, n, w] : reg.view<Name, WorldTransform2D>().each()) {
            if (e == entt::null && !reg.all_of<Marker>(ent) && !reg.all_of<Actor>(ent) && (WorldSlug(n.value) == slug || WorldSlug(n.value) == oid)) e = ent;
        }
        if (e == entt::null) {
            std::optional<std::pair<float, float>> at;
            const std::string where = WorldLower(o.value("where", ""));
            for (auto [ent, mk, w] : reg.view<Marker, WorldTransform2D>().each()) {
                if (!at && !mk.name.empty() && mk.name.find('/') == std::string::npos && where.find(WorldLower(mk.name)) != std::string::npos) at = std::make_pair(w.x, w.y);
            }
            const TileGrid grid = TileGrid::Build(reg);
            if (!at) {
                if (const auto mid = PlanOpenMiddle(reg, grid)) at = std::make_pair(mid->first + 0.5f, mid->second + 0.5f);
            }
            if (at) {
                if (const auto cell = NearestOpen(grid, static_cast<int>(at->first) + 1, static_cast<int>(at->second), 6)) at = std::make_pair(cell->first + 0.5f, cell->second + 0.5f);
            }
            const auto pos = at.value_or(std::make_pair(5.5f, 5.5f));
            const std::string art = "assets/textures/generated/" + oid + ".png";
            if (fs::exists(host_.projectRoot / art)) {
                e = reg.create();
                reg.emplace<WorldTransform2D>(e, pos.first, pos.second);
                auto& s = reg.emplace<Sprite>(e);
                s.texture = art, s.height = 72.0f, s.width = 72.0f;
            } else if (const auto made = prefabs.Instantiate(reg, "prefabs/placeholders/prop-sign.prefab", pos.first, pos.second)) {
                e = *made;
                reg.remove<PrefabInstance>(e);
                note("no picture of its own yet: a placeholder stands for it");
            } else {
                e = reg.create();
                reg.emplace<WorldTransform2D>(e, pos.first, pos.second);
                note("no picture yet");
            }
            reg.emplace_or_replace<Name>(e, name);
            note("stood " + (o.value("where", "").empty() ? std::string("in its place") : "at " + o.value("where", "")));
        }
        const auto& at = reg.get<WorldTransform2D>(e);
        const float ex = at.x, ey = at.y;
        // What characters do at it in their day (their packages find it by its type and what it affords).
        const json smart = o.value("smart", json::object());
        if (smart.is_object() && !smart.value("type", "").empty()) {
            SmartObject so;
            so.type = smart.value("type", "");
            for (const auto& a : smart.value("affordances", json::array())) {
                if (a.is_string()) so.affordances += (so.affordances.empty() ? "" : ",") + a.get<std::string>();
            }
            reg.emplace_or_replace<SmartObject>(e, so);
        }
        // Its Reactions: the first use's on it, each other's on a companion standing on it ("<name> · <n>"): an entity has one.
        std::vector<Entity> old;
        for (auto [ent, n] : reg.view<Name>().each()) {
            if (n.value.rfind(name + " · ", 0) == 0) old.push_back(ent);
        }
        for (Entity x : old) reg.destroy(x);
        int k = 0, reactions = 0;
        json becomesWaiting = json::array();
        for (const auto& use : o.value("uses", json::array())) {
            const json r = use.value("reaction", json::object());
            if (!r.is_object() || r.empty()) continue;
            ++k;
            Entity target = e;
            if (k > 1) {
                target = reg.create();
                reg.emplace<WorldTransform2D>(target, ex, ey);
                reg.emplace<Name>(target, name + " · " + std::to_string(k));
            }
            Action a;
            a.trigger = r.value("trigger", std::string("input"));
            a.action = r.value("action", std::string("interact"));
            a.range = r.value("range", 1.5f);
            a.prompt = r.value("prompt", "");
            a.requiresItem = r.value("requires", "");
            a.requiresFlag = r.value("requiresFlag", "");
            a.takes = r.value("takes", "");
            a.gives = r.value("gives", "");
            a.accepts = r.value("accepts", "");
            a.userAnimation = r.value("userClip", "");
            a.animation = r.value("clip", "");
            a.effect = r.value("effect", "");
            a.userEffect = r.value("userEffect", "");
            a.duration = r.value("seconds", 0.0f);
            a.once = r.value("once", false);
            a.cooldown = r.value("cooldown", 0.0f);
            a.emits = r.value("emits", "");
            a.setFlag = r.value("setFlag", "");
            a.text = r.value("text", "");
            a.conditions = JsonText(r.value("if", json::array()));
            a.doEffects = JsonText(r.value("do", json::array()));
            // What it turns into: its state's prefab (drawn, if it isn't yet: its Reaction gets it then).
            const std::string state = r.value("becomes", "");
            if (!state.empty()) {
                const std::string prefab = "prefabs/props/" + slug + "-" + WorldSlug(state) + ".prefab";
                if (prefabs.Find(prefab) || fs::exists(host_.projectRoot / prefab)) {
                    a.becomes = prefab;
                } else {
                    becomesWaiting.push_back({{"state", state}, {"entity", k > 1 ? name + " · " + std::to_string(k) : name}});
                }
            }
            reg.emplace_or_replace<Action>(target, a);
            ++reactions;
            // Who uses it: their animations, and a character's quest use as what Implement builds (a package to go and use it).
            for (const auto& who : use.value("who", json::array())) {
                if (!who.is_string()) continue;
                const std::string clip = r.value("userClip", "");
                const std::string person = store_.Read("characters/" + PlanSlug(who.get<std::string>())).value_or(json::object()).value("name", std::string());
                if (!clip.empty()) note((person.empty() ? who.get<std::string>() : person) + " needs a '" + clip + "' animation (Build ▸ Characters)");
                if (use.value("kind", "") == "quest" && reqs_.contains("items")) {
                    const std::string rid = "w-" + oid + "-" + std::to_string(k) + "-" + PlanSlug(who.get<std::string>());
                    bool there = false;
                    for (const auto& x : reqs_["items"]) there = there || x.value("id", "") == rid;
                    if (!there) {
                        reqs_["items"].push_back({{"id", rid}, {"scene", row->scene}, {"act", "world"}, {"kind", "movement"}, {"by", "world"}, {"status", "todo"},
                            {"text", (person.empty() ? who.get<std::string>() : person) + " goes to " + name + " and uses it (" + a.action + (clip.empty() ? "" : ", playing " + clip) +
                                     ") " + use.value("when", std::string()) + ": " + use.value("why", std::string())},
                            {"when", use.value("quest", json::object())}, {"where", {{"scene", row->scene}, {"target", name}}},
                            {"who", json::array({who})}, {"source", {{"object", oid}}}, {"accept", json::array()}});
                        note("its use by " + (person.empty() ? who.get<std::string>() : person) + " is a requirement for Implement");
                    }
                }
            }
        }
        if (reactions == 0 && !smart.is_object()) note("no use has a Reaction");
        // Its animations: the thing's own drawn from its picture (its Animator, the clip its Reactions play), its users'
        // added to their art; the names they're drawn under go into the Reactions (ReconcileClips).
        std::string player;
        for (const auto& c : store_.Read("outline").value_or(json::object()).value("cast", json::array())) {
            if (c.value("player", false)) player = c.value("id", "");
        }
        json anims = json::object();
        for (const auto& an : o.value("animations", json::array())) {
            const std::string cid = an.value("id", ""), who = an.value("who", std::string("object")), action = an.value("action", cid);
            if (cid.empty()) continue;
            json st = {{"who", who}, {"action", action}, {"status", "drawing"}};
            if (who == "object") {
                const Sprite* sprite = reg.try_get<Sprite>(e);
                const std::string texture = sprite ? sprite->texture : std::string();
                if (texture.empty() || texture.find("placeholders") != std::string::npos) {
                    st["status"] = "failed", st["error"] = "no picture of its own to animate yet";
                } else if (!host_.request) {
                    st["status"] = "failed", st["error"] = "the AI service isn't running";
                } else {
                    json body = {{"path", texture}, {"action", action}, {"thing", true}, {"description", name + ": " + o.value("look", "")},
                                 {"name", slug + "-" + WorldSlug(cid)}};
                    if (host_.animateModel) body["provider"] = host_.animateModel();
                    if (host_.objectAnimateModel) body["objectProvider"] = host_.objectAnimateModel();
                    if (const int request = host_.request("/animations/generate", body, 900); request != 0) pending_[request] = "obj:anim/" + oid + "/" + cid;
                    else st["status"] = "failed", st["error"] = "the AI service isn't running";
                }
            } else {
                const std::string subject = who == "player" ? player : who;
                st["subject"] = subject;
                if (subject.empty() || !QueueCharacterClip(subject, action)) st["status"] = "failed", st["error"] = "nothing of " + who + " is drawn to animate";
            }
            anims[cid] = st;
        }
        m["animations"] = anims;
        // A prefab of its own (its states are copies of it with another picture).
        if (!reg.all_of<PrefabInstance>(e)) {
            std::string why;
            if (!prefabs.SaveAsPrefab(reg, e, "prefabs/props/" + slug + ".prefab", &why)) note("not saved as a prefab: " + why);
        }
        SaveScene(reg, path);
        m["entity"] = name;
        m["scene"] = row->scene;
        m["reactions"] = reactions;
        // Each state still to be drawn: asked for; its Reactions get it when it comes back.
        m["waiting"] = becomesWaiting;
        for (const auto& st : o.value("states", json::array())) {
            const std::string sid = st.value("id", "");
            bool wanted = false;
            for (const auto& b : becomesWaiting) wanted = wanted || b.value("state", "") == sid;
            if (!wanted || !host_.request) continue;
            const json body = {{"name", slug + "-" + WorldSlug(sid)}, {"description", name + ", " + st.value("look", sid) + ". The same object, the same view and size."},
                               {"kind", "object"}, {"height", 72}};
            if (const int request = host_.request("/assets/generate", body, 600); request != 0) {
                pending_[request] = "obj:" + oid + ":" + sid;
                note("drawing its state '" + sid + "'");
            }
        }
        m["status"] = becomesWaiting.empty() ? "built" : "drawing";
        ++made;
    }
    SaveWorldState(world);
    SaveRequirements();
    Record("world", "objects", std::to_string(made) + " made", which);
    return made == 0 ? "no object " + which : std::string();
}

void GameBuild::ApplyObjectState(const std::string& key, const json& body) {
    if (key.rfind("anim/", 0) == 0) {
        const std::string rest = key.substr(5);
        ApplyObjectAnimation(rest.substr(0, rest.find('/')), rest.substr(rest.find('/') + 1), body);
        return;
    }
    // A state's picture came back: a prefab of it (the object's, another picture), its Reactions turn into it.
    const std::string oid = key.substr(0, key.find(':')), sid = key.substr(key.find(':') + 1);
    json world = WorldState();
    json& m = world["objects"][oid];
    const json objects = store_.Read("objects").value_or(json::object()).value("objects", json::array());
    json o;
    for (const auto& x : objects) {
        if (x.value("id", "") == oid) o = x;
    }
    const std::string made = body.value("ok", false) && !body.value("assets", json::array()).empty() ? body["assets"][0].value("path", "") : std::string();
    if (o.is_null() || made.empty()) {
        m["notes"].push_back("its state '" + sid + "' wasn't drawn: " + body.value("errors", json::array()).dump());
        m["status"] = "failed";
        SaveWorldState(world);
        return;
    }
    const Project project{host_.projectRoot};
    PrefabLibrary prefabs(project);
    const std::string name = o.value("name", oid), slug = WorldSlug(name);
    const std::string prefab = "prefabs/props/" + slug + "-" + WorldSlug(sid) + ".prefab";
    Registry reg;
    const std::string path = project.Resolve(m.value("scene", "")).string();
    if (!LoadScene(reg, path)) return;
    {
        Registry temp;
        const Entity e = temp.create();
        temp.emplace<Name>(e, name + " (" + sid + ")");
        temp.emplace<WorldTransform2D>(e, 0.0f, 0.0f);
        auto& s = temp.emplace<Sprite>(e);
        s.texture = made, s.height = 72.0f, s.width = 72.0f;
        std::string why;
        if (!prefabs.SaveAsPrefab(temp, e, prefab, &why)) {
            m["notes"].push_back("its state '" + sid + "' isn't a prefab: " + why);
            SaveWorldState(world);
            return;
        }
    }
    json still = json::array();
    for (const auto& w : m.value("waiting", json::array())) {
        if (w.value("state", "") != sid) {
            still.push_back(w);
            continue;
        }
        for (auto [ent, n, a] : reg.view<Name, Action>().each()) {
            if (n.value == w.value("entity", "")) a.becomes = prefab;
        }
    }
    SaveScene(reg, path);
    m["waiting"] = still;
    m["notes"].push_back("its state '" + sid + "' drawn: " + prefab);
    if (still.empty()) m["status"] = "built";
    SaveWorldState(world);
}

bool GameBuild::QueueCharacterClip(const std::string& subject, const std::string& action) {
    auto entry = [&]() -> json* {
        for (auto& x : art_) {
            if (x.value("subject", "") == subject) return &x;
        }
        return nullptr;
    };
    if (!entry() && subject.rfind("character/", 0) == 0) QueueArt(true);
    json* a = entry();
    if (!a) return false;
    json& actions = (*a)["actions"];
    if (!actions.is_array()) actions = json::array();
    const bool has = std::any_of(actions.begin(), actions.end(), [&](const json& x) { return x.value("action", "") == action; });
    if (!has) {
        actions.push_back({{"action", action}});
        if ((*a).value("status", "") == "done" || ((*a).value("status", "") == "failed" && !(*a).value("sheet", "").empty())) (*a)["status"] = "queued";
    } else if ((*a).value("status", "") == "failed") {
        (*a)["status"] = "queued", (*a)["error"] = "";
    }
    // Drawn now (as Draw the others does): the art step runs once the build is past placing everyone.
    artApproved_ = true;
    if (status_ != "working" && !stage_.empty() && stage_ != "build" && stage_ != "requirements") status_ = "working";
    return true;
}

void GameBuild::ApplyObjectAnimation(const std::string& oid, const std::string& clip, const json& body) {
    // Its frames as a sheet beside them, its one clip named as its Reactions play it; on the thing in its scene.
    json world = WorldState();
    json& st = world["objects"][oid]["animations"][clip];
    const Project project{host_.projectRoot};
    const std::string texture = body.value("sheet", "");
    const int frames = static_cast<int>(body.value("frames", json::array()).size());
    const auto [w, h] = PngSize(project.root / texture);
    if (!body.value("ok", false) || texture.empty() || frames <= 0 || w <= 0) {
        st["status"] = "failed", st["error"] = body.contains("errors") ? body["errors"].dump() : "no frames came back";
        SaveWorldState(world);
        return;
    }
    json o;
    for (const auto& x : store_.Read("objects").value_or(json::object()).value("objects", json::array())) {
        if (x.value("id", "") == oid) o = x;
    }
    bool loops = false;
    for (const auto& an : o.value("animations", json::array())) {
        if (an.value("id", "") == clip) loops = an.value("loop", false);
    }
    json sheet = AnimSheetFromGrid(texture, w / frames, h, frames, {clip}, body.value("fps", 8.0f));
    if (!loops && sheet["clips"].contains(clip)) sheet["clips"][clip]["loop"] = "once";
    const fs::path anim = fs::path(texture).parent_path() / (fs::path(texture).stem().string() + ".anim.json");
    std::ofstream(project.Resolve(anim.generic_string()), std::ios::binary) << sheet.dump(2) << "\n";
    files_[anim.generic_string()] = {{"planner", "objects"}};
    Registry reg;
    const std::string scene = world["objects"][oid].value("scene", "");
    const std::string path = project.Resolve(scene).string();
    const std::string name = o.value("name", oid);
    if (!scene.empty() && LoadScene(reg, path)) {
        for (auto [e, n] : reg.view<Name>().each()) {
            if (reg.all_of<Marker>(e) || WorldSlug(n.value) != WorldSlug(name)) continue;
            auto& animator = reg.get_or_emplace<Animator>(e);
            if (!animator.sheet.empty() && animator.sheet != anim.generic_string()) world["objects"][oid]["notes"].push_back("its clip " + clip + " replaced the one it had (one sheet a thing)");
            animator.sheet = anim.generic_string();
            animator.animator = "prop";
        }
        SaveScene(reg, path);
    }
    st["status"] = "built", st["sheet"] = anim.generic_string(), st["clip"] = clip;
    st.erase("error");
    SaveWorldState(world);
}

void GameBuild::ReconcileClips() {
    if (!fs::exists(store_.Dir() / "world-build.json")) return;
    json world = WorldState();
    const Project project{host_.projectRoot};
    // The clip each subject's action was drawn under, or why not.
    auto drawn = [&](const std::string& subject, const std::string& action, std::string& why) -> std::string {
        for (const auto& a : art_) {
            if (a.value("subject", "") != subject) continue;
            for (const auto& x : a.value("actions", json::array())) {
                if (x.value("action", "") == action && !x.value("clip", "").empty()) return x.value("clip", "");
            }
            if (a.value("status", "") == "failed") why = a.value("error", std::string("its art failed"));
        }
        return {};
    };
    // (who, the design's clip) -> the drawn one: for the small scenes' cutscenes, whose "use" beats play the things' clips.
    std::map<std::pair<std::string, std::string>, std::string> renamed;
    for (auto& [oid, m] : world["objects"].items()) {
        if (!m.contains("animations")) continue;
        std::map<std::string, std::string> users; // the design's clip -> the drawn one, for its Reactions
        for (auto& [cid, st] : m["animations"].items()) {
            if (st.value("who", "") == "object" || st.value("status", "") == "built") {
                if (st.value("status", "") == "built" && st.value("who", "") != "object") users[cid] = st.value("clip", cid);
                continue;
            }
            std::string why;
            const std::string clip = drawn(st.value("subject", ""), st.value("action", ""), why);
            if (!clip.empty()) st["status"] = "built", st["clip"] = clip, users[cid] = clip;
            else if (!why.empty()) st["status"] = "failed", st["error"] = why;
            if (!clip.empty()) renamed[{st.value("subject", ""), cid}] = clip;
        }
        if (users.empty() || m.value("scene", "").empty()) continue;
        Registry reg;
        const std::string path = project.Resolve(m.value("scene", "")).string();
        if (!LoadScene(reg, path)) continue;
        bool changed = false;
        const std::string name = m.value("entity", oid);
        for (auto [e, n, a] : reg.view<Name, Action>().each()) {
            if (n.value != name && n.value.rfind(name + " · ", 0) != 0) continue;
            if (const auto it = users.find(a.userAnimation); it != users.end() && it->second != a.userAnimation) a.userAnimation = it->second, changed = true;
        }
        if (changed) SaveScene(reg, path);
    }
    for (auto& [sid, m] : world["scenes"].items()) {
        if (!m.contains("animations") || !m["animations"].is_object()) m["animations"] = json::object();
        for (auto& [cid, st] : m["animations"].items()) {
            if (st.value("status", "") == "built") {
                renamed[{st.value("subject", ""), cid}] = st.value("clip", cid);
                continue;
            }
            std::string why;
            const std::string clip = drawn(st.value("subject", ""), st.value("action", ""), why);
            if (!clip.empty()) st["status"] = "built", st["clip"] = clip, renamed[{st.value("subject", ""), cid}] = clip;
            else if (!why.empty()) st["status"] = "failed", st["error"] = why;
        }
        // Its cutscene's animate keys: the drawn names.
        const std::string file = m.value("cutscene", "");
        if (file.empty()) continue;
        std::ifstream in(project.Resolve(file), std::ios::binary);
        json cut = in ? json::parse(in, nullptr, false) : json();
        in.close();
        if (!cut.is_object()) continue;
        bool changed = false;
        for (auto& t : cut["tracks"]) {
            if (t.value("type", "") != "actor") continue;
            for (auto& k : t["keys"]) {
                if (k.value("op", "") != "animate") continue;
                const auto it = renamed.find({t.value("actor", ""), k.value("clip", "")});
                if (it != renamed.end() && it->second != k.value("clip", "")) k["clip"] = it->second, changed = true;
            }
        }
        if (changed) std::ofstream(project.Resolve(file), std::ios::binary) << cut.dump(1);
    }
    SaveWorldState(world);
}

std::string GameBuild::BuildSmallScenes(const std::string& which) {
    const json scenes = store_.Read("smallscenes").value_or(json::object()).value("scenes", json::array());
    if (!scenes.is_array() || scenes.empty()) return "Life ▸ Small scenes hasn't written them yet";
    const json objects = store_.Read("objects").value_or(json::object()).value("objects", json::array());
    auto objectName = [&](const std::string& id) {
        for (const auto& o : objects) {
            if (o.value("id", "") == id) return o.value("name", id);
        }
        return id;
    };
    auto objectClip = [&](const std::string& id) {
        for (const auto& o : objects) {
            if (o.value("id", "") != id) continue;
            for (const auto& u : o.value("uses", json::array())) {
                const std::string c = u.value("reaction", json::object()).value("userClip", "");
                if (!c.empty()) return c;
            }
        }
        return std::string();
    };
    const Project project{host_.projectRoot};
    json world = WorldState();
    int made = 0;
    for (const auto& sc : scenes) {
        const std::string sid = sc.value("id", "");
        if (sid.empty() || (which != "all" && which != sid)) continue;
        json& m = world["scenes"][sid];
        m = {{"status", "building"}, {"notes", json::array()}};
        const json area = sc.value("area", json::object());
        Row* row = FindRow(!area.value("room", "").empty() ? area.value("room", "") : sc.value("place", ""));
        if (!row || row->scene.empty()) {
            m["status"] = "failed", m["error"] = sc.value("place", "") + " isn't built";
            continue;
        }
        Registry reg;
        const std::string path = project.Resolve(row->scene).string();
        if (!LoadScene(reg, path)) {
            m["status"] = "failed", m["error"] = "can't read " + row->scene;
            continue;
        }
        auto markerThere = [&](const std::string& name) -> std::optional<std::pair<float, float>> {
            for (auto [ent, mk, w] : reg.view<Marker, WorldTransform2D>().each()) {
                if (mk.name == name) return std::make_pair(w.x, w.y);
            }
            return std::nullopt;
        };
        // Its beats as tracks: a line each on the dialogue track, the rest on its person's actor track.
        json lines = json::array();
        std::map<std::string, json> actors;
        float t = 0.5f;
        for (const auto& b : sc.value("beats", json::array())) {
            const std::string who = b.value("who", ""), what = b.value("do", "");
            const std::string to = b.value("to", "");
            json target;
            if (!to.empty()) {
                if (markerThere(to)) target = {{"marker", to}};
                else if (to == "player" || to.rfind("character/", 0) == 0) target = {{"target", to}};
                else target = {{"target", objectName(to)}};
            }
            json& keys = actors[who];
            if (!keys.is_array()) keys = json::array();
            if (what == "say") {
                const std::string line = b.value("line", "");
                const float seconds = std::max(2.5f, static_cast<float>(line.size()) / 14.0f);
                lines.push_back({{"at", t}, {"op", "line"}, {"speaker", who}, {"text", line}, {"seconds", seconds}});
                t += seconds + 0.3f;
            } else if (what == "walk") {
                json k = {{"at", t}, {"op", "move"}};
                k.update(target);
                keys.push_back(k);
                t += b.value("seconds", 3.0f);
            } else if (what == "face") {
                json k = {{"at", t}, {"op", "face"}};
                k.update(target);
                keys.push_back(k);
                t += 0.5f;
            } else if (what == "use") {
                const std::string object = b.value("object", "");
                keys.push_back({{"at", t}, {"op", "move"}, {"target", objectName(object)}});
                const std::string clip = !b.value("clip", "").empty() ? b.value("clip", "") : objectClip(object);
                if (!clip.empty()) keys.push_back({{"at", t + 2.0f}, {"op", "animate"}, {"clip", clip}});
                t += std::max(3.0f, b.value("seconds", 4.0f));
            } else if (what == "animate" || what == "emote") {
                const std::string clip = b.value("clip", "");
                if (!clip.empty()) keys.push_back({{"at", t}, {"op", "animate"}, {"clip", clip}});
                t += b.value("seconds", 2.0f);
            } else {
                t += b.value("seconds", 2.0f);
            }
        }
        json tracks = json::array();
        if (!lines.empty()) tracks.push_back({{"type", "dialogue"}, {"keys", lines}});
        for (auto& [who, keys] : actors) {
            if (!keys.empty()) tracks.push_back({{"type", "actor"}, {"actor", who}, {"keys", keys}});
        }
        const std::string cutscene = "small-" + sid;
        const json file = {{"name", sc.value("name", sid)}, {"ambient", true}, {"world", "runs"}, {"letterbox", false}, {"skippable", true},
                           {"length", t + 0.5f}, {"tracks", tracks}};
        std::error_code ec;
        fs::create_directories(host_.projectRoot / "cutscenes", ec);
        std::ofstream(host_.projectRoot / "cutscenes" / (cutscene + ".json"), std::ios::binary) << file.dump(1);
        files_["cutscenes/" + cutscene + ".json"] = {{"planner", "smallscenes"}};
        // Its trigger: where the player has to be (its anchor, the thing it's near, the room's way in), for each quest stage
        // it plays in (the conditions hold one stage each), at its time of day, as often as it says.
        std::optional<std::pair<float, float>> at;
        float radius = area.value("radius", 6.0f);
        if (!area.value("room", "").empty()) {
            at = markerThere("arrive");
            for (auto [ent, mk, w] : reg.view<Marker, WorldTransform2D>().each()) {
                if (!at && mk.name.rfind("from-", 0) == 0) at = std::make_pair(w.x, w.y);
            }
            radius = 3.0f;
        }
        if (!at && !area.value("anchor", "").empty()) at = markerThere(area.value("anchor", ""));
        if (!at && !area.value("near", "").empty()) {
            const std::string thing = objectName(area.value("near", ""));
            for (auto [ent, n, w] : reg.view<Name, WorldTransform2D>().each()) {
                if (!at && !reg.all_of<Marker>(ent) && WorldSlug(n.value) == WorldSlug(thing)) at = std::make_pair(w.x, w.y);
            }
        }
        if (!at) {
            m["status"] = "failed", m["error"] = "nowhere to start it: no " + area.value("anchor", area.value("near", area.value("room", std::string("place"))));
            continue;
        }
        std::vector<Entity> old;
        for (auto [ent, n] : reg.view<Name>().each()) {
            if (n.value.rfind("Small scene: " + sc.value("name", sid), 0) == 0) old.push_back(ent);
        }
        for (Entity x : old) reg.destroy(x);
        json time = json();
        static const std::map<std::string, std::pair<const char*, const char*>> hours = {
            {"morning", {"06:00", "11:00"}}, {"day", {"11:00", "17:00"}}, {"evening", {"17:00", "21:00"}}, {"night", {"21:00", "06:00"}}};
        if (const auto h = hours.find(sc.value("time", "")); h != hours.end()) time = {{"time", {{"from", h->second.first}, {"to", h->second.second}}}};
        std::vector<json> conditionSets;
        for (const auto& q : sc.value("quests", json::array())) {
            for (const auto& stage : q.value("stages", json::array())) {
                json c = json::array({{{"quest", q.value("quest", "")}, {"is", "active"}, {"stage", stage}}});
                if (!time.is_null()) c.push_back(time);
                conditionSets.push_back(c);
            }
        }
        if (conditionSets.empty()) conditionSets.push_back(time.is_null() ? json::array() : json::array({time}));
        int n = 0;
        for (const auto& conditions : conditionSets) {
            const Entity trig = reg.create();
            reg.emplace<WorldTransform2D>(trig, at->first, at->second);
            reg.emplace<Name>(trig, "Small scene: " + sc.value("name", sid) + (conditionSets.size() > 1 ? " (" + std::to_string(++n) + ")" : ""));
            Action a;
            a.trigger = "player-near";
            a.range = radius;
            a.once = sc.value("repeat", "") == "once";
            a.cooldown = sc.value("repeat", "") == "cooldown" ? sc.value("cooldown", 120.0f) : 0.0f;
            a.conditions = JsonText(conditions);
            a.doEffects = json::array({{{"op", "play_cutscene"}, {"cutscene", cutscene}}}).dump();
            reg.emplace<Action>(trig, a);
        }
        SaveScene(reg, path);
        // The clips its people play: added to their art; the names they're drawn under go into its cutscene.
        json anims = json::object();
        for (const auto& an : sc.value("animations", json::array())) {
            const std::string cid = an.value("id", ""), who = an.value("who", ""), action = an.value("action", cid);
            if (cid.empty() || who.empty()) continue;
            json st = {{"who", who}, {"subject", who}, {"action", action}, {"status", "drawing"}};
            if (!QueueCharacterClip(who, action)) st["status"] = "failed", st["error"] = "nothing of " + who + " is drawn to animate";
            anims[cid] = st;
        }
        m["animations"] = anims;
        m["status"] = "built";
        m["cutscene"] = "cutscenes/" + cutscene + ".json";
        m["trigger"] = std::to_string(conditionSets.size()) + " in " + row->scene + " (within " + std::to_string(static_cast<int>(radius)) + " of it)";
        m["scene"] = row->scene;
        ++made;
    }
    SaveWorldState(world);
    Record("world", "small scenes", std::to_string(made) + " made", which);
    return made == 0 ? "no small scene " + which : std::string();
}

// --- Build ▸ Visuals ------------------------------------------------------------------------------------------------

json GameBuild::VisualsState() const {
    std::ifstream in(store_.Dir() / "visuals-build.json", std::ios::binary);
    const json j = in ? json::parse(in, nullptr, false) : json();
    return j.is_object() ? j : json{{"panels", json::object()}};
}

void GameBuild::SaveVisualsState(const json& state) const {
    std::ofstream(store_.Dir() / "visuals-build.json", std::ios::binary) << state.dump(1);
}

std::string GameBuild::BuildVisuals(const std::string& panel) {
    const json ui = store_.Read("ui").value_or(json::object());
    const json panels = ui.value("panels", json::array());
    if (!panels.is_array() || panels.empty()) return "⑧ Visuals hasn't designed the panels yet (Redesign Visuals)";
    if (!host_.request) return "the AI service isn't running";
    json state = VisualsState();
    int started = 0;
    for (const auto& p : panels) {
        const std::string id = p.value("id", "");
        if (id.empty() || (panel != "all" && panel != id)) continue;
        json& s = state["panels"][id];
        s = {{"status", "images"}, {"images", json::object()}, {"at", static_cast<long long>(std::time(nullptr))}};
        ++started;
    }
    if (started == 0) return "no panel " + panel;
    SaveVisualsState(state);
    for (const auto& p : panels) {
        const std::string id = p.value("id", "");
        if (!id.empty() && (panel == "all" || panel == id)) NextVisualStep(id);
    }
    Record("visuals", panel, "building", std::to_string(started) + " panel(s)");
    return {};
}

void GameBuild::NextVisualStep(const std::string& id) {
    // Its next image not made yet (one at a time), else its document; built once that's written.
    const json ui = store_.Read("ui").value_or(json::object());
    json design;
    for (const auto& p : ui.value("panels", json::array())) {
        if (p.value("id", "") == id) design = p;
    }
    json state = VisualsState();
    json& s = state["panels"][id];
    if (design.is_null()) {
        s["status"] = "failed", s["error"] = "⑧ Visuals has no panel " + id;
        SaveVisualsState(state);
        return;
    }
    const json style = ui.value("style", json::object());
    const json brief = store_.Read("brief").value_or(json::object());
    const json artStyle = BriefGet(brief, "artStyle.style");
    for (const auto& im : design.value("images", json::array())) {
        const std::string img = im.value("id", "");
        if (img.empty() || s["images"].contains(img)) continue;
        const std::string file = "ui/art/" + id + "-" + img + ".png";
        if (fs::exists(host_.projectRoot / file) && s.value("reuse", true)) {
            s["images"][img] = file; // made before: kept (Build again makes it anew when its design changed)
            continue;
        }
        const json body = {{"name", "ui-" + id + "-" + img},
                           {"description", im.value("prompt", "") + ". For a game interface (" + style.value("materials", std::string()) + "); " +
                                               (im.value("nine", false) ? "a frame whose edges and corners can be stretched in nine pieces; " : "") +
                                               "straight on, flat, no text, no letters."},
                           {"kind", "ui"}, {"height", std::clamp(im.value("height", 256), 32, 1024)},
                           {"style", artStyle.is_string() ? artStyle.get<std::string>() : std::string("painted")}};
        const int request = host_.request("/assets/generate", body, 600);
        if (request == 0) {
            s["status"] = "failed", s["error"] = "the AI service isn't running";
        } else {
            pending_[request] = "vis:img:" + id + ":" + img;
            s["status"] = "images", s["doing"] = "drawing " + img;
        }
        SaveVisualsState(state);
        return;
    }
    // Its document: written by the AI from the design, its images and the engine's document for the screen.
    std::string reference;
    {
        std::string doc = "screens/window.rml";
        for (const fs::path manifest : {host_.projectRoot / "ui" / "screens.json", EngineUiDir() / "screens.json"}) {
            std::ifstream in(manifest, std::ios::binary);
            const json m = in ? json::parse(in, nullptr, false) : json();
            bool found = false;
            for (const auto& e : m.is_object() ? m.value("screens", json::array()) : json::array()) {
                if (e.value("id", "") == id && !found) doc = e.value("document", doc), found = true;
            }
            if (found) break;
        }
        if (id == "hud") doc = "screens/hud.rml";
        for (const fs::path base : {EngineUiDir(), host_.projectRoot / "ui"}) {
            std::ifstream in(base / doc, std::ios::binary);
            if (in && reference.empty()) {
                std::ostringstream read;
                read << in.rdbuf();
                reference = read.str();
            }
        }
    }
    json images = json::array();
    for (const auto& im : design.value("images", json::array())) {
        const std::string img = im.value("id", "");
        if (s["images"].contains(img)) {
            // Its size as drawn (a frame's nine pieces are cut in its own pixels), else as designed.
            int w = im.value("width", 0), h = im.value("height", 0);
            {
                std::ifstream png(host_.projectRoot / s["images"][img].get<std::string>(), std::ios::binary);
                unsigned char head[24] = {};
                if (png.read(reinterpret_cast<char*>(head), 24) && head[1] == 'P' && head[2] == 'N' && head[3] == 'G') {
                    w = (head[16] << 24) | (head[17] << 16) | (head[18] << 8) | head[19];
                    h = (head[20] << 24) | (head[21] << 16) | (head[22] << 8) | head[23];
                }
            }
            images.push_back({{"id", img}, {"use", im.value("use", "")}, {"file", "../art/" + fs::path(s["images"][img].get<std::string>()).filename().string()},
                              {"width", w}, {"height", h}, {"nine", im.value("nine", false)}});
        }
    }
    std::ifstream tin(host_.projectRoot / "ui" / "theme.json", std::ios::binary);
    const json theme = tin ? json::parse(tin, nullptr, false) : json();
    const json body = {{"panel", design}, {"style", style}, {"images", images}, {"reference", reference}, {"screen", id},
                       {"theme", theme.is_object() ? theme : json::object()}, {"font", ui.value("font", "")}};
    const int request = host_.request("/planner/uipanel", body, 900);
    if (request == 0) {
        s["status"] = "failed", s["error"] = "the AI service isn't running";
    } else {
        pending_[request] = "vis:rml:" + id;
        s["status"] = "writing", s["doing"] = "writing its document";
    }
    SaveVisualsState(state);
}

void GameBuild::ApplyVisualAnswer(const std::string& key, const json& body) {
    // "img:<panel>:<image>" (its picture, copied into ui/art/) or "rml:<panel>" (its document).
    const bool image = key.rfind("img:", 0) == 0;
    const std::string rest = key.substr(4);
    const std::string id = image ? rest.substr(0, rest.find(':')) : rest;
    const std::string img = image && rest.find(':') != std::string::npos ? rest.substr(rest.find(':') + 1) : std::string();
    json state = VisualsState();
    json& s = state["panels"][id];
    if (!body.value("ok", false)) {
        s["status"] = "failed";
        s["error"] = (image ? "drawing " + img + ": " : std::string("writing its document: ")) + body.value("errors", json::array()).dump();
        SaveVisualsState(state);
        return;
    }
    if (image) {
        const json assets = body.value("assets", json::array());
        const std::string made = assets.empty() ? std::string() : assets[0].value("path", "");
        std::error_code ec;
        fs::create_directories(host_.projectRoot / "ui" / "art", ec);
        const std::string file = "ui/art/" + id + "-" + img + ".png";
        if (!made.empty()) fs::copy_file(host_.projectRoot / made, host_.projectRoot / file, fs::copy_options::overwrite_existing, ec);
        if (made.empty() || ec) {
            s["status"] = "failed", s["error"] = "drawing " + img + ": no picture came back";
            SaveVisualsState(state);
            return;
        }
        s["images"][img] = file;
        files_[file] = {{"planner", "ui"}};
        SaveVisualsState(state);
        NextVisualStep(id);
        return;
    }
    // Its design (laid out for the UI designer: Project ▸ UI ▸ Designer edits it), the document written from it; an
    // answer from before designs, its document as written.
    std::string rml = body.value("rml", "");
    std::error_code ec;
    if (body.contains("design") && body["design"].is_object()) {
        json design = body["design"];
        design["id"] = id;
        if (const auto problems = CheckDesign(design); !problems.empty()) {
            s["status"] = "failed", s["error"] = "its design: " + problems.front();
            SaveVisualsState(state);
            return;
        }
        fs::create_directories(host_.projectRoot / "ui" / "designs", ec);
        const std::string designFile = "ui/designs/" + id + ".json";
        std::ofstream(host_.projectRoot / designFile, std::ios::binary) << design.dump(1);
        files_[designFile] = {{"planner", "ui"}};
        rml = DesignToRml(design);
    }
    fs::create_directories(host_.projectRoot / "ui" / "screens", ec);
    const std::string file = "ui/screens/" + id + ".rml";
    std::ofstream(host_.projectRoot / file, std::ios::binary) << rml;
    files_[file] = {{"planner", "ui"}};
    // In the project's screens manifest: this screen shows its own document (the layer and pausing as designed).
    std::ifstream in(host_.projectRoot / "ui" / "screens.json", std::ios::binary);
    json manifest = in ? json::parse(in, nullptr, false) : json();
    in.close();
    if (!manifest.is_object()) manifest = json::object();
    if (!manifest["screens"].is_array()) manifest["screens"] = json::array();
    const json ui = store_.Read("ui").value_or(json::object());
    std::string layer = "window";
    for (const auto& p : ui.value("panels", json::array())) {
        if (p.value("id", "") == id) layer = p.value("layer", layer);
    }
    bool there = false;
    for (auto& e : manifest["screens"]) {
        if (e.value("id", "") == id) e["document"] = "screens/" + id + ".rml", e["layer"] = layer, there = true;
    }
    if (!there) manifest["screens"].push_back({{"id", id}, {"document", "screens/" + id + ".rml"}, {"layer", layer}, {"pauses", layer == "window" || layer == "screen"}});
    std::ofstream(host_.projectRoot / "ui" / "screens.json", std::ios::binary) << manifest.dump(1);
    s["status"] = "built";
    s["file"] = file;
    s["notes"] = body.value("notes", "");
    s.erase("doing");
    s.erase("error");
    SaveVisualsState(state);
    Record("visuals", id, "built", file);
}

// --- A Build tab's revision -------------------------------------------------------------------------------------------

json GameBuild::TabDocuments(const std::string& tab) const {
    // Each entry of the tab, a document of its own (only those the words are about are sent whole to be edited).
    json docs = json::object();
    const json p = placements_.contains("characters") ? placements_ : ReadPlacements(host_.projectRoot);
    if (tab == "placements") {
        for (const char* list : {"characters", "props"}) {
            for (const auto& e : p.value(list, json::array())) {
                if (e.is_object() && !e.value("id", "").empty()) docs[std::string("placements/") + list + "/" + PlanSlug(e.value("id", ""))] = e;
            }
        }
    } else if (tab == "requirements" || tab == "cast") {
        for (const auto& r : reqs_.value("items", json::array())) {
            const bool cast = r.value("kind", "") == "character-state";
            if (r.is_object() && (tab == "cast") == cast) docs["requirements/" + r.value("id", "")] = r;
        }
    } else if (tab == "gameplay") {
        for (const auto& r : reqs_.value("implementations", json::array())) {
            if (r.is_object()) docs["implementations/" + r.value("id", "")] = r;
        }
    } else if (tab == "quests") {
        std::error_code ec;
        for (const auto& f : fs::directory_iterator(host_.projectRoot / "quests", ec)) {
            if (f.path().extension() != ".json") continue;
            std::ifstream in(f.path(), std::ios::binary);
            const json q = in ? json::parse(in, nullptr, false) : json();
            if (q.is_object()) docs["quests/" + f.path().stem().string()] = q;
        }
    }
    return docs;
}

std::string GameBuild::RevertTabRevision(const std::string& tab, const std::string& id) {
    std::ifstream in(store_.Dir() / "build-revisions.json", std::ios::binary);
    json history = in ? json::parse(in, nullptr, false) : json();
    in.close();
    if (!history.is_object() || !history[tab].is_array()) return "no revision " + id;
    json entry;
    for (const auto& e : history[tab]) {
        if (e.value("id", "") == id) entry = e;
    }
    if (entry.is_null()) return "no revision " + id;
    if (entry.value("status", "") == "reverted") return "it's reverted already";
    const json undo = entry.value("undo", json::array());
    if (undo.empty()) return "what it changed wasn't recorded (a revision from before Revert): it can't be taken back";
    // The inverse, newest first: each value as it was; an item it added to a list, the list without it.
    json docs = TabDocuments(tab);
    json inverse = json::array();
    for (auto it = undo.rbegin(); it != undo.rend(); ++it) {
        const std::string doc = it->value("doc", ""), pointer = it->value("pointer", "");
        if (pointer.size() >= 2 && pointer.compare(pointer.size() - 2, 2, "/-") == 0) {
            const std::string listAt = pointer.substr(0, pointer.size() - 2);
            if (!docs.contains(doc) || !docs[doc].contains(json::json_pointer(listAt))) continue;
            json list = docs[doc].at(json::json_pointer(listAt));
            for (size_t i = list.is_array() ? list.size() : 0; i-- > 0;) {
                if (list[i] == it->value("value", json())) {
                    list.erase(i);
                    break;
                }
            }
            inverse.push_back({{"doc", doc}, {"pointer", listAt}, {"value", list}});
        } else if (it->contains("previous")) {
            inverse.push_back({{"doc", doc}, {"pointer", pointer}, {"value", (*it)["previous"]}});
        }
    }
    if (inverse.empty()) return "nothing of it can be taken back";
    // Applied as a revision of its own ("Revert ..."), the tab's way; the original marked reverted.
    const std::string rid = "r" + std::to_string(static_cast<long long>(std::time(nullptr))) + "-revert";
    for (auto& e : history[tab]) {
        if (e.value("id", "") == id) e["status"] = "reverted";
    }
    history[tab].push_back({{"id", rid}, {"at", static_cast<long long>(std::time(nullptr))}, {"text", "Revert: " + entry.value("text", std::string())},
                            {"status", "running"}});
    std::ofstream(store_.Dir() / "build-revisions.json", std::ios::binary) << history.dump(1);
    ApplyTabRevision(tab + ":" + rid, {{"ok", true}, {"edits", inverse}, {"rationale", "taken back"}});
    Save();
    return {};
}

std::string GameBuild::ReviseTab(const std::string& tab, const std::string& text) {
    if (text.empty()) return "say what to change";
    const json docs = TabDocuments(tab);
    if (docs.empty()) return "the " + tab + " tab has nothing to change yet";
    if (!host_.request) return "the AI service isn't running";
    json index = json::array();
    for (const auto& [doc, d] : docs.items()) {
        json fields = json::array();
        for (const auto& [k, v] : d.items()) fields.push_back(k);
        index.push_back({{"doc", doc}, {"name", d.value("name", d.value("text", std::string()))}, {"fields", fields}});
    }
    const std::string id = "r" + std::to_string(static_cast<long long>(std::time(nullptr)));
    const json body = {{"instruction", text}, {"scope", "build:" + tab}, {"documents", docs}, {"index", index},
                       {"brief", store_.Read("brief").value_or(json::object())}};
    const int request = host_.request("/planner/revise", body, 600);
    if (request == 0) return "the AI service isn't running";
    pending_[request] = "rev:" + tab + ":" + id;
    std::ifstream in(store_.Dir() / "build-revisions.json", std::ios::binary);
    json history = in ? json::parse(in, nullptr, false) : json();
    if (!history.is_object()) history = json::object();
    if (!history[tab].is_array()) history[tab] = json::array();
    history[tab].push_back({{"id", id}, {"at", static_cast<long long>(std::time(nullptr))}, {"text", text}, {"status", "running"}});
    in.close();
    std::ofstream(store_.Dir() / "build-revisions.json", std::ios::binary) << history.dump(1);
    Record("revise", tab, "asked", text);
    return {};
}

void GameBuild::ApplyTabRevision(const std::string& key, const json& body) {
    const size_t colon = key.find(':');
    const std::string tab = key.substr(0, colon), id = colon == std::string::npos ? std::string() : key.substr(colon + 1);
    json docs = TabDocuments(tab);
    std::vector<std::string> problems;
    std::set<std::string> changed;
    json undo = json::array(); // each edit as it was made, with what it replaced
    if (!body.value("ok", false)) problems.push_back(body.value("errors", json::array()).dump());
    for (const auto& e : body.value("edits", json::array())) {
        const std::string doc = e.value("doc", ""), pointer = e.value("pointer", "");
        if (!docs.contains(doc)) {
            problems.push_back("no " + doc);
            continue;
        }
        try {
            const json::json_pointer at(pointer);
            json done = {{"doc", doc}, {"pointer", pointer}, {"value", e.value("value", json())}};
            if (docs[doc].contains(at)) done["previous"] = docs[doc].at(at);
            undo.push_back(done);
            docs[doc][at] = e.value("value", json());
            changed.insert(doc);
        } catch (const std::exception& x) {
            problems.push_back(doc + pointer + ": " + x.what());
        }
    }
    // Back where each came from, the tab's own way.
    if (!changed.empty()) {
        if (tab == "placements") {
            for (const char* list : {"characters", "props"}) {
                for (auto& entry : placements_[list]) {
                    const std::string doc = std::string("placements/") + list + "/" + PlanSlug(entry.value("id", ""));
                    if (!changed.contains(doc)) continue;
                    entry = docs[doc];
                    entry["locked"] = true; // as an edit by hand: Replace Characters keeps it
                }
            }
            SavePlacements();
            if (const std::string why = PlaceCharactersAgain([](const std::string&) {}); !why.empty()) problems.push_back(why);
            RefreshPlacementRequirements();
            CheckPlacementRequirements();
        } else if (tab == "requirements" || tab == "cast") {
            for (auto& r : reqs_["items"]) {
                if (changed.contains("requirements/" + r.value("id", ""))) r = docs["requirements/" + r.value("id", "")];
            }
            SaveRequirements();
        } else if (tab == "gameplay") {
            for (auto& r : reqs_["implementations"]) {
                if (changed.contains("implementations/" + r.value("id", ""))) r = docs["implementations/" + r.value("id", "")];
            }
            SaveRequirements();
        } else if (tab == "quests") {
            for (const auto& doc : changed) {
                const std::string quest = doc.substr(7);
                if (const auto why = WriteStoryFile(host_.projectRoot, "quests", quest, docs[doc])) problems.push_back(doc + ": " + *why);
            }
        }
    }
    std::ifstream in(store_.Dir() / "build-revisions.json", std::ios::binary);
    json history = in ? json::parse(in, nullptr, false) : json();
    in.close();
    if (!history.is_object()) history = json::object();
    for (auto& entry : history[tab].is_array() ? history[tab] : json::array()) {
        if (entry.value("id", "") != id) continue;
        json edits = json::array();
        for (const auto& e : body.value("edits", json::array())) {
            edits.push_back({{"doc", e.value("doc", "")}, {"pointer", e.value("pointer", "")}, {"reason", e.value("reason", "")}});
        }
        entry["targets"] = body.value("targets", json::array());
        entry["edits"] = edits;
        entry["undo"] = undo;
        entry["rationale"] = body.value("rationale", "");
        entry["made"] = static_cast<int>(changed.size());
        entry["status"] = changed.empty() && !problems.empty() ? "failed" : "done";
        if (!problems.empty()) {
            std::string all;
            for (const auto& p : problems) all += (all.empty() ? "" : "; ") + p;
            entry["error"] = all;
        }
    }
    std::ofstream(store_.Dir() / "build-revisions.json", std::ios::binary) << history.dump(1);
    Record("revise", tab, std::to_string(changed.size()) + " changed", body.value("rationale", ""));
}

json GameBuild::Card() const {
    int built = 0;
    for (const auto& r : rows_) built += r.status == "built" || r.status == "placeholder";
    return {{"stage", stage_}, {"status", status_}, {"message", message_}, {"progress", {{"done", built}, {"total", rows_.size()}}}};
}

} // namespace aige
