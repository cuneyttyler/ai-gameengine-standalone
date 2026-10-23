#pragma once

#include "game/PlayerSettings.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace aige {

struct InputMap;

// Accessibility (development_plan.md 24): what the player's options change in the UI's theme
// (high contrast, colour-blind-safe palettes), the colour maths behind them, and the
// accessibility report an export carries (accessibility.json) and the editor shows.

// sRGB "#rrggbb[aa]" -> linear-light relative luminance (WCAG 2.x), alpha blended over black.
double RelativeLuminance(const std::string& color);
// WCAG contrast ratio of two colours (1-21); the panel's alpha blends over black.
double ContrastRatio(const std::string& foreground, const std::string& background);
// How a colour looks with a kind of colour blindness (protanopia, deuteranopia,
// tritanopia), as "#rrggbb" (Machado et al. 2009, full severity).
std::string SimulateColorBlindness(const std::string& color, const std::string& kind);
// How far apart two colours are (CIE76 ΔE in Lab); under ~20 they're easily confused.
double ColorDistance(const std::string& a, const std::string& b);

// The theme tokens the player's options put over the theme's: high contrast (white on
// black, a yellow accent), a colour-blind-safe palette for bars and warnings (Okabe-Ito),
// bigger text for the subtitles' size.
nlohmann::json AccessibilityTokens(const PlayerSettings& player);

// The checklist (WCAG-inspired, for games): text contrast, dim text, text size, subtitles,
// every action bindable on keyboard and gamepad, bars told apart without colour vision,
// motion and flash options, UI scale, assists. {passed, total, checks [{id, ok, severity,
// message}]}.
nlohmann::json AccessibilityReport(const nlohmann::json& themeTokens, const InputMap& input);

} // namespace aige
