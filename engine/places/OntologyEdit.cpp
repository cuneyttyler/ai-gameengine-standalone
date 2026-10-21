#include "places/OntologyEdit.h"

#include <algorithm>
#include <fstream>

namespace aige {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

json FieldJson(const FieldDef& f) {
    json out = {{"name", f.name}, {"group", f.quantity ? "quantity" : "quality"}, {"type", f.type}};
    if (!f.defaultValue.is_null()) out["default"] = f.defaultValue;
    if (f.min) out["min"] = *f.min;
    if (f.max) out["max"] = *f.max;
    if (!f.unit.empty()) out["unit"] = f.unit;
    if (!f.values.empty()) out["values"] = f.values;
    if (!f.vocabulary.empty()) out["vocabulary"] = f.vocabulary;
    if (!f.levels.empty()) out["levels"] = f.levels;
    if (!f.when.empty()) out["when"] = f.when;
    if (f.ask) out["ask"] = true;
    if (!f.description.empty()) out["description"] = f.description;
    return out;
}

json ModuleJson(const ModuleDef& m, const std::map<std::string, std::vector<std::string>>& parents) {
    json fields = json::array();
    for (const auto* group : {&m.quantity, &m.quality}) {
        for (const auto& [name, f] : *group) fields.push_back(FieldJson(f));
    }
    json slots = json::array();
    for (const auto& s : m.slots) {
        json roles = json::array();
        for (const auto& b : s.binds) roles.push_back(b.role);
        slots.push_back({{"name", s.name}, {"roles", roles}, {"essential", s.essential}, {"primary", s.primary}});
    }
    json children = json::array();
    for (const auto& c : m.children) {
        json one = {{"module", c.module}, {"min", c.min}, {"weight", c.weight}};
        if (c.max < 1'000'000) one["max"] = c.max;
        if (!c.count.empty()) one["count"] = c.count;
        if (!c.when.empty()) one["when"] = c.when;
        children.push_back(one);
    }
    const auto p = parents.find(m.id);
    return {{"id", m.id},
            {"name", m.name},
            {"description", m.description},
            {"level", PlaceLevelName(m.level)},
            {"classes", m.classes},
            {"buildTypes", m.buildTypes},
            {"fields", fields},
            {"slots", slots},
            {"children", children},
            {"parents", p == parents.end() ? json::array() : json(p->second)},
            {"source", m.source},
            {"editable", m.source == "user"}};
}

json ReadJsonFile(const fs::path& path) {
    std::ifstream in(path);
    if (!in) return json();
    return json::parse(in, nullptr, false);
}

bool WriteJsonFile(const fs::path& path, const json& value) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << value.dump(2) << "\n";
    return static_cast<bool>(out);
}

bool Slug(const std::string& id) {
    if (id.empty() || id.size() > 60) return false;
    for (const char c : id) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    }
    return id.front() != '-' && id.back() != '-';
}

// The ontology with the user's folder on top of the engine's, and what's wrong with it that
// concerns `about` (a module id; empty: anything).
std::vector<std::string> ProblemsWith(const fs::path& engineDir, const fs::path& userDir, const std::string& about) {
    Ontology o;
    std::vector<std::string> problems = o.Load({engineDir, userDir});
    for (auto& p : o.Check()) problems.push_back(std::move(p));
    if (about.empty()) return problems;
    std::vector<std::string> mine;
    for (const auto& p : problems) {
        if (p.find(about) != std::string::npos) mine.push_back(p);
    }
    return mine;
}

std::string Joined(const std::vector<std::string>& list) {
    std::string out;
    for (size_t i = 0; i < list.size() && i < 4; ++i) out += (i ? "; " : "") + list[i];
    return out;
}

} // namespace

const std::vector<std::string>& FieldTypes() {
    static const std::vector<std::string> types = {"count", "density", "share", "area", "size", "length", "spacing", "width", "ratio",
                                                   "enum", "enumSet", "mix", "tags", "description", "bool", "assetChoice"};
    return types;
}

