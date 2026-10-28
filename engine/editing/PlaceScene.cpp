#include "editing/PlaceScene.h"

#include "assets/ArtAnalysis.h"
#include "assets/PackImport.h"
#include "assets/SceneIO.h"
#include "assets/TextureCook.h"
#include "editing/Edits.h"
#include "editing/PaintedPlace.h"
#include "editing/SceneTools.h"
#include "gameplay/Archetypes.h"
#include "places/Brief.h"
#include "gameplay/Components.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/Behaviour.h"
#include "gameplay/Items.h"
#include "records/Records.h"
#include "gameplay/TileGrid.h"
#include "gameplay/TileLayer.h"
#include "physics2d/Components.h"
#include "places/Planner.h"
#include "places/Solver.h"
#include "renderer2d/Components.h"
#include "renderer2d/ImageData.h"

#include <algorithm>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <cctype>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
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

// Terrain nothing walks on: water, and a dungeon's rock between its rooms.
bool SolidTerrain(const std::string& t) { return t == "water" || t == "rock-wall"; }

// The project terrain a layout terrain is painted with: by name, then by kin.
std::string MatchTerrain(const TerrainSet& set, const std::string& wanted) {
    static const std::map<std::string, std::vector<std::string>> kin = {
        {"grass", {"grass", "meadow", "green", "lawn"}},
        {"tilled", {"tilled", "farmland", "furrow", "plough", "plow", "field", "soil"}},
        {"forest-floor", {"forest floor", "forest", "leaf", "leaves", "moss", "needle", "woodland"}},
        {"dirt", {"dirt", "road", "path", "earth", "mud", "sand", "soil"}},
        {"cobble", {"cobble", "stone", "paving", "pave", "brick", "gravel", "rock"}},
        {"water", {"water", "river", "lake", "sea", "pond"}},
        {"water-shallow", {"shallow", "water", "puddle"}},
        {"rock-wall", {"rock wall", "cave wall", "cliff", "wall", "rock"}},
        {"stone-floor", {"stone floor", "flagstone", "dungeon floor", "floor", "stone", "cobble"}},
        {"cave-floor", {"cave floor", "cave", "rock floor", "dirt", "earth"}},
        {"wood-floor", {"wood floor", "plank", "wooden floor", "floorboard", "wood", "floor"}},
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
            {"grass", {104, 158, 84}}, {"dirt", {176, 146, 102}}, {"cobble", {150, 150, 146}}, {"water", {72, 128, 186}},
            {"water-shallow", {104, 150, 186}}, {"rock-wall", {58, 56, 54}}, {"stone-floor", {143, 139, 132}},
            {"cave-floor", {125, 111, 95}}, {"wood-floor", {176, 138, 94}}, {"mud", {138, 111, 78}}, {"sand", {228, 210, 154}},
            {"snow", {238, 242, 245}}};
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

// A ground tile made seamless (8.3): the image wrapped by half its size and blended with
// itself, the original in the middle, the wrapped one at the edges, so copies side by side
// show no seams. Written beside it ("-seamless.png", without roles: never chosen itself).
// Its description says what it is: one terrain all over, centre and edges (made for that
// terrain, and seamless): no reading of its pixels (that took a grey cobble tile for grass).
void WriteTerrainMeta(const Project& project, const std::string& image, const std::string& source, const std::string& terrain) {
    std::vector<std::string> lines;
    {
        std::ifstream meta(project.Resolve(source + ".meta"));
        for (std::string line; std::getline(meta, line);) {
            if (line.rfind("roles=", 0) == 0 || line.rfind("center=", 0) == 0 || line.rfind("edges=", 0) == 0 || line.rfind("derived=", 0) == 0) continue;
            lines.push_back(line);
        }
    }
    std::ofstream written(project.Resolve(image + ".meta"), std::ios::binary);
    for (const auto& line : lines) written << line << "\n";
    written << "center=" << terrain << "\n";
    written << "edges=";
    for (int i = 0; i < 8; ++i) written << (i ? "," : "") << terrain;
    written << "\nderived=seamless\n";
}

std::string SeamlessCopy(const Project& project, const std::string& image, const std::string& terrain) {
    const fs::path from(image);
    const std::string copy = (from.parent_path() / (from.stem().string() + "-seamless.png")).generic_string();
    std::error_code ec;
    if (fs::exists(project.Resolve(copy), ec)) {
        // Made before: its description put right if it was read as another terrain.
        std::ifstream meta(project.Resolve(copy + ".meta"));
        bool right = false;
        for (std::string line; std::getline(meta, line);) right = right || line == "center=" + terrain;
        meta.close();
        if (!right) WriteTerrainMeta(project, copy, image, terrain);
        return copy;
    }
    const auto src = DecodeImage(project.Resolve(image));
    if (!src || src->width < 4 || src->height < 4) return image;
    const uint32_t w = src->width, h = src->height;
    ImageData out = *src;
    auto smooth = [](double t) { return t * t * (3 - 2 * t); };
    for (uint32_t y = 0; y < h; ++y) {
        const double fy = 1.0 - std::abs(2.0 * (y + 0.5) / h - 1.0);
        for (uint32_t x = 0; x < w; ++x) {
            const double fx = 1.0 - std::abs(2.0 * (x + 0.5) / w - 1.0);
            const double keep = smooth(std::min(1.0, 1.6 * std::min(fx, fy))); // 1 inside, 0 at the edges
            const uint32_t rx = (x + w / 2) % w, ry = (y + h / 2) % h;
            for (int c = 0; c < 4; ++c) {
                const double a = src->rgba[(y * w + x) * 4 + c], b = src->rgba[(ry * w + rx) * 4 + c];
                out.rgba[(y * w + x) * 4 + c] = static_cast<uint8_t>(std::lround(a * keep + b * (1 - keep)));
            }
        }
    }
    if (!WritePng(project.Resolve(copy), out)) return image;
    WriteTerrainMeta(project, copy, image, terrain);
    return copy;
}

// A ground tile the Art tool made, as a tile prefab (one per image: a refined one gets its own).
std::string GeneratedTilePrefab(PrefabLibrary& prefabs, const std::string& image, const std::string& terrain, bool solid, const IsoParams& iso) {
    const std::string path = "prefabs/generated/" + PrefabSlug("tile-" + fs::path(image).stem().string() + "-seamless") + ".prefab";
    if (prefabs.Find(path)) return path;
    PrefabPlan plan;
    plan.asset = SeamlessCopy(prefabs.GetProject(), image, terrain);
    plan.path = path;
    plan.name = "Generated " + terrain + " ground";
    plan.tile = true;
    plan.solid = solid;
    plan.width = iso.tileWidth;
    plan.height = iso.tileHeight;
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
    bool essential = true;
    std::string node; // a Spec node that has it (for the component's source policy)
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
    // The landscape's dressing (8.3): its own module's slots, whatever node it's under.
    if (object.value("statement", "").find("/landscape-") != std::string::npos) {
        m = ontology.Module("landscape");
        slot = SlotOf(m, object.value("slot", ""));
    }
    if (slot && !slot->binds.empty()) {
        const SlotBinding* bind = &slot->binds.front();
        for (const auto& b : slot->binds) {
            if (b.when.empty()) {
                bind = &b;
                break;
            }
        }
        need.role = bind->role;
        need.essential = slot->essential;
        need.concepts = bind->conceptList;
        if (!bind->concepts.empty() && node) {
            const json v = node->contains("quality") ? (*node)["quality"].value(bind->concepts, json()) : json();
            if (v.is_string()) need.concepts = {v.get<std::string>()};
        }
    }
    if (need.role.empty()) need.role = object.value("slot", "thing");
    // A slot of alternatives ("barrel|pot|bench|sign|lamp"): each object is one of them, by its
    // id (the same each build), so each kind gets its own art: not one picture for them all.
    if (need.role.find('|') != std::string::npos) {
        std::vector<std::string> alternatives;
        size_t start = 0;
        while (start <= need.role.size()) {
            const size_t bar = need.role.find('|', start);
            const std::string one = need.role.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
            if (!one.empty()) alternatives.push_back(one);
            if (bar == std::string::npos) break;
            start = bar + 1;
        }
        if (!alternatives.empty()) {
            uint32_t h = 2166136261u;
            for (const char c : object.value("id", std::string())) h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
            need.role = alternatives[h % alternatives.size()];
        }
    }
    need.node = object.value("node", "");
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
    if (entry.id.find('#') != std::string::npos) {
        // One object of a sheet (a web tileset): a prefab showing only its part of the image.
        const std::string path = "prefabs/sheets/" + PrefabSlug(entry.id) + ".prefab";
        if (prefabs.Find(path)) return path;
        const auto image = DecodeImage(prefabs.GetProject().Resolve(entry.path));
        if (!image || image->width == 0 || entry.rect.Empty()) return {};
        Registry scratch;
        const float width = std::max(1.0f, entry.spriteW) * options.iso.tileWidth;
        const float height = width * static_cast<float>(entry.rect.h) / std::max(1, entry.rect.w);
        Entity e = CreateProp(scratch, 0.0f, 0.0f, entry.path, width, height);
        auto& sprite = scratch.get<Sprite>(e);
        sprite.u0 = static_cast<float>(entry.rect.x) / image->width;
        sprite.v0 = static_cast<float>(entry.rect.y) / image->height;
        sprite.u1 = static_cast<float>(entry.rect.x + entry.rect.w) / image->width;
        sprite.v1 = static_cast<float>(entry.rect.y + entry.rect.h) / image->height;
        if (solid) scratch.emplace<Obstacle>(e);
        scratch.emplace_or_replace<Name>(e, entry.name.empty() ? entry.id : entry.name);
        std::error_code ec;
        fs::create_directories(prefabs.GetProject().Resolve("prefabs/sheets"), ec);
        if (!prefabs.SaveAsPrefab(scratch, e, path)) return {};
        prefabs.Refresh();
        return prefabs.Find(path) ? path : std::string();
    }
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

// A building whose lot the map's edge cuts, at its kind's own size: the footprint the same kind (its slot) has away from
// the edge, most often, facing the same way; reaching outward past the edge where it touches it (its inner sides stay).
// {x, y, w, h}, or nothing (not on the edge, or already its size).
std::optional<std::array<int, 4>> OwnSize(const json& o, const std::vector<const json*>& objects, int gridW, int gridH) {
    const int x = o.value("x", 0), y = o.value("y", 0), w = o.value("w", 1), h = o.value("h", 1);
    const bool left = x <= 0, top = y <= 0, right = x + w >= gridW, bottom = y + h >= gridH;
    if (!left && !top && !right && !bottom) return std::nullopt;
    std::map<std::pair<int, int>, int> sizes;
    for (const json* other : objects) {
        if (other->value("slot", "") != o.value("slot", "") || other->value("layer", "") != "building") continue;
        const int ox = other->value("x", 0), oy = other->value("y", 0), ow = other->value("w", 1), oh = other->value("h", 1);
        if (ox <= 0 || oy <= 0 || ox + ow >= gridW || oy + oh >= gridH) continue; // on an edge too
        if (other->value("facing", "s") != o.value("facing", "s")) continue;
        ++sizes[{ow, oh}];
    }
    if (sizes.empty()) return std::nullopt;
    const auto best = std::max_element(sizes.begin(), sizes.end(), [](const auto& a, const auto& b) { return a.second < b.second; })->first;
    int nx = x, ny = y, nw = w, nh = h;
    // Only across the edges it touches: along x for the left and right ones, along y for the top and bottom.
    if ((left || right) && best.first > w) {
        nw = best.first;
        if (left) nx = x + w - nw;
    }
    if ((top || bottom) && best.second > h) {
        nh = best.second;
        if (top) ny = y + h - nh;
    }
    if (nw == w && nh == h) return std::nullopt;
    return std::array<int, 4>{nx, ny, nw, nh};
}

// A building on the edge of the map cut at the edge (the map ends there: nothing of it stands past it). Each pixel of
// its picture is where its box shows it: its foot is its footprint's front corner (the scene seats it there), so each
// pixel is on the box's top or one of its two front faces, at a cell of the grid; what falls outside the map is
// cleared. The cut picture is the sprite's own (assets/generated/clipped/). False: nothing was outside.
bool ClipToMap(Registry& registry, PrefabLibrary& prefabs, Entity e, const json& o, int gridW, int gridH, const PlaceSceneOptions& options) {
    const auto* sprite = registry.try_get<Sprite>(e);
    if (sprite == nullptr || sprite->texture.empty() || sprite->width <= 0 || sprite->u0 > sprite->u1) return false;
    auto image = DecodeImage(prefabs.GetProject().Resolve(sprite->texture));
    if (!image || image->width < 8 || image->height < 8) return false;
    const int x = o.value("x", 0), y = o.value("y", 0), w = std::max(1, o.value("w", 1)), h = std::max(1, o.value("h", 1));
    const double k = image->width * options.iso.tileWidth / (2.0 * sprite->width); // picture pixels per half a cell across
    const double hz = image->height / (k / 2) - (w + h);                          // its height, in quarter cells
    const double fx = x + w, fy = y + h, e2 = 0.02;
    bool cut = false;
    for (uint32_t py = 0; py < image->height; ++py) {
        for (uint32_t px = 0; px < image->width; ++px) {
            uint8_t* p = &image->rgba[(static_cast<size_t>(py) * image->width + px) * 4];
            if (p[3] == 0) continue;
            const double u = (px + 0.5 - image->width / 2.0) / k, v = (py + 0.5 - image->height) / (k / 2);
            // From the front corner into the footprint: a along x, b along y.
            double a = -1, b = -1;
            const double ta = (-v - hz - u) / 2, tb = (u - v - hz) / 2;
            if (ta >= -e2 && ta <= w + e2 && tb >= -e2 && tb <= h + e2) a = ta, b = tb; // its top
            else if (u <= 0) a = -u, b = 0;                                            // its face along x
            else a = 0, b = u;                                                         // its face along y
            const double wx = fx - a, wy = fy - b;
            if (wx < -e2 || wy < -e2 || wx > gridW + e2 || wy > gridH + e2) p[3] = 0, cut = true;
        }
    }
    if (!cut) return false;
    const std::string path = "assets/generated/clipped/" + PrefabSlug(o.value("id", "thing")) + ".png";
    std::error_code ec;
    fs::create_directories(prefabs.GetProject().Resolve("assets/generated/clipped"), ec);
    if (!WritePng(prefabs.GetProject().Resolve(path), *image)) return false;
    EditField(EngineReflection(), registry, e, "Sprite", "texture", path);
    return true;
}

// Places one object with a prefab; returns the entity.
// Which of the grid's ways a picture of a thing that runs along its way (a car) is painted along, from where its lowest
// point is (measured once): towards the right, along x (a front facing east, a back facing west), 1; towards the left,
// along y (a front facing south, a back facing north), -1; in the middle, 0 (no telling).
int PictureAxis(PrefabLibrary& prefabs, const std::string& texture) {
    static std::map<std::string, int> axisOf;
    if (const auto it = axisOf.find(texture); it != axisOf.end()) return it->second;
    int axis = 0;
    if (const auto image = DecodeImage(prefabs.GetProject().Resolve(texture)); image && image->width > 8) {
        const int w = static_cast<int>(image->width), h = static_cast<int>(image->height);
        std::vector<int> foot(static_cast<size_t>(w), -1);
        for (int x = 0; x < w; ++x) {
            for (int y = h - 1; y >= 0; --y) {
                if (image->rgba[(static_cast<size_t>(y) * w + x) * 4 + 3] > 128) {
                    foot[x] = y;
                    break;
                }
            }
        }
        int x0 = 0, x1 = w - 1, low = -1;
        while (x0 < w && foot[x0] < 0) ++x0;
        while (x1 > x0 && foot[x1] < 0) --x1;
        for (int x = x0; x <= x1; ++x) low = std::max(low, foot[x]);
        double sum = 0;
        int n = 0;
        for (int x = x0; x <= x1; ++x) {
            if (foot[x] >= low - 2) sum += x, ++n;
        }
        if (n > 0 && x1 - x0 > 8) {
            const double at = (sum / n - x0) / (x1 - x0);
            axis = at > 0.58 ? 1 : at < 0.42 ? -1 : 0;
        }
    }
    axisOf.emplace(texture, axis);
    return axis;
}

// A field of an asset's .meta ("interior=assets/…"), or "".
std::string AssetField(const Project& project, const std::string& asset, const std::string& key) {
    std::ifstream in(project.Resolve(asset + ".meta"));
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind(key + "=", 0) == 0) return line.substr(key.size() + 1);
    }
    return {};
}

