#include "assets/PackImport.h"

#include "gameplay/Archetypes.h"
#include "gameplay/Components.h"
#include "renderer2d/Components.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>

namespace aige {

namespace fs = std::filesystem;

namespace {

float Median(std::vector<float> values) {
    if (values.empty()) return 0.0f;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

std::string Folder(const std::string& path) { return fs::path(path).parent_path().generic_string(); }

bool IsGroundTile(const AssetInfo& asset) {
    const auto kind = asset.analysis.find("kind");
    if (kind != asset.analysis.end()) return kind->second == "ground";
    return asset.analysis.contains("edges");
}

std::string Analysis(const AssetInfo& asset, const std::string& key) {
    const auto it = asset.analysis.find(key);
    return it == asset.analysis.end() ? std::string() : it->second;
}

} // namespace

std::string PrefabSlug(const std::string& text) {
    std::string slug;
    for (char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u)) {
            slug += static_cast<char>(std::tolower(u));
        } else if (!slug.empty() && slug.back() != '-') {
            slug += '-';
        }
    }
    while (!slug.empty() && slug.back() == '-') slug.pop_back();
    return slug.substr(0, 48);
}

std::optional<float> ReadPackScale(const Project& project, const std::string& folder) {
    std::ifstream in(project.Resolve(folder) / "pack.json");
    if (!in) return std::nullopt;
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    if (!j.is_object() || !j.contains("pixelsPerCell") || !j["pixelsPerCell"].is_number()) return std::nullopt;
    const float value = j["pixelsPerCell"].get<float>();
    return value > 0.0f ? std::optional<float>(value) : std::nullopt;
}

bool WritePackScale(const Project& project, const std::string& folder, float pixelsPerCell) {
    if (pixelsPerCell <= 0.0f) return false;
    std::ofstream out(project.Resolve(folder) / "pack.json");
    out << nlohmann::json{{"pixelsPerCell", pixelsPerCell}}.dump(2) << "\n";
    return static_cast<bool>(out);
}

PackScale PackScaleFor(const Project& project, const AssetDatabase& assets, const std::string& folder,
                       const IsoParams& iso) {
    if (auto set = ReadPackScale(project, folder)) return {*set, "set by hand"};
    std::vector<float> tiles, measured;
    for (const AssetInfo& asset : assets.Assets()) {
        if (Folder(asset.path) != folder || asset.width == 0 || asset.type == AssetType::Audio) continue;
        if (IsGroundTile(asset)) {
            tiles.push_back(static_cast<float>(asset.width));
        } else if (const std::string cells = Analysis(asset, "cells"); !cells.empty()) {
            try {
                const float c = std::stof(cells);
                if (c > 0.05f) measured.push_back(static_cast<float>(asset.width) / c);
            } catch (const std::exception&) {
            }
        }
    }
    if (!tiles.empty()) return {Median(tiles), "ground tiles"};
    if (!measured.empty()) return {Median(measured), "analysis"};
    return {iso.tileWidth, "default"};
}

std::vector<PrefabPlan> PlanPackImport(const AssetDatabase& assets, const std::vector<std::string>& images,
                                       const PrefabLibrary& prefabs, const IsoParams& iso,
                                       const std::function<float(const std::string& folder)>& pixelsPerCell) {
    std::vector<PrefabPlan> plans;
    std::set<std::string> taken;
    for (const std::string& path : images) {
        const AssetInfo* asset = assets.Find(path);
        if (asset == nullptr || asset->type == AssetType::Audio || !asset->error.empty() || asset->width == 0) continue;
        PrefabPlan plan;
        plan.asset = path;
        plan.tile = IsGroundTile(*asset);
        plan.kind = plan.tile ? "ground" : Analysis(*asset, "kind");
        if (plan.kind.empty()) plan.kind = "other";
        if (plan.tile) {
            plan.width = iso.tileWidth; // one cell, lying flat (5.2)
            plan.height = iso.tileHeight;
            plan.solid = false;
        } else {
            const float ppc = std::max(1.0f, pixelsPerCell(Folder(path)));
            plan.width = static_cast<float>(asset->width) / ppc * iso.tileWidth;
            plan.height = static_cast<float>(asset->height) / ppc * iso.tileWidth; // the image's proportions
            plan.solid = Analysis(*asset, "blocks") == "1" || plan.kind == "building";
        }
        // Names: the analysis's, else the file's; prefabs/<pack>/<name>.prefab.
        std::string base = PrefabSlug(Analysis(*asset, "name"));
        if (base.empty()) base = PrefabSlug(fs::path(path).stem().string());
        if (base.empty()) base = "asset";
        const std::string pack = PrefabSlug(fs::path(path).parent_path().filename().string());
        const std::string dir = pack.empty() ? "prefabs/" : "prefabs/" + pack + "/";
        std::string name = base;
        for (int n = 2; taken.contains(dir + name) || prefabs.Find(dir + name + ".prefab") != nullptr; ++n) {
            name = base + "-" + std::to_string(n);
        }
        taken.insert(dir + name);
        plan.name = name;
        plan.path = dir + name + ".prefab";
        plans.push_back(plan);
    }
    return plans;
}

std::string CreatePrefabFromPlan(PrefabLibrary& prefabs, const PrefabPlan& plan, const IsoParams& iso) {
    if (plan.path.rfind("prefabs/", 0) != 0 || plan.path.find("..") != std::string::npos) {
        return "prefabs live under prefabs/";
    }
    if (prefabs.Find(plan.path) != nullptr) return plan.path + " already exists";
    if (plan.width <= 0.0f || plan.height <= 0.0f) return "a prefab needs a size";
    Registry scratch;
    Entity entity = entt::null;
    if (plan.tile) {
        TileStyle style;
        style.texture = plan.asset;
        style.width = iso.tileWidth;
        style.height = iso.tileHeight;
        style.flat = true;
        style.solid = plan.solid;
        entity = CreateTile(scratch, 0, 0, style);
    } else {
        entity = CreateProp(scratch, 0.0f, 0.0f, plan.asset, std::min(plan.width, 4096.0f), std::min(plan.height, 4096.0f));
        if (plan.solid) scratch.emplace<Obstacle>(entity);
    }
    scratch.emplace_or_replace<Name>(entity, plan.name);
    std::error_code ec;
    fs::create_directories(prefabs.GetProject().Resolve(fs::path(plan.path).parent_path().generic_string()), ec);
    std::string error;
    if (!prefabs.SaveAsPrefab(scratch, entity, plan.path, &error)) return error.empty() ? "couldn't save" : error;
    return {};
}

} // namespace aige
