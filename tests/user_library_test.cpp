#include "assets/UserLibrary.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iostream>

// development_plan.md 10: the user's own library: folders, generated art kept across
// projects, items into a project, sharing as chosen, exported and imported.
using namespace aige;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
void WriteFile(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << bytes;
}
} // namespace

TEST(UserLibraryTest, GeneratedArtIsKeptOrganisedSharedAndExported) {
    const fs::path base = fs::temp_directory_path() / "aige_user_library";
    fs::remove_all(base);
    const fs::path project = base / "Village";
    WriteFile(project / "assets/textures/generated/house-1.png", "png-house");
    WriteFile(project / "assets/textures/generated/house-1.png.meta", "# AIGE Asset v1\nname=stone-house\nsource=generated\nroles=house\n");
    WriteFile(project / "assets/textures/generated/terrain-grass-1-seamless.png", "derived");
    WriteFile(project / "assets/textures/generated/tree-1.png", "png-tree");

    UserLibrary library(base / "library");
    EXPECT_EQ(library.KeepGenerated(project), 2); // the seamless copy is made from one that's kept
    EXPECT_EQ(library.KeepGenerated(project), 0); // nothing twice
    json state = library.State();
    ASSERT_EQ(state["items"].size(), 2u);
    EXPECT_TRUE(state["askSharing"].get<bool>()); // generated art, and no choice made yet
    const std::string generated = state["items"][0]["folder"];
    EXPECT_FALSE(generated.empty());
    std::string house;
    for (const auto& i : state["items"]) {
        if (i["name"] == "stone-house") house = i["id"];
        EXPECT_EQ(i["source"], "generated");
        EXPECT_EQ(i["shared"], "unasked");
        EXPECT_EQ(i["origin"], "Village");
    }
    ASSERT_FALSE(house.empty());

    // Sharing: chosen once, it applies to what was waiting; an item can differ.
    ASSERT_EQ(library.SetSharing("always"), "");
    const json shared = library.State()["items"];
    for (const auto& i : shared) EXPECT_EQ(i["shared"], "yes");
    ASSERT_EQ(library.SetShared(house, "no"), "");
    EXPECT_FALSE(library.State()["askSharing"].get<bool>());

    // Folders: made, an item moved into one, then the folder removed (its items go up).
    ASSERT_EQ(library.NewFolder("Houses"), "");
    std::string houses;
    const json folders = library.State()["folders"];
    for (const auto& f : folders) {
        if (f["name"] == "Houses") houses = f["id"];
    }
    ASSERT_EQ(library.Move(house, houses), "");
    EXPECT_NE(library.Move(house, "no-such-folder"), "");

    // Into another project, with its description.
    const fs::path other = base / "Castle";
    const std::string path = library.CopyToProject(house, other);
    EXPECT_EQ(path.rfind("assets/library/", 0), 0u);
    EXPECT_TRUE(fs::exists(other / path));
    EXPECT_TRUE(fs::exists(other / (path + ".meta")));

    // Exported (the Houses folder), imported into another library.
    ASSERT_EQ(library.Export(houses, base / "out"), "");
    UserLibrary friendLib(base / "friend");
    ASSERT_EQ(friendLib.Import(base / "out" / "Houses.aige-library"), "");
    const json theirs = friendLib.State();
    ASSERT_EQ(theirs["items"].size(), 1u);
    EXPECT_EQ(theirs["items"][0]["name"], "stone-house");
    EXPECT_EQ(friendLib.Import(base / "nowhere"), "not an AIGE library folder (no library.json)");

    ASSERT_EQ(library.DeleteFolder(houses), "");
    const json after = library.State()["items"];
    for (const auto& i : after) {
        if (i["id"] == house) EXPECT_EQ(i["folder"], "");
    }
    ASSERT_EQ(library.Remove(house), "");
    EXPECT_EQ(library.State()["items"].size(), 1u);
    fs::remove_all(base);
}