std::optional<Entity> PlaceObject(Registry& registry, PrefabLibrary& prefabs, const json& o, const json& choice,
                                  const PlaceSceneOptions& options) {
    std::string prefab = choice.value("prefab", "");
    size_t variant = 0;
    if (const json many = choice.value("prefabs", json::array()); many.is_array() && many.size() > 1) {
        // Its place among its kind (the caller numbers them, neighbours apart), else by its cell.
        const unsigned v = o.contains("variant") ? o["variant"].get<unsigned>() : CellVariety(o.value("x", 0) * 3 + 11, o.value("y", 0) * 5 + 7);
        variant = v % many.size();
        prefab = many[variant].get<std::string>();
    }
    const json look = choice.value("look", json::object()); // its role's (the ontology's)
    // A thing with a front shows it as it faces: its art faces the viewer's lower left (south); facing east, that
    // mirrored; facing north or west, its back view (mirrored for west). Without a back view, its front, mirrored.
    const std::string orient = look.value("orient", "none");
    bool mirrored = false;
    if (orient != "none" && o.value("layer", "prop") != "building") {
        const std::string f = o.value("facing", "s");
        mirrored = f == "e" || f == "w";
        if (f == "n" || f == "w") {
            const json backs = choice.value("backs", json::array());
            if (variant < backs.size() && backs[variant].is_string() && !backs[variant].get<std::string>().empty()) prefab = backs[variant].get<std::string>();
        }
    }
    if (prefab.empty()) return std::nullopt;
    const Stand stand = StandOf(o, options);
    // A wood of one tree drawn three hundred times mustn't look stamped (place-refine.md §7):
    // each natural thing is moved a little in its cell, turned the other way half the time (unless
    // it faces a way), and a little bigger or smaller.
    const bool varied = look.value("vary", false);
    const unsigned v = CellVariety(o.value("x", 0) + 7919, o.value("y", 0) + 104729);
    // A solid thing moves less, so it stays on the cell it blocks.
    const float spread = choice.value("solid", false) ? 0.3f : 0.7f;
    const float jx = varied ? (static_cast<float>(v % 97) / 96.0f - 0.5f) * spread : 0.0f; // up to a third of a cell: no lattice
    const float jy = varied ? (static_cast<float>((v >> 7) % 97) / 96.0f - 0.5f) * spread : 0.0f;
    // Where its foot lands. A sprite stands with its bottom a quarter cell (of height) below its point; art drawn to
    // the grid (matched art) has its lowest pixel where its footprint's front edge is: for a building or a run's piece
    // the front corner, half a cell below its front cell's middle; for a thing, half its width (in cells) below its
    // middle. So it's set forward (along x and y) by half its width less a quarter: a building, a wall piece, a car a
    // quarter of a cell, a lamp not at all. Otherwise everything floats a little above the ground.
    float seat = 0.0f;
    if (choice.value("seated", false) || (choice.value("drawnToGrid", false) && o.value("layer", "prop") == "building")) {
        seat = 0.25f;
    } else if (choice.value("drawnToGrid", false)) {
        float across = 1.0f;
        prefabs.VisitTemplate(prefab, [&](Registry& r, Entity t) {
            if (const auto* s = r.try_get<Sprite>(t); s && s->width > 0) across = s->width / options.iso.tileWidth;
        });
        seat = std::clamp(across / 2.0f - 0.25f, 0.0f, 0.25f);
    }
    auto placed = prefabs.Instantiate(registry, prefab, stand.x + jx + seat, stand.y + jy + seat, false);
    if (!placed) return std::nullopt;
    const Entity e = *placed;
    registry.emplace_or_replace<PlaceNode>(e, options.place, o.value("node", ""), o.value("id", ""));
    const std::string layer = o.value("layer", "prop");
    if (look.value("doorway", false) && options.doorAcrossX && choice.value("seated", false)) {
        // A door block (an edit of the wall block: its door on its face along x) turns with its wall: across a corridor
        // running along x it stands along y, mirrored.
        if (options.doorAcrossX(o.value("x", 0), o.value("y", 0))) MirrorEntity(EngineReflection(), registry, e);
    } else if (look.value("doorway", false) && options.doorAcrossX) {
        // The doorway's way (from the layout, done by the caller) against the picture's.
        const bool acrossX = options.doorAcrossX(o.value("x", 0), o.value("y", 0));
        if (const auto* sprite = registry.try_get<Sprite>(e)) {
            // The way the picture's door runs, from its foot (measured once): a door is tall, so its
            // long axis says nothing; its base, seen at an angle, slants. Lower at the right: it runs
            // down to the right, along the grid's x.
            static std::map<std::string, int> slantOf;
            auto it = slantOf.find(sprite->texture);
            if (it == slantOf.end()) {
                int slant = 0;
                if (const auto image = DecodeImage(prefabs.GetProject().Resolve(sprite->texture)); image && image->width > 4) {
                    const int w = static_cast<int>(image->width), h = static_cast<int>(image->height);
                    auto lowest = [&](int x) {
                        for (int y = h - 1; y >= 0; --y) {
                            if (image->rgba[(static_cast<size_t>(y) * w + x) * 4 + 3] > 128) return y;
                        }
                        return -1;
                    };
                    int x0 = 0, x1 = w - 1;
                    while (x0 < w && lowest(x0) < 0) ++x0;
                    while (x1 > x0 && lowest(x1) < 0) --x1;
                    if (x1 - x0 > 4) {
                        const int left = lowest(x0 + (x1 - x0) / 5), right = lowest(x1 - (x1 - x0) / 5);
                        const int tolerance = std::max(2, (x1 - x0) / 20);
                        slant = right - left > tolerance ? 1 : left - right > tolerance ? -1 : 0;
                    }
                }
                it = slantOf.emplace(sprite->texture, slant).first;
            }
            // A door across a corridor running along x stands along y: mirrored, if it's drawn along x.
            // (Drawn facing the viewer, slant 0: no way to turn it; the critic asks for it again.)
            const bool drawnAlongX = it->second > 0;
            if (it->second != 0 && drawnAlongX == acrossX) MirrorEntity(EngineReflection(), registry, e);
        }
    }
    if (orient == "along" && layer != "building") {
        // Painted along either of the grid's ways (a car's front to the lower left or the lower right): mirrored when its
        // picture runs the other way from its facing. No telling: as if drawn facing south.
        if (const auto* sprite = registry.try_get<Sprite>(e)) {
            if (const int axis = PictureAxis(prefabs, sprite->texture); axis != 0) {
                const std::string f = o.value("facing", "s");
                mirrored = (f == "e" || f == "w") != (axis > 0);
            }
        }
    }
    if (mirrored) MirrorEntity(EngineReflection(), registry, e);
    if (varied) {
        if (orient == "none" && ((v >> 14) & 1)) MirrorEntity(EngineReflection(), registry, e);
        // Trees from saplings to old giants (0.78 to 1.30): a wood reads as a mass of crowns, not a
        // grid of one tree; smaller things vary less.
        const bool tree = choice.value("role", "") == "tree" || o.value("slot", "") == "tree";
        const float scale = tree ? 0.78f + static_cast<float>((v >> 16) % 53) / 100.0f : 0.88f + static_cast<float>((v >> 16) % 25) / 100.0f;
        if (std::abs(scale - 1.0f) > 0.02f) ScaleEntity(EngineReflection(), registry, e, scale);
    }
    // A thing bigger than a cell (stairs up to another level, 2 x 3: the plan's footprint) at least nearly as wide as its
    // footprint, its art drawn for a smaller one.
    if (layer != "building" && (o.value("w", 1) > 1 || o.value("h", 1) > 1)) {
        if (const auto* sprite = registry.try_get<Sprite>(e); sprite && sprite->width > 0) {
            const float want = FootprintPixels(o.value("w", 1), o.value("h", 1), options) * 0.9f;
            if (sprite->width < want) ScaleEntity(EngineReflection(), registry, e, want / sprite->width);
        }
    }
    // A solid thing (its role's, roles.json) blocks its whole footprint; one that isn't, nothing, whatever its prefab
    // says (a prefab made earlier is reused as it is).
    if (layer != "building" && choice.contains("solid")) {
        if (choice.value("solid", false)) {
            auto& obstacle = registry.emplace_or_replace<Obstacle>(e);
            obstacle.left = stand.left;
            obstacle.right = stand.right;
            obstacle.back = stand.back;
            for (const char* field : {"left", "right", "back"}) RecordOverride(registry, e, "Obstacle", field);
        } else if (registry.all_of<Obstacle>(e)) {
            registry.remove<Obstacle>(e);
            RecordOverride(registry, e, "Obstacle", ""); // removed from its prefab's
        }
    }
    // A building is as wide as its footprint (unless its art is drawn to the grid), and blocks all of it.
    if (layer == "building") {
        if (const auto* sprite = registry.try_get<Sprite>(e); sprite && sprite->width > 0 && !choice.value("drawnToGrid", false)) {
            const float factor = FootprintPixels(o.value("w", 1), o.value("h", 1), options) / sprite->width;
            if (std::abs(factor - 1.0f) > 0.02f) ScaleEntity(EngineReflection(), registry, e, factor);
        }
        auto& obstacle = registry.emplace_or_replace<Obstacle>(e);
        obstacle.left = stand.left;
        obstacle.right = stand.right;
        obstacle.back = stand.back;
        for (const char* field : {"left", "right", "back"}) RecordOverride(registry, e, "Obstacle", field);
        // Its interior (docs/places/interiors.md): its own scene (built later, through its door), or the art's pair (its
        // .meta's interior) shown cut away, walked into through the door on its front.
        const json look = choice.value("look", json::object());
        if (look.value("has_interior", true) && look.value("interior_kind", std::string()) == "scene") {
            auto& interior = registry.emplace_or_replace<Interior>(e);
            interior.kind = "scene";
            interior.door = o.value("facing", std::string("s"));
        } else if (const auto* sprite = registry.try_get<Sprite>(e); sprite && !sprite->texture.empty()) {
            if (const std::string inside = AssetField(prefabs.GetProject(), sprite->texture, "interior"); !inside.empty()) {
                auto& interior = registry.emplace_or_replace<Interior>(e);
                interior.texture = inside;
                interior.floors = AssetField(prefabs.GetProject(), sprite->texture, "interiorFloors");
                interior.door = o.value("facing", std::string("s"));
            }
        }
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
                     const AssetDatabase& assets, const TerrainSet& terrains, const PlaceSceneOptions& options, const AssetSourcing* sourcing) {
    std::map<std::string, std::pair<Need, int>> needs; // key -> need, uses
    json out = {{"needs", json::array()}, {"terrains", json::object()}, {"pieces", json::object()}, {"byObject", json::object()},
                {"pending", json::array()}};
    // The place's look: its build type's (its light, the parts it stands up), and each ground's.
    const BuildType* buildType = spec.contains("root") ? ontology.BuildOfRoot(spec["root"].value("module", "")) : nullptr;
    const json placeLook = buildType && buildType->look.is_object() ? buildType->look : json::object();
    out["look"] = {{"lighting", placeLook.value("lighting", "sun")}, {"parts", placeLook.value("parts", json::array())}, {"terrains", json::object()}};
    {
        const TerrainGrid g = ReadTerrain(solved);
        std::set<std::string> grounds(g.cells.begin(), g.cells.end());
        if (!g.bridges.empty()) grounds.insert("dirt");
        for (const auto& [id, scheme] : ontology.Schemes()) {
            if (id != "terrain-kind") continue;
            for (const auto& c : scheme) {
                if (c.look.is_object() && c.look.value("shore", false)) grounds.insert(c.id);
            }
        }
        for (const auto& t : grounds) {
            json l = json::object();
            for (const char* k : {"tone", "made", "water", "solid", "blocksLight", "hills", "shore"}) {
                if (const json v = ontology.ConceptLook("terrain-kind", t, k); !v.is_null()) l[k] = v;
            }
            out["look"]["terrains"][t] = l;
        }
    }
    if (sourcing) {
        json extra = json::array();
        for (const auto& l : sourcing->settings.licences.extra) extra.push_back(l);
        out["licencePolicy"] = {{"shareAlike", sourcing->settings.licences.shareAlike}, {"extra", extra}};
    }
    // The policy of the component a need is used in: the nearest of its node's
    // ancestors the Brief names (a garden in a ward: the garden's, else the ward's).
    auto policyOf = [&](const std::string& node) -> std::string {
        if (sourcing == nullptr) return "auto";
        std::string path = node;
        json specCopy = spec;
        for (int guard = 0; guard < 8 && !path.empty(); ++guard) {
            const std::string base = path.substr(0, path.find('#'));
            if (const json* n = SpecNode(specCopy, base)) {
                const json source = BriefValue(sourcing->brief, "components." + n->value("module", "") + ".assets.source");
                // "auto" (what a Brief says unless asked otherwise) is the build's default; in a Planner
                // build, the plan's strategy (only a placeholder asked for stands).
                if (source.is_string() && !source.get<std::string>().empty() && source.get<std::string>() != "auto" &&
                    (!sourcing->strategyRules || source.get<std::string>() == "placeholder")) {
                    return source.get<std::string>();
                }
            }
            const size_t slash = path.find_last_of('/');
            if (slash == std::string::npos) break;
            path = path.substr(0, slash);
        }
        return sourcing->settings.defaultSource.empty() ? std::string("auto") : sourcing->settings.defaultSource;
    };
    // What the Brief wishes for a need's art (components.<id>.assets.<field>: "variants", how many
    // different pieces of art its things get; "style", words for how they look): the nearest of its
    // node's ancestors that says.
    auto wishOf = [&](const std::string& node, const std::string& field) -> json {
        if (sourcing == nullptr) return json();
        std::string path = node;
        json specCopy = spec;
        for (int guard = 0; guard < 8 && !path.empty(); ++guard) {
            if (const json* n = SpecNode(specCopy, path.substr(0, path.find('#')))) {
                const json v = BriefValue(sourcing->brief, "components." + n->value("module", "") + ".assets." + field);
                if (!v.is_null()) return v;
            }
            const size_t slash = path.find_last_of('/');
            if (slash == std::string::npos) break;
            path = path.substr(0, slash);
        }
        return json();
    };
    // The component a need is for: the nearest of its node's ancestors the Brief has, else its
    // zone's module; never the place itself (its dressing has no component's source to set).
    const std::string rootModule = spec.contains("root") ? spec["root"].value("module", "") : std::string();
    auto componentOf = [&](const std::string& node) -> std::string {
        std::string path = node, last;
        json specCopy = spec;
        const json components = sourcing ? sourcing->brief.value("components", json::object()) : json::object();
        for (int guard = 0; guard < 8 && !path.empty(); ++guard) {
            const std::string base = path.substr(0, path.find('#'));
            if (const json* n = SpecNode(specCopy, base)) {
                const std::string module = n->value("module", "");
                if (module == rootModule) break;
                if (components.contains(module)) return module;
                last = module;
            }
            const size_t slash = path.find_last_of('/');
            if (slash == std::string::npos) break;
            path = path.substr(0, slash);
        }
        return last;
    };
    std::map<std::string, int> paintedUses; // a painted place's things in its painting, by need
    for (const auto& o : solved.value("objects", json::array())) {
        Need need = NeedOf(ontology, spec, o);
        out["byObject"][o.value("id", "")] = need.key;
        if (options.paintedIds.contains(o.value("id", ""))) ++paintedUses[need.key];
        auto& entry = needs[need.key];
        if (entry.second == 0) entry.first = need;
        entry.second++;
    }
    auto slotFor = [](const Need& need) {
        SlotNeed slot;
        slot.role = need.role;
        slot.concepts = need.concepts;
        if (need.kind == "building") {
            slot.minCells = std::max(1.0f, need.w * 0.6f);
            slot.maxCells = need.w * 1.6f + 1;
            slot.layer = "";
        }
        return slot;
    };
    // Coherent art (2026-10-21): the folder (a pack) most needs' best art comes from is preferred
    // for the others too, among art that fits as well.
    std::map<std::string, int> folderVotes;
    auto folderOf = [](const CatalogEntry* e) { return e ? fs::path(e->path).parent_path().generic_string() : std::string(); };
    for (const auto& [key, item] : needs) {
        const Retrieval f = RetrieveForSlot(catalog, ontology, slotFor(item.first), options.isoGrid);
        if (!f.candidates.empty() && f.candidates.front().entry && !f.candidates.front().grade.empty()) ++folderVotes[folderOf(f.candidates.front().entry)];
    }
    // A need in words: its name ("Cottage house") and what it's for, for the list, the web search
    // and the Art tool.
    auto label = [](std::string id) {
        std::replace(id.begin(), id.end(), '-', ' ');
        if (!id.empty()) id[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(id[0])));
        return id;
    };
    auto titleOf = [&](const Need& need) {
        std::string concepts;
        for (const auto& c : need.concepts) concepts += (concepts.empty() ? "" : " ") + c;
        std::string role = need.role;
        std::replace(role.begin(), role.end(), '-', ' ');
        const std::string both = concepts.empty() ? role : concepts.find(role) != std::string::npos ? concepts : concepts + " " + role;
        return label(both);
    };
    for (const auto& [key, item] : needs) {
        const auto& [need, uses] = item;
        const SlotNeed slot = slotFor(need);
        Retrieval found = RetrieveForSlot(catalog, ontology, slot, options.isoGrid);
        for (auto& c : found.candidates) {
            const int votes = folderVotes[folderOf(c.entry)];
            if (votes > 1 && !c.grade.empty()) c.fit = std::min(1.0f, c.fit + std::min(0.15f, 0.05f * static_cast<float>(votes - 1)));
        }
        std::stable_sort(found.candidates.begin(), found.candidates.end(), [](const Candidate& a, const Candidate& b) { return a.fit > b.fit; });
        json row = {{"key", key}, {"role", need.role}, {"concepts", need.concepts}, {"layer", need.layer}, {"w", need.w},
                    {"h", need.h}, {"uses", uses}, {"component", componentOf(need.node)}};
        // How a thing of its role shows (the ontology's, place-refine.md §13): its size for its
        // shadow, whether it varies (a plant), gives light (a torch), or fills a doorway.
        {
            json look = {{"size", ontology.RoleSize(need.role)}, {"tall", ontology.RoleTall(need.role)}};
            for (const char* k : {"vary", "light", "doorway", "orient", "run", "relations", "has_interior", "interior_kind"}) {
                if (const json v = ontology.RoleLook(need.role, k); !v.is_null()) look[k] = v;
            }
            // Its interior as the Build panel chose for its building type (docs/places/interiors.md §1).
            if (const std::string kind = InteriorKindOf(&ontology, sourcing ? sourcing->interiors : json::object(), need.role); !kind.empty()) {
                look["has_interior"] = kind != "none";
                look["interior_kind"] = kind == "none" ? "cutaway" : kind;
            }
            row["look"] = look;
        }
        {
            // Its name and what it's for (the part it's in, its size).
            json specCopy = spec;
            const json* part = need.node.empty() ? nullptr : SpecNode(specCopy, need.node.substr(0, need.node.find('#')));
            const ModuleDef* pm = part ? ontology.Module(part->value("module", "")) : nullptr;
            const std::string title = titleOf(need);
            std::string what = title;
            for (auto& ch : what) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            row["title"] = title;
            row["description"] = "A " + what + (pm ? " in a " + label(pm->name) : std::string()) +
                                 (need.kind == "building" ? ": a building of about " + std::to_string(need.w) + " by " + std::to_string(need.h) + " cells"
                                  : need.layer == "decal" ? ": a small thing lying flat on the ground"
                                                          : ": " + std::string(need.essential ? "a prop" : "a small prop, for dressing")) +
                                 (pm && !pm->description.empty() ? ". " + pm->description : std::string());
        }
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
        // Blocks walking: a building, or a thing whose role is solid (roles.json: a statue, a well, a cart).
        const bool solid = need.kind == "building" || ontology.IsSolid(need.role);
        row["solid"] = solid;
        const NeedProfile profile = ProfileOf(need.layer, need.essential, false, uses);
        row["importance"] = ImportanceName(profile.importance);
        row["styleCritical"] = profile.styleCritical;
        auto take = [&](const Candidate& c) {
            // The user's library: into the project first (its file is outside it).
            CatalogEntry copied;
            const CatalogEntry* entry = c.entry;
            if (entry->id.rfind("user:", 0) == 0) {
                if (!sourcing || !sourcing->importLibrary) return false;
                const std::string path = sourcing->importLibrary(entry->id.substr(5));
                if (path.empty()) return false;
                copied = *entry;
                copied.path = copied.id = path;
                entry = &copied;
            }
            const std::string prefab = PrefabForEntry(prefabs, assets, *entry, solid, options);
            if (prefab.empty()) return false;
            row["prefab"] = prefab;
            row["entry"] = entry->id;
            row["source"] = c.entry->source;
            row["grade"] = c.grade;
            row["fit"] = c.fit;
            row["licence"] = c.entry->licence;
            row["author"] = c.entry->author;
            row["url"] = c.entry->url;
            row["name"] = c.entry->name;
            return true;
        };
        if (sourcing) {
            // The decision procedure (place-assets.md §5), by the component's policy and the user's choice.
            const json choice = sourcing->overrides.is_object() ? sourcing->overrides.value(key, json::object()) : json::object();
            const std::string policy = choice.value("source", policyOf(need.node));
            row["policy"] = policy;
            row["override"] = choice.value("source", "");
            if (const std::string picked = choice.value("entry", ""); !picked.empty()) {
                // Chosen by the user: that art, whatever else fits.
                const CatalogEntry* e = catalog.Find(picked);
                CatalogEntry own;
                if (e == nullptr) {
                    own.id = own.path = picked;
                    own.source = "project";
                    own.name = fs::path(picked).stem().string();
                    own.spriteW = static_cast<float>(need.kind == "building" ? need.w : 1);
                    e = &own;
                }
                Candidate c;
                c.entry = e;
                c.grade = "good";
                c.fit = 1.0f;
                if (take(c)) {
                    row["decision"] = {{"step", "U"}, {"action", "use"}, {"reason", "chosen by you"}, {"warn", false}};
                    row["source"] = "picked";
                    out["needs"].push_back(row);
                    continue;
                }
            }
            if (const auto p = paintedUses.find(key); p != paintedUses.end() && p->second >= uses) {
                // Every one of them is in the place's painting (painted-places.md): no art.
                row["decision"] = {{"step", "P"}, {"action", "use"}, {"reason", "in the painting"}, {"warn", false}};
                row["source"] = "painted";
                out["needs"].push_back(row);
                continue;
            }
            const Role* roleDef = ontology.FindRole(need.role.substr(0, need.role.find('|')));
            if ((!options.groundPicture.empty() || !options.paintedFolder.empty()) && roleDef != nullptr && roleDef->affordance == "terrain") {
                // Ground (water, a road, a square): the background has it painted; no art, nothing put down.
                row["decision"] = {{"step", "B"}, {"action", "use"}, {"reason", "painted in the background"}, {"warn", false}};
                row["source"] = "background";
                out["needs"].push_back(row);
                continue;
            }
            if (policy == "placeholder") {
                row["decision"] = {{"step", "U"}, {"action", "placeholder"}, {"reason", "a placeholder, as you chose"}, {"warn", false}};
            } else {
            SourceCandidates candidates;
            candidates.project = found.candidates;
            if (!sourcing->library.entries.empty()) candidates.library = RetrieveForSlot(sourcing->library, ontology, slot, options.isoGrid).candidates;
            for (const auto& c : found.candidates) {
                if (c.entry && c.entry->source == "web") candidates.web.push_back(c);
            }
            const auto tried = sourcing->tried.contains(key) ? sourcing->tried.at(key) : Tried{};
            // Made in one style for this place (Make the art: assets/matched/<place>/): it, whatever the policy.
            const std::string madeHere = "assets/matched/" + options.place + "/";
            // The piece made for this very need first (its variantKey, its first look), else the best fit of those made
            // here; never a back view (only its front's, through backOf). Its other looks are mixed in below (variantKey).
            const Candidate* matched = nullptr;
            int best = -1;
            for (const auto& c : found.candidates) {
                if (!c.entry || c.entry->path.rfind(madeHere, 0) != 0 || tried.refused.contains(c.entry->path)) continue;
                const AssetInfo* a = assets.Find(c.entry->path);
                auto field = [&](const char* k) {
                    if (a == nullptr) return std::string();
                    const auto f = a->analysis.find(k);
                    return f == a->analysis.end() ? std::string() : f->second;
                };
                if (!field("backOf").empty()) continue;
                const int rank = (field("variantKey") == key ? 2 : 0) + (field("variant").empty() || field("variant") == "1" ? 1 : 0);
                if (rank > best || (rank == best && c.fit > matched->fit)) {
                    matched = &c;
                    best = rank;
                }
            }
            if (matched && !options.place.empty() && !choice.value("exclude", false) && take(*matched)) {
                row["decision"] = {{"step", "M"}, {"action", "use"}, {"reason", "made in one style for this place"}, {"warn", false}};
            } else {
            Decision d = choice.value("exclude", false) ? Decision{"A7", "drop", "none", std::nullopt, "excluded by you", false}
                                                        : Decide(policy, profile, candidates, sourcing->settings.licences,
                                                                 sourcing->settings.webSearch || policy == "web", sourcing->canGenerate, tried);
            // Sketched: made from the place's painting (docs/build-fixes.md), unless that was tried.
            if (sourcing->sketched.contains(key) && sourcing->canGenerate && !tried.generate && !choice.value("exclude", false)) {
                d = Decision{"K", "generate", "generated", std::nullopt, "drawn from the place's sketch", false};
            }
            row["decision"] = {{"step", d.step}, {"action", d.action}, {"reason", d.reason}, {"warn", d.warn}};
            if (d.candidate && d.candidate->entry && take(*d.candidate)) {
                // A library asset is the project's from now on (copied when the build runs).
                if (d.action == "import" && d.candidate->entry->source == "library") row["import"] = d.candidate->entry->path;
                // Found art, not yet seen by the vision model for this place: looked at before it's used.
                const std::string entry = row.value("entry", "");
                if (sourcing->checkFound && sourcing->canGenerate && !entry.empty() && !tried.approved.contains(entry) &&
                    !tried.approved.contains(d.candidate->entry->path)) {
                    std::string concepts;
                    for (const auto& c : need.concepts) concepts += (concepts.empty() ? "" : " ") + c;
                    const std::string one = need.role.substr(0, need.role.find('|'));
                    const std::string what = concepts.empty() ? one : concepts + " " + one;
                    json check = {{"key", key}, {"action", "check"}, {"entry", entry}, {"candidate", d.candidate->entry->id}, {"role", need.role}, {"title", what},
                                  {"concepts", need.concepts}, {"layer", need.layer}, {"w", need.w}, {"h", need.h}, {"kind", need.kind},
                                  {"about", row.value("description", "")},
                                  {"description", "a " + what + " for a game, " + std::string(options.isoGrid ? "isometric" : "top-down") + " view"}};
                    const ImageRect& r = d.candidate->entry->rect;
                    if (entry.find('#') != std::string::npos && r.w > 0 && r.h > 0) check["rect"] = {r.x, r.y, r.w, r.h};
                    out["pending"].push_back(check);
                    row["why"] = "found; being checked against the scene";
                }
            } else if (d.action == "drop") {
                row["source"] = "dropped";
                row["why"] = d.reason;
            } else if (d.action == "generate" || d.action == "web") {
                std::string concepts;
                for (const auto& c : need.concepts) concepts += (concepts.empty() ? "" : " ") + c;
                // A role of alternatives ("barrel|pot|bench"): any will do; the art is asked for as the first.
                const std::string one = need.role.substr(0, need.role.find('|'));
                const std::string what = (concepts.empty() ? one : concepts + " " + one);
                const std::string view = options.isoGrid ? "isometric" : "top-down";
                json queries = json::array();
                if (!concepts.empty()) queries.push_back(concepts + " " + one);
                queries.push_back(one);
                for (const auto& c : need.concepts) queries.push_back(c);
                out["pending"].push_back({{"key", key}, {"action", d.action}, {"role", need.role}, {"title", what}, {"concepts", need.concepts},
                                          {"layer", need.layer}, {"w", need.w}, {"h", need.h}, {"kind", need.kind},
                                          {"query", view + " " + what}, {"queries", queries}, {"about", row.value("description", "")},
                                          {"description", "a " + what + " for a game, " + view + " view, " +
                                                              (need.kind == "building" ? "a building about " + std::to_string(need.w) + " by " +
                                                                                             std::to_string(need.h) + " cells"
                                                                                       : "a small " + need.kind)}});
                // The Brief's wishes for it: so many different pieces, and how they look.
                if (const json n = wishOf(need.node, "variants"); n.is_number() && n.get<int>() > 1) out["pending"].back()["variants"] = std::min(4, n.get<int>());
                if (const json st = wishOf(need.node, "style"); st.is_string() && !st.get<std::string>().empty()) out["pending"].back()["style"] = st;
                row["why"] = d.action == "generate" ? "to be made by the Art tool" : "to be found on the web";
            }
            }
            }
        } else {
            for (const auto& c : found.candidates) {
                if (c.entry && !c.grade.empty() && take(c)) break;
            }
        }
        // More than one piece of art for its things (the Brief's "variants"): the good ones the
        // catalog has for it, mixed across the place.
        if (const json n = wishOf(need.node, "variants"); row.contains("prefab") && n.is_number() && n.get<int>() > 1) {
            json prefabsOf = json::array({row["prefab"]});
            std::set<std::string> seenEntries = {row.value("entry", "")};
            for (const auto& c : found.candidates) {
                if (static_cast<int>(prefabsOf.size()) >= std::min(4, n.get<int>())) break;
                if (!c.entry || c.grade != "good" || seenEntries.contains(c.entry->id) || c.entry->id.rfind("user:", 0) == 0) continue;
                if (const std::string prefab = PrefabForEntry(prefabs, assets, *c.entry, solid, options); !prefab.empty()) {
                    prefabsOf.push_back(prefab);
                    seenEntries.insert(c.entry->id);
                }
            }
            if (prefabsOf.size() > 1) row["prefabs"] = prefabsOf;
        }
        if (!row.contains("prefab") && row.value("source", "") != "dropped") {
            // Nothing fits (or it's still to be made): a placeholder, and why (the best rejection).
            const float width = need.kind == "building" ? FootprintPixels(need.w, need.h, options)
                                : need.kind == "decal"  ? options.iso.tileWidth * 0.35f
                                                        : options.iso.tileWidth * 0.6f;
            const std::string label = need.concepts.empty() ? need.role : need.concepts.front();
            const std::string prefab = PlaceholderPrefab(prefabs, need.kind, label + (need.kind == "building" ? "-" + std::to_string(need.w) + "x" + std::to_string(need.h) : ""),
                                                         width, width, solid, options.iso);
            row["prefab"] = prefab;
            row["source"] = "placeholder";
            if (!row.contains("why")) {
                row["why"] = found.candidates.empty() ? (found.rejected.empty() ? "no art in the project for it"
                                                                                 : "nothing fits: " + found.rejected.front().reason)
                                                      : "no candidate could be made a prefab";
            }
        }
        // Matched art (MatchedArt) is drawn to the grid already: as wide as its sheet had it, standing where
        // the builder stands it; not scaled again.
        if (const AssetInfo* a = row.contains("entry") ? assets.Find(row.value("entry", "")) : nullptr) {
            if (const auto s = a->analysis.find("matched"); s != a->analysis.end() && s->second == "1") row["drawnToGrid"] = true;
            // A cell of a painted run (a door cut from its wall): its foot is its cell's front corner.
            if (const auto r = a->analysis.find("run"); r != a->analysis.end() && r->second == "1") row["seated"] = true;
            // Its kind's other looks (made together: variantKey) mixed across the place, and each one's back view
            // (backOf) for a thing that faces away.
            if (const auto vk = a->analysis.find("variantKey"); vk != a->analysis.end() && !vk->second.empty()) {
                auto field = [](const AssetInfo& x, const char* k) {
                    const auto f = x.analysis.find(k);
                    return f == x.analysis.end() ? std::string() : f->second;
                };
                std::vector<std::pair<int, std::string>> fronts;
                std::map<std::string, std::string> backOf;
                for (const AssetInfo& x : assets.Assets()) {
                    if (field(x, "variantKey") != vk->second) continue;
                    if (const std::string b = field(x, "backOf"); !b.empty()) backOf[b] = x.path;
                    else fronts.push_back({std::atoi(field(x, "variant").c_str()), x.path});
                }
                std::sort(fronts.begin(), fronts.end());
                json prefabsOf = json::array(), backs = json::array();
                bool anyBack = false;
                for (const auto& [n, path] : fronts) {
                    const CatalogEntry* e = catalog.Find(path);
                    const std::string prefab = e ? PrefabForEntry(prefabs, assets, *e, solid, options) : std::string();
                    if (prefab.empty()) continue;
                    prefabsOf.push_back(prefab);
                    std::string back;
                    if (const auto b = backOf.find(path); b != backOf.end()) {
                        if (const CatalogEntry* be = catalog.Find(b->second)) back = PrefabForEntry(prefabs, assets, *be, solid, options);
                    }
                    anyBack = anyBack || !back.empty();
                    backs.push_back(back);
                }
                if (prefabsOf.size() > 1) row["prefabs"] = prefabsOf;
                if (!prefabsOf.empty()) row["prefab"] = prefabsOf[0];
                if (anyBack) row["backs"] = backs;
            }
        }
        out["needs"].push_back(row);
    }
    // The parts the build type stands up (place-refine.md §12, its look's "parts": a dungeon's wall
    // block on each wall cell behind the floor, its torches where its lights are). The Art tool
    // makes them when the project has none.
    {
        const TerrainGrid g = ReadTerrain(solved);
        const json grounds = out["look"]["terrains"];
        const bool indoor = out["look"].value("lighting", "sun") == "indoor";
        const json lines = solved.value("lines", json::array()); // named: begin() and end() of one array
        const bool wallLines = std::any_of(lines.begin(), lines.end(), [](const json& l) { return l.value("slot", "") == "wall"; });
        const bool walls = std::any_of(g.cells.begin(), g.cells.end(), [&](const std::string& t) {
            return grounds.value(t, json::object()).value("blocksLight", false);
        }) || (wallLines && [&] {
            for (const auto& p : out["look"]["parts"]) {
                if (p.value("at", "") == "wall-edge") return true;
            }
            return false;
        }());
        (void)indoor;
        for (const auto& part : out["look"]["parts"]) {
            const std::string partKey = part.value("key", ""), role = part.value("role", partKey);
            if (partKey.empty() || !walls || !options.paintedFolder.empty()) continue; // a painted place's walls are painted
            const std::string key = "part|" + partKey;
            const Role* r = ontology.FindRole(role);
            const std::string word = r ? r->label : role;
            json row = {{"role", role}, {"at", part.value("at", "")}};
            {
                json look = {{"size", ontology.RoleSize(role)}, {"tall", ontology.RoleTall(role)}};
                for (const char* k : {"vary", "light", "doorway", "orient", "run", "relations"}) {
                    if (const json v = ontology.RoleLook(role, k); !v.is_null()) look[k] = v;
                }
                row["look"] = look;
            }
            // The newest made for it; a piece of a run (MatchedArt's kit: flat-sided, so copies join) before a lone block.
            std::string made, madeEnd;
            bool madeRun = false;
            for (const AssetInfo& a : assets.Assets()) {
                auto of = a.analysis.find("partOf"), roles = a.analysis.find("roles"), run = a.analysis.find("run");
                if (of == a.analysis.end() || of->second != partKey || roles == a.analysis.end() || roles->second.empty()) continue;
                if (run != a.analysis.end() && run->second == "end") {
                    if (madeEnd.empty() || a.path > madeEnd) madeEnd = a.path;
                    continue;
                }
                const bool isRun = run != a.analysis.end() && run->second == "1";
                if (made.empty() || isRun > madeRun || (isRun == madeRun && a.path > made)) made = a.path, madeRun = isRun;
            }
            if (madeRun) row["run"] = true;
            // A run's end (its end painted), for the cell where a run stops.
            if (const CatalogEntry* e = madeRun && !madeEnd.empty() ? catalog.Find(madeEnd) : nullptr) {
                if (const std::string prefab = PrefabForEntry(prefabs, assets, *e, false, options); !prefab.empty()) row["endPrefab"] = prefab;
            }
            if (const CatalogEntry* e = made.empty() ? nullptr : catalog.Find(made)) {
                if (const std::string prefab = PrefabForEntry(prefabs, assets, *e, false, options); !prefab.empty()) {
                    row["prefab"] = prefab;
                    row["source"] = "generated";
                    row["entry"] = made;
                }
            }
            if (!row.contains("prefab")) {
                bool pending = false;
                if (sourcing && sourcing->canGenerate) {
                    const auto tried = sourcing->tried.find(key);
                    const json override = sourcing->overrides.value(key, json::object());
                    pending = (tried == sourcing->tried.end() || !tried->second.generate) && !override.value("exclude", false);
                }
                if (pending) {
                    out["pending"].push_back({{"key", key}, {"action", "generate"}, {"role", role}, {"title", word},
                                              {"concepts", json::array({word})}, {"layer", "prop"}, {"w", 1}, {"h", 1}, {"kind", "prop"},
                                              {"part", partKey}, {"query", (options.isoGrid ? "isometric " : "") + word},
                                              {"description", ontology.RoleDraw(role)}});
                }
                row["source"] = "placeholder";
                row["why"] = pending ? "to be made by the Art tool" : "no " + word + " in the project";
            }
            out["parts"][partKey] = row;
        }
    }
    // Fence and wall pieces.
    std::set<std::string> lineSlots;
    for (const auto& l : solved.value("lines", json::array())) lineSlots.insert(l.value("slot", "fence"));
    const bool indoorWalls = [&] { // any place whose look stands its walls up as blocks
        for (const auto& p : placeLook.value("parts", json::array())) {
            if (p.value("at", "") == "wall-edge") return true;
        }
        return false;
    }();
    for (const auto& slotName : lineSlots) {
        if (slotName == "wall" && indoorWalls) continue; // stood up as the place's wall blocks
        if (!options.paintedFolder.empty()) {
            out["pieces"][slotName] = {{"role", slotName + "-piece-set"}, {"source", "painted"}, {"why", "in the painting"}};
            continue;
        }
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
        // The Art tool's piece (place-refine.md §5): one straight section, filed by the way it's
        // drawn (its long axis, measured: the image model doesn't always draw the diagonal it
        // was asked for); the other direction takes it mirrored, so the two always match.
        const std::string pieceKey = "piece|" + slotName;
        if (!row.contains("pieces")) {
            std::string made;
            for (const AssetInfo& a : assets.Assets()) {
                auto of = a.analysis.find("pieceSlot"), roles = a.analysis.find("roles");
                if (of == a.analysis.end() || of->second != slotName) continue;
                if (roles == a.analysis.end() || roles->second.empty()) continue; // it failed its check
                if (made.empty() || a.path > made) made = a.path;
            }
            if (const CatalogEntry* e = made.empty() ? nullptr : catalog.Find(made)) {
                if (const std::string prefab = PrefabForEntry(prefabs, assets, *e, false, options); !prefab.empty()) {
                    // Drawn down to the right (+26.6 degrees on screen): along the grid's x; up to the right: along y.
                    const bool alongY = options.isoGrid && e->shape.axisDegrees < -8.0f;
                    row["pieces"] = {{alongY ? "straight-v" : "straight-h", prefab}};
                    row["source"] = "generated";
                    row["entry"] = made;
                    row["fit"] = true; // sized to a cell's edge where it's laid
                }
            }
        }
        if (!row.contains("pieces")) {
            bool pending = false;
            if (sourcing && sourcing->canGenerate) {
                const auto tried = sourcing->tried.find(pieceKey);
                const json override = sourcing->overrides.value(pieceKey, json::object());
                pending = (tried == sourcing->tried.end() || !tried->second.generate) && !override.value("exclude", false);
            }
            if (pending) {
                const std::string what = slotName == "wall" ? "stone wall" : slotName == "hedge" ? "trimmed green hedge" : "wooden fence";
                const std::string run = options.isoGrid ? "running diagonally from the upper left down to the lower right, along the isometric grid"
                                                        : "running straight from left to right";
                out["pending"].push_back({{"key", pieceKey}, {"action", "generate"}, {"role", slot.role}, {"title", slotName + " section"},
                                          {"concepts", json::array({slotName})}, {"layer", "prop"}, {"w", 1}, {"h", 1}, {"kind", "prop"},
                                          {"piece", "straight-h"}, {"pieceSlot", slotName},
                                          {"query", (options.isoGrid ? "isometric " : "") + what + " section"},
                                          {"description", "one straight section of " + what + ", one cell long, " + run +
                                                              ", with a post at each end, so copies join end to end into a long " + slotName +
                                                              "; a single straight run only: not a closed square, not an enclosure, no corner, nothing inside it"}});
            }
            row["pieces"] = {{"straight-h", PlaceholderPrefab(prefabs, "fence", slotName, options.iso.tileWidth * 0.5f,
                                                              options.iso.tileWidth * 0.25f, true, options.iso)}};
            row["source"] = "placeholder";
            row["why"] = pending ? "to be made by the Art tool" : "no usable " + slot.role + " in the project";
        }
        out["pieces"][slotName] = row;
    }
    // The ground: each terrain of the layout, by the project's own if it has one.
    const TerrainGrid grid = ReadTerrain(solved);
    std::set<std::string> used(grid.cells.begin(), grid.cells.end());
    if (!grid.bridges.empty()) used.insert("dirt");
    used.erase("void"); // outside a building: left as it is
    for (const auto& t : used) {
        const std::string mine = MatchTerrain(terrains, t);
        if (!mine.empty()) {
            out["terrains"][t] = {{"terrain", mine}, {"source", "project"}};
            continue;
        }
        // Ground the Art tool made for it (each that passed its check), as plain tiles: the
        // latest first, the others its variants, mixed by cell.
        std::vector<std::string> madeTiles;
        std::set<std::string> sketchedTiles;
        for (const AssetInfo& a : assets.Assets()) {
            auto terrain = a.analysis.find("terrain"), source = a.analysis.find("source"), roles = a.analysis.find("roles");
            if (terrain == a.analysis.end() || terrain->second != t || source == a.analysis.end() || source->second != "generated") continue;
            if (roles == a.analysis.end() || roles->second.empty()) continue; // replaced by a refined one
            madeTiles.push_back(a.path);
            if (const auto s = a.analysis.find("fromSketch"); s != a.analysis.end() && s->second == "1") sketchedTiles.insert(a.path);
        }
        // The latest first; a patch cut from the place's own sketch before any other (its colours are
        // the painting's).
        std::sort(madeTiles.rbegin(), madeTiles.rend());
        std::stable_partition(madeTiles.begin(), madeTiles.end(), [&](const std::string& p) { return sketchedTiles.contains(p); });
        if (!madeTiles.empty() && sketchedTiles.contains(madeTiles.front())) out["look"]["terrains"][t]["sketched"] = true;
        json variants = json::array();
        for (const auto& made : madeTiles) {
            if (const std::string prefab = GeneratedTilePrefab(prefabs, made, t, SolidTerrain(t), options.iso); !prefab.empty()) {
                variants.push_back(prefab);
            }
        }
        if (!variants.empty()) {
            out["terrains"][t] = {{"prefab", variants[0]}, {"variants", variants}, {"source", "generated"}, {"entry", madeTiles.front()}};
            continue;
        }
        // A background painted whole (docs/places/background.md) is the ground: no tiles made for it (the placeholder
        // tile only carries the cell's terrain: water, solid ground).
        if (!options.groundPicture.empty() || !options.paintedFolder.empty()) {
            out["terrains"][t] = {{"prefab", PlaceholderPrefab(prefabs, "tile", t, options.iso.tileWidth, options.iso.tileHeight, SolidTerrain(t), options.iso)},
                                  {"source", "background"}, {"why", "painted in the background"}};
            continue;
        }
        // Else the Art tool makes one (with the AI, once), a placeholder meanwhile.
        const std::string key = "terrain|" + t;
        bool pending = false;
        if (sourcing && sourcing->canGenerate) {
            const auto tried = sourcing->tried.find(key);
            const json override = sourcing->overrides.value(key, json::object());
            pending = (tried == sourcing->tried.end() || !tried->second.generate) && !override.value("exclude", false);
        }
        // How many takes of it: as many as a component whose surface it is wishes for (a farmland's
        // fields of tilled soil, "more than one kind of field").
        int groundTakes = 1;
        if (sourcing) {
            const json components = sourcing->brief.value("components", json::object());
            for (const auto& [component, c] : components.items()) {
                const json n = c.is_object() ? c.value("assets", json::object()).value("variants", json()) : json();
                if (!n.is_number() || n.get<int>() < 2) continue;
                const ModuleDef* m = ontology.Module(component);
                const FieldDef* f = m ? m->Field("surface") : nullptr;
                if (f == nullptr) continue;
                const json given = c.value("params", json::object()).value("surface", json());
                const json value = given.is_string() ? given : f->defaultValue;
                if (value.is_string() && value.get<std::string>() == t) groundTakes = std::max(groundTakes, std::min(4, n.get<int>()));
            }
        }
        if (pending) {
            const std::string view = options.isoGrid ? "isometric" : "top-down";
            out["pending"].push_back({{"key", key}, {"action", "generate"}, {"role", "ground-tile"}, {"title", t + " ground"}, {"terrain", t},
                                      {"concepts", json::array()}, {"layer", "ground"}, {"w", 1}, {"h", 1}, {"kind", "tile"},
                                      {"query", view + " " + t + " ground tile"},
                                      {"description", "a seamless " + (t == "water" ? std::string("calm blue-green river water with small, gentle ripples, "
                                                                                              "only water: no shore, no land, no islands, no stones")
                                       : t == "forest-floor" ? std::string("dark, shaded forest floor of moss, fallen leaves and low undergrowth, "
                                                                           "no trees, no trunks")
                                                             : t + " ground") +
                                                          " tile for a game, " + view + " view, one cell, edges that tile with copies of itself, no objects on it"}});
            if (groundTakes > 1) out["pending"].back()["variants"] = groundTakes;
        }
        out["terrains"][t] = {{"prefab", PlaceholderPrefab(prefabs, "tile", t, options.iso.tileWidth, options.iso.tileHeight,
                                                           SolidTerrain(t), options.iso)},
                              {"source", "placeholder"},
                              {"why", pending ? "to be made by the Art tool" : "no " + t + " ground tiles in the project"}};
    }
    // What each relation means for art (docs/dungeon.md §0), for the scene (it has no ontology).
    for (const auto& [id, r] : ontology.Relations()) {
        if (r.use.is_object() && r.use.contains("art")) out["relationArt"][id] = r.use["art"];
    }
    // A wall cell with a thing mounted on it (MatchedArt's on-run kit: a torch painted on its wall), for each kind that
    // hangs on a wall (docs/dungeon.md §6): the scene puts that cell where the thing hangs, not the thing on the floor.
    {
        std::map<std::string, std::string> onWall;
        for (const AssetInfo& a : assets.Assets()) {
            if (const auto f = a.analysis.find("onWallOf"); f != a.analysis.end() && !f->second.empty() && (!onWall.contains(f->second) || a.path > onWall[f->second])) {
                onWall[f->second] = a.path;
            }
        }
        auto give = [&](json& row) {
            const auto it = onWall.find(row.value("role", ""));
            if (it == onWall.end()) return;
            if (const CatalogEntry* e = catalog.Find(it->second)) {
                if (const std::string prefab = PrefabForEntry(prefabs, assets, *e, false, options); !prefab.empty()) row["onWall"] = prefab;
            }
        };
        for (auto& row : out["needs"]) give(row);
        for (auto& [k, row] : out["parts"].items()) give(row);
    }
    return out;
}

