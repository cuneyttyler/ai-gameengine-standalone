#pragma once

#include "renderer2d/ImageData.h"

#include <array>
#include <string>
#include <vector>

namespace aige {

// What an image's pixels say about it (development_plan.md 7.3,
// ai-building.md §6): exact where a vision model would guess. Where the
// thing stands (its base and anchor), how big it is on the ground, whether
// it's a whole tile or a texture, its silhouette, palette and symmetry, and
// where it touches its borders (how a fence or wall piece joins).

struct ImageRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool Empty() const { return w <= 0 || h <= 0; }
    bool operator==(const ImageRect&) const = default;
};

struct ArtShape {
    ImageRect region;      // the part of the image analysed (the whole image, or one object of a sheet)
    ImageRect opaque;      // bounds of its solid pixels (shadow left out), in image pixels
    ImageRect base;        // the footprint band: solid columns across its lowest rows
    float anchorX = 0.0f;  // where it stands: the middle of the base, at its lowest solid row
    float anchorY = 0.0f;
    bool shadow = false;   // a soft dark shadow under or beside it
    float opaqueShare = 0.0f; // solid pixels / region pixels
    bool edgesOpaque = false; // every border of the region is solid
    bool seamless = false;    // opposite borders match: it repeats as a texture
    std::string tileType;     // "texture" (complete and seamless), "complete" (fills the region), "partial"
    // "rect" (fills the region), "diamond" (an isometric ground tile),
    // "upright" (stands on a base: at least half as tall as wide, or on a base
    // much narrower than its top), "blob" (anything else: a rug, a decal).
    std::string silhouette;
    bool symmetric = false;   // the same as its mirror image (so mirroring adds no facing)
    // Solid content reaching the middle of each border: N, E, S, W. Square-grid
    // pieces join where these are.
    std::array<bool, 4> touches{};
    // The main axis of the solid pixels, degrees from screen-horizontal
    // (clockwise, y down), and how elongated (major / minor spread). A
    // straight isometric fence runs at about ±26.6°.
    float axisDegrees = 0.0f;
    float elongation = 1.0f;
    std::vector<std::string> palette; // "#rrggbb", most common first (up to 6)
};

// Analyses `region` of `image` (the whole image when empty).
ArtShape AnalyzeShape(const ImageData& image, ImageRect region = {});

// The perspective the silhouette suggests: "top-down" (a full square
// tile), "isometric" (a diamond tile), "upright" (stands on its base: ¾ or
// isometric, which the vision model tells apart), or "" (can't say).
std::string PerspectiveCue(const ArtShape& shape);

// The thing's size on the ground in cells, from its base (never from the
// image size): flat art covers its solid bounds; upright art its base width,
// with a depth of about half its width (at least one cell).
struct Footprint {
    float w = 1.0f, h = 1.0f;  // exact
    int cellsW = 1, cellsH = 1; // rounded, at least 1
};
Footprint FootprintOf(const ArtShape& shape, bool flat, float pixelsPerCell);

// Whole objects on a sheet: groups of solid pixels, joined when closer than
// `gap` pixels, dropping specks under `minPixels`. In reading order.
std::vector<ImageRect> SegmentObjects(const ImageData& image, int gap = 2, int minPixels = 24);

// The same for a grid tilesheet: an object is the tiles whose content runs
// across their shared borders (a two-tile tree is one object). In reading
// order, in pixels, each a whole number of tiles.
std::vector<ImageRect> SegmentTiles(const ImageData& image, int tileWidth, int tileHeight);

// How a linear piece joins (fence, wall, hedge). Square grid: from where it
// touches its borders. Isometric grid: from its axis.
//   square: straight-h, straight-v, corner-ne, corner-se, corner-sw, corner-nw,
//           t-n, t-e, t-s, t-w, cross, end-n, end-e, end-s, end-w, post
//   iso:    straight-x (runs down-right), straight-y (runs down-left), post
// `name` (the file or suggested name) settles what the pixels don't
// ("gate", "corner", "end").
std::string PieceKind(const ArtShape& shape, bool isoGrid, const std::string& name = {});

// "#a1b2c3" -> rgb 0-1, and the perceptual distance of two colours (ΔE in
// CIELAB, about 2.3 just noticeable, 50+ unrelated).
std::array<float, 3> ParseHexColor(const std::string& hex);
float ColorDistance(const std::array<float, 3>& a, const std::array<float, 3>& b);

} // namespace aige
