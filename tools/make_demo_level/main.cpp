// Builds the Phase 1 vertical slice level (development_plan.md milestone 1.6)
// into a project's scenes/main.aige, from the ASCII layout below.
//
//   make_demo_level [project-dir]     (default: projects/demo)
//
// It writes the project's prefabs (prefabs/*.prefab) and places instances of
// them exactly like the editor's prefab brush, so the result is an ordinary
// scene: open it in the editor to tweak it by hand.
// Re-running this overwrites the scene — it exists so the level is
// reproducible and reviewable as text, not as the only way to change it.
//
// Expects the demo art in <project>/assets (tools/demo-art/generate.mjs).
#include "assets/AssetDatabase.h"
#include "assets/Prefab.h"
#include "assets/Project.h"
#include "assets/SceneIO.h"
#include "ecs/Registry.h"
#include "gameplay/Archetypes.h"
#include "gameplay/Components.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"

#include <array>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#ifndef AIGE_DEFAULT_PROJECT
#define AIGE_DEFAULT_PROJECT "."
#endif

namespace {

// Ground layer, one character per cell: column = gx, row = gy. (Grid x runs
// down-right on screen and grid y down-left, so this map appears rotated 45°
// in game.)
//   . grass   , flowers   = dirt path   _ stone floor
//   # wall    M mossy wall  ~ water     h hedge
//   T tree on grass   R rock on grass
// A walled courtyard (top right) holds two skeletons and a potion; the open
// field below it has two more; the player starts on the path, bottom left.
constexpr std::array<const char*, 14> kGround = {
    "hhhhhhhhhhhhhhhhhhhh",
    "h...,..T...h#######h",
    "h.T........h#_____Mh",
    "h....,.....h#_____#h",
    "h..====....h#_____#h",
    "h..=..=....h#__#__#h",
    "h..=..======______#h",
    "h..=..,....h#M#####h",
    "h..=.......h.......h",
    "h~~=...R.....,..T..h",
    "h~~=....,.......R..h",
    "h..==========......h",
    "h.T......,.....T...h",
    "hhhhhhhhhhhhhhhhhhhh",
};

struct Placement {
    char kind; // P player, S skeleton, + potion, V villager, C crate
    int gx;
    int gy;
};

constexpr std::array<Placement, 11> kEntities = {{
    {'P', 4, 11},
    {'V', 5, 2},
    {'+', 8, 1},
    {'S', 15, 9},
    {'S', 17, 11},
    {'+', 13, 10},
    {'S', 14, 3},
    {'S', 17, 5},
    {'+', 17, 2},
    {'C', 13, 2},
    {'C', 15, 8},
}};

const char* kTileset = "assets/textures/ground.png";
constexpr int kTilesetColumns = 4;
constexpr int kTilesetRows = 2;

aige::TileStyle TilesetTile(int index, bool solid) {
    aige::TileStyle style;
    style.texture = kTileset;
    int col = index % kTilesetColumns;
    int row = index / kTilesetColumns;
    style.u0 = static_cast<float>(col) / kTilesetColumns;
    style.v0 = static_cast<float>(row) / kTilesetRows;
    style.u1 = static_cast<float>(col + 1) / kTilesetColumns;
    style.v1 = static_cast<float>(row + 1) / kTilesetRows;
    style.width = 64.0f;
    style.height = 64.0f;
    style.solid = solid;
    return style;
}

void UseTexture(aige::Registry& registry, aige::Entity entity, const std::string& texture, float width, float height) {
    auto& sprite = registry.get<aige::Sprite>(entity);
    sprite = aige::Sprite{width, height, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, texture};
}


// Writes one prefab built by `make` in a scratch registry.
void MakePrefab(aige::PrefabLibrary& prefabs, const std::string& name,
                const std::function<aige::Entity(aige::Registry&)>& make) {
    aige::Registry scratch;
    aige::Entity entity = make(scratch);
    std::string error;
    if (!prefabs.SaveAsPrefab(scratch, entity, "prefabs/" + name + ".prefab", &error)) {
        std::cerr << "Couldn't write prefab " << name << ": " << error << std::endl;
    }
}

aige::Entity MakeProp(aige::Registry& registry, const char* name, const std::string& texture, float width,
                      float height) {
    auto entity = aige::CreateProp(registry, 0.0f, 0.0f, texture, width, height);
    registry.emplace<aige::Name>(entity, name);
    registry.emplace<aige::Obstacle>(entity);
    return entity;
}

// The project's palette: every placeable thing is a prefab file, so the
// editor's brush, this tool and scripts' spawn() all use the same data.
void WritePrefabs(aige::PrefabLibrary& prefabs) {
    const std::array<std::pair<const char*, bool>, 8> tiles = {{
        {"grass", false},
        {"flowers", false},
        {"dirt", false},
        {"stone_floor", false},
        {"wall", true},
        {"mossy_wall", true},
        {"water", true},
        {"hedge", true},
    }};
    for (int i = 0; i < static_cast<int>(tiles.size()); ++i) {
        MakePrefab(prefabs, tiles[i].first,
                   [&](aige::Registry& r) { return aige::CreateTile(r, 0, 0, TilesetTile(i, tiles[i].second)); });
    }

    MakePrefab(prefabs, "hero", [](aige::Registry& r) {
        auto e = aige::CreatePlayer(r, 0.0f, 0.0f);
        r.get<aige::Name>(e).value = "Hero";
        UseTexture(r, e, "assets/textures/hero.png", 32.0f, 48.0f);
        r.emplace<aige::Sounds>(e, "assets/audio/swing.wav", "assets/audio/hurt.wav", "assets/audio/death.wav", "");
        return e;
    });
    MakePrefab(prefabs, "skeleton", [](aige::Registry& r) {
        auto e = aige::CreateEnemy(r, 0.0f, 0.0f, entt::null); // no target = chases the player
        r.get<aige::Name>(e).value = "Skeleton";
        UseTexture(r, e, "assets/textures/skeleton.png", 32.0f, 48.0f);
        r.emplace<aige::Sounds>(e, "assets/audio/swing.wav", "assets/audio/hit.wav", "assets/audio/death.wav", "");
        return e;
    });
    MakePrefab(prefabs, "potion", [](aige::Registry& r) {
        auto e = aige::CreateHealthPickup(r, 0.0f, 0.0f, 8.0f);
        UseTexture(r, e, "assets/textures/potion.png", 16.0f, 20.0f);
        r.emplace<aige::Sounds>(e, "", "", "", "assets/audio/pickup.wav");
        return e;
    });
    MakePrefab(prefabs, "villager", [](aige::Registry& r) {
        auto e = aige::CreateNpc(r, 0.0f, 0.0f);
        r.get<aige::Name>(e).value = "Villager";
        UseTexture(r, e, "assets/textures/villager.png", 32.0f, 48.0f);
        return e;
    });
    MakePrefab(prefabs, "tree", [](aige::Registry& r) { return MakeProp(r, "Tree", "assets/textures/tree.png", 64.0f, 96.0f); });
    MakePrefab(prefabs, "rock", [](aige::Registry& r) { return MakeProp(r, "Rock", "assets/textures/rock.png", 32.0f, 24.0f); });
    MakePrefab(prefabs, "crate", [](aige::Registry& r) { return MakeProp(r, "Crate", "assets/textures/crate.png", 32.0f, 32.0f); });
}

} // namespace

