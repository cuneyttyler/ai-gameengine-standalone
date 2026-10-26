#include "planner/PlannerDocs.h"

#include "places/Ontology.h"
#include "places/PlaceValidate.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>

#ifndef AIGE_PLANNER_DATA_DIR
#define AIGE_PLANNER_DATA_DIR "data/planner"
#endif

namespace aige {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

json ReadJson(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return json::object();
    json j = json::parse(in, nullptr, false);
    return j.is_discarded() ? json::object() : j;
}

std::vector<std::string> Strings(const json& j) {
    std::vector<std::string> out;
    if (j.is_string()) out.push_back(j.get<std::string>());
    if (j.is_array()) {
        for (const auto& v : j) {
            if (v.is_string()) out.push_back(v.get<std::string>());
        }
    }
    return out;
}

bool Contains(const std::vector<std::string>& list, const std::string& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

std::string Text(const json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::string Join(const std::vector<std::string>& list) {
    std::string out;
    for (const auto& s : list) out += (out.empty() ? "" : ", ") + s;
    return out;
}

// Under `target`, every leaf of `defaults` that target lacks; their paths go to `assumed`.
void MergeUnder(json& target, const json& defaults, const std::string& prefix, std::vector<std::string>* assumed) {
    if (!defaults.is_object()) return;
    for (const auto& [k, v] : defaults.items()) {
        const std::string path = prefix.empty() ? k : prefix + "." + k;
        if (!target.contains(k) || target[k].is_null()) {
            target[k] = v;
            if (assumed) {
                if (v.is_object()) {
                    for (const auto& [kk, vv] : v.items()) assumed->push_back(path + "." + kk);
                } else {
                    assumed->push_back(path);
                }
            }
        } else if (v.is_object() && target[k].is_object()) {
            MergeUnder(target[k], v, path, assumed);
        }
    }
}

std::vector<std::string> FieldOptions(const json& field, const StoryOntology& gso) {
    if (field.contains("options")) return Strings(field["options"]);
    if (field.contains("vocab")) {
        auto it = gso.vocabularies.find(field.value("vocab", ""));
        if (it != gso.vocabularies.end()) return it->second;
    }
    return {};
}

} // namespace

std::vector<std::string> ProblemTexts(const std::vector<PlanProblem>& problems, bool errorsOnly) {
    std::vector<std::string> out;
    for (const auto& p : problems) {
        if (errorsOnly && p.warning) continue;
        out.push_back((p.where.empty() ? "" : p.where + ": ") + p.message);
    }
    return out;
}

bool HasErrors(const std::vector<PlanProblem>& problems) {
    return std::any_of(problems.begin(), problems.end(), [](const PlanProblem& p) { return !p.warning; });
}

// --- Data -----------------------------------------------------------------------------------

const PlannerData& PlannerData::Engine() {
    static const PlannerData data = [] {
        PlannerData d;
        const fs::path dir = AIGE_PLANNER_DATA_DIR;
        d.briefSchema = ReadJson(dir / "brief-schema.json");
        d.scope = ReadJson(dir / "scope.json");
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(dir / "profiles", ec)) {
            if (entry.path().extension() != ".json") continue;
            const json j = ReadJson(entry.path());
            GenreProfile p;
            p.id = j.value("id", entry.path().stem().string());
            p.label = j.value("label", p.id);
            p.stages = j.value("stages", json::object());
            p.defaults = j.value("defaults", json::object());
            p.asked = Strings(j.value("asked", json::array()));
            d.profiles[p.id] = std::move(p);
        }
        return d;
    }();
    return data;
}

const GenreProfile* PlannerData::Profile(const std::string& id) const {
    auto it = profiles.find(id);
    return it == profiles.end() ? nullptr : &it->second;
}

std::vector<std::string> PlannerData::Genres() const {
    std::vector<std::string> out;
    for (const auto& [id, p] : profiles) out.push_back(id);
    return out;
}

// --- The Brief --------------------------------------------------------------------------------

json BriefGet(const json& brief, const std::string& path) {
    const json* at = &brief;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t dot = path.find('.', start);
        const std::string key = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (!at->is_object() || !at->contains(key)) return nullptr;
        at = &(*at)[key];
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return *at;
}

void BriefSet(json& brief, const std::string& path, const json& value) {
    json* at = &brief;
    size_t start = 0;
    while (true) {
        const size_t dot = path.find('.', start);
        const std::string key = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (!at->is_object()) *at = json::object();
        if (dot == std::string::npos) {
            if (value.is_null()) at->erase(key);
            else (*at)[key] = value;
            return;
        }
        at = &(*at)[key];
        start = dot + 1;
    }
}

std::string ScopeClassFor(double hours) {
    for (const auto& c : PlannerData::Engine().scope.value("classes", json::array())) {
        const auto h = c.value("hours", json::array({0, 1000}));
        if (hours < h[1].get<double>()) return c.value("id", "short");
    }
    return "epic";
}

json ScopeNumbers(const std::string& scopeClass) {
    for (const auto& c : PlannerData::Engine().scope.value("classes", json::array())) {
        if (c.value("id", "") == scopeClass) return c;
    }
    return json::object();
}

json CompleteGameBrief(json brief, const json& projectDefaults, std::vector<std::string>* assumed) {
    if (!brief.is_object()) brief = json::object();
    brief["schema"] = "aige.game-brief/1";
    const PlannerData& data = PlannerData::Engine();
    if (!brief.contains("genre") || !data.Profile(brief.value("genre", ""))) {
        brief["genre"] = "arpg";
        if (assumed) assumed->push_back("genre");
    }
    MergeUnder(brief, projectDefaults, "", assumed);
    if (const GenreProfile* profile = data.Profile(brief["genre"])) MergeUnder(brief, profile->defaults, "", assumed);
    if (!brief.contains("playTime") || !brief["playTime"].is_number()) {
        brief["playTime"] = 3;
        if (assumed) assumed->push_back("playTime");
    }
    if (!brief.contains("scopeClass")) {
        brief["scopeClass"] = ScopeClassFor(brief["playTime"].get<double>());
        if (assumed) assumed->push_back("scopeClass");
    }
    const json numbers = ScopeNumbers(brief["scopeClass"]);
    for (const char* key : {"places", "scenes", "mainQuestStages", "sideQuests"}) {
        if (BriefGet(brief, std::string("scope.") + key).is_null() && numbers.contains(key)) {
            BriefSet(brief, std::string("scope.") + key, numbers[key]);
            if (assumed) assumed->push_back(std::string("scope.") + key);
        }
    }
    for (const char* importance : {"major", "supporting", "minor"}) {
        const std::string path = std::string("cast.") + importance;
        if (BriefGet(brief, path).is_null() && numbers.contains("cast")) {
            const auto range = numbers["cast"].value(importance, json::array({0, 0}));
            BriefSet(brief, path, static_cast<int>(std::lround((range[0].get<double>() + range[1].get<double>()) / 2.0)));
            if (assumed) assumed->push_back(path);
        }
    }
    for (const char* stage : {"concept", "outline", "story", "characters", "quests", "places", "systems", "visuals"}) {
        const std::string path = std::string("plan.autonomy.") + stage;
        if (BriefGet(brief, path) == "ask") BriefSet(brief, path, "confirm"); // its old name
        if (BriefGet(brief, path).is_null()) BriefSet(brief, path, std::string(stage) == "concept" || std::string(stage) == "outline" ? "confirm" : "review");
    }
    if (BriefGet(brief, "plan.building").is_null()) BriefSet(brief, "plan.building", "automatic");
    // How the places' builds find their art (written to the project's sourcing when confirmed).
    if (BriefGet(brief, "plan.art.source").is_null()) BriefSet(brief, "plan.art.source", "search");
    if (BriefGet(brief, "plan.art.webSearch").is_null()) BriefSet(brief, "plan.art.webSearch", false);
    if (BriefGet(brief, "plan.art.shareAlike").is_null()) BriefSet(brief, "plan.art.shareAlike", false);
    if (BriefGet(brief, "plan.building") == "ask-per-place") BriefSet(brief, "plan.building", "confirm-per-place");
    if (!brief.contains("tone")) {
        brief["tone"] = json::array({"heroic"});
        if (assumed) assumed->push_back("tone");
    }
    if (assumed) {
        std::sort(assumed->begin(), assumed->end());
        assumed->erase(std::unique(assumed->begin(), assumed->end()), assumed->end());
    }
    return brief;
}

std::vector<PlanProblem> ValidateGameBrief(const json& brief, const StoryOntology& gso) {
    std::vector<PlanProblem> out;
    if (!brief.is_object()) return {{"", "the Brief isn't an object"}};
    if (Text(brief, "title").empty()) out.push_back({"title", "the game needs a title"});
    if (Text(brief, "logline").empty()) out.push_back({"logline", "the game needs a logline"});
    if (!PlannerData::Engine().Profile(Text(brief, "genre"))) {
        out.push_back({"genre", "'" + Text(brief, "genre") + "' isn't a genre the Planner knows (" + Join(PlannerData::Engine().Genres()) + ")"});
    }
    for (const auto& tab : PlannerData::Engine().briefSchema.value("tabs", json::array())) {
        for (const auto& field : tab.value("fields", json::array())) {
            const std::string path = field.value("path", ""), type = field.value("type", "text");
            const json value = BriefGet(brief, path);
            if (value.is_null()) continue;
            const auto options = FieldOptions(field, gso);
            if (type == "enum") {
                if (!value.is_string() || (!options.empty() && !Contains(options, value.get<std::string>()))) {
                    out.push_back({path, (value.is_string() ? "'" + value.get<std::string>() + "'" : value.dump()) + " isn't one of " + Join(options)});
                }
            } else if (type == "enumSet") {
                for (const auto& v : Strings(value)) {
                    if (!options.empty() && !Contains(options, v)) out.push_back({path, "'" + v + "' isn't one of " + Join(options)});
                }
                if (!value.is_array()) out.push_back({path, "is a list"});
            } else if (type == "int" || type == "number") {
                if (!value.is_number()) out.push_back({path, "is a number"});
                else if ((field.contains("min") && value.get<double>() < field["min"].get<double>()) ||
                         (field.contains("max") && value.get<double>() > field["max"].get<double>())) {
                    out.push_back({path, "is out of range (" + field.value("min", json()).dump() + "-" + field.value("max", json()).dump() + ")"});
                }
            } else if (type == "bool") {
                if (!value.is_boolean()) out.push_back({path, "is true or false"});
            } else if (type == "range") {
                if (!(value.is_array() && value.size() == 2 && value[0].is_number() && value[1].is_number()) && !value.is_number()) {
                    out.push_back({path, "is a range [low, high]"});
                } else if (value.is_array() && value[0].get<double>() > value[1].get<double>()) {
                    out.push_back({path, "runs high to low"});
                }
            } else if (type == "list") {
                if (!value.is_array()) out.push_back({path, "is a list"});
                else if (field.contains("maxItems") && value.size() > field["maxItems"].get<size_t>()) {
                    out.push_back({path, "has more than " + field["maxItems"].dump() + " items", true});
                }
            } else if (type == "text") {
                if (!value.is_string()) out.push_back({path, "is text"});
                else if (field.contains("max") && value.get<std::string>().size() > field["max"].get<size_t>()) {
                    out.push_back({path, "is too long", true});
                }
            }
        }
    }
    const json levels = BriefGet(brief, "difficulty.levels");
    if (levels.is_array() && (levels.empty() || levels.size() > 7)) out.push_back({"difficulty.levels", "has 1 to 7 levels"});
    return out;
}

std::vector<std::string> RepairGameBrief(json& brief, const StoryOntology& gso, const json& projectDefaults) {
    std::vector<std::string> out;
    std::vector<std::pair<std::string, json>> removed;
    for (const auto& tab : PlannerData::Engine().briefSchema.value("tabs", json::array())) {
        for (const auto& field : tab.value("fields", json::array())) {
            const std::string path = field.value("path", ""), type = field.value("type", "text");
            const json value = BriefGet(brief, path);
            if (value.is_null()) continue;
            const auto options = FieldOptions(field, gso);
            if (type == "enum" && !options.empty() && (!value.is_string() || !Contains(options, value.get<std::string>()))) {
                removed.push_back({path, value});
                BriefSet(brief, path, nullptr);
            } else if (type == "enumSet" && !options.empty() && value.is_array()) {
                json kept = json::array();
                for (const auto& v : value) {
                    if (v.is_string() && Contains(options, v.get<std::string>())) kept.push_back(v);
                }
                if (kept.size() != value.size()) {
                    out.push_back(path + ": dropped what isn't an option (" + value.dump() + " → " + kept.dump() + ")");
                    BriefSet(brief, path, kept.empty() ? json() : kept);
                    if (kept.empty()) removed.push_back({path, value});
                }
            } else if ((type == "int" || type == "number") && value.is_number() &&
                       ((field.contains("min") && value.get<double>() < field["min"].get<double>()) ||
                        (field.contains("max") && value.get<double>() > field["max"].get<double>()))) {
                const double clamped = std::clamp(value.get<double>(), field.value("min", value.get<double>()), field.value("max", value.get<double>()));
                out.push_back(path + ": " + value.dump() + " → " + json(clamped).dump());
                BriefSet(brief, path, type == "int" ? json(static_cast<int>(std::lround(clamped))) : json(clamped));
            }
        }
    }
    if (removed.empty()) return out;
    // The profile's (or the scope table's) default in their place.
    std::vector<std::string> refilled;
    brief = CompleteGameBrief(brief, projectDefaults, &refilled);
    json assumed = brief.value("assumed", json::array());
    for (const auto& [path, was] : removed) {
        const json now = BriefGet(brief, path);
        out.push_back(path + ": " + was.dump() + " isn't an option → " + (now.is_null() ? std::string("left empty") : now.dump()));
        if (std::find(assumed.begin(), assumed.end(), path) == assumed.end()) assumed.push_back(path);
    }
    brief["assumed"] = assumed;
    return out;
}

json BriefWindow(const json& brief, const json& marks, const StoryOntology& gso) {
    const GenreProfile* profile = PlannerData::Engine().Profile(Text(brief, "genre"));
    const auto assumed = Strings(brief.value("assumed", json::array()));
    json tabs = json::array();
    for (const auto& tab : PlannerData::Engine().briefSchema.value("tabs", json::array())) {
        json fields = json::array();
        for (const auto& field : tab.value("fields", json::array())) {
            const std::string path = field.value("path", "");
            json f = {{"path", path},
                      {"label", field.value("label", path)},
                      {"type", field.value("type", "text")},
                      {"value", BriefGet(brief, path)},
                      {"asked", field.value("asked", false) || (profile && Contains(profile->asked, path))},
                      {"assumed", Contains(assumed, path)}};
            const auto options = FieldOptions(field, gso);
            if (!options.empty()) f["options"] = options;
            for (const char* k : {"min", "max"}) {
                if (field.contains(k)) f[k] = field[k];
            }
            if (marks.is_object() && marks.contains(path)) f["mark"] = marks[path];
            fields.push_back(std::move(f));
        }
        std::stable_sort(fields.begin(), fields.end(), [](const json& a, const json& b) { return a["asked"].get<bool>() > b["asked"].get<bool>(); });
        tabs.push_back({{"id", tab.value("id", "")}, {"label", tab.value("label", "")}, {"fields", fields}});
    }
    return tabs;
}

json BriefSchemaForAi(const StoryOntology& gso) {
    json fields = json::array();
    for (const auto& tab : PlannerData::Engine().briefSchema.value("tabs", json::array())) {
        if (tab.value("id", "") == "plan") continue; // the user's, not the AI's
        for (const auto& field : tab.value("fields", json::array())) {
            json f = {{"path", field.value("path", "")}, {"type", field.value("type", "text")}};
            const auto options = FieldOptions(field, gso);
            if (!options.empty()) f["options"] = options;
            fields.push_back(std::move(f));
        }
    }
    json genres = json::array();
    for (const auto& [id, p] : PlannerData::Engine().profiles) genres.push_back({{"id", id}, {"label", p.label}});
    return {{"fields", fields}, {"genres", genres}};
}

// --- Stage documents ------------------------------------------------------------------------

std::string PlanSlug(const std::string& id) {
    const size_t slash = id.find_last_of('/');
    return slash == std::string::npos ? id : id.substr(slash + 1);
}

std::vector<std::string> RepairOutline(json& outline, const StoryOntology& gso) {
    std::vector<std::string> out;
    if (!outline.is_object() || !outline.contains("cast") || !outline["cast"].is_array()) return out;
    auto& cast = outline["cast"];
    for (size_t i = 0; i < cast.size(); ++i) {
        json& c = cast[i];
        if (!c.is_object()) continue;
        const std::string where = "cast[" + std::to_string(i) + "]";
        json narrative = json::array(), gameplay = json::array();
        auto add = [](json& list, const std::string& r) {
            if (std::find(list.begin(), list.end(), r) == list.end()) list.push_back(r);
        };
        // Each role to the list it belongs to; what's in neither, dropped.
        auto sort = [&](const json& roles, bool listedNarrative) {
            if (!roles.is_array()) return;
            for (const auto& role : roles) {
                if (!role.is_string()) continue;
                const std::string r = role.get<std::string>();
                const bool isNarrative = gso.InVocabulary("NarrativeRole", r), isGameplay = gso.InVocabulary("GameplayRole", r);
                if (listedNarrative ? isNarrative : isGameplay) add(listedNarrative ? narrative : gameplay, r);
                else if (isGameplay) add(gameplay, r), out.push_back(where + ": '" + r + "' is a gameplay role");
                else if (isNarrative) add(narrative, r), out.push_back(where + ": '" + r + "' is a narrative role");
                else out.push_back(where + ": dropped '" + r + "' (not a role)");
            }
        };
        sort(c.value("narrativeRoles", json::array()), true);
        sort(c.value("gameplayRoles", json::array()), false);
        c["narrativeRoles"] = narrative;
        c["gameplayRoles"] = gameplay;
    }
    return out;
}

std::vector<PlanProblem> ValidateOutline(const json& outline, const json& brief, const StoryOntology& gso,
                                         const std::vector<std::string>& buildTypes) {
    std::vector<PlanProblem> out;
    const json story = outline.value("story", json::object());
    if (Text(story, "logline").empty()) out.push_back({"story.logline", "is empty"});
    if (Text(story, "synopsis").empty()) out.push_back({"story.synopsis", "is empty", true});
    const std::string structure = Text(story, "structure");
    const auto acts = story.value("acts", json::array());
    if (acts.empty()) out.push_back({"story.acts", "the story has no acts"});
    std::set<std::string> actIds;
    for (size_t i = 0; i < acts.size(); ++i) {
        const std::string id = Text(acts[i], "id");
        if (id.empty() || !actIds.insert(id).second) out.push_back({"story.acts[" + std::to_string(i) + "]", "needs a unique id"});
        if (Text(acts[i], "summary").empty()) out.push_back({"story.acts[" + std::to_string(i) + "].summary", "is empty"});
    }
    auto t = gso.templates.find(structure.empty() ? Text(brief, "structure") : structure);
    if (t != gso.templates.end() && !acts.empty() && acts.size() != t->second.acts.size()) {
        out.push_back({"story.acts", "the " + t->first + " template has " + std::to_string(t->second.acts.size()) + " acts", true});
    }
    if (story.value("endings", json::array()).empty()) out.push_back({"story.endings", "the story needs an ending"});

    // The cast.
    const auto cast = outline.value("cast", json::array());
    if (cast.empty()) out.push_back({"cast", "there are no characters"});
    std::set<std::string> ids;
    std::set<std::string> roles;
    int players = 0;
    for (size_t i = 0; i < cast.size(); ++i) {
        const std::string where = "cast[" + std::to_string(i) + "]";
        const json& c = cast[i];
        const std::string id = Text(c, "id");
        if (id.rfind("character/", 0) != 0) out.push_back({where + ".id", "'" + id + "' should be character/<name>"});
        if (!ids.insert(id).second) out.push_back({where + ".id", id + " appears twice"});
        if (Text(c, "name").empty()) out.push_back({where + ".name", "is empty"});
        if (!gso.InVocabulary("Importance", Text(c, "importance"))) out.push_back({where + ".importance", "is major, supporting, minor or extra"});
        for (const auto& r : Strings(c.value("narrativeRoles", json::array()))) {
            if (!gso.InVocabulary("NarrativeRole", r)) out.push_back({where + ".narrativeRoles", "'" + r + "' isn't a narrative role"});
            roles.insert(r);
        }
        for (const auto& r : Strings(c.value("gameplayRoles", json::array()))) {
            if (!gso.InVocabulary("GameplayRole", r)) out.push_back({where + ".gameplayRoles", "'" + r + "' isn't a gameplay role"});
        }
        players += c.value("player", false) ? 1 : 0;
    }
    if (players != 1) out.push_back({"cast", "exactly one character is the player (\"player\": true)"});
    if (t != gso.templates.end()) {
        for (const auto& role : t->second.roles) {
            if (!roles.contains(role)) out.push_back({"cast", "nobody fills the " + role + " role (R-K1)"});
        }
    }
    for (size_t i = 0; i < outline.value("factions", json::array()).size(); ++i) {
        const std::string id = Text(outline["factions"][i], "id");
        if (id.rfind("faction/", 0) != 0) out.push_back({"factions[" + std::to_string(i) + "].id", "should be faction/<name>"});
        ids.insert(id);
    }

    // Places and the world graph.
    const auto places = outline.value("places", json::array());
    if (places.empty()) out.push_back({"places", "there are no places"});
    std::set<std::string> placeIds;
    for (const auto& p : places) placeIds.insert(Text(p, "id"));
    for (size_t i = 0; i < places.size(); ++i) {
        const std::string where = "places[" + std::to_string(i) + "]";
        const std::string id = Text(places[i], "id");
        if (id.rfind("location/", 0) != 0) out.push_back({where + ".id", "'" + id + "' should be location/<name>"});
        if (!buildTypes.empty() && !Contains(buildTypes, Text(places[i], "kind"))) {
            out.push_back({where + ".kind", "'" + Text(places[i], "kind") + "' isn't a build type (" + Join(buildTypes) + ")"});
        }
        const std::string parent = Text(places[i], "parent");
        if (!parent.empty() && !placeIds.contains(parent)) out.push_back({where + ".parent", "no place " + parent});
    }
    std::map<std::string, std::set<std::string>> links;
    for (size_t i = 0; i < outline.value("connections", json::array()).size(); ++i) {
        const json& c = outline["connections"][i];
        const std::string from = Text(c, "from"), to = Text(c, "to");
        if (!placeIds.contains(from) || !placeIds.contains(to)) {
            out.push_back({"connections[" + std::to_string(i) + "]", "joins " + from + " and " + to + ", which aren't both places"});
            continue;
        }
        links[from].insert(to);
        links[to].insert(from);
    }
    for (const auto& p : places) {
        const std::string parent = Text(p, "parent");
        if (!parent.empty()) {
            links[parent].insert(Text(p, "id"));
            links[Text(p, "id")].insert(parent);
        }
    }
    if (!places.empty()) {
        std::set<std::string> seen{Text(places[0], "id")};
        std::vector<std::string> todo{Text(places[0], "id")};
        while (!todo.empty()) {
            const std::string x = todo.back();
            todo.pop_back();
            for (const auto& y : links[x]) {
                if (seen.insert(y).second) todo.push_back(y);
            }
        }
        for (const auto& id : placeIds) {
            if (!seen.contains(id)) out.push_back({"connections", id + " can't be reached from " + Text(places[0], "id") + " (R-P2)"});
        }
    }
    return out;
}

std::vector<PlanProblem> ValidateSceneCard(const json& card, const StoryOntology& gso, const std::string& where) {
    std::vector<PlanProblem> out;
    if (Text(card, "id").empty()) out.push_back({where + ".id", "is empty"});
    if (!gso.InVocabulary("SceneMode", Text(card, "mode"))) out.push_back({where + ".mode", "'" + Text(card, "mode") + "' isn't a scene mode"});
    if (card.contains("value") && !gso.InVocabulary("Value", Text(card, "value"))) out.push_back({where + ".value", "'" + Text(card, "value") + "' isn't a value", true});
    for (const char* k : {"chargeBefore", "chargeAfter"}) {
        if (card.contains(k) && (!card[k].is_number() || std::abs(card[k].get<double>()) > 1.0)) out.push_back({where + "." + k, "is -1 to 1"});
    }
    if (Text(card, "goal").empty()) out.push_back({where + ".goal", "is empty", true});
    if (Text(card, "location").empty() && Text(card, "mode") != "ambient") out.push_back({where + ".location", "is empty", true});
    return out;
}

std::vector<PlanProblem> ValidateActPlan(const json& act, const json& outline, const StoryOntology& gso) {
    std::vector<PlanProblem> out;
    const auto chapters = act.value("chapters", json::array());
    if (chapters.empty()) out.push_back({"chapters", "the act has no chapters"});
    std::set<std::string> known, scenes;
    for (const auto& c : outline.value("cast", json::array())) known.insert(Text(c, "id"));
    for (const auto& p : outline.value("places", json::array())) known.insert(Text(p, "id"));
    for (size_t i = 0; i < chapters.size(); ++i) {
        const std::string where = "chapters[" + std::to_string(i) + "]";
        if (Text(chapters[i], "id").empty()) out.push_back({where + ".id", "is empty"});
        const auto cards = chapters[i].value("scenes", json::array());
        if (cards.empty()) out.push_back({where + ".scenes", "the chapter has no scenes"});
        for (size_t j = 0; j < cards.size(); ++j) {
            const std::string w = where + ".scenes[" + std::to_string(j) + "]";
            for (auto& p : ValidateSceneCard(cards[j], gso, w)) out.push_back(std::move(p));
            if (!scenes.insert(Text(cards[j], "id")).second) out.push_back({w + ".id", Text(cards[j], "id") + " appears twice"});
            const std::string loc = Text(cards[j], "location");
            if (!loc.empty() && !known.contains(loc)) out.push_back({w + ".location", loc + " isn't in the outline (it will be added as a stub)", true});
        }
    }
    return out;
}

std::vector<PlanProblem> ValidateSceneDoc(const json& scene, const StoryOntology& gso) {
    std::vector<PlanProblem> out;
    if (Text(scene, "summary").empty()) out.push_back({"summary", "is empty"});
    if (scene.value("beats", json::array()).empty()) out.push_back({"beats", "the scene has no beats", true});
    for (size_t i = 0; i < scene.value("events", json::array()).size(); ++i) {
        const json& e = scene["events"][i];
        const std::string where = "events[" + std::to_string(i) + "]";
        if (Text(e, "id").empty()) out.push_back({where + ".id", "is empty"});
        const std::string cls = Text(e, "class").empty() ? "Event" : Text(e, "class");
        if (!gso.IsA(cls, "Event")) out.push_back({where + ".class", "'" + cls + "' isn't an event class"});
    }
    for (size_t i = 0; i < scene.value("facts", json::array()).size(); ++i) {
        const json& f = scene["facts"][i];
        const std::string where = "facts[" + std::to_string(i) + "]";
        if (!gso.Property(Text(f, "p"))) out.push_back({where + ".p", "no property '" + Text(f, "p") + "'"});
        if (Text(f, "s").empty()) out.push_back({where + ".s", "is empty"});
    }
    for (size_t i = 0; i < scene.value("newEntities", json::array()).size(); ++i) {
        const json& n = scene["newEntities"][i];
        const std::string where = "newEntities[" + std::to_string(i) + "]";
        if (!gso.Class(Text(n, "class"))) out.push_back({where + ".class", "no class '" + Text(n, "class") + "'"});
        if (Text(n, "id").find('/') == std::string::npos) out.push_back({where + ".id", "should be <kind>/<name>"});
    }
    if (scene.contains("questStage") && scene["questStage"].is_object()) {
        const json& q = scene["questStage"];
        if (!q.value("index", json()).is_number_integer()) out.push_back({"questStage.index", "is a whole number (10, 20, ...)"});
        if (Text(q, "journal").empty()) out.push_back({"questStage.journal", "is empty", true});
    }
    return out;
}

std::vector<PlanProblem> ValidateCharacterDoc(const json& c, const StoryOntology& gso, const std::vector<std::string>& packageTemplates) {
    std::vector<PlanProblem> out;
    if (Text(c, "name").empty()) out.push_back({"name", "is empty"});
    const json bio = c.value("biography", json::object());
    for (const auto& m : Strings(bio.value("motivations", json::array()))) {
        if (!gso.InVocabulary("Motivation", m)) out.push_back({"biography.motivations", "'" + m + "' isn't a motivation"});
    }
    for (const auto& t : bio.value("personality", json::array())) {
        const std::string trait = t.is_array() && !t.empty() && t[0].is_string() ? t[0].get<std::string>() : t.is_string() ? t.get<std::string>() : "";
        if (!gso.InVocabulary("Trait", trait)) out.push_back({"biography.personality", "'" + trait + "' isn't a trait", true});
    }
    if (Text(c, "importance") == "major") {
        for (const char* k : {"need", "desire"}) {
            if (Text(bio, k).empty()) out.push_back({std::string("biography.") + k, "a major character needs one", true});
        }
    }
    for (size_t i = 0; i < c.value("relationships", json::array()).size(); ++i) {
        const json& r = c["relationships"][i];
        if (!gso.InVocabulary("RelationshipType", Text(r, "type"))) {
            out.push_back({"relationships[" + std::to_string(i) + "].type", "'" + Text(r, "type") + "' isn't a relationship type"});
        }
        if (Text(r, "with").empty()) out.push_back({"relationships[" + std::to_string(i) + "].with", "is empty"});
    }
    const json behaviour = c.value("behaviour", json::object());
    for (size_t i = 0; i < behaviour.value("packages", json::array()).size(); ++i) {
        const std::string tmpl = Text(behaviour["packages"][i], "template");
        if (!packageTemplates.empty() && !Contains(packageTemplates, tmpl)) {
            out.push_back({"behaviour.packages[" + std::to_string(i) + "].template", "'" + tmpl + "' isn't a package template (" + Join(packageTemplates) + ")"});
        }
    }
    return out;
}

std::vector<PlanProblem> ValidateQuestDesign(const json& q, const StoryOntology& gso, const std::vector<std::string>& knownIds) {
    std::vector<PlanProblem> out;
    static const std::vector<std::string> types = {"main", "side", "faction", "companion", "repeatable", "misc"};
    static const std::vector<std::string> kinds = {"character", "item", "location", "spot", "container"};
    if (Text(q, "id").empty()) out.push_back({"id", "is empty"});
    if (Text(q, "name").empty()) out.push_back({"name", "is empty"});
    if (!Contains(types, Text(q, "type"))) out.push_back({"type", "is one of " + Join(types)});
    const std::string motivation = Text(q, "motivation");
    if (!motivation.empty() && !gso.InVocabulary("Motivation", motivation)) out.push_back({"motivation", "'" + motivation + "' isn't a motivation"});
    if (!motivation.empty() && !Text(q, "strategy").empty()) {
        auto s = gso.strategies.find(motivation);
        if (s != gso.strategies.end() && !Contains(s->second, Text(q, "strategy"))) {
            out.push_back({"strategy", "'" + Text(q, "strategy") + "' isn't a strategy for " + motivation + " (" + Join(s->second) + ")", true});
        }
    }
    std::set<std::string> aliases;
    for (size_t i = 0; i < q.value("aliases", json::array()).size(); ++i) {
        const json& a = q["aliases"][i];
        const std::string where = "aliases[" + std::to_string(i) + "]";
        if (!aliases.insert(Text(a, "id")).second || Text(a, "id").empty()) out.push_back({where + ".id", "needs a unique id"});
        if (!Contains(kinds, Text(a, "kind"))) out.push_back({where + ".kind", "is one of " + Join(kinds)});
        const std::string fill = Text(a, "fill");
        if (fill.empty() && Text(a, "query").empty() && Text(a, "in").empty()) out.push_back({where, "needs a fill, a query or a place it's in (R-Q2)"});
        if (!fill.empty() && !knownIds.empty() && !Contains(knownIds, fill)) out.push_back({where + ".fill", "no " + fill + " in the story"});
        if (!Text(a, "in").empty() && !knownIds.empty() && !Contains(knownIds, Text(a, "in"))) out.push_back({where + ".in", "no " + Text(a, "in") + " in the story"});
    }
    const auto stages = q.value("stages", json::array());
    if (stages.empty()) out.push_back({"stages", "the quest has no stages"});
    int last = -1;
    bool terminal = false;
    for (size_t i = 0; i < stages.size(); ++i) {
        const json& s = stages[i];
        const std::string where = "stages[" + std::to_string(i) + "]";
        if (!s.value("index", json()).is_number_integer() || s["index"].get<int>() <= last) {
            out.push_back({where + ".index", "stage numbers rise (10, 20, ...)"});
        } else {
            last = s["index"].get<int>();
        }
        terminal = terminal || s.value("terminal", json()).is_string();
        for (size_t j = 0; j < s.value("objectives", json::array()).size(); ++j) {
            const json& o = s["objectives"][j];
            const std::string w = where + ".objectives[" + std::to_string(j) + "]";
            if (!gso.InVocabulary("ObjectiveAction", Text(o, "action"))) out.push_back({w + ".action", "'" + Text(o, "action") + "' isn't an objective action"});
            const std::string target = Text(o, "target");
            if (!target.empty() && !aliases.contains(target)) out.push_back({w + ".target", "'" + target + "' isn't one of the quest's aliases"});
        }
    }
    if (!terminal) out.push_back({"stages", "no stage completes the quest (\"terminal\": \"completed\")", true});
    const json giver = q.value("giver", json::object());
    if (giver.is_object() && !Text(giver, "alias").empty() && !aliases.contains(Text(giver, "alias"))) {
        out.push_back({"giver.alias", "isn't one of the quest's aliases"});
    }
    return out;
}

std::vector<PlanProblem> ValidatePlaceDesc(const json& place, const Ontology& places) {
    std::vector<PlanProblem> out;
    if (Text(place, "id").rfind("location/", 0) != 0) out.push_back({"id", "should be location/<name>"});
    if (Text(place.value("description", json::object()), "purpose").empty()) out.push_back({"description.purpose", "is empty", true});
    const json brief = place.value("brief", json::object());
    if (!brief.is_object() || brief.empty()) {
        out.push_back({"brief", "the place has no Brief"});
        return out;
    }
    if (Text(brief, "buildType") != Text(place, "kind")) {
        out.push_back({"brief.buildType", "is '" + Text(brief, "buildType") + "' but the place is a " + Text(place, "kind")});
    }
    for (const auto& p : ValidateBrief(places, brief)) out.push_back({"brief." + p.where, p.message, p.warning});
    return out;
}

const std::vector<std::string>& PlanHudWidgets() {
    static const std::vector<std::string> widgets = {"health", "mana", "stamina", "xp", "gold", "quickslots", "quest-tracker",
                                                     "minimap", "compass", "clock", "boss-bar", "notifications", "interaction-prompt"};
    return widgets;
}

const std::vector<std::string>& PlanWindows() {
    static const std::vector<std::string> windows = {"title", "pause", "settings", "save-load", "game-over", "credits", "inventory",
                                                     "journal", "map", "spellbook", "character", "shop", "dialogue", "crafting",
                                                     "relationships"};
    return windows;
}

std::vector<PlanProblem> ValidateUiPlan(const json& ui, const std::vector<std::string>& themes, const std::vector<std::string>& screens,
                                        const std::vector<std::string>& widgets) {
    std::vector<PlanProblem> out;
    if (!themes.empty() && !Contains(themes, Text(ui, "theme"))) out.push_back({"theme", "'" + Text(ui, "theme") + "' isn't a theme (" + Join(themes) + ")"});
    for (const auto& s : Strings(ui.value("screens", json::array()))) {
        if (!Contains(screens, s)) out.push_back({"screens", "'" + s + "' isn't a screen or window (" + Join(screens) + ")"});
    }
    for (const auto& w : Strings(ui.value("hud", json::array()))) {
        if (!Contains(widgets, w)) out.push_back({"hud", "'" + w + "' isn't a HUD widget (" + Join(widgets) + ")"});
    }
    return out;
}

} // namespace aige
