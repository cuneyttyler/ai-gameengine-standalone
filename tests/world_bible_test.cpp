#include <gtest/gtest.h>

#include "ai_bridge/Proposal.h"
#include "assets/Prefab.h"
#include "assets/Project.h"
#include "editing/Commands.h"
#include "gameplay/EngineReflection.h"
#include "world/WorldBible.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#ifndef AIGE_SOURCE_DIR
#define AIGE_SOURCE_DIR "."
#endif

namespace fs = std::filesystem;

namespace {

class WorldBibleTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("aige_world_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root_);
        fs::create_directories(root_);
        project_ = aige::Project{root_};
    }
    void TearDown() override { fs::remove_all(root_); }

    aige::WorldEntry Entry(std::string kind, std::string id, std::string name) {
        aige::WorldEntry entry;
        entry.kind = std::move(kind);
        entry.id = std::move(id);
        entry.name = std::move(name);
        return entry;
    }
    void WriteFile(const std::string& relative, const std::string& text) {
        fs::create_directories((root_ / relative).parent_path());
        std::ofstream(root_ / relative, std::ios::binary) << text;
    }
    std::string ReadFile(const std::string& relative) {
        std::ifstream in(root_ / relative, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    }

    fs::path root_;
    aige::Project project_;
};

} // namespace

TEST_F(WorldBibleTest, EntriesRoundTripThroughTheirFiles) {
    aige::WorldEntry mara = Entry("character", "mara", "Mara the Smith");
    mara.summary = "The village blacksmith.";
    mara.tags = {"blacksmith", "brookvale"};
    mara.relations = {{"lives_in", "location/brookvale"}, {"member_of", "faction/iron-guild"}};
    mara.body = "Gruff but fair.\n\n- Owes the miller money.";
    mara.extra = {{"voice", "low, slow"}}; // a hand-written key this version doesn't know

    aige::WorldBible world(project_);
    ASSERT_FALSE(world.Save(mara));
    const std::string text = ReadFile("world/character/mara.md");
    EXPECT_EQ(text,
              "---\nname: Mara the Smith\nsummary: The village blacksmith.\ntags: blacksmith, brookvale\n"
              "rel: lives_in location/brookvale\nrel: member_of faction/iron-guild\nvoice: low, slow\n---\n"
              "Gruff but fair.\n\n- Owes the miller money.\n");

    const aige::WorldEntry* loaded = world.Find("character/mara");
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->name, mara.name);
    EXPECT_EQ(loaded->summary, mara.summary);
    EXPECT_EQ(loaded->tags, mara.tags);
    ASSERT_EQ(loaded->relations.size(), 2u);
    EXPECT_EQ(loaded->relations[1].type, "member_of");
    EXPECT_EQ(loaded->relations[1].target, "faction/iron-guild");
    EXPECT_EQ(loaded->body, mara.body);
    EXPECT_EQ(loaded->extra, mara.extra);
    EXPECT_EQ(aige::FormatWorldEntry(*loaded), text); // byte-stable
}

TEST_F(WorldBibleTest, HandWrittenFilesAreForgiving) {
    WriteFile("world/location/brookvale.md",
              "\n---\r\nname:   Brookvale  \r\ntags: village,, home ,\r\nrel: near   location/old-keep\r\n"
              "kind: ignored-the-folder-decides\r\n\r\n---\r\n\r\nA village.\r\n\r\n");
    aige::WorldBible world(project_);
    world.Refresh();
    const aige::WorldEntry* entry = world.Find("location/brookvale");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->kind, "location");
    EXPECT_EQ(entry->name, "Brookvale");
    EXPECT_EQ(entry->tags, (std::vector<std::string>{"village", "home"}));
    ASSERT_EQ(entry->relations.size(), 1u);
    EXPECT_EQ(entry->relations[0].type, "near");
    EXPECT_EQ(entry->relations[0].target, "location/old-keep");
    EXPECT_EQ(entry->body, "A village.");
}

TEST_F(WorldBibleTest, ValidationReportsWhatAWriterWouldWantToKnow) {
    WriteFile("world/character/nobody.md", "---\nsummary: no name\n---\n");
    WriteFile("world/character/broken.md", "just text, no header\n");
    WriteFile("world/character/Bad Name.md", "---\nname: Bad\nrel: knows character/ghost\n---\n");
    WriteFile("world/spaceship/ignored.md", "---\nname: Not a kind\n---\n"); // unknown folders aren't read
    aige::WorldBible world(project_);
    world.Refresh();
    EXPECT_EQ(world.Entries().size(), 2u);

    std::vector<std::string> messages;
    for (const auto& problem : world.Validate()) {
        messages.push_back(problem.key + ": " + problem.message);
    }
    auto has = [&](const std::string& text) {
        return std::any_of(messages.begin(), messages.end(),
                           [&](const std::string& m) { return m.find(text) != std::string::npos; });
    };
    EXPECT_TRUE(has("character/broken: doesn't start with a --- header"));
    EXPECT_TRUE(has("character/nobody: has no name"));
    EXPECT_TRUE(has("character/Bad Name: file name should be"));
    EXPECT_TRUE(has("knows points at character/ghost, which doesn't exist"));
    EXPECT_EQ(messages.size(), 4u);
}