int main(int argc, char** argv) {
    aige::Project project{std::filesystem::absolute(argc > 1 ? argv[1] : AIGE_DEFAULT_PROJECT)};

    aige::AssetDatabase assets(project);
    assets.Refresh();
    if (assets.Find(kTileset) == nullptr) {
        std::cerr << "Missing " << kTileset << " — run: node tools/demo-art/generate.mjs "
                  << project.AssetsDir().string() << std::endl;
        return 1;
    }
    assets.SetImageSettings(kTileset, aige::AssetType::Tileset, 64, 64);

    aige::PrefabLibrary prefabs(project);
    WritePrefabs(prefabs);
    prefabs.Refresh();

    aige::Registry registry;
    auto place = [&](const char* prefab, int gx, int gy) {
        auto entity = prefabs.Instantiate(registry, std::string("prefabs/") + prefab + ".prefab", gx + 0.5f, gy + 0.5f);
        if (!entity) {
            std::cerr << "Couldn't place " << prefab << std::endl;
        }
        return entity;
    };

    // Ground, then the scenery standing on it.
    for (int gy = 0; gy < static_cast<int>(kGround.size()); ++gy) {
        const std::string row = kGround[gy];
        for (int gx = 0; gx < static_cast<int>(row.size()); ++gx) {
            switch (row[gx]) {
                case '.': case 'T': case 'R': place("grass", gx, gy); break;
                case ',': place("flowers", gx, gy); break;
                case '=': place("dirt", gx, gy); break;
                case '_': place("stone_floor", gx, gy); break;
                case '#': place("wall", gx, gy); break;
                case 'M': place("mossy_wall", gx, gy); break;
                case '~': place("water", gx, gy); break;
                case 'h': place("hedge", gx, gy); break;
                default: break;
            }
        }
    }
    for (int gy = 0; gy < static_cast<int>(kGround.size()); ++gy) {
        const std::string row = kGround[gy];
        for (int gx = 0; gx < static_cast<int>(row.size()); ++gx) {
            if (row[gx] == 'T') {
                place("tree", gx, gy);
            } else if (row[gx] == 'R') {
                place("rock", gx, gy);
            }
        }
    }

    int skeletons = 0;
    for (const auto& placement : kEntities) {
        switch (placement.kind) {
            case 'P': place("hero", placement.gx, placement.gy); break;
            case 'S': place("skeleton", placement.gx, placement.gy); ++skeletons; break;
            case '+': place("potion", placement.gx, placement.gy); break;
            case 'V': place("villager", placement.gx, placement.gy); break;
            case 'C': place("crate", placement.gx, placement.gy); break;
            default: break;
        }
    }

    std::filesystem::create_directories(project.MainScenePath().parent_path());
    aige::SaveScene(registry, project.MainScenePath().string());
    std::cout << "Wrote " << prefabs.Prefabs().size() << " prefabs and " << project.MainScenePath().string() << " ("
              << registry.view<aige::Tile>().size() << " tiles, " << skeletons << " skeletons)" << std::endl;
    return 0;
}
