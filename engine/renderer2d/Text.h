#pragma once

#include "renderer2d/ImageData.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace aige {

// A quad in viewport pixels, drawn after everything else with no camera:
// in-game UI (dialogue boxes, HUD text). `texture` names a texture
// registered with Renderer2D::RegisterTexture (a font atlas); an empty name
// draws a solid color.
struct UiQuad {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
    std::string texture;
};

struct UiColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

void AddUiRect(std::vector<UiQuad>& out, float x0, float y0, float x1, float y1, UiColor color);

// A TrueType font baked at one pixel size into an RGBA atlas (white, coverage
// in alpha) covering Latin-1; other characters draw as '?'. Layout is CPU-only,
// so it's testable without a GPU. Text is UTF-8.
class Font {
public:
    Font(std::span<const uint8_t> ttf, float pixelHeight);
    ~Font();
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    // The engine's built-in font (Atkinson Hyperlegible, SIL OFL 1.1) at a
    // size; baked once per size and kept.
    static Font& Builtin(float pixelHeight);

    float PixelHeight() const { return pixelHeight_; }
    float LineHeight() const { return lineHeight_; }
    float Ascent() const { return ascent_; }
    // Texture name to register the atlas under (unique per font).
    const std::string& AtlasName() const { return atlasName_; }
    const ImageData& Atlas() const { return atlas_; }

    float Measure(const std::string& text) const; // width of one line, in pixels
    // Greedy word wrap to `maxWidth`; '\n' starts a new line; a word longer
    // than a line is broken between characters.
    std::vector<std::string> Wrap(const std::string& text, float maxWidth) const;

    // One line of text with its top-left at (x, top). Returns its width.
    float Draw(std::vector<UiQuad>& out, const std::string& text, float x, float top, UiColor color) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    float pixelHeight_;
    float lineHeight_ = 0.0f;
    float ascent_ = 0.0f;
    std::string atlasName_;
    ImageData atlas_;
};

// UTF-8 to code points (invalid bytes become U+FFFD).
std::vector<uint32_t> DecodeUtf8(const std::string& text);

} // namespace aige
