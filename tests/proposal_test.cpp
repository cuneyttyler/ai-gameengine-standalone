#include <gtest/gtest.h>

#include "ai_bridge/Proposal.h"
#include "assets/Prefab.h"
#include "assets/Project.h"
#include "assets/SceneIO.h"
#include "editing/Commands.h"
#include "gameplay/Archetypes.h"
#include "gameplay/Components.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/Settings.h"
#include "physics2d/Components.h"
#include "scripting/Components.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

class ProposalTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("aige_proposal_test_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root_);
        fs::create_directories(root_ / "scripts");
        project_ = aige::Project{root_};
        prefabs_ = std::make_unique<aige::PrefabLibrary>(project_);
        aige::RegisterEditingCommands(aige::EngineReflection());

        aige::Registry scratch;
        prefabs_->SaveAsPrefab(scratch, aige::CreateEnemy(scratch, 0.0f, 0.0f, entt::null, ""), "prefabs/skeleton.prefab");
        hero_ = aige::CreatePlayer(registry_, 2.5f, 2.5f);
        skeletonA_ = *prefabs_->Instantiate(registry_, "prefabs/skeleton.prefab", 5.5f, 5.5f);
        skeletonB_ = *prefabs_->Instantiate(registry_, "prefabs/skeleton.prefab", 7.5f, 5.5f);
        registry_.get<aige::Name>(skeletonA_).value = "Skeleton A";
        savedSettings_ = aige::ActiveSettings();
    }
    void TearDown() override {
        aige::ActiveSettings() = savedSettings_;
        fs::remove_all(root_);
    }

    nlohmann::json Proposal(std::vector<nlohmann::json> operations) {
        return {{"summary", "test"}, {"operations", operations}};
    }
    aige::ResolvedProposal Resolve(const nlohmann::json& proposal) {
        return aige::ResolveProposal(proposal, registry_, aige::EngineReflection(), *prefabs_, project_);
    }
    aige::ApplyOutcome Apply(std::vector<nlohmann::json> operations) {
        return aige::ApplyOperations(operations, registry_, aige::EngineReflection(), *prefabs_, project_);
    }
    uint32_t Id(aige::Entity entity) { return static_cast<uint32_t>(entity); }

    fs::path root_;
    aige::Project project_;
    std::unique_ptr<aige::PrefabLibrary> prefabs_;
    aige::Registry registry_;
    aige::Entity hero_{}, skeletonA_{}, skeletonB_{};
    aige::ProjectSettings savedSettings_;
};

} // namespace

TEST_F(ProposalTest, ResolveShowsBeforeAndAfterWithoutChangingAnything) {
    std::string before = aige::SerializeScene(registry_);
    auto resolved = Resolve(Proposal({{{"op", "set_field"}, {"entity", Id(hero_)}, {"component", "MeleeAttack"},
                                       {"field", "effect"}, {"value", "Burn"}}}));
    ASSERT_EQ(resolved.items.size(), 1u);
    const auto& item = resolved.items[0];
    EXPECT_TRUE(item.error.empty()) << item.error;
    EXPECT_EQ(item.kind, "field");
    EXPECT_EQ(item.target, "Player #" + std::to_string(Id(hero_)) + " · MeleeAttack.effect");
    EXPECT_EQ(item.before, "None");
    EXPECT_EQ(item.after, "Burn");
    EXPECT_EQ(item.field["type"], "enum"); // enough for inline editing
    EXPECT_EQ(aige::SerializeScene(registry_), before);
}

TEST_F(ProposalTest, ResolveCatchesStaleAndInvalidOperations) {
    aige::Entity gone = registry_.create();
    registry_.destroy(gone);
    auto resolved = Resolve(Proposal({
        {{"op", "set_field"}, {"entity", Id(gone)}, {"component", "Health"}, {"field", "max"}, {"value", 5}},
        {{"op", "set_field"}, {"entity", Id(hero_)}, {"component", "Health"}, {"field", "max"}, {"value", -3}},
        {{"op", "set_field"}, {"entity", Id(hero_)}, {"component", "Health"}, {"field", "mana"}, {"value", 1}},
        {{"op", "add_component"}, {"entity", Id(hero_)}, {"component", "Health"}},
        {{"op", "set_prefab_field"}, {"prefab", "prefabs/dragon.prefab"}, {"component", "Health"}, {"field", "max"},
         {"value", 1}},
        {{"op", "teleport"}},
    }));
    ASSERT_EQ(resolved.items.size(), 6u);
    EXPECT_FALSE(resolved.AllValid());
    EXPECT_NE(resolved.items[0].error.find("no longer exists"), std::string::npos);
    EXPECT_NE(resolved.items[1].error.find("at least 1"), std::string::npos);
    EXPECT_NE(resolved.items[2].error.find("unknown field"), std::string::npos);
    EXPECT_NE(resolved.items[3].error.find("already has"), std::string::npos);
    EXPECT_NE(resolved.items[4].error.find("no prefab"), std::string::npos);
    EXPECT_NE(resolved.items[5].error.find("unknown operation"), std::string::npos);
}

TEST_F(ProposalTest, FieldEditsOnPrefabCopiesBecomeOverrides) {
    auto outcome = Apply({{{"op", "set_field"}, {"entity", Id(skeletonA_)}, {"component", "Health"}, {"field", "max"},
                           {"value", 30}}});
    EXPECT_EQ(outcome.applied, 1);
    EXPECT_FLOAT_EQ(registry_.get<aige::Health>(skeletonA_).max, 30.0f);
    EXPECT_TRUE(aige::HasOverride(registry_, skeletonA_, "Health.max"));
}

