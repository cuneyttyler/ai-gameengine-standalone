// Builds the Phase 1 vertical slice level (development_plan.md milestone 1.6)
// into a project's scenes/main.aige, from the ASCII layout below.
//
//   make_demo_level [project-dir]     (default: projects/demo)
//
// It uses the same archetypes and editing API as the editor's tools, so the
// result is an ordinary scene: open it in the editor to tweak it by hand.
// Re-running this overwrites the scene — it exists so the level is
// reproducible and reviewable as text, not as the only way to change it.
//
// Expects the demo art in <project>/assets (tools/demo-art/generate.mjs).
#include "assets/AssetDatabase.h"
#include "assets/Project.h"
#include "assets/SceneIO.h"
#include "ecs/Registry.h"
#include "gameplay/Archetypes.h"
#include "gameplay/Components.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"

#include <array>
#include <filesystem>
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

aige::Entity Prop(aige::Registry& registry, int gx, int gy, const char* name, const std::string& texture, float width,
                  float height) {
    auto entity = aige::CreateProp(registry, gx + 0.5f, gy + 0.5f, texture, width, height);
    registry.emplace<aige::Name>(entity, name);
    registry.emplace<aige::Obstacle>(entity);
    return entity;
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

    aige::Registry registry;

    // Ground.
    for (int gy = 0; gy < static_cast<int>(kGround.size()); ++gy) {
        const std::string row = kGround[gy];
        for (int gx = 0; gx < static_cast<int>(row.size()); ++gx) {
            switch (row[gx]) {
                case '.': case 'T': case 'R': aige::CreateTile(registry, gx, gy, TilesetTile(0, false)); break;
                case ',': aige::CreateTile(registry, gx, gy, TilesetTile(1, false)); break;
                case '=': aige::CreateTile(registry, gx, gy, TilesetTile(2, false)); break;
                case '_': aige::CreateTile(registry, gx, gy, TilesetTile(3, false)); break;
                case '#': aige::CreateTile(registry, gx, gy, TilesetTile(4, true)); break;
                case 'M': aige::CreateTile(registry, gx, gy, TilesetTile(5, true)); break;
                case '~': aige::CreateTile(registry, gx, gy, TilesetTile(6, true)); break;
                case 'h': aige::CreateTile(registry, gx, gy, TilesetTile(7, true)); break;
                default: break;
            }
        }
    }

    // Scenery standing on the grass.
    for (int gy = 0; gy < static_cast<int>(kGround.size()); ++gy) {
        const std::string row = kGround[gy];
        for (int gx = 0; gx < static_cast<int>(row.size()); ++gx) {
            if (row[gx] == 'T') {
                Prop(registry, gx, gy, "Tree", "assets/textures/tree.png", 64.0f, 96.0f);
            } else if (row[gx] == 'R') {
                Prop(registry, gx, gy, "Rock", "assets/textures/rock.png", 32.0f, 24.0f);
            }
        }
    }

    // The player first, so enemies can target it.
    aige::Entity player = entt::null;
    for (const auto& placement : kEntities) {
        if (placement.kind == 'P') {
            player = aige::CreatePlayer(registry, placement.gx + 0.5f, placement.gy + 0.5f);
            UseTexture(registry, player, "assets/textures/hero.png", 32.0f, 48.0f);
            registry.emplace<aige::Sounds>(player, "assets/audio/swing.wav", "assets/audio/hurt.wav",
                                           "assets/audio/death.wav", "");
        }
    }

    int skeletons = 0;
    for (const auto& placement : kEntities) {
        float x = placement.gx + 0.5f;
        float y = placement.gy + 0.5f;
        switch (placement.kind) {
            case 'S': {
                auto skeleton = aige::CreateEnemy(registry, x, y, player);
                registry.get<aige::Name>(skeleton).value = "Skeleton " + std::to_string(++skeletons);
                UseTexture(registry, skeleton, "assets/textures/skeleton.png", 32.0f, 48.0f);
                registry.emplace<aige::Sounds>(skeleton, "assets/audio/swing.wav", "assets/audio/hit.wav",
                                               "assets/audio/death.wav", "");
                break;
            }
            case '+': {
                auto potion = aige::CreateHealthPickup(registry, x, y, 8.0f);
                UseTexture(registry, potion, "assets/textures/potion.png", 16.0f, 20.0f);
                registry.emplace<aige::Sounds>(potion, "", "", "", "assets/audio/pickup.wav");
                break;
            }
            case 'V': {
                auto villager = aige::CreateNpc(registry, x, y);
                registry.get<aige::Name>(villager).value = "Villager";
                UseTexture(registry, villager, "assets/textures/villager.png", 32.0f, 48.0f);
                break;
            }
            case 'C':
                Prop(registry, placement.gx, placement.gy, "Crate", "assets/textures/crate.png", 32.0f, 32.0f);
                break;
            default:
                break;
        }
    }

    std::filesystem::create_directories(project.MainScenePath().parent_path());
    aige::SaveScene(registry, project.MainScenePath().string());
    std::cout << "Wrote " << project.MainScenePath().string() << " (" << registry.view<aige::Tile>().size()
              << " tiles, " << skeletons << " skeletons)" << std::endl;
    return 0;
}
