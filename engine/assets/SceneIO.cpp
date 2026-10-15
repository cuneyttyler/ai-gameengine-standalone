#include "assets/SceneIO.h"

#include "gameplay/Components.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "scripting/Components.h"

#include <entt/entt.hpp>

#include <cctype>
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

// `key=value` pairs separated by whitespace. A value may be double-quoted
// (with \" and \\ escapes) to contain spaces — see Quote().
FieldMap ParseFields(const std::string& rest) {
    FieldMap fields;
    size_t i = 0;
    const size_t n = rest.size();
    while (i < n) {
        while (i < n && std::isspace(static_cast<unsigned char>(rest[i]))) {
            ++i;
        }
        size_t keyStart = i;
        while (i < n && rest[i] != '=' && !std::isspace(static_cast<unsigned char>(rest[i]))) {
            ++i;
        }
        if (i >= n || rest[i] != '=') {
            continue; // a bare word without '=': skip it
        }
        std::string key = rest.substr(keyStart, i - keyStart);
        ++i; // '='
        std::string value;
        if (i < n && rest[i] == '"') {
            ++i;
            while (i < n && rest[i] != '"') {
                if (rest[i] == '\\' && i + 1 < n) {
                    ++i;
                }
                value += rest[i++];
            }
            ++i; // closing quote
        } else {
            while (i < n && !std::isspace(static_cast<unsigned char>(rest[i]))) {
                value += rest[i++];
            }
        }
        fields[key] = value;
    }
    return fields;
}

// Quotes a string value when it would otherwise break whitespace splitting.
std::string Quote(const std::string& value) {
    bool needsQuotes = value.empty() || value.find_first_of(" \t\"\\") != std::string::npos;
    if (!needsQuotes) {
        return value;
    }
    std::string quoted = "\"";
    for (char c : value) {
        if (c == '"' || c == '\\') {
            quoted += '\\';
        }
        quoted += c;
    }
    return quoted + "\"";
}

std::string GetString(const FieldMap& fields, const char* key) {
    auto it = fields.find(key);
    return it != fields.end() ? it->second : std::string();
}

float GetFloat(const FieldMap& fields, const char* key, float fallback = 0.0f) {
    auto it = fields.find(key);
    return it != fields.end() ? std::stof(it->second) : fallback;
}

void WriteScene(Registry& registry, std::ostream& out) {
    out.precision(9); // enough significant digits to round-trip an IEEE 754 float exactly
    out << "# AIGE Scene v1\n";

    for (auto entity : EntitiesInOrder(registry)) {
        out << "\nentity\n";

        if (auto* c = registry.try_get<Tile>(entity)) {
            out << "  Tile gx=" << c->gx << " gy=" << c->gy << " solid=" << (c->solid ? 1 : 0) << "\n";
        }
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
                << " a=" << c->a;
            if (!c->texture.empty()) {
                out << " texture=" << Quote(c->texture);
            }
            out << "\n";
        }
        if (auto* c = registry.try_get<LuaScript>(entity)) {
            out << "  LuaScript path=" << Quote(c->path) << "\n";
        }
    }
}

void ReadScene(Registry& registry, std::istream& in) {
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

        if (componentName == "Tile") {
            auto fields = ParseFields(rest);
            registry.emplace<Tile>(current, static_cast<int>(GetFloat(fields, "gx")),
                                   static_cast<int>(GetFloat(fields, "gy")), GetFloat(fields, "solid") != 0.0f);
        } else if (componentName == "WorldTransform2D") {
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
            registry.get<Sprite>(current).texture = GetString(fields, "texture");
        } else if (componentName == "LuaScript") {
            std::string trimmedRest = Trim(rest);
            const std::string prefix = "path=";
            // Quoted since 1.3; older files hold the raw rest-of-line, spaces and all.
            std::string scriptPath;
            if (trimmedRest.rfind(prefix + "\"", 0) == 0) {
                scriptPath = GetString(ParseFields(trimmedRest), "path");
            } else if (trimmedRest.rfind(prefix, 0) == 0) {
                scriptPath = trimmedRest.substr(prefix.size());
            }
            registry.emplace<LuaScript>(current).path = scriptPath;
        }
        // Unknown component names are skipped rather than failing the load —
        // forward/backward compatible with engine versions that know about
        // more or fewer component types.
    }
}

} // namespace

void SaveScene(Registry& registry, const std::string& path) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Failed to open scene file for writing: " + path);
    }
    WriteScene(registry, out);
}

bool LoadScene(Registry& registry, const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    ReadScene(registry, in);
    return true;
}

std::string SerializeScene(Registry& registry) {
    std::ostringstream out;
    WriteScene(registry, out);
    return out.str();
}

void DeserializeScene(Registry& registry, const std::string& text) {
    std::istringstream in(text);
    ReadScene(registry, in);
}

} // namespace aige
