#include "assets/UserLibrary.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
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

// Every project's own art in a folder into the user's library (AIGE_LIBRARY_IMPORT=<projects folder>|<library folder>):
// a folder per project ("Projects ▸ <name>"), a folder inside per kind of art (matched, textures, web…). Left out: the
// painted ground's chunks (one place's background, cut up), placeholders, and what came from the library already.
TEST(UserLibraryTest, ProjectsArtIntoTheLibrary) {
    const char* env = std::getenv("AIGE_LIBRARY_IMPORT");
    if (env == nullptr) GTEST_SKIP() << "AIGE_LIBRARY_IMPORT=<projects folder>|<library folder>";
    const std::string spec = env;
    const fs::path projects = spec.substr(0, spec.find('|')), dir = spec.substr(spec.find('|') + 1);
    aige::UserLibrary library(dir);
    // A folder's id by its name and parent, made when there's none (NewFolder answers "" when done).
    auto folderNamed = [&](const std::string& name, const std::string& parent) {
        auto find = [&]() -> std::string {
            for (const auto& f : library.State().value("folders", nlohmann::json::array())) {
                if (f.value("name", "") == name && f.value("parent", "") == parent) return f.value("id", "");
            }
            return {};
        };
        if (std::string id = find(); !id.empty()) return id;
        EXPECT_EQ(library.NewFolder(name, parent), "");
        return find();
    };
    // An earlier run's folders made at the top by mistake (all but Generated and Projects): gone, their items at the top.
    for (const auto& f : library.State().value("folders", nlohmann::json::array())) {
        const std::string id = f.value("id", "");
        if (f.value("parent", "") == "" && id != "Generated" && id != "Projects") library.DeleteFolder(id);
    }
    const std::string top = folderNamed("Projects", "");
    auto folderOf = [&](const std::string& item) {
        for (const auto& i : library.State().value("items", nlohmann::json::array())) {
            if (i.value("id", "") == item) return i.value("folder", "");
        }
        return std::string();
    };
    int added = 0, seen = 0;
    for (const auto& project : fs::directory_iterator(projects)) {
        const fs::path assets = project.path() / "assets";
        if (!project.is_directory() || !fs::is_directory(assets)) continue;
        const std::string name = project.path().filename().string();
        const std::string mine = folderNamed(name, top);
        std::map<std::string, std::string> kinds;
        for (const auto& entry : fs::recursive_directory_iterator(assets)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext != ".png" && ext != ".jpg" && ext != ".jpeg") continue;
            const std::string rel = fs::relative(entry.path(), assets).generic_string();
            if (rel.rfind("generated/ground/", 0) == 0 || rel.rfind("generated/placeholders/", 0) == 0 || rel.rfind("library/", 0) == 0) continue;
            const std::string kind = rel.substr(0, rel.find('/') == std::string::npos ? 0 : rel.find('/'));
            const std::string label = kind.empty() ? "Other" : kind;
            if (!kinds.contains(label)) kinds[label] = folderNamed(label, mine);
            ++seen;
            const std::string id = library.Add(entry.path(), {{"name", entry.path().stem().string()}, {"source", "project"}, {"origin", name + "/assets/" + rel}},
                                               kinds[label]);
            added += !id.empty();
            // New here (or left at the top before): into its folder. Art already kept elsewhere (Generated) stays there.
            if (!id.empty() && folderOf(id).empty()) library.Move(id, kinds[label]);
        }
    }
    std::cout << "art seen: " << seen << ", in the library: " << added << "\n";
    EXPECT_EQ(added, seen);
}