// ③ Assets' rows: each need, and the ground (each terrain: the project's tiles, made ones, or the Art tool).
nlohmann::json AssetNeedRows(const nlohmann::json& assets) {
    nlohmann::json needs = nlohmann::json::array();
    for (const auto& n : assets.value("needs", nlohmann::json::array())) {
        if (n.value("source", "") != "background" && n.value("source", "") != "painted") needs.push_back(n); // painted already: nothing to find
    }
    const nlohmann::json terrains = assets.value("terrains", nlohmann::json::object());
    for (const auto& [t, g] : terrains.items()) {
        if (g.value("source", "") == "background") continue; // the background is the ground: no tiles needed
        nlohmann::json row = {{"key", "terrain|" + t}, {"role", "ground-tile"}, {"concepts", nlohmann::json::array({t})}, {"source", g.value("source", "")},
                              {"entry", g.value("entry", g.value("terrain", ""))}, {"why", g.value("why", "")}, {"component", "ground"}};
        if (g.value("why", "") == "to be made by the Art tool") row["decision"] = {{"action", "generate"}, {"reason", "no " + t + " ground tiles in the project"}};
        needs.push_back(row);
    }
    // A dungeon's standing parts: its wall block and its torch.
    const nlohmann::json parts = assets.value("parts", nlohmann::json::object());
    for (const auto& [part, p] : parts.items()) {
        nlohmann::json row = {{"key", "part|" + part}, {"role", p.value("role", part)}, {"concepts", nlohmann::json::array({part})},
                              {"source", p.value("source", "")}, {"entry", p.value("entry", "")}, {"why", p.value("why", "")}, {"title", part}};
        if (p.value("why", "") == "to be made by the Art tool") row["decision"] = {{"action", "generate"}, {"reason", "none in the project"}};
        needs.push_back(row);
    }
    // The lines' pieces: a fence, wall or hedge section.
    const nlohmann::json pieces = assets.value("pieces", nlohmann::json::object());
    for (const auto& [slot, p] : pieces.items()) {
        nlohmann::json row = {{"key", "piece|" + slot}, {"role", p.value("role", slot)}, {"concepts", nlohmann::json::array({slot})},
                              {"source", p.value("source", "")}, {"entry", p.value("entry", p.value("set", ""))}, {"why", p.value("why", "")},
                              {"title", slot + " section"}};
        if (p.value("why", "") == "to be made by the Art tool") row["decision"] = {{"action", "generate"}, {"reason", "no " + slot + " pieces in the project"}};
        needs.push_back(row);
    }
    return needs;
}

