#include <gtest/gtest.h>

#include "PlannerFixture.h"

#include "assets/Project.h"
#include "assets/SceneFiles.h"
#include "assets/SceneIO.h"
#include "gamebuild/GameBuild.h"
#include "gameplay/Components.h"
#include "physics2d/Components.h"
#include "gameplay/Settings.h"
#include "gameplay/Story.h"

#include <fstream>

// development_plan.md Phase 23: the AI Planner builds the game it designed — every place built
// automatically (the place builder's own fallbacks standing in for its AI here), scenes
// connected by gateways, characters placed, quests bound, the game's files written, a
// playtest, the result.
using aige::GameBuild;
using nlohmann::json;
namespace fs = std::filesystem;
using namespace planner_fixture;

namespace {

json ReadJson(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return json::parse(in, nullptr, false);
}

// The designed game, built: place builds run without the AI (their fallbacks), dialogue from the script.
struct Builder {
    Planner p;
    std::unique_ptr<GameBuild> build;
    explicit Builder(const std::string& name) : p(name) {
        p.ThroughOutline();
        p.service.Drive(*p.plan);
    }
    ~Builder() { aige::ActiveSettings().game.startScene.clear(); } // writing the game sets it
    GameBuild::Host Host() {
        GameBuild::Host host;
        host.projectRoot = p.root;
        host.places = []() -> const aige::Ontology& { return Places(); };
        host.autoApprove = [] { return true; };
        host.request = [this](const std::string& path, const json& body, int) {
            if (path.rfind("/places/", 0) == 0) return 0; // the place builder's own fallbacks
            p.service.outstanding.push_back({p.service.next, path, body});
            p.service.paths.push_back(path);
            return p.service.next++;
        };
        return host;
    }
    void Run(int limit = 400) {
        while (limit-- > 0) {
            bool did = build->Pump();
            while (!p.service.outstanding.empty()) {
                const auto r = p.service.outstanding.front();
                p.service.outstanding.pop_front();
                build->OnResponse(r.id, Answer(r.path, r.body));
                did = true;
            }
            if (!did) break;
        }
    }
};

} // namespace

TEST(GameBuildTest, QuestsFromTheDesignBecomeEngineQuests) {
    const json design = json::parse(R"({"id": "q-main", "name": "The Moon Amulet", "giver": {"alias": "giver"},
      "aliases": [{"id": "giver", "kind": "character", "fill": "character/tamsin"},
                  {"id": "hiding-spot", "kind": "spot", "in": "location/crypt", "where": {"anchor": "altar"}}],
      "stages": [{"index": 10, "journal": "Take it back.", "objectives": [{"action": "reach", "target": "hiding-spot"}]},
                 {"index": 100, "terminal": "completed"}],
      "rewards": [{"xp": 300}, {"item": "warden-blade"}]})");
    const json bindings = {{"aliases", {{{"quest", "q-main"}, {"alias", "hiding-spot"}, {"marker", "q-main/hiding-spot"}}}}};
    const json quest = aige::EngineQuestFromDesign(design, bindings);
    std::string error;
    const auto parsed = aige::QuestFromJson(quest, "q-main", &error);
    ASSERT_TRUE(parsed) << error;
    EXPECT_EQ(parsed->giver, "character/tamsin");
    ASSERT_EQ(parsed->stages.size(), 2u);
    EXPECT_EQ(parsed->stages[0].objectives[0].type, "reach");
    EXPECT_EQ(parsed->Resolve(parsed->stages[0].objectives[0].target), "q-main/hiding-spot");
    EXPECT_EQ(parsed->stages[1].ends, "complete");
    EXPECT_EQ(parsed->rewards.experience, 300.0f);
    EXPECT_EQ(parsed->rewards.items, "warden-blade");
}

