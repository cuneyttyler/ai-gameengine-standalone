#pragma once

#include "ecs/Registry.h"
#include "renderer2d/Components.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>
#include <string>
#include <utility>
#include <vector>

namespace aige {

// Chunked tile layers (development_plan.md 7.2): ground for big scenes.
//
// Hand-painted tiles are entities (Tile + Sprite), one per cell, which suits
// editing a few hundred of them. A built place has hundreds of thousands, so
// builds write layers instead: a palette of tile kinds and, per 32x32 chunk,
// one 16-bit palette index per cell. A 400x400 place is ~170 chunks, ~0.5 MB.
//
// Layers live in the registry's context (TileLayers), not on entities, so
// they aren't part of the scene's text form: whole-scene undo snapshots
// skip them, and only layer commands (TileLayerEdit) change them. They're
// saved next to the scene in region files (assets/TileLayerIO.h); a region
// is 4x4 chunks (128x128 cells), and only changed regions are rewritten.

// One kind of tile: how it looks, whether it blocks, and the prefab it came
// from. Each rotation of the same art is its own palette entry.
struct TileDef {
    Sprite sprite;
    bool solid = false;
    std::string prefab; // project-relative, or ""

    bool operator==(const TileDef& other) const;
};

class TileLayer {
public:
    static constexpr int kChunkSize = 32;
    static constexpr int kRegionChunks = 4;
    static constexpr int kRegionSize = kChunkSize * kRegionChunks;
    static constexpr uint16_t kEmpty = 0;

    struct Chunk {
        std::array<uint16_t, kChunkSize * kChunkSize> cells{}; // palette index + 1; 0 = empty
        int count = 0;                                          // non-empty cells
    };

    std::string name;
    int order = 0; // draw order among layers, and which wins a cell (higher on top)

    // The palette index (+1) for a tile kind, adding it if new. Indices never
    // change once given, so recorded edits stay valid.
    uint16_t Intern(const TileDef& def);
    const TileDef* Def(uint16_t id) const;
    const std::vector<TileDef>& Palette() const { return palette_; }

    uint16_t Get(int gx, int gy) const;
    // Returns the previous value. Marks the cell's region changed.
    uint16_t Set(int gx, int gy, uint16_t id);
    void Erase(int gx, int gy) { Set(gx, gy, kEmpty); }
    void Clear();

    size_t CellCount() const;
    bool Empty() const { return chunks_.empty(); }
    // Cells in [x0, x1) x [y0, y1), chunk by chunk.
    void ForEachIn(int x0, int y0, int x1, int y1, const std::function<void(int gx, int gy, uint16_t id)>& fn) const;
    void ForEach(const std::function<void(int gx, int gy, uint16_t id)>& fn) const;
    // The occupied cell bounds (x0, y0, x1, y1), exclusive; all zero when empty.
    std::array<int, 4> Bounds() const;
    // The same, rounded out to whole chunks: cheap (no cell is read).
    std::array<int, 4> ChunkBounds() const;

    // Chunks, keyed by (chunk x, chunk y).
    const std::map<std::pair<int, int>, Chunk>& Chunks() const { return chunks_; }

    // Regions changed since the last MarkSaved, keyed by (region x, region y).
    const std::set<std::pair<int, int>>& DirtyRegions() const { return dirty_; }
    void MarkSaved() { dirty_.clear(); }
    void MarkAllDirty();
    // Regions that hold any cell.
    std::set<std::pair<int, int>> Regions() const;
    // Bumped on every change (renderers and grids can cache by it).
    uint64_t Revision() const { return revision_; }

    // For loading: put a palette in place as saved, and a whole chunk.
    void SetPalette(std::vector<TileDef> palette) { palette_ = std::move(palette); }
    void SetChunk(int cx, int cy, const Chunk& chunk);

    static int FloorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

private:
    std::vector<TileDef> palette_;
    std::map<std::pair<int, int>, Chunk> chunks_;
    std::set<std::pair<int, int>> dirty_;
    uint64_t revision_ = 1;
};

// Every layer of a scene, by name, in the registry's context.
struct TileLayers {
    std::map<std::string, TileLayer> layers;
    // Layers removed since the last save (their region files go too).
    std::set<std::string> removed;
    // The scene file they were last loaded from or saved to: saving
    // anywhere else writes every region.
    std::string savedTo;

    TileLayer& Get(const std::string& name);       // creates it if missing
    TileLayer* Find(const std::string& name);
    const TileLayer* Find(const std::string& name) const;
    void Remove(const std::string& name);
    // Draw / priority order: low order first, then by name.
    std::vector<const TileLayer*> Ordered() const;
    size_t CellCount() const;
};

// The scene's layers (created empty on first use), or null if it has none.
TileLayers& LayersOf(Registry& registry);
const TileLayers* FindLayers(const Registry& registry);
// Drops every layer (a scene switch).
void ResetLayers(Registry& registry);

// One cell's changes, recorded for undo.
struct TileChange {
    int gx = 0;
    int gy = 0;
    uint16_t before = 0;
    uint16_t after = 0;
};

// A bulk edit of one layer, as a command: Apply and Revert set the cells to
// `after` or `before`. Layers are found by name, so the edit survives a
// whole-scene snapshot undo in between (entity ids don't).
struct TileLayerEdit {
    explicit TileLayerEdit(std::string layerName = {}) : layer(std::move(layerName)) {}

    std::string layer;
    std::vector<TileChange> changes;

    // Sets a cell through the edit, recording it (the first `before` of a
    // cell is kept if the cell is set twice).
    void Set(TileLayer& target, int gx, int gy, uint16_t id);
    void Apply(Registry& registry) const;
    void Revert(Registry& registry) const;

private:
    std::unordered_map<int64_t, size_t> seen_; // cell -> its index in changes
};

} // namespace aige
