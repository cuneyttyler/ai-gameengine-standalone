#include <gtest/gtest.h>

#include "ai_bridge/DryRun.h"
#include "ai_bridge/Proposal.h"
#include "assets/Prefab.h"
#include "assets/Project.h"
#include "assets/SceneIO.h"
#include "editing/Commands.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/Combat.h"
#include "gameplay/Components.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/GameSession.h"
#include "physics2d/Components.h"
#include "scripting/Components.h"
#include "scripting/LuaVM.h"

#include <filesystem>
#include <fstream>

#ifndef AIGE_SOURCE_DIR
#define AIGE_SOURCE_DIR "."
#endif

namespace fs = std::filesystem;

namespace {

// The plan's user checkpoint for 2.5 is "does the applied change actually
// behave correctly in a playtest". These tests are the automated part of
// that: they apply what the real model proposed (recorded golden answers)
// to a copy of the demo project and simulate the result.
class AiGameplayTest : public ::testing::Test {
protected:
    void SetUp() override {
        source_ = fs::path(AIGE_SOURCE_DIR) / "projects" / "demo";
        root_ = fs::temp_directory_path() /
                ("aige_ai_gameplay_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root_);
        fs::create_directories(root_);
        for (const char* dir : {"scenes", "scripts", "prefabs", "assets"}) {
            fs::copy(source_ / dir, root_ / dir, fs::copy_options::recursive);
        }
        // Note: this is the working copy, so local edits to the demo (e.g. from
        // a playtest) show up here; tests below shouldn't depend on them.
        project_ = aige::Project{root_};
        prefabs_ = std::make_unique<aige::PrefabLibrary>(project_);
        prefabs_->Refresh();
        ASSERT_TRUE(aige::LoadScene(registry_, project_.MainScenePath().string()));
        prefabs_->SyncAll(registry_);
        aige::RegisterEditingCommands(aige::EngineReflection());
    }
    void TearDown() override { fs::remove_all(root_); }

    // The final answer recorded for a golden prompt.
    nlohmann::json GoldenProposal(const std::string& name) {
        std::ifstream in(fs::path(AIGE_SOURCE_DIR) / "ai_service" / "tests" / "golden" / (name + ".json"));
        nlohmann::json record = nlohmann::json::parse(in);
        return nlohmann::json::parse(record["responses"].back().get<std::string>());
    }

    aige::ApplyOutcome ApplyAll(const nlohmann::json& proposal) {
        auto resolved = aige::ResolveProposal(proposal, registry_, aige::EngineReflection(), *prefabs_, project_);
        EXPECT_TRUE(resolved.AllValid());
        std::vector<nlohmann::json> operations;
        for (const auto& item : resolved.items) {
            operations.push_back(item.op);
        }
        return aige::ApplyOperations(operations, registry_, aige::EngineReflection(), *prefabs_, project_);
    }

    aige::Entity Named(const std::string& name) {
        for (auto [entity, n] : registry_.view<aige::Name>().each()) {
            if (n.value == name) {
                return entity;
            }
        }
        return entt::null;
    }

    fs::path source_;
    fs::path root_;
    aige::Project project_;
    std::unique_ptr<aige::PrefabLibrary> prefabs_;
    aige::LuaVM lua_; // must outlive registry_: scripts keep environments in its state
    aige::Registry registry_;
};

} // namespace

TEST_F(AiGameplayTest, BurningProposalMakesHitsBurnInPlay) {
    auto outcome = ApplyAll(GoldenProposal("burning"));
    ASSERT_TRUE(outcome.errors.empty()) << outcome.errors[0];

    aige::Entity hero = Named("Hero");
    aige::Entity skeleton = Named("Skeleton");
    ASSERT_TRUE(registry_.valid(hero) && registry_.valid(skeleton));
    ASSERT_EQ(registry_.get<aige::MeleeAttack>(hero).effect, aige::EffectKind::Burn);

    // Stage a fight: the hero next to a lone skeleton, which can't fight back
    // (so only the hero's hit and the burn change its health).
    auto& skeletonPos = registry_.get<aige::WorldTransform2D>(skeleton);
    registry_.get<aige::WorldTransform2D>(hero) = {skeletonPos.x - 0.6f, skeletonPos.y};
    registry_.remove<aige::MeleeAttack>(skeleton);
    registry_.remove<aige::Follow>(skeleton);
    auto& health = registry_.get<aige::Health>(skeleton);
    health = {50.0f, 50.0f};

    aige::GameSession session(registry_, lua_, aige::BootstrapIso(), root_);
    session.RequestPlayerAttack(skeletonPos.x, skeletonPos.y);
    session.Advance(0.2, [](aige::Key) { return false; });
    float afterHit = registry_.get<aige::Health>(skeleton).current;
    EXPECT_LT(afterHit, 50.0f) << "the swing should land";

    bool burning = false;
    for (auto [entity, effect] : registry_.view<aige::StatusEffect>().each()) {
        burning |= effect.target == skeleton && effect.kind == aige::EffectKind::Burn;
    }
    EXPECT_TRUE(burning) << "the hit should set it on fire";

    for (int i = 0; i < 120; ++i) { // two more seconds, no more swings
        session.Advance(aige::GameSession::kFixedDt, [](aige::Key) { return false; });
    }
    EXPECT_LT(registry_.get<aige::Health>(skeleton).current, afterHit) << "burn ticks keep hurting after the hit";
}

TEST_F(AiGameplayTest, DryRunCatchesTheBuggyModelWrittenScript) {
    fs::copy_file(fs::path(AIGE_SOURCE_DIR) / "tests" / "fixtures" / "model_written_flee_buggy.lua",
                  root_ / "scripts" / "flee.lua");
    // Every enemy runs it, whatever script the (possibly locally edited) demo
    // prefab currently uses.
    for (auto [entity, script, faction] : registry_.view<aige::LuaScript, aige::Faction>().each()) {
        if (faction.team == aige::Team::Enemy) {
            script.path = "scripts/flee.lua";
        }
    }
    auto problems = aige::DryRunScripts(aige::SerializeScene(registry_), project_, 1.0, {"scripts/flee.lua"});
    ASSERT_FALSE(problems.empty());
    EXPECT_NE(problems[0].message.find("attempt to index"), std::string::npos) << problems[0].message;
    EXPECT_GT(problems[0].count, 1); // it fails every frame, reported once
}

TEST_F(AiGameplayTest, DryRunPassesWorkingScripts) {
    // The shipped enemy script, and the flee script the model writes now
    // that the prompt documents the API precisely.
    auto proposal = GoldenProposal("flee_script");
    auto outcome = ApplyAll(proposal);
    ASSERT_TRUE(outcome.errors.empty()) << outcome.errors[0];
    std::vector<std::string> written;
    for (const auto& op : proposal["operations"]) {
        if (op.value("name", "") == "script.write") {
            written.push_back(op["args"]["path"]);
        }
    }
    ASSERT_EQ(written.size(), 1u);
    auto problems = aige::DryRunScripts(aige::SerializeScene(registry_), project_, 2.0);
    for (const auto& problem : problems) {
        ADD_FAILURE() << problem.path << ": " << problem.message;
    }
}