TEST(GameBuildTest, ObjectivesCountWhatPopulateMadeForTheirRoles) {
    // ⑪ made the ledger an item lying in the graveyard, the knot a thing to use, the skeleton the quest's own enemy:
    // collecting the ledger is having it, it's delivered to whom the stage talks to, the knot is used by its name, the
    // skeleton is defeated by its key; the story's staging is added as the stage begins.
    const json design = json::parse(R"({"id": "q-main", "name": "Ledger", "giver": {"alias": "sella"},
      "aliases": [{"id": "sella", "kind": "character", "fill": "character/sella"},
                  {"id": "lysa", "kind": "character", "fill": "character/lysa"},
                  {"id": "ledger", "kind": "item"}, {"id": "knot", "kind": "spot"},
                  {"id": "skeleton", "kind": "character", "query": "A skeleton"}],
      "stages": [{"index": 10, "objectives": [{"action": "kill", "target": "skeleton"}, {"action": "interact", "target": "knot"},
                                              {"action": "collect", "target": "ledger"}]},
                 {"index": 20, "objectives": [{"action": "deliver", "target": "ledger"}, {"action": "talk", "target": "lysa"}]}]})");
    const json bindings = {{"aliases", json::array({
        {{"quest", "q-main"}, {"alias", "ledger"}, {"marker", "q-main/ledger"}, {"item", "ledger"}},
        {{"quest", "q-main"}, {"alias", "knot"}, {"marker", "q-main/knot"}, {"thing", "Knot"}},
        {{"quest", "q-main"}, {"alias", "skeleton"}, {"marker", "q-main/skeleton"}, {"entity", "q-main/skeleton"}}})}};
    const json staging = {{"20", json::array({{{"op", "move_actor"}, {"who", "character/lysa"}, {"scene", "scenes/yard.aige"}, {"marker", "story/s2/lysa"}}})}};
    const json quest = aige::EngineQuestFromDesign(design, bindings, {}, staging);
    const json& first = quest["stages"][0]["objectives"];
    EXPECT_EQ(first[0]["type"], "defeat");
    EXPECT_EQ(first[0]["target"], "q-main/skeleton");
    EXPECT_EQ(first[1]["type"], "use");
    EXPECT_EQ(first[1]["target"], "Knot");
    EXPECT_EQ(first[2]["type"], "have");
    EXPECT_EQ(first[2]["target"], "ledger");
    const json& second = quest["stages"][1];
    EXPECT_EQ(second["objectives"][0]["type"], "deliver");
    EXPECT_EQ(second["objectives"][0]["target"], "@lysa");
    EXPECT_EQ(second["objectives"][0]["item"], "ledger");
    ASSERT_EQ(second["onEnter"].size(), 1u);
    EXPECT_EQ(second["onEnter"][0]["marker"], "story/s2/lysa");
    std::string error;
    EXPECT_TRUE(aige::QuestFromJson(quest, "q-main", &error)) << error;
}