std::array<int, 4> OccupiedBounds(const json& solved) {
    const json bounds = solved.value("grid", json::object()).value("bounds", json::array({0, 0, 64, 64}));
    const int W = bounds[2].get<int>(), H = bounds[3].get<int>();
    const TerrainGrid grid = ReadTerrain(solved);
    int x0 = W, y0 = H, x1 = 0, y1 = 0;
    for (int y = 0; y < grid.h; ++y) {
        for (int x = 0; x < grid.w; ++x) {
            if (grid.At(x, y) == "void") continue;
            x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x + 1), y1 = std::max(y1, y + 1);
        }
    }
    if (x1 <= x0 || y1 <= y0) return {0, 0, W, H};
    return {std::max(0, x0 - 1), std::max(0, y0 - 1), std::min(W, x1 + 1), std::min(H, y1 + 1)};
}

std::vector<PlaceLevelArea> PlaceLevelAreas(const Ontology& ontology, const json& spec, const json& solved) {
    std::vector<PlaceLevelArea> out;
    const json root = spec.is_object() && spec.contains("root") ? spec["root"] : json::object();
    for (const auto& z : root.value("children", json::array())) {
        if (!ontology.IsA(z.value("module", ""), "Level")) continue;
        PlaceLevelArea a{z.value("id", ""), 1 << 30, 1 << 30, -1, -1, {}};
        for (const auto& r : solved.value("regions", json::array())) {
            const std::string id = r.value("id", "");
            if (id != a.id && id.rfind(a.id + "/", 0) != 0) continue;
            for (const auto& run : r.value("cells", json::array())) {
                const int y = run[0].get<int>();
                a.x0 = std::min(a.x0, run[1].get<int>()), a.x1 = std::max(a.x1, run[2].get<int>());
                a.y0 = std::min(a.y0, y), a.y1 = std::max(a.y1, y + 1);
            }
        }
        if (a.x1 < 0) continue;
        for (const auto& o : solved.value("objects", json::array())) {
            const int x = o.value("x", -1), y = o.value("y", -1);
            if (o.value("slot", "") == "stairs" && x >= a.x0 && y >= a.y0 && x < a.x1 && y < a.y1) a.stairs.push_back({x, y});
        }
        out.push_back(a);
    }
    if (out.size() < 2) out.clear();
    return out;
}

void SetMoodGrade(PlaceSceneOptions& options, const json& brief, const json& story) {
    std::string words = brief.is_object() ? brief.value("style", json::object()).dump() + " " + brief.value("name", "") : "";
    if (story.is_object()) words += " " + story.value("mood", json()).dump() + " " + story.value("purpose", json()).dump();
    for (auto& c : words) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto says = [&](std::initializer_list<const char*> list) {
        int n = 0;
        for (const char* w : list) n += words.find(w) != std::string::npos;
        return n;
    };
    const int grim = says({"cursed", "ruin", "grim", "dark", "oppress", "decay", "haunt", "blacken", "gloom", "desolat", "rot", "dread", "bleak", "plague", "dying", "sinister"});
    const int bright = says({"cheer", "prosper", "sunny", "festive", "bright", "idyllic", "peaceful", "lush", "vibrant"});
    if (grim >= 2 || (grim == 1 && bright == 0)) {
        const float k = std::min(1.0f, 0.5f + 0.15f * grim); // the grimmer it reads, the more
        options.gradeSaturation = 1.0f - 0.4f * k;
        options.gradeBrightness = 1.0f - 0.2f * k;
        options.gradeTint = {1.0f - 0.08f * k, 1.0f - 0.04f * k, 1.0f};
    } else if (bright > 0) {
        options.gradeSaturation = 1.05f;
    }
}

float HillsFor(const json& brief) {
    const json root = brief.value("root", json::object());
    const std::string relief = root.is_object() ? root.value("relief", "gentle") : "gentle";
    return relief == "flat" ? 0.0f : relief == "hilly" ? 60.0f : 36.0f;
}

// --- The ground, painted ---------------------------------------------------------------------------
//
// A strategy game of the 90s (Age of Empires II) doesn't show its cells: each ground's texture
// runs across many of them, and two grounds meet in a soft, ragged edge, water through a strip
// of shore (place-refine.md §1). The place's ground is painted so, into pictures of 16 x 16
// cells, and each cell's tile is its square of the picture.

namespace {

float Hash01(int x, int y, int salt) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + static_cast<uint32_t>(salt) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xffffff) / static_cast<float>(0xffffff);
}

// Smooth noise, 0-1, about one bump per unit.
float ValueNoise(float x, float y, int salt) {
    const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    const float fx = x - ix, fy = y - iy;
    const float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    const float a = Hash01(ix, iy, salt), b = Hash01(ix + 1, iy, salt), c = Hash01(ix, iy + 1, salt), d = Hash01(ix + 1, iy + 1, salt);
    return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sy;
}

// How each ground shows (the plan's look, from the ontology's terrain kinds): made by hand (a
// field, a square: straight edges), water (a shore), a wall (stops light), its colour, hills.
struct GroundLook {
    json terrains = json::object();
    // Read in place (the painter asks it millions of times: no copy of the terrain's object).
    bool Is(const std::string& t, const char* key) const {
        const auto it = terrains.find(t);
        if (it == terrains.end() || !it->is_object()) return false;
        const auto k = it->find(key);
        return k != it->end() && k->is_boolean() && k->get<bool>();
    }
    bool Made(const std::string& t) const { return Is(t, "made") || t == "void"; }
    bool Water(const std::string& t) const { return Is(t, "water"); }
    bool Wall(const std::string& t) const { return Is(t, "blocksLight"); }
    bool Solid(const std::string& t) const { return Is(t, "solid"); }
    bool Hills(const std::string& t) const { return Is(t, "hills"); }
};

struct GroundRgb {
    float r = 0, g = 0, b = 0;
};

// A texture's colour at (u, v) in its own pixels, wrapping, between pixels (bilinear).
GroundRgb SampleWrapped(const ImageData& image, float u, float v) {
    const int w = static_cast<int>(image.width), h = static_cast<int>(image.height);
    u -= 0.5f, v -= 0.5f;
    const int x0 = static_cast<int>(std::floor(u)), y0 = static_cast<int>(std::floor(v));
    const float fx = u - x0, fy = v - y0;
    auto at = [&](int x, int y) {
        x = ((x % w) + w) % w, y = ((y % h) + h) % h;
        const uint8_t* p = &image.rgba[(static_cast<size_t>(y) * w + x) * 4];
        return GroundRgb{static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2])};
    };
    const GroundRgb a = at(x0, y0), b = at(x0 + 1, y0), c = at(x0, y0 + 1), d = at(x0 + 1, y0 + 1);
    auto mix = [](float p, float q, float t) { return p + (q - p) * t; };
    return {mix(mix(a.r, b.r, fx), mix(c.r, d.r, fx), fy), mix(mix(a.g, b.g, fx), mix(c.g, d.g, fx), fy),
            mix(mix(a.b, b.b, fx), mix(c.b, d.b, fx), fy)};
}

// What casts a shadow on the ground: a thing's foot (cells, local), its reach and how dark.
struct ShadowCaster {
    float x = 0, y = 0;   // the middle of its foot
    float rx = 0.5f, ry = 0.5f;
    float shift = 0.3f;   // cast away from the light (upper left on screen): towards +x
    float dark = 0.35f;
    bool box = false;     // a building: its footprint, not a round crown
};

struct PaintedGround {
    std::map<std::pair<int, int>, std::pair<std::string, std::array<float, 4>>> cells; // local cell: picture, uv
};

// Indoors (a dungeon, an interior): dark but for its lights, and its walls in their way.
struct PointLight {
    float x = 0, y = 0; // cells, local
    float radius = 5.5f;
    float strength = 1.0f;
};
struct Lighting {
    bool indoor = false;
    std::vector<PointLight> lights;
    float saturation = 1.0f, brightness = 1.0f; // the place's grade (its mood)
    std::array<float, 3> tint{1.0f, 1.0f, 1.0f};
};

// The land's height at each corner of the cells ((w + 1) x (h + 1), in screen pixels): low,
// rolling hills in the wild, level where it's built, a road, water or a field, and rising
// gently away from them (place-refine.md §10).
struct HeightField {
    int w = 0, h = 0;
    std::vector<float> corner;
    float At(float x, float y) const { // between corners
        if (corner.empty()) return 0.0f;
        x = std::clamp(x, 0.0f, static_cast<float>(w)), y = std::clamp(y, 0.0f, static_cast<float>(h));
        const int x0 = std::min(static_cast<int>(x), w - 1), y0 = std::min(static_cast<int>(y), h - 1);
        const float fx = x - x0, fy = y - y0;
        auto c = [&](int i, int j) { return corner[static_cast<size_t>(j) * (w + 1) + i]; };
        return (c(x0, y0) * (1 - fx) + c(x0 + 1, y0) * fx) * (1 - fy) + (c(x0, y0 + 1) * (1 - fx) + c(x0 + 1, y0 + 1) * fx) * fy;
    }
};

HeightField MakeHills(const TerrainGrid& grid, const json& solved, float top, const GroundLook& look,
                      const std::function<json(const json&)>& lookOf) {
    HeightField f;
    f.w = grid.w, f.h = grid.h;
    if (top <= 0 || grid.w <= 0 || grid.h <= 0) return f;
    // Level: what's built (with a cell round it), roads, water, fields and squares.
    std::vector<uint8_t> level(static_cast<size_t>(grid.w) * grid.h, 0);
    for (int y = 0; y < grid.h; ++y) {
        for (int x = 0; x < grid.w; ++x) {
            const std::string t = grid.bridges.contains({x, y}) ? "dirt" : grid.At(x, y);
            if (!look.Hills(t)) level[static_cast<size_t>(y) * grid.w + x] = 1;
        }
    }
    for (const auto& o : solved.value("objects", json::array())) {
        const std::string layer = o.value("layer", "prop");
        if (layer != "building" && layer != "prop") continue;
        const int ox = o.value("x", 0), oy = o.value("y", 0), ow = std::max(1, o.value("w", 1)), oh = std::max(1, o.value("h", 1));
        const int pad = layer == "building" ? 1 : 0;
        if (layer == "prop" && lookOf(o).value("vary", false)) continue; // a tree, a rock: it stands on a hill
        for (int y = oy - pad; y < oy + oh + pad; ++y)
            for (int x = ox - pad; x < ox + ow + pad; ++x)
                if (x >= 0 && y >= 0 && x < grid.w && y < grid.h) level[static_cast<size_t>(y) * grid.w + x] = 1;
    }
    // How far each cell is from level land (a breadth-first walk).
    std::vector<int> far(level.size(), 1 << 20);
    std::deque<int> queue;
    for (size_t i = 0; i < level.size(); ++i) {
        if (level[i]) far[i] = 0, queue.push_back(static_cast<int>(i));
    }
    while (!queue.empty()) {
        const int c = queue.front();
        queue.pop_front();
        const int x = c % grid.w, y = c / grid.w;
        for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
            const int nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= grid.w || ny >= grid.h) continue;
            const int n = ny * grid.w + nx;
            if (far[n] > far[c] + 1) far[n] = far[c] + 1, queue.push_back(n);
        }
    }
    f.corner.assign(static_cast<size_t>(grid.w + 1) * (grid.h + 1), 0.0f);
    for (int j = 0; j <= grid.h; ++j) {
        for (int i = 0; i <= grid.w; ++i) {
            // A corner is as far from level land as the nearest of its four cells.
            int d = 1 << 20;
            for (int cj = j - 1; cj <= j; ++cj)
                for (int ci = i - 1; ci <= i; ++ci)
                    if (ci >= 0 && cj >= 0 && ci < grid.w && cj < grid.h) d = std::min(d, far[static_cast<size_t>(cj) * grid.w + ci]);
            const float rise = std::clamp((d - 1.0f) / 5.0f, 0.0f, 1.0f); // from the edge of level land, gently
            const float n = ValueNoise(i * 0.09f, j * 0.09f, 31) * 0.7f + ValueNoise(i * 0.23f, j * 0.23f, 32) * 0.3f;
            const float hill = std::clamp((n - 0.38f) / 0.4f, 0.0f, 1.0f);
            f.corner[static_cast<size_t>(j) * (grid.w + 1) + i] = top * hill * hill * (3 - 2 * hill) * rise * rise * (3 - 2 * rise);
        }
    }
    return f;
}

