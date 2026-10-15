#include <gtest/gtest.h>

#include "renderer2d/Text.h"
#include "ui/GameUi.h"

#include <algorithm>

// development_plan.md 3.2a: text layout and the game's UI, CPU-only.

TEST(TextTest, TheBuiltinFontBakesAndMeasures) {
    const aige::Font& font = aige::Font::Builtin(20.0f);
    EXPECT_EQ(&font, &aige::Font::Builtin(20.0f)); // cached per size
    EXPECT_GT(font.Atlas().width, 0u);
    EXPECT_EQ(font.Atlas().rgba.size(), static_cast<size_t>(font.Atlas().width) * font.Atlas().height * 4);
    EXPECT_GE(font.LineHeight(), 20.0f);
    EXPECT_GT(font.Ascent(), 0.0f);

    EXPECT_EQ(font.Measure(""), 0.0f);
    EXPECT_GT(font.Measure("ab"), font.Measure("a"));
    EXPECT_GT(font.Measure("WWW"), font.Measure("iii"));
    EXPECT_GT(aige::Font::Builtin(40.0f).Measure("Victory"), font.Measure("Victory"));
}

TEST(TextTest, DrawingMakesOneQuadPerVisibleCharacter) {
    const aige::Font& font = aige::Font::Builtin(20.0f);
    std::vector<aige::UiQuad> quads;
    float width = font.Draw(quads, "Hi there", 10.0f, 50.0f, {});
    EXPECT_EQ(quads.size(), 7u); // the space has no quad
    EXPECT_NEAR(width, font.Measure("Hi there"), 1.0f);
    for (const auto& q : quads) {
        EXPECT_EQ(q.texture, font.AtlasName());
        EXPECT_GE(q.x0, 10.0f - 2.0f);
        EXPECT_GE(q.y0, 50.0f - 2.0f);
        EXPECT_LE(q.y1, 50.0f + font.LineHeight() + 2.0f);
        EXPECT_EQ(q.x0, std::round(q.x0)); // pixel-aligned: crisp with the nearest sampler
    }
}

TEST(TextTest, Latin1AndUnknownCharacters) {
    const aige::Font& font = aige::Font::Builtin(20.0f);
    EXPECT_EQ(aige::DecodeUtf8("caf\xC3\xA9"), (std::vector<uint32_t>{'c', 'a', 'f', 0xE9}));
    EXPECT_EQ(aige::DecodeUtf8("\xE2\x86\x92"), (std::vector<uint32_t>{0x2192}));
    EXPECT_EQ(aige::DecodeUtf8("a\xFF" "b"), (std::vector<uint32_t>{'a', 0xFFFD, 'b'}));
    EXPECT_EQ(aige::DecodeUtf8("\xC3"), (std::vector<uint32_t>{0xFFFD})); // truncated
    std::vector<aige::UiQuad> quads;
    font.Draw(quads, "\xC3\xA9\xE2\x86\x92", 0, 0, {}); // é, then an arrow (outside Latin-1: '?')
    EXPECT_EQ(quads.size(), 2u);
    EXPECT_EQ(font.Measure("\xE2\x86\x92"), font.Measure("?"));
}

TEST(TextTest, WrappingKeepsEveryLineInsideTheWidth) {
    const aige::Font& font = aige::Font::Builtin(20.0f);
    const std::string text =
        "The Wardens sent me because I grew up here, and because nobody else would come. "
        "Supercalifragilisticexpialidociousness is not a word anyone in Brookvale uses.";
    for (float width : {120.0f, 240.0f, 500.0f}) {
        auto lines = font.Wrap(text, width);
        ASSERT_FALSE(lines.empty());
        std::string joined;
        for (const auto& line : lines) {
            EXPECT_LE(font.Measure(line), width) << "'" << line << "' at " << width;
            joined += line;
        }
        // Nothing lost but the spaces that became line breaks.
        auto stripped = [](std::string s) {
            s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
            return s;
        };
        EXPECT_EQ(stripped(joined), stripped(text));
    }
    EXPECT_EQ(font.Wrap("one\ntwo\n\nfour", 1000.0f), (std::vector<std::string>{"one", "two", "", "four"}));
    EXPECT_EQ(font.Wrap("", 100.0f), (std::vector<std::string>{""}));
}

TEST(GameUiTest, TheDialogueBoxFitsItsTextAndChoices) {
    aige::DialogueView view;
    view.speaker = "Old Tamsin";
    view.text = "Another Warden? Last one went up that hill and came back in a sack. Take a draught, at least.";
    view.choices = {"I'll be careful.", "What happened to the last one?", "Goodbye."};
    view.highlighted = 1;

    std::vector<aige::UiQuad> quads;
    auto layout = aige::AddDialogueBox(quads, view, 1280.0f, 720.0f);
    EXPECT_LE(layout.x1 - layout.x0, 760.0f);
    EXPECT_GT(layout.y0, 0.0f);
    EXPECT_LE(layout.y1, 720.0f);
    EXPECT_EQ(layout.choiceTops.size(), 3u);
    EXPECT_TRUE(std::is_sorted(layout.choiceTops.begin(), layout.choiceTops.end()));
    for (const auto& q : quads) { // everything is inside the box
        EXPECT_GE(q.x0, layout.x0 - 1.0f);
        EXPECT_LE(q.x1, layout.x1 + 1.0f);
        EXPECT_GE(q.y0, layout.y0 - 1.0f);
        EXPECT_LE(q.y1, layout.y1 + 1.0f);
    }

    // On a narrow screen the text wraps more and the box grows upward.
    std::vector<aige::UiQuad> narrow;
    auto small = aige::AddDialogueBox(narrow, view, 420.0f, 720.0f);
    EXPECT_GT(small.textLines.size(), layout.textLines.size());
    EXPECT_LT(small.y0, layout.y0);
    EXPECT_GE(small.x0, 0.0f);
}

TEST(GameUiTest, BannersObjectivesAndPromptsStayOnScreen) {
    for (float width : {640.0f, 1920.0f}) {
        std::vector<aige::UiQuad> quads;
        aige::AddBanner(quads, "Victory!", "All enemies defeated", width, 480.0f);
        aige::AddObjective(quads, "Clear the courtyard", "Skeletons 2/4", width, 480.0f);
        aige::AddPrompt(quads, "E", "Talk to Old Tamsin", width, 480.0f);
        ASSERT_FALSE(quads.empty());
        for (const auto& q : quads) {
            EXPECT_GE(q.x0, 0.0f);
            EXPECT_LE(q.x1, width);
            EXPECT_GE(q.y0, 0.0f);
            EXPECT_LE(q.y1, 480.0f);
        }
    }
}
