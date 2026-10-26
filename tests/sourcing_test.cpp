#include "assets/Catalog.h"
#include "assets/Sourcing.h"
#include "editing/PlaceBuild.h"
#include "places/Ontology.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

// development_plan.md 7.9, place-assets.md: where a need's art comes from.
using namespace aige;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

const Ontology& Library() {
    static Ontology o = [] {
        Ontology ontology;
        ontology.Load({EnginePlacesDir()});
        return ontology;
    }();
    return o;
}

// A candidate of a given grade, from a given source, with a licence.
struct Synthetic {
    std::vector<CatalogEntry> entries;
    Candidate Make(const std::string& id, const std::string& grade, float fit, const std::string& source = "project", const std::string& licence = "") {
        CatalogEntry e;
        e.id = e.path = id;
        e.source = source;
        e.licence = licence;
        entries.push_back(e);
        Candidate c;
        c.grade = grade;
        c.fit = fit;
        return c;
    }
    // Pointers once all are made (the vector no longer moves).
    std::vector<Candidate> List(std::vector<std::pair<Candidate, size_t>> made) {
        std::vector<Candidate> out;
        for (auto& [c, i] : made) {
            c.entry = &entries[i];
            out.push_back(c);
        }
        return out;
    }
};

const NeedProfile kNormal{Importance::Normal, false};
const NeedProfile kCritical{Importance::Critical, true};
const NeedProfile kOptional{Importance::Optional, false};

} // namespace

TEST(SourcingTest, NeedsHaveAnImportance) {
    EXPECT_EQ(ProfileOf("building", true, false, 3).importance, Importance::Critical);
    EXPECT_TRUE(ProfileOf("building", true, false, 3).styleCritical);
    EXPECT_EQ(ProfileOf("prop", true, false, 3).importance, Importance::Normal);
    EXPECT_FALSE(ProfileOf("prop", true, false, 3).styleCritical);
    EXPECT_TRUE(ProfileOf("decal", true, false, 40).styleCritical); // seen many times
    EXPECT_EQ(ProfileOf("prop", false, false, 3).importance, Importance::Optional);
    EXPECT_EQ(ProfileOf("prop", true, true, 3).importance, Importance::Critical); // a piece set
}

