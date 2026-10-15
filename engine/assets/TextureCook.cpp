#include "assets/TextureCook.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <array>
#include <cstring>
#include <fstream>

namespace aige {

namespace {

constexpr std::array<char, 4> kCookedMagic = {'A', 'I', 'G', 'T'};
constexpr uint32_t kCookedVersion = 1;

// Reads a whole file; stb's own fopen takes narrow paths, which would break
// on non-ASCII Windows paths.
std::optional<std::string> ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteToStream(void* context, void* data, int size) {
    static_cast<std::ofstream*>(context)->write(static_cast<const char*>(data), size);
}

} // namespace

std::optional<ImageData> DecodeImage(const std::filesystem::path& path, std::string* error) {
    auto bytes = ReadFileBytes(path);
    if (!bytes) {
        if (error) *error = "can't read file";
        return std::nullopt;
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes->data()),
                                            static_cast<int>(bytes->size()), &width, &height, &channels, 4);
    if (pixels == nullptr) {
        if (error) *error = stbi_failure_reason();
        return std::nullopt;
    }
    ImageData image;
    image.width = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);
    image.rgba.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
    stbi_image_free(pixels);
    return image;
}

bool WritePng(const std::filesystem::path& path, const ImageData& image) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    int ok = stbi_write_png_to_func(&WriteToStream, &out, static_cast<int>(image.width),
                                    static_cast<int>(image.height), 4, image.rgba.data(),
                                    static_cast<int>(image.width * 4));
    return ok != 0 && out.good();
}

bool WriteCookedTexture(const std::filesystem::path& path, const ImageData& image) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(kCookedMagic.data(), kCookedMagic.size());
    out.write(reinterpret_cast<const char*>(&kCookedVersion), sizeof(kCookedVersion));
    out.write(reinterpret_cast<const char*>(&image.width), sizeof(image.width));
    out.write(reinterpret_cast<const char*>(&image.height), sizeof(image.height));
    out.write(reinterpret_cast<const char*>(image.rgba.data()), static_cast<std::streamsize>(image.rgba.size()));
    return out.good();
}

std::optional<ImageData> ReadCookedTexture(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::array<char, 4> magic{};
    uint32_t version = 0;
    ImageData image;
    in.read(magic.data(), magic.size());
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&image.width), sizeof(image.width));
    in.read(reinterpret_cast<char*>(&image.height), sizeof(image.height));
    if (!in || magic != kCookedMagic || version != kCookedVersion || image.width == 0 || image.height == 0 ||
        image.width > 16384 || image.height > 16384) {
        return std::nullopt;
    }
    image.rgba.resize(static_cast<size_t>(image.width) * image.height * 4);
    in.read(reinterpret_cast<char*>(image.rgba.data()), static_cast<std::streamsize>(image.rgba.size()));
    if (!in) {
        return std::nullopt;
    }
    return image;
}

bool ReadCookedTextureSize(const std::filesystem::path& path, uint32_t& width, uint32_t& height) {
    std::ifstream in(path, std::ios::binary);
    std::array<char, 4> magic{};
    uint32_t version = 0;
    in.read(magic.data(), magic.size());
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&width), sizeof(width));
    in.read(reinterpret_cast<char*>(&height), sizeof(height));
    return in.good() && magic == kCookedMagic && version == kCookedVersion;
}

} // namespace aige
