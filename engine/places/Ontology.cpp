#include "places/Ontology.h"

#include "places/Expr.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>

namespace aige {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

const std::vector<std::pair<PlaceLevel, const char*>> kLevels = {
    {PlaceLevel::Site, "site"},         {PlaceLevel::Network, "network"},   {PlaceLevel::Zone, "zone"},
    {PlaceLevel::Parcel, "parcel"},     {PlaceLevel::Feature, "feature"},   {PlaceLevel::Dressing, "dressing"},
    {PlaceLevel::Abstract, "abstract"},
};

const std::vector<std::string> kQuantityTypes = {"count", "density", "share", "area", "size", "length", "spacing", "width", "ratio"};
const std::vector<std::string> kQualityTypes = {"enum", "enumSet", "mix", "tags", "description", "bool", "assetChoice"};

bool Has(const std::vector<std::string>& list, const std::string& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

std::string Lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::optional<json> ReadJson(const fs::path& path, std::vector<std::string>& problems) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    json value = json::parse(in, nullptr, false);
    if (value.is_discarded()) {
        problems.push_back(path.filename().string() + ": isn't valid JSON");
        return std::nullopt;
    }
    return value;
}

std::vector<std::string> Strings(const json& value) {
    std::vector<std::string> out;
    if (value.is_string()) out.push_back(value.get<std::string>());
    if (value.is_array()) {
        for (const auto& item : value) {
            if (item.is_string()) out.push_back(item.get<std::string>());
        }
    }
    return out;
}

std::pair<int, int> Range(const json& value, std::pair<int, int> fallback) {
    if (value.is_number_integer()) return {value.get<int>(), value.get<int>()};
    if (value.is_array() && value.size() == 2 && value[0].is_number() && value[1].is_number()) {
        return {value[0].get<int>(), value[1].get<int>()};
    }
    return fallback;
}

FieldDef ParseField(const std::string& name, const json& j, bool quantity) {
    FieldDef f;
    f.name = name;
    f.quantity = quantity;
    f.type = j.value("type", "");
    f.defaultValue = j.value("default", json());
    if (j.contains("min") && j["min"].is_number()) f.min = j["min"].get<double>();
    if (j.contains("max") && j["max"].is_number()) f.max = j["max"].get<double>();
    if (j.contains("min") && j["min"].is_object()) f.minSize = j["min"];
    if (j.contains("max") && j["max"].is_object()) f.maxSize = j["max"];
    if (j.contains("levels") && j["levels"].is_array()) {
        for (const auto& level : j["levels"]) {
            if (level.is_number()) f.levels.push_back(level.get<double>());
        }
    }
    f.unit = j.value("unit", "");
    f.values = Strings(j.value("values", json::array()));
    f.vocabulary = j.value("vocabulary", "");
    f.when = j.value("when", "");
    f.ask = j.value("ask", false);
    f.description = j.value("description", "");
    if (f.type == "density" && f.levels.empty()) f.levels = {0.0, 0.05, 0.15, 0.3, 0.5}; // the shared default scale
    return f;
}

ModuleDef ParseModule(const json& j, const std::string& source) {
    ModuleDef m;
    m.id = j.value("id", "");
    m.name = j.value("name", m.id);
    m.description = j.value("description", "");
    m.level = PlaceLevelFromName(j.value("level", "feature")).value_or(PlaceLevel::Feature);
    m.buildTypes = Strings(j.value("buildTypes", json::array()));
    m.classes = Strings(j.value("classes", json::array()));
    for (const char* kind : {"quantity", "quality"}) {
        const json& fields = j.value(kind, json::object());
        if (!fields.is_object()) continue;
        for (const auto& [name, spec] : fields.items()) {
            if (!spec.is_object()) continue;
            (std::string(kind) == "quantity" ? m.quantity : m.quality)[name] = ParseField(name, spec, std::string(kind) == "quantity");
        }
    }
    const json& slots = j.value("slots", json::object());
    if (slots.is_object()) {
        for (const auto& [name, spec] : slots.items()) {
            if (!spec.is_object()) continue;
            SlotDef slot;
            slot.name = name;
            slot.essential = spec.value("essential", true);
            slot.when = spec.value("when", "");
            slot.fallback = spec.value("fallback", json());
            slot.primary = spec.value("primary", false);
            auto binding = [](const json& b) {
                SlotBinding binding;
                binding.when = b.value("when", "");
                binding.role = b.value("role", "");
                if (b.contains("concepts") && b["concepts"].is_string()) binding.concepts = b["concepts"].get<std::string>();
                if (b.contains("concepts") && b["concepts"].is_array()) binding.conceptList = Strings(b["concepts"]);
                return binding;
            };
            if (spec.contains("bind") && spec["bind"].is_array()) {
                for (const auto& b : spec["bind"]) {
                    if (b.is_object()) slot.binds.push_back(binding(b));
                }
            } else {
                slot.binds.push_back(binding(spec));
            }
            m.slots.push_back(slot);
        }
    }
    const json& children = j.value("children", json::object());
    if (children.is_object()) {
        for (const auto& [module, spec] : children.items()) {
            ChildDef child;
            child.module = module;
            if (spec.is_object()) {
                child.min = spec.value("min", 0);
                child.max = spec.value("max", 1'000'000);
                child.count = spec.value("count", "");
                child.weight = spec.value("weight", 1.0);
                child.when = spec.value("when", "");
            }
            m.children.push_back(child);
        }
    }
    m.constraints = Strings(j.value("constraints", json::array()));
    for (const auto& r : j.value("relations", json::array())) {
        if (!r.is_object()) continue;
        RelationDef relation;
        relation.type = r.value("type", "");
        relation.members = Strings(r.value("members", json::array()));
        relation.args = r.value("args", json::object());
        relation.hard = r.value("hard", false);
        relation.weight = r.value("weight", 1.0);
        m.relations.push_back(relation);
    }
    m.required = Strings(j.value("requires", json::array()));
    m.excludes = Strings(j.value("excludes", json::array()));
    const json& prefers = j.value("prefersSite", json::object());
    if (prefers.is_object()) {
        for (const auto& [criterion, weight] : prefers.items()) {
            if (weight.is_number()) m.prefersSite[criterion] = weight.get<double>();
        }
    }
    m.compile = j.value("compile", json());
    m.layout = j.value("layout", json::object());
    m.source = source;
    return m;
}

} // namespace