TEST(GameBuildTest, TheDesignedGameIsBuiltBoundWrittenAndPlaytested) {
    Builder b("build");
    ASSERT_EQ(b.p.plan->Status(), "done");
    b.build = std::make_unique<GameBuild>(b.Host());
    ASSERT_EQ(b.build->Start("automatic"), "");
    b.Run();
    ASSERT_EQ(b.build->Status(), "done") << b.build->State()["message"] << "\n" << b.build->State()["rows"].dump(1);
    const json state = b.build->State();
    // ⑩ every place has a scene; the start scene has the player; gateways connect them.
    ASSERT_EQ(state["rows"].size(), 3u);
    const aige::Project project{b.p.root};
    for (const auto& row : state["rows"]) {
        EXPECT_TRUE(row["status"] == "built" || row["status"] == "placeholder") << row.dump();
        EXPECT_TRUE(fs::exists(project.Resolve(row["scene"].get<std::string>()))) << row["scene"];
    }
    EXPECT_TRUE(aige::GatewayProblems(project).empty()) << aige::GatewayProblems(project).front();
    aige::Registry riverford;
    ASSERT_TRUE(aige::LoadScene(riverford, project.Resolve("scenes/riverford.aige").string()));
    auto players = riverford.view<aige::Velocity2D, aige::Collider2D, aige::Health>();
    const bool player = players.begin() != players.end();
    EXPECT_TRUE(player); // the player's there
    int gateways = 0;
    for (auto [e, g] : riverford.view<aige::Gateway>().each()) ++gateways;
    for (auto [e, i] : riverford.view<aige::Interior>().each()) gateways += i.kind == "scene" && !i.scene.empty(); // a building's door
    EXPECT_GE(gateways, 2); // to the temple and to the crypt
    // ⑪ the quest's spot is bound to the crypt's altar; Tamsin is in the temple with her dialogue.
    const json bindings = ReadJson(b.p.root / "planner" / "bindings.json");
    bool spot = false;
    for (const auto& a : bindings["aliases"]) {
        if (a["alias"] == "hiding-spot") {
            spot = true;
            EXPECT_EQ(a["how"], "story-anchor") << a.dump();
            EXPECT_EQ(a["scene"], "scenes/crypt.aige");
        }
    }
    EXPECT_TRUE(spot);
    aige::Registry temple;
    ASSERT_TRUE(aige::LoadScene(temple, project.Resolve("scenes/temple.aige").string()));
    bool tamsin = false;
    for (auto [e, talk] : temple.view<aige::Talk>().each()) tamsin = tamsin || talk.character == "character/tamsin";
    EXPECT_TRUE(tamsin);
    // ⑫ the game's files.
    for (const char* file : {"quests/q-main.json", "quests/q-herbs.json", "dialogue/tamsin.json", "game.json", "records/item/warden-blade.json"}) {
        EXPECT_TRUE(fs::exists(b.p.root / file)) << file;
    }
    EXPECT_EQ(ReadJson(b.p.root / "game.json")["startScene"], "scenes/riverford.aige");
    EXPECT_EQ(aige::ActiveSettings().game.startScene, "scenes/riverford.aige"); // and saved in the project's settings
    aige::ActiveSettings().game.startScene.clear();
    aige::StoryLibrary story(b.p.root);
    story.Refresh();
    ASSERT_NE(story.FindQuest("q-main"), nullptr);
    EXPECT_TRUE(aige::ValidateQuest(*story.FindQuest("q-main"), [&](const std::string& q) { return story.FindQuest(q); }).empty());
    // The playtest: every quest can be finished.
    const json playtest = state["playtest"];
    for (const auto& q : playtest["quests"]) EXPECT_EQ(q["result"], "completed") << q.dump();
    for (const auto& c : playtest["checks"]) EXPECT_TRUE(c["ok"].get<bool>()) << c.dump();
    // ⑬ the result.
    const json result = ReadJson(b.p.root / "planner" / "result.json");
    EXPECT_NE(result["headline"].get<std::string>().find("3 places"), std::string::npos) << result["headline"];
    EXPECT_NE(result["headline"].get<std::string>().find("2 of 2 quests can be finished"), std::string::npos) << result["headline"];
    // Every step of every build is in the decision record.
    int steps = 0;
    for (const auto& d : b.p.plan->Store().Decisions()) steps += d.actor == "planner";
    EXPECT_GT(steps, 3);
}

TEST(GameBuildTest, AFailedPlaceGetsAPlaceholderAndTheRestGoesOn) {
    Builder b("placeholder");
    // The crypt's Brief is broken: its build can't start, so it gets a placeholder room.
    auto crypt = b.p.plan->Document("places/crypt");
    crypt["brief"]["buildType"] = "sky-city";
    b.p.plan->Store().Touch("places/crypt", crypt, "test");
    b.build = std::make_unique<GameBuild>(b.Host());
    ASSERT_EQ(b.build->Start("automatic"), "");
    b.Run();
    ASSERT_EQ(b.build->Status(), "done");
    bool placeholder = false;
    const json rows = b.build->State()["rows"];
    for (const auto& row : rows) placeholder = placeholder || (row["place"] == "location/crypt" && row["status"] == "placeholder");
    EXPECT_TRUE(placeholder);
    // The quest still binds to the altar marker in the placeholder room.
    EXPECT_EQ(b.build->State()["playtest"]["quests"][0]["result"], "completed");
}

