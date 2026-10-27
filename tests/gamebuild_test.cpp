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