json OntologyJson(const Ontology& ontology) {
    // Where each module may go: its parents (the modules whose children name it or its class).
    std::map<std::string, std::vector<std::string>> parents;
    for (const auto& [id, m] : ontology.Modules()) {
        for (const auto& c : m.children) {
            if (c.module.rfind("class:", 0) == 0) {
                for (const auto& [childId, child] : ontology.Modules()) {
                    if (ontology.IsA(childId, c.module.substr(6))) parents[childId].push_back(id);
                }
            } else {
                parents[c.module].push_back(id);
            }
        }
    }
    json modules = json::array();
    for (const auto& [id, m] : ontology.Modules()) modules.push_back(ModuleJson(m, parents));
    json builds = json::array();
    for (const auto& [id, b] : ontology.Builds()) {
        builds.push_back({{"id", b.id}, {"name", b.name}, {"root", b.root}, {"components", b.components}, {"unit", b.unit.label}});
    }
    json classes = json::array();
    for (const auto& [id, c] : ontology.Classes()) classes.push_back({{"id", id}, {"label", c.label}, {"superclasses", c.superclasses}});
    json roles = json::array();
    for (const auto& [id, r] : ontology.Roles()) roles.push_back({{"id", id}, {"label", r.label}, {"broader", r.broader}});
    json schemes = json::array();
    for (const auto& [id, list] : ontology.Schemes()) {
        json values = json::array();
        for (const auto& c : list) values.push_back(c.id);
        schemes.push_back({{"id", id}, {"values", values}});
    }
    json relations = json::array();
    for (const auto& [id, r] : ontology.Relations()) relations.push_back({{"id", id}, {"description", r.description}});
    json levels = json::array();
    for (const auto level : {PlaceLevel::Site, PlaceLevel::Network, PlaceLevel::Zone, PlaceLevel::Parcel, PlaceLevel::Feature, PlaceLevel::Dressing}) {
        levels.push_back(PlaceLevelName(level));
    }
    return {{"modules", modules}, {"builds", builds},     {"classes", classes},       {"roles", roles},
            {"schemes", schemes}, {"relations", relations}, {"fieldTypes", FieldTypes()}, {"levels", levels}};
}

std::string SaveUserModule(const fs::path& userDir, const fs::path& engineDir, const json& given) {
    if (userDir.empty()) return "there's no user folder to keep it in";
    if (!given.is_object()) return "a module is an object";
    json module = given;
    module["schema"] = "aige.module/1";
    const std::string id = module.value("id", "");
    if (!Slug(id)) return "its id is lowercase letters, digits and dashes (e.g. watermill)";
    {
        Ontology engine;
        engine.Load({engineDir});
        if (engine.Module(id)) return "'" + id + "' is a built-in module: built-ins aren't editable (extend it instead)";
    }
    if (!PlaceLevelFromName(module.value("level", ""))) return "its level is one of site, network, zone, parcel, feature, dressing";
    if (module.value("name", "").empty()) module["name"] = id;
    for (const char* group : {"quantity", "quality"}) {
        if (!module.contains(group)) module[group] = json::object();
        if (!module[group].is_object()) return std::string(group) + " is an object of fields";
        for (const auto& [name, f] : module[group].items()) {
            const std::string type = f.is_object() ? f.value("type", "") : "";
            if (std::find(FieldTypes().begin(), FieldTypes().end(), type) == FieldTypes().end()) {
                return "field '" + name + "': its type is one of count, density, area, size, enum, bool, …";
            }
        }
    }
    for (const char* key : {"slots", "children"}) {
        if (!module.contains(key)) module[key] = json::object();
    }
    // Saved, then the whole checked: what's wrong with it puts the old one back.
    const fs::path file = userDir / "modules" / (id + ".module.json");
    const json before = ReadJsonFile(file);
    if (!WriteJsonFile(file, module)) return "couldn't write " + file.string();
    if (const auto problems = ProblemsWith(engineDir, userDir, id); !problems.empty()) {
        std::error_code ec;
        if (before.is_object()) WriteJsonFile(file, before);
        else fs::remove(file, ec);
        return Joined(problems);
    }
    return {};
}

