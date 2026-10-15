#include <gtest/gtest.h>

#include "ai_bridge/Proposal.h"
#include "assets/Prefab.h"
#include "assets/Project.h"
#include "assets/SceneIO.h"
#include "editing/Commands.h"
#include "editing/Edits.h"
#include "gameplay/Components.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/Settings.h"
#include "scripting/Components.h"
#include "scripting/LuaVM.h"

#include <filesystem>
#include <fstream>

#ifndef AIGE_SOURCE_DIR
#define AIGE_SOURCE_DIR "."
#endif

namespace fs = std::filesystem;

namespace {

std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// One copy of the demo project, loaded the way the editor loads it.
struct ProjectCopy {
    explicit ProjectCopy(const std::string& name) {
        fs::path source = fs::path(AIGE_SOURCE_DIR) / "projects" / "demo";
        project.root = fs::temp_directory_path() / ("aige_edit_parity_" + name);
        fs::remove_all(project.root);
        fs::create_directories(project.root);
        for (const char* dir : {"scenes", "scripts", "prefabs"}) {
            fs::copy(source / dir, project.root / dir, fs::copy_options::recursive);
        }
        prefabs = std::make_unique<aige::PrefabLibrary>(project);
        prefabs->Refresh();
        aige::LoadScene(registry, project.MainScenePath().string());
        prefabs->SyncAll(registry);
    }
    ~ProjectCopy() { fs::remove_all(project.root); }

    aige::Entity Named(const std::string& name) {
        for (auto [entity, n] : registry.view<aige::Name>().each()) {
            if (n.value == name) {
                return entity;
            }
        }
        return entt::null;
    }

    void Save() {
        aige::SaveScene(registry, project.MainScenePath().string());
        aige::SaveSettings(aige::EngineReflection(), project.SettingsPath().string());
    }

    aige::Project project;
    std::unique_ptr<aige::PrefabLibrary> prefabs;
    aige::LuaVM lua;
    aige::Registry registry;
};

} // namespace

// development_plan.md 2.6: an edit made by hand and the same edit made by the
// AI must leave identical files. The Inspector calls editing/Edits directly;
// an applied proposal goes through ApplyOperations. Both must produce
// byte-identical scene and settings files — no conversion, no AI-only format.
TEST(EditParityTest, SameEditsByHandAndByAiProduceIdenticalFiles) {
    aige::RegisterEditingCommands(aige::EngineReflection());
    aige::ProjectSettings saved = aige::ActiveSettings();

    // --- By hand: what the Inspector does for each edit -----------------------
    aige::ActiveSettings() = aige::ProjectSettings{};
    ProjectCopy manual("manual");
    aige::Entity hero = manual.Named("Hero");
    aige::Entity villager = manual.Named("Villager");
    aige::Entity skeleton = manual.Named("Skeleton");
    ASSERT_TRUE(manual.registry.valid(hero) && manual.registry.valid(villager) && manual.registry.valid(skeleton));
    const auto& r = aige::EngineReflection();
    EXPECT_FALSE(aige::EditField(r, manual.registry, hero, "MeleeAttack", "effect", "Burn"));
    EXPECT_FALSE(aige::EditField(r, manual.registry, hero, "MeleeAttack", "effectDuration", 3.0));
    EXPECT_FALSE(aige::EditAddComponent(r, manual.registry, manual.prefabs.get(), villager, "Follow",
                                        {{"speed", 1.5}}));
    EXPECT_FALSE(aige::EditRemoveComponent(r, manual.registry, skeleton, "Follow"));
    EXPECT_FALSE(aige::EditScriptProperty(manual.registry, skeleton, "swing_range", 1.6));
    EXPECT_FALSE(aige::EditSetting(r, "Combat", "knockbackScale", 1.5));
    manual.Save();

    // --- By the AI: the same changes as an applied proposal --------------------
    aige::ActiveSettings() = aige::ProjectSettings{};
    ProjectCopy driven("driven");
    auto id = [&](const std::string& name) { return static_cast<uint32_t>(driven.Named(name)); };
    std::vector<nlohmann::json> operations = {
        {{"op", "set_field"}, {"entity", id("Hero")}, {"component", "MeleeAttack"}, {"field", "effect"}, {"value", "Burn"}},
        {{"op", "set_field"}, {"entity", id("Hero")}, {"component", "MeleeAttack"}, {"field", "effectDuration"}, {"value", 3.0}},
        {{"op", "add_component"}, {"entity", id("Villager")}, {"component", "Follow"}, {"fields", {{"speed", 1.5}}}},
        {{"op", "remove_component"}, {"entity", id("Skeleton")}, {"component", "Follow"}},
        {{"op", "set_script_property"}, {"entity", id("Skeleton")}, {"name", "swing_range"}, {"value", 1.6}},
        {{"op", "set_setting"}, {"section", "Combat"}, {"field", "knockbackScale"}, {"value", 1.5}},
    };
    auto outcome = aige::ApplyOperations(operations, driven.registry, r, *driven.prefabs, driven.project);
    ASSERT_TRUE(outcome.errors.empty()) << outcome.errors[0];
    EXPECT_EQ(outcome.applied, 6);
    driven.Save();

    EXPECT_EQ(ReadFile(manual.project.MainScenePath()), ReadFile(driven.project.MainScenePath()));
    EXPECT_EQ(ReadFile(manual.project.SettingsPath()), ReadFile(driven.project.SettingsPath()));
    // And the edits really happened (identical-but-untouched would also be equal).
    EXPECT_NE(ReadFile(manual.project.MainScenePath()),
              ReadFile(fs::path(AIGE_SOURCE_DIR) / "projects" / "demo" / "scenes" / "main.aige"));
    EXPECT_NE(ReadFile(manual.project.MainScenePath()).find("effect=Burn"), std::string::npos);

    aige::ActiveSettings() = saved;
}