TEST_F(WorldBibleTest, SaveRefusesWhatCouldNotBeReadBack) {
    aige::WorldBible world(project_);
    EXPECT_TRUE(world.Save(Entry("spaceship", "x", "X")));
    EXPECT_TRUE(world.Save(Entry("character", "Has Spaces", "X")));
    EXPECT_TRUE(world.Save(Entry("character", "x", "  ")));
    EXPECT_FALSE(fs::exists(root_ / "world"));
    EXPECT_EQ(aige::WorldIdFromName("Mara the Smith!"), "mara-the-smith");
    EXPECT_EQ(aige::WorldIdFromName("  Old  Keep (ruins) "), "old-keep-ruins");
    EXPECT_EQ(aige::WorldIdFromName("!!!"), "");
}

TEST_F(WorldBibleTest, RenameMovesTheFileAndRewritesRelationsToIt) {
    aige::WorldBible world(project_);
    aige::WorldEntry keep = Entry("location", "keep", "The Old Keep");
    aige::WorldEntry village = Entry("location", "brookvale", "Brookvale");
    village.relations = {{"near", "location/keep"}};
    aige::WorldEntry dead = Entry("faction", "dead", "The Restless Dead");
    dead.relations = {{"haunts", "location/keep"}, {"fears", "faction/wardens"}};
    ASSERT_FALSE(world.Save(keep));
    ASSERT_FALSE(world.Save(village));
    ASSERT_FALSE(world.Save(dead));

    ASSERT_FALSE(world.Rename("location/keep", "location/old-keep"));
    EXPECT_FALSE(fs::exists(root_ / "world/location/keep.md"));
    EXPECT_NE(world.Find("location/old-keep"), nullptr);
    EXPECT_EQ(world.Find("location/brookvale")->relations[0].target, "location/old-keep");
    EXPECT_EQ(world.Find("faction/dead")->relations[0].target, "location/old-keep");
    EXPECT_EQ(world.Find("faction/dead")->relations[1].target, "faction/wardens"); // untouched

    EXPECT_TRUE(world.Rename("location/old-keep", "location/brookvale")); // taken
    EXPECT_TRUE(world.Rename("location/old-keep", "nowhere/x"));
    EXPECT_TRUE(world.Rename("location/missing", "location/y"));

    ASSERT_FALSE(world.Remove("faction/dead"));
    EXPECT_EQ(world.Find("faction/dead"), nullptr);
    EXPECT_FALSE(fs::exists(root_ / "world/faction/dead.md"));
}

// The AI proposes canon like any other change: a world.write command, shown
// in Diff Review as the file it will write.
TEST_F(WorldBibleTest, TheAiWritesEntriesThroughAReviewableCommand) {
    aige::RegisterEditingCommands(aige::EngineReflection());
    aige::PrefabLibrary prefabs(project_);
    aige::Registry registry;
    nlohmann::json op = {{"op", "command"},
                         {"name", "world.write"},
                         {"args",
                          {{"kind", "character"},
                           {"id", ""},
                           {"name", "Old Tamsin"},
                           {"summary", "Herb-keeper."},
                           {"tags", "villager, healer"},
                           {"relations", "lives_in location/brookvale"},
                           {"body", "Gruff and kind."}}}};
    nlohmann::json proposal = {{"summary", "x"}, {"operations", nlohmann::json::array({op})}};
    auto resolved = aige::ResolveProposal(proposal, registry, aige::EngineReflection(), prefabs, project_);
    ASSERT_EQ(resolved.items.size(), 1u);
    const auto item = resolved.items[0];
    EXPECT_TRUE(item.error.empty()) << item.error;
    EXPECT_TRUE(item.touchesFiles);
    EXPECT_EQ(item.target, "World Bible · character/old-tamsin (Old Tamsin)");
    EXPECT_NE(item.detail.find("rel: lives_in location/brookvale"), std::string::npos) << item.detail;

    auto outcome = aige::ApplyOperations({op}, registry, aige::EngineReflection(), prefabs, project_);
    ASSERT_TRUE(outcome.errors.empty()) << outcome.errors[0];
    aige::WorldBible world(project_);
    world.Refresh();
    const aige::WorldEntry* tamsin = world.Find("character/old-tamsin");
    ASSERT_NE(tamsin, nullptr);
    EXPECT_EQ(tamsin->tags, (std::vector<std::string>{"villager", "healer"}));
    EXPECT_EQ(aige::FormatWorldEntry(*tamsin), item.detail); // the review showed exactly this

    // Replacing it says so; deleting goes through world.delete.
    resolved = aige::ResolveProposal(proposal, registry, aige::EngineReflection(), prefabs, project_);
    EXPECT_EQ(resolved.items[0].before, "(replaces the existing entry)");
    nlohmann::json remove = {{"op", "command"}, {"name", "world.delete"}, {"args", {{"key", "character/old-tamsin"}}}};
    outcome = aige::ApplyOperations({remove}, registry, aige::EngineReflection(), prefabs, project_);
    ASSERT_TRUE(outcome.errors.empty()) << outcome.errors[0];
    EXPECT_FALSE(fs::exists(root_ / "world/character/old-tamsin.md"));
}

