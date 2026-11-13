// Builds a project's planned places into their scenes again, without the editor (development_plan.md 32): each place
// whose build is done (built, or its art sourced) is built as the Build panel's Build does it (the art its Assets
// found, the scene placed, checked and repaired; a painted place with its marks: the occluders and walkable ground
// painted in ⑤ Mark), then its story markers. The editor should be closed (it would save its own copy of a scene over).
//
//   build_scenes <project root> [place ...]     e.g. build_scenes "D:/AIGE Projects/Haunted Village" location/pike-arms
#include "ai_bridge/UserPrefs.h"
#include "assets/Project.h"
#include "assets/SceneIO.h"
#include "gamebuild/GameBuild.h"
#include "gameplay/EngineReflection.h"
#include "places/Ontology.h"

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: build_scenes <project root> [place ...]\n";
        return 2;
    }
    const fs::path root = fs::absolute(argv[1]);
    if (!fs::exists(root / "planner" / "buildplan.json")) {
        std::cerr << root.string() << " has no AI Planner build plan (planner/buildplan.json)\n";
        return 1;
    }
    std::vector<std::string> only;
    bool carryOn = false; // --continue: the Planner's next steps (connect, populate, write, playtest) run as Continue would
    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "--continue") carryOn = true;
        else if (std::string(argv[i]) == "--place-characters") only.push_back("--place-characters");
        else if (std::string(argv[i]) == "--replace-characters") only.push_back("--replace-characters");
        else only.push_back(argv[i]);
    }
    // The project's settings (its grid: isometric or top-down), as the editor has them.
    const aige::Project project{root};
    aige::LoadSettings(aige::EngineReflection(), project.SettingsPath().string());
    // The place ontology as the editor loads it: the engine's, the user's own, the project's.
    aige::Ontology places;
    {
        std::vector<fs::path> dirs = {aige::EnginePlacesDir()};
        if (const fs::path prefs = aige::UserPrefs::DefaultPath(); !prefs.empty()) dirs.push_back(prefs.parent_path() / "ontology");
        dirs.push_back(root / "places");
        places.Load(dirs);
    }
    aige::GameBuild::Host host;
    host.projectRoot = root;
    host.places = [&]() -> const aige::Ontology& { return places; };
    aige::GameBuild build(host);
    try {
        if (const std::string why = build.Open(); !why.empty()) {
            std::cerr << "can't open the build plan: " << why << "\n";
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "opening the build plan stopped: " << e.what() << std::endl;
        return 1;
    }
    if (std::find(only.begin(), only.end(), "--replace-characters") != only.end()) {
        // 34.11 Replace Characters, by the rules (the AI's decision needs the editor's AI service): the buildings found, where
        // everyone stands decided (planner/placements.json, locked entries kept), everyone placed by it, its requirements.
        const std::string why = build.ReplaceCharactersNow([](const std::string& line) { std::cout << line << std::endl; });
        if (!why.empty()) std::cout << "not placed: " << why << std::endl;
        return why.empty() ? 0 : 1;
    }
    if (std::find(only.begin(), only.end(), "--place-characters") != only.end()) {
        // ⑪ again: the characters placed in the scenes as they are (nothing rebuilt).
        const std::string why = build.PlaceCharactersAgain([](const std::string& line) { std::cout << line << std::endl; });
        if (!why.empty()) std::cout << "not placed: " << why << std::endl;
        return why.empty() ? 0 : 1;
    }
    if (const auto t = std::find(only.begin(), only.end(), "--start"); t != only.end() && t + 1 != only.end()) {
        // ⑬ Story ▸ Play from here: where a story scene's test and play begin (TestStart).
        try {
            std::cout << build.TestStart(*(t + 1)).dump(1) << std::endl;
        } catch (const std::exception& e) {
            std::cout << "the start couldn't be found: " << e.what() << std::endl;
            return 1;
        }
        return 0;
    }
    if (const auto t = std::find(only.begin(), only.end(), "--test"); t != only.end() && t + 1 != only.end()) {
        // ⑬ Test a story scene, its autopilot run (the AI's judgement needs the editor's AI service): its log.
        try {
            std::cout << build.RunSceneTest(*(t + 1)).dump(1) << std::endl;
        } catch (const std::exception& e) {
            std::cout << "the run stopped: " << e.what() << std::endl;
            return 1;
        }
        return 0;
    }
    if (std::find(only.begin(), only.end(), "--requirements") != only.end()) {
        // 34.1: the requirements by rule (the AI's reading of the scenes needs the editor's AI service), each printed.
        if (const std::string why = build.MakeRequirements(); !why.empty()) {
            std::cout << "not made: " << why << std::endl;
            return 1;
        }
        const auto reqs = build.State().value("requirements", nlohmann::json::object());
        for (const auto& r : reqs.value("items", nlohmann::json::array())) {
            std::cout << r.value("scene", "") << " [" << r.value("kind", "") << "] " << r.value("text", "") << std::endl;
        }
        for (const auto& i : reqs.value("implementations", nlohmann::json::array())) {
            std::cout << "  " << i.value("req", "") << " " << i.value("how", "") << " (" << i.value("by", "") << "): " << i.value("does", "") << std::endl;
        }
        std::cout << reqs.value("items", nlohmann::json::array()).size() << " requirements, "
                  << reqs.value("implementations", nlohmann::json::array()).size() << " implementation items" << std::endl;
        return 0;
    }
    if (std::find(only.begin(), only.end(), "--apply") != only.end()) {
        // Fix all on the findings as they are (planner/buildcheck.json), not validated again; game fixes need the AI panel.
        if (const std::string why = build.FixFindings(nlohmann::json::array()); !why.empty()) std::cout << "not fixed: " << why << std::endl;
        for (int i = 0; i < 50 && build.Pump(); ++i) std::cout << build.Card().value("message", "") << std::endl;
        const auto check = build.State().value("check", nlohmann::json::object());
        if (check.contains("mergeError")) std::cout << "merge: " << check.value("mergeError", "") << std::endl;
        for (const auto& f : check.value("findings", nlohmann::json::array())) {
            std::cout << "  " << f.value("status", "") << ": " << f.value("scene", "") << " " << f.value("what", "") << " " << f.value("error", "") << std::endl;
        }
        return 0;
    }
    if (std::find(only.begin(), only.end(), "--check") != only.end()) {
        // ⑬ Validate, its rules (the AI's look needs the editor's AI service): each finding, and planner/buildcheck.json.
        if (const std::string why = build.CheckBuild(); !why.empty()) {
            std::cout << "not validated: " << why << std::endl;
            return 1;
        }
        build.Pump();
        if (std::find(only.begin(), only.end(), "--fix") != only.end()) {
            // Then every finding with a fix fixed (plan edits: ⑪ again; game fixes need the editor's AI panel), the
            // playtest and result again, and the rules again.
            if (const std::string why = build.FixFindings(nlohmann::json::array()); !why.empty()) std::cout << "not fixed: " << why << std::endl;
            for (int i = 0; i < 50 && build.Pump(); ++i) std::cout << build.Card().value("message", "") << std::endl;
            for (const auto& f : build.State().value("check", nlohmann::json::object()).value("findings", nlohmann::json::array())) {
                std::cout << "  " << f.value("status", "") << ": " << f.value("what", "") << " " << f.value("error", "") << std::endl;
            }
            if (const std::string why = build.CheckBuild(); !why.empty()) std::cout << "not validated again: " << why << std::endl;
            build.Pump();
        }
        const auto check = build.State().value("check", nlohmann::json::object());
        for (const auto& t : check.value("trace", nlohmann::json::array())) {
            std::cout << t.value("scene", "") << " [" << t.value("mode", "") << "] " << t.value("goal", "") << " @ " << t.value("locationScene", "")
                      << " | stages " << t.value("stages", nlohmann::json::array()).size() << ", features " << t.value("features", nlohmann::json::array()).size()
                      << ", scripts " << t.value("scripts", nlohmann::json::array()).size() << ", packages " << t.value("packages", nlohmann::json::array()).size() << std::endl;
        }
        for (const auto& f : check.value("findings", nlohmann::json::array())) {
            std::cout << f.value("severity", "") << " " << f.value("scene", "") << ": " << f.value("what", "") << " — " << f.value("detail", "")
                      << " [fix: " << f.value("fix", nlohmann::json::object()).dump() << "]" << std::endl;
        }
        return 0;
    }
    if (carryOn) {
        if (const std::string why = build.Resume(); !why.empty()) std::cout << "resume: " << why << std::endl;
        for (int i = 0; i < 400; ++i) {
            bool did = false;
            try {
                did = build.Pump();
            } catch (const std::exception& e) {
                std::cout << "Pump threw: " << e.what() << std::endl;
                return 1;
            }
            const auto card = build.Card();
            std::cout << card.value("stage", "") << " / " << card.value("status", "") << " | " << card.value("message", "") << std::endl;
            if (!did) break;
        }
        return 0;
    }
    const auto built = build.RebuildScenes(only, [](const std::string& line) { std::cout << line << std::endl; });
    std::cout << built.size() << " scene(s) built" << std::endl;
    return built.empty() ? 1 : 0;
}
