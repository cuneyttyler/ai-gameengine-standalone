#include "assets/SceneIO.h"

#include "gameplay/ComponentTable.h"
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
// (with \" and \\ escapes) to contain spaces — see ComponentTable's
// FormatField.
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

// Format: a header comment, then one block per entity — an `entity` line
// followed by one indented line per component, `Name field=value ...`.
// Which components exist and how their fields are written comes entirely
// from the component table.
void WriteScene(Registry& registry, std::ostream& out, const std::vector<Entity>& entities) {
    out << "# AIGE Scene v1\n";

    std::unordered_map<Entity, size_t> indexOf;
    for (size_t i = 0; i < entities.size(); ++i) {
        indexOf[entities[i]] = i;
    }
    auto entityIndex = [&](Entity entity) -> std::optional<size_t> {
        auto it = indexOf.find(entity);
        return it == indexOf.end() ? std::nullopt : std::optional<size_t>(it->second);
    };

    for (auto entity : entities) {
        out << "\nentity\n";
        for (const auto& component : Components()) {
            if (!component.has(registry, entity)) {
                continue;
            }
            out << "  " << component.name;
            for (const auto& field : component.fields) {
                out << " " << field.name << "=" << FormatField(field, field.get(registry, entity), entityIndex);
            }
            if (component.writeExtra) {
                for (const auto& [key, text] : component.writeExtra(registry, entity)) {
                    out << " " << key << "=" << text;
                }
            }
            out << "\n";
        }
    }
}

std::vector<Entity> ReadScene(Registry& registry, std::istream& in) {

    // Entity references point at entities later in the file too, so they're
    // resolved after everything is created.
    struct PendingRef {
        Entity entity;
        const FieldDesc* field;
        size_t index;
    };
    std::vector<PendingRef> pendingRefs;
    std::vector<Entity> created;

    std::string line;
    Entity current = entt::null;
    while (std::getline(in, line)) {
        std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        if (trimmed == "entity") {
            current = registry.create();
            created.push_back(current);
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

        // Unknown component names are skipped rather than failing the load —
        // forward/backward compatible with engine versions that know about
        // more or fewer component types.
        const ComponentDesc* component = FindComponent(componentName);
        if (component == nullptr) {
            continue;
        }
        if (!component->has(registry, current)) {
            component->add(registry, current);
        }

        // Before 1.3 a LuaScript path was written unquoted as the rest of the
        // line, spaces and all. Only that exact shape (an unquoted path with
        // spaces and nothing after it) takes this route; anything else,
        // including paths followed by script properties, parses normally.
        std::string trimmedRest = Trim(rest);
        if (componentName == "LuaScript" && trimmedRest.rfind("path=", 0) == 0 &&
            trimmedRest.rfind("path=\"", 0) != 0) {
            std::string legacyPath = trimmedRest.substr(5);
            if (legacyPath.find(' ') != std::string::npos && legacyPath.find('=') == std::string::npos) {
                registry.get<LuaScript>(current).path = legacyPath;
                continue;
            }
        }

        for (const auto& [key, text] : ParseFields(rest)) {
            const FieldDesc* field = component->Field(key);
            if (field == nullptr) {
                if (component->readExtra) {
                    component->readExtra(registry, current, key, text);
                }
                continue;
            }
            std::optional<size_t> refIndex;
            auto value = ParseField(*field, text, &refIndex);
            if (refIndex) {
                pendingRefs.push_back({current, field, *refIndex});
            } else if (value) {
                field->set(registry, current, *value);
            }
        }
    }

    for (const auto& ref : pendingRefs) {
        if (ref.index < created.size()) {
            ref.field->set(registry, ref.entity, created[ref.index]);
        }
    }
    return created;
}

} // namespace

void SaveScene(Registry& registry, const std::string& path) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Failed to open scene file for writing: " + path);
    }
    WriteScene(registry, out, EntitiesInOrder(registry));
}

bool LoadScene(Registry& registry, const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    registry.clear();
    ReadScene(registry, in);
    return true;
}

std::string SerializeScene(Registry& registry) {
    return SerializeEntities(registry, EntitiesInOrder(registry));
}

void DeserializeScene(Registry& registry, const std::string& text) {
    registry.clear();
    std::istringstream in(text);
    ReadScene(registry, in);
}

std::string SerializeEntities(Registry& registry, const std::vector<Entity>& entities) {
    std::ostringstream out;
    WriteScene(registry, out, entities);
    return out.str();
}

std::vector<Entity> AppendEntities(Registry& registry, const std::string& text) {
    std::istringstream in(text);
    return ReadScene(registry, in);
}

} // namespace aige