// Which way a thing faces from where it stands (n, s, e, w), by its role's look "orient" (docs/places/matched-art.md
// §2b): "front", away from the nearest edge of its room or area (a wall, rock, the area's bounds), so it backs onto
// the wall and faces in; "along", down the path it stands on (the long way of its ground there), on the right-hand
// side (a car in its lane). Anything else: as the layout has it.
std::string FacingFromPlace(const json& o, const std::string& orient, const TerrainGrid& grid, const json& solved, const GroundLook& look) {
    const int x = o.value("x", 0), y = o.value("y", 0);
    const std::string here = grid.At(x, y);
    if (orient == "along") {
        // The same ground's run through the cell, each way (up to 16 cells each side).
        auto reach = [&](int dx, int dy) {
            int n = 0;
            while (n < 16 && grid.At(x + dx * (n + 1), y + dy * (n + 1)) == here) ++n;
            return n;
        };
        const int west = reach(-1, 0), east = reach(1, 0), north = reach(0, -1), south = reach(0, 1);
        const int alongX = west + east, alongY = north + south;
        const unsigned v = CellVariety(x * 7 + 3, y * 11 + 5);
        if (alongX > alongY || (alongX == alongY && (v & 1))) return south < north ? "e" : "w"; // the south half drives east
        return east < west ? "n" : "s";                                                          // the east half drives north
    }
    if (orient != "front") return o.value("facing", "s");
    // Its area: the smallest region holding it.
    std::array<int, 4> area = {0, 0, grid.w, grid.h};
    long best = static_cast<long>(grid.w) * grid.h + 1;
    for (const auto& r : solved.value("regions", json::array())) {
        const json b = r.value("bounds", json::array());
        if (!b.is_array() || b.size() != 4) continue;
        const int bx = b[0], by = b[1], bw = b[2], bh = b[3];
        if (x < bx || y < by || x >= bx + bw || y >= by + bh || static_cast<long>(bw) * bh >= best) continue;
        best = static_cast<long>(bw) * bh, area = {bx, by, bx + bw, by + bh};
    }
    auto open = [&](int cx, int cy) {
        if (cx < area[0] || cy < area[1] || cx >= area[2] || cy >= area[3]) return false;
        const std::string& t = grid.At(cx, cy);
        return !t.empty() && t != "void" && !look.Wall(t) && !look.Solid(t);
    };
    const int w = std::max(1, o.value("w", 1)), h = std::max(1, o.value("h", 1));
    auto distance = [&](int dx, int dy) {
        int n = 0;
        int cx = dx > 0 ? x + w - 1 : x, cy = dy > 0 ? y + h - 1 : y;
        while (n < 8 && open(cx + dx * (n + 1), cy + dy * (n + 1))) ++n;
        return n;
    };
    const int dn = distance(0, -1), ds = distance(0, 1), dw = distance(-1, 0), de = distance(1, 0);
    const int nearest = std::min({dn, ds, dw, de});
    if (nearest >= 8) return o.value("facing", "s"); // nothing near: as laid out
    // Backs onto the nearest edge; on a tie, the way with more room in front.
    std::vector<std::pair<int, std::string>> ways = {{ds, "n"}, {dn, "s"}, {de, "w"}, {dw, "e"}}; // (room behind, facing)
    std::string facing = "s";
    int room = -1;
    for (const auto& [behind, f] : ways) {
        if (behind != nearest) continue;
        const int ahead = f == "n" ? dn : f == "s" ? ds : f == "e" ? de : dw;
        if (ahead > room) room = ahead, facing = f;
    }
    return facing;
}

constexpr int kGroundChunk = 16;   // cells across a picture
constexpr int kGroundCellPx = 64;  // pixels across a cell: a 64 x 32 diamond needs 32; twice that keeps the ground sharp close up
// With a painted ground picture: as sharp as a painted ground at zoom 1 to 1.4. Finer is too big to draw: 192 px made
// 128 x 128 cells 64 pictures of 3072 px (2.3 GB of textures), and the editor stopped while it loaded them.
constexpr int kGroundCellPxPainted = 64;
constexpr float kTextureCells = 4; // cells a ground's texture spans before it repeats

// Each ground's own colour (place-refine.md §3): the image model's grass is green one time and
// mustard the next. A painted ground is moved most of the way to its terrain's colour (its
// average shifted, its detail kept), so every place has the same muted 90s strategy-game
// palette.
std::map<std::string, ImageData> LockPalette(std::map<std::string, ImageData> textures, const GroundLook& look) {
    for (auto& [t, image] : textures) {
        const json tone = look.terrains.value(t.substr(0, t.find('#')), json::object()).value("tone", json()); // a take ("t#2") is its ground's
        if (!tone.is_array() || tone.size() != 3 || image.rgba.empty()) continue;
        const GroundRgb target{tone[0].get<float>(), tone[1].get<float>(), tone[2].get<float>()};
        GroundRgb mean;
        const size_t n = image.rgba.size() / 4;
        for (size_t i = 0; i < n; ++i) mean.r += image.rgba[i * 4], mean.g += image.rgba[i * 4 + 1], mean.b += image.rgba[i * 4 + 2];
        mean.r /= n, mean.g /= n, mean.b /= n;
        const float k = look.Is(t.substr(0, t.find('#')), "sketched") ? 0.25f : 0.8f; // the sketch's own ground: its colours mostly kept
        const float dr = (target.r - mean.r) * k, dg = (target.g - mean.g) * k, db = (target.b - mean.b) * k;
        for (size_t i = 0; i < n; ++i) {
            image.rgba[i * 4] = static_cast<uint8_t>(std::clamp(image.rgba[i * 4] + dr, 0.0f, 255.0f));
            image.rgba[i * 4 + 1] = static_cast<uint8_t>(std::clamp(image.rgba[i * 4 + 1] + dg, 0.0f, 255.0f));
            image.rgba[i * 4 + 2] = static_cast<uint8_t>(std::clamp(image.rgba[i * 4 + 2] + db, 0.0f, 255.0f));
        }
    }
    return textures;
}

PaintedGround PaintGround(const Project& project, const TerrainGrid& grid, const std::map<std::string, ImageData>& rawTextures,
                          const std::string& place, const std::vector<ShadowCaster>& casters, const HeightField& heights,
                          const Lighting& lighting, const GroundLook& look,
                          const std::function<std::string(const std::string&, int, int)>& takeOf = {},
                          const ImageData* picture = nullptr, int pictureCellPx = 0) {
    // With a picture of the ground (painted whole), its colours; drawn at a finer scale, its detail kept.
    // The picture's own detail, no more (drawn finer, it only blurs and takes minutes): at least kGroundCellPx / 2.
    const int kCellPx = picture ? std::clamp(pictureCellPx, kGroundCellPx / 2, kGroundCellPxPainted) : kGroundCellPx;
    PaintedGround out;
    const std::map<std::string, ImageData> textures = LockPalette(rawTextures, look);
    auto nameAt = [&](int x, int y) -> std::string {
        x = std::clamp(x, 0, grid.w - 1), y = std::clamp(y, 0, grid.h - 1);
        return grid.bridges.contains({x, y}) ? "dirt" : grid.At(x, y);
    };
    // Shore and shallows: the colour of the ground the ontology says a shore is (sand), else a sand colour.
    GroundRgb sand{196, 176, 124};
    for (const auto& [t, l] : look.terrains.items()) {
        if (l.value("shore", false) && l.contains("tone") && l["tone"].is_array() && l["tone"].size() == 3) {
            sand = {l["tone"][0].get<float>(), l["tone"][1].get<float>(), l["tone"][2].get<float>()};
        }
    }
    // The picture names: the place's, and what it was painted from (so a changed ground is a new picture).
    size_t hash = std::hash<std::string>{}(place);
    for (const auto& c : grid.cells) hash = hash * 31 + std::hash<std::string>{}(c);
    for (const auto& [t, image] : textures) hash = hash * 131 + std::hash<std::string>{}(t) + image.rgba.size();
    for (const auto& c : casters) hash = hash * 7 + static_cast<size_t>(c.x * 13 + c.y * 131);
    for (const float c : heights.corner) hash = hash * 3 + static_cast<size_t>(c * 8);
    // Each light's reach, worked out once, four points to a cell: what it sees, the walls
    // (rock) stopping it, so a wall casts a hard shadow and a doorway lets a beam through.
    constexpr int kLightSub = 4;
    auto wallAt = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= grid.w || y >= grid.h) return true;
        return look.Wall(grid.At(x, y));
    };
    const int lw = grid.w * kLightSub, lh = grid.h * kLightSub;
    std::vector<float> lit;
    if (lighting.indoor) {
        lit.assign(static_cast<size_t>(lw) * lh, 0.0f);
        for (const auto& l : lighting.lights) {
            const int x0 = std::max(0, static_cast<int>((l.x - l.radius) * kLightSub)), x1 = std::min(lw, static_cast<int>((l.x + l.radius) * kLightSub) + 1);
            const int y0 = std::max(0, static_cast<int>((l.y - l.radius) * kLightSub)), y1 = std::min(lh, static_cast<int>((l.y + l.radius) * kLightSub) + 1);
            for (int sy = y0; sy < y1; ++sy) {
                for (int sx = x0; sx < x1; ++sx) {
                    const float px = (sx + 0.5f) / kLightSub, py = (sy + 0.5f) / kLightSub;
                    const float d = std::hypot(px - l.x, py - l.y);
                    if (d >= l.radius) continue;
                    // On the way from the light: a wall cell (other than the point's own) stops it.
                    const int steps = static_cast<int>(d / 0.2f);
                    const int ox = static_cast<int>(std::floor(px)), oy = static_cast<int>(std::floor(py));
                    const int lcx = static_cast<int>(std::floor(l.x)), lcy = static_cast<int>(std::floor(l.y));
                    bool blocked = false;
                    for (int k = 1; k < steps && !blocked; ++k) {
                        const float t = static_cast<float>(k) / steps;
                        const int cx = static_cast<int>(std::floor(l.x + (px - l.x) * t)), cy = static_cast<int>(std::floor(l.y + (py - l.y) * t));
                        if ((cx != ox || cy != oy) && (cx != lcx || cy != lcy) && wallAt(cx, cy)) blocked = true;
                    }
                    if (blocked) continue;
                    const float f = 1.0f - d / l.radius;
                    lit[static_cast<size_t>(sy) * lw + sx] += l.strength * f * f;
                }
            }
        }
    }
    for (const auto& l : lighting.lights) hash = hash * 5 + static_cast<size_t>(l.x * 17 + l.y * 101);
    std::string slug = PrefabSlug(place);
    const fs::path folder = project.Resolve("assets/generated/ground");
    std::error_code ec;
    fs::create_directories(folder, ec);
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        if (entry.path().filename().string().rfind(slug + "-", 0) == 0) fs::remove(entry.path(), ec); // the place's earlier ground
    }
    char tag[16];
    std::snprintf(tag, sizeof(tag), "%08x", static_cast<unsigned>(hash & 0xffffffffu));
    // Smooth shapes (place-refine.md §6): a river laid cell by cell is a staircase. Each natural
    // ground's share of a point is weighed from the cells round it (the nearer, the more), and
    // the greatest wins: diagonals become curves, corners round. Made grounds (a field, a
    // square) keep their straight edges: a point in one is that ground, and they don't spread.
    // Worked out once, eight points to a cell.
    constexpr int kSub = 8;
    std::vector<std::string> names;
    std::vector<int> index(static_cast<size_t>(grid.w) * grid.h, 0);
    for (int y = 0; y < grid.h; ++y) {
        for (int x = 0; x < grid.w; ++x) {
            const std::string n = nameAt(x, y);
            auto it = std::find(names.begin(), names.end(), n);
            if (it == names.end()) it = names.insert(names.end(), n);
            index[static_cast<size_t>(y) * grid.w + x] = static_cast<int>(it - names.begin());
        }
    }
    std::vector<uint8_t> fixedShape(names.size(), 0);
    for (size_t i = 0; i < names.size(); ++i) fixedShape[i] = look.Made(names[i]);
    const int fw = grid.w * kSub, fh = grid.h * kSub;
    std::vector<int> fine(static_cast<size_t>(fw) * fh, 0);
    std::vector<float> weight(names.size());
    for (int fy = 0; fy < fh; ++fy) {
        for (int fx = 0; fx < fw; ++fx) {
            const float x = (fx + 0.5f) / kSub, y = (fy + 0.5f) / kSub;
            const int ci = fx / kSub, cj = fy / kSub;
            const int own = index[static_cast<size_t>(cj) * grid.w + ci];
            int best = own;
            if (!fixedShape[own]) {
                std::fill(weight.begin(), weight.end(), 0.0f);
                for (int j = cj - 2; j <= cj + 2; ++j) {
                    for (int i = ci - 2; i <= ci + 2; ++i) {
                        const int t = index[static_cast<size_t>(std::clamp(j, 0, grid.h - 1)) * grid.w + std::clamp(i, 0, grid.w - 1)];
                        if (fixedShape[t]) continue;
                        const float ex = i + 0.5f - x, ey = j + 0.5f - y;
                        weight[t] += std::exp(-(ex * ex + ey * ey) / 0.7f);
                    }
                }
                for (size_t t = 0; t < names.size(); ++t) {
                    if (weight[t] > weight[best]) best = static_cast<int>(t);
                }
            }
            fine[static_cast<size_t>(fy) * fw + fx] = best;
        }
    }
    auto groundAt = [&](float x, float y) -> int {
        const int fx = std::clamp(static_cast<int>(std::floor(x * kSub)), 0, fw - 1), fy = std::clamp(static_cast<int>(std::floor(y * kSub)), 0, fh - 1);
        return fine[static_cast<size_t>(fy) * fw + fx];
    };
    // Each ground's facts and plain texture, worked out once (the pixels ask by index, not by name).
    const size_t groundCount = names.size();
    std::vector<uint8_t> isMade(groundCount), isWater(groundCount), isWall(groundCount), isVoid(groundCount);
    std::vector<const ImageData*> plainTexture(groundCount, nullptr);
    auto lookupTexture = [&](const std::string& name) -> const ImageData* {
        const auto tex = textures.find(name);
        return tex == textures.end() || tex->second.width == 0 ? nullptr : &tex->second;
    };
    std::vector<uint8_t> isRoadLike(groundCount);
    std::vector<float> meanLum(groundCount, 0.0f);
    for (size_t i = 0; i < groundCount; ++i) {
        isMade[i] = look.Made(names[i]);
        isWater[i] = look.Water(names[i]);
        isWall[i] = look.Wall(names[i]);
        isVoid[i] = names[i] == "void";
        plainTexture[i] = lookupTexture(names[i]);
        isRoadLike[i] = names[i] == "dirt" || names[i] == "gravel" || names[i] == "cobble" || names[i] == "paved" || names[i] == "path";
        // Its texture's mean brightness: grounds of much the same colour are told apart at their edge.
        if (const ImageData* t = plainTexture[i]; t && t->width > 0) {
            double sum = 0;
            const size_t n = static_cast<size_t>(t->width) * t->height;
            for (size_t p = 0; p < n; p += 7) sum += 0.3 * t->rgba[p * 4] + 0.59 * t->rgba[p * 4 + 1] + 0.11 * t->rgba[p * 4 + 2];
            meanLum[i] = static_cast<float>(sum / std::max<size_t>(1, (n + 6) / 7));
        }
    }
    // Four looks a little round each point: the edge between two grounds, soft.
    const float jitter[4][2] = {{0.16f, 0.06f}, {-0.06f, 0.16f}, {-0.16f, -0.06f}, {0.06f, -0.16f}};
    const int side = kGroundChunk * kCellPx;
    const int chunksX = (grid.w + kGroundChunk - 1) / kGroundChunk, chunksY = (grid.h + kGroundChunk - 1) / kGroundChunk;
    const int chunkCount = chunksX * chunksY;
    std::vector<uint8_t> painted(static_cast<size_t>(chunkCount), 0);
    std::atomic<bool> failed{false};
    std::atomic<int> nextChunk{0};
    // One picture per chunk, each on its own: the chunks are painted side by side, on every core.
    auto paintChunks = [&] {
        std::unordered_map<int64_t, const ImageData*> takes; // a field's take of a ground, per cell
        for (int chunk = nextChunk++; chunk < chunkCount && !failed; chunk = nextChunk++) {
            const int cx = chunk % chunksX, cy = chunk / chunksX;
            ImageData image;
            image.width = image.height = static_cast<uint32_t>(side);
            image.rgba.assign(static_cast<size_t>(side) * side * 4, 0);
            bool any = false;
            // The shadows on this picture (place-refine.md §3): soft, the darkest kept where two meet.
            std::vector<float> shade(static_cast<size_t>(side) * side, 0.0f);
            const float x0 = static_cast<float>(cx * kGroundChunk), y0 = static_cast<float>(cy * kGroundChunk);
            for (const auto& c : casters) {
                const float sx = c.x + c.shift, sy = c.y + c.shift * 0.35f;
                const float reachX = c.rx + 0.6f, reachY = c.ry + 0.6f;
                if (sx + reachX < x0 || sx - reachX > x0 + kGroundChunk || sy + reachY < y0 || sy - reachY > y0 + kGroundChunk) continue;
                const int pxa = std::max(0, static_cast<int>((sx - reachX - x0) * kCellPx)), pxb = std::min(side, static_cast<int>((sx + reachX - x0) * kCellPx) + 1);
                const int pya = std::max(0, static_cast<int>((sy - reachY - y0) * kCellPx)), pyb = std::min(side, static_cast<int>((sy + reachY - y0) * kCellPx) + 1);
                for (int py = pya; py < pyb; ++py) {
                    for (int px = pxa; px < pxb; ++px) {
                        const float wx = x0 + (px + 0.5f) / kCellPx, wy = y0 + (py + 0.5f) / kCellPx;
                        float d;
                        if (c.box) {
                            // Outside the footprint's box, by how far (0 inside).
                            const float ex = std::max(0.0f, std::abs(wx - sx) - c.rx), ey = std::max(0.0f, std::abs(wy - sy) - c.ry);
                            d = std::sqrt(ex * ex + ey * ey) / 0.45f;
                        } else {
                            const float ex = (wx - sx) / c.rx, ey = (wy - sy) / c.ry;
                            d = std::sqrt(ex * ex + ey * ey);
                        }
                        const float s = d <= 0.7f ? 1.0f : d >= 1.25f ? 0.0f : 1.0f - (d - 0.7f) / 0.55f;
                        float& at = shade[static_cast<size_t>(py) * side + px];
                        at = std::max(at, s * s * (3 - 2 * s) * c.dark);
                    }
                }
            }
            for (int py = 0; py < side; ++py) {
                for (int px = 0; px < side; ++px) {
                    const float wx = cx * kGroundChunk + (px + 0.5f) / kCellPx, wy = cy * kGroundChunk + (py + 0.5f) / kCellPx;
                    const int ox = static_cast<int>(wx), oy = static_cast<int>(wy);
                    if (ox >= grid.w || oy >= grid.h) continue;
                    const int own = index[static_cast<size_t>(oy) * grid.w + ox];
                    if (isVoid[own]) continue;
                    GroundRgb color;
                    if (picture != nullptr && pictureCellPx > 0) {
                        // A background painted whole (docs/places/background.md): its colour here (bilinear). Its
                        // edges, shores and roads are painted in it already: no textures, no borders worked out.
                        const float fx = std::clamp(wx * pictureCellPx - 0.5f, 0.0f, picture->width - 1.0f);
                        const float fy = std::clamp(wy * pictureCellPx - 0.5f, 0.0f, picture->height - 1.0f);
                        const int ax = static_cast<int>(fx), ay = static_cast<int>(fy);
                        const int bx = std::min<int>(ax + 1, picture->width - 1), by = std::min<int>(ay + 1, picture->height - 1);
                        const float tx = fx - ax, ty = fy - ay;
                        auto at = [&](int x, int y, int k) { return static_cast<float>(picture->rgba[(static_cast<size_t>(y) * picture->width + x) * 4 + k]); };
                        auto mix = [&](int k) { return (at(ax, ay, k) * (1 - tx) + at(bx, ay, k) * tx) * (1 - ty) + (at(ax, by, k) * (1 - tx) + at(bx, by, k) * tx) * ty; };
                        color = {mix(0), mix(1), mix(2)};
                    } else {
                    // A ragged border: where it looks up its ground, moved a little by noise.
                    const float amp = isMade[own] ? 0.05f : 0.34f;
                    const float dx = (ValueNoise(wx * 1.4f, wy * 1.4f, 1) * 2 - 1) * amp + (ValueNoise(wx * 3.7f, wy * 3.7f, 2) * 2 - 1) * amp * 0.45f;
                    const float dy = (ValueNoise(wx * 1.4f, wy * 1.4f, 3) * 2 - 1) * amp + (ValueNoise(wx * 3.7f, wy * 3.7f, 4) * 2 - 1) * amp * 0.45f;
                    const float soft = isMade[own] ? 0.25f : 1.0f;
                    int water = 0, samples = 0;
                    for (const auto& j : jitter) {
                        int t = groundAt(wx + dx + j[0] * soft, wy + dy + j[1] * soft);
                        if (isVoid[t]) t = own;
                        // Its take where the ground has several (one per field: takeOf), else the ground's.
                        const ImageData* ti = nullptr;
                        if (takeOf) {
                            const int64_t key = (static_cast<int64_t>(t) << 40) ^ (static_cast<int64_t>(static_cast<int>(wx)) << 20) ^ static_cast<int>(wy);
                            auto cached = takes.find(key);
                            if (cached == takes.end()) cached = takes.emplace(key, lookupTexture(takeOf(names[t], static_cast<int>(wx), static_cast<int>(wy)))).first;
                            ti = cached->second;
                        } else {
                            ti = plainTexture[t];
                        }
                        if (!ti) ti = plainTexture[t];
                        if (!ti) ti = plainTexture[own];
                        if (!ti) continue;
                        // Its texture and, where slow noise says, the same texture turned and moved
                        // ("texture bombing"): no two stretches of a field sample it alike.
                        const float tu = wx / kTextureCells * ti->width, tv = wy / kTextureCells * ti->height;
                        const GroundRgb a = SampleWrapped(*ti, tu, tv);
                        const GroundRgb b = SampleWrapped(*ti, tv + 0.37f * ti->width, -tu + 0.61f * ti->height);
                        const float k = std::clamp((ValueNoise(wx * 0.33f, wy * 0.33f, 21) - 0.35f) / 0.3f, 0.0f, 1.0f);
                        const float m = k * k * (3 - 2 * k);
                        const GroundRgb c{a.r + (b.r - a.r) * m, a.g + (b.g - a.g) * m, a.b + (b.b - a.b) * m};
                        color.r += c.r, color.g += c.g, color.b += c.b;
                        water += isWater[t] ? 1 : 0;
                        ++samples;
                    }
                    if (samples == 0) continue;
                    color.r /= samples, color.g /= samples, color.b /= samples;
                    // The shore: land by the water turns to sand; the water by the land, shallow.
                    int nearWater = 0;
                    for (int k = 0; k < 6; ++k) {
                        const float a = k * 1.0472f;
                        nearWater += isWater[groundAt(wx + dx + 0.42f * std::cos(a), wy + dy + 0.42f * std::sin(a))] ? 1 : 0;
                    }
                    if (water * 2 < samples && nearWater > 0) {
                        const float t = 0.55f * std::min(1.0f, nearWater / 3.0f);
                        color = {color.r + (sand.r - color.r) * t, color.g + (sand.g - color.g) * t, color.b + (sand.b - color.b) * t};
                    } else if (water * 2 >= samples && nearWater < 6) {
                        const float t = 0.3f * (1.0f - nearWater / 6.0f);
                        color = {color.r + (sand.r - color.r) * t, color.g + (sand.g - color.g) * t, color.b + (sand.b - color.b) * t};
                    }
                    // A road on ground of much the same colour (a dirt track through mud) disappears into
                    // it (docs/build-fixes.md): its edge is worn darker, so it reads as a road.
                    if (isRoadLike[own] && !isWater[own]) {
                        for (int k = 0; k < 6; ++k) {
                            const float a = k * 1.0472f + 0.3f;
                            const int t = groundAt(wx + dx + 0.3f * std::cos(a), wy + dy + 0.3f * std::sin(a));
                            if (t == own || isWater[t] || isVoid[t] || isWall[t] || std::abs(meanLum[t] - meanLum[own]) > 32.0f) continue;
                            color = {color.r * 0.8f, color.g * 0.8f, color.b * 0.8f};
                            break;
                        }
                    }
                    }
                    // A slow change of light across the place: no two stretches of grass alike.
                    float light = 1.0f + (ValueNoise(wx * 0.12f, wy * 0.12f, 9) - 0.5f) * 0.14f + (ValueNoise(wx * 0.5f, wy * 0.5f, 10) - 0.5f) * 0.06f;
                    light *= 1.0f - shade[static_cast<size_t>(py) * side + px];
                    // Indoors: a cold dark, and each light's warm pool where it reaches.
                    float lr = light, lg = light, lb = light;
                    if (lighting.indoor) {
                        const int sx = std::min(lw - 1, static_cast<int>(wx * kLightSub)), sy = std::min(lh - 1, static_cast<int>(wy * kLightSub));
                        const float warm = std::min(1.6f, lit[static_cast<size_t>(sy) * lw + sx]);
                        // Solid rock nobody stands in is darkness, as in Diablo (docs/build-fixes.md): its
                        // texture flattened to a faint grain, so no tile pattern shows; the floors show.
                        if (isWall[own]) {
                            const float mean = (color.r + color.g + color.b) / 3.0f;
                            color = {mean + (color.r - mean) * 0.25f, mean + (color.g - mean) * 0.25f, mean + (color.b - mean) * 0.25f};
                            const float grain = 0.85f + 0.3f * ValueNoise(wx * 2.3f, wy * 2.3f, 31);
                            color = {color.r * 0.35f * grain + 9.0f, color.g * 0.35f * grain + 8.0f, color.b * 0.35f * grain + 11.0f};
                        }
                        const float ambient = isWall[own] ? 0.16f : 0.42f; // dark, but what's there still shows
                        const float glow = isWall[own] ? 0.35f : 1.0f;      // the light falls on the floor
                        lr = light * (ambient * 0.9f + warm * 1.05f * glow);
                        lg = light * (ambient * 0.95f + warm * 0.82f * glow);
                        lb = light * (ambient * 1.15f + warm * 0.58f * glow);
                    }
                    // The place's grade: its mood's saturation, brightness and tint.
                    if (lighting.saturation != 1.0f || lighting.brightness != 1.0f) {
                        const float lum = 0.3f * color.r + 0.59f * color.g + 0.11f * color.b;
                        color = {lum + (color.r - lum) * lighting.saturation, lum + (color.g - lum) * lighting.saturation, lum + (color.b - lum) * lighting.saturation};
                        lr *= lighting.brightness * lighting.tint[0], lg *= lighting.brightness * lighting.tint[1], lb *= lighting.brightness * lighting.tint[2];
                    }
                    uint8_t* p = &image.rgba[(static_cast<size_t>(py) * side + px) * 4];
                    p[0] = static_cast<uint8_t>(std::clamp(color.r * lr, 0.0f, 255.0f));
                    p[1] = static_cast<uint8_t>(std::clamp(color.g * lg, 0.0f, 255.0f));
                    p[2] = static_cast<uint8_t>(std::clamp(color.b * lb, 0.0f, 255.0f));
                    p[3] = 255;
                    any = true;
                }
            }
            if (!any) continue;
            const std::string path = "assets/generated/ground/" + slug + "-" + std::to_string(cx) + "-" + std::to_string(cy) + ".png";
            if (!WritePng(project.Resolve(path), image)) {
                failed = true;
                return;
            }
            painted[static_cast<size_t>(chunk)] = 1;
        }
    };
    {
        // Every core but one: the editor (and the rest of the machine) keeps one, and a laptop runs less hot.
        const int workers = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 1, 1, std::max(1, chunkCount));
        std::vector<std::thread> threads;
        for (int i = 1; i < workers; ++i) threads.emplace_back(paintChunks);
        paintChunks();
        for (auto& t : threads) t.join();
    }
    (void)tag;
    if (failed) return {};
    for (int chunk = 0; chunk < chunkCount; ++chunk) {
        if (!painted[static_cast<size_t>(chunk)]) continue;
        const int cx = chunk % chunksX, cy = chunk / chunksX;
        const std::string path = "assets/generated/ground/" + slug + "-" + std::to_string(cx) + "-" + std::to_string(cy) + ".png";
        for (int y = cy * kGroundChunk; y < std::min(grid.h, (cy + 1) * kGroundChunk); ++y) {
            for (int x = cx * kGroundChunk; x < std::min(grid.w, (cx + 1) * kGroundChunk); ++x) {
                const float u0 = static_cast<float>(x - cx * kGroundChunk) / kGroundChunk, v0 = static_cast<float>(y - cy * kGroundChunk) / kGroundChunk;
                out.cells[{x, y}] = {path, {u0, v0, u0 + 1.0f / kGroundChunk, v0 + 1.0f / kGroundChunk}};
            }
        }
    }
    return out;
}

} // namespace

