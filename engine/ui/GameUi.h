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

// A short prompt above the dialogue area ("E  Talk to Old Tamsin").
void AddPrompt(std::vector<UiQuad>& out, const std::string& key, const std::string& text, float viewportWidth,
               float viewportHeight);

} // namespace aige
