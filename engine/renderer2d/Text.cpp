#include "renderer2d/Text.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <stdexcept>

namespace aige {

// Generated from fonts/AtkinsonHyperlegible-Regular.ttf at configure time.
extern const uint8_t kBuiltinFontData[];
extern const size_t kBuiltinFontSize;

namespace {

constexpr uint32_t kFirstAscii = 32;   // ' '
constexpr uint32_t kAsciiCount = 95;   // through '~'
constexpr uint32_t kFirstLatin1 = 160; // NBSP
constexpr uint32_t kLatin1Count = 96;  // through 'ÿ'
constexpr uint32_t kFallback = '?';

} // namespace

struct Font::Impl {
    std::vector<uint8_t> ttf; // stbtt keeps pointers into it
    stbtt_fontinfo info{};
    float scale = 0.0f;
    stbtt_packedchar ascii[kAsciiCount]{};
    stbtt_packedchar latin1[kLatin1Count]{};

    const stbtt_packedchar* Glyph(uint32_t cp, uint32_t& resolved) const {
        if (cp >= kFirstAscii && cp < kFirstAscii + kAsciiCount) {
            resolved = cp;
            return &ascii[cp - kFirstAscii];
        }
        if (cp >= kFirstLatin1 && cp < kFirstLatin1 + kLatin1Count) {
            resolved = cp;
            return &latin1[cp - kFirstLatin1];
        }
        resolved = kFallback;
        return &ascii[kFallback - kFirstAscii];
    }
};

namespace {

// Continuation bytes after a lead byte: 0-3, or -1 if it can't start a character.
int Utf8Extra(unsigned char lead) {
    return lead < 0x80 ? 0 : (lead >> 5) == 0x6 ? 1 : (lead >> 4) == 0xE ? 2 : (lead >> 3) == 0x1E ? 3 : -1;
}

} // namespace

std::vector<uint32_t> DecodeUtf8(const std::string& text) {
    std::vector<uint32_t> out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        auto c = static_cast<unsigned char>(text[i]);
        int extra = Utf8Extra(c);
        if (extra < 0 || i + static_cast<size_t>(extra) >= text.size()) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        uint32_t cp = extra == 0 ? c : c & (0x3F >> extra);
        bool valid = true;
        for (int k = 1; k <= extra; ++k) {
            auto next = static_cast<unsigned char>(text[i + k]);
            if ((next >> 6) != 0x2) {
                valid = false;
                break;
            }
            cp = (cp << 6) | (next & 0x3F);
        }
        if (!valid) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        out.push_back(cp);
        i += extra + 1;
    }
    return out;
}

void AddUiRect(std::vector<UiQuad>& out, float x0, float y0, float x1, float y1, UiColor color) {
    UiQuad quad;
    quad.x0 = x0;
    quad.y0 = y0;
    quad.x1 = x1;
    quad.y1 = y1;
    quad.u0 = quad.v0 = quad.u1 = quad.v1 = -1.0f; // untextured
    quad.r = color.r;
    quad.g = color.g;
    quad.b = color.b;
    quad.a = color.a;
    out.push_back(std::move(quad));
}

Font::Font(std::span<const uint8_t> ttf, float pixelHeight) : impl_(std::make_unique<Impl>()), pixelHeight_(pixelHeight) {
    impl_->ttf.assign(ttf.begin(), ttf.end());
    if (!stbtt_InitFont(&impl_->info, impl_->ttf.data(), stbtt_GetFontOffsetForIndex(impl_->ttf.data(), 0))) {
        throw std::runtime_error("not a TrueType font");
    }
    impl_->scale = stbtt_ScaleForPixelHeight(&impl_->info, pixelHeight);
    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&impl_->info, &ascent, &descent, &lineGap);
    ascent_ = std::round(ascent * impl_->scale);
    lineHeight_ = std::round((ascent - descent + lineGap) * impl_->scale);

    // Pack into the smallest square atlas that fits.
    for (int size = 256;; size *= 2) {
        std::vector<uint8_t> alpha(static_cast<size_t>(size) * size);
        stbtt_pack_context pack;
        if (!stbtt_PackBegin(&pack, alpha.data(), size, size, 0, 1, nullptr)) {
            throw std::runtime_error("font atlas allocation failed");
        }
        stbtt_PackSetOversampling(&pack, 1, 1); // 1:1, drawn pixel-aligned: crisp with the nearest sampler
        stbtt_pack_range ranges[2] = {};
        ranges[0].font_size = pixelHeight;
        ranges[0].first_unicode_codepoint_in_range = kFirstAscii;
        ranges[0].num_chars = kAsciiCount;
        ranges[0].chardata_for_range = impl_->ascii;
        ranges[1].font_size = pixelHeight;
        ranges[1].first_unicode_codepoint_in_range = kFirstLatin1;
        ranges[1].num_chars = kLatin1Count;
        ranges[1].chardata_for_range = impl_->latin1;
        bool packed = stbtt_PackFontRanges(&pack, impl_->ttf.data(), 0, ranges, 2) != 0;
        stbtt_PackEnd(&pack);
        if (packed || size >= 4096) {
            atlas_.width = atlas_.height = static_cast<uint32_t>(size);
            atlas_.rgba.resize(alpha.size() * 4);
            for (size_t i = 0; i < alpha.size(); ++i) {
                atlas_.rgba[i * 4 + 0] = 255;
                atlas_.rgba[i * 4 + 1] = 255;
                atlas_.rgba[i * 4 + 2] = 255;
                atlas_.rgba[i * 4 + 3] = alpha[i];
            }
            break;
        }
    }
    static int counter = 0;
    atlasName_ = "@font/" + std::to_string(counter++) + "/" + std::to_string(static_cast<int>(pixelHeight));
}