TEST(GameBuildTest, ALifeSimIsMadeTheSameWay) {
    Planner p("lifesim");
    ASSERT_EQ(p.plan->Start("a cozy top-down life-sim in a river village"), "");
    p.service.Drive(*p.plan);
    p.plan->EditBrief("genre", "life-sim"); // its profile: top-down, farming and relationships on, side quests
    const json brief = p.plan->Document("brief");
    EXPECT_EQ(brief["perspective"], "top-down");
    EXPECT_EQ(aige::BriefGet(brief, "systems.farming.on"), true);
    ASSERT_EQ(p.plan->Confirm(), "");
    p.service.Drive(*p.plan);
    ASSERT_EQ(p.plan->Approve(), "");
    p.service.Drive(*p.plan);
    ASSERT_EQ(p.plan->Status(), "done") << Findings(p.plan->Document("report"));
    GameBuild::Host host;
    host.projectRoot = p.root;
    host.places = []() -> const aige::Ontology& { return Places(); };
    host.request = [&](const std::string& path, const json& body, int) {
        if (path.rfind("/places/", 0) == 0) return 0;
        p.service.outstanding.push_back({p.service.next, path, body});
        return p.service.next++;
    };
    GameBuild build(host);
    ASSERT_EQ(build.Start(), ""); // the Brief's building: automatic
    for (int i = 0; i < 400; ++i) {
        bool did = build.Pump();
        while (!p.service.outstanding.empty()) {
            const auto r = p.service.outstanding.front();
            p.service.outstanding.pop_front();
            build.OnResponse(r.id, Answer(r.path, r.body));
            did = true;
        }
        if (!did) break;
    }
    ASSERT_EQ(build.Status(), "done");
    const json playtest = build.State()["playtest"];
    for (const auto& q : playtest["quests"]) EXPECT_EQ(q["result"], "completed") << q.dump();
    EXPECT_EQ(ReadJson(p.root / "game.json")["genre"], "life-sim");
    aige::ActiveSettings().game.startScene.clear();
}

// --- development_plan.md 32: the plan made real --------------------------------------------------------------

#include "gamebuild/PlanCast.h"
#include "gamebuild/PlanKeys.h"
#include "gameplay/Attributes.h"
#include "gameplay/Items.h"

TEST(PlanCastTest, AClassGivesItsLevelsNumbersKitAndLoot) {
    const json systems = json::parse(R"({"records": [
      {"type": "item", "id": "rusty-sword", "record": {"name": "Rusty sword", "kind": "weapon", "slot": "weapon", "damage": 2}},
      {"type": "character-class", "id": "bandit", "record": {"name": "Bandit", "role": "fighter",
        "attributes": [{"attribute": "maxHealth", "value": 12}, {"attribute": "resistShock", "value": 2}],
        "perLevel": [{"attribute": "maxHealth", "value": 4}], "combat": "melee", "damage": 3,
        "equipment": [{"slot": "weapon", "item": "rusty-sword"}], "carries": ["healing-draught*2"], "gold": 5,
        "loot": "bandit-loot", "experience": 12}},
      {"type": "character-class", "id": "storm-archer", "record": {"name": "Storm archer", "role": "archer",
        "combat": "ranged", "damage": 2, "damageType": "shock"}}]})");
    const aige::PlanRecords records(systems);
    aige::Registry registry;
    const aige::Entity bandit = registry.create();
    aige::CastSpec spec;
    spec.kind = aige::CastKind::Enemy;
    spec.level = 3;
    spec.cls = aige::ClassFor(records, "bandit", spec.kind);
    ASSERT_EQ(spec.cls, "bandit");
    const json cast = aige::ApplyCast(registry, bandit, spec, records);
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(bandit).max, 20.0f); // 12 + 4 x 2 levels
    EXPECT_FLOAT_EQ(registry.get<aige::Attributes>(bandit).resistShock, 0.9f); // a share, at most 0.9
    EXPECT_EQ(registry.get<aige::Equipment>(bandit).weapon, "rusty-sword");
    EXPECT_EQ(registry.get<aige::Inventory>(bandit).items, "healing-draught*2");
    EXPECT_EQ(registry.get<aige::Rewards>(bandit).loot, "bandit-loot");
    EXPECT_FLOAT_EQ(registry.get<aige::Rewards>(bandit).experience, 36.0f);
    EXPECT_TRUE(registry.all_of<aige::MeleeAttack>(bandit));
    EXPECT_EQ(cast["combat"], "melee");
    // A class by its words ("a storm archer of the hills"), shooting shock.
    const aige::Entity archer = registry.create();
    spec.cls = aige::ClassFor(records, "storm archer of the hills", spec.kind);
    ASSERT_EQ(spec.cls, "storm-archer");
    aige::ApplyCast(registry, archer, spec, records);
    ASSERT_TRUE(registry.all_of<aige::RangedAttack>(archer));
    EXPECT_EQ(registry.get<aige::RangedAttack>(archer).damageType, aige::DamageType::Shock);
    // No class: the kind's own numbers (a villager doesn't fight).
    const aige::Entity villager = registry.create();
    aige::CastSpec plain;
    aige::ApplyCast(registry, villager, plain, records);
    EXPECT_FALSE(registry.all_of<aige::MeleeAttack>(villager));
    EXPECT_GT(registry.get<aige::Health>(villager).max, 0.0f);
}

