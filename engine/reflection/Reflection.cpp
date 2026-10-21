#include "reflection/Reflection.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace aige {

namespace {

std::string FormatNumber(double value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

nlohmann::json MetaToJson(const FieldMeta& meta) {
    nlohmann::json json = {{"name", meta.name}, {"type", FieldTypeName(meta.type)}, {"description", meta.description}};
    if (!meta.options.empty()) {
        json["options"] = meta.options;
    }
    if (meta.min) {
        json["min"] = *meta.min;
    }
    if (meta.max) {
        json["max"] = *meta.max;
    }
    if (meta.optional) {
        json["optional"] = true;
    }
    if (!meta.scalable.empty()) {
        json["scalable"] = meta.scalable;
    }
    return json;
}

} // namespace

const char* FieldTypeName(FieldType type) {
    switch (type) {
        case FieldType::Float: return "float";
        case FieldType::Int: return "int";
        case FieldType::Bool: return "bool";
        case FieldType::String: return "string";
        case FieldType::Enum: return "enum";
        case FieldType::EntityRef: return "entity";
        case FieldType::Texture: return "texture";
        case FieldType::Script: return "script";
        case FieldType::Audio: return "audio";
        case FieldType::Prefab: return "prefab";
        case FieldType::Component: return "component";
    }
    return "float";
}

// --- Values ---------------------------------------------------------------------------

float ToFloat(const FieldValue& value) {
    if (auto* f = std::get_if<float>(&value)) return *f;
    if (auto* i = std::get_if<int>(&value)) return static_cast<float>(*i);
    if (auto* b = std::get_if<bool>(&value)) return *b ? 1.0f : 0.0f;
    return 0.0f;
}

int ToInt(const FieldValue& value) {
    if (auto* i = std::get_if<int>(&value)) return *i;
    if (auto* f = std::get_if<float>(&value)) return static_cast<int>(*f);
    if (auto* b = std::get_if<bool>(&value)) return *b ? 1 : 0;
    return 0;
}

bool ToBool(const FieldValue& value) {
    if (auto* b = std::get_if<bool>(&value)) return *b;
    if (auto* i = std::get_if<int>(&value)) return *i != 0;
    if (auto* f = std::get_if<float>(&value)) return *f != 0.0f;
    return false;
}

std::string ToString(const FieldValue& value) {
    if (auto* s = std::get_if<std::string>(&value)) return *s;
    return {};
}

Entity ToEntity(const FieldValue& value) {
    if (auto* e = std::get_if<Entity>(&value)) return *e;
    return entt::null;
}

std::optional<std::string> Validate(const FieldMeta& meta, const FieldValue& value) {
    auto checkRange = [&](double number) -> std::optional<std::string> {
        if (!std::isfinite(number)) {
            return meta.name + " must be a finite number";
        }
        if (meta.min && number < *meta.min) {
            return meta.name + " must be at least " + FormatNumber(*meta.min);
        }
        if (meta.max && number > *meta.max) {
            return meta.name + " must be at most " + FormatNumber(*meta.max);
        }
        return std::nullopt;
    };

    switch (meta.type) {
        case FieldType::Float:
            if (!std::holds_alternative<float>(value) && !std::holds_alternative<int>(value)) {
                return meta.name + " must be a number";
            }
            return checkRange(ToFloat(value));
        case FieldType::Int:
            if (const auto* f = std::get_if<float>(&value); f && std::floor(*f) != *f) {
                return meta.name + " must be a whole number";
            }
            if (!std::holds_alternative<int>(value) && !std::holds_alternative<float>(value)) {
                return meta.name + " must be a whole number";
            }
            return checkRange(ToFloat(value));
        case FieldType::Bool:
            if (!std::holds_alternative<bool>(value)) {
                return meta.name + " must be true or false";
            }
            return std::nullopt;
        case FieldType::Enum: {
            if (!std::holds_alternative<int>(value)) {
                return meta.name + " must be one of its options";
            }
            int index = std::get<int>(value);
            if (index < 0 || index >= static_cast<int>(meta.options.size())) {
                return meta.name + " has no option " + std::to_string(index);
            }
            return std::nullopt;
        }
        case FieldType::EntityRef:
            if (!std::holds_alternative<Entity>(value)) {
                return meta.name + " must be an entity";
            }
            return std::nullopt;
        default:
            if (!std::holds_alternative<std::string>(value)) {
                return meta.name + " must be text";
            }
            if ((meta.type == FieldType::Prefab || meta.type == FieldType::Component) && !meta.optional &&
                std::get<std::string>(value).empty()) {
                return meta.name + " is required";
            }
            return std::nullopt;
    }
}

// --- Lookups -------------------------------------------------------------------------

const FieldInfo* ComponentInfo::Field(const std::string& fieldName) const {
    for (const auto& field : fields) {
        if (field.name == fieldName) {
            return &field;
        }
    }
    return nullptr;
}

const SettingField* SettingsInfo::Field(const std::string& fieldName) const {
    for (const auto& field : fields) {
        if (field.name == fieldName) {
            return &field;
        }
    }
    return nullptr;
}

const ComponentInfo* ReflectionRegistry::FindComponent(const std::string& name) const {
    for (const auto& component : components_) {
        if (component.name == name) {
            return &component;
        }
    }
    return nullptr;
}

ComponentInfo& ReflectionRegistry::MutableComponent(const std::string& name) {
    for (auto& component : components_) {
        if (component.name == name) {
            return component;
        }
    }
    throw std::out_of_range("no component " + name);
}

const SettingsInfo* ReflectionRegistry::FindSettings(const std::string& name) const {
    for (const auto& settings : settings_) {
        if (settings.name == name) {
            return &settings;
        }
    }
    return nullptr;
}

const CommandInfo* ReflectionRegistry::FindCommand(const std::string& name) const {
    for (const auto& command : commands_) {
        if (command.name == name) {
            return &command;
        }
    }
    return nullptr;
}

CommandBuilder ReflectionRegistry::Command(std::string name, std::string description) {
    commands_.push_back({std::move(name), std::move(description), {}, {}});
    return CommandBuilder(*this, commands_.size() - 1);
}

// --- Record types ------------------------------------------------------------------------

const RecordField* RecordField::Field(const std::string& fieldName) const {
    for (const auto& field : fields) {
        if (field.name == fieldName) return &field;
    }
    return nullptr;
}

const RecordField* RecordTypeInfo::Field(const std::string& fieldName) const {
    for (const auto& field : fields) {
        if (field.name == fieldName) return &field;
    }
    return nullptr;
}

nlohmann::json RecordFieldToJson(const RecordField& field) {
    nlohmann::json json = MetaToJson(field);
    if (field.kind == RecordFieldKind::Record) {
        json["type"] = "record";
        json["recordType"] = field.recordType;
    } else if (field.kind == RecordFieldKind::Object) {
        json["type"] = "object";
        nlohmann::json fields = nlohmann::json::array();
        for (const auto& sub : field.fields) fields.push_back(RecordFieldToJson(sub));
        json["fields"] = fields;
    }
    if (field.list) json["list"] = true;
    if (field.required) json["required"] = true;
    if (!field.defaultValue.is_null()) json["default"] = field.defaultValue;
    return json;
}

const RecordTypeInfo* ReflectionRegistry::FindRecordType(const std::string& name) const {
    for (const auto& type : recordTypes_) {
        if (type.name == name) return &type;
    }
    return nullptr;
}

RecordTypeBuilder ReflectionRegistry::RecordType(std::string name, std::string description) {
    RecordTypeInfo info;
    info.name = std::move(name);
    info.description = std::move(description);
    recordTypes_.push_back(std::move(info));
    return RecordTypeBuilder(*this, recordTypes_.size() - 1);
}

RecordField MakeRecordValue(std::string name, FieldType type, std::string description) {
    RecordField field;
    field.name = std::move(name);
    field.type = type;
    field.description = std::move(description);
    return field;
}

RecordField MakeRecordRef(std::string name, std::string recordType, std::string description) {
    RecordField field = MakeRecordValue(std::move(name), FieldType::String, std::move(description));
    field.kind = RecordFieldKind::Record;
    field.recordType = std::move(recordType);
    return field;
}

RecordField& RecordTypeBuilder::Last() { return registry_.recordTypes_[index_].fields.back(); }

RecordTypeBuilder& RecordTypeBuilder::Field(std::string name, FieldType type, std::string description) {
    registry_.recordTypes_[index_].fields.push_back(MakeRecordValue(std::move(name), type, std::move(description)));
    return *this;
}
RecordTypeBuilder& RecordTypeBuilder::Ref(std::string name, std::string recordType, std::string description) {
    registry_.recordTypes_[index_].fields.push_back(
        MakeRecordRef(std::move(name), std::move(recordType), std::move(description)));
    return *this;
}
RecordTypeBuilder& RecordTypeBuilder::Object(std::string name, std::vector<RecordField> fields,
                                             std::string description) {
    RecordField field = MakeRecordValue(std::move(name), FieldType::String, std::move(description));
    field.kind = RecordFieldKind::Object;
    field.fields = std::move(fields);
    registry_.recordTypes_[index_].fields.push_back(std::move(field));
    return *this;
}
RecordTypeBuilder& RecordTypeBuilder::Min(double value) {
    Last().min = value;
    return *this;
}
RecordTypeBuilder& RecordTypeBuilder::Max(double value) {
    Last().max = value;
    return *this;
}
RecordTypeBuilder& RecordTypeBuilder::Options(std::vector<std::string> options) {
    Last().options = std::move(options);
    return *this;
}
RecordTypeBuilder& RecordTypeBuilder::List() {
    Last().list = true;
    return *this;
}
RecordTypeBuilder& RecordTypeBuilder::Required() {
    Last().required = true;
    return *this;
}
RecordTypeBuilder& RecordTypeBuilder::Default(nlohmann::json value) {
    Last().defaultValue = std::move(value);
    return *this;
}

CommandResult ReflectionRegistry::Run(CommandContext& context, const std::string& name,
                                      const std::vector<FieldValue>& args) const {
    const CommandInfo* command = FindCommand(name);
    if (command == nullptr || !command->run) {
        return {false, "unknown command " + name};
    }
    if (args.size() != command->params.size()) {
        return {false, name + " takes " + std::to_string(command->params.size()) + " arguments, got " +
                           std::to_string(args.size())};
    }
    for (size_t i = 0; i < args.size(); ++i) {
        if (auto error = Validate(command->params[i], args[i])) {
            return {false, name + ": " + *error};
        }
    }
    return command->run(context, args);
}

nlohmann::json ReflectionRegistry::ExportSchema() const {
    nlohmann::json components = nlohmann::json::array();
    for (const auto& component : components_) {
        if (!component.inspectorVisible) {
            continue; // bookkeeping isn't something to reason about
        }
        nlohmann::json fields = nlohmann::json::array();
        for (const auto& field : component.fields) {
            fields.push_back(MetaToJson(field));
        }
        components.push_back({{"name", component.name},
                              {"description", component.description},
                              {"userEditable", component.userEditable},
                              {"dependencies", component.dependencies},
                              {"fields", fields}});
    }
    nlohmann::json settings = nlohmann::json::array();
    for (const auto& section : settings_) {
        nlohmann::json fields = nlohmann::json::array();
        for (const auto& field : section.fields) {
            fields.push_back(MetaToJson(field));
        }
        settings.push_back({{"name", section.name}, {"description", section.description}, {"fields", fields}});
    }
    nlohmann::json commands = nlohmann::json::array();
    for (const auto& command : commands_) {
        nlohmann::json params = nlohmann::json::array();
        for (const auto& param : command.params) {
            params.push_back(MetaToJson(param));
        }
        commands.push_back({{"name", command.name}, {"description", command.description}, {"params", params}});
    }
    nlohmann::json records = nlohmann::json::array();
    for (const auto& type : recordTypes_) {
        nlohmann::json fields = nlohmann::json::array();
        for (const auto& field : type.fields) {
            fields.push_back(RecordFieldToJson(field));
        }
        records.push_back({{"name", type.name}, {"description", type.description}, {"fields", fields}});
    }
    return {{"version", 1},         {"components", components}, {"settings", settings},
            {"commands", commands}, {"records", records}};
}

// --- Component operations ------------------------------------------------------------

bool AddComponent(const ReflectionRegistry& reflection, Registry& registry, Entity entity, const std::string& name) {
    const ComponentInfo* component = reflection.FindComponent(name);
    if (component == nullptr || !registry.valid(entity) || component->has(registry, entity)) {
        return false;
    }
    for (const auto& required : component->dependencies) {
        const ComponentInfo* dependency = reflection.FindComponent(required);
        if (dependency != nullptr && !dependency->has(registry, entity)) {
            AddComponent(reflection, registry, entity, required);
        }
    }
    component->add(registry, entity);
    return true;
}

std::optional<std::string> RemoveComponent(const ReflectionRegistry& reflection, Registry& registry, Entity entity,
                                           const std::string& name) {
    const ComponentInfo* component = reflection.FindComponent(name);
    if (component == nullptr || !registry.valid(entity) || !component->has(registry, entity)) {
        return name + " isn't on this entity";
    }
    for (const auto& other : reflection.Components()) {
        if (other.has(registry, entity) &&
            std::find(other.dependencies.begin(), other.dependencies.end(), name) != other.dependencies.end()) {
            return name + " is required by " + other.name;
        }
    }
    component->remove(registry, entity);
    return std::nullopt;
}

// --- Text form -----------------------------------------------------------------------

std::string QuoteValue(const std::string& value) {
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

bool IsInstanceLocal(const std::string& component, const FieldMeta& field) {
    if (field.type == FieldType::EntityRef) {
        return true; // references point at other entities in this scene
    }
    if (component == "WorldTransform2D" || component == "ScreenTransform2D" || component == "PrefabInstance") {
        return true;
    }
    return component == "Tile" && (field.name == "gx" || field.name == "gy");
}

std::string FormatField(const FieldMeta& field, const FieldValue& value,
                        const std::function<std::optional<size_t>(Entity)>& entityIndex) {
    switch (field.type) {
        case FieldType::Float: {
            std::ostringstream out;
            out.precision(9); // enough significant digits to round-trip an IEEE 754 float exactly
            out << ToFloat(value);
            return out.str();
        }
        case FieldType::Int:
            return std::to_string(ToInt(value));
        case FieldType::Bool:
            return ToBool(value) ? "1" : "0";
        case FieldType::Enum: {
            int index = ToInt(value);
            return index >= 0 && index < static_cast<int>(field.options.size()) ? field.options[index]
                                                                                 : std::to_string(index);
        }
        case FieldType::EntityRef: {
            Entity target = ToEntity(value);
            auto index = target == entt::null || !entityIndex ? std::nullopt : entityIndex(target);
            return index ? "#" + std::to_string(*index) : "none";
        }
        default:
            return QuoteValue(ToString(value));
    }
}

std::optional<FieldValue> ParseField(const FieldMeta& field, const std::string& text,
                                     std::optional<size_t>* refIndex) {
    try {
        switch (field.type) {
            case FieldType::Float:
                return FieldValue(std::stof(text));
            case FieldType::Int:
                return FieldValue(std::stoi(text));
            case FieldType::Bool:
                return FieldValue(text == "1" || text == "true");
            case FieldType::Enum: {
                for (size_t i = 0; i < field.options.size(); ++i) {
                    if (field.options[i] == text) {
                        return FieldValue(static_cast<int>(i));
                    }
                }
                return FieldValue(std::stoi(text)); // numeric fallback
            }
            case FieldType::EntityRef:
                if (text.size() > 1 && text[0] == '#' && refIndex) {
                    *refIndex = static_cast<size_t>(std::stoul(text.substr(1)));
                }
                return FieldValue(Entity{entt::null});
            default:
                return FieldValue(text);
        }
    } catch (const std::exception&) {
        // Malformed number: the caller keeps the default.
    }
    return std::nullopt;
}

// --- JSON form ------------------------------------------------------------------------

nlohmann::json ValueToJson(const FieldMeta& meta, const FieldValue& value) {
    switch (meta.type) {
        case FieldType::Float:
            return ToFloat(value);
        case FieldType::Int:
            return ToInt(value);
        case FieldType::Bool:
            return ToBool(value);
        case FieldType::Enum: {
            int index = ToInt(value);
            if (index >= 0 && index < static_cast<int>(meta.options.size())) {
                return meta.options[index];
            }
            return index;
        }
        case FieldType::EntityRef: {
            Entity target = ToEntity(value);
            return target == entt::null ? nlohmann::json(nullptr) : nlohmann::json(static_cast<uint32_t>(target));
        }
        default:
            return ToString(value);
    }
}

std::optional<FieldValue> ValueFromJson(const FieldMeta& meta, const nlohmann::json& json) {
    switch (meta.type) {
        case FieldType::Float:
            return json.is_number() ? std::optional<FieldValue>(json.get<float>()) : std::nullopt;
        case FieldType::Int:
            if (json.is_number_integer()) {
                return FieldValue(json.get<int>());
            }
            if (json.is_number_float() && std::floor(json.get<double>()) == json.get<double>()) {
                return FieldValue(static_cast<int>(json.get<double>()));
            }
            return std::nullopt;
        case FieldType::Bool:
            return json.is_boolean() ? std::optional<FieldValue>(json.get<bool>()) : std::nullopt;
        case FieldType::Enum:
            if (json.is_string()) {
                auto it = std::find(meta.options.begin(), meta.options.end(), json.get<std::string>());
                if (it == meta.options.end()) {
                    return std::nullopt;
                }
                return FieldValue(static_cast<int>(it - meta.options.begin()));
            }
            return json.is_number_integer() ? std::optional<FieldValue>(json.get<int>()) : std::nullopt;
        case FieldType::EntityRef:
            if (json.is_null()) {
                return FieldValue(Entity{entt::null});
            }
            return json.is_number_unsigned() || json.is_number_integer()
                       ? std::optional<FieldValue>(static_cast<Entity>(json.get<uint32_t>()))
                       : std::nullopt;
        default:
            return json.is_string() ? std::optional<FieldValue>(json.get<std::string>()) : std::nullopt;
    }
}

} // namespace aige
