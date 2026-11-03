#include <gtest/gtest.h>

#include "ui/UiDesign.h"

#include <filesystem>
#include <fstream>

#ifndef AIGE_SOURCE_DIR
#define AIGE_SOURCE_DIR "."
#endif

using nlohmann::json;
using namespace aige;

TEST(UiDesignTest, AnchorsKeepTheirPlaceAtAnySize) {
    const json design = {{"model", "hud"}, {"root", {
        {{"id", "corner"}, {"type", "panel"}, {"anchor", "br"}, {"x", 20}, {"y", 30}, {"w", 100}, {"h", 50}},
        {{"id", "middle"}, {"type", "panel"}, {"anchor", "c"}, {"x", 0}, {"y", 0}, {"w", 200}, {"h", 100}},
        {{"id", "across"}, {"type", "panel"}, {"anchor", "stretch-bottom"}, {"x", 10}, {"y", 5}, {"w", 10}, {"h", 80}},
    }}};
    const std::string rml = DesignToRml(design);
    EXPECT_NE(rml.find(".n-corner { position: absolute; right: 20dp; width: 100dp; bottom: 30dp; height: 50dp;"), std::string::npos) << rml;
    EXPECT_NE(rml.find("left: 50%; margin-left: -100dp; width: 200dp; top: 50%; margin-top: -50dp;"), std::string::npos);
    EXPECT_NE(rml.find(".n-across { position: absolute; left: 10dp; right: 10dp; bottom: 5dp; height: 80dp;"), std::string::npos);
    EXPECT_NE(rml.find("data-model=\"hud\""), std::string::npos);
}

TEST(UiDesignTest, ButtonsCarryTheirActionsStatesAndPictures) {
    const json design = {{"model", "hud"}, {"root", {
        {{"id", "go"}, {"type", "button"}, {"text", "Craft"}, {"image", "assets/ui/button.png"}, {"slice", {8, 8, 8, 8}}, {"imgW", 64}, {"imgH", 32},
         {"states", {{"hover", {{"bg", "accent"}}}}},
         {"actions", {{{"do", "open"}, {"arg", "crafting"}}, {{"do", "effects"}, {"arg", "[{\"op\": \"set_flag\", \"flag\": \"x\"}]"}}}},
         {"show", {{{"flag", "met_mira"}}}}},
    }}};
    const std::string rml = DesignToRml(design);
    EXPECT_NE(rml.find("action('open:crafting')"), std::string::npos) << rml;
    EXPECT_NE(rml.find("action('effects~"), std::string::npos);
    EXPECT_NE(rml.find("decorator: ninepatch("), std::string::npos);
    EXPECT_NE(rml.find("@spritesheet"), std::string::npos);
    EXPECT_NE(rml.find("src: /assets/ui/button.png"), std::string::npos);
    EXPECT_NE(rml.find(".n-go:hover { background-color: var(--accent);"), std::string::npos);
    EXPECT_NE(rml.find("tick | holds('"), std::string::npos);
    EXPECT_EQ(HexDecode(HexEncode("[{\"flag\": \"a b\"}]")), "[{\"flag\": \"a b\"}]");
    EXPECT_TRUE(CheckDesign(design).empty());
}

TEST(UiDesignTest, ChecksCatchWhatWouldBreak) {
    const json design = {{"root", {
        {{"id", "a"}, {"type", "button"}},
        {{"id", "a"}, {"type", "wobble"}, {"anchor", "nowhere"}},
    }}};
    const auto problems = CheckDesign(design);
    auto has = [&](const std::string& text) {
        return std::any_of(problems.begin(), problems.end(), [&](const std::string& p) { return p.find(text) != std::string::npos; });
    };
    EXPECT_TRUE(has("does nothing"));
    EXPECT_TRUE(has("used twice"));
    EXPECT_TRUE(has("no element type"));
    EXPECT_TRUE(has("no anchor"));
}

TEST(UiDesignTest, AHandWrittenScreenIsKeptWhole) {
    const std::string rml = "<rml><head><title>Map</title><link type=\"text/rcss\" href=\"../styles/base.rcss\"/><style>.x { color: red; }</style></head>"
                            "<body><div data-model=\"ui\"><p>Hello</p></div></body></rml>";
    const json design = DesignFromRml("map", rml);
    ASSERT_EQ(design["root"].size(), 1u);
    EXPECT_EQ(design["root"][0]["type"], "rml");
    const std::string again = DesignToRml(design);
    EXPECT_NE(again.find("<div data-model=\"ui\"><p>Hello</p></div>"), std::string::npos) << again;
    EXPECT_NE(again.find(".x { color: red; }"), std::string::npos);
    // Outside the designer's own model (models can't nest).
    EXPECT_LT(again.find("</div>\n\t<div data-model") == std::string::npos ? 0u : 1u, 2u);
}

TEST(UiDesignTest, TheEnginesStartingDesignsAreValid) {
    for (const char* name : {"hud", "window", "menu", "dialogue"}) {
        std::ifstream in(std::filesystem::path(AIGE_SOURCE_DIR) / "data" / "ui" / "designs" / (std::string(name) + ".json"));
        ASSERT_TRUE(in) << name;
        const json design = json::parse(in);
        EXPECT_TRUE(CheckDesign(design).empty()) << name << ": " << CheckDesign(design).front();
        EXPECT_FALSE(DesignToRml(design).empty());
    }
}