std::optional<PlaceLevel> PlaceLevelFromName(const std::string& name) {
    for (const auto& [level, text] : kLevels) {
        if (name == text) return level;
    }
    return std::nullopt;
}

const char* PlaceLevelName(PlaceLevel level) {
    for (const auto& [l, text] : kLevels) {
        if (l == level) return text;
    }
    return "?";
}

const FieldDef* ModuleDef::Field(const std::string& name) const {
    if (auto it = quantity.find(name); it != quantity.end()) return &it->second;
    if (auto it = quality.find(name); it != quality.end()) return &it->second;
    return nullptr;
}

const ScaleClass* BuildType::Scale(const std::string& id) const {
    for (const auto& s : scale) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

double Tolerances::Count(double target) const {
    if (std::abs(target) < exactBelow) return 0.0;
    return std::abs(target) * countFraction;
}

std::vector<std::string> Ontology::Load(const std::vector<fs::path>& dirs) {
    std::vector<std::string> problems;
    json extensions = json::array();
    for (const fs::path& dir : dirs) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        // Where its entries come from: the engine's library, the user's folder (…/aige/ontology), a project.
        const std::string source = dir == EnginePlacesDir() ? "engine"
                                   : dir.filename() == "ontology" && dir.parent_path().filename() == "aige" ? "user"
                                                                                                            : dir.parent_path().filename().string();

        if (auto classes = ReadJson(dir / "classes.json", problems); classes && classes->is_object()) {
            for (const auto& [cls, spec] : classes->items()) {
                superclasses_[cls] = spec.is_object() ? Strings(spec.value("subClassOf", json::array())) : std::vector<std::string>{};
                ModuleClass& c = classes_[cls];
                c.id = cls;
                c.label = spec.is_object() ? spec.value("label", cls) : cls;
                c.description = spec.is_object() ? spec.value("description", "") : "";
                c.superclasses = superclasses_[cls];
            }
        }
        if (auto builds = ReadJson(dir / "buildtypes.json", problems); builds && builds->is_array()) {
            for (const auto& b : *builds) {
                if (!b.is_object() || b.value("id", "").empty()) {
                    problems.push_back("buildtypes.json: every build type needs an id");
                    continue;
                }
                BuildType build;
                build.id = b.value("id", "");
                build.name = b.value("name", build.id);
                build.root = b.value("root", "");
                build.stages = Strings(b.value("stages", json::array()));
                build.components = Strings(b.value("components", json::array()));
                build.briefFields = b.value("briefFields", json::object());
                const json unit = b.value("unit", json::object());
                if (unit.is_object()) {
                    build.unit.field = unit.value("field", build.unit.field);
                    build.unit.cls = unit.value("class", build.unit.cls);
                    build.unit.label = unit.value("label", build.unit.label);
                }
                build.layout = b.value("layout", "site");
                build.words = Strings(b.value("words", json::array()));
                build.isDefault = b.value("default", false);
                build.rank = b.value("rank", 0);
                build.look = b.value("look", json::object());
                for (const auto& s : b.value("scale", json::array())) {
                    if (!s.is_object()) continue;
                    ScaleClass scale;
                    scale.id = s.value("class", "");
                    scale.buildings = Range(s.value("buildings", json()), {0, 0});
                    scale.side = Range(s.value("side", json()), {0, 0});
                    scale.zones = Range(s.value("zones", json()), {1, 1});
                    build.scale.push_back(scale);
                }
                builds_[build.id] = build;
            }
        }
        if (auto vocab = ReadJson(dir / "vocabularies.json", problems); vocab && vocab->is_object()) {
            for (const auto& [scheme, values] : vocab->items()) {
                auto& list = schemes_[scheme];
                for (const auto& v : values) {
                    Concept c;
                    c.scheme = scheme;
                    if (v.is_string()) {
                        c.id = v.get<std::string>();
                    } else if (v.is_object()) {
                        c.id = v.value("id", "");
                        c.label = v.value("label", "");
                        c.alt = Strings(v.value("alt", json::array()));
                        c.broader = v.value("broader", "");
                        c.look = v.value("look", json::object());
                        c.prompt = v.value("prompt", "");
                    }
                    if (c.id.empty()) continue;
                    if (c.label.empty()) c.label = c.id;
                    list.erase(std::remove_if(list.begin(), list.end(), [&](const Concept& o) { return o.id == c.id; }), list.end());
                    list.push_back(c);
                }
            }
        }
        if (auto roles = ReadJson(dir / "roles.json", problems); roles && roles->is_object()) {
            for (const auto& [id, spec] : roles->items()) {
                Role role;
                role.id = id;
                role.label = spec.is_object() ? spec.value("label", id) : id;
                role.broader = spec.is_object() ? spec.value("broader", "") : "";
                role.affordance = spec.is_object() ? spec.value("affordance", "") : "";
                role.size = spec.is_object() ? spec.value("size", 0.0f) : 0.0f;
                role.tall = spec.is_object() ? spec.value("tall", 0.0f) : 0.0f;
                role.draw = spec.is_object() ? spec.value("draw", "") : "";
                role.look = spec.is_object() ? spec.value("look", nlohmann::json::object()) : nlohmann::json::object();
                roles_[id] = role;
            }
        }
        if (auto relations = ReadJson(dir / "relations.json", problems); relations && relations->is_object()) {
            for (const auto& [id, spec] : relations->items()) {
                RelationType r;
                r.id = id;
                if (spec.is_object()) {
                    r.minMembers = spec.value("minMembers", 2);
                    r.maxMembers = spec.value("maxMembers", r.minMembers);
                    r.args = Strings(spec.value("args", json::array()));
                    r.hardByDefault = spec.value("hard", false);
                    r.description = spec.value("description", "");
                    r.use = spec.value("use", json::object());
                }
                relations_[id] = r;
            }
        }
        if (auto rules = ReadJson(dir / "rules.json", problems); rules && rules->is_array()) {
            for (const auto& r : *rules) {
                if (!r.is_object()) continue;
                DesignRule rule;
                rule.id = r.value("id", "");
                rule.text = r.value("text", "");
                rule.source = r.value("source", "");
                rule.appliesTo = r.value("appliesTo", "");
                rule.relation = r.value("relation", json());
                rules_.erase(std::remove_if(rules_.begin(), rules_.end(), [&](const DesignRule& o) { return o.id == rule.id; }),
                             rules_.end());
                rules_.push_back(rule);
            }
        }
        if (auto tol = ReadJson(dir / "tolerances.json", problems); tol && tol->is_object()) {
            tolerances_.countFraction = tol->value("countFraction", tolerances_.countFraction);
            tolerances_.exactBelow = tol->value("exactBelow", tolerances_.exactBelow);
            tolerances_.areaFraction = tol->value("areaFraction", tolerances_.areaFraction);
            tolerances_.densityLevels = tol->value("densityLevels", tolerances_.densityLevels);
        }
        if (fs::is_directory(dir / "modules", ec)) {
            std::vector<fs::path> files;
            for (const auto& entry : fs::directory_iterator(dir / "modules", ec)) {
                const std::string name = entry.path().filename().string();
                if (name.size() > 12 && name.substr(name.size() - 12) == ".module.json") files.push_back(entry.path());
            }
            std::sort(files.begin(), files.end());
            for (const auto& file : files) {
                auto j = ReadJson(file, problems);
                if (!j) continue;
                if (!j->is_object() || j->value("schema", "") != "aige.module/1") {
                    problems.push_back(file.filename().string() + ": not an aige.module/1 file");
                    continue;
                }
                ModuleDef m = ParseModule(*j, source);
                const std::string expected = file.filename().string().substr(0, file.filename().string().size() - 12);
                if (m.id != expected) {
                    problems.push_back(file.filename().string() + ": its id is '" + m.id + "', the file says '" + expected + "'");
                    continue;
                }
                modules_[m.id] = m;
                if (j->contains("extends") && (*j)["extends"].is_object()) extensions.push_back({{"module", m.id}, {"extends", (*j)["extends"]}});
            }
        }
        // A user's or a project's additions to what's there (development_plan.md 9): fields on
        // built-in modules, children of them, components of build types.
        if (auto more = ReadJson(dir / "extensions.json", problems); more && more->is_object()) extensions.push_back({{"file", *more}});
    }
    ApplyExtensions(extensions, problems);
    return problems;
}

