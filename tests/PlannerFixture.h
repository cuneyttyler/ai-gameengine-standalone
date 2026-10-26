#pragma once

// The AI Planner's tests' bench (development_plan.md 22-23): a scripted AI service answering
// every planner step for a short ARPG, "The Moon Amulet", and a planner on a temporary project.
#include "places/Ontology.h"
#include "planner/GamePlan.h"
#include "planner/PlannerDocs.h"

#include <gtest/gtest.h>

#include <deque>
#include <filesystem>
#include <memory>
#include <string>

namespace planner_fixture {

using aige::GamePlan;
using nlohmann::json;
namespace fs = std::filesystem;

inline const aige::Ontology& Places() {
    static aige::Ontology ontology = [] {
        aige::Ontology o;
        o.Load({aige::EnginePlacesDir()});
        return o;
    }();
    return ontology;
}

// The AI service, answering from a script: a short ARPG, "The Moon Amulet".
inline json Answer(const std::string& path, const json& body) {
    json a = {{"ok", true}, {"rationale", "Because the story needs it."}};
    if (path == "/planner/concept") {
        a["brief"] = {{"title", "The Moon Amulet"}, {"logline", "A disgraced warden must recover a stolen amulet before the dead wake."},
                      {"genre", "arpg"}, {"playTime", body.value("reading", "") == "generous" ? 4 : 2}, {"tone", {"dark"}},
                      {"setting", {{"world", "A river valley under a pale moon."}}}};
        a["confidence"] = {{"genre", 0.9}};
        a["decisions"] = {{{"question", "A fixed hero or one you make?"}, {"assumed", "fixed"}, {"path", "playerCharacter.kind"}}};
    } else if (path == "/planner/outline") {
        a["outline"] = json::parse(R"({
          "story": {"logline": "A disgraced warden must recover a stolen amulet.", "synopsis": "Aldric steals the amulet; Tamsin names him; the warden takes it back.",
            "structure": "three-act",
            "acts": [{"id": "act-1", "purpose": "setup", "summary": "The theft and the accusation."},
                     {"id": "act-2", "purpose": "confrontation", "summary": "Into the crypt after Aldric."},
                     {"id": "act-3", "purpose": "resolution", "summary": "The amulet comes home."}],
            "storylines": [{"id": "main", "kind": "main", "summary": "The amulet."}],
            "endings": [{"id": "end-a", "summary": "The dead sleep."}]},
          "cast": [{"id": "character/warden", "name": "The Warden", "importance": "major", "player": true, "narrativeRoles": ["protagonist"], "line": "A disgraced guard."},
                   {"id": "character/aldric", "name": "Aldric", "importance": "major", "narrativeRoles": ["antagonist"], "gameplayRoles": ["boss"], "line": "The thief."},
                   {"id": "character/tamsin", "name": "Tamsin", "importance": "supporting", "narrativeRoles": ["mentor"], "gameplayRoles": ["quest-giver"], "line": "The temple healer."},
                   {"id": "character/mara", "name": "Mara", "importance": "minor", "gameplayRoles": ["vendor"], "line": "Sells potions."}],
          "places": [{"id": "location/riverford", "name": "Riverford", "kind": "settlement", "line": "A river town."},
                     {"id": "location/temple", "name": "The Temple", "kind": "interior", "parent": "location/riverford", "line": "Tamsin's temple."},
                     {"id": "location/crypt", "name": "The Crypt", "kind": "dungeon", "line": "Where Aldric hides."}],
          "connections": [{"from": "location/riverford", "to": "location/crypt", "kind": "road"}]})");
    } else if (path == "/planner/act") {
        const std::string act = body["act"].value("id", "");
        json scenes;
        if (act == "act-1") {
            scenes = json::parse(R"([
              {"id": "a", "mode": "cutscene", "kernel": true, "beat": "inciting-incident", "goal": "Show the theft", "value": "life-death",
               "chargeBefore": 0, "chargeAfter": -1, "location": "location/riverford", "participants": ["character/warden", "character/tamsin"]},
              {"id": "b", "mode": "dialogue", "kernel": true, "goal": "Learn who stole it", "value": "truth-lie", "chargeBefore": -1, "chargeAfter": 1,
               "location": "location/temple", "participants": ["character/warden", "character/tamsin"], "needsParts": ["altar"]}])");
        } else if (act == "act-2") {
            scenes = json::parse(R"([{"id": "c", "mode": "gameplay", "kernel": true, "goal": "Defeat Aldric", "value": "life-death",
               "chargeBefore": -1, "chargeAfter": 1, "location": "location/crypt", "participants": ["character/warden", "character/aldric"]}])");
        } else {
            scenes = json::parse(R"([{"id": "d", "mode": "dialogue", "beat": "climax", "goal": "Return the amulet", "location": "location/riverford",
               "participants": ["character/warden", "character/tamsin"]}])");
        }
        a["act"] = {{"chapters", {{{"id", "ch"}, {"question", "Will the amulet come home?"}, {"summary", "..."}, {"scenes", scenes}}}}};
    } else if (path == "/planner/scene") {
        const std::string id = body.value("id", "");
        json scene = {{"summary", "Scene " + id + " happens."}, {"beats", {{{"function", "turn"}, {"text", "Something turns."}}}}};
        if (id == "s1-01") {
            scene["events"] = json::parse(R"([{"id": "theft", "class": "Action", "agent": "character/aldric", "serves": "aldric-power",
                                               "text": "Aldric steals the amulet", "kernel": true, "inciting": true}])");
            scene["newEntities"] = json::parse(R"([{"id": "item/amulet", "class": "Item", "name": "The Moon Amulet", "line": "It keeps the dead asleep."}])");
            scene["facts"] = json::parse(R"([{"p": "possesses", "s": "character/aldric", "o": "item/amulet"}])");
        } else if (id == "s1-02") {
            // As a real model wrote it (2026-10-23): "from" as a bool, a cause as a number, a fact as text.
            scene["facts"] = json::parse(R"([{"p": "knows", "s": "character/warden", "about": "aldric-stole-the-amulet", "from": false},
                                              "the warden knows"])");
            // And what real models named that isn't there (2026-10-23): an id without its kind, a
            // made-up earlier event, a conflict never set up, an item nobody introduced.
            scene["events"] = json::parse(R"([{"id": "news", "class": "Event", "text": "Word reaches the warden", "causes": [3],
                                               "paysOff": "letter-received-from-previous-scene", "resolves": "conflict/guard-demands-proof"}])");
            scene["facts"].push_back(json::parse(R"({"p": "possesses", "s": "character/warden", "o": "item/ledger"})"));
            scene["facts"].push_back(json::parse(R"({"p": "hasGoal", "s": "character/warden", "o": "goal:seek-amulet"})"));
            scene["facts"].push_back(json::parse(R"({"p": "conflictActive", "s": "character/warden"})"));
            scene["facts"].push_back(json::parse(R"({"p": "locatedAt", "s": "character/warden", "o": "location/riverford"})"));
            scene["events"].push_back(json::parse(R"({"class": "Revelation", "text": "The seal was broken from inside", "paysOff": "s1-01"})"));
            scene["newEntities"] = json::parse(R"([{"id": "Old Map", "class": "Item", "name": "The Old Map", "line": "Inked by the first wardens."}])");
        } else if (id == "s2-01") {
            scene["events"] = json::parse(R"([{"id": "fight", "class": "Action", "agent": "character/warden", "serves": "recover-amulet",
                                               "text": "The warden defeats Aldric", "kernel": true, "causes": []}])");
            scene["facts"] = json::parse(R"([{"p": "state", "s": "character/aldric", "state": "dead"},
                                              {"p": "possesses", "s": "character/warden", "o": "item/amulet"}])");
            scene["end"] = json::parse(R"([{"p": "possesses", "s": "character/aldric", "o": "item/amulet"}])");
            scene["questStage"] = {{"index", 10}, {"journal", "Take the amulet back from Aldric."}};
            // As Test2's model wrote them (2026-10-23): "from" an event id, a made-up relationship
            // with no valence, a mention of what she doesn't know yet, a move dated to an earlier scene.
            for (const char* f : {R"({"p": "knows", "s": "character/warden", "about": "aldric-is-dead", "from": "e11"})",
                                  R"({"p": "hasRelationship", "s": "character/warden", "o": "character/tamsin", "type": "skeptical-of"})",
                                  R"({"p": "mentions", "s": "character/tamsin", "about": "the-old-seal", "scene": "s2-01"})",
                                  R"({"p": "locatedAt", "s": "character/warden", "o": "location/crypt", "from": "s1-02"})"}) {
                scene["facts"].push_back(json::parse(f));
            }
        }
        a["scene"] = scene;
    } else if (path == "/planner/critic" || path == "/planner/review") {
        a["findings"] = json::array();
    } else if (path == "/planner/character") {
        const std::string id = body["entry"].value("id", "");
        const std::string slug = aige::PlanSlug(id);
        json c = {{"name", body["entry"].value("name", slug)},
                  {"line", body["entry"].value("line", "")},
                  {"biography", {{"backstory", slug + " grew up by the river."}, {"personality", {{"brave", 0.6}}}, {"need", "peace"},
                                 {"desire", "the amulet"}, {"motivations", {"protection"}}, {"arc", {{"from", "doubt"}, {"to", "resolve"}}}}},
                  {"gameplay", {{"roles", json::array()}, {"class", "c-" + slug}}},
                  {"behaviour", {{"packages", {{{"template", "work"}, {"place", "location/riverford"}}}}}},
                  {"home", id == "character/tamsin" ? "location/temple" : "location/riverford"}};
        if (id == "character/mara") c["gameplay"]["stock"] = "potions";
        if (id == "character/warden") c["behaviour"] = {{"packages", json::array()}};
        if (id == "character/tamsin") c["relationships"] = {{{"with", "character/aldric"}, {"type", "trusts"}, {"valence", 0.4}}};
        a["character"] = c;
    } else if (path == "/planner/quests") {
        a["quests"] = json::parse(R"([
          {"id": "q-main", "name": "The Moon Amulet", "type": "main", "motivation": "protection", "strategy": "recover-stolen-item",
           "giver": {"alias": "giver"}, "level": [1, 3],
           "aliases": [{"id": "giver", "kind": "character", "fill": "character/tamsin"},
                       {"id": "hiding-spot", "kind": "spot", "in": "location/crypt", "where": {"anchor": "altar"}}],
           "stages": [{"index": 10, "journal": "Take the amulet back.", "advancesScene": "s2-01",
                       "objectives": [{"action": "reach", "target": "hiding-spot"}]},
                      {"index": 100, "terminal": "completed"}],
           "rewards": [{"xp": 300}, {"item": "warden-blade"}]},
          {"id": "q-herbs", "name": "Herbs for Mara", "type": "side", "motivation": "wealth", "strategy": "gather-raw-materials",
           "giver": {"alias": "giver"}, "level": [1, 2],
           "aliases": [{"id": "giver", "kind": "character", "fill": "character/mara"}],
           "stages": [{"index": 10, "journal": "Gather herbs.", "objectives": [{"action": "gather", "target": "giver", "count": 5}]},
                      {"index": 100, "terminal": "completed"}]}])");
        // One quest per call (body.only): the main quest, the first side quest, then small errands.
        const std::string only = body.value("only", "");
        if (only == "q-main") a["quests"] = json::array({a["quests"][0]});
        else if (only == "q-side-1") a["quests"] = json::array({a["quests"][1]});
        else if (!only.empty()) a["quests"] = json::array(); // no further side quest fits: the slot stays empty
    } else if (path == "/planner/place") {
        const std::string kind = body.value("kind", "settlement");
        json place = {{"kind", kind},
                      {"description", {{"purpose", "Where the story needs it."}, {"mood", "grim"}}},
                      {"brief", {{"schema", "aige.brief/1"}, {"buildType", kind}}}};
        if (body["entry"].value("id", "") == "location/crypt") place["storyAnchors"] = {{{"id", "altar"}, {"module", "altar"}, {"for", {{"quest", "q-main"}}}}};
        if (body["entry"].value("id", "") == "location/temple") place["storyAnchors"] = {{{"id", "altar"}, {"module", "altar"}}};
        // As Test2's model wrote Briefs (2026-10-23): components the build type hasn't, an empty feature.
        place["brief"]["components"] = {{"rooms", {{"count", 4}}}, {"kind", "crypt"}, {"features", json::array()}};
        place["brief"]["features"] = json::array({""});
        a["place"] = place;
    } else if (path == "/planner/systems") {
        a["systems"] = {{"summary", {{"economy", "gold"}}}, {"records", {{{"type", "item"}, {"id", "warden-blade"}, {"record", {{"name", "Warden's Blade"}}}}}}};
    } else if (path == "/planner/dialogue") {
        json choices = json::array();
        for (const auto& q : body.value("gives", json::array())) {
            choices.push_back({{"text", "I'll help."}, {"effects", {{{"op", "start_quest"}, {"name", q.value("id", "")}}}},
                               {"conditions", {{{"quest", q.value("id", "")}, {"is", "inactive"}}}}});
        }
        choices.push_back({{"text", "Farewell."}});
        a["dialogue"] = {{"start", "greet"}, {"nodes", {{{"id", "greet"}, {"text", "Well met."}, {"choices", choices}}}}};
    } else if (path == "/planner/revise") {
        a["edits"] = {{{"doc", "characters/tamsin"}, {"pointer", "/biography/need"}, {"value", "forgiveness"}, {"reason", "asked"}}};
    } else if (path == "/planner/visuals") {
        a["ui"] = {{"theme", "stone"},
                   {"screens", {"title", "pause", "settings", "save-load", "game-over", "inventory", "journal", "map", "spellbook", "dialogue"}},
                   {"hud", {"health", "mana", "quest-tracker"}}};
    }
    return a;
}

