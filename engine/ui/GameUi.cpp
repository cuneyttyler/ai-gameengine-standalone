#include "ui/GameUi.h"

#include "renderer2d/Renderer2D.h"

#include <algorithm>
#include <cmath>

namespace aige {

namespace {

// Sizes and colors in one place, so the game's UI reads as one design.
constexpr float kTextSize = 20.0f;
constexpr float kSmallSize = 15.0f;
constexpr float kTitleSize = 40.0f;
constexpr float kMargin = 20.0f;
constexpr float kPadding = 16.0f;
constexpr float kMaxBoxWidth = 760.0f;

constexpr UiColor kPanel{0.05f, 0.05f, 0.09f, 0.88f};
constexpr UiColor kBorder{0.23f, 0.51f, 0.96f, 0.9f}; // the editor's accent blue
constexpr UiColor kText{0.93f, 0.93f, 0.95f, 1.0f};
constexpr UiColor kDim{0.93f, 0.93f, 0.95f, 0.55f};
constexpr UiColor kSpeaker{0.58f, 0.77f, 0.99f, 1.0f};
constexpr UiColor kHighlight{1.0f, 0.86f, 0.45f, 1.0f};

void AddFramedPanel(std::vector<UiQuad>& out, float x0, float y0, float x1, float y1) {
    AddUiRect(out, x0, y0, x1, y1, kPanel);
    AddUiRect(out, x0, y0, x1, y0 + 2.0f, kBorder); // a top rule, square corners
}

} // namespace

void RegisterGameUiFonts(Renderer2D& renderer) {
    for (float size : {kTextSize, kSmallSize, kTitleSize}) {
        const Font& font = Font::Builtin(size);
        renderer.RegisterTexture(font.AtlasName(), font.Atlas());
    }
}

void AddBanner(std::vector<UiQuad>& out, const std::string& title, const std::string& subtitle, float viewportWidth,
               float viewportHeight) {
    const Font& big = Font::Builtin(kTitleSize);
    const Font& small = Font::Builtin(kTextSize);
    float width = std::max(big.Measure(title), small.Measure(subtitle)) + kPadding * 4.0f;
    width = std::min(width, viewportWidth - kMargin * 2.0f);
    float height = big.LineHeight() + (subtitle.empty() ? 0.0f : small.LineHeight() + 6.0f) + kPadding * 2.0f;
    float x0 = std::round((viewportWidth - width) * 0.5f);
    float y0 = std::round(viewportHeight * 0.38f - height * 0.5f);
    AddFramedPanel(out, x0, y0, x0 + width, y0 + height);
    big.Draw(out, title, std::round((viewportWidth - big.Measure(title)) * 0.5f), y0 + kPadding, kText);
    if (!subtitle.empty()) {
        small.Draw(out, subtitle, std::round((viewportWidth - small.Measure(subtitle)) * 0.5f),
                   y0 + kPadding + big.LineHeight() + 6.0f, kDim);
    }
}

DialogueBoxLayout AddDialogueBox(std::vector<UiQuad>& out, const DialogueView& view, float viewportWidth,
                                 float viewportHeight) {
    const Font& font = Font::Builtin(kTextSize);
    const Font& small = Font::Builtin(kSmallSize);
    DialogueBoxLayout layout;
    float width = std::min(kMaxBoxWidth, viewportWidth - kMargin * 2.0f);
    float inner = width - kPadding * 2.0f;
    layout.textLines = font.Wrap(view.text, inner);

    // Each choice is "1. text", wrapped with a hanging indent.
    const float indent = font.Measure("4.  ");
    std::vector<std::vector<std::string>> choiceLines;
    for (const auto& choice : view.choices) {
        choiceLines.push_back(font.Wrap(choice, inner - indent));
    }

    float height = kPadding * 2.0f;
    if (!view.speaker.empty()) height += small.LineHeight() + 4.0f;
    height += font.LineHeight() * static_cast<float>(layout.textLines.size());
    if (!choiceLines.empty()) height += 8.0f;
    for (const auto& lines : choiceLines) height += font.LineHeight() * static_cast<float>(lines.size()) + 2.0f;
    height += small.LineHeight() + 6.0f; // hint

    layout.x0 = std::round((viewportWidth - width) * 0.5f);
    layout.x1 = layout.x0 + width;
    layout.y1 = std::round(viewportHeight - kMargin);
    layout.y0 = layout.y1 - std::round(height);
    AddFramedPanel(out, layout.x0, layout.y0, layout.x1, layout.y1);
    if (!view.portrait.empty()) {
        const float size = 112.0f;
        AddUiRect(out, layout.x0, layout.y0 - size - 8.0f, layout.x0 + size + 4.0f, layout.y0 - 4.0f, kBorder);
        UiQuad picture;
        picture.x0 = layout.x0 + 2.0f;
        picture.y0 = layout.y0 - size - 6.0f;
        picture.x1 = picture.x0 + size;
        picture.y1 = picture.y0 + size;
        picture.u1 = 1.0f;
        picture.v1 = 1.0f;
        picture.texture = view.portrait;
        out.push_back(picture);
    }

    float x = layout.x0 + kPadding;
    float y = layout.y0 + kPadding;
    if (!view.speaker.empty()) {
        small.Draw(out, view.speaker, x, y, kSpeaker);
        y += small.LineHeight() + 4.0f;
    }
    for (const auto& line : layout.textLines) {
        font.Draw(out, line, x, y, kText);
        y += font.LineHeight();
    }
    if (!choiceLines.empty()) y += 8.0f;
    for (size_t i = 0; i < choiceLines.size(); ++i) {
        const bool on = static_cast<int>(i) == view.highlighted;
        const UiColor color = on ? kHighlight : kText;
        layout.choiceTops.push_back(y);
        font.Draw(out, std::to_string(i + 1) + ".", x, y, on ? kHighlight : kDim);
        for (const auto& line : choiceLines[i]) {
            font.Draw(out, line, x + indent, y, color);
            y += font.LineHeight();
        }
        y += 2.0f;
    }
    const std::string hint = view.choices.empty() ? "Space to continue" : "1-4, or Up/Down and Enter to choose";
    small.Draw(out, hint, layout.x1 - kPadding - small.Measure(hint), layout.y1 - kPadding - small.LineHeight(), kDim);
    return layout;
}

void AddObjective(std::vector<UiQuad>& out, const std::string& title, const std::string& line, float viewportWidth,
                  float viewportHeight) {
    (void)viewportHeight;
    const Font& small = Font::Builtin(kSmallSize);
    const Font& font = Font::Builtin(kTextSize);
    float width = std::max(small.Measure(title), font.Measure(line)) + kPadding * 2.0f;
    width = std::min(width, viewportWidth - kMargin * 2.0f);
    float height = small.LineHeight() + font.LineHeight() + 4.0f + 20.0f;
    AddFramedPanel(out, kMargin, kMargin, kMargin + width, kMargin + height);
    small.Draw(out, title, kMargin + kPadding, kMargin + 10.0f, kSpeaker);
    font.Draw(out, line, kMargin + kPadding, kMargin + 14.0f + small.LineHeight(), kText);
}

void AddClock(std::vector<UiQuad>& out, const std::string& text, float viewportWidth, float viewportHeight) {
    (void)viewportHeight;
    const Font& font = Font::Builtin(kSmallSize);
    float width = font.Measure(text) + 20.0f;
    float x1 = viewportWidth - kMargin;
    AddFramedPanel(out, x1 - width, kMargin, x1, kMargin + font.LineHeight() + 12.0f);
    font.Draw(out, text, x1 - width + 10.0f, kMargin + 6.0f, kText);
}

void AddToast(std::vector<UiQuad>& out, const std::string& text, float viewportWidth, float viewportHeight) {
    (void)viewportHeight;
    const Font& font = Font::Builtin(kTextSize);
    float width = std::min(font.Measure(text) + kPadding * 2.0f, viewportWidth - kMargin * 2.0f);
    float height = font.LineHeight() + 16.0f;
    float x0 = std::round((viewportWidth - width) * 0.5f);
    AddFramedPanel(out, x0, kMargin, x0 + width, kMargin + height);
    font.Draw(out, text, x0 + kPadding, kMargin + 8.0f, kHighlight);
}

void AddPrompt(std::vector<UiQuad>& out, const std::string& key, const std::string& text, float viewportWidth,
               float viewportHeight) {
    const Font& font = Font::Builtin(kTextSize);
    float keyWidth = font.Measure(key) + 14.0f;
    float width = keyWidth + 10.0f + font.Measure(text) + 20.0f;
    float height = font.LineHeight() + 12.0f;
    float x0 = std::round((viewportWidth - width) * 0.5f);
    float y0 = std::round(viewportHeight - kMargin - 120.0f - height);
    AddUiRect(out, x0, y0, x0 + width, y0 + height, kPanel);
    AddUiRect(out, x0 + 6.0f, y0 + 4.0f, x0 + 6.0f + keyWidth, y0 + height - 4.0f, kBorder);
    font.Draw(out, key, x0 + 13.0f, y0 + 6.0f, kText);
    font.Draw(out, text, x0 + 6.0f + keyWidth + 10.0f, y0 + 6.0f, kText);
}

} // namespace aige