Font::~Font() = default;

Font& Font::Builtin(float pixelHeight) {
    static std::mutex mutex;
    static std::map<int, std::unique_ptr<Font>> fonts;
    std::lock_guard lock(mutex);
    int key = static_cast<int>(std::round(pixelHeight));
    auto& font = fonts[key];
    if (!font) {
        font = std::make_unique<Font>(std::span<const uint8_t>(kBuiltinFontData, kBuiltinFontSize),
                                      static_cast<float>(key));
    }
    return *font;
}

float Font::Measure(const std::string& text) const {
    float width = 0.0f;
    uint32_t previous = 0;
    for (uint32_t cp : DecodeUtf8(text)) {
        uint32_t resolved = 0;
        const stbtt_packedchar* glyph = impl_->Glyph(cp, resolved);
        if (previous != 0) {
            width += stbtt_GetCodepointKernAdvance(&impl_->info, static_cast<int>(previous),
                                                   static_cast<int>(resolved)) *
                     impl_->scale;
        }
        width += glyph->xadvance;
        previous = resolved;
    }
    return width;
}

std::vector<std::string> Font::Wrap(const std::string& text, float maxWidth) const {
    std::vector<std::string> lines;
    size_t start = 0;
    while (true) {
        size_t newline = text.find('\n', start);
        std::string paragraph = text.substr(start, newline == std::string::npos ? std::string::npos : newline - start);
        if (!paragraph.empty() && paragraph.back() == '\r') {
            paragraph.pop_back();
        }

        std::string line;
        size_t i = 0;
        while (i <= paragraph.size()) {
            size_t space = paragraph.find(' ', i);
            std::string word = paragraph.substr(i, space == std::string::npos ? std::string::npos : space - i);
            std::string candidate = line.empty() ? word : line + " " + word;
            if (Measure(candidate) <= maxWidth || (line.empty() && word.empty())) {
                line = candidate;
            } else {
                if (!line.empty()) {
                    lines.push_back(line);
                    line.clear();
                }
                // A word wider than the line is broken between characters.
                while (Measure(word) > maxWidth && word.size() > 1) {
                    std::string head;
                    size_t byte = 0;
                    while (byte < word.size()) {
                        int extra = Utf8Extra(static_cast<unsigned char>(word[byte]));
                        size_t len = std::min(word.size() - byte, static_cast<size_t>(extra < 0 ? 1 : extra + 1));
                        std::string next = head + word.substr(byte, len);
                        if (Measure(next) > maxWidth && !head.empty()) {
                            break;
                        }
                        head = next;
                        byte += len;
                    }
                    lines.push_back(head);
                    word = word.substr(byte);
                }
                line = word;
            }
            if (space == std::string::npos) {
                break;
            }
            i = space + 1;
        }
        lines.push_back(line);
        if (newline == std::string::npos) {
            break;
        }
        start = newline + 1;
    }
    return lines;
}

float Font::Draw(std::vector<UiQuad>& out, const std::string& text, float x, float top, UiColor color) const {
    // stbtt positions glyphs relative to the baseline; snap it to a pixel.
    float penX = std::round(x);
    float penY = std::round(top + ascent_);
    const float startX = penX;
    uint32_t previous = 0;
    for (uint32_t cp : DecodeUtf8(text)) {
        uint32_t resolved = 0;
        impl_->Glyph(cp, resolved);
        if (previous != 0) {
            penX += stbtt_GetCodepointKernAdvance(&impl_->info, static_cast<int>(previous),
                                                  static_cast<int>(resolved)) *
                    impl_->scale;
        }
        const stbtt_packedchar* table = resolved < kFirstLatin1 ? impl_->ascii : impl_->latin1;
        int index = static_cast<int>(resolved - (resolved < kFirstLatin1 ? kFirstAscii : kFirstLatin1));
        stbtt_aligned_quad q;
        stbtt_GetPackedQuad(table, static_cast<int>(atlas_.width), static_cast<int>(atlas_.height), index, &penX,
                            &penY, &q, 1); // snap to whole pixels
        if (q.x1 > q.x0 && q.y1 > q.y0) { // spaces have no quad
            UiQuad quad;
            quad.x0 = q.x0;
            quad.y0 = q.y0;
            quad.x1 = q.x1;
            quad.y1 = q.y1;
            quad.u0 = q.s0;
            quad.v0 = q.t0;
            quad.u1 = q.s1;
            quad.v1 = q.t1;
            quad.r = color.r;
            quad.g = color.g;
            quad.b = color.b;
            quad.a = color.a;
            quad.texture = atlasName_;
            out.push_back(std::move(quad));
        }
        previous = resolved;
    }
    return penX - startX;
}

} // namespace aige