void Ontology::ApplyExtensions(const json& extensions, std::vector<std::string>& problems) {
    auto addChild = [&](const std::string& parent, const std::string& child, const json& rule) {
        auto it = modules_.find(parent);
        if (it == modules_.end()) {
            problems.push_back(child + ": its parent '" + parent + "' isn't a module");
            return;
        }
        // The rule as a module file writes it, parsed the same way.
        const ModuleDef parsed = ParseModule({{"schema", "aige.module/1"}, {"id", parent}, {"children", {{child, rule.is_object() ? rule : json::object()}}}}, "");
        auto& kids = it->second.children;
        kids.erase(std::remove_if(kids.begin(), kids.end(), [&](const ChildDef& c) { return c.module == child; }), kids.end());
        for (const auto& c : parsed.children) kids.push_back(c);
    };
    auto addComponent = [&](const std::string& build, const std::string& module) {
        auto it = builds_.find(build);
        if (it == builds_.end()) {
            problems.push_back(module + ": no build type '" + build + "'");
            return;
        }
        auto& list = it->second.components;
        if (std::find(list.begin(), list.end(), module) == list.end()) list.push_back(module);
    };
    for (const auto& e : extensions) {
        if (e.contains("module")) {
            const std::string id = e["module"].get<std::string>();
            const json ext = e["extends"];
            const json parents = ext.value("parents", json::object());
            for (const auto& [parent, rule] : parents.items()) addChild(parent, id, rule);
            const json components = ext.value("components", json::array());
            for (const auto& b : components) {
                if (b.is_string()) addComponent(b.get<std::string>(), id);
            }
            continue;
        }
        const json file = e.value("file", json::object());
        const json fields = file.value("fields", json::object());
        for (const auto& [moduleId, groups] : fields.items()) {
            auto it = modules_.find(moduleId);
            if (it == modules_.end()) {
                problems.push_back("extensions.json: no module '" + moduleId + "' to add fields to");
                continue;
            }
            json fake = {{"schema", "aige.module/1"}, {"id", moduleId}};
            for (const char* kind : {"quantity", "quality"}) {
                if (groups.is_object() && groups.contains(kind)) fake[kind] = groups[kind];
            }
            const ModuleDef parsed = ParseModule(fake, "");
            for (const auto& [name, f] : parsed.quantity) {
                if (!it->second.Field(name)) it->second.quantity[name] = f;
            }
            for (const auto& [name, f] : parsed.quality) {
                if (!it->second.Field(name)) it->second.quality[name] = f;
            }
        }
        const json children = file.value("children", json::object());
        for (const auto& [parent, kids] : children.items()) {
            if (!kids.is_object()) continue;
            for (const auto& [child, rule] : kids.items()) addChild(parent, child, rule);
        }
        const json components = file.value("components", json::object());
        for (const auto& [build, list] : components.items()) {
            if (!list.is_array()) continue;
            for (const auto& m : list) {
                if (m.is_string()) addComponent(build, m.get<std::string>());
            }
        }
    }
}

