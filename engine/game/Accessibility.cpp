#include "game/Accessibility.h"

#include "gameplay/InputMap.h"
#include "platform/Input.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace aige {

using json = nlohmann::json;

namespace {

struct Rgba {
    double r = 0, g = 0, b = 0, a = 1;
};

Rgba Parse(const std::string& color) {
    Rgba c;
    std::string hex = color;
    if (!hex.empty() && hex[0] == '#') hex = hex.substr(1);
    auto byte = [&](size_t at) { return at + 2 <= hex.size() ? std::stoi(hex.substr(at, 2), nullptr, 16) / 255.0 : 1.0; };
    if (hex.size() >= 6) {
        try {
            c.r = byte(0);
            c.g = byte(2);
            c.b = byte(4);
            c.a = hex.size() >= 8 ? byte(6) : 1.0;
        } catch (...) {
        }
    }
    return c;
}

double Linear(double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); }
double Gamma(double v) {
    v = std::clamp(v, 0.0, 1.0);
    return v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}

std::string Hex(double r, double g, double b) {
    char out[8];
    std::snprintf(out, sizeof(out), "#%02x%02x%02x", static_cast<int>(std::lround(std::clamp(r, 0.0, 1.0) * 255)),
                  static_cast<int>(std::lround(std::clamp(g, 0.0, 1.0) * 255)), static_cast<int>(std::lround(std::clamp(b, 0.0, 1.0) * 255)));
    return out;
}

std::array<double, 3> Lab(const std::string& color) {
    const Rgba c = Parse(color);
    const double r = Linear(c.r), g = Linear(c.g), b = Linear(c.b);
    double x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047;
    double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    double z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883;
    auto f = [](double t) { return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116.0; };
    x = f(x);
    y = f(y);
    z = f(z);
    return {116.0 * y - 16.0, 500.0 * (x - y), 200.0 * (y - z)};
}

std::string Token(const json& tokens, const char* name, const char* fallback) {
    return tokens.contains(name) && tokens[name].is_string() ? tokens[name].get<std::string>() : fallback;
}

} // namespace

double RelativeLuminance(const std::string& color) {
    const Rgba c = Parse(color);
    // Blended over black (a translucent panel over a dark scene: the worst case for light text).
    const double r = c.r * c.a, g = c.g * c.a, b = c.b * c.a;
    return 0.2126 * Linear(r) + 0.7152 * Linear(g) + 0.0722 * Linear(b);
}

