#include "assets/AssetDatabase.h"

#include "assets/TextureCook.h"
#include "audio/Audio.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace aige {

namespace {

namespace fs = std::filesystem;

std::string LowerExtension(const fs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext;
}

bool IsImageExtension(const std::string& ext) {
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga";
}

bool IsAudioExtension(const std::string& ext) {
    return ext == ".wav" || ext == ".mp3" || ext == ".flac";
}

fs::file_time_type WriteTimeOr(const fs::path& path, fs::file_time_type fallback) {
    std::error_code ec;
    auto time = fs::last_write_time(path, ec);
    return ec ? fallback : time;
}

bool ParseType(const std::string& value, AssetType& type) {
    if (value == "texture") {
        type = AssetType::Texture;
    } else if (value == "tileset") {
        type = AssetType::Tileset;
    } else if (value == "audio") {
        type = AssetType::Audio;
    } else {
        return false;
    }
    return true;
}

} // namespace

std::map<std::string, std::string> ReadAssetAnalysis(const Project& project, const std::string& assetPath) {
    std::map<std::string, std::string> fields;
    std::ifstream in(project.Resolve(assetPath + ".meta"));
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        while (!value.empty() && (value.back() == '' || value.back() == ' ')) value.pop_back();
        if (key != "type" && key != "tileWidth" && key != "tileHeight" && !key.empty()) fields[key] = value;
    }
    return fields;
}

const char* AssetTypeName(AssetType type) {
    switch (type) {
        case AssetType::Texture: return "texture";
        case AssetType::Tileset: return "tileset";
        case AssetType::Audio: return "audio";
    }
    return "texture";
}

AssetDatabase::AssetDatabase(Project project) : project_(std::move(project)) {}

const AssetInfo* AssetDatabase::Find(const std::string& path) const {
    for (const auto& asset : assets_) {
        if (asset.path == path) {
            return &asset;
        }
    }
    return nullptr;
}

fs::path AssetDatabase::MetaPathFor(const fs::path& source) const {
    fs::path meta = source;
    meta += ".meta";
    return meta;
}

fs::path AssetDatabase::CookedPathFor(const std::string& path) const {
    return project_.CacheDir() / (path + ".tex");
}

std::optional<AssetDatabase::Meta> AssetDatabase::ReadMeta(const fs::path& metaPath) const {
    std::ifstream in(metaPath);
    if (!in) {
        return std::nullopt;
    }
    Meta meta;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        while (!value.empty() && (value.back() == '\r' || value.back() == ' ')) {
            value.pop_back();
        }
        try {
            if (key == "type") {
                ParseType(value, meta.type);
            } else if (key == "tileWidth") {
                meta.tileWidth = static_cast<uint32_t>(std::stoul(value));
            } else if (key == "tileHeight") {
                meta.tileHeight = static_cast<uint32_t>(std::stoul(value));
            } else if (!key.empty()) {
                meta.analysis[key] = value; // analysis (6.2) and anything newer: kept
            }
        } catch (const std::exception&) {
            // A malformed number keeps the default rather than failing the asset.
        }
    }
    return meta;
}

bool AssetDatabase::WriteMeta(const fs::path& metaPath, const Meta& meta) const {
    std::ofstream out(metaPath, std::ios::trunc);
    if (!out) {
        return false;
    }
    out << "# AIGE Asset v1\n";
    out << "type=" << AssetTypeName(meta.type) << "\n";
    if (meta.type == AssetType::Tileset) {
        out << "tileWidth=" << meta.tileWidth << "\n";
        out << "tileHeight=" << meta.tileHeight << "\n";
    }
    for (const auto& [key, value] : meta.analysis) {
        if (key == "type" || key == "tileWidth" || key == "tileHeight" || value.empty()) continue;
        std::string line = value;
        for (char& c : line) {
            if (c == '\n' || c == '\r') c = ' '; // one line per key
        }
        out << key << "=" << line << "\n";
    }
    return out.good();
}

AssetInfo AssetDatabase::Process(const fs::path& source, bool* cooked) {
    if (cooked) {
        *cooked = false;
    }
    AssetInfo info;
    info.path = project_.Relative(source);
    const std::string ext = LowerExtension(source);
    const fs::path metaPath = MetaPathFor(source);

    std::optional<Meta> meta = ReadMeta(metaPath);
    if (!meta) {
        meta = Meta{};
        meta->type = IsAudioExtension(ext) ? AssetType::Audio : AssetType::Texture;
        WriteMeta(metaPath, *meta);
    }
    info.type = meta->type;
    info.analysis = meta->analysis;

    if (info.type == AssetType::Audio) {
        if (auto audio = ProbeAudio(source)) {
            info.durationSeconds = audio->durationSeconds;
        } else {
            info.error = "unsupported or corrupt audio file";
        }
        return info;
    }

    // Texture / Tileset: re-cook when the source or its settings are newer
    // than the cooked file.
    const fs::path cookedPath = CookedPathFor(info.path);
    auto cookedTime = WriteTimeOr(cookedPath, fs::file_time_type::min());
    bool stale = cookedTime == fs::file_time_type::min() ||
                 cookedTime < WriteTimeOr(source, fs::file_time_type::max()) ||
                 cookedTime < WriteTimeOr(metaPath, fs::file_time_type::min());
    if (stale) {
        std::string error;
        auto image = DecodeImage(source, &error);
        if (!image) {
            info.error = "can't decode image: " + error;
            return info;
        }
        if (!WriteCookedTexture(cookedPath, *image)) {
            info.error = "can't write cooked texture";
            return info;
        }
        info.width = image->width;
        info.height = image->height;
        if (cooked) {
            *cooked = true;
        }
    } else if (!ReadCookedTextureSize(cookedPath, info.width, info.height)) {
        info.error = "corrupt cooked texture";
    }

    if (info.type == AssetType::Tileset) {
        info.tileWidth = meta->tileWidth > 0 ? meta->tileWidth : info.width;
        info.tileHeight = meta->tileHeight > 0 ? meta->tileHeight : info.height;
    }
    return info;
}