std::string DeleteUserModule(const fs::path& userDir, const std::string& id) {
    if (!Slug(id)) return "no such module";
    std::error_code ec;
    const fs::path file = userDir / "modules" / (id + ".module.json");
    if (!fs::exists(file, ec)) return "'" + id + "' isn't one of yours";
    fs::remove(file, ec);
    return ec ? "couldn't remove it: " + ec.message() : std::string();
}

json UserExtensions(const fs::path& userDir) {
    const json e = ReadJsonFile(userDir / "extensions.json");
    return e.is_object() ? e : json{{"fields", json::object()}, {"children", json::object()}, {"components", json::object()}};
}

std::string SaveUserExtensions(const fs::path& userDir, const fs::path& engineDir, const json& extensions) {
    if (userDir.empty()) return "there's no user folder to keep it in";
    if (!extensions.is_object()) return "the extensions are an object";
    const fs::path file = userDir / "extensions.json";
    const json before = ReadJsonFile(file);
    if (!WriteJsonFile(file, extensions)) return "couldn't write " + file.string();
    if (const auto problems = ProblemsWith(engineDir, userDir, ""); !problems.empty()) {
        std::error_code ec;
        if (before.is_object()) WriteJsonFile(file, before);
        else fs::remove(file, ec);
        return Joined(problems);
    }
    return {};
}

json ExportUserOntology(const fs::path& userDir) {
    json modules = json::array();
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(userDir / "modules", ec)) {
        const std::string name = entry.path().filename().string();
        if (name.size() <= 12 || name.substr(name.size() - 12) != ".module.json") continue;
        const json m = ReadJsonFile(entry.path());
        if (m.is_object()) modules.push_back(m);
    }
    return {{"schema", "aige.ontology-extension/1"}, {"modules", modules}, {"extensions", UserExtensions(userDir)}};
}

std::string ImportUserOntology(const fs::path& userDir, const fs::path& engineDir, const json& bundle) {
    if (!bundle.is_object() || bundle.value("schema", "") != "aige.ontology-extension/1") return "not an AIGE ontology file";
    std::vector<std::string> problems;
    int added = 0;
    for (const auto& m : bundle.value("modules", json::array())) {
        if (const std::string error = SaveUserModule(userDir, engineDir, m); !error.empty()) {
            problems.push_back(m.value("id", "?") + ": " + error);
        } else {
            ++added;
        }
    }
    // Extensions merge into the user's own (theirs win where both set the same thing).
    json mine = UserExtensions(userDir);
    const json theirs = bundle.value("extensions", json::object());
    for (const char* key : {"fields", "children", "components"}) {
        if (!mine.contains(key) || !mine[key].is_object()) mine[key] = json::object();
        const json part = theirs.value(key, json::object());
        for (const auto& [k, v] : part.items()) {
            if (std::string(key) == "components" && v.is_array()) {
                json list = mine[key].value(k, json::array());
                for (const auto& m : v) {
                    if (std::find(list.begin(), list.end(), m) == list.end()) list.push_back(m);
                }
                mine[key][k] = list;
            } else if (v.is_object()) {
                if (!mine[key].contains(k) || !mine[key][k].is_object()) mine[key][k] = json::object();
                mine[key][k].update(v, true);
            }
        }
    }
    if (const std::string error = SaveUserExtensions(userDir, engineDir, mine); !error.empty()) problems.push_back("extensions: " + error);
    if (!problems.empty()) return std::to_string(added) + " module(s) imported; " + Joined(problems);
    return {};
}

} // namespace aige
