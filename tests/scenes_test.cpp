#include <gtest/gtest.h>

#include "assets/ProjectMove.h"
#include "assets/SceneFiles.h"
#include "assets/SceneIO.h"
#include "gameplay/Archetypes.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/Components.h"
#include "gameplay/GameSession.h"
#include "gameplay/Settings.h"
#include "physics2d/Components.h"
#include "scripting/LuaVM.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

// development_plan.md 5.3: several scenes, connected by gateways.
namespace {

struct TempProject {
    TempProject() {
        project.root = fs::temp_directory_path() /
                       ("aige_scenes_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(project.root);
        fs::create_directories(project.root / "scenes");
        savedStart = aige::ActiveSettings().game.startScene;
        aige::ActiveSettings().game.startScene = "scenes/main.aige";
    }
    ~TempProject() {
        aige::ActiveSettings().game.startScene = savedStart;
        fs::remove_all(project.root);
    }
    // A scene with a floor, the player, a marker and (optionally) a gateway.
    void WriteScene(const std::string& scene, const std::string& marker, const std::string& gatewayTo = {},
                    const std::string& gatewayMarker = {}) {
        aige::Registry registry;
        for (int x = 0; x < 6; ++x)
            for (int y = 0; y < 6; ++y) aige::CreateTile(registry, x, y, aige::TileKind::Grass);
        aige::CreatePlayer(registry, 1.5f, 1.5f);
        aige::Entity m = registry.create();
        registry.emplace<aige::Marker>(m, marker);
        registry.emplace<aige::WorldTransform2D>(m, 4.5f, 2.5f);
        if (!gatewayTo.empty()) aige::CreateGateway(registry, 3.5f, 3.5f, gatewayTo, gatewayMarker);
        aige::SaveScene(registry, project.Resolve(scene).string());
    }
    std::string Read(const std::string& file) {
        std::ifstream in(project.Resolve(file), std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        return text.str();
    }
    aige::Project project;
    std::string savedStart;
};

} // namespace

TEST(ScenesTest, CreateDuplicateAndListScenes) {
    TempProject t;
    t.WriteScene("scenes/main.aige", "home");
    EXPECT_EQ(aige::ScenePathFor("Dark Cellar!"), "scenes/dark-cellar.aige");
    EXPECT_EQ(aige::ScenePathFor("  ...  "), "");

    auto created = aige::CreateScene(t.project, "Dark Cellar");
    ASSERT_EQ(created.error, "");
    EXPECT_EQ(created.path, "scenes/dark-cellar.aige");
    EXPECT_NE(aige::CreateScene(t.project, "dark cellar").error, ""); // never over an existing one

    auto copy = aige::DuplicateScene(t.project, "scenes/main.aige", "main 2");
    ASSERT_EQ(copy.error, "");
    EXPECT_EQ(t.Read(copy.path), t.Read("scenes/main.aige"));

    EXPECT_EQ(aige::ListScenes(t.project),
              (std::vector<std::string>{"scenes/dark-cellar.aige", "scenes/main-2.aige", "scenes/main.aige"}));
    EXPECT_EQ(aige::StartScenePath(t.project), t.project.Resolve("scenes/main.aige"));
}

TEST(ScenesTest, DeletingRefusesTheStartSceneAndScenesGatewaysLeadTo) {
    TempProject t;
    t.WriteScene("scenes/main.aige", "home", "scenes/cave.aige", "entrance");
    t.WriteScene("scenes/cave.aige", "entrance");
    t.WriteScene("scenes/attic.aige", "top");

    EXPECT_EQ(aige::SceneReferences(t.project, "scenes/cave.aige"), (std::vector<std::string>{"scenes/main.aige"}));
    EXPECT_NE(aige::DeleteScene(t.project, "scenes/main.aige"), "");    // the start scene
    EXPECT_NE(aige::DeleteScene(t.project, "scenes/cave.aige"), "");    // main's gateway leads there
    EXPECT_TRUE(fs::exists(t.project.Resolve("scenes/cave.aige")));
    EXPECT_EQ(aige::DeleteScene(t.project, "scenes/attic.aige"), "");
    EXPECT_FALSE(fs::exists(t.project.Resolve("scenes/attic.aige")));
}

TEST(ScenesTest, RenamingASceneRewritesGatewaysAndTheStartSetting) {
    TempProject t;
    t.WriteScene("scenes/main.aige", "home", "scenes/cave.aige", "entrance");
    t.WriteScene("scenes/cave.aige", "entrance", "scenes/main.aige", "home");
    std::ofstream(t.project.Resolve("settings.aige")) << "Game startScene=scenes/main.aige\n";

    auto result = aige::MoveProjectFile(t.project, "scenes/main.aige", "scenes/village.aige");
    ASSERT_EQ(result.error, "");
    EXPECT_NE(t.Read("scenes/cave.aige").find("scene=scenes/village.aige"), std::string::npos);
    EXPECT_NE(t.Read("settings.aige").find("startScene=scenes/village.aige"), std::string::npos);
    EXPECT_EQ(aige::CheckMove(t.project, "scenes/cave.aige", "prefabs/cave.aige").empty(), false);
}

TEST(ScenesTest, GatewaysThatLeadNowhereAreProblems) {
    TempProject t;
    t.WriteScene("scenes/main.aige", "home", "scenes/cave.aige", "entrance");
    EXPECT_EQ(aige::GatewayProblems(t.project).size(), 1u); // no cave yet
    t.WriteScene("scenes/cave.aige", "somewhere-else");
    auto problems = aige::GatewayProblems(t.project);
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_NE(problems[0].find("\"entrance\""), std::string::npos); // the marker is missing
    t.WriteScene("scenes/cave.aige", "entrance");
    EXPECT_TRUE(aige::GatewayProblems(t.project).empty());
    EXPECT_EQ(aige::SceneMarkers(t.project, "scenes/cave.aige"), (std::vector<std::string>{"entrance"}));
}

TEST(ScenesTest, TravelCarriesThePlayerAsTheyAreToTheMarker) {
    TempProject t;
    t.WriteScene("scenes/cave.aige", "entrance");
    aige::Registry registry;
    aige::Entity hero = aige::CreatePlayer(registry, 2.5f, 2.5f);
    registry.get<aige::Health>(hero).current = 3.0f; // hurt on the way
    registry.emplace<aige::Name>(hero, "Wren");

    // A follower in the cave pointed at the cave's own player.
    aige::Registry caveEdit;
    aige::LoadScene(caveEdit, t.project.Resolve("scenes/cave.aige").string());
    aige::Entity cavePlayer = *aige::FindPlayer(caveEdit);
    aige::Entity bat = aige::CreateEnemy(caveEdit, 5.5f, 5.5f, cavePlayer, "");
    const std::string caveText = aige::SerializeScene(caveEdit);

    EXPECT_EQ(aige::TravelToScene(registry, caveText, "entrance"), "");
    auto player = aige::FindPlayer(registry);
    ASSERT_TRUE(player);
    size_t players = 0;
    for (auto e : registry.view<aige::Velocity2D, aige::Collider2D>()) { (void)e; ++players; }
    EXPECT_EQ(players, 1u); // the cave's own player gave way
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(*player).current, 3.0f);
    EXPECT_EQ(registry.get<aige::Name>(*player).value, "Wren");
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(*player).x, 4.5f);
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(*player).y, 2.5f);
    // The bat now follows the arrival.
    (void)bat;
    bool retargeted = false;
    for (auto [e, follow] : registry.view<aige::Follow>().each()) retargeted |= follow.target == *player;
    EXPECT_TRUE(retargeted);

    // A missing marker: still travels, says so.
    EXPECT_NE(aige::TravelToScene(registry, caveText, "nowhere"), "");
    EXPECT_TRUE(aige::FindPlayer(registry));
}

