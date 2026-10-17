#pragma once

#include "assets/Project.h"
#include "renderer2d/ImageData.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace aige {

enum class AssetType {
    Texture, // a single image (sprite, prop)
    Tileset, // an image sliced into a grid of tileWidth x tileHeight tiles
    Audio,
};

const char* AssetTypeName(AssetType type);

// An asset's analysis (6.2) straight from its .meta, without a database:
// for commands and tools that only have the project.
std::map<std::string, std::string> ReadAssetAnalysis(const Project& project, const std::string& assetPath);

// Everything known about one imported asset. Identity is the project-
// relative path of the source file ("assets/textures/hero.png"), which is
// also what scenes store in Sprite::texture — readable and diffable.
struct AssetInfo {
    std::string path;
    AssetType type = AssetType::Texture;
    std::string error; // non-empty if the asset failed to import/cook

    // Texture / Tileset
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t tileWidth = 0; // Tileset only
    uint32_t tileHeight = 0;

    // Audio
    double durationSeconds = 0.0;

    // What's known about the art (development_plan.md 6.2), kept in the meta:
    // description, kind, tags, direction, cells, blocks, name (a suggested
    // file name) from image analysis; edges (each corner and edge's terrain,
    // clockwise from the top-left) from the tile-edge analysis.
    std::map<std::string, std::string> analysis;

    uint32_t TileColumns() const { return tileWidth ? width / tileWidth : 0; }
    uint32_t TileRows() const { return tileHeight ? height / tileHeight : 0; }
    uint32_t TileCount() const { return TileColumns() * TileRows(); }
};

// The project's asset pipeline (development_plan.md milestone 1.3).
//
// Each source file under assets/ has a sidecar "<file>.meta" holding its
// import settings (type, tile size), in the same Key=Value text style as
// scene files. Textures are cooked to .cache/ (raw RGBA8) and re-cooked
// whenever the source or meta is newer than the cooked file. Audio is
// validated on import and played from the source file.
class AssetDatabase {
public:
    explicit AssetDatabase(Project project);

    const Project& GetProject() const { return project_; }

    // Rescans assets/: picks up files added or edited outside the editor
    // (writing default metas for new ones) and drops deleted ones. Returns
    // the paths of textures whose pixels changed since the last scan, so the
    // renderer can reload them.
    std::vector<std::string> Refresh();

    const std::vector<AssetInfo>& Assets() const { return assets_; }
    const AssetInfo* Find(const std::string& path) const;

    // Copies an external file into assets/textures or assets/audio (renaming
    // on collision), writes its meta and cooks it. Returns the new asset's
    // path, or nullopt with `error` set.
    std::optional<std::string> Import(const std::filesystem::path& source, std::string* error = nullptr);

    // Changes an image asset between Texture and Tileset (and its tile
    // size); rewrites the meta.
    bool SetImageSettings(const std::string& path, AssetType type, uint32_t tileWidth, uint32_t tileHeight);

    // Merges `fields` into the asset's analysis (an empty value removes a
    // key) and rewrites the meta; import settings are kept.
    bool SetAnalysis(const std::string& path, const std::map<std::string, std::string>& fields);

    // Cooked pixels for a Texture/Tileset asset, cooking first if stale.
    std::optional<ImageData> LoadTexture(const std::string& path);

private:
    struct Meta {
        AssetType type = AssetType::Texture;
        uint32_t tileWidth = 0;
        uint32_t tileHeight = 0;
        std::map<std::string, std::string> analysis; // every other key=value line, kept as is
    };

    std::filesystem::path MetaPathFor(const std::filesystem::path& source) const;
    std::filesystem::path CookedPathFor(const std::string& path) const;
    std::optional<Meta> ReadMeta(const std::filesystem::path& metaPath) const;
    bool WriteMeta(const std::filesystem::path& metaPath, const Meta& meta) const;

    // (Re)builds an asset's info, cooking textures if stale. `cooked` is set
    // when new pixels were written.
    AssetInfo Process(const std::filesystem::path& source, bool* cooked);

    Project project_;
    std::vector<AssetInfo> assets_;
};

} // namespace aige