nlohmann::json Ontology::RoleLook(const std::string& role, const std::string& key) const {
    int guard = 0;
    for (const Role* r = FindRole(role.substr(0, role.find('|'))); r && guard++ < 12; r = FindRole(r->broader)) {
        if (r->look.is_object() && r->look.contains(key)) return r->look[key];
    }
    return nullptr;
}

nlohmann::json Ontology::ConceptLook(const std::string& scheme, const std::string& id, const std::string& key) const {
    int guard = 0;
    for (const Concept* c = FindConcept(scheme, id); c && guard++ < 12; c = c->broader.empty() ? nullptr : FindConcept(scheme, c->broader)) {
        if (c->look.is_object() && c->look.contains(key)) return c->look[key];
    }
    return nullptr;
}

float Ontology::RoleSize(const std::string& role) const {
    int guard = 0;
    for (const Role* r = FindRole(role.substr(0, role.find('|'))); r && guard++ < 12; r = FindRole(r->broader)) {
        if (r->size > 0) return r->size;
    }
    return 0.0f;
}

float Ontology::RoleTall(const std::string& role) const {
    int guard = 0;
    for (const Role* r = FindRole(role.substr(0, role.find('|'))); r && guard++ < 12; r = FindRole(r->broader)) {
        if (r->tall > 0) return r->tall;
    }
    return 0.0f;
}