TEST(ScenesTest, SteppingIntoAGatewayAsksToTravelOnlyOnTheWayIn) {
    aige::LuaVM lua;
    aige::Registry registry;
    for (int x = 0; x < 6; ++x)
        for (int y = 0; y < 6; ++y) aige::CreateTile(registry, x, y, aige::TileKind::Grass);
    aige::Entity player = aige::CreatePlayer(registry, 3.5f, 3.5f); // starts on the gateway
    aige::CreateGateway(registry, 3.5f, 3.5f, "scenes/cave.aige", "entrance");
    aige::GameSession session(registry, lua, aige::BootstrapIso());
    auto noKeys = [](aige::Key) { return false; };

    session.Advance(aige::GameSession::kFixedDt, noKeys);
    EXPECT_FALSE(session.TakeTravel()); // standing on it from the start: nothing

    registry.get<aige::WorldTransform2D>(player) = {1.5f, 1.5f}; // step off
    session.Advance(aige::GameSession::kFixedDt, noKeys);
    EXPECT_FALSE(session.TakeTravel());
    registry.get<aige::WorldTransform2D>(player) = {3.2f, 3.8f}; // and in
    session.Advance(aige::GameSession::kFixedDt, noKeys);
    auto travel = session.TakeTravel();
    ASSERT_TRUE(travel);
    EXPECT_EQ(travel->scene, "scenes/cave.aige");
    EXPECT_EQ(travel->marker, "entrance");

    // After arriving (here: the same spot), standing on a gateway doesn't fire again.
    session.OnSceneChanged();
    session.Advance(aige::GameSession::kFixedDt, noKeys);
    EXPECT_FALSE(session.TakeTravel());
}