TEST(SourcingTest, TheDecisionTable) {
    Synthetic s;
    s.entries.reserve(16);
    const Candidate good = s.Make("good", "good", 0.8f), usable = s.Make("usable", "usable", 0.6f);
    const Candidate lib = s.Make("lib", "good", 0.9f, "library", "CC0"), web = s.Make("web", "good", 0.85f, "web", "CC-BY");
    const Candidate gen = s.Make("gen", "good", 0.8f, "generated");
    const auto project = s.List({{good, 0}}), projectUsable = s.List({{usable, 1}});
    const auto library = s.List({{lib, 2}}), webs = s.List({{web, 3}}), generated = s.List({{gen, 4}});
    const LicencePolicy licences;
    auto decide = [&](const std::string& policy, const NeedProfile& p, SourceCandidates c, bool webOn, bool canGen, Tried t = {}) {
        return Decide(policy, p, c, licences, webOn, canGen, t);
    };
    // auto
    EXPECT_EQ(decide("auto", kNormal, {project, library, {}}, false, true).step, "A1");
    EXPECT_EQ(decide("auto", kNormal, {projectUsable, library, {}}, false, true).step, "A2");
    EXPECT_EQ(decide("auto", kNormal, {projectUsable, library, {}}, false, true).action, "import");
    EXPECT_EQ(decide("auto", kCritical, {projectUsable, {}, {}}, true, true).step, "A3"); // style-critical: generate, not the web
    EXPECT_EQ(decide("auto", kCritical, {projectUsable, {}, {}}, true, true).action, "generate");
    EXPECT_EQ(decide("auto", kNormal, {projectUsable, {}, {}}, true, true).action, "web");          // A4
    EXPECT_EQ(decide("auto", kNormal, {projectUsable, {}, webs}, true, true).step, "A4");           // a good web find
    EXPECT_EQ(decide("auto", kNormal, {projectUsable, {}, {}}, false, true).step, "A5");            // web off: generate
    EXPECT_EQ(decide("auto", kNormal, {projectUsable, {}, {}}, true, true, {true, true}).step, "A6"); // tried both: the usable one
    EXPECT_EQ(decide("auto", kNormal, {projectUsable, {}, {}}, true, true, {true, true}).action, "adapt");
    EXPECT_EQ(decide("auto", kOptional, {{}, {}, {}}, false, false).action, "drop");                // A7
    EXPECT_EQ(decide("auto", kNormal, {{}, {}, {}}, false, false).action, "placeholder");
    EXPECT_EQ(decide("auto", kCritical, {{}, {}, {}}, false, false).action, "placeholder");
    // project: the project's art; what it has nothing good for is made by the Art tool (2026-10-23),
    // and only when that was tried, the nearest it has, adapted, or a placeholder
    EXPECT_EQ(decide("project", kNormal, {projectUsable, library, webs}, true, true).step, "A5");
    EXPECT_EQ(decide("project", kNormal, {{}, library, webs}, true, true).action, "generate");
    EXPECT_EQ(decide("project", kNormal, {projectUsable, library, webs}, true, true, {false, true}).step, "A6");
    EXPECT_EQ(decide("project", kNormal, {{}, library, webs}, true, true, {false, true}).action, "placeholder");
    // library
    EXPECT_EQ(decide("library", kNormal, {{}, library, {}}, false, true).step, "A2");
    // web: even style-critical, with a warning
    const Decision w = decide("web", kCritical, {{}, {}, {}}, false, true);
    EXPECT_EQ(w.action, "web");
    EXPECT_TRUE(w.warn);
    EXPECT_EQ(decide("web", kNormal, {{}, {}, webs}, false, true).action, "import");
    // generate: the Art tool, then what it made
    EXPECT_EQ(decide("generate", kNormal, {project, {}, {}}, false, true).action, "generate");
    EXPECT_EQ(decide("generate", kNormal, {generated, {}, {}}, false, true, {false, true}).step, "G");
    EXPECT_EQ(decide("generate", kNormal, {{}, {}, {}}, false, false).action, "placeholder"); // no AI
}

TEST(SourcingTest, DisallowedLicencesAreNeverUsed) {
    Synthetic s;
    s.entries.reserve(8);
    const Candidate nc = s.Make("nc", "good", 0.95f, "web", "CC-BY-NC 3.0"), sa = s.Make("sa", "good", 0.9f, "web", "CC-BY-SA 4.0");
    const Candidate unknown = s.Make("unknown", "good", 0.9f, "web", ""), own = s.Make("own", "good", 0.8f, "project", "");
    const auto webs = s.List({{nc, 0}, {sa, 1}, {unknown, 2}});
    LicencePolicy licences;
    const Decision d = Decide("web", kNormal, {{}, {}, webs}, licences, true, false, {true, false});
    EXPECT_NE(d.action, "import"); // none of them allowed
    EXPECT_EQ(d.action, "placeholder");
    licences.shareAlike = true;
    const Decision sharing = Decide("web", kNormal, {{}, {}, webs}, licences, true, false, {});
    ASSERT_TRUE(sharing.candidate.has_value());
    EXPECT_EQ(sharing.candidate->entry->id, "sa");
    // The project's own art (no licence recorded) is the project's.
    EXPECT_EQ(Decide("project", kNormal, {s.List({{own, 3}}), {}, {}}, LicencePolicy{}, false, false, {}).step, "A1");
    EXPECT_EQ(LicencePolicy::Normalise("CC-BY 3.0"), "CC-BY");
    EXPECT_EQ(LicencePolicy::Normalise("OGA-BY 3.0"), "OGA-BY");
    EXPECT_EQ(LicencePolicy::Normalise("unknown"), "");
    EXPECT_FALSE(LicencePolicy{}.Allowed("GPL 3.0"));
    EXPECT_TRUE(LicencePolicy{}.NeedsAttribution("CC-BY 4.0"));
    EXPECT_FALSE(LicencePolicy{}.NeedsAttribution("CC0"));
}

