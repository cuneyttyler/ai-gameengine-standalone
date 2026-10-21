#include "editing/Autotile.h"

#include "assets/AssetDatabase.h"
#include "assets/TextureCook.h"
#include "assets/TileEdges.h"
#include "editing/Edits.h"
#include "gameplay/Components.h"
#include "gameplay/EngineReflection.h"
#include "renderer2d/Components.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>

namespace aige {

std::optional<std::string> TerrainSet::PlainTile(const std::string& terrain) const {
    for (const TerrainTile& tile : tiles) {
        if (tile.center == terrain &&
            std::all_of(tile.edges.begin(), tile.edges.end(), [&](const std::string& t) { return t == terrain; })) {
            return tile.prefab;
        }
    }
    return std::nullopt;
}

std::optional<std::string> TerrainSet::TerrainOf(const std::string& prefab) const {
    for (const TerrainTile& tile : tiles) {
        if (tile.prefab == prefab) return tile.center;
    }
    return std::nullopt;
}

TerrainSet BuildTerrainSet(const PrefabLibrary& prefabs,
                           const std::function<std::map<std::string, std::string>(const std::string&)>& analysisOf) {
    TerrainSet set;
    std::set<std::string> terrains;
    for (const PrefabInfo& info : prefabs.Prefabs()) {
        if (!info.isTile || !info.error.empty() || info.texture.empty()) continue;
        const auto fields = analysisOf(info.texture);
        const auto edges = fields.find("edges");
        const auto center = fields.find("center");
        if (edges == fields.end() || center == fields.end()) continue;
        TerrainTile tile;
        tile.prefab = info.path;
        tile.center = center->second;
        if (!ParseSignature(edges->second, tile.edges)) continue;
        terrains.insert(tile.center);
        for (const auto& t : tile.edges) terrains.insert(t);
        set.tiles.push_back(tile);
    }
    set.terrains.assign(terrains.begin(), terrains.end());
    return set;
}

EdgeSignature TargetSignature(const std::function<std::string(int, int)>& terrainAt, int gx, int gy,
                              const std::string& background) {
    const std::string self = terrainAt(gx, gy);
    if (self == background) {
        EdgeSignature plain;
        plain.fill(background);
        return plain;
    }
    // Clockwise from the top-left: NW, N, NE, E, SE, S, SW, W.
    const int dx[8] = {-1, 0, 1, 1, 1, 0, -1, -1};
    const int dy[8] = {-1, -1, -1, 0, 1, 1, 1, 0};
    EdgeSignature target;
    for (int p = 0; p < kBorderPoints; ++p) {
        if (p % 2 == 1) {
            // An edge: shared with one neighbour.
            const std::string other = terrainAt(gx + dx[p], gy + dy[p]);
            target[p] = other == self ? self : other;
        } else {
            // A corner: shared by four cells; ours only if all four are.
            const std::string a = terrainAt(gx + dx[p], gy);
            const std::string b = terrainAt(gx, gy + dy[p]);
            const std::string c = terrainAt(gx + dx[p], gy + dy[p]);
            if (a == self && b == self && c == self) {
                target[p] = self;
            } else {
                // The other terrain there (the edge neighbours first).
                target[p] = a != self ? a : b != self ? b : c;
            }
        }
    }
    return target;
}

EdgeSignature Rotated(const EdgeSignature& signature, int turns) {
    turns = ((turns % 4) + 4) % 4;
    EdgeSignature out;
    for (int p = 0; p < kBorderPoints; ++p) out[(p + 2 * turns) % kBorderPoints] = signature[p];
    return out;
}

std::optional<TileChoice> ChooseTile(const TerrainSet& set, const EdgeSignature& target, const std::string& center) {
    std::optional<TileChoice> best;
    float bestScore = 1e9f;
    for (const TerrainTile& tile : set.tiles) {
        if (tile.center != center) continue;
        for (int turns = 0; turns < 4; ++turns) {
            const EdgeSignature edges = Rotated(tile.edges, turns);
            int mismatches = 0;
            for (int p = 0; p < kBorderPoints; ++p) {
                // An edge midpoint counts double: a wrong edge shows a seam.
                if (edges[p] != target[p]) mismatches += p % 2 == 1 ? 2 : 1;
            }
            const float score = static_cast<float>(mismatches) + (turns > 0 ? 0.25f : 0.0f);
            if (score < bestScore) {
                bestScore = score;
                best = TileChoice{tile.prefab, turns, mismatches};
            }
        }
    }
    return best;
}

int PaintTerrain(Registry& registry, PrefabLibrary& prefabs, const TerrainSet& set,
                 const std::vector<std::pair<int, int>>& cells, const std::string& terrain,
                 const std::string& background) {
    // The cells' current tiles, once.
    std::map<std::pair<int, int>, Entity> tileAt;
    for (auto [entity, tile] : registry.view<Tile>().each()) tileAt[{tile.gx, tile.gy}] = entity;
    std::map<std::pair<int, int>, std::string> painted;
    for (const auto& cell : cells) painted[cell] = terrain;

    auto terrainAt = [&](int gx, int gy) -> std::string {
        if (auto it = painted.find({gx, gy}); it != painted.end()) return it->second;
        if (auto it = tileAt.find({gx, gy}); it != tileAt.end()) {
            if (const auto* link = registry.try_get<PrefabInstance>(it->second)) {
                if (auto t = set.TerrainOf(link->path)) return *t;
            }
        }
        return background;
    };

    // Re-fit the painted cells and every neighbour that's part of the set (or empty).
    std::set<std::pair<int, int>> refit;
    for (const auto& [cell, _] : painted) {
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x) refit.insert({cell.first + x, cell.second + y});
    }
    int changed = 0;
    for (const auto& cell : refit) {
        const bool isPainted = painted.contains(cell);
        auto existing = tileAt.find(cell);
        if (!isPainted) {
            // A neighbour: only one of ours is re-fitted; others (walls, empty space) stay.
            if (existing == tileAt.end()) continue;
            const auto* link = registry.try_get<PrefabInstance>(existing->second);
            if (link == nullptr || !set.TerrainOf(link->path)) continue;
        }
        const std::string center = terrainAt(cell.first, cell.second);
        const EdgeSignature target = TargetSignature(terrainAt, cell.first, cell.second, background);
        auto choice = ChooseTile(set, target, center);
        if (!choice) continue;
        // Already right? Leave it (no churn in undo or the file).
        if (existing != tileAt.end()) {
            const auto* link = registry.try_get<PrefabInstance>(existing->second);
            const auto* sprite = registry.try_get<Sprite>(existing->second);
            if (link && sprite && link->path == choice->prefab && sprite->rotation == choice->rotation) continue;
            registry.destroy(existing->second);
            tileAt.erase(existing);
        }
        auto placed = prefabs.Instantiate(registry, choice->prefab, cell.first + 0.5f, cell.second + 0.5f, false);
        if (!placed) continue;
        tileAt[cell] = *placed;
        if (choice->rotation != 0) {
            EditField(EngineReflection(), registry, *placed, "Sprite", "rotation", choice->rotation);
        }
        ++changed;
    }
    return changed;
}

