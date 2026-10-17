#include "editing/VillageBuild.h"

#include "assets/AssetDatabase.h"
#include "assets/Prefab.h"
#include "editing/Edits.h"
#include "editing/SceneTools.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/Components.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/GameSession.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <set>
#include <sstream>

namespace aige {

namespace {

// The words of a text, lowercased: "Stone well, village" -> {stone, well, village}.
std::set<std::string> Words(const std::string& text) {
    std::set<std::string> words;
    std::string word;
    for (char c : text + " ") {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (!word.empty()) {
            words.insert(word);
            // "trees" counts as "tree", "bushes" as "bush"
            if (word.size() > 3 && word.back() == 's') words.insert(word.substr(0, word.size() - 1));
            if (word.size() > 4 && word.ends_with("es")) words.insert(word.substr(0, word.size() - 2));
            word.clear();
        }
    }
    return words;
}

bool AnyOf(const std::set<std::string>& words, std::initializer_list<const char*> wanted) {
    for (const char* w : wanted) {
        if (words.contains(w)) return true;
    }
    return false;
}

std::string Field(const std::map<std::string, std::string>& analysis, const char* key) {
    auto it = analysis.find(key);
    return it == analysis.end() ? std::string() : it->second;
}

// How wide each role's things should be, in cells: {smallest, usual, largest}.
// Fences are measured along their longest side: one piece spans one cell.
struct RoleSize {
    float min, usual, max;
};
RoleSize SizeOf(VillageRole role, int houseCells) {
    const float h = static_cast<float>(houseCells);
    switch (role) {
    case VillageRole::House: return {1.0f, h * 0.9f, h};
    case VillageRole::Landmark: return {1.5f, h, h + 0.5f};
    case VillageRole::Well: return {0.8f, 1.1f, 1.5f};
    case VillageRole::Tree: return {0.8f, 1.5f, 2.2f};
    case VillageRole::Bush: return {0.4f, 0.8f, 1.2f};
    case VillageRole::Rock: return {0.3f, 0.7f, 1.2f};
    case VillageRole::Decoration: return {0.3f, 0.7f, 1.2f};
    case VillageRole::Entrance: return {0.3f, 0.6f, 1.2f};
    case VillageRole::FenceH:
    case VillageRole::FenceV: return {1.0f, 1.0f, 1.0f};
    }
    return {1.0f, 1.0f, 1.0f};
}

bool IsFence(VillageRole role) { return role == VillageRole::FenceH || role == VillageRole::FenceV; }

const std::vector<VillageRole> kUsedRoles = {VillageRole::House, VillageRole::Well,       VillageRole::FenceH,
                                             VillageRole::Tree,  VillageRole::Bush,       VillageRole::Rock,
                                             VillageRole::Decoration, VillageRole::Entrance, VillageRole::Landmark};

} // namespace

std::vector<VillageRole> VillageRolesOf(const std::map<std::string, std::string>& analysis, const std::string& prefabName) {
    const std::string kind = Field(analysis, "kind");
    if (kind == "ground" || kind == "character" || kind == "item" || kind == "effect") return {};
    // What it is, most telling first: the analysis' name and tags, then its
    // description; without any analysis, the prefab's own name.
    const bool described = !Field(analysis, "name").empty() || !Field(analysis, "tags").empty();
    const std::set<std::string> name = Words(Field(analysis, "name") + " " + Field(analysis, "tags") +
                                             (described ? std::string() : " " + prefabName));
    const std::set<std::string> all = [&] {
        auto words = name;
        words.merge(Words(Field(analysis, "description")));
        return words;
    }();

    // Fences and wall pieces both close the gardens.
    if (AnyOf(name, {"fence", "fencing", "palisade", "railing", "hedge", "wall"})) {
        const std::string direction = Field(analysis, "direction");
        if (direction == "horizontal" || (direction.empty() && AnyOf(name, {"horizontal"}))) return {VillageRole::FenceH};
        if (direction == "vertical" || (direction.empty() && AnyOf(name, {"vertical"}))) return {VillageRole::FenceV};
        return {VillageRole::FenceH, VillageRole::FenceV};
    }
    if (AnyOf(name, {"well"})) return {VillageRole::Well};
    if (AnyOf(name, {"tower", "castle", "windmill", "mill", "keep", "fort", "fortress", "temple", "church", "chapel",
                     "lighthouse", "watchtower"})) {
        return {VillageRole::Landmark};
    }
    if (AnyOf(name, {"banner", "flag", "signpost", "sign", "lamppost", "lantern", "torch", "gate", "archway"})) {
        return {VillageRole::Entrance};
    }
    if (kind == "building" ||
        AnyOf(name, {"house", "cottage", "home", "hut", "cabin", "inn", "tavern", "shop", "barn", "warehouse",
                     "smithy", "bakery", "stable", "hovel", "shack", "dwelling"})) {
        if (AnyOf(name, {"ruin", "bridge"})) return {};
        return {VillageRole::House};
    }
    if (AnyOf(name, {"tree", "pine", "oak", "birch", "palm", "willow", "spruce", "fir", "maple"})) return {VillageRole::Tree};
    if (AnyOf(name, {"bush", "shrub", "flower", "plant", "fern", "grass", "reed"})) return {VillageRole::Bush};
    if (AnyOf(name, {"rock", "boulder", "stone", "pebble"}) && kind != "decoration") return {VillageRole::Rock};
    if (kind == "nature") {
        if (AnyOf(all, {"tree", "trunk", "canopy"})) return {VillageRole::Tree};
        if (AnyOf(all, {"rock", "boulder", "stone"})) return {VillageRole::Rock};
        return {VillageRole::Bush};
    }
    if (kind == "decoration" ||
        AnyOf(name, {"barrel", "crate", "cart", "wagon", "campfire", "chest", "bench", "sack", "hay", "haystack",
                     "box", "pot", "statue", "trough", "woodpile", "anvil", "stall", "tent"})) {
        return {VillageRole::Decoration};
    }
    return {};
}

std::vector<std::string> VillageRoles::Missing() const {
    std::vector<std::string> missing;
    for (VillageRole role : kUsedRoles) {
        auto it = prefabs.find(role);
        if (it == prefabs.end() || it->second.empty()) {
            missing.push_back(role == VillageRole::FenceH ? "fence" : VillageRoleName(role));
        }
    }
    return missing;
}

VillageRoles FindVillageRoles(const PrefabLibrary& prefabs, const TerrainSet& set,
                              const std::function<std::map<std::string, std::string>(const std::string& asset)>& analysisOf,
                              float tileWidth, const std::string& ground, const std::string& road) {
    VillageRoles roles;
    // Each prefab's size as the art says ("cells" from the analysis) or, if
    // it doesn't, as imported, which may be the image's pixel size.
    struct Measured {
        float cells;
        bool fromAnalysis;
    };
    std::map<std::string, Measured> measured;
    for (const PrefabInfo& info : prefabs.Prefabs()) {
        if (!info.error.empty() || info.isPlayer || info.kind == "character" || info.kind == "item") continue;
        const auto analysis = info.texture.empty() ? std::map<std::string, std::string>{} : analysisOf(info.texture);
        if (info.isTile) {
            // A bush or a rock made into a ground tile: it would replace the
            // ground under it. Named in the review so it can be re-made as a prop.
            if (!analysis.contains("edges") && !VillageRolesOf(analysis, info.name).empty()) {
                roles.tilesNotProps.push_back(info.path);
            }
            continue;
        }
        const auto found = VillageRolesOf(analysis, info.name);
        for (VillageRole role : found) roles.prefabs[role].push_back(info.path);
        if (found.empty() || tileWidth <= 0.0f) continue;
        const float stated = static_cast<float>(std::atof(Field(analysis, "cells").c_str()));
        const float longest = IsFence(found.front()) ? std::max(info.width, info.height) : info.width;
        measured[info.path] = stated > 0.0f ? Measured{stated, true} : Measured{longest / tileWidth, false};
    }
    // Fences that only run one way stand in for the other way too (better a
    // fence than a gap; turning them is 6.6).
    if (roles.prefabs[VillageRole::FenceV].empty()) roles.prefabs[VillageRole::FenceV] = roles.prefabs[VillageRole::FenceH];
    if (roles.prefabs[VillageRole::FenceH].empty()) roles.prefabs[VillageRole::FenceH] = roles.prefabs[VillageRole::FenceV];

    // The houses' footprint: their usual size when it's believable, else
    // three cells (art imported at its pixel size says nothing).
    auto median = [](std::vector<float> values) {
        std::sort(values.begin(), values.end());
        return values.empty() ? 0.0f : values[values.size() / 2];
    };
    {
        std::vector<float> houses;
        for (const auto& path : roles.prefabs[VillageRole::House]) {
            if (measured.contains(path)) houses.push_back(measured.at(path).cells);
        }
        const float usual = median(houses);
        roles.houseCells = usual >= 1.5f && usual <= 4.5f ? std::clamp(static_cast<int>(std::lround(usual)), 2, 4) : 3;
    }
    // Every prefab's width in the village. Unanalyzed ones in a role are
    // scaled together to the role's usual size, so a small tree stays smaller
    // than a large one; then each is kept within the role's range.
    for (const auto& [role, paths] : roles.prefabs) {
        const RoleSize size = SizeOf(role, roles.houseCells);
        std::vector<float> guessed;
        for (const auto& path : paths) {
            if (measured.contains(path) && !measured.at(path).fromAnalysis) guessed.push_back(measured.at(path).cells);
        }
        const float usual = median(guessed);
        const float scale = usual > 0.0f && (usual < size.min || usual > size.max) ? size.usual / usual : 1.0f;
        for (const auto& path : paths) {
            if (!measured.contains(path) || roles.cells.contains(path)) continue;
            const Measured m = measured.at(path);
            roles.cells[path] = std::clamp(m.fromAnalysis ? m.cells : m.cells * scale, size.min, size.max);
        }
    }

    // The ground: what's asked, else the grass, else the commonest middle.
    auto has = [&](const std::string& t) { return std::find(set.terrains.begin(), set.terrains.end(), t) != set.terrains.end(); };
    auto findLike = [&](std::initializer_list<const char*> words, const std::string& not_) -> std::string {
        for (const char* w : words)
            for (const auto& t : set.terrains)
                if (t != not_ && t.find(w) != std::string::npos) return t;
        return {};
    };
    roles.road = has(road) ? road : findLike({"dirt", "road", "path", "cobble", "gravel", "stone", "sand", "mud"}, ground);
    roles.ground = has(ground) ? ground : findLike({"grass", "meadow", "field", "moss"}, roles.road);
    if (roles.ground.empty() && !roles.road.empty()) roles.ground = DefaultBackground(set, roles.road);
    if (roles.road.empty() && !roles.ground.empty()) {
        for (const auto& t : set.terrains) {
            if (t != roles.ground) {
                roles.road = t;
                break;
            }
        }
    }
    if (!roles.ground.empty()) roles.groundTile = set.PlainTile(roles.ground).value_or("");
    return roles;
}

VillageRequest VillageRequestFromArgs(const std::vector<FieldValue>& args) {
    VillageRequest r;
    r.params.x0 = ToInt(args[0]);
    r.params.y0 = ToInt(args[1]);
    r.params.houses = ToInt(args[2]);
    r.params.seed = static_cast<uint32_t>(ToInt(args[3]));
    r.params.roadWidth = ToInt(args[4]);
    r.params.houseCells = ToInt(args[5]);
    r.autoHouseCells = r.params.houseCells <= 0;
    r.params.nature = ToFloat(args[6]);
    r.params.fences = ToBool(args[7]);
    r.params.square = ToBool(args[8]);
    r.ground = ToString(args[9]);
    r.road = ToString(args[10]);
    r.movePlayer = ToBool(args[11]);
    return r;
}

VillageRequest VillageRequestFromJson(const nlohmann::json& a) {
    VillageRequest r;
    auto i = [&](const char* key, int fallback) { return a.contains(key) && a[key].is_number() ? a[key].get<int>() : fallback; };
    auto b = [&](const char* key, bool fallback) { return a.contains(key) && a[key].is_boolean() ? a[key].get<bool>() : fallback; };
    r.params.x0 = i("x0", 0);
    r.params.y0 = i("y0", 0);
    r.params.houses = i("houses", r.params.houses);
    r.params.seed = static_cast<uint32_t>(i("seed", 1));
    r.params.roadWidth = i("roadWidth", r.params.roadWidth);
    r.params.houseCells = i("houseCells", 0);
    r.autoHouseCells = r.params.houseCells <= 0;
    r.params.nature = a.contains("nature") && a["nature"].is_number() ? a["nature"].get<float>() : r.params.nature;
    r.params.fences = b("fences", true);
    r.params.square = b("square", true);
    r.ground = a.value("ground", std::string());
    r.road = a.value("road", std::string());
    r.movePlayer = b("movePlayer", true);
    return r;
}

std::optional<VillagePlan> PlanVillage(const PrefabLibrary& prefabs, const Project& project, const VillageRequest& request,
                                       std::string* error) {
    auto fail = [&](const std::string& why) -> std::optional<VillagePlan> {
        if (error) *error = why;
        return std::nullopt;
    };
    VillagePlan plan;
    // Ground tiles never analyzed get their edges from their pixels here.
    const AssetAnalysis analysis = ProjectAnalysis(prefabs, project);
    auto analysisOf = [&](const std::string& asset) {
        auto it = analysis.find(asset);
        return it == analysis.end() ? std::map<std::string, std::string>{} : it->second;
    };
    plan.set = BuildTerrainSet(prefabs, analysisOf);
    plan.roles = FindVillageRoles(prefabs, plan.set, analysisOf, BootstrapIso().tileWidth, request.ground, request.road);
    if (plan.roles.groundTile.empty() || plan.roles.road.empty()) {
        std::string known;
        for (const auto& t : plan.set.terrains) known += (known.empty() ? "" : ", ") + t;
        return fail("a village needs ground tile prefabs for a plain ground and a road on it, with the pieces "
                    "between them (have terrains: " +
                    (known.empty() ? std::string("none; import the pack's ground tiles as tile prefabs") : known) + ")");
    }
    if (plan.roles.prefabs[VillageRole::House].empty()) {
        return fail("no house prefabs: import the pack's buildings as prefabs (Assets ▸ Import as prefabs)");
    }
    plan.params = request.params;
    if (request.autoHouseCells) plan.params.houseCells = plan.roles.houseCells;
    auto layout = GenerateVillage(plan.params, error);
    if (!layout) return std::nullopt;
    plan.layout = std::move(*layout);
    return plan;
}

std::optional<std::string> BuildVillage(Registry& registry, PrefabLibrary& prefabs, const VillagePlan& plan,
                                        bool movePlayer, VillageBuildResult* result) {
    const VillageLayout& layout = plan.layout;
    VillageBuildResult built;
    auto inArea = [&](int gx, int gy) { return gx >= layout.x0 && gx <= layout.x1 && gy >= layout.y0 && gy <= layout.y1; };

    // Clear the area: tiles by cell, everything else by position.
    const auto player = FindPlayer(registry);
    std::vector<Entity> doomed;
    for (auto [entity, tile] : registry.view<Tile>().each()) {
        if (inArea(tile.gx, tile.gy)) doomed.push_back(entity);
    }
    for (auto [entity, world] : registry.view<WorldTransform2D>(entt::exclude<Tile>).each()) {
        if (inArea(static_cast<int>(std::floor(world.x)), static_cast<int>(std::floor(world.y))) &&
            (!player || entity != *player)) {
            doomed.push_back(entity);
        }
    }
    for (Entity entity : doomed) {
        if (registry.valid(entity)) {
            registry.destroy(entity);
            built.cleared++;
        }
    }

    // Ground everywhere, then the streets and the square, fitted.
    for (int y = layout.y0; y <= layout.y1; ++y)
        for (int x = layout.x0; x <= layout.x1; ++x) {
            if (prefabs.Instantiate(registry, plan.roles.groundTile, x + 0.5f, y + 0.5f, false)) built.groundTiles++;
        }
    std::vector<std::pair<int, int>> paved(layout.road.begin(), layout.road.end());
    paved.insert(paved.end(), layout.square.begin(), layout.square.end());
    PaintTerrain(registry, prefabs, plan.set, paved, plan.roles.road, plan.roles.ground);
    built.roadTiles = static_cast<int>(paved.size());

    const IsoParams iso = BootstrapIso();
    const bool topDown = iso.topDown;
    const float tileWidth = iso.tileWidth;
    // The things, back to front so the scene file reads top to bottom.
    std::vector<const VillageItem*> items;
    for (const auto& item : layout.items) items.push_back(&item);
    std::stable_sort(items.begin(), items.end(), [](const VillageItem* a, const VillageItem* b) { return a->y < b->y; });
    for (const VillageItem* item : items) {
        auto it = plan.roles.prefabs.find(item->role);
        if (it == plan.roles.prefabs.end() || it->second.empty()) {
            // Banners missing: a decoration does; anything else is left out.
            if (item->role == VillageRole::Entrance) it = plan.roles.prefabs.find(VillageRole::Decoration);
            if (it == plan.roles.prefabs.end() || it->second.empty()) continue;
        }
        const std::string& path = it->second[static_cast<size_t>(item->pick) % it->second.size()];
        // Upright art stands with its feet on its point: on a big footprint
        // that's the front cell, so the picture covers the footprint instead
        // of the plot behind it.
        float x = item->x, y = item->y;
        if (item->size > 1) {
            const float toFront = (item->size - 1) / 2.0f;
            if (!topDown) x += toFront;
            y += toFront;
        }
        auto placed = prefabs.Instantiate(registry, path, x, y);
        if (!placed) continue;
        built.props++;
        if (auto want = plan.roles.cells.find(path); want != plan.roles.cells.end()) {
            if (const auto* sized = registry.try_get<Sprite>(*placed); sized != nullptr && sized->width > 0.0f) {
                const float current = IsFence(it->first) ? std::max(sized->width, sized->height) : sized->width;
                const float factor = want->second * tileWidth / current;
                if (std::abs(factor - 1.0f) > 0.02f) ScaleEntity(EngineReflection(), registry, *placed, factor);
            }
        }
        const auto* sprite = registry.try_get<Sprite>(*placed);
        if (item->mirror && sprite && !sprite->flat) {
            MirrorEntity(EngineReflection(), registry, *placed);
        }
    }

    // The player arrives on the main street, by the middle.
    if (movePlayer && player && registry.valid(*player) && !layout.road.empty()) {
        const float cx = (layout.x0 + layout.x1) / 2.0f, cy = (layout.y0 + layout.y1) / 2.0f;
        auto best = *layout.road.begin();
        float bestDistance = 1e9f;
        for (const auto& cell : layout.road) {
            const float d = std::abs(cell.first + 0.5f - cx) + std::abs(cell.second + 0.5f - cy);
            if (d < bestDistance) {
                bestDistance = d;
                best = cell;
            }
        }
        registry.get<WorldTransform2D>(*player) = {best.first + 0.5f, best.second + 0.5f};
    }
    if (result) *result = built;
    return std::nullopt;
}

std::string DescribeVillage(const VillagePlan& plan) {
    const VillageLayout& layout = plan.layout;
    std::map<VillageRole, int> counts;
    for (const auto& item : layout.items) counts[item.role]++;
    auto count = [&](VillageRole role) { return counts.contains(role) ? counts.at(role) : 0; };
    auto names = [&](VillageRole role) {
        std::string out;
        auto it = plan.roles.prefabs.find(role);
        if (it == plan.roles.prefabs.end()) return out;
        for (size_t i = 0; i < it->second.size() && i < 4; ++i) {
            const std::string& path = it->second[i];
            const size_t slash = path.find_last_of('/');
            std::string stem = path.substr(slash == std::string::npos ? 0 : slash + 1);
            if (stem.size() > 7 && stem.ends_with(".prefab")) stem.resize(stem.size() - 7);
            out += (out.empty() ? "" : ", ") + stem;
        }
        if (it->second.size() > 4) out += ", +" + std::to_string(it->second.size() - 4);
        return out;
    };

    std::ostringstream out;
    out << layout.houses << " houses (" << plan.params.houseCells << "x" << plan.params.houseCells << " cells) in ("
        << layout.x0 << "," << layout.y0 << ")-(" << layout.x1 << "," << layout.y1 << "), "
        << layout.road.size() + layout.square.size() << " cells of " << plan.roles.road << " road on "
        << plan.roles.ground << "; " << count(VillageRole::FenceH) + count(VillageRole::FenceV) << " fence pieces, "
        << count(VillageRole::Tree) << " trees, " << count(VillageRole::Bush) << " bushes, " << count(VillageRole::Rock)
        << " rocks, " << count(VillageRole::Decoration) << " decorations.\n";
    for (VillageRole role : kUsedRoles) {
        const std::string used = names(role);
        if (!used.empty()) out << "  " << VillageRoleName(role) << ": " << used << "\n";
    }
    if (!plan.roles.tilesNotProps.empty()) {
        out << "  Not used, made as ground tiles instead of props: ";
        for (size_t i = 0; i < plan.roles.tilesNotProps.size(); ++i) {
            const std::string& path = plan.roles.tilesNotProps[i];
            const size_t slash = path.find_last_of('/');
            std::string stem = path.substr(slash == std::string::npos ? 0 : slash + 1);
            if (stem.ends_with(".prefab")) stem.resize(stem.size() - 7);
            out << (i ? ", " : "") << stem;
        }
        out << ". Re-import their images as props (Assets ▸ Import as prefabs) to use them.\n";
    }
    const auto missing = plan.roles.Missing();
    if (!missing.empty()) {
        out << "  Nothing in the pack for: ";
        for (size_t i = 0; i < missing.size(); ++i) out << (i ? ", " : "") << missing[i];
        out << " (left out).\n";
    }
    out << "Map (north up): = road  # square  H house  f fence  w well  T tree  b bush  r rock  d decoration  "
           "! entrance  L landmark\n"
        << layout.Ascii();
    return out.str();
}

} // namespace aige