TEST(SourcingTest, CreditsAreWrittenAndKeptUpToDate) {
    const fs::path root = fs::temp_directory_path() / "aige_credits";
    fs::remove_all(root);
    fs::create_directories(root);
    std::ofstream(root / "CREDITS.md") << "# Credits\n\nMusic by me.\n";
    const json used = json::array({{{"path", "assets/web/house/a.png"}, {"licence", "CC-BY 3.0"}, {"author", "Alice"}, {"url", "https://x/a"}},
                                   {{"path", "assets/web/tree/b.png"}, {"licence", "CC0"}, {"author", "Bob"}}});
    ASSERT_TRUE(WriteCredits(root, used, LicencePolicy{}));
    auto read = [&] {
        std::stringstream s;
        s << std::ifstream(root / "CREDITS.md").rdbuf();
        return s.str();
    };
    std::string text = read();
    EXPECT_NE(text.find("Music by me."), std::string::npos); // the user's own text stays
    EXPECT_NE(text.find("a.png by Alice (CC-BY): https://x/a"), std::string::npos);
    EXPECT_EQ(text.find("b.png"), std::string::npos); // CC0 needs no attribution
    // Written again: the section replaced, not doubled.
    ASSERT_TRUE(WriteCredits(root, used, LicencePolicy{}));
    text = read();
    EXPECT_EQ(text.find("aige:credits"), text.rfind("aige:credits (kept"));
    fs::remove_all(root);
}

TEST(SourcingTest, TheLibraryIsEmptyUntilItExists) {
    const fs::path root = fs::temp_directory_path() / "aige_library";
    fs::remove_all(root);
    fs::create_directories(root / "library");
    EXPECT_TRUE(LoadAssetLibrary(root).entries.empty());
    std::ofstream(root / "library" / "catalog.json")
        << R"([{"id":"well","path":"props/well.png","roles":["well"],"licence":"CC0","w":2,"h":2}])";
    const Catalog library = LoadAssetLibrary(root);
    ASSERT_EQ(library.entries.size(), 1u);
    EXPECT_EQ(library.entries[0].source, "library");
    EXPECT_EQ(library.entries[0].path, "library/props/well.png");
    fs::remove_all(root);
}

