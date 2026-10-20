#include "editing/PlaceScene.h"

#include "assets/PackImport.h"
#include "assets/SceneIO.h"
#include "assets/TextureCook.h"
#include "editing/SceneTools.h"
#include "gameplay/Components.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/TileGrid.h"
#include "gameplay/TileLayer.h"
#include "physics2d/Components.h"
#include "places/Planner.h"
#include "renderer2d/Components.h"
#include "renderer2d/ImageData.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <set>

namespace aige {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string LayerName(const std::string& place) { return "place:" + place; }

uint32_t Hash(const std::string& text) {
    uint32_t h = 2166136261u;
    for (const unsigned char c : text) h = (h ^ c) * 16777619u;
    return h;
}

std::string Lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The solved terrain grid: a name per cell (rows of [count, palette index] runs).
struct TerrainGrid {
    int x0 = 0, y0 = 0, w = 0, h = 0;
    std::vector<std::string> cells;
    std::set<std::pair<int, int>> bridges;
    const std::string& At(int x, int y) const {
        static const std::string none;
        if (x < 0 || y < 0 || x >= w || y >= h) return none;
        return cells[static_cast<size_t>(y) * w + x];
    }
};

TerrainGrid ReadTerrain(const json& solved) {
    TerrainGrid grid;
    const json bounds = solved.contains("grid") ? solved["grid"].value("bounds", json::array({0, 0, 0, 0})) : json::array({0, 0, 0, 0});
    grid.x0 = bounds[0].get<int>();
    grid.y0 = bounds[1].get<int>();
    grid.w = bounds[2].get<int>();
    grid.h = bounds[3].get<int>();
    grid.cells.assign(static_cast<size_t>(std::max(0, grid.w * grid.h)), "grass");
    if (!solved.contains("terrain")) return grid;
    const json& terrain = solved["terrain"];
    const json palette = terrain.value("palette", json::array());
    const json rows = terrain.value("rows", json::array());
    for (int y = 0; y < static_cast<int>(rows.size()) && y < grid.h; ++y) {
        int x = 0;
        for (const auto& run : rows[y]) {
            const int count = run[0].get<int>(), index = run[1].get<int>();
            const std::string name = index >= 0 && index < static_cast<int>(palette.size()) ? palette[index].get<std::string>() : "";
            for (int k = 0; k < count && x < grid.w; ++k, ++x) grid.cells[static_cast<size_t>(y) * grid.w + x] = name.empty() ? "grass" : name;
        }
    }
    for (const auto& b : terrain.value("bridges", json::array())) grid.bridges.insert({b[0].get<int>(), b[1].get<int>()});
    return grid;
}

// The project terrain a layout terrain is painted with: by name, then by kin.
std::string MatchTerrain(const TerrainSet& set, const std::string& wanted) {
    static const std::map<std::string, std::vector<std::string>> kin = {
        {"grass", {"grass", "meadow", "green", "lawn"}},
        {"dirt", {"dirt", "road", "path", "earth", "mud", "sand", "soil"}},
        {"cobble", {"cobble", "stone", "paving", "pave", "brick", "gravel", "rock"}},
        {"water", {"water", "river", "lake", "sea", "pond"}},
    };
    auto it = kin.find(wanted);
    const std::vector<std::string> words = it == kin.end() ? std::vector<std::string>{wanted} : it->second;
    for (const auto& word : words) {
        for (const auto& t : set.terrains) {
            if (Lower(t).find(word) != std::string::npos && set.PlainTile(t)) return t;
        }
    }
    return {};
}

// --- Placeholders ---------------------------------------------------------------------------

struct Rgb {
    uint8_t r, g, b;
};

Rgb ColourFor(const std::string& key) {
    static const Rgb palette[] = {{196, 92, 72}, {214, 160, 84}, {120, 156, 196}, {150, 116, 180}, {94, 160, 120},
                                  {200, 120, 160}, {170, 150, 110}, {112, 132, 160}};
    return palette[Hash(key) % (sizeof(palette) / sizeof(palette[0]))];
}

void Fill(ImageData& image, int x0, int y0, int x1, int y1, Rgb c, uint8_t a = 255) {
    for (int y = std::max(0, y0); y < std::min<int>(image.height, y1); ++y) {
        for (int x = std::max(0, x0); x < std::min<int>(image.width, x1); ++x) {
            uint8_t* p = &image.rgba[(static_cast<size_t>(y) * image.width + x) * 4];
            p[0] = c.r, p[1] = c.g, p[2] = c.b, p[3] = a;
        }
    }
}

Rgb Darker(Rgb c, float f = 0.65f) {
    return {static_cast<uint8_t>(c.r * f), static_cast<uint8_t>(c.g * f), static_cast<uint8_t>(c.b * f)};
}

// A plain shape for something there's no art for: a house (walls and a roof),
// a prop (a block), a plant (a tuft), a fence piece (a rail), a ground tile.
ImageData PlaceholderImage(const std::string& kind, const std::string& key) {
    ImageData image;
    const Rgb c = ColourFor(key);
    auto make = [&](int w, int h) {
        image.width = w;
        image.height = h;
        image.rgba.assign(static_cast<size_t>(w) * h * 4, 0);
    };
    if (kind == "tile") {
        make(32, 32);
        static const std::map<std::string, Rgb> ground = {
            {"grass", {104, 158, 84}}, {"dirt", {176, 146, 102}}, {"cobble", {150, 150, 146}}, {"water", {72, 128, 186}}};
        auto it = ground.find(key);
        const Rgb g = it == ground.end() ? c : it->second;
        Fill(image, 0, 0, 32, 32, g);
        for (int k = 0; k < 32; k += 8) Fill(image, k, k, k + 2, k + 2, Darker(g, 0.88f)); // a little texture
        return image;
    }
    if (kind == "building") {
        make(64, 64);
        Fill(image, 6, 30, 58, 62, c);                    // walls
        Fill(image, 6, 60, 58, 64, Darker(c, 0.5f));      // their foot
        for (int y = 6; y < 32; ++y) {                    // a roof, narrowing upward
            const int inset = (32 - y) * 22 / 26;
            Fill(image, 2 + inset, y, 62 - inset, y + 1, Darker(c, 0.7f));
        }
        Fill(image, 28, 46, 36, 62, Darker(c, 0.45f));    // a door
        return image;
    }
    if (kind == "fence") {
        make(32, 16);
        Fill(image, 0, 4, 32, 7, Darker(c, 0.55f));
        Fill(image, 0, 10, 32, 12, Darker(c, 0.55f));
        Fill(image, 2, 1, 5, 16, Darker(c, 0.45f));
        Fill(image, 27, 1, 30, 16, Darker(c, 0.45f));
        return image;
    }
    if (kind == "decal") {
        make(16, 16);
        Fill(image, 6, 4, 10, 15, Darker(c, 0.8f));
        Fill(image, 3, 6, 13, 11, c);
        return image;
    }
    make(32, 32); // a prop
    Fill(image, 6, 8, 26, 30, c);
    Fill(image, 6, 28, 26, 32, Darker(c, 0.5f));
    return image;
}

// The placeholder prefab for a need (made once).
std::string PlaceholderPrefab(PrefabLibrary& prefabs, const std::string& kind, const std::string& key, float widthPx,
                              float heightPx, bool solid, const IsoParams& iso) {
    const std::string slug = PrefabSlug(kind + "-" + key);
    const std::string path = "prefabs/placeholders/" + slug + ".prefab";
    if (prefabs.Find(path)) return path;
    const std::string image = "assets/generated/placeholders/" + slug + ".png";
    const fs::path file = prefabs.GetProject().Resolve(image);
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    if (!WritePng(file, PlaceholderImage(kind, key))) return {};
    PrefabPlan plan;
    plan.asset = image;
    plan.path = path;
    plan.name = "Placeholder " + key;
    plan.tile = kind == "tile";
    plan.solid = solid;
    plan.width = widthPx;
    plan.height = heightPx;
    if (!CreatePrefabFromPlan(prefabs, plan, iso).empty()) return {};
    prefabs.Refresh();
    return prefabs.Find(path) ? path : std::string();
}

// --- Needs ------------------------------------------------------------------------------------

const SlotDef* SlotOf(const ModuleDef* m, const std::string& name) {
    if (m == nullptr) return nullptr;
    for (const auto& s : m->slots) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

const json* NodeOf(const json& spec, const std::string& id) {
    std::function<const json*(const json&)> find = [&](const json& n) -> const json* {
        if (n.value("id", "") == id) return &n;
        if (!n.contains("children") || !n["children"].is_array()) return nullptr;
        for (const auto& c : n["children"]) { // the node's own children: a copy's would die with the loop
            if (const json* hit = find(c)) return hit;
        }
        return nullptr;
    };
    return spec.contains("root") ? find(spec["root"]) : nullptr;
}

struct Need {
    std::string key, role, layer, kind; // kind: building, prop, decal, fence
    std::vector<std::string> concepts;
    int w = 1, h = 1;
};

// What a solved object needs: its slot's role, what it must show (the mix's
// pick, or the field its slot names: a cottage), its layer and footprint.
Need NeedOf(const Ontology& ontology, const json& spec, const json& object) {
    Need need;
    need.layer = object.value("layer", "prop");
    need.w = object.value("w", 1);
    need.h = object.value("h", 1);
    const json* node = NodeOf(spec, object.value("node", ""));
    const ModuleDef* m = node ? ontology.Module(node->value("module", "")) : nullptr;
    const SlotDef* slot = SlotOf(m, object.value("slot", ""));
    if (slot && !slot->binds.empty()) {
        const SlotBinding* bind = &slot->binds.front();
        for (const auto& b : slot->binds) {
            if (b.when.empty()) {
                bind = &b;
                break;
            }
        }
        need.role = bind->role;
        need.concepts = bind->conceptList;
        if (!bind->concepts.empty() && node) {
            const json v = node->contains("quality") ? (*node)["quality"].value(bind->concepts, json()) : json();
            if (v.is_string()) need.concepts = {v.get<std::string>()};
        }
    }
    if (need.role.empty()) need.role = object.value("slot", "thing");
    if (object.contains("module") && object["module"].is_string()) need.concepts = {object["module"].get<std::string>()};
    need.kind = need.layer == "building" ? "building" : need.layer == "decal" ? "decal" : "prop";
    std::string concepts;
    for (const auto& c : need.concepts) concepts += (concepts.empty() ? "" : "+") + c;
    need.key = need.role + "|" + concepts + "|" + need.layer + (need.kind == "building" ? "|" + std::to_string(need.w) + "x" + std::to_string(need.h) : "");
    return need;
}

// Screen width, in pixels, a footprint of w x h cells takes.
float FootprintPixels(int w, int h, const PlaceSceneOptions& options) {
    return options.iso.topDown ? w * options.iso.tileWidth : (w + h) * options.iso.tileWidth / 2.0f;
}

// The prefab showing a catalog entry: an existing one of the whole image, or a new one.
std::string PrefabForEntry(PrefabLibrary& prefabs, const AssetDatabase& assets, const CatalogEntry& entry, bool solid,
                           const PlaceSceneOptions& options) {
    if (entry.id.find('#') != std::string::npos) return {}; // one object of a sheet: not yet (7.9)
    for (const auto& info : prefabs.Prefabs()) {
        if (!info.isTile && info.texture == entry.path && info.u0 <= 0.001f && info.v0 <= 0.001f && info.u1 >= 0.999f && info.v1 >= 0.999f) {
            return info.path;
        }
    }
    auto plans = PlanPackImport(assets, {entry.path}, prefabs, options.iso, {});
    if (plans.empty()) return {};
    PrefabPlan plan = plans.front();
    plan.tile = false;
    plan.solid = solid;
    if (plan.width <= 0 || plan.height <= 0) {
        plan.width = std::max(1.0f, entry.spriteW) * options.iso.tileWidth;
        plan.height = plan.width * (entry.rect.w > 0 ? static_cast<float>(entry.rect.h) / entry.rect.w : 1.0f);
    }
    if (!CreatePrefabFromPlan(prefabs, plan, options.iso).empty()) return {};
    prefabs.Refresh();
    return prefabs.Find(plan.path) ? plan.path : std::string();
}

// A tile layer palette entry from a tile prefab, turned `rotation` quarter turns.
std::optional<TileDef> TileDefFromPrefab(PrefabLibrary& prefabs, const std::string& path, int rotation, bool solidOverride) {
    std::optional<TileDef> def;
    prefabs.VisitTemplate(path, [&](Registry& registry, Entity entity) {
        const auto* sprite = registry.try_get<Sprite>(entity);
        if (sprite == nullptr) return;
        TileDef d;
        d.sprite = *sprite;
        d.sprite.rotation = rotation;
        const auto* tile = registry.try_get<Tile>(entity);
        d.solid = solidOverride || (tile && tile->solid);
        d.prefab = path;
        def = d;
    });
    return def;
}

// Where a solved object stands in the world, and the cells it blocks around that.
struct Stand {
    float x = 0, y = 0;
    int left = 0, right = 0, back = 0;
};

Stand StandOf(const json& o, const PlaceSceneOptions& options) {
    const int x = o.value("x", 0) + options.originX, y = o.value("y", 0) + options.originY;
    const int w = std::max(1, o.value("w", 1)), h = std::max(1, o.value("h", 1));
    Stand s;
    if (w == 1 && h == 1) {
        s.x = x + 0.5f;
        s.y = y + 0.5f;
        return s;
    }
    // Upright art stands on its footprint's front row: on an isometric grid the
    // front corner, top-down the middle of the front row.
    if (options.iso.topDown) {
        s.x = x + w / 2.0f;
        s.left = w / 2;
        s.right = w - 1 - s.left;
    } else {
        s.x = x + w - 0.5f;
        s.left = w - 1;
    }
    s.y = y + h - 0.5f;
    s.back = h - 1;
    return s;
}

bool UnderAny(const std::set<std::string>& roots, const std::string& v) {
    for (const auto& r : roots) {
        if (v == r || v.rfind(r + "/", 0) == 0 || v.rfind(r + "#", 0) == 0) return true;
    }
    return false;
}

// Whether a thing of the place is left as it is in the scene: it's locked, or
// the edit is about other parts.
bool Kept(const PlaceSceneOptions& options, const std::string& node, const std::string& id) {
    if (UnderAny(options.locked, node) || UnderAny(options.locked, id)) return true;
    return !options.only.empty() && !UnderAny(options.only, node) && !UnderAny(options.only, id);
}

std::vector<Entity> PlaceEntities(Registry& registry, const std::string& place) {
    std::vector<Entity> out;
    for (auto entity : EntitiesInOrder(registry)) {
        if (const auto* node = registry.try_get<PlaceNode>(entity); node && node->place == place) out.push_back(entity);
    }
    return out;
}

// The cell outside the middle of a building's front: its door.
std::pair<int, int> DoorCell(const json& o, const PlaceSceneOptions& options) {
    const int x = o.value("x", 0) + options.originX, y = o.value("y", 0) + options.originY;
    const int w = std::max(1, o.value("w", 1)), h = std::max(1, o.value("h", 1));
    const std::string f = o.value("facing", "s");
    if (f == "n") return {x + w / 2, y - 1};
    if (f == "w") return {x - 1, y + h / 2};
    if (f == "e") return {x + w, y + h / 2};
    return {x + w / 2, y + h};
}

// Places one object with a prefab; returns the entity.
std::optional<Entity> PlaceObject(Registry& registry, PrefabLibrary& prefabs, const json& o, const json& choice,
                                  const PlaceSceneOptions& options) {
    const std::string prefab = choice.value("prefab", "");
    if (prefab.empty()) return std::nullopt;
    const Stand stand = StandOf(o, options);
    auto placed = prefabs.Instantiate(registry, prefab, stand.x, stand.y, false);
    if (!placed) return std::nullopt;
    const Entity e = *placed;
    registry.emplace_or_replace<PlaceNode>(e, options.place, o.value("node", ""), o.value("id", ""));
    const std::string layer = o.value("layer", "prop");
    // A building is as wide as its footprint, and blocks all of it.
    if (layer == "building") {
        if (const auto* sprite = registry.try_get<Sprite>(e); sprite && sprite->width > 0) {
            const float factor = FootprintPixels(o.value("w", 1), o.value("h", 1), options) / sprite->width;
            if (std::abs(factor - 1.0f) > 0.02f) ScaleEntity(EngineReflection(), registry, e, factor);
        }
        auto& obstacle = registry.emplace_or_replace<Obstacle>(e);
        obstacle.left = stand.left;
        obstacle.right = stand.right;
        obstacle.back = stand.back;
        for (const char* field : {"left", "right", "back"}) RecordOverride(registry, e, "Obstacle", field);
    }
    return e;
}

} // namespace

// --- Where it goes -----------------------------------------------------------------------------

std::pair<int, int> FreeOrigin(Registry& registry, const std::string& place, int side, int gap) {
    int minX = INT32_MAX, minY = INT32_MAX, maxX = INT32_MIN, maxY = INT32_MIN;
    auto take = [&](int x, int y) {
        minX = std::min(minX, x), minY = std::min(minY, y);
        maxX = std::max(maxX, x), maxY = std::max(maxY, y);
    };
    for (auto [entity, world] : registry.view<WorldTransform2D>().each()) {
        if (const auto* node = registry.try_get<PlaceNode>(entity); node && node->place == place) continue;
        take(static_cast<int>(std::floor(world.x)), static_cast<int>(std::floor(world.y)));
    }
    for (auto [entity, tile] : registry.view<Tile>().each()) take(tile.gx, tile.gy);
    if (const TileLayers* layers = FindLayers(registry)) {
        for (const auto& [name, layer] : layers->layers) {
            if (name == LayerName(place) || layer.Empty()) continue;
            const auto b = layer.Bounds();
            take(b[0], b[1]);
            take(b[2] - 1, b[3] - 1);
        }
    }
    (void)side;
    if (minX == INT32_MAX) return {0, 0};
    return {maxX + 1 + gap, minY};
}

// --- The asset plan ------------------------------------------------------------------------------

json PlanPlaceAssets(const Ontology& ontology, const json& spec, const json& solved, const Catalog& catalog, PrefabLibrary& prefabs,
                     const AssetDatabase& assets, const TerrainSet& terrains, const PlaceSceneOptions& options) {
    std::map<std::string, std::pair<Need, int>> needs; // key -> need, uses
    json out = {{"needs", json::array()}, {"terrains", json::object()}, {"pieces", json::object()}, {"byObject", json::object()}};
    for (const auto& o : solved.value("objects", json::array())) {
        Need need = NeedOf(ontology, spec, o);
        out["byObject"][o.value("id", "")] = need.key;
        auto& entry = needs[need.key];
        if (entry.second == 0) entry.first = need;
        entry.second++;
    }
    for (const auto& [key, item] : needs) {
        const auto& [need, uses] = item;
        SlotNeed slot;
        slot.role = need.role;
        slot.concepts = need.concepts;
        if (need.kind == "building") {
            slot.minCells = std::max(1.0f, need.w * 0.6f);
            slot.maxCells = need.w * 1.6f + 1;
            slot.layer = "";
        }
        Retrieval found = RetrieveForSlot(catalog, ontology, slot, options.isoGrid);
        json row = {{"key", key}, {"role", need.role}, {"concepts", need.concepts}, {"layer", need.layer}, {"w", need.w},
                    {"h", need.h}, {"uses", uses}};
        // Nothing shows the concept (no cottage art): any art of the role beats a placeholder.
        if (found.candidates.empty() && !slot.concepts.empty()) {
            SlotNeed any = slot;
            any.concepts.clear();
            Retrieval broader = RetrieveForSlot(catalog, ontology, any, options.isoGrid);
            if (!broader.candidates.empty()) {
                std::string list;
                for (const auto& c : slot.concepts) list += (list.empty() ? "" : " or ") + c;
                row["nearest"] = "no art shows " + list + ": any " + need.role;
                found = std::move(broader);
            }
        }
        const bool solid = need.kind == "building";
        for (const auto& c : found.candidates) {
            if (c.entry == nullptr || c.grade.empty()) continue;
            const std::string prefab = PrefabForEntry(prefabs, assets, *c.entry, solid, options);
            if (prefab.empty()) continue;
            row["prefab"] = prefab;
            row["entry"] = c.entry->id;
            row["source"] = c.entry->source;
            row["grade"] = c.grade;
            row["fit"] = c.fit;
            row["licence"] = c.entry->licence;
            row["name"] = c.entry->name;
            break;
        }
        if (!row.contains("prefab")) {
            // Nothing fits: a placeholder, and why (the best rejection).
            const float width = need.kind == "building" ? FootprintPixels(need.w, need.h, options)
                                : need.kind == "decal"  ? options.iso.tileWidth * 0.35f
                                                        : options.iso.tileWidth * 0.6f;
            const std::string label = need.concepts.empty() ? need.role : need.concepts.front();
            const std::string prefab = PlaceholderPrefab(prefabs, need.kind, label + (need.kind == "building" ? "-" + std::to_string(need.w) + "x" + std::to_string(need.h) : ""),
                                                         width, width, solid, options.iso);
            row["prefab"] = prefab;
            row["source"] = "placeholder";
            row["why"] = found.candidates.empty() ? (found.rejected.empty() ? "no art in the project for it"
                                                                             : "nothing fits: " + found.rejected.front().reason)
                                                  : "no candidate could be made a prefab";
        }
        out["needs"].push_back(row);
    }
    // Fence and wall pieces.
    std::set<std::string> lineSlots;
    for (const auto& l : solved.value("lines", json::array())) lineSlots.insert(l.value("slot", "fence"));
    for (const auto& slotName : lineSlots) {
        SlotNeed slot;
        slot.role = slotName == "wall" ? "wall-piece-set" : slotName == "hedge" ? "hedge-piece-set" : "fence-piece-set";
        slot.joined = true;
        const Retrieval found = RetrieveForSlot(catalog, ontology, slot, options.isoGrid);
        json row = {{"role", slot.role}};
        for (const auto& c : found.candidates) {
            if (c.pieceSet == nullptr) continue;
            json pieces = json::object();
            for (const auto& [kind, id] : c.pieceSet->pieces) {
                if (const CatalogEntry* e = catalog.Find(id)) {
                    const std::string prefab = PrefabForEntry(prefabs, assets, *e, false, options);
                    if (!prefab.empty()) pieces[kind] = prefab;
                }
            }
            if (pieces.contains("straight-h") || pieces.contains("straight-v")) {
                row["set"] = c.pieceSet->id;
                row["pieces"] = pieces;
                row["source"] = "project";
                break;
            }
        }
        if (!row.contains("pieces")) {
            row["pieces"] = {{"straight-h", PlaceholderPrefab(prefabs, "fence", slotName, options.iso.tileWidth * 0.5f,
                                                              options.iso.tileWidth * 0.25f, true, options.iso)}};
            row["source"] = "placeholder";
            row["why"] = "no usable " + slot.role + " in the project";
        }
        out["pieces"][slotName] = row;
    }
    // The ground: each terrain of the layout, by the project's own if it has one.
    const TerrainGrid grid = ReadTerrain(solved);
    std::set<std::string> used(grid.cells.begin(), grid.cells.end());
    if (!grid.bridges.empty()) used.insert("dirt");
    for (const auto& t : used) {
        const std::string mine = MatchTerrain(terrains, t);
        if (!mine.empty()) {
            out["terrains"][t] = {{"terrain", mine}, {"source", "project"}};
        } else {
            out["terrains"][t] = {{"prefab", PlaceholderPrefab(prefabs, "tile", t, options.iso.tileWidth, options.iso.tileHeight,
                                                               t == "water", options.iso)},
                                  {"source", "placeholder"}, {"why", "no " + t + " ground tiles in the project"}};
        }
    }
    return out;
}

// --- Building ------------------------------------------------------------------------------------

std::unique_ptr<EditCommand> BuildPlaceScene(Registry& registry, PrefabLibrary& prefabs, const json& solved, const json& assetPlan,
                                             const TerrainSet& terrains, const PlaceSceneOptions& options, json* report) {
    json out = {{"cells", json::object()}, {"objects", json::object()}, {"missing", json::array()}, {"pieces", 0}};
    // The previous build of this place goes (kept for undo); nothing else does. An
    // edit replaces only its parts; locked parts stay as they were edited by hand.
    const auto previous = PlaceEntities(registry, options.place);
    std::string previousText = SerializeEntities(registry, previous);
    std::vector<Entity> replaced;
    for (Entity e : previous) {
        const auto& n = registry.get<PlaceNode>(e);
        if (!Kept(options, n.node, n.statement)) replaced.push_back(e);
    }
    registry.destroy(replaced.begin(), replaced.end());

    // The ground: a layer of its own, fitted by the autotiler.
    const TerrainGrid grid = ReadTerrain(solved);
    TileLayer& layer = LayersOf(registry).Get(LayerName(options.place));
    TileLayerEdit edit(LayerName(options.place));
    const json terrainsUsed = assetPlan.value("terrains", json::object());
    auto mine = [&](const std::string& t) -> std::string {
        const json row = terrainsUsed.value(t, json::object());
        return row.value("terrain", "");
    };
    const std::string background = mine("grass");
    auto terrainAt = [&](int gx, int gy) -> std::string {
        const int x = gx - options.originX, y = gy - options.originY;
        if (grid.bridges.contains({x, y})) return mine("dirt");
        const std::string m = mine(grid.At(x, y));
        return m.empty() ? "placeholder:" + grid.At(x, y) : m;
    };
    std::map<std::pair<std::string, int>, uint16_t> palette;
    auto defFor = [&](const std::string& prefab, int rotation, bool solid) -> uint16_t {
        auto key = std::pair{prefab, rotation * 2 + (solid ? 1 : 0)};
        if (auto it = palette.find(key); it != palette.end()) return it->second;
        const auto def = TileDefFromPrefab(prefabs, prefab, rotation, solid);
        const uint16_t id = def ? layer.Intern(*def) : TileLayer::kEmpty;
        palette[key] = id;
        return id;
    };
    std::set<std::pair<int, int>> painted;
    for (int y = 0; y < grid.h; ++y) {
        for (int x = 0; x < grid.w; ++x) {
            const int gx = x + options.originX, gy = y + options.originY;
            const std::string name = grid.bridges.contains({x, y}) ? "dirt" : grid.At(x, y);
            const json row = terrainsUsed.value(name, json::object());
            const bool water = name == "water";
            uint16_t id = TileLayer::kEmpty;
            if (row.contains("terrain")) {
                const std::string center = terrainAt(gx, gy);
                const EdgeSignature target = TargetSignature(terrainAt, gx, gy, background.empty() ? center : background);
                if (auto choice = ChooseTile(terrains, target, center)) id = defFor(choice->prefab, choice->rotation, water);
            }
            if (id == TileLayer::kEmpty && row.contains("prefab")) id = defFor(row["prefab"], 0, water);
            if (id == TileLayer::kEmpty) continue;
            if (layer.Get(gx, gy) != id) edit.Set(layer, gx, gy, id); // only what changed
            painted.insert({gx, gy});
            out["cells"][name] = out["cells"].value(name, 0) + 1;
        }
    }
    // Cells the previous build had that this one doesn't: cleared.
    std::vector<std::pair<int, int>> stale;
    layer.ForEach([&](int gx, int gy, uint16_t) {
        if (!painted.contains({gx, gy})) stale.push_back({gx, gy});
    });
    for (const auto& [gx, gy] : stale) edit.Set(layer, gx, gy, TileLayer::kEmpty);

    // Things, back to front.
    std::map<std::string, json> choices;
    for (const auto& n : assetPlan.value("needs", json::array())) choices[n.value("key", "")] = n;
    const json list = solved.value("objects", json::array()); // pointers below point into it
    std::vector<const json*> objects;
    for (const auto& o : list) objects.push_back(&o);
    std::stable_sort(objects.begin(), objects.end(), [](const json* a, const json* b) {
        return a->value("y", 0) + a->value("h", 1) < b->value("y", 0) + b->value("h", 1);
    });
    // Which need each object is (the plan worked it out from the Spec).
    const json byObject = assetPlan.value("byObject", json::object());
    for (const json* o : objects) {
        const std::string id = o->value("id", "");
        if (Kept(options, o->value("node", ""), id)) continue; // not this edit's, or locked: as it is
        const std::string key = byObject.value(id, "");
        auto it = choices.find(key);
        if (it == choices.end() || !PlaceObject(registry, prefabs, *o, it->second, options)) {
            out["missing"].push_back(id);
            continue;
        }
        const std::string layerName = o->value("layer", "prop");
        out["objects"][layerName] = out["objects"].value(layerName, 0) + 1;
    }
    // Fences, walls and hedges: a piece on every edge (gates left open).
    const json pieces = assetPlan.value("pieces", json::object());
    for (const auto& line : solved.value("lines", json::array())) {
        if (Kept(options, line.value("node", ""), line.value("id", ""))) continue;
        const json row = pieces.value(line.value("slot", "fence"), json::object());
        const json kinds = row.value("pieces", json::object());
        std::set<std::tuple<int, int, std::string>> gaps;
        for (const auto& g : line.value("gaps", json::array())) gaps.insert({g[0].get<int>(), g[1].get<int>(), g[2].get<std::string>()});
        for (const auto& e : line.value("edges", json::array())) {
            const int x = e[0].get<int>(), y = e[1].get<int>();
            const std::string side = e[2].get<std::string>();
            if (gaps.contains({x, y, side})) continue;
            const bool along = side == "n" || side == "s"; // runs along x
            std::string prefab = kinds.value(along ? "straight-h" : "straight-v", "");
            bool mirror = false;
            if (prefab.empty()) {
                prefab = kinds.value(along ? "straight-v" : "straight-h", "");
                mirror = true; // the other direction's piece, mirrored
            }
            if (prefab.empty()) continue;
            float wx = x + options.originX + 0.5f, wy = y + options.originY + 0.5f;
            if (side == "n") wy -= 0.5f;
            if (side == "s") wy += 0.5f;
            if (side == "w") wx -= 0.5f;
            if (side == "e") wx += 0.5f;
            auto placed = prefabs.Instantiate(registry, prefab, wx, wy, false);
            if (!placed) continue;
            registry.emplace_or_replace<PlaceNode>(*placed, options.place, line.value("node", ""), line.value("id", ""));
            registry.remove<Obstacle>(*placed); // an edge, not a cell: the fence doesn't fill the cells beside it
            if (mirror) MirrorEntity(EngineReflection(), registry, *placed);
            out["pieces"] = out["pieces"].get<int>() + 1;
        }
    }
    if (report) *report = out;
    auto build = std::make_unique<CompoundCommand>("Build " + options.place);
    build->Add(std::make_unique<TileLayerCommand>(std::vector<TileLayerEdit>{edit}, "Ground of " + options.place));
    build->Add(std::make_unique<RebuildPlaceCommand>(options.place, std::move(previousText)));
    return build;
}

// --- Verifying ---------------------------------------------------------------------------------------

json VerifyPlaceScene(Registry& registry, const json& solved, const json& assetPlan, const PlaceSceneOptions& options) {
    json checks = json::array();
    std::map<std::string, Entity> byStatement;
    for (auto entity : PlaceEntities(registry, options.place)) byStatement[registry.get<PlaceNode>(entity).statement] = entity;

    // Every object there, at its place.
    int expected = 0, found = 0, misplaced = 0;
    json missing = json::array(), off = json::array();
    for (const auto& o : solved.value("objects", json::array())) {
        ++expected;
        auto it = byStatement.find(o.value("id", ""));
        if (it == byStatement.end()) {
            if (missing.size() < 30) missing.push_back(o.value("id", ""));
            continue;
        }
        ++found;
        const auto* world = registry.try_get<WorldTransform2D>(it->second);
        const Stand stand = StandOf(o, options);
        if (world == nullptr || std::abs(world->x - stand.x) > 0.51f || std::abs(world->y - stand.y) > 0.51f) {
            ++misplaced;
            if (off.size() < 30) off.push_back(o.value("id", ""));
        }
    }
    checks.push_back({{"id", "objects"}, {"what", "Every object of the layout is in the scene"}, {"ok", found == expected},
                      {"detail", std::to_string(found) + " of " + std::to_string(expected)}, {"items", missing}});
    checks.push_back({{"id", "positions"}, {"what", "Each stands where the layout put it"}, {"ok", misplaced == 0},
                      {"detail", std::to_string(found - misplaced) + " of " + std::to_string(found)}, {"items", off}});

    // The ground whole.
    const TerrainGrid grid = ReadTerrain(solved);
    int holes = 0;
    const TileLayer* layer = LayersOf(registry).Find(LayerName(options.place));
    for (int y = 0; y < grid.h; ++y) {
        for (int x = 0; x < grid.w; ++x) holes += !layer || layer->Get(x + options.originX, y + options.originY) == TileLayer::kEmpty;
    }
    checks.push_back({{"id", "ground"}, {"what", "The ground covers the site"}, {"ok", holes == 0},
                      {"detail", holes == 0 ? "every cell" : std::to_string(holes) + " cells without ground"}});

    // Doors reachable from the entrances, over the scene's real collision.
    const TileGrid collision = TileGrid::Build(registry);
    std::set<std::pair<int, int>> seen;
    std::deque<std::pair<int, int>> todo;
    for (const auto& a : solved.value("anchors", json::array())) {
        if (a.value("kind", "") != "entrance" && a.value("kind", "") != "gate") continue;
        const std::pair<int, int> c = {a["cell"][0].get<int>() + options.originX, a["cell"][1].get<int>() + options.originY};
        if (seen.insert(c).second) todo.push_back(c);
    }
    const int x0 = options.originX - 1, y0 = options.originY - 1, x1 = options.originX + grid.w, y1 = options.originY + grid.h;
    while (!todo.empty()) {
        const auto [cx, cy] = todo.front();
        todo.pop_front();
        for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
            const std::pair<int, int> n = {cx + dx, cy + dy};
            if (n.first < x0 || n.second < y0 || n.first > x1 || n.second > y1 || seen.contains(n)) continue;
            if (collision.IsSolid(n.first, n.second)) continue;
            seen.insert(n);
            todo.push_back(n);
        }
    }
    int doors = 0, reached = 0;
    json unreached = json::array();
    for (const auto& o : solved.value("objects", json::array())) {
        if (o.value("layer", "") != "building" || !o.value("primary", false)) continue;
        ++doors;
        if (seen.contains(DoorCell(o, options))) ++reached;
        else if (unreached.size() < 30) unreached.push_back(o.value("id", ""));
    }
    checks.push_back({{"id", "doors"}, {"what", "Every door can be walked to from the entrances"}, {"ok", reached == doors},
                      {"detail", std::to_string(reached) + " of " + std::to_string(doors)}, {"items", unreached}});