// --- Building ------------------------------------------------------------------------------------

namespace {

// The item id a dungeon key gets: "key-" and its layout id made safe.
std::string KeyItemId(const std::string& placeKey) {
    std::string id = "key-";
    for (char c : placeKey) {
        const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        id += (std::isalnum(static_cast<unsigned char>(lower)) || lower == '-' || lower == '_') ? lower : '-';
    }
    return id;
}

int WireLocksAndKeys(Registry& registry, PrefabLibrary& prefabs, const json& solved, const PlaceSceneOptions& options) {
    std::map<std::string, std::string> lockKey; // connector id -> key id
    const json connectors = solved.value("connectors", json::array());
    for (const auto& c : connectors) {
        if (c.value("kind", "") == "locked-door" && !c.value("key", "").empty()) lockKey[c.value("id", "")] = c.value("key", "");
    }
    std::set<std::string> keys;
    const json spaces = solved.value("spaces", json::object());
    const json graphs = spaces.value("graphs", json::array());
    for (const auto& g : graphs) {
        const json list = g.value("keys", json::array());
        for (const auto& k : list) keys.insert(k.value("id", ""));
    }
    if (lockKey.empty() && keys.empty()) return 0;
    RecordDatabase records;
    records.Open(EngineReflection(), prefabs.GetProject().root);
    int wired = 0;
    for (auto entity : PlaceEntities(registry, options.place)) {
        const std::string statement = registry.get<PlaceNode>(entity).statement;
        if (auto it = lockKey.find(statement); it != lockKey.end()) {
            auto& lock = registry.emplace_or_replace<Interactable>(entity);
            lock.verb = "Open";
            lock.name = "locked door";
            lock.locked = true;
            lock.key = KeyItemId(it->second);
            registry.emplace_or_replace<Door>(entity);
            if (!registry.all_of<Obstacle>(entity)) registry.emplace<Obstacle>(entity); // shut, it blocks the way
            ++wired;
        } else if (keys.contains(statement)) {
            const std::string item = KeyItemId(statement);
            registry.emplace_or_replace<ItemPickup>(entity, item, 1, false);
            if (!records.Find("item", item)) {
                records.Write("item", item, {{"name", "Key"}, {"kind", "key"}, {"unique", true},
                                             {"description", "It opens a locked door somewhere in " + options.place + "."}});
            }
            ++wired;
        }
    }
    if (ActiveRecords().IsOpen() && ActiveRecords().Root() == prefabs.GetProject().root) ActiveRecords().Refresh();
    return wired;
}

int WireSmartObjects(Registry& registry, const json& assetPlan, const PlaceSceneOptions& options) {
    const json byObject = assetPlan.value("byObject", json::object());
    int wired = 0;
    for (auto entity : PlaceEntities(registry, options.place)) {
        const std::string statement = registry.get<PlaceNode>(entity).statement;
        if (!byObject.contains(statement) || !byObject[statement].is_string()) continue;
        const std::string key = byObject[statement].get<std::string>();
        const std::string role = key.substr(0, key.find('|'));
        const std::string affordances = AffordancesForRole(role);
        if (affordances.empty() || registry.all_of<SmartObject>(entity)) continue;
        auto& object = registry.emplace<SmartObject>(entity);
        object.type = role;
        object.affordances = affordances;
        object.slots = role == "bench" || role == "pew" || role == "table" || role == "campfire" ? 3 : 1;
        if (role == "bed") object.minDuration = object.maxDuration = 1e6f; // until the package ends
        ++wired;
    }
    return wired;
}

} // namespace

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
    // The grounds without tiles drawn to fit each other (made by the Art tool, placeholders, a
    // pack's plain tiles): painted as one picture, blended where they meet. A ground with
    // transition tiles of its own (a pack's grass-to-dirt edges) keeps them, fitted per cell.
    std::map<std::string, ImageData> textures;
    std::map<std::string, TileDef> baseDefs;
    std::map<std::string, int> takes; // a ground's takes beyond its first ("t#1"...): one per region
    if (options.paintGround) {
        for (const auto& [t, row] : terrainsUsed.items()) {
            std::string prefab = row.value("prefab", "");
            std::vector<std::string> others; // its other plain tiles: more takes of it
            if (const std::string set = row.value("terrain", ""); !set.empty()) {
                bool transitions = false;
                prefab.clear();
                for (const TerrainTile& tile : terrains.tiles) {
                    if (tile.center != set) continue;
                    bool plain = true;
                    for (const auto& e : tile.edges) plain = plain && e == set;
                    if (!plain) transitions = true;
                    else if (prefab.empty()) prefab = tile.prefab;
                    else others.push_back(tile.prefab);
                }
                if (transitions) continue;
            }
            for (const auto& v : row.value("variants", json::array())) {
                if (v.is_string() && v.get<std::string>() != prefab) others.push_back(v.get<std::string>());
            }
            for (size_t k = 0; k < others.size() && k < 3; ++k) {
                if (const auto d = TileDefFromPrefab(prefabs, others[k], 0, false)) {
                    if (auto image = DecodeImage(prefabs.GetProject().Resolve(d->sprite.texture)); image && image->width > 0) {
                        textures[t + "#" + std::to_string(++takes[t])] = std::move(*image);
                    }
                }
            }
            if (prefab.empty()) continue;
            const auto def = TileDefFromPrefab(prefabs, prefab, 0, SolidTerrain(t));
            if (!def) continue;
            if (auto image = DecodeImage(prefabs.GetProject().Resolve(def->sprite.texture)); image && image->width > 0) {
                textures[t] = std::move(*image);
                baseDefs[t] = *def;
            }
        }
    }
    // Each object's look: its need's (its role's, from the ontology).
    const json byObjectLook = assetPlan.value("byObject", json::object());
    std::map<std::string, json> lookByKey;
    for (const auto& n : assetPlan.value("needs", json::array())) lookByKey[n.value("key", "")] = n.value("look", json::object());
    auto lookOf = [&](const json& o) -> json {
        auto it = lookByKey.find(byObjectLook.value(o.value("id", ""), ""));
        return it == lookByKey.end() ? json::object() : it->second;
    };
    const GroundLook groundLook{assetPlan.value("look", json::object()).value("terrains", json::object())};
    // What stands on it casts its shadow onto it (place-refine.md §3), in every kind of place:
    // a building its block, anything else a round one its size (its role's), cast away from the
    // light by how tall it is. A doorway's door is in the wall: none.
    std::vector<ShadowCaster> casters;
    for (const auto& o : solved.value("objects", json::array())) {
        const std::string layer = o.value("layer", "prop");
        const json look = lookOf(o);
        const float w = static_cast<float>(std::max(1, o.value("w", 1))), h = static_cast<float>(std::max(1, o.value("h", 1)));
        const float x = o.value("x", 0) + w / 2, y = o.value("y", 0) + h / 2;
        if (layer == "building") {
            casters.push_back({x, y, w / 2 - 0.1f, h / 2 - 0.1f, 0.75f, 0.42f, true});
        } else if (layer == "prop" && !look.value("doorway", false)) {
            const float size = look.value("size", 0.0f) > 0 ? look.value("size", 0.0f) : 0.6f * std::max(w, h);
            const float tall = look.value("tall", 0.0f) > 0 ? look.value("tall", 0.0f) : 1.0f;
            const float r = std::clamp(0.46f * size, 0.12f, 1.2f);
            // Cast by its height, not its width: a low thing (a car, a crate) keeps its shadow under it, so it stands on the
            // ground; a tall one (a lamp post, a tree) casts it further.
            casters.push_back({x, y, r, r * 0.75f, 0.02f + 0.22f * std::clamp(tall - 0.6f, 0.0f, 2.0f), size >= 1.0f ? 0.4f : 0.32f, false});
        }
    }
    Lighting lighting;
    lighting.indoor = (options.lighting.empty() ? assetPlan.value("look", json::object()).value("lighting", "sun") : options.lighting) == "indoor";
    lighting.saturation = options.gradeSaturation, lighting.brightness = options.gradeBrightness, lighting.tint = options.gradeTint;
    std::vector<std::pair<int, int>> torches; // wall cells with a torch on them
    if (lighting.indoor) {
        // Indoors the shadows fall all round a thing (its lights are everywhere), close under it.
        for (auto& c : casters) c.shift = 0.08f, c.dark = 0.45f;
        // What gives light (its role's look: a torch, a lamp, a brazier).
        for (const auto& o : solved.value("objects", json::array())) {
            const json light = lookOf(o).value("light", json());
            if (!light.is_object()) continue;
            lighting.lights.push_back({o.value("x", 0) + 0.5f, o.value("y", 0) + 0.5f, light.value("radius", 5.5f), light.value("strength", 1.0f)});
        }
        // Torches on the walls behind the floor (the ones drawn), every few cells, so no room is dark.
        auto open = [&](int x, int y) { return x >= 0 && y >= 0 && x < grid.w && y < grid.h && !groundLook.Wall(grid.At(x, y)) && grid.At(x, y) != "void"; };
        int floorCells = 0;
        for (int y = 0; y < grid.h; ++y)
            for (int x = 0; x < grid.w; ++x) floorCells += open(x, y);
        // A part that hangs where the lights are (its "at": "lights"): lights along the walls.
        bool hangs = false;
        const json lookParts = assetPlan.value("look", json::object()).value("parts", json::array());
        for (const auto& part : lookParts) hangs = hangs || part.value("at", "") == "lights";
        if (hangs && static_cast<int>(lighting.lights.size()) * 30 < floorCells) {
            for (int y = 0; y < grid.h; ++y) {
                for (int x = 0; x < grid.w; ++x) {
                    if (!groundLook.Wall(grid.At(x, y)) || (x * 7 + y * 13) % 5 != 0) continue;
                    // A back wall: the floor in front of it, towards the viewer (+x or +y).
                    int fx = 0, fy = 0;
                    if (open(x + 1, y)) fx = 1;
                    else if (open(x, y + 1)) fy = 1;
                    else continue;
                    lighting.lights.push_back({x + 0.5f + fx * 0.6f, y + 0.5f + fy * 0.6f, 5.0f, 1.05f});
                    torches.push_back({x, y});
                }
            }
        }
    }
    // Which region each cell is in (its smallest: a field in its farmland), for a ground's takes:
    // one field one take, the next another.
    std::vector<int> regionAt(static_cast<size_t>(std::max(0, grid.w * grid.h)), -1);
    if (!takes.empty()) {
        std::vector<std::pair<size_t, int>> bySize;
        const json regions = solved.value("regions", json::array());
        std::vector<std::vector<int>> cellsOf;
        for (const auto& r : regions) cellsOf.push_back(DecodeCells(r.value("cells", json::array()), grid.w));
        for (size_t i = 0; i < cellsOf.size(); ++i) bySize.push_back({cellsOf[i].size(), static_cast<int>(i)});
        std::sort(bySize.rbegin(), bySize.rend()); // the biggest first: the smallest is painted last
        for (const auto& [n, i] : bySize) {
            for (int c : cellsOf[i]) {
                if (c >= 0 && c < static_cast<int>(regionAt.size())) regionAt[c] = i;
            }
        }
    }
    const std::function<std::string(const std::string&, int, int)> takeOf = [&](const std::string& t, int x, int y) -> std::string {
        auto it = takes.find(t);
        if (it == takes.end() || x < 0 || y < 0 || x >= grid.w || y >= grid.h) return t;
        const int region = regionAt[static_cast<size_t>(y) * grid.w + x];
        if (region < 0) return t;
        const unsigned k = CellVariety(region * 7 + 3, region * 13 + 5) % static_cast<unsigned>(it->second + 1);
        return k == 0 ? t : t + "#" + std::to_string(k);
    };
    const HeightField heights = textures.empty() || options.iso.topDown ? HeightField{} : MakeHills(grid, solved, options.hills, groundLook, lookOf);
    std::optional<ImageData> groundPicture;
    if (!options.groundPicture.empty() && options.groundPictureCellPx > 0) groundPicture = DecodeImage(prefabs.GetProject().Resolve(options.groundPicture));
    const bool paintedPlace = !options.paintedFolder.empty();
    const PaintedGround paintedGround = (textures.empty() && !groundPicture) || paintedPlace ? PaintedGround{}
                                        : PaintGround(prefabs.GetProject(), grid, textures, options.place, casters, heights, lighting, groundLook, takeOf,
                                                      groundPicture ? &*groundPicture : nullptr, options.groundPictureCellPx);
    {
        std::set<std::string> pictures;
        for (const auto& [cell, picture] : paintedGround.cells) pictures.insert(picture.first);
        out["groundPictures"] = json(std::vector<std::string>(pictures.begin(), pictures.end()));
    }
    std::set<std::pair<int, int>> painted;
    if (paintedPlace) {
        // The painting shows the ground: the cells only say where one can walk (the marks, else the plan's solid ground).
        const auto blocked = MarkedBlocked(LoadMarks(options.paintedFolder), grid.w, grid.h);
        TileDef open, solid;
        open.sprite.a = solid.sprite.a = 0.0f;
        solid.solid = true;
        const uint16_t openId = layer.Intern(open), solidId = layer.Intern(solid);
        for (int y = 0; y < grid.h; ++y) {
            for (int x = 0; x < grid.w; ++x) {
                const bool no = blocked.empty() ? SolidTerrain(grid.At(x, y)) || grid.At(x, y) == "void" : blocked[static_cast<size_t>(x)][static_cast<size_t>(y)];
                const int gx = x + options.originX, gy = y + options.originY;
                const uint16_t id = no ? solidId : openId;
                if (layer.Get(gx, gy) != id) edit.Set(layer, gx, gy, id);
                painted.insert({gx, gy});
                out["cells"][no ? "blocked" : "walkable"] = out["cells"].value(no ? "blocked" : "walkable", 0) + 1;
            }
        }
    } else
    for (int y = 0; y < grid.h; ++y) {
        for (int x = 0; x < grid.w; ++x) {
            const int gx = x + options.originX, gy = y + options.originY;
            const std::string name = grid.bridges.contains({x, y}) ? "dirt" : grid.At(x, y);
            const json row = terrainsUsed.value(name, json::object());
            if (name == "void") continue; // outside a building: nothing
            const bool water = groundLook.terrains.contains(name) ? groundLook.Solid(name) : SolidTerrain(name);
            uint16_t id = TileLayer::kEmpty;
            if (auto cell = paintedGround.cells.find({x, y}); cell != paintedGround.cells.end() && baseDefs.contains(name)) {
                // One tile kind per picture: each cell shows its square of it (CellSprite).
                TileDef def = baseDefs.at(name);
                def.sprite.texture = cell->second.first;
                def.sprite.u0 = def.sprite.v0 = 0.0f, def.sprite.u1 = def.sprite.v1 = 1.0f;
                def.sprite.rotation = 0;
                def.sprite.uvCells = kGroundChunk;
                def.sprite.uvOriginX = (x / kGroundChunk) * kGroundChunk + options.originX;
                def.sprite.uvOriginY = (y / kGroundChunk) * kGroundChunk + options.originY;
                id = layer.Intern(def);
            }
            // Its height, in the layer: the cell's corners (top, right, bottom, left: (x, y),
            // (x+1, y), (x+1, y+1), (x, y+1)) on the land.
            {
                TileLayer::CornerLift lift{};
                if (!heights.corner.empty() && paintedGround.cells.contains({x, y})) {
                    auto c = [&](int i, int j) { return heights.corner[static_cast<size_t>(j) * (grid.w + 1) + i]; };
                    lift = {c(x, y), c(x + 1, y), c(x + 1, y + 1), c(x, y + 1)};
                }
                edit.SetLift(layer, gx, gy, lift);
            }
            if (id == TileLayer::kEmpty && row.contains("terrain")) {
                const std::string center = terrainAt(gx, gy);
                const EdgeSignature target = TargetSignature(terrainAt, gx, gy, background.empty() ? center : background);
                if (auto choice = ChooseTile(terrains, target, center, CellVariety(gx, gy))) {
                    id = defFor(choice->prefab, choice->rotation, water);
                }
            }
            if (id == TileLayer::kEmpty && row.contains("prefab")) {
                // Its variants (and their turns) by cell: a field of one ground without a repeat.
                const json variants = row.value("variants", json::array({row["prefab"]}));
                const unsigned v = CellVariety(gx, gy);
                id = defFor(variants.empty() ? row["prefab"] : variants[v % variants.size()], static_cast<int>((v >> 8) % 4), water);
            }
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
    json list = solved.value("objects", json::array()); // pointers below point into it
    // Which way each thing faces, from where it stands (its role's look "orient"), and which of its kind's looks it
    // shows: neighbours of a kind get different ones.
    {
        const json byKey = assetPlan.value("byObject", json::object());
        std::map<std::string, std::vector<json*>> ofKey;
        for (auto& o : list) {
            const std::string key = byKey.value(o.value("id", ""), "");
            const auto c = choices.find(key);
            if (c == choices.end()) continue;
            const std::string orient = c->second.value("look", json::object()).value("orient", "none");
            // The plan's facing (FaceObjects, by the arrangement's rule) first; else worked out here.
            if (orient != "none" && o.value("layer", "") != "building" && !o.contains("faced")) o["facing"] = FacingFromPlace(o, orient, grid, solved, groundLook);
            ofKey[key].push_back(&o);
        }
        for (auto& [key, of] : ofKey) {
            std::stable_sort(of.begin(), of.end(), [](const json* a, const json* b) {
                return std::make_pair(a->value("y", 0), a->value("x", 0)) < std::make_pair(b->value("y", 0), b->value("x", 0));
            });
            for (size_t i = 0; i < of.size(); ++i) (*of[i])["variant"] = static_cast<int>(i);
        }
    }
    std::vector<const json*> objects;
    for (const auto& o : list) objects.push_back(&o);
    std::stable_sort(objects.begin(), objects.end(), [](const json* a, const json* b) {
        return a->value("y", 0) + a->value("h", 1) < b->value("y", 0) + b->value("h", 1);
    });
    // Which need each object is (the plan worked it out from the Spec).
    const json byObject = assetPlan.value("byObject", json::object());
    // A door stands across its doorway: the corridor's way, from the ground round it.
    PlaceSceneOptions placing = options;
    placing.doorAcrossX = [&grid, &groundLook](int x, int y) {
        auto open = [&](int cx, int cy) {
            return cx >= 0 && cy >= 0 && cx < grid.w && cy < grid.h && !groundLook.Wall(grid.At(cx, cy)) && grid.At(cx, cy) != "void";
        };
        const bool alongX = open(x - 1, y) && open(x + 1, y), alongY = open(x, y - 1) && open(x, y + 1);
        return alongX && !alongY;
    };
    std::map<std::pair<int, int>, std::string> onWallAt; // wall cells that show a thing mounted on them
    for (const json* o : objects) {
        const std::string id = o->value("id", "");
        if (Kept(options, o->value("node", ""), id)) continue; // not this edit's, or locked: as it is
        if (options.paintedIds.contains(id)) {
            out["painted"] = out.value("painted", 0) + 1; // in the painting
            continue;
        }
        const std::string key = byObject.value(id, "");
        auto it = choices.find(key);
        if (it != choices.end() && it->second.value("source", "") == "dropped") {
            out["dropped"] = out.value("dropped", 0) + 1; // excluded, or optional with nothing to show
            continue;
        }
        if (it != choices.end() && it->second.value("source", "") == "background") continue; // ground, painted in the background
        // Mounted on a wall (a relation whose art is "on-run", with that wall cell made): the nearest wall cell shown
        // gets the piece with it painted on; nothing stands on the floor. Its light still shines (from the plan).
        if (it != choices.end() && it->second.contains("onWall")) {
            json relations = o->value("relations", json());
            if (!relations.is_array()) relations = it->second.value("look", json::object()).value("relations", json::array());
            const json art = assetPlan.value("relationArt", json::object());
            bool mounted = false;
            for (const auto& r : relations.is_array() ? relations : json::array()) mounted = mounted || art.value(r.value("type", ""), "") == "on-run";
            if (mounted) {
                const int ox = o->value("x", 0), oy = o->value("y", 0);
                auto shown = [&](int cx, int cy) {
                    auto floor = [&](int fx, int fy) { return fx >= 0 && fy >= 0 && fx < grid.w && fy < grid.h && !groundLook.Wall(grid.At(fx, fy)) && grid.At(fx, fy) != "void"; };
                    return cx >= 0 && cy >= 0 && cx < grid.w && cy < grid.h && groundLook.Wall(grid.At(cx, cy)) && (floor(cx + 1, cy) || floor(cx, cy + 1) || floor(cx + 1, cy + 1));
                };
                int best = 99;
                std::pair<int, int> at{-1, -1};
                for (int dy = -2; dy <= 2; ++dy) {
                    for (int dx = -2; dx <= 2; ++dx) {
                        if (shown(ox + dx, oy + dy) && std::abs(dx) + std::abs(dy) < best) best = std::abs(dx) + std::abs(dy), at = {ox + dx, oy + dy};
                    }
                }
                if (at.first >= 0) {
                    onWallAt[at] = it->second.value("onWall", "");
                    out["objects"]["mounted"] = out["objects"].value("mounted", 0) + 1;
                    continue;
                }
            }
            // No wall near, and its only art is the wall piece (no floor version made): not stood on the floor as a
            // wall block. Its light still shines.
            if (it->second.value("prefab", "") == it->second.value("onWall", "")) {
                out["objects"]["unmounted"] = out["objects"].value("unmounted", 0) + 1;
                continue;
            }
        }
        // A building on a lot the map's edge cuts: shown at its kind's own size (the plan's: the footprint the same kind
        // has away from the edge), reaching past the edge with the art made for that size, and cut at the edge, so the
        // map reads as a window on a bigger place, not a row of shrunken buildings.
        json shown = *o;
        json shownChoice = it != choices.end() ? it->second : json();
        if (o->value("layer", "") == "building" && it != choices.end()) {
            const auto ext = OwnSize(*o, objects, grid.w, grid.h);
            if (ext) {
                shown["x"] = (*ext)[0], shown["y"] = (*ext)[1], shown["w"] = (*ext)[2], shown["h"] = (*ext)[3];
                // The art made for that size (a need of the same role with that footprint), else its own.
                const std::string role = it->second.value("role", "");
                for (const auto& [k, row] : choices) {
                    if (row.value("role", "") == role && row.value("w", 0) == (*ext)[2] && row.value("h", 0) == (*ext)[3] && row.contains("prefab") &&
                        row.value("source", "") != "placeholder") {
                        shownChoice["prefab"] = row["prefab"];
                        shownChoice.erase("prefabs");
                        if (row.contains("prefabs")) shownChoice["prefabs"] = row["prefabs"];
                        break;
                    }
                }
                out["extended"] = out.value("extended", 0) + 1;
            }
        }
        const auto placedEntity = it == choices.end() ? std::nullopt : PlaceObject(registry, prefabs, shown, shownChoice, placing);
        if (!placedEntity) {
            out["missing"].push_back(id);
            continue;
        }
        // On the map's edge: cut there.
        if (shown.value("layer", "") == "building" && (shown.value("x", 0) <= 2 || shown.value("y", 0) <= 2 ||
                                                       shown.value("x", 0) + shown.value("w", 1) >= grid.w - 2 || shown.value("y", 0) + shown.value("h", 1) >= grid.h - 2)) {
            if (ClipToMap(registry, prefabs, *placedEntity, shown, grid.w, grid.h, options)) out["clipped"] = out.value("clipped", 0) + 1;
        }
        const std::string layerName = o->value("layer", "prop");
        out["objects"][layerName] = out["objects"].value(layerName, 0) + 1;
    }
    // Fences, walls and hedges: a piece on every edge (gates left open). A fence within a fence
    // (a garden's, in a yard that has its own) isn't drawn: two fences a cell apart read as
    // clutter (place-refine.md §5).
    const json pieces = assetPlan.value("pieces", json::object());
    std::set<std::string> lineIds;
    for (const auto& line : solved.value("lines", json::array())) lineIds.insert(line.value("id", ""));
    for (const auto& line : solved.value("lines", json::array())) {
        if (Kept(options, line.value("node", ""), line.value("id", ""))) continue;
        if (paintedPlace) continue; // painted
        if (const std::string lid = line.value("id", ""); lid.size() > 6 && lid.compare(lid.size() - 6, 6, "/fence") == 0) {
            const std::string area = lid.substr(0, lid.size() - 6);
            if (area.find('/') != std::string::npos && lineIds.contains(area.substr(0, area.rfind('/')) + "/fence-line")) continue;
        } else if (lid.size() > 11 && lid.compare(lid.size() - 11, 11, "/yard-fence") == 0) {
            if (lineIds.contains(lid.substr(0, lid.size() - 11) + "/fence-line")) continue;
        }
        if (line.value("slot", "") == "wall") {
            // An indoor place's walls (docs/build-fixes.md): its wall block on each edge, the floor
            // shown as in Diablo or Divinity: a wall with the floor on the viewer's side of it (the
            // room behind it is seen) stands whole; one between two rooms, lower; one in front of a
            // room (it would hide it) is left out.
            std::string block;
            const json partRows = assetPlan.value("parts", json::object()); // named: items() of a temporary dangles
            for (const auto& [key, part] : partRows.items()) {
                if (part.value("at", "") == "wall-edge") block = part.value("prefab", "");
            }
            if (!block.empty()) {
                auto floorCell = [&](int cx, int cy) {
                    return cx >= 0 && cy >= 0 && cx < grid.w && cy < grid.h && !groundLook.Wall(grid.At(cx, cy)) && grid.At(cx, cy) != "void" &&
                           !grid.At(cx, cy).empty();
                };
                std::set<std::tuple<int, int, std::string>> doorGaps;
                for (const auto& g : line.value("gaps", json::array())) doorGaps.insert({g[0].get<int>(), g[1].get<int>(), g[2].get<std::string>()});
                for (const auto& e : line.value("edges", json::array())) {
                    const int x = e[0].get<int>(), y = e[1].get<int>();
                    const std::string side = e[2].get<std::string>();
                    if (doorGaps.contains({x, y, side})) continue;
                    // The cells either side: "far" up the screen (lower x or y), "near" towards the viewer.
                    int fx = x, fy = y, nx = x, ny = y;
                    if (side == "n") fy = y - 1;
                    if (side == "s") ny = y + 1;
                    if (side == "w") fx = x - 1;
                    if (side == "e") nx = x + 1;
                    const bool farFloor = floorCell(fx, fy), nearFloor = floorCell(nx, ny);
                    if (lighting.indoor && !nearFloor) continue; // in front of a room: left out (indoors)
                    float wx = x + options.originX + 0.5f, wy = y + options.originY + 0.5f;
                    if (side == "n") wy -= 0.5f;
                    if (side == "s") wy += 0.5f;
                    if (side == "w") wx -= 0.5f;
                    if (side == "e") wx += 0.5f;
                    auto placed = prefabs.Instantiate(registry, block, wx, wy, false);
                    if (!placed) continue;
                    registry.emplace_or_replace<PlaceNode>(*placed, options.place, line.value("node", ""), line.value("id", ""));
                    registry.remove<Obstacle>(*placed); // an edge: the cells beside it stay walkable
                    if (const auto* sprite = registry.try_get<Sprite>(*placed); sprite && sprite->width > 0) {
                        const float factor = options.iso.tileWidth * (lighting.indoor ? (farFloor ? 0.62f : 0.82f) : 1.0f) / sprite->width;
                        if (std::abs(factor - 1.0f) > 0.02f) ScaleEntity(EngineReflection(), registry, *placed, factor);
                    }
                    out["pieces"] = out["pieces"].get<int>() + 1;
                }
                continue;
            }
        }
        const json row = pieces.value(line.value("slot", "fence"), json::object());
        const json kinds = row.value("pieces", json::object());
        const bool fit = row.value("fit", false) && !options.iso.topDown;
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
            // A section made for this place: it spans the cell's edge (half a tile across on
            // screen), and stands on the middle of its slanting foot, not its lowest point.
            if (fit) wx += 0.25f, wy += 0.25f;
            auto placed = prefabs.Instantiate(registry, prefab, wx, wy, false);
            if (!placed) continue;
            if (const auto* sprite = registry.try_get<Sprite>(*placed); fit && sprite && sprite->width > 0) {
                // Its visible part spans the edge (half a tile across on screen), a little over so the
                // next one joins it: measured once, the picture's opaque width (its margins aside).
                static std::map<std::string, float> opaqueOf;
                auto it = opaqueOf.find(sprite->texture);
                if (it == opaqueOf.end()) {
                    float fraction = 1.0f;
                    if (const auto image = DecodeImage(prefabs.GetProject().Resolve(sprite->texture)); image && image->width > 0) {
                        const int w = static_cast<int>(image->width), h = static_cast<int>(image->height);
                        int x0 = w, x1 = -1;
                        for (int y = 0; y < h; ++y) {
                            for (int x = 0; x < w; ++x) {
                                if (image->rgba[(static_cast<size_t>(y) * w + x) * 4 + 3] > 128) x0 = std::min(x0, x), x1 = std::max(x1, x);
                            }
                        }
                        if (x1 >= x0) fraction = std::max(0.2f, static_cast<float>(x1 - x0 + 1) / w);
                    }
                    it = opaqueOf.emplace(sprite->texture, fraction).first;
                }
                const float factor = options.iso.tileWidth * 0.5f * 1.1f / (sprite->width * it->second);
                if (std::abs(factor - 1.0f) > 0.02f) ScaleEntity(EngineReflection(), registry, *placed, factor);
            }
            registry.emplace_or_replace<PlaceNode>(*placed, options.place, line.value("node", ""), line.value("id", ""));
            registry.remove<Obstacle>(*placed); // an edge, not a cell: the fence doesn't fill the cells beside it
            if (mirror) MirrorEntity(EngineReflection(), registry, *placed);
            out["pieces"] = out["pieces"].get<int>() + 1;
        }
    }
    // On high ground, each thing stands at the land's height under its foot.
    if (!heights.corner.empty()) {
        for (auto e : PlaceEntities(registry, options.place)) {
            const auto* sprite = registry.try_get<Sprite>(e);
            const auto* at = registry.try_get<WorldTransform2D>(e);
            if (sprite == nullptr || at == nullptr || registry.all_of<Tile>(e)) continue;
            const float lift = heights.At(at->x - options.originX, at->y - options.originY);
            if (lift > 0.5f) EditField(EngineReflection(), registry, e, "Sprite", "lift", lift);
        }
    }
    // A dungeon's walls stand behind its floor (the wall cells with floor in front of them,
    // towards the viewer); the ones in front are left out, so the rooms show (as in Diablo).
    // Its torches hang on them, where its lights are.
    if (lighting.indoor && !paintedPlace) {
        std::string wall, wallEnd, torch, torchOnWall;
        bool wallRun = false;
        const json partRows = assetPlan.value("parts", json::object()); // named: items() of a temporary dangles
        for (const auto& [key, part] : partRows.items()) {
            if (part.value("at", "") == "wall-edge") wall = part.value("prefab", ""), wallEnd = part.value("endPrefab", ""), wallRun = part.value("run", false);
            if (part.value("at", "") == "lights") torch = part.value("prefab", ""), torchOnWall = part.value("onWall", "");
        }
        auto floorAt = [&](int x, int y) { return x >= 0 && y >= 0 && x < grid.w && y < grid.h && !groundLook.Wall(grid.At(x, y)) && grid.At(x, y) != "void"; };
        int walls = 0;
        for (int y = 0; y < grid.h && !wall.empty(); ++y) {
            for (int x = 0; x < grid.w; ++x) {
                if (!groundLook.Wall(grid.At(x, y)) || !(floorAt(x + 1, y) || floorAt(x, y + 1) || floorAt(x + 1, y + 1))) continue;
                // A run's piece is cut to its cell exactly (its foot the cell's front corner, a quarter cell below
                // where a thing stands): set a quarter cell forward, a cell wide, so neighbours meet with no gap. It's
                // made running along x, its long face to the south: a wall showing its face to the east is mirrored.
                const float forward = wallRun ? 0.25f : 0.0f;
                // Its face to the east (floor only there): mirrored. Inside a run its middle piece; where the run stops
                // (no shown wall next along it, towards the viewer) its end piece, the end painted.
                const bool east = floorAt(x + 1, y) && !floorAt(x, y + 1);
                auto shownWall = [&](int cx, int cy) {
                    return cx >= 0 && cy >= 0 && cx < grid.w && cy < grid.h && groundLook.Wall(grid.At(cx, cy)) &&
                           (floorAt(cx + 1, cy) || floorAt(cx, cy + 1) || floorAt(cx + 1, cy + 1));
                };
                const bool ends = east ? !shownWall(x, y + 1) : !shownWall(x + 1, y);
                // A cell with a torch, or a thing mounted on it: that wall cell, painted with it.
                const bool torchHere = !torchOnWall.empty() && std::find(torches.begin(), torches.end(), std::pair{x, y}) != torches.end();
                const auto mountedHere = onWallAt.find({x, y});
                const std::string& piece = mountedHere != onWallAt.end() ? mountedHere->second
                                           : torchHere                  ? torchOnWall
                                           : wallRun && ends && !wallEnd.empty() ? wallEnd : wall;
                auto placed = prefabs.Instantiate(registry, piece, x + options.originX + 0.5f + forward, y + options.originY + 0.5f + forward, false);
                if (!placed) continue;
                registry.emplace_or_replace<PlaceNode>(*placed, options.place, "walls", "walls#" + std::to_string(x) + "," + std::to_string(y));
                // A block drawn to the grid (one cell by its foot) as it is; a lone block of other art a cell wide.
                if (const auto* sprite = registry.try_get<Sprite>(*placed); sprite && sprite->width > 0 && !wallRun) {
                    const float factor = options.iso.tileWidth * 1.02f / sprite->width;
                    if (std::abs(factor - 1.0f) > 0.002f) ScaleEntity(EngineReflection(), registry, *placed, factor);
                }
                if (wallRun && east) MirrorEntity(EngineReflection(), registry, *placed);
                ++walls;
            }
        }
        // A plain door whose walls are both cut away (in front of a room) would stand alone in the dark:
        // left out, the doorway open (docs/build-fixes.md). Locked and secret doors stay (play needs them).
        if (!wall.empty()) {
            auto wallShown = [&](int x, int y) {
                return x >= 0 && y >= 0 && x < grid.w && y < grid.h && groundLook.Wall(grid.At(x, y)) &&
                       (floorAt(x + 1, y) || floorAt(x, y + 1) || floorAt(x + 1, y + 1));
            };
            std::set<std::string> floating;
            for (const auto& o : solved.value("objects", json::array())) {
                if (o.value("slot", "") != "door") continue;
                const int x = o.value("x", 0), y = o.value("y", 0);
                if (!wallShown(x - 1, y) && !wallShown(x + 1, y) && !wallShown(x, y - 1) && !wallShown(x, y + 1)) floating.insert(o.value("id", ""));
            }
            std::vector<Entity> gone;
            for (auto [e, node] : registry.view<PlaceNode>().each()) {
                if (node.place == options.place && floating.contains(node.statement)) gone.push_back(e);
            }
            for (auto e : gone) registry.destroy(e);
            out["doorsOpen"] = static_cast<int>(gone.size());
        }
        int hung = 0;
        for (const auto& [x, y] : torches) {
            if (torch.empty() || !torchOnWall.empty()) break; // painted on their wall cells instead
            const float fx = floorAt(x + 1, y) ? 0.5f : 0.0f, fy = fx == 0.0f ? 0.5f : 0.0f;
            auto placed = prefabs.Instantiate(registry, torch, x + options.originX + 0.5f + fx, y + options.originY + 0.5f + fy, false);
            if (!placed) continue;
            registry.emplace_or_replace<PlaceNode>(*placed, options.place, "torches", "torches#" + std::to_string(x) + "," + std::to_string(y));
            EditField(EngineReflection(), registry, *placed, "Sprite", "lift", options.iso.tileHeight * 0.9f); // on the wall's face
            ++hung;
        }
        out["walls"] = walls;
        out["torches"] = hung;
        // What hangs on a wall (its role's look "hang": a banner, a shield) goes on the wall's face
        // (docs/build-fixes.md): beside it, the back walls first (they're the ones seen), lifted.
        int hanging = 0;
        std::map<std::string, std::pair<int, int>> hangAt;
        for (const auto& o : solved.value("objects", json::array())) {
            const std::string key = byObject.value(o.value("id", ""), "");
            const auto look = lookByKey.find(key);
            if (look == lookByKey.end() || !look->second.is_object() || !look->second.value("hang", false)) continue;
            hangAt[o.value("id", "")] = {o.value("x", 0), o.value("y", 0)};
        }
        std::set<std::tuple<int, int, char>> lineWalls; // wall edges of an interior's rooms: (x, y, n|w)
        for (const auto& line : solved.value("lines", json::array())) {
            if (line.value("slot", "") != "wall") continue;
            for (const auto& e : line.value("edges", json::array())) {
                const int x = e[0].get<int>(), y = e[1].get<int>();
                const std::string side = e[2].get<std::string>();
                if (side == "n") lineWalls.insert({x, y, 'n'});
                if (side == "w") lineWalls.insert({x, y, 'w'});
                if (side == "s") lineWalls.insert({x, y + 1, 'n'});
                if (side == "e") lineWalls.insert({x + 1, y, 'w'});
            }
        }
        for (auto [e, node] : registry.view<PlaceNode>().each()) {
            const auto at = hangAt.find(node.statement);
            if (node.place != options.place || at == hangAt.end()) continue;
            const auto [x, y] = at->second;
            // The wall it hangs on: north or west (seen), else south or east.
            float dx = 0, dy = 0;
            if (!floorAt(x, y - 1) || lineWalls.contains({x, y, 'n'})) dy = -0.45f;
            else if (!floorAt(x - 1, y) || lineWalls.contains({x, y, 'w'})) dx = -0.45f;
            else if (!floorAt(x, y + 1) || lineWalls.contains({x, y + 1, 'n'})) dy = 0.45f;
            else if (!floorAt(x + 1, y) || lineWalls.contains({x + 1, y, 'w'})) dx = 0.45f;
            else continue; // no wall beside it: it stands where it is
            if (auto* w = registry.try_get<WorldTransform2D>(e)) {
                w->x = x + options.originX + 0.5f + dx;
                w->y = y + options.originY + 0.5f + dy;
            }
            registry.remove<Obstacle>(e);
            EditField(EngineReflection(), registry, e, "Sprite", "lift", options.iso.tileHeight * 0.8f);
            ++hanging;
        }
        out["hanging"] = hanging;
    }
    // Locks and keys that work in play (development_plan.md 13.5): a locked door
    // opens with its key; the key lies in its room to be picked up. Each key is an
    // item record of its own, so it has a name in the inventory.
    // The things toned to the background (docs/dungeon.md §7): painted apart, by another model, they come out in another
    // light (a grey wall on a brown floor). Each picture's colour on average against the background's, a little brighter
    // (a thing standing up catches more light than the floor): each channel most of the way there, never brighter (a
    // sprite is only tinted down), so walls, doors and props sit in the ground's light. Lights keep theirs.
    if (groundPicture && groundPicture->width > 0) {
        double bg[3] = {0, 0, 0};
        long nb = 0;
        const ImageData& g = *groundPicture;
        for (size_t i = 0; i < g.rgba.size(); i += 4 * 97) {
            if (g.rgba[i] + g.rgba[i + 1] + g.rgba[i + 2] < 30) continue; // rock and the void
            for (int c = 0; c < 3; ++c) bg[c] += g.rgba[i + c];
            ++nb;
        }
        constexpr double lift = 1.15, strength = 0.8;
        std::map<std::string, std::optional<std::array<float, 3>>> gains;
        json toned = json::object();
        for (auto [e, node, sprite] : registry.view<PlaceNode, Sprite>(entt::exclude<Tile>).each()) {
            if (nb <= 100 || node.place != options.place || sprite.emissive || sprite.texture.empty()) continue;
            auto it = gains.find(sprite.texture);
            if (it == gains.end()) {
                std::optional<std::array<float, 3>> gain;
                if (const auto image = DecodeImage(prefabs.GetProject().Resolve(sprite.texture))) {
                    double art[3] = {0, 0, 0};
                    long na = 0;
                    for (size_t i = 0; i < image->rgba.size(); i += 4 * 7) {
                        if (image->rgba[i + 3] < 128) continue;
                        for (int c = 0; c < 3; ++c) art[c] += image->rgba[i + c];
                        ++na;
                    }
                    if (na > 50) {
                        gain = std::array<float, 3>{};
                        for (int c = 0; c < 3; ++c) {
                            const double want = lift * bg[c] / nb / std::max(1.0, art[c] / na);
                            (*gain)[c] = static_cast<float>(std::clamp(1.0 + strength * (want - 1.0), 0.45, 1.0));
                        }
                        toned[sprite.texture] = {(*gain)[0], (*gain)[1], (*gain)[2]};
                    }
                }
                it = gains.emplace(sprite.texture, gain).first;
            }
            if (!it->second) continue;
            sprite.r *= (*it->second)[0], sprite.g *= (*it->second)[1], sprite.b *= (*it->second)[2];
        }
        if (!toned.empty()) out["toned"] = toned;
    }
    // Every thing of the place through its grade's brightness and tint (a sprite can't be desaturated).
    if (options.gradeBrightness != 1.0f || options.gradeTint != std::array<float, 3>{1.0f, 1.0f, 1.0f}) {
        for (auto [e, node, sprite] : registry.view<PlaceNode, Sprite>().each()) {
            if (node.place != options.place || sprite.emissive) continue;
            sprite.r *= options.gradeBrightness * options.gradeTint[0];
            sprite.g *= options.gradeBrightness * options.gradeTint[1];
            sprite.b *= options.gradeBrightness * options.gradeTint[2];
        }
    }
    if (paintedPlace) {
        // The painting at the back, its occluders over whoever walks behind them (its own colours: not graded again).
        out["painting"] = AddPaintingEntities(registry, options.paintedFolder, "places/" + options.place, options.place, options.originX,
                                              options.originY, options.iso);
    }
    out["locks"] = WireLocksAndKeys(registry, prefabs, solved, options);
    // Furniture characters use (14.3): beds, chairs, counters, forges become smart
    // objects their routines find by type and affordance.
    out["smartObjects"] = WireSmartObjects(registry, assetPlan, options);
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
    std::set<std::string> dropped; // needs left out on purpose
    for (const auto& n : assetPlan.value("needs", json::array())) {
        if (n.value("source", "") == "dropped") dropped.insert(n.value("key", ""));
    }
    const json byObject = assetPlan.value("byObject", json::object());
    for (const auto& o : solved.value("objects", json::array())) {
        if (dropped.contains(byObject.value(o.value("id", ""), ""))) continue;
        if (options.paintedIds.contains(o.value("id", ""))) continue; // in the painting
        ++expected;
        auto it = byStatement.find(o.value("id", ""));
        if (it == byStatement.end()) {
            if (missing.size() < 30) missing.push_back(o.value("id", ""));
            continue;
        }
        ++found;
        const auto* world = registry.try_get<WorldTransform2D>(it->second);
        const Stand stand = StandOf(o, options);
        // Within its cell, allowing the quarter cell art drawn to the grid is set forward to stand on its foot (PlaceObject).
        if (world == nullptr || std::abs(world->x - stand.x) > 0.8f || std::abs(world->y - stand.y) > 0.8f) {
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

    // Licences: every asset used is allowed (the project's own, generated, or a licence the project takes).
    {
        LicencePolicy policy;
        const json used = assetPlan.value("licencePolicy", json::object());
        policy.shareAlike = used.value("shareAlike", false);
        for (const auto& l : used.value("extra", json::array())) policy.extra.insert(l.get<std::string>());
        json bad = json::array();
        for (const auto& n : assetPlan.value("needs", json::array())) {
            const std::string source = n.value("source", "");
            if (source != "web" && source != "library") continue;
            if (!policy.Allowed(n.value("licence", ""))) bad.push_back(n.value("entry", n.value("key", "")));
        }
        checks.push_back({{"id", "licences"}, {"what", "Every asset's licence is allowed"}, {"ok", bad.empty()},
                          {"detail", bad.empty() ? "all of them" : std::to_string(bad.size()) + " not allowed"}, {"items", bad}});
    }
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

int FitObstaclesToArt(Registry& registry, const Project& project, const IsoParams& iso) {
    if (iso.topDown || iso.tileWidth <= 0.0f) return 0;
    // Each picture's ground silhouette (its lowest 40 %): how far it reaches left and right of its middle, as shares of
    // its width (kept by picture).
    static std::map<std::string, std::pair<float, float>> reach;
    int changed = 0;
    for (auto [e, obstacle, sprite] : registry.view<Obstacle, Sprite>().each()) {
        if ((obstacle.left <= 0 && obstacle.back <= 0) || sprite.texture.empty() || sprite.flat) continue;
        auto it = reach.find(sprite.texture);
        if (it == reach.end()) {
            std::pair<float, float> r{0.5f, 0.5f};
            if (const auto image = DecodeImage(project.Resolve(sprite.texture)); image && image->width > 2 && image->height > 2) {
                const int w = static_cast<int>(image->width), h = static_cast<int>(image->height);
                auto opaque = [&](int x, int y) { return image->rgba[(static_cast<size_t>(y) * w + x) * 4 + 3] > 128; };
                int top = h, bottom = -1;
                for (int y = 0; y < h; ++y) {
                    for (int x = 0; x < w; ++x) {
                        if (opaque(x, y)) {
                            top = std::min(top, y), bottom = std::max(bottom, y);
                            break;
                        }
                    }
                }
                if (bottom > top) {
                    int x0 = w, x1 = -1;
                    for (int y = bottom - static_cast<int>((bottom - top) * 0.4f); y <= bottom; ++y) {
                        for (int x = 0; x < w; ++x) {
                            if (opaque(x, y)) x0 = std::min(x0, x), x1 = std::max(x1, x);
                        }
                    }
                    if (x1 > x0) r = {0.5f - static_cast<float>(x0) / w, static_cast<float>(x1 + 1) / w - 0.5f};
                }
            }
            it = reach.emplace(sprite.texture, r).first;
        }
        // Its pixels on screen; mirrored, its left is its right. A cell further along x reaches half a tile further
        // left, along y further right; the footprint's own corner is half a tile out from its front cell's middle.
        float left = it->second.first * std::abs(sprite.width), right = it->second.second * std::abs(sprite.width);
        if (sprite.u0 > sprite.u1) std::swap(left, right);
        const float half = iso.tileWidth * 0.5f;
        const int fitLeft = std::clamp(static_cast<int>(std::lround(left / half)) - 1, 0, obstacle.left);
        const int fitBack = std::clamp(static_cast<int>(std::lround(right / half)) - 1, 0, obstacle.back);
        if (fitLeft == obstacle.left && fitBack == obstacle.back) continue;
        obstacle.left = fitLeft;
        obstacle.back = fitBack;
        for (const char* field : {"left", "back"}) RecordOverride(registry, e, "Obstacle", field);
        ++changed;
    }
    return changed;
}

int RepairPlaceSolidity(Registry& registry, const Project& project, const Ontology& ontology, bool topDown) {
    struct Plan {
        json spec;
        std::map<std::string, json> objects;
    };
    std::map<std::string, Plan> plans;
    auto planOf = [&](const std::string& place) -> const Plan* {
        auto it = plans.find(place);
        if (it == plans.end()) {
            Plan plan;
            auto read = [&](const std::string& name) {
                std::ifstream in(project.Resolve("places/" + place + "/" + name), std::ios::binary);
                return in ? json::parse(in, nullptr, false) : json();
            };
            plan.spec = read("spec.json");
            const json solved = read("layout.solved.json");
            if (solved.is_object()) {
                for (const auto& o : solved.value("objects", json::array())) {
                    if (o.is_object() && o.contains("id")) plan.objects[o.value("id", "")] = o;
                }
            }
            it = plans.emplace(place, std::move(plan)).first;
        }
        return it->second.spec.is_object() ? &it->second : nullptr;
    };
    std::vector<Entity> placed;
    for (auto [e, node] : registry.view<PlaceNode>().each()) {
        if (!node.place.empty() && !node.statement.empty()) placed.push_back(e);
    }
    int changed = 0;
    PlaceSceneOptions options;
    options.iso.topDown = topDown;
    for (Entity e : placed) {
        const auto& node = registry.get<PlaceNode>(e);
        const Plan* plan = planOf(node.place);
        if (!plan) continue;
        auto it = plan->objects.find(node.statement);
        if (it == plan->objects.end() || it->second.value("layer", "prop") == "building") continue;
        const Need need = NeedOf(ontology, plan->spec, it->second);
        if (need.kind == "building") continue;
        const bool solid = ontology.IsSolid(need.role);
        const Stand stand = StandOf(it->second, options);
        if (solid) {
            const auto* had = registry.try_get<Obstacle>(e);
            if (had && had->left == stand.left && had->right == stand.right && had->back == stand.back) continue;
            auto& obstacle = registry.emplace_or_replace<Obstacle>(e);
            obstacle.left = stand.left;
            obstacle.right = stand.right;
            obstacle.back = stand.back;
            for (const char* field : {"left", "right", "back"}) RecordOverride(registry, e, "Obstacle", field);
            ++changed;
        } else if (registry.all_of<Obstacle>(e)) {
            registry.remove<Obstacle>(e);
            RecordOverride(registry, e, "Obstacle", "");
            ++changed;
        }
    }
    return changed;
}

} // namespace aige