namespace aige {

void AddScreenFade(std::vector<UiQuad>& out, float alpha, float viewportWidth, float viewportHeight) {
    if (alpha <= 0.0f) return;
    AddUiRect(out, 0.0f, 0.0f, viewportWidth, viewportHeight, {0.0f, 0.0f, 0.0f, std::min(1.0f, alpha)});
}

void AddMenu(std::vector<UiQuad>& out, const MenuView& view, float viewportWidth, float viewportHeight) {
    const Font& big = Font::Builtin(kTitleSize);
    const Font& font = Font::Builtin(kTextSize);
    const Font& small = Font::Builtin(kSmallSize);
    if (view.dimBackground) AddUiRect(out, 0.0f, 0.0f, viewportWidth, viewportHeight, {0.0f, 0.0f, 0.0f, 0.55f});

    float width = std::max(big.Measure(view.title), small.Measure(view.subtitle));
    for (const auto& item : view.items) width = std::max(width, font.Measure(item.label) + font.Measure("> "));
    std::vector<std::string> bodyLines;
    if (!view.body.empty()) {
        bodyLines = small.Wrap(view.body, std::min(kMaxBoxWidth, viewportWidth - kMargin * 2.0f) - kPadding * 2.0f);
        for (const auto& line : bodyLines) width = std::max(width, small.Measure(line));
    }
    width = std::min(width + kPadding * 4.0f, viewportWidth - kMargin * 2.0f);
    float height = kPadding * 2.0f + big.LineHeight();
    if (!view.subtitle.empty()) height += small.LineHeight() + 4.0f;
    if (!bodyLines.empty()) height += 10.0f + small.LineHeight() * static_cast<float>(bodyLines.size());
    height += 14.0f + (font.LineHeight() + 6.0f) * static_cast<float>(view.items.size());
    if (!view.footer.empty()) height += small.LineHeight() + 8.0f;

    const float x0 = std::round((viewportWidth - width) * 0.5f);
    float y = std::round(std::max(kMargin, viewportHeight * 0.45f - height * 0.5f));
    AddFramedPanel(out, x0, y, x0 + width, y + height);
    y += kPadding;
    big.Draw(out, view.title, std::round((viewportWidth - big.Measure(view.title)) * 0.5f), y, kText);
    y += big.LineHeight();
    if (!view.subtitle.empty()) {
        small.Draw(out, view.subtitle, std::round((viewportWidth - small.Measure(view.subtitle)) * 0.5f), y + 4.0f, kDim);
        y += small.LineHeight() + 4.0f;
    }
    if (!bodyLines.empty()) {
        y += 10.0f;
        for (const auto& line : bodyLines) {
            small.Draw(out, line, x0 + kPadding * 2.0f, y, kDim);
            y += small.LineHeight();
        }
    }
    y += 14.0f;
    for (size_t i = 0; i < view.items.size(); ++i) {
        const auto& item = view.items[i];
        const bool lit = static_cast<int>(i) == view.highlighted;
        const UiColor color = !item.enabled ? kDim : lit ? kHighlight : kText;
        const std::string label = (lit ? "> " : "  ") + item.label;
        font.Draw(out, label, std::round((viewportWidth - font.Measure(label)) * 0.5f), y, color);
        y += font.LineHeight() + 6.0f;
    }
    if (!view.footer.empty()) {
        small.Draw(out, view.footer, std::round((viewportWidth - small.Measure(view.footer)) * 0.5f), y + 4.0f, kDim);
    }
}

} // namespace aige