    // No two solid things on one cell.
    std::map<std::pair<int, int>, std::string> taken;
    json overlaps = json::array();
    for (auto entity : PlaceEntities(registry, options.place)) {
        const auto* obstacle = registry.try_get<Obstacle>(entity);
        const auto* world = registry.try_get<WorldTransform2D>(entity);
        if (!obstacle || !world) continue;
        const int sx = static_cast<int>(std::floor(world->x)), sy = static_cast<int>(std::floor(world->y));
        const std::string id = registry.get<PlaceNode>(entity).statement;
        bool clash = false;
        for (int y = sy - obstacle->back; y <= sy && !clash; ++y) {
            for (int x = sx - obstacle->left; x <= sx + obstacle->right && !clash; ++x) {
                auto [it, fresh] = taken.insert({{x, y}, id});
                if (!fresh && it->second != id) {
                    clash = true;
                    if (overlaps.size() < 30) overlaps.push_back(id);
                }
            }
        }
    }
    checks.push_back({{"id", "overlaps"}, {"what", "No two solid things share a cell"}, {"ok", overlaps.empty()},
                      {"detail", overlaps.empty() ? "none" : std::to_string(overlaps.size()) + " overlapping"}, {"items", overlaps}});

    // Placeholders: there, but to replace.
    int placeholders = 0, kinds = 0;
    for (const auto& n : assetPlan.value("needs", json::array())) {
        if (n.value("source", "") != "placeholder") continue;
        ++kinds;
        placeholders += n.value("uses", 0);
    }
    const json terrains = assetPlan.value("terrains", json::object());
    for (const auto& [name, t] : terrains.items()) kinds += t.value("source", "") == "placeholder";
    checks.push_back({{"id", "art"}, {"what", "Everything has art from the project"}, {"ok", kinds == 0}, {"warning", true},
                      {"detail", kinds == 0 ? "all of it" : std::to_string(kinds) + " kinds shown by placeholders (" +
                                                                std::to_string(placeholders) + " things)"}});
    return checks;
}