std::vector<std::string> AssetDatabase::Refresh() {
    std::vector<std::string> changedTextures;
    std::vector<AssetInfo> scanned;

    std::error_code ec;
    if (fs::exists(project_.AssetsDir(), ec)) {
        for (const auto& entry : fs::recursive_directory_iterator(project_.AssetsDir(), ec)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            std::string ext = LowerExtension(entry.path());
            if (!IsImageExtension(ext) && !IsAudioExtension(ext)) {
                continue; // .meta files and anything we don't import
            }
            bool cooked = false;
            AssetInfo info = Process(entry.path(), &cooked);
            if (cooked) {
                changedTextures.push_back(info.path);
            }
            scanned.push_back(std::move(info));
        }
    }

    std::sort(scanned.begin(), scanned.end(), [](const AssetInfo& a, const AssetInfo& b) { return a.path < b.path; });
    assets_ = std::move(scanned);
    return changedTextures;
}

std::optional<std::string> AssetDatabase::Import(const fs::path& source, std::string* error) {
    auto fail = [&](const std::string& message) -> std::optional<std::string> {
        if (error) *error = message;
        return std::nullopt;
    };

    std::error_code ec;
    if (!fs::is_regular_file(source, ec)) {
        return fail("file not found: " + source.string());
    }
    std::string ext = LowerExtension(source);
    if (!IsImageExtension(ext) && !IsAudioExtension(ext)) {
        return fail("unsupported file type '" + ext + "' (images: png jpg bmp tga; audio: wav mp3 flac)");
    }

    fs::path destDir = project_.AssetsDir() / (IsAudioExtension(ext) ? "audio" : "textures");
    fs::create_directories(destDir, ec);

    // Never overwrite: an existing asset may be referenced by scenes.
    fs::path dest = destDir / source.filename();
    for (int n = 1; fs::exists(dest, ec); ++n) {
        dest = destDir / (source.stem().string() + "_" + std::to_string(n) + source.extension().string());
    }
    if (!fs::copy_file(source, dest, ec)) {
        return fail("copy failed: " + ec.message());
    }

    AssetInfo info = Process(dest, nullptr);
    if (!info.error.empty()) {
        // Don't leave a broken asset behind.
        fs::remove(dest, ec);
        fs::remove(MetaPathFor(dest), ec);
        return fail(info.error);
    }
    std::string path = info.path;
    assets_.push_back(std::move(info));
    std::sort(assets_.begin(), assets_.end(), [](const AssetInfo& a, const AssetInfo& b) { return a.path < b.path; });
    return path;
}

bool AssetDatabase::SetImageSettings(const std::string& path, AssetType type, uint32_t tileWidth,
                                     uint32_t tileHeight) {
    if (type == AssetType::Audio) {
        return false;
    }
    auto it = std::find_if(assets_.begin(), assets_.end(), [&](const AssetInfo& a) { return a.path == path; });
    if (it == assets_.end() || it->type == AssetType::Audio) {
        return false;
    }
    fs::path source = project_.Resolve(path);
    Meta meta = ReadMeta(MetaPathFor(source)).value_or(Meta{});
    meta.type = type;
    meta.tileWidth = tileWidth;
    meta.tileHeight = tileHeight;
    if (!WriteMeta(MetaPathFor(source), meta)) {
        return false;
    }
    *it = Process(source, nullptr);
    return it->error.empty();
}

bool AssetDatabase::SetAnalysis(const std::string& path, const std::map<std::string, std::string>& fields) {
    auto it = std::find_if(assets_.begin(), assets_.end(), [&](const AssetInfo& a) { return a.path == path; });
    if (it == assets_.end()) {
        return false;
    }
    const fs::path source = project_.Resolve(path);
    Meta meta = ReadMeta(MetaPathFor(source)).value_or(Meta{});
    meta.type = it->type;
    for (const auto& [key, value] : fields) {
        if (value.empty()) {
            meta.analysis.erase(key);
        } else {
            meta.analysis[key] = value;
        }
    }
    if (!WriteMeta(MetaPathFor(source), meta)) {
        return false;
    }
    it->analysis = meta.analysis;
    return true;
}

std::optional<ImageData> AssetDatabase::LoadTexture(const std::string& path) {
    fs::path source = project_.Resolve(path);
    std::error_code ec;
    if (!fs::is_regular_file(source, ec)) {
        return std::nullopt;
    }
    // Process re-cooks if stale (e.g. the PNG was edited since the last scan).
    AssetInfo info = Process(source, nullptr);
    if (!info.error.empty() || info.type == AssetType::Audio) {
        return std::nullopt;
    }
    auto it = std::find_if(assets_.begin(), assets_.end(), [&](const AssetInfo& a) { return a.path == path; });
    if (it != assets_.end()) {
        *it = info;
    }
    return ReadCookedTexture(CookedPathFor(path));
}

} // namespace aige
