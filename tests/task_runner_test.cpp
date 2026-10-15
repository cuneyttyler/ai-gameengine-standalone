#include <gtest/gtest.h>

#include "ai_bridge/TaskRunner.h"
#include "assets/AssetDatabase.h"
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
#include <fstream>

namespace fs = std::filesystem;

namespace {

const char* kBuggyScript = "function update(dt)\n    local target = nil\n    target.x = 1\nend\n";
const char* kFixedScript = "function update(dt)\nend\n";

// development_plan.md 2.9, with scripted model answers: the working copy,
// the feedback loop, the limits, and replaying the result on the real scene.
class TaskRunnerTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("aige_task_runner_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root_);
        fs::create_directories(root_ / "scripts");
        project_ = aige::Project{root_};
        prefabs_ = std::make_unique<aige::PrefabLibrary>(project_);
        aige::RegisterEditingCommands(aige::EngineReflection());

        aige::Registry scratch;
        prefabs_->SaveAsPrefab(scratch, aige::CreateEnemy(scratch, 0.0f, 0.0f, entt::null, ""), "prefabs/skeleton.prefab");
        prefabs_->Refresh();
        hero_ = aige::CreatePlayer(registry_, 2.5f, 2.5f);
        skeleton_ = *prefabs_->Instantiate(registry_, "prefabs/skeleton.prefab", 5.5f, 5.5f);
        savedSettings_ = aige::ActiveSettings();
    }
    void TearDown() override {
        aige::ActiveSettings() = savedSettings_;
        fs::remove_all(root_);
    }

    std::unique_ptr<aige::TaskRunner> Start(aige::TaskLimits limits = {}) {
        return std::make_unique<aige::TaskRunner>(registry_, project_, aige::EngineReflection(), "add a dodge roll",
                                                  limits);
    }
    static nlohmann::json WriteScript(const std::string& path, const std::string& source) {
        return {{"op", "command"}, {"name", "script.write"}, {"args", {{"path", path}, {"source", source}}}};
    }
    static nlohmann::json Answer(std::vector<nlohmann::json> operations, bool done,
                                 std::vector<std::string> plan = {}) {
        return {{"summary", "step"}, {"scope", "task"}, {"plan", plan}, {"operations", operations}, {"done", done}};
    }
    nlohmann::json Id(aige::Entity entity) { return static_cast<uint32_t>(entity); }

    // Replays everything the task did, the way the editor does on Accept.
    aige::ApplyOutcome ReplayAll(const aige::TaskRunner& task) {
        std::vector<std::pair<size_t, nlohmann::json>> accepted;
        const auto operations = task.CombinedProposal()["operations"];
        for (size_t i = 0; i < operations.size(); ++i) {
            accepted.emplace_back(i, operations[i]);
        }
        return task.Replay(accepted, registry_, *prefabs_, project_);
    }

    fs::path root_;
    aige::Project project_;
    std::unique_ptr<aige::PrefabLibrary> prefabs_;
    aige::Registry registry_;
    aige::Entity hero_{}, skeleton_{};
    aige::ProjectSettings savedSettings_;
};

} // namespace

TEST_F(TaskRunnerTest, ScriptErrorsGoBackToTheModelAndTheFixFinishesTheTask) {
    const std::string sceneBefore = aige::SerializeScene(registry_);
    auto task = Start();

    auto state = task->OnAnswer(Answer({WriteScript("scripts/dodge.lua", kBuggyScript),
                                        {{"op", "add_component"},
                                         {"entity", Id(hero_)},
                                         {"component", "LuaScript"},
                                         {"fields", {{"path", "scripts/dodge.lua"}}}}},
                                       false, {"write the dodge script", "attach it to the hero"}));
    ASSERT_EQ(state, aige::TaskRunner::State::Working);
    ASSERT_EQ(task->Steps().size(), 1u);
    ASSERT_FALSE(task->Steps()[0].problems.empty());
    EXPECT_NE(task->NextInstruction().find("attempt to index"), std::string::npos) << task->NextInstruction();
    EXPECT_NE(task->NextInstruction().find("1. write the dodge script"), std::string::npos);
    EXPECT_EQ(task->Plan().size(), 2u);

    // Nothing has touched the real project.
    EXPECT_EQ(aige::SerializeScene(registry_), sceneBefore);
    EXPECT_FALSE(fs::exists(root_ / "scripts" / "dodge.lua"));

    state = task->OnAnswer(Answer({WriteScript("scripts/dodge.lua", kFixedScript)}, true));
    ASSERT_EQ(state, aige::TaskRunner::State::Finished);
    EXPECT_TRUE(task->Steps()[1].problems.empty());

    // The conversation the model saw: request, answer, follow-up, answer.
    EXPECT_EQ(task->History().size(), 4u);

    auto combined = task->CombinedProposal();
    ASSERT_EQ(combined["operations"].size(), 3u);
    auto outcome = ReplayAll(*task);
    ASSERT_TRUE(outcome.errors.empty()) << outcome.errors[0];
    EXPECT_EQ(registry_.get<aige::LuaScript>(hero_).path, "scripts/dodge.lua");
    std::ifstream written(root_ / "scripts" / "dodge.lua");
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(written), {}), kFixedScript);
}