struct FakeService {
    struct Request {
        int id;
        std::string path;
        json body;
    };
    std::deque<Request> outstanding;
    std::vector<std::string> paths; // every request, in order
    int next = 1;
    bool on = true;

    GamePlan::Host Host(const fs::path& root) {
        GamePlan::Host host;
        host.projectRoot = root;
        host.places = [] () -> const aige::Ontology& { return Places(); };
        host.catalog = {{"packageTemplates", {"work", "wander", "guard", "sleep"}},
                        {"themes", {"stone", "parchment"}},
                        {"buildTypes", {"settlement", "interior", "dungeon", "wilderness"}},
                        {"recordTypes", {{"item", {"name"}}}}};
        host.request = [this](const std::string& path, const json& body, int) {
            if (!on) return 0;
            outstanding.push_back({next, path, body});
            paths.push_back(path);
            return next++;
        };
        return host;
    }
    // Answers what's asked (and what that asks in turn) until nothing is outstanding.
    void Drive(GamePlan& plan, int limit = 1000) {
        while (!outstanding.empty() && limit-- > 0) {
            Request r = outstanding.front();
            outstanding.pop_front();
            plan.OnResponse(r.id, Answer(r.path, r.body));
        }
    }
};

struct Planner {
    fs::path root;
    FakeService service;
    std::unique_ptr<GamePlan> plan;
    explicit Planner(const std::string& name) : root(fs::temp_directory_path() / ("aige_planner_" + name)) {
        fs::remove_all(root);
        fs::create_directories(root / "world");
        plan = std::make_unique<GamePlan>(service.Host(root));
    }
    ~Planner() {
        plan.reset();
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    // ① to the end of ②, approving the outline.
    void ThroughOutline() {
        ASSERT_EQ(plan->Start("a short dark-fantasy ARPG about a disgraced warden recovering a stolen amulet"), "");
        service.Drive(*plan);
        ASSERT_EQ(plan->Status(), "waiting");
        ASSERT_EQ(plan->Confirm(), "");
        service.Drive(*plan);
        ASSERT_EQ(plan->Stage(), "outline");
        ASSERT_EQ(plan->Status(), "waiting"); // autonomy "confirm" for the outline
        ASSERT_EQ(plan->Approve(), "");
    }
};

inline std::string Findings(const json& report) {
    std::string out;
    for (const auto& f : report.value("findings", json::array())) {
        out += "  " + f.value("rule", "") + " " + f.value("severity", "") + " " + f.value("subject", "") + ": " + f.value("message", "") + "\n";
    }
    return out;
}

} // namespace planner_fixture