namespace aige {

void AddSubtitle(std::vector<UiQuad>& out, const std::string& text, float viewportWidth, float viewportHeight) {
    if (text.empty()) return;
    const Font& font = Font::Builtin(kTextSize);
    const auto lines = font.Wrap(text, std::min(kMaxBoxWidth, viewportWidth - kMargin * 2.0f));
    float width = 0.0f;
    for (const auto& line : lines) width = std::max(width, font.Measure(line));
    const float height = font.LineHeight() * static_cast<float>(lines.size()) + 12.0f;
    const float x0 = std::round((viewportWidth - width) * 0.5f) - 10.0f;
    const float y0 = viewportHeight - kMargin - 70.0f - height;
    AddUiRect(out, x0, y0, x0 + width + 20.0f, y0 + height, {0.0f, 0.0f, 0.0f, 0.6f});
    float y = y0 + 6.0f;
    for (const auto& line : lines) {
        font.Draw(out, line, std::round((viewportWidth - font.Measure(line)) * 0.5f), y, kText);
        y += font.LineHeight();
    }
}

void AddDebugLines(std::vector<UiQuad>& out, const std::vector<std::string>& lines, float viewportWidth,
                   float viewportHeight) {
    (void)viewportHeight;
    if (lines.empty()) return;
    const Font& font = Font::Builtin(kSmallSize);
    float width = 0.0f;
    for (const auto& line : lines) width = std::max(width, font.Measure(line));
    const float x0 = viewportWidth - width - 24.0f;
    const float y0 = 56.0f;
    AddUiRect(out, x0, y0, x0 + width + 16.0f, y0 + font.LineHeight() * static_cast<float>(lines.size()) + 10.0f,
              {0.0f, 0.0f, 0.0f, 0.65f});
    float y = y0 + 5.0f;
    for (const auto& line : lines) {
        font.Draw(out, line, x0 + 8.0f, y, {0.8f, 0.95f, 0.8f, 1.0f});
        y += font.LineHeight();
    }
}

} // namespace aige