TEST_F(TaskRunnerTest, EntitiesTheTaskSpawnedAreTranslatedOnReplay) {
    auto task = Start();
    // Step 1 spawns a skeleton in the working copy; step 2 edits it by the id
    // it got there (as the model would, reading the context).
    task->OnAnswer(Answer({{{"op", "command"},
                            {"name", "scene.spawn"},
                            {"args", {{"prefab", "prefabs/skeleton.prefab"}, {"x", 9.5}, {"y", 9.5}}}}},
                          false));
    ASSERT_EQ(task->GetState(), aige::TaskRunner::State::Working);
    aige::AssetDatabase assets(project_);
    nlohmann::json context = task->Context(assets);
    std::optional<uint32_t> spawnedId;
    for (const auto& entity : context["entities"]) {
        if (entity["components"].contains("WorldTransform2D") && entity["components"]["WorldTransform2D"]["x"] == 9.5) {
            spawnedId = entity["id"].get<uint32_t>();
        }
    }
    ASSERT_TRUE(spawnedId);
    task->OnAnswer(Answer({{{"op", "set_field"},
                            {"entity", *spawnedId},
                            {"component", "Health"},
                            {"field", "max"},
                            {"value", 30}}},
                          true));
    ASSERT_EQ(task->GetState(), aige::TaskRunner::State::Finished);

    // Meanwhile the user placed something in the real scene, which takes the id
    // the working copy's skeleton had.
    aige::Entity users = *prefabs_->Instantiate(registry_, "prefabs/skeleton.prefab", 1.5f, 1.5f);
    ASSERT_EQ(static_cast<uint32_t>(users), *spawnedId);
    const float usersMax = registry_.get<aige::Health>(users).max;

    auto outcome = ReplayAll(*task);
    ASSERT_TRUE(outcome.errors.empty()) << outcome.errors[0];
    aige::Entity replayed = outcome.created[0];
    ASSERT_TRUE(replayed != entt::null);
    EXPECT_NE(replayed, users);
    EXPECT_EQ(registry_.get<aige::Health>(replayed).max, 30.0f);
    EXPECT_EQ(registry_.get<aige::Health>(users).max, usersMax); // untouched
}

TEST_F(TaskRunnerTest, GivesUpAtTheStepLimit) {
    aige::TaskLimits limits;
    limits.maxSteps = 3;
    auto task = Start(limits);
    task->OnAnswer(Answer({WriteScript("scripts/dodge.lua", kBuggyScript),
                           {{"op", "add_component"},
                            {"entity", Id(hero_)},
                            {"component", "LuaScript"},
                            {"fields", {{"path", "scripts/dodge.lua"}}}}},
                          false));
    EXPECT_EQ(task->GetState(), aige::TaskRunner::State::Working);
    task->OnAnswer(Answer({WriteScript("scripts/dodge.lua", kBuggyScript)}, true)); // "done", but still broken
    EXPECT_EQ(task->GetState(), aige::TaskRunner::State::Working);
    EXPECT_NE(task->NextInstruction().find("You marked the task done"), std::string::npos);
    task->OnAnswer(Answer({WriteScript("scripts/dodge.lua", kBuggyScript)}, true));
    EXPECT_EQ(task->GetState(), aige::TaskRunner::State::GaveUp);
    EXPECT_NE(task->StopReason().find("3 steps"), std::string::npos) << task->StopReason();
    // Further answers are ignored.
    task->OnAnswer(Answer({WriteScript("scripts/dodge.lua", kFixedScript)}, true));
    EXPECT_EQ(task->Steps().size(), 3u);
}

TEST_F(TaskRunnerTest, StoppingLeavesTheProjectUntouched) {
    const std::string sceneBefore = aige::SerializeScene(registry_);
    const float knockbackBefore = aige::ActiveSettings().combat.knockbackScale;
    std::ifstream prefabIn(root_ / "prefabs" / "skeleton.prefab");
    const std::string prefabBefore(std::istreambuf_iterator<char>(prefabIn), {});
    fs::path workingCopy;
    {
        auto task = Start();
        workingCopy = task->WorkingCopy();
        task->OnAnswer(Answer({WriteScript("scripts/dodge.lua", kFixedScript),
                               {{"op", "set_setting"}, {"section", "Combat"}, {"field", "knockbackScale"}, {"value", 2}},
                               {{"op", "set_prefab_field"},
                                {"prefab", "prefabs/skeleton.prefab"},
                                {"component", "Health"},
                                {"field", "max"},
                                {"value", 40}},
                               {{"op", "entity.delete"}}, // invalid: reported, skipped
                               {{"op", "command"}, {"name", "entity.delete"}, {"args", {{"entity", Id(skeleton_)}}}}},
                              false));
        ASSERT_EQ(task->Steps()[0].applyErrors.size(), 1u);
        EXPECT_TRUE(fs::exists(workingCopy / "scripts" / "dodge.lua"));
        // The working copy's settings changed; the editor's didn't.
        EXPECT_FLOAT_EQ(aige::ActiveSettings().combat.knockbackScale, knockbackBefore);
    } // the user pressed Stop

    EXPECT_EQ(aige::SerializeScene(registry_), sceneBefore);
    EXPECT_FLOAT_EQ(aige::ActiveSettings().combat.knockbackScale, knockbackBefore);
    std::ifstream prefabAfter(root_ / "prefabs" / "skeleton.prefab");
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(prefabAfter), {}), prefabBefore);
    EXPECT_FALSE(fs::exists(root_ / "scripts" / "dodge.lua"));
    EXPECT_FALSE(fs::exists(workingCopy));
}

TEST_F(TaskRunnerTest, AnAnswerWithNothingLeftToDoFinishes) {
    auto task = Start();
    task->OnAnswer(Answer({{{"op", "set_field"}, {"entity", Id(skeleton_)}, {"component", "Health"}, {"field", "max"},
                            {"value", 20}}},
                          false));
    EXPECT_EQ(task->GetState(), aige::TaskRunner::State::Working);
    task->OnAnswer({{"summary", "nothing more"}, {"scope", "task"}, {"operations", nlohmann::json::array()}});
    EXPECT_EQ(task->GetState(), aige::TaskRunner::State::Finished);
    EXPECT_EQ(task->CombinedProposal()["operations"].size(), 1u);
}
