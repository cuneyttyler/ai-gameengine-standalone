#include <gtest/gtest.h>

#include "editing/Commands.h"
#include "gameplay/EngineReflection.h"

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