TEST(PlanKeysTest, PlannedKeysGoWhereTheyAreFree) {
    EXPECT_EQ(aige::NormaliseKeyName("1"), "Num1");
    EXPECT_EQ(aige::NormaliseKeyName("shift"), "LeftShift");
    EXPECT_EQ(aige::NormaliseKeyName("g"), "G");
    EXPECT_EQ(aige::NormaliseKeyName("LMB"), "MouseLeft");
    aige::InputMap map = aige::InputMap::Defaults();
    const auto done = aige::ApplyPlannedActions(map, json::parse(R"([
      {"name": "cook", "key": "g", "nearOnly": true},
      {"name": "pray", "key": "E"},
      {"name": "attack", "key": "MouseLeft"}])"));
    ASSERT_NE(map.Find("cook"), nullptr);
    EXPECT_EQ(map.Find("cook")->keys.front().key, aige::Key::G);
    EXPECT_TRUE(map.Find("cook")->nearOnly);
    ASSERT_NE(map.Find("pray"), nullptr);
    EXPECT_NE(map.Find("pray")->keys.front().key, aige::Key::E); // interact has E
    EXPECT_GT(map.Find("attack")->keys.size(), 1u);               // its gamepad button kept
    EXPECT_TRUE(aige::KeyClashes(map).empty()) << aige::KeyClashes(map).front();
    EXPECT_EQ(done.size(), 3u);
}

TEST(GameBuildTest, AQuestStageMovesSpawnsAndTurnsCharacters) {
    const json design = json::parse(R"({"id": "q-flight", "name": "Flight", "giver": {"alias": "smith"},
      "aliases": [{"id": "smith", "kind": "character", "fill": "character/garran"},
                  {"id": "capital", "kind": "location", "fill": "location/capital"},
                  {"id": "ambush", "kind": "spot", "in": "location/road"}],
      "stages": [{"index": 10, "journal": "The smith has fled.", "objectives": [{"action": "goto", "target": "capital"}],
                  "onEnter": [{"op": "move_actor", "who": "smith", "to": "capital", "anchor": "forge"},
                              {"op": "spawn", "class": "bandit", "at": "ambush", "count": 3},
                              {"op": "set_hostile", "who": "smith"}]},
                 {"index": 100, "terminal": "completed"}]})");
    const json bindings = {{"aliases", {{{"quest", "q-flight"}, {"alias", "ambush"}, {"marker", "q-flight/ambush"}}}}};
    const json quest = aige::EngineQuestFromDesign(design, bindings, [](const std::string& place) {
        return place == "location/capital" ? std::string("scenes/capital.aige") : std::string();
    });
    const json onEnter = quest["stages"][0]["onEnter"];
    ASSERT_EQ(onEnter.size(), 3u);
    EXPECT_EQ(onEnter[0]["who"], "character/garran");
    EXPECT_EQ(onEnter[0]["scene"], "scenes/capital.aige");
    EXPECT_EQ(onEnter[0]["marker"], "forge");
    EXPECT_EQ(onEnter[1]["prefab"], "prefabs/enemies/bandit.prefab");
    EXPECT_EQ(onEnter[1]["marker"], "q-flight/ambush");
    std::string error;
    const auto parsed = aige::QuestFromJson(quest, "q-flight", &error);
    ASSERT_TRUE(parsed) << error;
    ASSERT_EQ(parsed->stages[0].onEnter.size(), 3u);
    EXPECT_EQ(parsed->stages[0].onEnter[0].op, "move_actor");
    EXPECT_TRUE(aige::ValidateQuest(*parsed, [](const std::string&) { return nullptr; }).empty())
        << aige::ValidateQuest(*parsed, [](const std::string&) { return nullptr; }).front();
}
