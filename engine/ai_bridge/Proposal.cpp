#include "ai_bridge/Proposal.h"

#include "assets/Prefab.h"
#include "editing/Edits.h"
#include "assets/Project.h"
#include "gameplay/Components.h"
#include "reflection/Reflection.h"
#include "scripting/Components.h"
#include "editing/LevelBuild.h"
#include "gameplay/Schedule.h"
#include "gameplay/Story.h"
#include "world/WorldBible.h"

#include <map>
#include <set>

namespace aige {

namespace {

enum class Mode { Resolve, Apply };

// What the whole proposal adds, so one operation may refer to another's
// result: a dialogue that starts a quest written in the same proposal, or
// speaks for a character it adds to the World Bible.
struct ProposalScope {
    std::map<std::string, Quest> quests; // write_quest, by id
    std::set<std::string> worldKeys;     // world.write, "<kind>/<id>"
    std::set<std::string> markers;       // scene.addMarker, by name
};

ProposalScope ScanProposal(const std::vector<nlohmann::json>& operations) {
    ProposalScope scope;
    for (const auto& op : operations) {
        const std::string kind = op.value("op", "");
        if (kind == "write_quest") {
            std::string error;
            if (auto quest = QuestFromJson(op.value("quest", nlohmann::json()), op.value("id", ""), &error)) {
                scope.quests[quest->id] = *quest;
            }
        } else if (kind == "command" && op.value("name", "") == "scene.addMarker") {
            scope.markers.insert(op.value("args", nlohmann::json::object()).value("name", ""));
        } else if (kind == "command" && op.value("name", "") == "world.write") {
            const nlohmann::json args = op.value("args", nlohmann::json::object());
            std::string id = args.value("id", "");
            if (id.empty()) id = WorldIdFromName(args.value("name", ""));
            scope.worldKeys.insert(args.value("kind", "") + "/" + id);
        }
    }
    return scope;
}

// Problems with a World Bible reference ("character/x"); plain names are fine.
bool KnownCharacter(const std::string& who, const WorldBible& world, const ProposalScope& scope) {
    return who.empty() || who == "player" || who.find('/') == std::string::npos || world.Find(who) != nullptr ||
           scope.worldKeys.contains(who);
}

std::string JoinProblems(const std::vector<std::string>& problems) {
    std::string text;
    for (size_t i = 0; i < problems.size() && i < 4; ++i) {
        text += (i ? "; " : "") + problems[i];
    }
    if (problems.size() > 4) text += "; and " + std::to_string(problems.size() - 4) + " more";
    return text;
}

nlohmann::json MetaJson(const FieldMeta& meta) {
    nlohmann::json json = {{"name", meta.name}, {"type", FieldTypeName(meta.type)}};
    if (!meta.options.empty()) json["options"] = meta.options;
    if (meta.min) json["min"] = *meta.min;
    if (meta.max) json["max"] = *meta.max;
    return json;
}

std::string EntityLabel(Registry& registry, Entity entity) {
    std::string id = "#" + std::to_string(static_cast<uint32_t>(entity));
    if (const auto* name = registry.try_get<Name>(entity); name != nullptr && !name->value.empty()) {
        return name->value + " " + id;
    }
    return "Entity " + id;
}

std::optional<Entity> ReadEntity(const nlohmann::json& op, Registry& registry, std::string& error) {
    if (!op.contains("entity") || !op["entity"].is_number_integer()) {
        error = "missing entity";
        return std::nullopt;
    }
    Entity entity = static_cast<Entity>(op["entity"].get<uint32_t>());
    if (!registry.valid(entity)) {
        error = "entity #" + std::to_string(op["entity"].get<uint32_t>()) + " no longer exists";
        return std::nullopt;
    }
    return entity;
}

// Converts and validates a JSON value for a field; sets `error` on failure.
std::optional<FieldValue> ReadValue(const FieldMeta& meta, const nlohmann::json& value, Registry& registry,
                                    std::string& error) {
    auto converted = ValueFromJson(meta, value);
    if (!converted) {
        error = meta.name + " can't be " + value.dump();
        return std::nullopt;
    }
    if (auto problem = Validate(meta, *converted)) {
        error = *problem;
        return std::nullopt;
    }
    if (meta.type == FieldType::EntityRef) {
        Entity target = ToEntity(*converted);
        if (target != entt::null && !registry.valid(target)) {
            error = meta.name + " refers to an entity that no longer exists";
            return std::nullopt;
        }
    }
    return converted;
}

std::optional<ScriptValue> ReadScriptValue(const nlohmann::json& value) {
    if (value.is_boolean()) return ScriptValue(value.get<bool>());
    if (value.is_number()) return ScriptValue(value.get<double>());
    if (value.is_string()) return ScriptValue(value.get<std::string>());
    return std::nullopt;
}

// Resolves one operation (and, in Apply mode, performs it). Everything about
// an operation — what it targets, validation, before/after — lives here once,
// so review and apply can't disagree.
ChangeItem Process(const nlohmann::json& op, Mode mode, Registry& registry, const ReflectionRegistry& reflection,
                   PrefabLibrary& prefabs, const Project& project, const ProposalScope& scope, ApplyOutcome* outcome) {
    ChangeItem item;
    item.op = op;
    const std::string kind = op.value("op", "");
    std::string& error = item.error;
    const bool apply = mode == Mode::Apply;

    if (kind == "set_field") {
        item.kind = "field";
        const std::string componentName = op.value("component", "");
        const std::string fieldName = op.value("field", "");
        auto entity = ReadEntity(op, registry, error);
        if (!entity) return item;
        item.target = EntityLabel(registry, *entity) + " · " + componentName + "." + fieldName;
        const ComponentInfo* component = reflection.FindComponent(componentName);
        const FieldInfo* field = component ? component->Field(fieldName) : nullptr;
        if (field == nullptr) {
            error = "unknown field " + componentName + "." + fieldName;
            return item;
        }
        item.field = MetaJson(*field);
        if (!component->has(registry, *entity)) {
            error = "has no " + componentName;
            return item;
        }
        item.before = ValueToJson(*field, field->get(registry, *entity));
        auto value = ReadValue(*field, op.value("value", nlohmann::json()), registry, error);
        if (!value) return item;
        item.after = ValueToJson(*field, *value);
        if (apply) {
            if (auto problem = EditField(reflection, registry, *entity, componentName, fieldName, op["value"])) {
                error = *problem;
            }
        }

    } else if (kind == "add_component") {
        item.kind = "add_component";
        const std::string componentName = op.value("component", "");
        auto entity = ReadEntity(op, registry, error);
        if (!entity) return item;
        item.target = EntityLabel(registry, *entity) + " · + " + componentName;
        const ComponentInfo* component = reflection.FindComponent(componentName);
        if (component == nullptr) {
            error = "unknown component " + componentName;
            return item;
        }
        if (!component->userEditable) {
            error = componentName + " can't be added by hand";
            return item;
        }
        if (component->has(registry, *entity)) {
            error = "already has " + componentName;
            return item;
        }
        nlohmann::json fields = op.value("fields", nlohmann::json::object());
        std::vector<std::pair<const FieldInfo*, FieldValue>> values;
        for (auto it = fields.begin(); it != fields.end(); ++it) {
            const FieldInfo* field = component->Field(it.key());
            if (field == nullptr) {
                error = componentName + " has no field " + it.key();
                return item;
            }
            auto value = ReadValue(*field, it.value(), registry, error);
            if (!value) return item;
            values.emplace_back(field, *value);
        }
        item.after = fields;
        if (apply) {
            if (auto problem = EditAddComponent(reflection, registry, &prefabs, *entity, componentName, fields)) {
                error = *problem;
            }
        }

    } else if (kind == "remove_component") {
        item.kind = "remove_component";
        const std::string componentName = op.value("component", "");
        auto entity = ReadEntity(op, registry, error);
        if (!entity) return item;
        item.target = EntityLabel(registry, *entity) + " · − " + componentName;
        const ComponentInfo* component = reflection.FindComponent(componentName);
        if (component == nullptr || !component->has(registry, *entity)) {
            error = "has no " + componentName;
            return item;
        }
        if (!component->userEditable) {
            error = componentName + " can't be removed by hand";
            return item;
        }
        if (apply) {
            if (auto problem = EditRemoveComponent(reflection, registry, *entity, componentName)) {
                error = *problem;
            }
        }

    } else if (kind == "set_prefab_field") {
        item.kind = "prefab_field";
        item.touchesFiles = true;
        const std::string path = op.value("prefab", "");
        const std::string componentName = op.value("component", "");
        const std::string fieldName = op.value("field", "");
        const PrefabInfo* info = prefabs.Find(path);
        item.target = (info ? info->name : path) + " (prefab) · " + componentName + "." + fieldName;
        const ComponentInfo* component = reflection.FindComponent(componentName);
        const FieldInfo* field = component ? component->Field(fieldName) : nullptr;
        if (info == nullptr) {
            error = "no prefab " + path;
            return item;
        }
        if (field == nullptr) {
            error = "unknown field " + componentName + "." + fieldName;
            return item;
        }
        item.field = MetaJson(*field);
        bool has = false;
        prefabs.VisitTemplate(path, [&](Registry& r, Entity e) {
            has = component->has(r, e);
            if (has) {
                item.before = ValueToJson(*field, field->get(r, e));
            }
        });
        if (!has) {
            error = path + " has no " + componentName;
            return item;
        }
        auto value = ReadValue(*field, op.value("value", nlohmann::json()), registry, error);
        if (!value) return item;
        item.after = ValueToJson(*field, *value);
        if (apply) {
            if (auto problem = prefabs.SetTemplateField(path, componentName, fieldName, *value)) {
                error = *problem;
                return item;
            }
            prefabs.SyncAll(registry, path);
            if (outcome) outcome->prefabsChanged = true;
        }

    } else if (kind == "set_setting") {
        item.kind = "setting";
        item.touchesFiles = true;
        const std::string sectionName = op.value("section", "");
        const std::string fieldName = op.value("field", "");
        item.target = "Settings · " + sectionName + "." + fieldName;
        const SettingsInfo* section = reflection.FindSettings(sectionName);
        const SettingField* field = section ? section->Field(fieldName) : nullptr;
        if (field == nullptr) {
            error = "no setting " + sectionName + "." + fieldName;
            return item;
        }
        item.field = MetaJson(*field);
        item.before = ValueToJson(*field, field->get());
        auto value = ReadValue(*field, op.value("value", nlohmann::json()), registry, error);
        if (!value) return item;
        item.after = ValueToJson(*field, *value);
        if (apply) {
            if (auto problem = EditSetting(reflection, sectionName, fieldName, op["value"])) {
                error = *problem;
                return item;
            }
            if (outcome) outcome->settingsChanged = true;
        }

    } else if (kind == "set_script_property") {
        item.kind = "script_property";
        const std::string name = op.value("name", "");
        auto entity = ReadEntity(op, registry, error);
        if (!entity) return item;
        item.target = EntityLabel(registry, *entity) + " · script." + name;
        auto* script = registry.try_get<LuaScript>(*entity);
        if (script == nullptr) {
            error = "has no script";
            return item;
        }
        auto value = ReadScriptValue(op.value("value", nlohmann::json()));
        if (!value || name.empty()) {
            error = "script properties are numbers, true/false or text";
            return item;
        }
        if (auto it = script->properties.find(name); it != script->properties.end()) {
            std::visit([&](const auto& v) { item.before = v; }, it->second);
        }
        item.after = op["value"];
        if (apply) {
            if (auto problem = EditScriptProperty(registry, *entity, name, op["value"])) {
                error = *problem;
            }
        }

    } else if (kind == "write_schedule") {
        // A daily routine (development_plan.md 3.5), shown as its file.
        const std::string id = op.value("id", "");
        const std::string relative = "schedules/" + id + ".json";
        const nlohmann::json content = op.value("schedule", nlohmann::json());
        item.kind = "schedule";
        item.touchesFiles = true;
        item.target = "Schedule · " + relative;
        std::string parseError;
        auto schedule = ScheduleFromJson(content, id, &parseError);
        if (!schedule) {
            error = parseError;
            return item;
        }
        if (!schedule->name.empty()) item.target += " (" + schedule->name + ")";
        item.detail = ScheduleToJson(*schedule).dump(2);
        auto markers = MarkerPositions(registry);
        auto problems = ValidateSchedule(*schedule, [&](const std::string& name) {
            return scope.markers.contains(name) ||
                   std::any_of(markers.begin(), markers.end(), [&](const auto& m) { return m.first == name; });
        });
        if (!problems.empty()) {
            error = JoinProblems(problems);
            return item;
        }
        std::error_code ec;
        if (std::filesystem::exists(project.Resolve(relative), ec)) item.before = "(replaces the existing file)";
        item.after = content;
        if (apply) {
            if (auto problem = WriteScheduleFile(project.root, id, content)) error = *problem;
        }

    } else if (kind == "write_dialogue" || kind == "write_quest") {
        // Dialogue and quests (development_plan.md 3.2d): whole files, shown as
        // the file they'll write, checked like the Story tab checks them.
        const bool isQuest = kind == "write_quest";
        const std::string folder = isQuest ? "quests" : "dialogue";
        const std::string id = op.value("id", "");
        const std::string relative = folder + "/" + id + ".json";
        const nlohmann::json content = op.value(isQuest ? "quest" : "dialogue", nlohmann::json());
        item.kind = isQuest ? "quest" : "dialogue";
        item.touchesFiles = true;
        item.target = (isQuest ? "Quest · " : "Dialogue · ") + relative;
        StoryLibrary library(project.root);
        library.Refresh();
        QuestLookup findQuest = [&](const std::string& questId) -> const Quest* {
            auto it = scope.quests.find(questId);
            return it != scope.quests.end() ? &it->second : library.FindQuest(questId);
        };
        WorldBible world(project);
        world.Refresh();
        std::vector<std::string> problems;
        std::string parseError;
        if (isQuest) {
            auto quest = QuestFromJson(content, id, &parseError);
            if (!quest) {
                error = parseError;
                return item;
            }
            if (!quest->name.empty()) item.target += " (" + quest->name + ")";
            item.detail = QuestToJson(*quest).dump(2);
            problems = ValidateQuest(*quest, findQuest);
            if (!KnownCharacter(quest->giver, world, scope)) {
                problems.push_back("giver " + quest->giver + " isn't in the World Bible");
            }
            for (const auto& objective : quest->objectives) {
                const std::string where = "objective '" + objective.id + "'";
                if (objective.type == "talk" && world.Find(objective.target) == nullptr &&
                    !scope.worldKeys.contains(objective.target)) {
                    problems.push_back(where + ": " + objective.target + " isn't in the World Bible");
                } else if ((objective.type == "defeat" || objective.type == "collect") &&
                           prefabs.Find(objective.target) == nullptr &&
                           !(objective.type == "defeat" && (objective.target == "Enemy" || objective.target == "Neutral"))) {
                    problems.push_back(where + ": " + objective.target + " isn't a prefab");
                }
            }
        } else {
            auto dialogue = DialogueFromJson(content, relative, &parseError);
            if (!dialogue) {
                error = parseError;
                return item;
            }
            item.detail = DialogueToJson(*dialogue).dump(2);
            problems = ValidateDialogue(*dialogue, findQuest);
            if (!KnownCharacter(dialogue->speaker, world, scope)) {
                problems.push_back("speaker " + dialogue->speaker + " isn't in the World Bible");
            }
            for (const auto& node : dialogue->nodes) {
                if (!KnownCharacter(node.speaker, world, scope)) {
                    problems.push_back("node '" + node.id + "' speaker " + node.speaker + " isn't in the World Bible");
                }
            }
        }
        if (!problems.empty()) {
            error = JoinProblems(problems);
            return item;
        }
        std::error_code ec;
        if (std::filesystem::exists(project.Resolve(relative), ec)) {
            item.before = "(replaces the existing file)";
        }
        item.after = content;
        if (apply) {
            if (auto problem = WriteStoryFile(project.root, folder, id, content)) {
                error = *problem;
            }
        }

    } else if (kind == "command") {
        item.kind = "command";
        const std::string name = op.value("name", "");
        item.target = name;
        const CommandInfo* command = reflection.FindCommand(name);
        if (command == nullptr) {
            error = "unknown command " + name;
            return item;
        }
        item.touchesFiles = name == "script.write" || name == "prefab.createFromEntity" || name == "world.write" ||
                            name == "world.delete" || name == "prefab.create";
        nlohmann::json args = op.value("args", nlohmann::json::object());
        std::vector<FieldValue> values;
        for (const auto& param : command->params) {
            if (!args.contains(param.name)) {
                error = "needs " + param.name;
                return item;
            }
            auto value = ReadValue(param, args[param.name], registry, error);
            if (!value) return item;
            values.push_back(*value);
        }
        if (name == "script.write") {
            item.target = "Write " + args.value("path", std::string());
            item.detail = args.value("source", std::string());
            std::error_code ec;
            if (std::filesystem::exists(project.Resolve(args.value("path", std::string())), ec)) {
                item.before = "(replaces the existing file)";
            }
        } else if (name == "world.write") {
            // Shown as the file it will write.
            WorldEntry entry;
            entry.kind = args.value("kind", std::string());
            entry.name = args.value("name", std::string());
            entry.id = args.value("id", std::string());
            if (entry.id.empty()) entry.id = WorldIdFromName(entry.name);
            entry.summary = args.value("summary", std::string());
            entry.body = args.value("body", std::string());
            entry.tags = ParseTagList(args.value("tags", std::string()));
            entry.relations = ParseRelationList(args.value("relations", std::string()));
            item.target = "World Bible · " + entry.Key() + " (" + entry.name + ")";
            item.detail = FormatWorldEntry(entry);
            WorldBible world(project);
            world.Refresh();
            if (world.Find(entry.Key()) != nullptr) {
                item.before = "(replaces the existing entry)";
            }
        } else if (name == "level.generate") {
            // Shown as what it will build: generation is pure and quick, so
            // review gets the real layout (as a map) before anything changes.
            LevelParams params = LevelParamsFromJson(args);
            item.target = "Generate a level in (" + std::to_string(params.x0) + "," + std::to_string(params.y0) +
                          ")-(" + std::to_string(params.x1) + "," + std::to_string(params.y1) + ")";
            std::string why;
            auto layout = GenerateLevel(params, &why);
            if (!layout) {
                error = why;
                return item;
            }
            item.detail = DescribeLevel(params, *layout);
        } else if (name == "world.delete") {
            item.target = "World Bible · delete " + args.value("key", std::string());
        } else if (name == "prefab.create") {
            // Shown as the prefab it will make (6.3).
            item.target = "New prefab prefabs/" + args.value("name", std::string()) + ".prefab";
            item.detail = std::string(args.value("tile", false) ? "Ground tile" : "Prop") + " from " +
                          args.value("asset", std::string()) +
                          (args.value("tile", false) ? std::string()
                                                     : ", " + std::to_string(args.value("cells", 0.0)).substr(0, 4) + " cells wide") +
                          (args.value("solid", false) ? ", solid" : "");
        } else {
            nlohmann::json shown = args;
            item.detail = shown.dump();
        }
        item.after = args;
        if (apply) {
            CommandContext context{registry, &prefabs, &project};
            CommandResult result = reflection.Run(context, name, values);
            if (!result.ok) {
                error = result.error;
                return item;
            }
            if ((name == "prefab.createFromEntity" || name == "prefab.create") && outcome) {
                outcome->prefabsChanged = true;
            }
            if (result.created && outcome) {
                outcome->created.back() = *result.created;
            }
        }

    } else {
        item.kind = "unknown";
        item.target = kind.empty() ? "(missing op)" : kind;
        error = "unknown operation " + kind;
    }
    return item;
}

} // namespace

bool ResolvedProposal::AllValid() const {
    for (const auto& item : items) {
        if (!item.error.empty()) {
            return false;
        }
    }
    return true;
}

ResolvedProposal ResolveProposal(const nlohmann::json& proposal, Registry& registry,
                                 const ReflectionRegistry& reflection, PrefabLibrary& prefabs,
                                 const Project& project) {
    ResolvedProposal resolved;
    resolved.summary = proposal.value("summary", "");
    resolved.notes = proposal.value("notes", "");
    const auto operations = proposal.value("operations", nlohmann::json::array()).get<std::vector<nlohmann::json>>();
    const ProposalScope scope = ScanProposal(operations);
    for (const auto& op : operations) {
        resolved.items.push_back(Process(op, Mode::Resolve, registry, reflection, prefabs, project, scope, nullptr));
    }
    return resolved;
}

ApplyOutcome ApplyOperations(const std::vector<nlohmann::json>& operations, Registry& registry,
                             const ReflectionRegistry& reflection, PrefabLibrary& prefabs, const Project& project) {
    ApplyOutcome outcome;
    const ProposalScope scope = ScanProposal(operations);
    for (const auto& op : operations) {
        outcome.created.push_back(entt::null);
        ChangeItem item = Process(op, Mode::Apply, registry, reflection, prefabs, project, scope, &outcome);
        if (item.error.empty()) {
            ++outcome.applied;
        } else {
            outcome.errors.push_back(item.target + ": " + item.error);
        }
    }
    return outcome;
}

} // namespace aige
