#include <gtest/gtest.h>

#include "ai_bridge/AiTools.h"
#include "ai_bridge/TaskRunner.h"
#include "assets/AssetDatabase.h"
#include "assets/Prefab.h"
#include "assets/Project.h"
#include "assets/SceneIO.h"
#include "assets/TextureCook.h"
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

// A step that only generated characters (to use next) isn't the end (2026-11-04:
// six wards of residents ended after the first four sheets, with nothing to apply).
TEST_F(TaskRunnerTest, AStepThatOnlyMadeCharactersGoesOn) {
    auto task = Start();
    const nlohmann::json generate = {{{"kind", "character"}, {"name", "Nella Voss"}, {"description", "a shopkeeper"}}};
    task->OnAnswer({{"summary", "draw Nella"}, {"scope", "task"}, {"generate", generate},
                    {"notes", "Made: character:Nella Voss -> assets/characters/nella-voss.character.anim.json"},
                    {"operations", nlohmann::json::array()}});
    EXPECT_EQ(task->GetState(), aige::TaskRunner::State::Working);
    EXPECT_NE(task->NextInstruction().find("use those paths now"), std::string::npos);
    // ... but two empty steps running end it.
    task->OnAnswer({{"summary", "draw her again"}, {"scope", "task"}, {"generate", generate},
                    {"operations", nlohmann::json::array()}});
    EXPECT_EQ(task->GetState(), aige::TaskRunner::State::Finished);
}

// Steps that read art (prefab.create, terrain commands, level.village) see the
// project's assets: the working copy reads them in place instead of copying
// them (2026-10-17: every village step failed with "can't read the image").
TEST_F(TaskRunnerTest, StepsReadTheProjectsArtInPlace) {
    fs::create_directories(root_ / "assets" / "pack");
    aige::ImageData image;
    image.width = image.height = 32;
    image.rgba.assign(32 * 32 * 4, 200);
    ASSERT_TRUE(aige::WritePng(root_ / "assets" / "pack" / "well.png", image));

    auto task = Start();
    task->OnAnswer(Answer({{{"op", "command"},
                            {"name", "prefab.create"},
                            {"args", {{"asset", "assets/pack/well.png"}, {"name", "well"}, {"tile", false},
                                      {"solid", true}, {"cells", 1.0}}}}},
                          false));
    ASSERT_EQ(task->Steps().size(), 1u);
    EXPECT_TRUE(task->Steps().front().applyErrors.empty())
        << (task->Steps().front().applyErrors.empty() ? "" : task->Steps().front().applyErrors.front());
    EXPECT_TRUE(fs::exists(task->WorkingCopy() / "prefabs" / "well.prefab"));
    EXPECT_FALSE(fs::exists(task->WorkingCopy() / "assets")); // read in place, not copied
    EXPECT_FALSE(fs::exists(root_ / "prefabs" / "well.prefab")); // the project is untouched until accepted
}

// Operations the service left out go back to the model like failed ones; a
// step that can't be had at all ends the task with what was done, not nothing.
TEST_F(TaskRunnerTest, SkippedOperationsAreReportedAndAFailedStepKeepsTheWorkSoFar) {
    auto task = Start();
    nlohmann::json answer = Answer({{{"op", "set_field"}, {"entity", Id(skeleton_)}, {"component", "Health"},
                                     {"field", "max"}, {"value", 20}}},
                                   true);
    answer["skipped"] = {"operation 2 (add_component): entity 2414 doesn't exist"};
    EXPECT_EQ(task->OnAnswer(answer), aige::TaskRunner::State::Working); // not done: something to redo
    ASSERT_EQ(task->Steps().size(), 1u);
    ASSERT_EQ(task->Steps().front().applyErrors.size(), 1u);
    EXPECT_NE(task->NextInstruction().find("entity 2414"), std::string::npos);

    task->GiveUp("The AI couldn't produce the next step.");
    EXPECT_EQ(task->GetState(), aige::TaskRunner::State::GaveUp);
    EXPECT_EQ(task->CombinedProposal()["operations"].size(), 1u); // step 1 is still there to accept
    EXPECT_EQ(task->StopReason(), "The AI couldn't produce the next step.");
}

