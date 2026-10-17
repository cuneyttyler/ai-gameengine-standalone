#include "editing/VillageBuild.h"

#include "assets/AssetDatabase.h"
#include "assets/Prefab.h"
#include "editing/Edits.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/Components.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/GameSession.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"

#include <algorithm>
#include <cctype>
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
            // "trees" counts as "tree"
            if (word.size() > 3 && word.back() == 's') words.insert(word.substr(0, word.size() - 1));
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

const std::vector<VillageRole> kUsedRoles = {VillageRole::House, VillageRole::Well,       VillageRole::FenceH,
                                             VillageRole::Tree,  VillageRole::Bush,       VillageRole::Rock,
                                             VillageRole::Decoration, VillageRole::Entrance, VillageRole::Landmark};

} // namespace

std::vector<VillageRole> VillageRolesOf(const std::map<std::string, std::string>& analysis, const std::string& prefabName) {
    const std::string kind = Field(analysis, "kind");
    if (kind == "ground" || kind == "character" || kind == "item" || kind == "effect") return {};
    // What it is, most telling first: the analysis' name and tags, then its
    // description; without any analysis, the prefab's own name.
    const std::set<std::string> name = Words(Field(analysis, "name") + " " + Field(analysis, "tags") +
                                             (analysis.empty() ? " " + prefabName : std::string()));
    const std::set<std::string> all = [&] {
        auto words = name;
        words.merge(Words(Field(analysis, "description")));
        return words;
    }();

    // Fences and wall pieces both close the gardens.
    if (AnyOf(name, {"fence", "fencing", "palisade", "railing", "hedge", "wall"})) {
        const std::string direction = Field(analysis, "direction");
        if (direction == "horizontal") return {VillageRole::FenceH};
        if (direction == "vertical") return {VillageRole::FenceV};
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
                     "box", "pot", "statue", "trough", "woodpile", "anvil", "stall"})) {
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
    std::vector<float> houseWidths;
    for (const PrefabInfo& info : prefabs.Prefabs()) {
        if (!info.error.empty() || info.isTile || info.isPlayer || info.kind == "character" || info.kind == "item") continue;
        const auto analysis = info.texture.empty() ? std::map<std::string, std::string>{} : analysisOf(info.texture);
        for (VillageRole role : VillageRolesOf(analysis, info.name)) {
            roles.prefabs[role].push_back(info.path);
            if (role == VillageRole::House && info.width > 0.0f && tileWidth > 0.0f) {
                houseWidths.push_back(info.width / tileWidth);
            }
        }
    }
    // Fences that only run one way stand in for the other way too (better a
    // fence than a gap; turning them is 6.6).
    if (roles.prefabs[VillageRole::FenceV].empty()) roles.prefabs[VillageRole::FenceV] = roles.prefabs[VillageRole::FenceH];
    if (roles.prefabs[VillageRole::FenceH].empty()) roles.prefabs[VillageRole::FenceH] = roles.prefabs[VillageRole::FenceV];

    if (!houseWidths.empty()) {
        std::sort(houseWidths.begin(), houseWidths.end());
        const float median = houseWidths[houseWidths.size() / 2];
        roles.houseCells = std::clamp(static_cast<int>(std::lround(median)), 1, 6);
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
    auto analysisOf = [&](const std::string& asset) { return ReadAssetAnalysis(project, asset); };
    plan.set = BuildTerrainSet(prefabs, analysisOf);
    plan.roles = FindVillageRoles(prefabs, plan.set, analysisOf, BootstrapIso().tileWidth, request.ground, request.road);
    if (plan.roles.groundTile.empty() || plan.roles.road.empty()) {
        std::string known;
        for (const auto& t : plan.set.terrains) known += (known.empty() ? "" : ", ") + t;
        return fail("a village needs analyzed ground tiles: a plain ground tile and a road terrain (have: " +
                    (known.empty() ? std::string("none; analyze the pack's tiles and import them") : known) + ")");
    }
    if (plan.roles.prefabs[VillageRole::House].empty()) {
        return fail("no house prefabs: import the pack's buildings (Assets ▸ Import as prefabs) after analyzing them");
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
        auto placed = prefabs.Instantiate(registry, path, item->x, item->y);
        if (!placed) continue;
        built.props++;
        const auto* sprite = registry.try_get<Sprite>(*placed);
        if (item->mirror && sprite && !sprite->flat) {
            const float u0 = sprite->u0, u1 = sprite->u1;
            EditField(EngineReflection(), registry, *placed, "Sprite", "u0", u1);
            EditField(EngineReflection(), registry, *placed, "Sprite", "u1", u0);
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
