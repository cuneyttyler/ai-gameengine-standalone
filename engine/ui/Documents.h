#pragma once

#include "renderer2d/Text.h"
#include "ui/GameUi.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace aige {

// The game's UI as documents (development_plan.md Phase 17, game-ui.md): RmlUi lays
// out and styles HTML/CSS-like documents (RML, RCSS); this layer gives it our font,
// our renderer (its geometry becomes UiQuads), the game's data as bound models, the
// theme's tokens and the screens manifest. The game's logic stays where it is:
// menus are still GameFlow and PlayWindows, which say what's on screen (MenuView);
// a document only shows it, and a click is routed back to the same Choose().
//
// Documents are looked for in the project's ui/ folder first, then the engine's
// data/ui, so a project customises a screen by copying it.

// What the HUD shows (bound as the "hud" model).
struct HudData {
    bool visible = true;
    float health = 0, healthMax = 0, mana = 0, manaMax = 0, stamina = 0, staminaMax = 0;
    int level = 0;
    float xp = 0, xpNext = 0;
    int gold = 0;
    std::string quest, objective; // the tracked quest and its objective
    std::string clock;            // "Day 2  14:30" (empty: hidden)
    std::string promptKey, promptText; // "E", "Talk to Old Tamsin"
    std::string subtitle;         // a bark or a cutscene line
    std::string marker;           // the objective marker's distance ("12 m"; empty: hidden)
    float markerX = 0, markerY = 0; // its position on screen
};

// A timed message (game-ui.md §9), in a channel: toast, feed, banner, alert.
struct Notification {
    std::string channel = "toast";
    std::string text;
    std::string icon;
    double seconds = 3.0;
    int count = 1; // feed entries coalesced ("+3 gold" merged)
    std::string key; // what merges: the same key in the same channel
};

// The screens manifest (ui/screens.json): which document shows a screen, its layer
// and whether it pauses. Screens without an entry use the generic menu document.
struct ScreenEntry {
    std::string id;       // title, pause, settings, load, save, game-over, victory, credits, difficulty,
                          // inventory, shop, journal, dialogue, hud
    std::string document; // screens/<id>.rml
    std::string layer = "screen"; // screen | window | hud | popup
    bool pauses = true;
};

class DocumentUi {
public:
    using ImageLoader = std::function<std::optional<ImageData>(const std::string& path)>;
    // `projectRoot`'s ui/ wins over the engine's data/ui. `images` loads a picture a
    // document names (project-relative or under data/ui).
    DocumentUi(std::filesystem::path projectRoot, ImageLoader images = {});
    void SetImageLoader(ImageLoader images);
    ~DocumentUi();
    DocumentUi(const DocumentUi&) = delete;
    DocumentUi& operator=(const DocumentUi&) = delete;

    bool Ready() const;
    void SetViewport(int width, int height);
    // Re-reads the manifest, theme and documents (after editing them).
    void Reload();

    // The theme (ui/theme.json): {"base": "stone", "tokens": {"panel": "#1d1d2b", ...}}.
    // Styles say var(--panel); the tokens are put in when a style is read.
    nlohmann::json ThemeTokens() const;
    std::vector<std::string> Themes() const; // the built-in themes' names

    // What's on screen. `screen` names the menu (title, pause, inventory...); nullopt hides it.
    void SetMenu(const std::optional<MenuView>& view, const std::string& screen);
    void SetDialogue(const std::optional<DialogueView>& view);
    void SetHud(const HudData& hud);
    void Notify(Notification note);
    const std::vector<Notification>& History() const;
    const std::vector<Notification>& Live() const; // showing now
    // A menu or dialogue item picked with the mouse (its index among the items).
    std::function<void(int index)> onMenuChoose;
    std::function<void(int index)> onDialogueChoose;
    // A screen of the project's own (ui/screens/<id>.rml), opened by Lua; its
    // elements call action('...') (the "ui" model), handed to `onAction`.
    void ShowScreen(const std::string& id, bool visible);
    bool IsShown(const std::string& id) const;
    std::function<void(const std::string& action)> onAction;

    // Mouse input in viewport pixels; true when the UI took it.
    bool MouseMove(int x, int y);
    bool MouseButton(int button, bool down);
    bool MouseWheel(float delta);
    // Clicks the menu's (or dialogue's) item `index` as the mouse would (tests, automation).
    bool ClickMenuItem(int index);
    bool ClickDialogueChoice(int index);
    // The text of the shown documents' elements of a class (tests: "item" in the menu).
    std::vector<std::string> TextOf(const std::string& className) const;

    void Update(double seconds);
    // Appends the documents' quads (call after the world's).
    void Render(std::vector<UiQuad>& out);
    // Textures documents made since the last call (fonts, pictures), to register
    // with the renderer under their names.
    struct NewTexture {
        std::string name;
        ImageData image;
    };
    std::vector<NewTexture> TakeNewTextures();

    // Document and style problems (parse errors, unknown properties) since the last call.
    std::vector<std::string> TakeProblems();
    const std::vector<ScreenEntry>& Screens() const;
    // Where the document for a screen comes from: "project", "engine" or "" (generic menu).
    std::string SourceOf(const std::string& screen) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Problems with a UI file a person or the AI wants to write (ui/...): where it may go,
// JSON that parses and has the expected shape (theme, screens, notifications), and a
// document's models (menu, dialogue, hud, notes, ui; not on <body>). Empty when fine.
std::vector<std::string> CheckUiFile(const std::string& path, const std::string& content);

// Puts a theme's tokens into a style: var(--name) -> its value (unknown names stay).
std::string ApplyThemeTokens(const std::string& style, const nlohmann::json& tokens);
// The engine's UI documents and themes (data/ui).
std::filesystem::path EngineUiDir();

// Turns a triangle mesh into axis-aligned quads (RmlUi's boxes, borders, glyphs and
// images are rectangles), clipped to `clip` when given. Exposed for tests.
struct UiMeshVertex {
    float x, y, u, v;
    float r, g, b, a; // straight alpha
};
void MeshToQuads(const std::vector<UiMeshVertex>& vertices, const std::vector<int>& indices, float dx, float dy,
                 const std::string& texture, const float* clip /* x0, y0, x1, y1 or null */, std::vector<UiQuad>& out);

} // namespace aige