AssetAnalysis ProjectAnalysis(const PrefabLibrary& prefabs, const Project& project) {
    AssetAnalysis analysis;
    std::map<std::string, std::vector<std::pair<std::string, BorderSamples>>> byFolder;
    for (const PrefabInfo& info : prefabs.Prefabs()) {
        if (info.texture.empty() || analysis.contains(info.texture)) continue;
        auto fields = ReadAssetAnalysis(project, info.texture);
        const bool needsEdges = info.isTile && (!fields.contains("edges") || !fields.contains("center"));
        analysis[info.texture] = std::move(fields);
        if (!needsEdges) continue;
        // A ground tile: square and opaque at the border (else it's a prop).
        auto image = DecodeImage(project.Resolve(info.texture));
        if (!image || image->width == 0 || image->height == 0) continue;
        const float aspect = static_cast<float>(image->width) / static_cast<float>(image->height);
        if (aspect < 0.95f || aspect > 1.05f) continue;
        BorderSamples samples = SampleBorder(*image);
        if (!samples.opaque) continue;
        byFolder[std::filesystem::path(info.texture).parent_path().generic_string()].push_back({info.texture, samples});
    }
    for (const auto& [folder, tiles] : byFolder) {
        std::vector<BorderSamples> samples;
        for (const auto& tile : tiles) samples.push_back(tile.second);
        const EdgeClassification result = ClassifyEdges(samples);
        for (size_t i = 0; i < tiles.size(); ++i) {
            auto& fields = analysis[tiles[i].first];
            fields["edges"] = FormatSignature(result.signatures[i]);
            fields["center"] = result.centers[i];
            if (!fields.contains("kind")) fields["kind"] = "ground";
        }
    }
    return analysis;
}

