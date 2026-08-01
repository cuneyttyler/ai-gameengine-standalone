#include "assets/SceneIO.h"

#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "scripting/Components.h"

#include <entt/entt.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace aige {

namespace {

std::string Trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

using FieldMap = std::unordered_map<std::string, std::string>;

FieldMap ParseFields(const std::string& rest) {
    FieldMap fields;
    std::istringstream iss(rest);
    std::string token;
    while (iss >> token) {
        size_t eq = token.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        fields[token.substr(0, eq)] = token.substr(eq + 1);
    }
    return fields;
}

float GetFloat(const FieldMap& fields, const char* key, float fallback = 0.0f) {
    auto it = fields.find(key);
    return it != fields.end() ? std::stof(it->second) : fallback;
}

} // namespace

void SaveScene(Registry& registry, const std::string& path) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Failed to open scene file for writing: " + path);
    }
    out.precision(9); // enough significant digits to round-trip an IEEE 754 float exactly
    out << "# AIGE Scene v1\n";

    auto view = registry.view<entt::entity>();
    for (auto entity : view) {
        out << "\nentity\n";

        if (auto* c = registry.try_get<WorldTransform2D>(entity)) {
            out << "  WorldTransform2D x=" << c->x << " y=" << c->y << "\n";
        }
        if (auto* c = registry.try_get<Velocity2D>(entity)) {
            out << "  Velocity2D x=" << c->x << " y=" << c->y << "\n";
        }
        if (auto* c = registry.try_get<Collider2D>(entity)) {
            out << "  Collider2D halfWidth=" << c->halfWidth << " halfHeight=" << c->halfHeight << "\n";
        }
        if (auto* c = registry.try_get<ScreenTransform2D>(entity)) {
            out << "  ScreenTransform2D x=" << c->x << " y=" << c->y << " depth=" << c->depth << "\n";
        }
        if (auto* c = registry.try_get<Sprite>(entity)) {
            out << "  Sprite width=" << c->width << " height=" << c->height << " u0=" << c->u0 << " v0=" << c->v0
                << " u1=" << c->u1 << " v1=" << c->v1 << " r=" << c->r << " g=" << c->g << " b=" << c->b
                << " a=" << c->a << "\n";
        }
        if (auto* c = registry.try_get<LuaScript>(entity)) {
            out << "  LuaScript path=" << c->path << "\n";
        }
    }
}

bool LoadScene(Registry& registry, const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }

    registry.clear();

    std::string line;
    Entity current = entt::null;
    while (std::getline(in, line)) {
        std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        if (trimmed == "entity") {
            current = registry.create();
            continue;
        }
        if (current == entt::null) {
            continue; // content before the first "entity" line; ignore
        }

        std::istringstream iss(trimmed);
        std::string componentName;
        iss >> componentName;
        std::string rest;
        std::getline(iss, rest);

        if (componentName == "WorldTransform2D") {
            auto fields = ParseFields(rest);
            registry.emplace<WorldTransform2D>(current, GetFloat(fields, "x"), GetFloat(fields, "y"));
        } else if (componentName == "Velocity2D") {
            auto fields = ParseFields(rest);
            registry.emplace<Velocity2D>(current, GetFloat(fields, "x"), GetFloat(fields, "y"));
        } else if (componentName == "Collider2D") {
            auto fields = ParseFields(rest);
            registry.emplace<Collider2D>(current, GetFloat(fields, "halfWidth", 0.3f),
                                          GetFloat(fields, "halfHeight", 0.3f));
        } else if (componentName == "ScreenTransform2D") {
            auto fields = ParseFields(rest);
            registry.emplace<ScreenTransform2D>(current, GetFloat(fields, "x"), GetFloat(fields, "y"),
                                                 GetFloat(fields, "depth"));
        } else if (componentName == "Sprite") {
            auto fields = ParseFields(rest);
            registry.emplace<Sprite>(current, GetFloat(fields, "width", 32.0f), GetFloat(fields, "height", 32.0f),
                                      GetFloat(fields, "u0"), GetFloat(fields, "v0"), GetFloat(fields, "u1", 1.0f),
                                      GetFloat(fields, "v1", 1.0f), GetFloat(fields, "r", 1.0f),
                                      GetFloat(fields, "g", 1.0f), GetFloat(fields, "b", 1.0f),
                                      GetFloat(fields, "a", 1.0f));
        } else if (componentName == "LuaScript") {
            std::string trimmedRest = Trim(rest);
            const std::string prefix = "path=";
            std::string scriptPath =
                trimmedRest.rfind(prefix, 0) == 0 ? trimmedRest.substr(prefix.size()) : "";
            registry.emplace<LuaScript>(current).path = scriptPath;
        }
        // Unknown component names are skipped rather than failing the load —
        // forward/backward compatible with engine versions that know about
        // more or fewer component types.
    }
    return true;
}

} // namespace aige
