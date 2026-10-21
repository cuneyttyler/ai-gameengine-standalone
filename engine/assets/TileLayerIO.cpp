#include "assets/TileLayerIO.h"

#include "gameplay/TileLayer.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstring>
#include <fstream>
#include <set>
#include <vector>

namespace aige {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr char kMagic[4] = {'A', 'I', 'G', 'T'};
constexpr uint32_t kVersion = 1;
constexpr int kCells = TileLayer::kChunkSize * TileLayer::kChunkSize;

json DefJson(const TileDef& d) {
    const Sprite& s = d.sprite;
    json j = {{"w", s.width}, {"h", s.height}, {"uv", {s.u0, s.v0, s.u1, s.v1}}};
    if (!s.texture.empty()) j["texture"] = s.texture;
    if (s.r != 1.0f || s.g != 1.0f || s.b != 1.0f || s.a != 1.0f) j["tint"] = {s.r, s.g, s.b, s.a};
    if (s.flat) j["flat"] = true;
    if (s.rotation != 0) j["rotation"] = s.rotation;
    if (s.lift != 0.0f) j["lift"] = s.lift;
    if (s.uvCells > 0) j["uvCells"] = {s.uvCells, s.uvOriginX, s.uvOriginY};
    if (s.cornerLift[0] != 0.0f || s.cornerLift[1] != 0.0f || s.cornerLift[2] != 0.0f || s.cornerLift[3] != 0.0f) {
        j["corners"] = {s.cornerLift[0], s.cornerLift[1], s.cornerLift[2], s.cornerLift[3]};
    }
    if (d.solid) j["solid"] = true;
    if (!d.prefab.empty()) j["prefab"] = d.prefab;
    return j;
}

TileDef DefFrom(const json& j) {
    TileDef d;
    Sprite& s = d.sprite;
    s.width = j.value("w", 64.0f);
    s.height = j.value("h", 32.0f);
    if (j.contains("uv") && j["uv"].is_array() && j["uv"].size() == 4) {
        s.u0 = j["uv"][0].get<float>();
        s.v0 = j["uv"][1].get<float>();
        s.u1 = j["uv"][2].get<float>();
        s.v1 = j["uv"][3].get<float>();
    }
    s.texture = j.value("texture", "");
    if (j.contains("tint") && j["tint"].is_array() && j["tint"].size() == 4) {
        s.r = j["tint"][0].get<float>();
        s.g = j["tint"][1].get<float>();
        s.b = j["tint"][2].get<float>();
        s.a = j["tint"][3].get<float>();
    }
    s.flat = j.value("flat", false);
    s.rotation = j.value("rotation", 0);
    s.lift = j.value("lift", 0.0f);
    if (j.contains("uvCells") && j["uvCells"].is_array() && j["uvCells"].size() == 3) {
        s.uvCells = j["uvCells"][0].get<int>();
        s.uvOriginX = j["uvCells"][1].get<int>();
        s.uvOriginY = j["uvCells"][2].get<int>();
    }
    if (j.contains("corners") && j["corners"].is_array() && j["corners"].size() == 4) {
        for (int i = 0; i < 4; ++i) s.cornerLift[i] = j["corners"][i].get<float>();
    }
    d.solid = j.value("solid", false);
    d.prefab = j.value("prefab", "");
    return d;
}

// A layer's folder: its name made safe for a path.
std::string FolderFor(const std::string& name, std::set<std::string>& used) {
    std::string base;
    for (const char c : name) base += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
    if (base.empty()) base = "layer";
    std::string folder = base;
    for (int n = 2; used.contains(folder); ++n) folder = base + "-" + std::to_string(n);
    used.insert(folder);
    return folder;
}

std::string RegionFile(std::pair<int, int> region) {
    return "r_" + std::to_string(region.first) + "_" + std::to_string(region.second) + ".bin";
}

template <typename T>
void Put(std::string& out, T value) {
    char bytes[sizeof(T)];
    std::memcpy(bytes, &value, sizeof(T));
    out.append(bytes, sizeof(T));
}

template <typename T>
bool Take(const std::string& in, size_t& at, T& value) {
    if (at + sizeof(T) > in.size()) return false;
    std::memcpy(&value, in.data() + at, sizeof(T));
    at += sizeof(T);
    return true;
}

std::string EncodeRegion(const TileLayer& layer, std::pair<int, int> region) {
    std::vector<std::pair<std::pair<int, int>, const TileLayer::Chunk*>> chunks;
    for (const auto& [key, chunk] : layer.Chunks()) {
        if (TileLayer::FloorDiv(key.first, TileLayer::kRegionChunks) == region.first &&
            TileLayer::FloorDiv(key.second, TileLayer::kRegionChunks) == region.second) {
            chunks.push_back({key, &chunk});
        }
    }
    if (chunks.empty()) return {};
    std::string out(kMagic, 4);
    Put<uint32_t>(out, kVersion);
    Put<int32_t>(out, region.first);
    Put<int32_t>(out, region.second);
    Put<uint32_t>(out, static_cast<uint32_t>(chunks.size()));
    for (const auto& [key, chunk] : chunks) {
        Put<int32_t>(out, key.first);
        Put<int32_t>(out, key.second);
        // Runs of one value: (length, value) pairs covering the chunk's cells.
        for (int i = 0; i < kCells;) {
            const uint16_t value = chunk->cells[i];
            int run = 1;
            while (i + run < kCells && chunk->cells[i + run] == value && run < 0xFFFF) ++run;
            Put<uint16_t>(out, static_cast<uint16_t>(run));
            Put<uint16_t>(out, value);
            i += run;
        }
    }
    return out;
}

bool DecodeRegion(const std::string& in, TileLayer& layer) {
    if (in.size() < 4 || std::memcmp(in.data(), kMagic, 4) != 0) return false;
    size_t at = 4;
    uint32_t version = 0, count = 0;
    int32_t rx = 0, ry = 0;
    if (!Take(in, at, version) || version != kVersion || !Take(in, at, rx) || !Take(in, at, ry) || !Take(in, at, count)) {
        return false;
    }
    const size_t palette = layer.Palette().size();
    for (uint32_t c = 0; c < count; ++c) {
        int32_t cx = 0, cy = 0;
        if (!Take(in, at, cx) || !Take(in, at, cy)) return false;
        TileLayer::Chunk chunk;
        for (int i = 0; i < kCells;) {
            uint16_t run = 0, value = 0;
            if (!Take(in, at, run) || !Take(in, at, value) || run == 0 || i + run > kCells) return false;
            if (value > palette) value = TileLayer::kEmpty; // a tile kind the palette no longer has
            std::fill(chunk.cells.begin() + i, chunk.cells.begin() + i + run, value);
            i += run;
        }
        layer.SetChunk(cx, cy, chunk);
    }
    return true;
}

void WriteFile(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Failed to write " + path.string());
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

fs::path TileLayersDir(const fs::path& scenePath) {
    fs::path dir = scenePath;
    dir.replace_extension(".tiles");
    return dir;
}

void SaveTileLayers(Registry& registry, const fs::path& scenePath) {
    const fs::path dir = TileLayersDir(scenePath);
    std::error_code ec;
    TileLayers* layers = registry.ctx().find<TileLayers>();
    if (layers == nullptr || layers->layers.empty()) {
        fs::remove_all(dir, ec);
        if (layers) {
            layers->removed.clear();
            layers->savedTo = scenePath.string();
        }
        return;
    }
    const bool elsewhere = layers->savedTo != scenePath.string();
    if (elsewhere) fs::remove_all(dir, ec); // a fresh copy, nothing stale from an older one
    fs::create_directories(dir, ec);

    // The folders the previous save used, to clear out removed layers.
    std::map<std::string, std::string> oldFolders;
    if (!elsewhere) {
        std::ifstream in(dir / "layers.json", std::ios::binary);
        json old = in ? json::parse(in, nullptr, false) : json();
        if (old.is_object()) {
            for (const auto& l : old.value("layers", json::array())) {
                if (l.is_object()) oldFolders[l.value("name", "")] = l.value("folder", "");
            }
        }
    }
    for (const auto& name : layers->removed) {
        if (auto it = oldFolders.find(name); it != oldFolders.end() && !it->second.empty()) fs::remove_all(dir / it->second, ec);
    }
    layers->removed.clear();

    json index = {{"schema", "aige.tiles/1"}, {"chunkSize", TileLayer::kChunkSize}, {"regionChunks", TileLayer::kRegionChunks},
                  {"layers", json::array()}};
    std::set<std::string> used;
    for (auto& [name, layer] : layers->layers) {
        // Keep a layer's folder from save to save.
        std::string folder;
        bool known = false;
        if (auto it = oldFolders.find(name); it != oldFolders.end() && !it->second.empty() && !used.contains(it->second)) {
            folder = it->second;
            used.insert(folder);
            known = true;
        } else {
            folder = FolderFor(name, used);
        }
        json palette = json::array();
        for (const auto& def : layer.Palette()) palette.push_back(DefJson(def));
        json entry = {{"name", name}, {"order", layer.order}, {"folder", folder}, {"palette", palette}};
        // Its heights: [x, y, top, right, bottom, left] per raised cell, to a tenth of a pixel.
        if (!layer.Lifts().empty()) {
            std::vector<std::pair<std::pair<int, int>, TileLayer::CornerLift>> cells;
            for (const auto& [key, l] : layer.Lifts()) cells.push_back({TileLayer::LiftCell(key), l});
            std::sort(cells.begin(), cells.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            json lifts = json::array();
            for (const auto& [cell, l] : cells) {
                json row = {cell.first, cell.second};
                for (const float v : l) row.push_back(std::round(v * 10.0f) / 10.0f);
                lifts.push_back(row);
            }
            entry["lifts"] = lifts;
        }
        index["layers"].push_back(entry);

        const fs::path layerDir = dir / folder;
        // Only what changed, unless this folder is new (a new layer, or another path).
        std::set<std::pair<int, int>> regions = known ? layer.DirtyRegions() : layer.Regions();
        if (!known) fs::remove_all(layerDir, ec);
        fs::create_directories(layerDir, ec);
        for (const auto& region : regions) {
            const std::string bytes = EncodeRegion(layer, region);
            const fs::path file = layerDir / RegionFile(region);
            if (bytes.empty()) fs::remove(file, ec);
            else WriteFile(file, bytes);
        }
        layer.MarkSaved();
    }
    WriteFile(dir / "layers.json", index.dump(1) + "\n");
    layers->savedTo = scenePath.string();
}

std::string LoadTileLayers(Registry& registry, const fs::path& scenePath) {
    ResetLayers(registry);
    const fs::path dir = TileLayersDir(scenePath);
    std::ifstream in(dir / "layers.json", std::ios::binary);
    if (!in) return {};
    const json index = json::parse(in, nullptr, false);
    if (!index.is_object() || index.value("schema", "") != "aige.tiles/1") return dir.string() + ": not a tile layer index";
    if (index.value("chunkSize", 0) != TileLayer::kChunkSize) return dir.string() + ": made with another chunk size";
    TileLayers& layers = LayersOf(registry);
    std::string problems;
    for (const auto& l : index.value("layers", json::array())) {
        if (!l.is_object()) continue;
        TileLayer& layer = layers.Get(l.value("name", "layer"));
        layer.order = l.value("order", 0);
        std::vector<TileDef> palette;
        for (const auto& d : l.value("palette", json::array())) palette.push_back(DefFrom(d));
        layer.SetPalette(std::move(palette));
        for (const auto& row : l.value("lifts", json::array())) {
            if (!row.is_array() || row.size() != 6) continue;
            layer.SetLift(row[0].get<int>(), row[1].get<int>(),
                          {row[2].get<float>(), row[3].get<float>(), row[4].get<float>(), row[5].get<float>()});
        }
        const fs::path layerDir = dir / l.value("folder", "");
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(layerDir, ec)) {
            if (entry.path().extension() != ".bin") continue;
            std::ifstream file(entry.path(), std::ios::binary);
            const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            if (!DecodeRegion(bytes, layer)) problems += (problems.empty() ? "" : "; ") + entry.path().filename().string() + " is damaged";
        }
        layer.MarkSaved();
    }
    layers.removed.clear();
    layers.savedTo = scenePath.string();
    return problems;
}

void CopyTileLayers(const Registry& from, Registry& to) {
    ResetLayers(to);
    if (const TileLayers* layers = FindLayers(from)) to.ctx().emplace<TileLayers>(*layers);
}

} // namespace aige