std::string Ontology::RoleDraw(const std::string& role) const {
    int guard = 0;
    for (const Role* r = FindRole(role.substr(0, role.find('|'))); r && guard++ < 12; r = FindRole(r->broader)) {
        if (!r->draw.empty()) return r->draw;
    }
    return {};
}

std::vector<std::string> Ontology::Check() const {
    std::vector<std::string> problems;
    auto knownTarget = [&](const std::string& target) {
        if (target.rfind("class:", 0) == 0) return superclasses_.contains(target.substr(6));
        return modules_.contains(target) || superclasses_.contains(target);
    };
    for (const auto& [cls, supers] : superclasses_) {
        for (const auto& s : supers) {
            if (!superclasses_.contains(s)) problems.push_back("class " + cls + ": unknown superclass " + s);
        }
    }
    for (const auto& [id, m] : modules_) {
        const std::string where = "module " + id;
        for (const auto& cls : m.classes) {
            if (!superclasses_.contains(cls)) problems.push_back(where + ": unknown class " + cls);
        }
        for (const auto& build : m.buildTypes) {
            if (!builds_.contains(build)) problems.push_back(where + ": unknown build type " + build);
        }
        for (const auto* fields : {&m.quantity, &m.quality}) {
            for (const auto& [name, f] : *fields) {
                const auto& allowed = f.quantity ? kQuantityTypes : kQualityTypes;
                if (!Has(allowed, f.type)) {
                    problems.push_back(where + ": field " + name + " has type '" + f.type + "', not a " +
                                       (f.quantity ? "quantity" : "quality") + " type");
                }
                if (!f.vocabulary.empty() && !schemes_.contains(f.vocabulary)) {
                    problems.push_back(where + ": field " + name + " uses unknown vocabulary " + f.vocabulary);
                }
                if ((f.type == "enum" || f.type == "enumSet" || f.type == "mix") && f.values.empty() && f.vocabulary.empty()) {
                    problems.push_back(where + ": field " + name + " needs values or a vocabulary");
                }
                if (f.type == "density" && f.levels.size() != 5) {
                    problems.push_back(where + ": field " + name + " needs five density levels");
                }
                if (!f.when.empty()) {
                    std::string error;
                    if (!places::Expr::Parse(f.when, &error)) problems.push_back(where + ": field " + name + " when: " + error);
                }
            }
        }
        for (const auto& slot : m.slots) {
            for (const auto& b : slot.binds) {
                std::string role = b.role;
                for (size_t start = 0; start <= role.size();) {
                    const size_t bar = role.find('|', start);
                    const std::string one = role.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
                    if (!one.empty() && !roles_.contains(one)) problems.push_back(where + ": slot " + slot.name + " uses unknown role " + one);
                    if (bar == std::string::npos) break;
                    start = bar + 1;
                }
                if (!b.concepts.empty()) {
                    // A field (or fields joined with '+') holding the concepts.
                    for (size_t start = 0; start <= b.concepts.size();) {
                        const size_t plus = b.concepts.find('+', start);
                        std::string field = b.concepts.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
                        field.erase(0, field.find_first_not_of(' '));
                        field.erase(field.find_last_not_of(' ') + 1);
                        if (!field.empty() && m.Field(field) == nullptr) {
                            problems.push_back(where + ": slot " + slot.name + " takes concepts from unknown field " + field);
                        }
                        if (plus == std::string::npos) break;
                        start = plus + 1;
                    }
                }
            }
        }
        for (const auto& child : m.children) {
            if (!knownTarget(child.module)) {
                problems.push_back(where + ": unknown child " + child.module);
                continue;
            }
            if (const ModuleDef* c = Module(child.module); c && static_cast<int>(c->level) < static_cast<int>(m.level) &&
                                                            c->level != PlaceLevel::Abstract && c->level != PlaceLevel::Network) {
                problems.push_back(where + " (" + PlaceLevelName(m.level) + ") contains " + child.module + " at an outer level (" +
                                   PlaceLevelName(c->level) + ")");
            }
            if (!child.count.empty() && m.Field(child.count) == nullptr) {
                problems.push_back(where + ": child " + child.module + " counts by unknown field " + child.count);
            }
        }
        for (const auto& constraint : m.constraints) {
            std::string error;
            if (!places::Expr::Parse(constraint, &error)) problems.push_back(where + ": constraint '" + constraint + "': " + error);
        }
        for (const auto& r : m.relations) {
            const RelationType* type = Relation(r.type);
            if (type == nullptr) {
                problems.push_back(where + ": unknown relation " + r.type);
            } else if (static_cast<int>(r.members.size()) < type->minMembers ||
                       (type->maxMembers >= 0 && static_cast<int>(r.members.size()) > type->maxMembers)) {
                problems.push_back(where + ": relation " + r.type + " takes " + std::to_string(type->minMembers) +
                                   (type->maxMembers == type->minMembers ? "" : "+") + " members");
            }
        }
        for (const auto& target : m.required) {
            if (!knownTarget(target)) problems.push_back(where + ": requires unknown " + target);
        }
        for (const auto& target : m.excludes) {
            if (!knownTarget(target)) problems.push_back(where + ": excludes unknown " + target);
        }
    }
    for (const auto& [id, b] : builds_) {
        if (!modules_.contains(b.root)) problems.push_back("build type " + id + ": unknown root module " + b.root);
        for (const auto& c : b.components) {
            if (!modules_.contains(c)) problems.push_back("build type " + id + ": unknown component " + c);
        }
        if (b.scale.empty()) problems.push_back("build type " + id + ": has no scale table");
    }
    for (const auto& [scheme, values] : schemes_) {
        for (const auto& c : values) {
            if (!c.broader.empty() && !FindConcept(scheme, c.broader)) {
                problems.push_back("vocabulary " + scheme + ": " + c.id + " is narrower than unknown " + c.broader);
            }
        }
    }
    for (const auto& [id, role] : roles_) {
        if (!role.broader.empty() && !roles_.contains(role.broader)) problems.push_back("role " + id + ": unknown broader role " + role.broader);
    }
    for (const auto& rule : rules_) {
        if (rule.source.empty()) problems.push_back("rule " + rule.id + ": every design rule names its source");
        if (!rule.appliesTo.empty() && !knownTarget(rule.appliesTo)) problems.push_back("rule " + rule.id + ": applies to unknown " + rule.appliesTo);
    }
    return problems;
}

