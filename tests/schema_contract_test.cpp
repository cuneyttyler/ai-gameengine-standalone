#include <gtest/gtest.h>

#include "ai_bridge/ContextBuilder.h"
#include "assets/AssetDatabase.h"
#include "assets/Prefab.h"
#include "assets/Project.h"
#include "assets/SceneIO.h"
#include "editing/Commands.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/GameSession.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#ifndef AIGE_SOURCE_DIR
#define AIGE_SOURCE_DIR "."
#endif

// The contract between the engine and the AI service (technical.md §7.3):
// the schema the editor sends must be exactly what the service's tests were
// written against. ai_service/tests/fixtures/engine_schema.json is a
// snapshot of EngineReflection().ExportSchema(); the Python test suite
// validates its models and proposal checks against the same file.
//
// After changing a registration on purpose, regenerate the snapshot with
//   AIGE_UPDATE_FIXTURES=1 aige_tests --gtest_filter=SchemaContract*
// and commit it together with the change.
TEST(SchemaContractTest, CommittedSnapshotMatchesTheRegistry) {
    aige::RegisterEditingCommands(aige::EngineReflection());
    std::string current = aige::EngineReflection().ExportSchema().dump(2) + "\n";
    std::filesystem::path fixture =
        std::filesystem::path(AIGE_SOURCE_DIR) / "ai_service" / "tests" / "fixtures" / "engine_schema.json";

    if (std::getenv("AIGE_UPDATE_FIXTURES") != nullptr) {
        std::filesystem::create_directories(fixture.parent_path());
        std::ofstream(fixture, std::ios::binary | std::ios::trunc) << current;
    }

    std::ifstream in(fixture, std::ios::binary);
    ASSERT_TRUE(in) << "missing " << fixture << " — generate it with AIGE_UPDATE_FIXTURES=1";
    std::string committed((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(committed, current)
        << "The reflection registry changed but the AI service's schema snapshot didn't. If the change is "
           "intended, regenerate it with AIGE_UPDATE_FIXTURES=1 and run the ai_service tests.";
}

// The demo project as the AI sees it: the other half of the fixtures the
// service's golden tests run against (with engine_schema.json). Regenerated
// the same way; it only needs updating when the demo scene or registry
// changes in a way the golden prompts care about.
TEST(SchemaContractTest, DemoContextSnapshotIsCurrent) {
    if (std::getenv("AIGE_UPDATE_FIXTURES") == nullptr) {
        GTEST_SKIP() << "only regenerated on request (AIGE_UPDATE_FIXTURES=1)";
    }
    // AIGE_DEMO_PROJECT points at a clean copy of the committed demo, so local
    // edits in the working copy don't leak into the fixture:
    //   git archive HEAD projects/demo | tar -x -C <tmp>
    //   AIGE_UPDATE_FIXTURES=1 AIGE_DEMO_PROJECT=<tmp>/projects/demo aige_tests --gtest_filter=*DemoContext*
    const char* demo = std::getenv("AIGE_DEMO_PROJECT");
    aige::Project project{demo ? std::filesystem::path(demo)
                               : std::filesystem::path(AIGE_SOURCE_DIR) / "projects" / "demo"};
    aige::AssetDatabase assets(project);
    assets.Refresh();
    aige::PrefabLibrary prefabs(project);
    prefabs.Refresh();
    aige::Registry registry;
    ASSERT_TRUE(aige::LoadScene(registry, project.MainScenePath().string()));
    prefabs.SyncAll(registry);
    nlohmann::json context = aige::BuildEditorContext(registry, aige::EngineReflection(), prefabs, assets, project,
                                                      {aige::FindPlayer(registry)});
    std::filesystem::path fixture =
        std::filesystem::path(AIGE_SOURCE_DIR) / "ai_service" / "tests" / "fixtures" / "demo_context.json";
    std::ofstream(fixture, std::ios::binary | std::ios::trunc) << context.dump(2) << "\n";
}
