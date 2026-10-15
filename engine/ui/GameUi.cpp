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