double ContrastRatio(const std::string& foreground, const std::string& background) {
    const double a = RelativeLuminance(foreground), b = RelativeLuminance(background);
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

std::string SimulateColorBlindness(const std::string& color, const std::string& kind) {
    // Machado, Oliveira & Fernandes (2009), severity 1.0, on linear RGB.
    static const double protan[9] = {0.152286, 1.052583, -0.204868, 0.114503, 0.786281, 0.099216, -0.003882, -0.048116, 1.051998};
    static const double deutan[9] = {0.367322, 0.860646, -0.227968, 0.280085, 0.672501, 0.047413, -0.011820, 0.042940, 0.968881};
    static const double tritan[9] = {1.255528, -0.076749, -0.178779, -0.078411, 0.930809, 0.147602, 0.004733, 0.691367, 0.303900};
    const double* m = kind == "protanopia" ? protan : kind == "deuteranopia" ? deutan : kind == "tritanopia" ? tritan : nullptr;
    const Rgba c = Parse(color);
    if (!m) return Hex(c.r, c.g, c.b);
    const double r = Linear(c.r), g = Linear(c.g), b = Linear(c.b);
    return Hex(Gamma(m[0] * r + m[1] * g + m[2] * b), Gamma(m[3] * r + m[4] * g + m[5] * b), Gamma(m[6] * r + m[7] * g + m[8] * b));
}

double ColorDistance(const std::string& a, const std::string& b) {
    const auto x = Lab(a), y = Lab(b);
    return std::sqrt((x[0] - y[0]) * (x[0] - y[0]) + (x[1] - y[1]) * (x[1] - y[1]) + (x[2] - y[2]) * (x[2] - y[2]));
}

json AccessibilityTokens(const PlayerSettings& player) {
    json t = json::object();
    if (player.highContrast) {
        t["text"] = "#ffffff";
        t["text-dim"] = "#e6e6e6";
        t["panel"] = "#000000f5";
        t["panel-border"] = "#ffffff";
        t["accent"] = "#ffd400";
        t["highlight"] = "#ffd400";
        t["shade"] = "#000000cc";
    }
    // Okabe-Ito: told apart with any colour vision.
    if (player.colorblind == "protanopia" || player.colorblind == "deuteranopia") {
        t["health"] = "#e69f00";
        t["mana"] = "#0072b2";
        t["stamina"] = "#f0e442";
        t["xp"] = "#cc79a7";
        t["danger"] = "#d55e00";
    } else if (player.colorblind == "tritanopia") {
        t["health"] = "#d55e00";
        t["mana"] = "#009e73";
        t["stamina"] = "#cc79a7";
        t["xp"] = "#e69f00";
        t["danger"] = "#d55e00";
    }
    if (player.textScale > 1.01f) {
        char size[16];
        std::snprintf(size, sizeof(size), "%dpx", static_cast<int>(std::lround(17.0f * player.textScale)));
        t["font-size"] = size;
    }
    return t;
}

json AccessibilityReport(const json& tokens, const InputMap& input) {
    json checks = json::array();
    int passed = 0;
    auto add = [&](const std::string& id, bool ok, const std::string& severity, const std::string& message) {
        passed += ok;
        checks.push_back({{"id", id}, {"ok", ok}, {"severity", severity}, {"message", message}});
    };
    char line[200];
    const std::string text = Token(tokens, "text", "#eeeef2"), dim = Token(tokens, "text-dim", "#eeeef28c"), panel = Token(tokens, "panel", "#1b1b24e6");
    const double contrast = ContrastRatio(text, panel);
    std::snprintf(line, sizeof(line), "Text on panels has a contrast of %.1f:1 (at least 4.5:1, WCAG AA).", contrast);
    add("text-contrast", contrast >= 4.5, "error", line);
    const double dimContrast = ContrastRatio(dim, panel);
    std::snprintf(line, sizeof(line), "Dim text on panels has a contrast of %.1f:1 (at least 3:1 for secondary text).", dimContrast);
    add("dim-contrast", dimContrast >= 3.0, "warning", line);
    const std::string size = Token(tokens, "font-size", "17px");
    const int px = std::atoi(size.c_str());
    std::snprintf(line, sizeof(line), "Body text is %dpx (at least 16px at 1x; players can scale the UI and text).", px);
    add("text-size", px >= 16, px >= 12 ? "warning" : "error", line);
    // Bars told apart without colour vision.
    const std::string health = Token(tokens, "health", "#d64545"), mana = Token(tokens, "mana", "#4a7bd6"), stamina = Token(tokens, "stamina", "#4caf6a");
    double worst = 1e9;
    std::string worstKind;
    for (const char* kind : {"protanopia", "deuteranopia", "tritanopia"}) {
        for (const auto& [a, b] : std::vector<std::pair<std::string, std::string>>{{health, mana}, {health, stamina}, {mana, stamina}}) {
            const double d = ColorDistance(SimulateColorBlindness(a, kind), SimulateColorBlindness(b, kind));
            if (d < worst) {
                worst = d;
                worstKind = kind;
            }
        }
    }
    std::snprintf(line, sizeof(line), "Health, mana and stamina bars stay %.0f apart (ΔE) even with %s (at least 20; the colour-blind palettes fix it).",
                  worst, worstKind.c_str());
    add("bars-colorblind", worst >= 20.0, "warning", line);
    // Every action on keyboard and gamepad, and rebindable (the controls window).
    std::vector<std::string> keyboardless, padless;
    for (const auto& a : input.actions) {
        if (a.name.rfind("debug", 0) == 0) continue;
        bool keyboard = false, pad = false;
        for (const auto& k : a.keys) {
            const std::string name = KeyName(k.key);
            if (name.rfind("Pad", 0) == 0) pad = true;
            else if (name.rfind("Mouse", 0) != 0 && k.key != Key::Unknown) keyboard = true;
        }
        if (a.axis && !a.axes.empty()) pad = true;
        if (!keyboard) keyboardless.push_back(a.name);
        if (!pad) padless.push_back(a.name);
    }
    auto list = [](const std::vector<std::string>& v) {
        std::string s;
        for (size_t i = 0; i < v.size() && i < 6; ++i) s += (s.empty() ? "" : ", ") + v[i];
        return s;
    };
    add("keyboard", keyboardless.empty(), "error",
        keyboardless.empty() ? "Every action has a keyboard key." : "No keyboard key for: " + list(keyboardless) + ".");
    add("gamepad", padless.empty(), "warning", padless.empty() ? "Every action has a gamepad button." : "No gamepad button for: " + list(padless) + ".");
    add("remap", true, "error", "Players can rebind every action (Settings ▸ Controls).");
    // What the engine always offers.
    add("subtitles", true, "error", "Dialogue is always written on screen, on a panel; subtitles are on by default and follow the text size.");
    add("motion", true, "error", "Reduce motion turns off screen shake, hit-stop and slow motion; reduce flashes turns off flashes.");
    add("ui-scale", true, "warning", "The UI scales as a whole (Settings), and its text on its own (Accessibility).");
    add("assists", true, "warning", "Assists apart from difficulty: invincibility, damage reduction, game speed.");
    add("hold-toggle", true, "warning", "Hold-to-toggle: actions that need holding can be toggled instead.");
    return {{"schema", "aige.accessibility/1"}, {"passed", passed}, {"total", checks.size()}, {"checks", checks}};
}

} // namespace aige
