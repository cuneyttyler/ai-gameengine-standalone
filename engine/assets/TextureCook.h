#pragma once

#include "renderer2d/ImageData.h"

#include <filesystem>
#include <optional>
#include <string>

namespace aige {

// Decodes PNG/JPEG/BMP/TGA from disk to RGBA8 (stb_image).
std::optional<ImageData> DecodeImage(const std::filesystem::path& path, std::string* error = nullptr);

// Writes a PNG (stb_image_write). Used by tests and tooling.
bool WritePng(const std::filesystem::path& path, const ImageData& image);

// The cooked texture format: a tiny header ('AIGT', version, width, height)
// followed by raw RGBA8 — what the runtime loads, so it never decodes PNGs
// on the hot path.
bool WriteCookedTexture(const std::filesystem::path& path, const ImageData& image);
std::optional<ImageData> ReadCookedTexture(const std::filesystem::path& path);

// Reads only the cooked header — for listing asset sizes without loading pixels.
bool ReadCookedTextureSize(const std::filesystem::path& path, uint32_t& width, uint32_t& height);

} // namespace aige