namespace aige {

void AddLetterbox(std::vector<UiQuad>& out, float amount, float viewportWidth, float viewportHeight) {
    if (amount <= 0.0f) return;
    const float bar = std::round(viewportHeight * 0.11f * std::min(1.0f, amount));
    AddUiRect(out, 0.0f, 0.0f, viewportWidth, bar, {0.0f, 0.0f, 0.0f, 1.0f});
    AddUiRect(out, 0.0f, viewportHeight - bar, viewportWidth, viewportHeight, {0.0f, 0.0f, 0.0f, 1.0f});
}

void AddTitleCard(std::vector<UiQuad>& out, const std::string& text, float alpha, float viewportWidth,
                  float viewportHeight) {
    if (text.empty() || alpha <= 0.0f) return;
    const Font& big = Font::Builtin(kTitleSize);
    const UiColor color{kText.r, kText.g, kText.b, std::min(1.0f, alpha)};
    big.Draw(out, text, std::round((viewportWidth - big.Measure(text)) * 0.5f),
             std::round(viewportHeight * 0.42f - big.LineHeight() * 0.5f), color);
}

void AddObjectiveMarker(std::vector<UiQuad>& out, float dirX, float dirY, const std::string& label,
                        float viewportWidth, float viewportHeight) {
    const float length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length < 0.0001f) return;
    const float radius = std::min(viewportWidth, viewportHeight) * 0.36f;
    const float cx = viewportWidth * 0.5f + dirX / length * radius;
    const float cy = viewportHeight * 0.5f + dirY / length * radius;
    AddUiRect(out, cx - 7.0f, cy - 7.0f, cx + 7.0f, cy + 7.0f, {0.0f, 0.0f, 0.0f, 0.6f});
    AddUiRect(out, cx - 5.0f, cy - 5.0f, cx + 5.0f, cy + 5.0f, kHighlight);
    if (!label.empty()) {
        const Font& small = Font::Builtin(kSmallSize);
        small.Draw(out, label, std::round(cx - small.Measure(label) * 0.5f), cy + 9.0f, kHighlight);
    }
}

} // namespace aige
