#pragma once

#include "renderer2d/Text.h"

#include <string>
#include <vector>

namespace aige {

class Renderer2D;

// The game's own UI (development_plan.md 3.2a), drawn by the engine in both
// the editor's Play mode and a shipped game — not by the editor's web UI.
// Each function appends quads for Renderer2D::Render's `ui` layer; layout is
// CPU-only (testable), positions are viewport pixels.

// Uploads the font atlases these functions use. Call once per renderer.
void RegisterGameUiFonts(Renderer2D& renderer);

// A centered title card ("Victory!", "You died").
void AddBanner(std::vector<UiQuad>& out, const std::string& title, const std::string& subtitle, float viewportWidth,
               float viewportHeight);

struct DialogueView {
    std::string speaker;
    std::string text;
    std::vector<std::string> choices; // empty: a line to continue past
    int highlighted = 0;
    std::string portrait; // an image above the box's left edge (15.3); empty: none
};

struct DialogueBoxLayout {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // the box
    std::vector<std::string> textLines;    // the wrapped text
    std::vector<float> choiceTops;         // top of each choice's first line
};

// The dialogue box along the bottom of the screen: speaker, wrapped text,
// numbered choices (the highlighted one marked), and a hint line.
DialogueBoxLayout AddDialogueBox(std::vector<UiQuad>& out, const DialogueView& view, float viewportWidth,
                                 float viewportHeight);

// The current quest objective, top-left ("Clear the courtyard — Skeletons 2/4").
void AddObjective(std::vector<UiQuad>& out, const std::string& title, const std::string& line, float viewportWidth,
                  float viewportHeight);

// The game clock, top right ("Day 2  14:30").
void AddClock(std::vector<UiQuad>& out, const std::string& text, float viewportWidth, float viewportHeight);

// A brief notice at the top center ("New quest: Clear the courtyard").
void AddToast(std::vector<UiQuad>& out, const std::string& text, float viewportWidth, float viewportHeight);

// A full-screen menu (development_plan.md 12.4): a title, items to pick with the
// highlighted one marked, and an optional body text and footer. The built-in
// look until the game UI system (Phase 17) replaces it with documents.
struct MenuItemView {
    std::string label;
    bool enabled = true;
};
struct MenuView {
    std::string title;
    std::string subtitle;
    std::string body; // wrapped (credits, a save's details)
    std::vector<MenuItemView> items;
    int highlighted = 0;
    std::string footer;
    bool dimBackground = true; // the game shows through, darkened
};
void AddMenu(std::vector<UiQuad>& out, const MenuView& view, float viewportWidth, float viewportHeight);

// A spoken line along the bottom (a character's remark, a subtitle).
void AddSubtitle(std::vector<UiQuad>& out, const std::string& text, float viewportWidth, float viewportHeight);

// Plain lines at the top right, for debug overlays (the AI's, F4).
void AddDebugLines(std::vector<UiQuad>& out, const std::vector<std::string>& lines, float viewportWidth,
                   float viewportHeight);

// Black over everything at `alpha` (scene transitions).
void AddScreenFade(std::vector<UiQuad>& out, float alpha, float viewportWidth, float viewportHeight);

// Cutscenes (15.5): black bars top and bottom (`amount` 0-1 slides them in) and a
// title card in the middle ("Chapter One"), faded by `alpha`.
void AddLetterbox(std::vector<UiQuad>& out, float amount, float viewportWidth, float viewportHeight);
void AddTitleCard(std::vector<UiQuad>& out, const std::string& text, float alpha, float viewportWidth,
                  float viewportHeight);

// The objective marker (15.4): a pip on a ring around the middle of the screen, the
// way the objective lies (screen direction), with a label ("12 m").
void AddObjectiveMarker(std::vector<UiQuad>& out, float dirX, float dirY, const std::string& label,
                        float viewportWidth, float viewportHeight);

// A short prompt above the dialogue area ("E  Talk to Old Tamsin").
void AddPrompt(std::vector<UiQuad>& out, const std::string& key, const std::string& text, float viewportWidth,
               float viewportHeight);

} // namespace aige