// The sourcing step: the Art tool, the critic, tries again with the reasons;
// the web tool's finds with their licences.
TEST(SourcingTest, ArtIsMadeCheckedAndFound) {
    const fs::path root = fs::temp_directory_path() / "aige_sourcing";
    fs::remove_all(root);
    fs::create_directories(root);
    std::vector<std::pair<std::string, json>> sent;
    PlaceBuild::Host host;
    host.ontology = []() -> const Ontology& { return Library(); };
    host.catalog = [] { return Catalog{}; };
    host.isoGrid = [] { return true; };
    host.request = [&](const std::string& path, const json& body, int) {
        sent.push_back({path, body});
        return static_cast<int>(sent.size());
    };
    host.projectRoot = root;
    PlaceBuild build(host);
    const json pending = json::array({{{"key", "house||building|4x3"}, {"action", "generate"}, {"role", "house"}, {"concepts", json::array()},
                                       {"layer", "building"}, {"kind", "building"}, {"description", "a house for a game"}},
                                      {{"key", "well||prop"}, {"action", "web"}, {"role", "well"}, {"concepts", json::array()},
                                       {"layer", "prop"}, {"kind", "prop"}, {"query", "isometric well"}}});
    ASSERT_TRUE(build.StartSourcing(pending, SourcingSettings{}));
    ASSERT_EQ(sent.size(), 2u);
    EXPECT_EQ(sent[0].first, "/assets/generate");
    EXPECT_EQ(sent[1].first, "/assets/web-search");
    EXPECT_EQ(build.Stage(), "sourcing");
    // The house: made, but the critic sees a barn: made again, with why.
    build.OnResponse(1, {{"ok", true}, {"assets", json::array({{{"path", "assets/textures/generated/house-1.png"}}})}});
    ASSERT_EQ(sent.back().first, "/assets/describe");
    build.OnResponse(static_cast<int>(sent.size()),
                     {{"ok", true}, {"assets", json::array({{{"path", "assets/textures/generated/house-1.png"}, {"role", "barn"}, {"description", "a barn"}}})}});
    ASSERT_EQ(sent.back().first, "/assets/generate");
    EXPECT_NE(sent.back().second["description"].get<std::string>().find("not a house"), std::string::npos);
    build.OnResponse(static_cast<int>(sent.size()), {{"ok", true}, {"assets", json::array({{{"path", "assets/textures/generated/house-2.png"}}})}});
    build.OnResponse(static_cast<int>(sent.size()),
                     {{"ok", true}, {"assets", json::array({{{"path", "assets/textures/generated/house-2.png"}, {"role", "house"}, {"kind", "building"}, {"description", "a house"}}})}});
    EXPECT_FALSE(build.SourcingDone()); // the well is still out
    // The well: found on the web, with its licence; described.
    build.OnResponse(2, {{"ok", true}, {"assets", json::array({{{"path", "assets/web/isometric-well/well.png"}, {"licence", "CC-BY"},
                                                                 {"author", "Alice"}, {"url", "https://opengameart.org/content/well"}}})}});
    build.OnResponse(static_cast<int>(sent.size()),
                     {{"ok", true}, {"assets", json::array({{{"path", "assets/web/isometric-well/well.png"}, {"role", "well"}, {"description", "a stone well"}}})}});
    ASSERT_TRUE(build.SourcingDone()) << build.State().value("sourcing", json()).dump();
    EXPECT_EQ(build.Stage(), "sourced"); // the art is in: ③ Assets waits for Build
    EXPECT_TRUE(build.TriedSources().at("house||building|4x3").generate);
    EXPECT_TRUE(build.TriedSources().at("well||prop").web);
    const json added = build.TakeNewAssets();
    std::map<std::string, json> byPath;
    for (const auto& a : added) byPath[a["path"]] = a["fields"];
    EXPECT_EQ(byPath["assets/textures/generated/house-2.png"]["source"], "generated");
    EXPECT_EQ(byPath["assets/web/isometric-well/well.png"]["licence"], "CC-BY");
    EXPECT_EQ(byPath["assets/web/isometric-well/well.png"]["author"], "Alice");
    EXPECT_EQ(byPath["assets/web/isometric-well/well.png"]["roles"], "well");
    fs::remove_all(root);
}

// A source the user chose is honoured (2026-10-21): with art already generated into the
// project, "web" still searches the web, and "library" the library; art that came from
// there is used again.
TEST(SourcingTest, AChosenSourceIsSearchedEvenWithGeneratedArtThere) {
    Synthetic s;
    s.entries.reserve(4);
    const Candidate made = s.Make("made", "good", 0.95f, "generated", ""), fetched = s.Make("fetched", "good", 0.9f, "web", "CC0");
    const auto generatedOnly = s.List({{made, 0}});
    const Decision web = Decide("web", kNormal, {generatedOnly, {}, {}}, LicencePolicy{}, true, true, {});
    EXPECT_EQ(web.action, "web") << web.reason;
    // "library": the libraries first, then the project's own art (2026-10-23), then the Art tool.
    const Decision library = Decide("library", kNormal, {generatedOnly, {}, {}}, LicencePolicy{}, false, true, {});
    EXPECT_EQ(library.source, "generated") << library.reason;
    // Art fetched from the web before, in the project: used again, not fetched twice.
    const auto both = s.List({{made, 0}, {fetched, 1}});
    const Decision again = Decide("web", kNormal, {both, {}, {}}, LicencePolicy{}, true, true, {});
    ASSERT_TRUE(again.candidate.has_value());
    EXPECT_EQ(again.candidate->entry->id, "fetched");
    // Searched already and nothing came: what the project has is better than a placeholder.
    const Decision tried = Decide("web", kNormal, {generatedOnly, {}, {}}, LicencePolicy{}, true, true, {false, true});
    EXPECT_NE(tried.action, "placeholder") << tried.reason;
}