// --- Repairing ---------------------------------------------------------------------------------------

json RepairPlaceScene(Registry& registry, PrefabLibrary& prefabs, const json& solved, const json& assetPlan, const json& verify,
                      const PlaceSceneOptions& options) {
    json repairs = json::array();
    auto itemsOf = [&](const std::string& id) {
        std::set<std::string> out;
        for (const auto& c : verify) {
            if (c.value("id", "") == id && !c.value("ok", true)) {
                for (const auto& i : c.value("items", json::array())) out.insert(i.get<std::string>());
            }
        }
        return out;
    };
    const json list = solved.value("objects", json::array()); // pointers below point into it
    std::map<std::string, const json*> objects;
    for (const auto& o : list) objects[o.value("id", "")] = &o;

    // A missing object: placed with its kind's placeholder.
    for (const auto& id : itemsOf("objects")) {
        auto it = objects.find(id);
        if (it == objects.end()) continue;
        const json& o = *it->second;
        const std::string kind = o.value("layer", "") == "building" ? "building" : o.value("layer", "") == "decal" ? "decal" : "prop";
        const float width = kind == "building" ? FootprintPixels(o.value("w", 1), o.value("h", 1), options) : options.iso.tileWidth * 0.5f;
        const std::string prefab = PlaceholderPrefab(prefabs, kind, o.value("slot", "thing"), width, width, kind == "building", options.iso);
        if (PlaceObject(registry, prefabs, o, {{"prefab", prefab}}, options)) {
            repairs.push_back({{"what", "placed with a placeholder"}, {"id", id}, {"how", "its art couldn't be placed"}});
        }
    }
    // Adaptable things (not buildings) in a door's way or on another's cell: removed.
    std::set<std::pair<int, int>> doorCells;
    for (const auto& id : itemsOf("doors")) {
        if (auto it = objects.find(id); it != objects.end()) {
            const auto [dx, dy] = DoorCell(*it->second, options);
            for (int y = dy - 1; y <= dy + 1; ++y)
                for (int x = dx - 1; x <= dx + 1; ++x) doorCells.insert({x, y});
        }
    }
    const std::set<std::string> overlapping = itemsOf("overlaps");
    for (auto entity : PlaceEntities(registry, options.place)) {
        if (!registry.valid(entity) || !registry.all_of<Obstacle>(entity)) continue;
        const std::string statement = registry.get<PlaceNode>(entity).statement;
        auto it = objects.find(statement);
        if (it != objects.end() && it->second->value("layer", "") == "building") continue; // never a building
        const auto& world = registry.get<WorldTransform2D>(entity);
        const std::pair<int, int> cell = {static_cast<int>(std::floor(world.x)), static_cast<int>(std::floor(world.y))};
        const bool inWay = doorCells.contains(cell);
        if (!inWay && !overlapping.contains(statement)) continue;
        registry.destroy(entity);
        repairs.push_back({{"what", "removed"}, {"id", statement}, {"how", inWay ? "it stood in a door's way" : "it overlapped another"}});
    }
    return repairs;
}

// --- Undo -------------------------------------------------------------------------------------------

void RebuildPlaceCommand::Undo(Registry& registry) {
    const auto built = PlaceEntities(registry, place_);
    built_ = SerializeEntities(registry, built);
    registry.destroy(built.begin(), built.end());
    if (!previous_.empty()) AppendEntities(registry, previous_);
}

void RebuildPlaceCommand::Redo(Registry& registry) {
    const auto before = PlaceEntities(registry, place_);
    previous_ = SerializeEntities(registry, before);
    registry.destroy(before.begin(), before.end());
    AppendEntities(registry, built_);
    built_.clear();
}

} // namespace aige