const ModuleDef* Ontology::Module(const std::string& id) const {
    auto it = modules_.find(id);
    return it == modules_.end() ? nullptr : &it->second;
}

const BuildType* Ontology::BuildOfRoot(const std::string& rootModule) const {
    for (const auto& [id, b] : builds_) {
        if (b.root == rootModule) return &b;
    }
    return nullptr;
}

const BuildType* Ontology::DefaultBuild() const {
    for (const auto& [id, b] : builds_) {
        if (b.isDefault) return &b;
    }
    return builds_.empty() ? nullptr : &builds_.begin()->second;
}

const BuildType* Ontology::Build(const std::string& id) const {
    auto it = builds_.find(id);
    return it == builds_.end() ? nullptr : &it->second;
}

const RelationType* Ontology::Relation(const std::string& id) const {
    auto it = relations_.find(id);
    return it == relations_.end() ? nullptr : &it->second;
}

const Role* Ontology::FindRole(const std::string& id) const {
    auto it = roles_.find(id);
    return it == roles_.end() ? nullptr : &it->second;
}

const std::vector<Concept>* Ontology::Scheme(const std::string& id) const {
    auto it = schemes_.find(id);
    return it == schemes_.end() ? nullptr : &it->second;
}

const Concept* Ontology::FindConcept(const std::string& scheme, const std::string& id) const {
    const auto* values = Scheme(scheme);
    if (values == nullptr) return nullptr;
    for (const auto& c : *values) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

bool Ontology::IsA(const std::string& module, const std::string& cls) const {
    if (module == cls) return true;
    std::vector<std::string> todo;
    if (const ModuleDef* m = Module(module)) todo = m->classes;
    std::set<std::string> seen;
    while (!todo.empty()) {
        const std::string c = todo.back();
        todo.pop_back();
        if (c == cls) return true;
        if (!seen.insert(c).second) continue;
        if (auto it = superclasses_.find(c); it != superclasses_.end()) todo.insert(todo.end(), it->second.begin(), it->second.end());
    }
    return false;
}

bool Ontology::IsKindOf(const std::string& scheme, const std::string& id, const std::string& ancestor) const {
    std::string current = id;
    for (int guard = 0; guard < 32 && !current.empty(); ++guard) {
        if (current == ancestor) return true;
        const Concept* c = FindConcept(scheme, current);
        current = c ? c->broader : "";
    }
    return false;
}

std::vector<const Concept*> Ontology::ConceptsNamed(const std::string& word) const {
    std::vector<const Concept*> found;
    const std::string w = Lower(word);
    for (const auto& [scheme, values] : schemes_) {
        for (const auto& c : values) {
            bool match = Lower(c.id) == w || Lower(c.label) == w;
            for (const auto& a : c.alt) match = match || Lower(a) == w;
            if (match) found.push_back(&c);
        }
    }
    return found;
}

const ChildDef* Ontology::ChildRule(const std::string& parent, const std::string& child) const {
    const ModuleDef* p = Module(parent);
    if (p == nullptr) return nullptr;
    for (const auto& rule : p->children) {
        if (rule.module == child) return &rule;
    }
    for (const auto& rule : p->children) {
        const std::string cls = rule.module.rfind("class:", 0) == 0 ? rule.module.substr(6) : rule.module;
        if (superclasses_.contains(cls) && IsA(child, cls)) return &rule;
    }
    return nullptr;
}

json Ontology::Subgraph(const std::string& id) const {
    const ModuleDef* m = Module(id);
    if (m == nullptr) return nullptr;
    auto fieldsJson = [](const std::map<std::string, FieldDef>& fields) {
        json out = json::object();
        for (const auto& [name, f] : fields) {
            json j = {{"type", f.type}};
            if (!f.defaultValue.is_null()) j["default"] = f.defaultValue;
            if (f.min) j["min"] = *f.min;
            if (f.max) j["max"] = *f.max;
            if (!f.values.empty()) j["values"] = f.values;
            if (!f.vocabulary.empty()) j["vocabulary"] = f.vocabulary;
            if (!f.levels.empty()) j["levels"] = f.levels;
            if (!f.unit.empty()) j["unit"] = f.unit;
            if (!f.when.empty()) j["when"] = f.when;
            if (!f.description.empty()) j["description"] = f.description;
            out[name] = j;
        }
        return out;
    };
    json children = json::array();
    std::set<std::string> vocabularies;
    for (const auto& c : m->children) {
        json child = {{"module", c.module}, {"min", c.min}, {"weight", c.weight}};
        if (c.max < 1'000'000) child["max"] = c.max;
        if (!c.count.empty()) child["count"] = c.count;
        if (!c.when.empty()) child["when"] = c.when;
        if (const ModuleDef* cm = Module(c.module)) {
            child["name"] = cm->name;
            child["description"] = cm->description;
            child["level"] = PlaceLevelName(cm->level);
        }
        children.push_back(child);
    }
    for (const auto* fields : {&m->quantity, &m->quality}) {
        for (const auto& [name, f] : *fields) {
            if (!f.vocabulary.empty()) vocabularies.insert(f.vocabulary);
        }
    }
    json concepts = json::object();
    for (const auto& scheme : vocabularies) {
        json values = json::array();
        if (const auto* list = Scheme(scheme)) {
            for (const auto& c : *list) {
                json v = {{"id", c.id}};
                if (!c.broader.empty()) v["broader"] = c.broader;
                values.push_back(v);
            }
        }
        concepts[scheme] = values;
    }
    json relations = json::array();
    for (const auto& r : m->relations) {
        relations.push_back({{"type", r.type}, {"members", r.members}, {"weight", r.weight}, {"hard", r.hard}});
    }
    json rules = json::array();
    for (const auto& rule : rules_) {
        const std::string target = rule.appliesTo.rfind("class:", 0) == 0 ? rule.appliesTo.substr(6) : rule.appliesTo;
        if (target.empty() || IsA(id, target)) rules.push_back({{"id", rule.id}, {"text", rule.text}, {"source", rule.source}});
    }
    return {{"module", m->id},
            {"name", m->name},
            {"description", m->description},
            {"level", PlaceLevelName(m->level)},
            {"quantity", fieldsJson(m->quantity)},
            {"quality", fieldsJson(m->quality)},
            {"children", children},
            {"relations", relations},
            {"constraints", m->constraints},
            {"requires", m->required},
            {"prefersSite", m->prefersSite},
            {"rules", rules},
            {"concepts", concepts}};
}

fs::path EnginePlacesDir() {
#ifdef AIGE_PLACES_DATA_DIR
    return fs::path(AIGE_PLACES_DATA_DIR);
#else
    return fs::path("data") / "places";
#endif
}

} // namespace aige