TEST_F(ProposalTest, PrefabFieldChangesReachEveryCopy) {
    registry_.get<aige::Stats>(skeletonB_).moveSpeed = 9.0f;
    aige::AddOverride(registry_, skeletonB_, "Stats.moveSpeed");

    auto outcome = Apply({
        {{"op", "set_prefab_field"}, {"prefab", "prefabs/skeleton.prefab"}, {"component", "Health"}, {"field", "max"},
         {"value", 15}},
        {{"op", "set_prefab_field"}, {"prefab", "prefabs/skeleton.prefab"}, {"component", "Stats"},
         {"field", "moveSpeed"}, {"value", 1.0}},
    });
    EXPECT_EQ(outcome.applied, 2);
    EXPECT_TRUE(outcome.prefabsChanged);
    EXPECT_FLOAT_EQ(registry_.get<aige::Health>(skeletonA_).max, 15.0f);
    EXPECT_FLOAT_EQ(registry_.get<aige::Health>(skeletonB_).max, 15.0f);
    EXPECT_FLOAT_EQ(registry_.get<aige::Stats>(skeletonA_).moveSpeed, 1.0f);
    EXPECT_FLOAT_EQ(registry_.get<aige::Stats>(skeletonB_).moveSpeed, 9.0f); // its own override survives

    std::ifstream file(root_ / "prefabs/skeleton.prefab");
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("Health current=8 max=15"), std::string::npos);
}

TEST_F(ProposalTest, ComponentsSettingsAndScriptProperties) {
    auto outcome = Apply({
        {{"op", "add_component"}, {"entity", Id(hero_)}, {"component", "Sounds"},
         {"fields", {{"hurt", ""}, {"attack", ""}}}},
        {{"op", "remove_component"}, {"entity", Id(skeletonA_)}, {"component", "Follow"}},
        {{"op", "set_setting"}, {"section", "Combat"}, {"field", "knockbackScale"}, {"value", 2}},
    });
    EXPECT_EQ(outcome.applied, 3) << (outcome.errors.empty() ? "" : outcome.errors[0]);
    EXPECT_TRUE(registry_.all_of<aige::Sounds>(hero_));
    EXPECT_FALSE(registry_.all_of<aige::Follow>(skeletonA_));
    EXPECT_TRUE(aige::HasOverride(registry_, skeletonA_, "-Follow"));
    EXPECT_FLOAT_EQ(aige::ActiveSettings().combat.knockbackScale, 2.0f);
    EXPECT_TRUE(outcome.settingsChanged);
}

TEST_F(ProposalTest, NewBehaviorScriptIsWrittenThenAttached) {
    const std::string source = "properties = { radius = 2 }\nfunction update(dt) end\n";
    auto outcome = Apply({
        {{"op", "command"}, {"name", "script.write"}, {"args", {{"path", "scripts/aura.lua"}, {"source", source}}}},
        {{"op", "add_component"}, {"entity", Id(hero_)}, {"component", "LuaScript"},
         {"fields", {{"path", "scripts/aura.lua"}}}},
        {{"op", "set_script_property"}, {"entity", Id(hero_)}, {"name", "radius"}, {"value", 3.5}},
    });
    EXPECT_EQ(outcome.applied, 3) << (outcome.errors.empty() ? "" : outcome.errors[0]);
    std::ifstream file(root_ / "scripts/aura.lua");
    std::string written((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_EQ(written, source);
    EXPECT_EQ(registry_.get<aige::LuaScript>(hero_).path, "scripts/aura.lua");
    EXPECT_DOUBLE_EQ(std::get<double>(registry_.get<aige::LuaScript>(hero_).properties.at("radius")), 3.5);
}

TEST_F(ProposalTest, CommandsResolveWithDetailsAndRun) {
    auto resolved = Resolve(Proposal({{{"op", "command"}, {"name", "scene.spawn"},
                                       {"args", {{"prefab", "prefabs/skeleton.prefab"}, {"x", 3.5}, {"y", 3.5}}}}}));
    ASSERT_TRUE(resolved.AllValid()) << resolved.items[0].error;
    EXPECT_EQ(resolved.items[0].target, "scene.spawn");

    size_t before = registry_.view<aige::PrefabInstance>().size();
    auto outcome = Apply({resolved.items[0].op});
    EXPECT_EQ(outcome.applied, 1);
    EXPECT_EQ(registry_.view<aige::PrefabInstance>().size(), before + 1);

    auto bad = Resolve(Proposal({{{"op", "command"}, {"name", "script.write"},
                                  {"args", {{"path", "../outside.lua"}, {"source", "--"}}}}}));
    // Path safety is the command's own check, so it surfaces when applied.
    auto badOutcome = Apply({bad.items[0].op});
    EXPECT_EQ(badOutcome.applied, 0);
    EXPECT_FALSE(fs::exists(root_.parent_path() / "outside.lua"));
}

TEST_F(ProposalTest, OneFailingOperationDoesNotBlockTheRest) {
    auto outcome = Apply({
        {{"op", "set_field"}, {"entity", Id(hero_)}, {"component", "Health"}, {"field", "max"}, {"value", -1}},
        {{"op", "set_field"}, {"entity", Id(hero_)}, {"component", "Health"}, {"field", "max"}, {"value", 40}},
    });
    EXPECT_EQ(outcome.applied, 1);
    ASSERT_EQ(outcome.errors.size(), 1u);
    EXPECT_NE(outcome.errors[0].find("Health.max"), std::string::npos);
    EXPECT_FLOAT_EQ(registry_.get<aige::Health>(hero_).max, 40.0f);
}