// An outline first (a village's people): nothing is applied until the user approves it; changes bring a revised one,
// and the approved outline goes with every step after.
TEST_F(TaskRunnerTest, AnOutlineWaitsForApprovalBeforeAnyStep) {
    auto task = Start();
    const nlohmann::json outline = {{"title", "Oakvale's people"},
                                    {"text", "The miller owes the smith."},
                                    {"parts", {{{"heading", "The Mill"}, {"items", {"Bren, the miller"}}}}}};
    auto state = task->OnAnswer({{"summary", "outline"}, {"scope", "task"}, {"plan", {"canon", "people", "dialogue"}},
                                 {"outline", outline}, {"operations", nlohmann::json::array()}});
    EXPECT_EQ(state, aige::TaskRunner::State::Working); // an outline alone doesn't end the task
    EXPECT_TRUE(task->AwaitingOutline());
    EXPECT_TRUE(task->Steps().empty());
    EXPECT_EQ(task->Plan().size(), 3u);

    task->AnswerOutline("make the smith a woman");
    EXPECT_FALSE(task->AwaitingOutline());
    EXPECT_NE(task->NextInstruction().find("make the smith a woman"), std::string::npos);
    nlohmann::json revised = outline;
    revised["text"] = "The miller owes the smith, Mira.";
    task->OnAnswer({{"summary", "outline"}, {"scope", "task"}, {"outline", revised}, {"operations", nlohmann::json::array()}});
    ASSERT_TRUE(task->AwaitingOutline());
    EXPECT_EQ(task->Outline()["text"], "The miller owes the smith, Mira.");
    EXPECT_EQ(task->Plan().size(), 3u); // kept: the revision didn't give a new one

    task->AnswerOutline("");
    EXPECT_NE(task->NextInstruction().find("approved"), std::string::npos);
    state = task->OnAnswer(Answer({{{"op", "set_field"}, {"entity", Id(skeleton_)}, {"component", "Health"}, {"field", "max"},
                                    {"value", 30}}},
                                  false));
    ASSERT_EQ(state, aige::TaskRunner::State::Working);
    ASSERT_EQ(task->Steps().size(), 1u);
    EXPECT_NE(task->NextInstruction().find("Mira"), std::string::npos); // the approved outline goes along
}

// The buildings tool: a cutaway's cells to stand someone in, inside its walls and off its furniture; a scene
// interior's people go in its own scene.
TEST_F(TaskRunnerTest, TheBuildingsToolSaysWhereSomeoneCanStandInside) {
    const aige::Entity house = registry_.create();
    registry_.emplace<aige::WorldTransform2D>(house, 12.0f, 12.0f);
    registry_.emplace<aige::Name>(house, aige::Name{"Mill"});
    aige::Interior cutaway;
    cutaway.cells = "10,10,13,13"; // inside its walls: 11..12 x 11..12
    cutaway.solids = "1:2,2";      // a quarter cell at (1,1) cells from its corner: (11, 11) is taken
    registry_.emplace<aige::Interior>(house, cutaway);
    const aige::Entity inn = registry_.create();
    registry_.emplace<aige::WorldTransform2D>(inn, 20.0f, 20.0f);
    aige::Interior scene;
    scene.kind = "scene";
    scene.scene = "scenes/inn.aige";
    scene.cells = "18,18,22,22";
    registry_.emplace<aige::Interior>(inn, scene);

    aige::AssetDatabase assets(project_);
    aige::AiToolContext tools{registry_, aige::EngineReflection(), *prefabs_, assets, project_};
    const nlohmann::json result = aige::RunAiTool("buildings", nlohmann::json::object(), tools);
    ASSERT_EQ(result["buildings"].size(), 2u) << result.dump();
    for (const auto& b : result["buildings"]) {
        if (b["kind"] == "cutaway") {
            EXPECT_EQ(b["name"], "Mill");
            EXPECT_EQ(b["footprint"], nlohmann::json({10, 10, 13, 13}));
            std::vector<std::pair<double, double>> spots;
            for (const auto& s : b["standHere"]) spots.push_back({s[0].get<double>(), s[1].get<double>()});
            EXPECT_EQ(spots.size(), 3u) << b.dump();
            for (const auto& [x, y] : spots) {
                EXPECT_TRUE(x > 11 && x < 13 && y > 11 && y < 13) << x << "," << y;
                EXPECT_FALSE(x == 11.5 && y == 11.5) << "under its furniture";
            }
        } else {
            EXPECT_EQ(b["scene"], "scenes/inn.aige");
            EXPECT_FALSE(b.contains("standHere"));
        }
    }
}
