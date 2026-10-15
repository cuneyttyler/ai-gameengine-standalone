#include <gtest/gtest.h>

#include "ai_bridge/Proposal.h"
#include "ai_bridge/ReviewPolicy.h"
#include "assets/Prefab.h"
#include "assets/Project.h"
#include "assets/SceneIO.h"
#include "editing/Commands.h"
#include "gameplay/Archetypes.h"
#include "gameplay/Components.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/Settings.h"
#include "scripting/Components.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace {

class ReviewPolicyTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / ("aige_review_policy_" +
                                             std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root_);
        fs::create_directories(root_ / "scripts");
        project_ = aige::Project{root_};
        prefabs_ = std::make_unique<aige::PrefabLibrary>(project_);
        aige::RegisterEditingCommands(aige::EngineReflection());

        aige::Registry scratch;
        prefabs_->SaveAsPrefab(scratch, aige::CreateEnemy(scratch, 0.0f, 0.0f, entt::null, ""), "prefabs/skeleton.prefab");
        hero_ = aige::CreatePlayer(registry_, 2.5f, 2.5f);
        auto& script = registry_.emplace_or_replace<aige::LuaScript>(hero_);
        script.path = "scripts/hero.lua";
        script.properties["dash_speed"] = 4.0;
        skeleton_ = *prefabs_->Instantiate(registry_, "prefabs/skeleton.prefab", 5.5f, 5.5f);
        savedSettings_ = aige::ActiveSettings();
    }
    void TearDown() override {
        aige::ActiveSettings() = savedSettings_;
        fs::remove_all(root_);
    }

    std::vector<aige::ChangeItem> Resolve(const std::vector<nlohmann::json>& operations) {
        nlohmann::json proposal = {{"summary", "test"}, {"operations", operations}};
        return aige::ResolveProposal(proposal, registry_, aige::EngineReflection(), *prefabs_, project_).items;
    }
    aige::ApplyOutcome Apply(const std::vector<nlohmann::json>& operations) {
        return aige::ApplyOperations(operations, registry_, aige::EngineReflection(), *prefabs_, project_);
    }
    nlohmann::json Id(aige::Entity entity) { return static_cast<uint32_t>(entity); }

    fs::path root_;
    aige::Project project_;
    std::unique_ptr<aige::PrefabLibrary> prefabs_;
    aige::Registry registry_;
    aige::Entity hero_{}, skeleton_{};
    aige::ProjectSettings savedSettings_;
};

} // namespace

TEST_F(ReviewPolicyTest, OnlyReversibleTweaksSkipReviewAndOnlyWhenAsked) {
    auto items = Resolve({
        {{"op", "set_field"}, {"entity", Id(skeleton_)}, {"component", "Health"}, {"field", "max"}, {"value", 12}},
        {{"op", "set_setting"}, {"section", "Combat"}, {"field", "knockbackScale"}, {"value", 1.5}},
        {{"op", "set_script_property"}, {"entity", Id(hero_)}, {"name", "dash_speed"}, {"value", 6}},
        {{"op", "set_script_property"}, {"entity", Id(hero_)}, {"name", "brand_new"}, {"value", 1}},
        {{"op", "set_field"}, {"entity", Id(hero_)}, {"component", "LuaScript"}, {"field", "path"},
         {"value", "scripts/other.lua"}},
        {{"op", "add_component"}, {"entity", Id(hero_)}, {"component", "Follow"}},
        {{"op", "remove_component"}, {"entity", Id(skeleton_)}, {"component", "Follow"}},
        {{"op", "set_prefab_field"}, {"prefab", "prefabs/skeleton.prefab"}, {"component", "Health"}, {"field", "max"},
         {"value", 20}},
        {{"op", "command"}, {"name", "entity.delete"}, {"args", {{"entity", Id(skeleton_)}}}},
        {{"op", "set_field"}, {"entity", Id(skeleton_)}, {"component", "Health"}, {"field", "max"}, {"value", -5}},
    });
    ASSERT_EQ(items.size(), 10u);
    std::vector<bool> expected = {true, true, true, false, false, false, false, false, false, false};
    for (size_t i = 0; i < items.size(); ++i) {
        EXPECT_FALSE(aige::AppliesWithoutReview(items[i], aige::ReviewPolicy::ReviewAll)) << items[i].target;
        EXPECT_EQ(aige::AppliesWithoutReview(items[i], aige::ReviewPolicy::AutoApplyTweaks), expected[i])
            << i << ": " << items[i].target << " " << items[i].error;
    }
}

TEST_F(ReviewPolicyTest, PolicyNamesRoundTripAndUnknownMeansReviewAll) {
    for (auto policy : {aige::ReviewPolicy::ReviewAll, aige::ReviewPolicy::AutoApplyTweaks}) {
        EXPECT_EQ(aige::ParseReviewPolicy(aige::ReviewPolicyName(policy)), policy);
    }
    EXPECT_EQ(aige::ParseReviewPolicy("applyEverything"), aige::ReviewPolicy::ReviewAll);
    EXPECT_EQ(aige::ParseReviewPolicy(""), aige::ReviewPolicy::ReviewAll);
}

// What the editor does with a mixed proposal: apply the tweaks, leave the rest
// pending; "Undo" then puts everything back exactly.
TEST_F(ReviewPolicyTest, AutoAppliedTweaksUndoExactlyAndTheRestStaysPending) {
    const float maxBefore = registry_.get<aige::Health>(skeleton_).max;
    const float knockbackBefore = aige::ActiveSettings().combat.knockbackScale;
    auto items = Resolve({
        {{"op", "set_field"}, {"entity", Id(skeleton_)}, {"component", "Health"}, {"field", "max"}, {"value", 12}},
        {{"op", "set_setting"}, {"section", "Combat"}, {"field", "knockbackScale"}, {"value", 1.5}},
        {{"op", "set_script_property"}, {"entity", Id(hero_)}, {"name", "dash_speed"}, {"value", 6}},
        {{"op", "add_component"}, {"entity", Id(hero_)}, {"component", "Follow"}},
    });
    std::vector<nlohmann::json> autoOps, undoOps;
    std::vector<size_t> pending;
    for (size_t i = 0; i < items.size(); ++i) {
        if (aige::AppliesWithoutReview(items[i], aige::ReviewPolicy::AutoApplyTweaks)) {
            autoOps.push_back(items[i].op);
            undoOps.push_back(*aige::InverseOperation(items[i]));
        } else {
            pending.push_back(i);
        }
    }
    ASSERT_EQ(autoOps.size(), 3u);
    EXPECT_EQ(pending, std::vector<size_t>{3}); // the new component waits for review

    auto applied = Apply(autoOps);
    ASSERT_TRUE(applied.errors.empty()) << applied.errors[0];
    EXPECT_EQ(registry_.get<aige::Health>(skeleton_).max, 12.0f);
    EXPECT_FLOAT_EQ(aige::ActiveSettings().combat.knockbackScale, 1.5f);
    EXPECT_FALSE(registry_.all_of<aige::Follow>(hero_));

    auto undone = Apply(undoOps);
    ASSERT_TRUE(undone.errors.empty()) << undone.errors[0];
    EXPECT_FLOAT_EQ(aige::ActiveSettings().combat.knockbackScale, knockbackBefore);
    // Values are back; the prefab copy keeps an override marker for Health.max,
    // which is what an Inspector edit back to the old value does too.
    EXPECT_EQ(registry_.get<aige::Health>(skeleton_).max, maxBefore);
    EXPECT_EQ(std::get<double>(registry_.get<aige::LuaScript>(hero_).properties["dash_speed"]), 4.0);
}