TerrainSet ProjectTerrainSet(const PrefabLibrary& prefabs, const Project& project) {
    const AssetAnalysis analysis = ProjectAnalysis(prefabs, project);
    return BuildTerrainSet(prefabs, [&](const std::string& asset) {
        // A placeholder an earlier build left isn't the project's ground (it hid the real art).
        if (asset.find("generated/placeholders/") != std::string::npos) return std::map<std::string, std::string>{};
        auto it = analysis.find(asset);
        if (it == analysis.end()) return std::map<std::string, std::string>{};
        // A ground tile the Art tool made for one terrain (and made seamless): that terrain
        // all over, whatever reading its pixels said (a grey cobble tile was taken for grass).
        std::map<std::string, std::string> fields = it->second;
        const auto terrain = fields.find("terrain"), source = fields.find("source");
        if (terrain != fields.end() && !terrain->second.empty() && source != fields.end() && source->second == "generated") {
            fields["center"] = terrain->second;
            std::string edges;
            for (int i = 0; i < 8; ++i) edges += (i ? "," : "") + terrain->second;
            fields["edges"] = edges;
        }
        return fields;
    });
}

std::string DefaultBackground(const TerrainSet& set, const std::string& terrain) {
    std::map<std::string, int> counts;
    for (const TerrainTile& tile : set.tiles) {
        if (tile.center != terrain) ++counts[tile.center];
    }
    std::string best;
    int most = -1;
    for (const auto& [name, count] : counts) {
        if (count > most) {
            most = count;
            best = name;
        }
    }
    if (best.empty()) {
        for (const auto& t : set.terrains) {
            if (t != terrain) return t;
        }
    }
    return best;
}

std::vector<std::pair<int, int>> ParseCells(const std::string& text) {
    std::vector<std::pair<int, int>> cells;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(';', start);
        if (end == std::string::npos) end = text.size();
        const std::string part = text.substr(start, end - start);
        const size_t comma = part.find(',');
        if (comma == std::string::npos) return {};
        char* rest = nullptr;
        const long x = std::strtol(part.c_str(), &rest, 10);
        const long y = std::strtol(part.c_str() + comma + 1, &rest, 10);
        cells.push_back({static_cast<int>(x), static_cast<int>(y)});
        start = end + 1;
    }
    return cells;
}

std::vector<std::pair<int, int>> PathCells(const std::vector<std::pair<int, int>>& points, int width) {
    std::set<std::pair<int, int>> cells;
    width = std::clamp(width, 1, 9);
    const int before = (width - 1) / 2, after = width / 2;
    // Widened sideways only, so a road ends where it was told to; bends get
    // the full square so the corner has no notch.
    auto across = [&](int x, int y, bool alongX) {
        for (int o = -before; o <= after; ++o) cells.insert(alongX ? std::pair{x, y + o} : std::pair{x + o, y});
    };
    auto square = [&](int x, int y) {
        for (int oy = -before; oy <= after; ++oy)
            for (int ox = -before; ox <= after; ++ox) cells.insert({x + ox, y + oy});
    };
    if (points.size() == 1) square(points[0].first, points[0].second);
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        auto [x, y] = points[i];
        const auto [tx, ty] = points[i + 1];
        // Along x, then along y: roads follow the grid.
        across(x, y, x != tx);
        while (x != tx) {
            x += x < tx ? 1 : -1;
            across(x, y, true);
        }
        if (y != ty && x != points[i].first) square(x, y); // the bend
        while (y != ty) {
            y += y < ty ? 1 : -1;
            across(x, y, false);
        }
        if (i + 2 < points.size()) square(x, y); // a bend between segments
    }
    return {cells.begin(), cells.end()};
}

} // namespace aige
