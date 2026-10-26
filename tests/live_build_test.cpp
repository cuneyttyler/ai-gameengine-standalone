// A real build, end to end, with the real AI service (2026-10-21, docs/test.md): the same
// steps the Build panel runs (Confirm, Layout, Plan, Assets, Result), each step's picture and
// data written out to be judged. Skipped unless asked for:
//
//   AIGE_LIVE_BUILD="<request>|<out dir>|<service port>[|web]"
//
// The service must already be running on that port (uvicorn app.main:app --port <port>), with
// no AIGE_SERVICE_TOKEN (it's then open to this harness). A fresh project is made in <out
// dir>/project, so every piece of art is found or made from nothing.
#include "assets/ArtAnalysis.h"
#include "assets/AssetDatabase.h"
#include "assets/Catalog.h"
#include "assets/Prefab.h"
#include "assets/SceneIO.h"
#include "assets/ProjectTemplate.h"
#include "assets/Sourcing.h"
#include "assets/TextureCook.h"
#include "assets/UserLibrary.h"
#include "editing/Autotile.h"
#include "editing/EditHistory.h"
#include "editing/PlaceBuild.h"
#include "editing/PlaceBlockout.h"
#include "editing/PlaceInspect.h"
#include "editing/PlacePaint.h"
#include "editing/PlaceSketch.h"
#include "gamebuild/GameBuild.h"
#include "editing/PlaceScene.h"
#include "editing/ScenePreview.h"
#include "places/Brief.h"
#include "places/Compile.h"
#include "places/Ontology.h"
#include "places/Solver.h"

#include <gtest/gtest.h>
#include <httplib.h>

#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>

using namespace aige;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

struct Call {
    int id;
    std::string path;
    json body;
    int timeout;
};

void WriteText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

json ReadFile(const fs::path& p) {
    std::ifstream in(p);
    return json::parse(in);
}

// A saved layout again, with what the compiler adds now that it didn't then (the woods),
// solved: the same zones, seed and parcels. Returns the new statements' count.
int Resolve(const fs::path& placeFolder, json& solved) {
    Ontology o;
    o.Load({EnginePlacesDir()});
    const json spec = ReadFile(placeFolder / "spec.json");
    json ir = ReadFile(placeFolder / "layout.ir.json");
    CompileOptions co;
    co.seed = ir.value("seed", uint64_t{7});
    co.side = ir["grid"]["bounds"][2].get<int>();
    std::set<std::string> have;
    for (const auto& st : ir["statements"]) have.insert(st.value("id", ""));
    int added = 0;
    for (const auto& st : CompileSpec(o, spec, co).value("statements", json::array())) {
        if (!have.contains(st.value("id", "")) && st.value("id", "").find("landscape-") != std::string::npos) {
            ir["statements"].push_back(st);
            ++added;
        }
    }
    solved = SolveLayout(o, ir).document;
    std::ofstream(placeFolder / "layout.ir.json") << ir.dump(1);
    std::ofstream(placeFolder / "layout.solved.json") << solved.dump(1);
    return added;
}

json Rows(const json& plan) {
    json needs = plan.value("needs", json::array());
    const json terrains = plan.value("terrains", json::object());
    for (const auto& [t, g] : terrains.items()) {
        needs.push_back({{"key", "terrain|" + t}, {"role", "ground-tile"}, {"concepts", json::array({t})}, {"source", g.value("source", "")},
                         {"entry", g.value("entry", g.value("terrain", ""))}, {"why", g.value("why", "")}, {"title", t + " ground"}});
    }
    const json parts = plan.value("parts", json::object());
    for (const auto& [part, p] : parts.items()) {
        needs.push_back({{"key", "part|" + part}, {"role", p.value("role", part)}, {"concepts", json::array({part})}, {"source", p.value("source", "")},
                         {"entry", p.value("entry", "")}, {"why", p.value("why", "")}, {"title", part}});
    }
    const json pieces = plan.value("pieces", json::object());
    for (const auto& [slot, p] : pieces.items()) {
        needs.push_back({{"key", "piece|" + slot}, {"role", p.value("role", slot)}, {"concepts", json::array({slot})}, {"source", p.value("source", "")},
                         {"entry", p.value("entry", p.value("set", ""))}, {"why", p.value("why", "")}, {"title", slot + " section"}});
    }
    return needs;
}

} // namespace

TEST(LiveBuildTest, ARealBuildEndToEnd) {
    const char* arg = std::getenv("AIGE_LIVE_BUILD");
    if (arg == nullptr) GTEST_SKIP() << "set AIGE_LIVE_BUILD=<request>|<out dir>|<port>[|web]";
    std::vector<std::string> parts;
    {
        std::string text = arg, part;
        std::istringstream in(text);
        while (std::getline(in, part, '|')) parts.push_back(part);
    }
    ASSERT_GE(parts.size(), 3u);
    const std::string request = parts[0];
    const fs::path out = parts[1];
    const int port = std::stoi(parts[2]);
    const bool web = parts.size() > 3 && parts[3] == "web";
    fs::remove_all(out / "project");
    fs::create_directories(out);
    const fs::path project = out / "project";
    ASSERT_FALSE(CreateProject(project, std::nullopt, false).has_value());
    std::ofstream log(out / "run.log");
    const auto started = std::chrono::steady_clock::now();
    auto say = [&](const std::string& text) {
        const auto s = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count();
        log << "[" << s << "s] " << text << std::endl;
        std::cout << "[" << s << "s] " << text << std::endl;
    };

    // The service, told which project is open.
    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(5);
    client.set_read_timeout(900);
    {
        const auto r = client.Put("/project", json{{"root", fs::absolute(project).string()}}.dump(), "application/json");
        ASSERT_TRUE(r && r->status == 200) << "the AI service isn't answering on port " << port;
    }

    Ontology ontology;
    ontology.Load({EnginePlacesDir()});
    PrefabLibrary prefabs(Project{project});
    AssetDatabase assets(Project{project});
    assets.Refresh();
    CatalogOptions catalogOptions;
    catalogOptions.isoGrid = true;

    std::deque<Call> queue;
    int nextId = 1;
    PlaceBuild::Host host;
    host.ontology = [&]() -> const Ontology& { return ontology; };
    host.catalog = [&] { return BuildCatalog(assets, &ontology, catalogOptions); };
    host.isoGrid = [] { return true; };
    host.request = [&](const std::string& path, const json& body, int timeout) {
        queue.push_back({nextId, path, body, timeout});
        return nextId++;
    };
    host.projectRoot = project;
    host.layoutStep = true;
    PlaceBuild build(host);

    // The requests, up to four at a time as the editor sends them; each answer to the build (on
    // this thread) as it comes.
    struct Flight {
        Call call;
        std::chrono::steady_clock::time_point t0;
        std::future<json> answer;
    };
    auto pump = [&] {
        std::vector<Flight> flying;
        while (!queue.empty() || !flying.empty()) {
            while (!queue.empty() && flying.size() < 4) {
                Call call = queue.front();
                queue.pop_front();
                auto answer = std::async(std::launch::async, [port, path = call.path, body = call.body.dump()] {
                    httplib::Client c("127.0.0.1", port);
                    c.set_connection_timeout(5);
                    c.set_read_timeout(900);
                    const auto r = c.Post(path, body, "application/json");
                    json out = r && r->status == 200 ? json::parse(r->body, nullptr, false) : json{{"ok", false}, {"errors", {"no answer"}}};
                    return out.is_discarded() ? json{{"ok", false}, {"errors", {"not JSON"}}} : out;
                });
                flying.push_back({std::move(call), std::chrono::steady_clock::now(), std::move(answer)});
            }
            for (size_t i = 0; i < flying.size();) {
                if (flying[i].answer.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready) {
                    ++i;
                    continue;
                }
                Flight done = std::move(flying[i]);
                flying.erase(flying.begin() + static_cast<std::ptrdiff_t>(i));
                const json body = done.answer.get();
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - done.t0).count();
                say(done.call.path + " " + (body.value("ok", false) ? "ok" : "failed") + " (" + std::to_string(ms / 1000) + "s)");
                build.OnResponse(done.call.id, body);
            }
        }
    };
    auto copy = [&](const fs::path& from, const std::string& name) {
        std::error_code ec;
        if (fs::exists(from, ec)) fs::copy_file(from, out / name, fs::copy_options::overwrite_existing, ec);
    };

    // From a run's saved place (AIGE_LIVE_FROM=<project>|<place id>): the same plan, only
    // ④ and ⑤ again, so two versions of the asset steps are judged on one layout.
    if (const char* from = std::getenv("AIGE_LIVE_FROM")) {
        const std::string text = from;
        const fs::path source = fs::path(text.substr(0, text.find('|'))) / "places" / text.substr(text.find('|') + 1);
        const fs::path into = project / "places" / source.filename();
        fs::create_directories(into);
        for (const auto& entry : fs::directory_iterator(source)) {
            if (entry.is_regular_file()) fs::copy_file(entry.path(), into / entry.path().filename(), fs::copy_options::overwrite_existing);
        }
        fs::copy(source / "revisions", into / "revisions", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        // Its session up to the plan (no art made yet).
        std::ifstream in(source / "session.json");
        json session = json::parse(in);
        session.erase("sourcing");
        session.erase("assets");
        session.erase("sourced");
        session.erase("tried");
        session["stage"] = "planned";
        WriteText(into / "session.json", session.dump(2));
        if (std::getenv("AIGE_LIVE_RESOLVE") != nullptr) {
            json solved;
            say("re-solved with " + std::to_string(Resolve(into, solved)) + " new statements");
        }
        ASSERT_EQ(build.Open(source.filename().string()), "");
        pump();
        say("from " + source.string() + ": " + build.Stage());
    } else {
    // ① Confirm and ② Layout: the request read three ways, the zones planned and laid out.
    say("request: " + request);
    ASSERT_EQ(build.Start(request), "");
    pump();
    ASSERT_EQ(build.Stage(), "layout") << build.State().value("status", "");
    WriteText(out / "1-confirm-brief.json", build.Brief().dump(2));
    copy(build.Folder() / "zones.png", "2-layout.png");
    {
        const json state = build.State();
        WriteText(out / "2-layout.json", state.value("layout", json::object()).dump(2));
    }
    say("layout: " + std::to_string(build.State()["layout"]["zones"].size()) + " parts");

    // ③ Plan: each zone's parcels, the whole solved; approved (the AI looks at the map).
    ASSERT_EQ(build.PlanFromLayout(), "");
    pump();
    ASSERT_EQ(build.Stage(), "plan") << build.State().value("status", "");
    copy(build.Folder() / "layout.png", "3-plan.png");
    WriteText(out / "3-plan-verify.json", build.State().value("verify", json::array()).dump(2));
    WriteText(out / "3-spec.json", build.Spec().dump(2));
    ASSERT_EQ(build.Approve(), "");
    pump();
    copy(build.Folder() / "layout.png", "3-plan-approved.png");
    say("plan: " + build.Stage());
    }

    // ④ Assets: each part's art decided; what's missing made or searched for, checked; another
    // way for what's still missing (as the editor's FindAssets and AfterSourcing).
    PlaceSceneOptions options;
    options.place = build.Id();
    options.isoGrid = true;
    options.hills = HillsFor(build.Brief());
    auto planAssets = [&](bool canGenerate) {
        TerrainSet terrains = ProjectTerrainSet(prefabs, Project{project});
        const Catalog catalog = BuildCatalog(assets, &ontology, catalogOptions);
        AssetSourcing sourcing;
        sourcing.brief = build.Brief();
        sourcing.settings = ReadSourcingSettings(project);
        sourcing.settings.webSearch = web;
        sourcing.canGenerate = canGenerate;
        sourcing.tried = build.TriedSources();
        return std::make_pair(PlanPlaceAssets(ontology, build.Spec(), build.Solved(), catalog, prefabs, assets, terrains, options, &sourcing),
                              sourcing.settings);
    };
    auto [plan, settings] = planAssets(true);
    WriteText(out / "4-assets-decided.json", Rows(plan).dump(2));
    build.MarkSourced(Rows(plan));
    for (int round = 0; round < 3; ++round) {
        const json pending = plan.value("pending", json::array());
        if (pending.empty()) break;
        say("assets round " + std::to_string(round + 1) + ": " + std::to_string(pending.size()) + " to find or make");
        if (!build.StartSourcing(pending, settings)) break;
        pump();
        if (!build.SourcingDone()) break;
        build.ClearSourcingDone();
        assets.Refresh();
        for (const auto& a : build.TakeNewAssets()) {
            std::map<std::string, std::string> fields;
            const json given = a.value("fields", json::object());
            for (const auto& [k, v] : given.items()) {
                if (v.is_string()) fields[k] = v.get<std::string>();
            }
            SetCatalogFields(assets, a.value("path", ""), fields);
        }
        assets.Refresh();
        std::tie(plan, settings) = planAssets(true);
        build.MarkSourced(Rows(plan));
    }
    WriteText(out / "4-assets.json", Rows(plan).dump(2));
    {
        // The art itself, beside the run.
        std::error_code ec;
        fs::create_directories(out / "4-art", ec);
        for (const auto& entry : fs::directory_iterator(project / "assets", ec)) (void)entry;
        for (auto it = fs::recursive_directory_iterator(project / "assets", ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (it->path().extension() == ".png" && it->path().string().find("placeholders") == std::string::npos) {
                fs::copy_file(it->path(), out / "4-art" / it->path().filename(), fs::copy_options::overwrite_existing, ec);
            }
        }
    }
    int placeholders = 0;
    for (const auto& n : Rows(plan)) placeholders += n.value("source", "") == "placeholder";
    say("assets: " + std::to_string(Rows(plan).size()) + " pieces, " + std::to_string(placeholders) + " placeholders");

    // ⑤ Result: the place into a scene, checked, drawn as the editor draws it.
    std::tie(plan, settings) = planAssets(false);
    Registry registry;
    json report;
    const TerrainSet terrains = ProjectTerrainSet(prefabs, Project{project});
    auto command = BuildPlaceScene(registry, prefabs, build.Solved(), plan, terrains, options, &report);
    const json verify = VerifyPlaceScene(registry, build.Solved(), plan, options);
    WriteText(out / "5-verify.json", verify.dump(2));
    WriteText(out / "5-report.json", report.dump(2));
    const int side = build.Solved()["grid"]["bounds"][2].get<int>();
    WritePng(out / "5-scene.png", RenderScenePreview(registry, Project{project}, options.iso, 0, 0, side, side, 1800));
    // A close look, as a player would see it: a window of the middle.
    const int c = side / 2, r = std::max(8, side / 6);
    WritePng(out / "5-scene-detail.png", RenderScenePreview(registry, Project{project}, options.iso, c - r, c - r, c + r, c + r, 1600));
    for (const auto& check : verify) say(std::string(check.value("ok", true) ? "ok   " : "FAIL ") + check.value("id", "") + " " + check.value("detail", ""));
    say("done");
}

// The Assets step alone, on a run's saved place (AIGE_REPLAY_ASSETS=<project>|<place id>).
TEST(LiveBuildTest, ReplayTheAssetsStep) {
    const char* arg = std::getenv("AIGE_REPLAY_ASSETS");
    if (arg == nullptr) GTEST_SKIP() << "set AIGE_REPLAY_ASSETS=<project>|<place id>";
    const std::string text = arg;
    const fs::path project = text.substr(0, text.find('|'));
    const std::string place = text.substr(text.find('|') + 1);
    auto read = [](const fs::path& p) { std::ifstream in(p); return json::parse(in); };
    const json spec = read(project / "places" / place / "spec.json");
    json solved = read(project / "places" / place / "layout.solved.json");
    // AIGE_REPLAY_RESOLVE=1: re-solved with the compiler's additions (Resolve).
    if (std::getenv("AIGE_REPLAY_RESOLVE") != nullptr) std::cout << "re-solved with " << Resolve(project / "places" / place, solved) << " new statements" << std::endl;
    const json brief = read(project / "places" / place / "brief.json");
    Ontology ontology;
    ontology.Load({EnginePlacesDir()});
    PrefabLibrary prefabs(Project{project});
    AssetDatabase assets(Project{project});
    assets.Refresh();
    CatalogOptions catalogOptions;
    catalogOptions.isoGrid = true;
    PlaceSceneOptions options;
    options.place = place;
    options.isoGrid = true;
    options.hills = HillsFor(brief);
    AssetSourcing sourcing;
    sourcing.brief = brief;
    sourcing.settings = ReadSourcingSettings(project);
    sourcing.canGenerate = false; // the art there is, as the Result step uses it
    const json plan = PlanPlaceAssets(ontology, spec, solved, BuildCatalog(assets, &ontology, catalogOptions), prefabs, assets,
                                      ProjectTerrainSet(prefabs, Project{project}), options, &sourcing);
    std::cout << Rows(plan).size() << " rows" << std::endl;
    for (const auto& entry : fs::directory_iterator(project / "assets/textures/generated")) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("piece-", 0) != 0 || entry.path().extension() != ".png") continue;
        if (auto image = DecodeImage(entry.path())) {
            const ArtShape shape = AnalyzeShape(*image);
            std::cout << name << ": " << PieceKind(shape, false) << " (axis " << shape.axisDegrees << ", elongation " << shape.elongation << ")" << std::endl;
        }
    }
    // And ⑤ again, drawn beside the project (replay-scene.png, replay-detail.png).
    Registry registry;
    json report;
    auto command = BuildPlaceScene(registry, prefabs, solved, plan, ProjectTerrainSet(prefabs, Project{project}), options, &report);
    const int side = solved["grid"]["bounds"][2].get<int>();
    const fs::path out = project.parent_path();
    WritePng(out / "replay-scene.png", RenderScenePreview(registry, Project{project}, options.iso, 0, 0, side, side, 1800));
    const int c = side / 2, r = std::max(8, side / 6);
    WritePng(out / "replay-detail.png", RenderScenePreview(registry, Project{project}, options.iso, c - r, c - r, c + r, c + r, 1600));
    std::cout << report.value("objects", json()).dump() << std::endl;
}

// A project's built places drawn as the editor draws them (AIGE_RENDER_SCENES=<project>|<out dir>):
// each row of the Planner's build plan that has a build, from its saved scene; the whole place and
// a close window of its middle. For judging builds without opening the editor.
TEST(LiveBuildTest, RenderTheBuiltPlaces) {
    const char* arg = std::getenv("AIGE_RENDER_SCENES");
    if (arg == nullptr) GTEST_SKIP() << "set AIGE_RENDER_SCENES=<project>|<out dir>";
    const std::string text = arg;
    const fs::path project = text.substr(0, text.find('|'));
    const fs::path out = text.substr(text.find('|') + 1);
    fs::create_directories(out);
    const json plan = ReadFile(project / "planner" / "buildplan.json");
    for (const auto& row : plan.value("rows", json::array())) {
        const std::string build = row.value("build", ""), scene = row.value("scene", "");
        if (build.empty() || scene.empty() || !fs::exists(project / scene)) continue;
        const fs::path solvedPath = project / "places" / build / "layout.solved.json";
        if (!fs::exists(solvedPath)) continue;
        const int side = ReadFile(solvedPath)["grid"]["bounds"][2].get<int>();
        Registry registry;
        if (!LoadScene(registry, (project / scene).string())) continue;
        IsoParams iso;
        const std::string name = fs::path(scene).stem().string();
        WritePng(out / (name + ".png"), RenderScenePreview(registry, Project{project}, iso, 0, 0, side, side, 1800));
        const int c = side / 2, r = std::max(8, side / 6);
        WritePng(out / (name + "-detail.png"), RenderScenePreview(registry, Project{project}, iso, c - r, c - r, c + r, c + r, 1600));
        std::cout << name << " (" << row.value("place", "") << ", " << side << " cells)" << std::endl;
    }
}

// A place's plan drawn as an isometric block-out (AIGE_BLOCKOUT=<solved json>|<out png>|<width>): what an
// image model repaints in the Painted build method (PlaceBlockout).
TEST(LiveBuildTest, RenderPlanBlockout) {
    const char* arg = std::getenv("AIGE_BLOCKOUT");
    if (arg == nullptr) GTEST_SKIP() << "set AIGE_BLOCKOUT=<solved json>|<out png>|<width>";
    std::vector<std::string> parts;
    {
        std::string part;
        std::istringstream in(arg);
        while (std::getline(in, part, '|')) parts.push_back(part);
    }
    ASSERT_EQ(parts.size(), 3u);
    const json solved = ReadFile(parts[0]);
    const auto [x0, y0, x1, y1] = OccupiedBounds(solved);
    Ontology ontology;
    ontology.Load({EnginePlacesDir()});
    json legend;
    const ImageData image = RenderPlanBlockout(solved, x0, y0, x1, y1, std::stoi(parts[2]), &ontology, &legend);
    ASSERT_TRUE(WritePng(parts[1], image));
    WriteText(fs::path(parts[1]).replace_extension(".legend.json"), legend.dump(2));
    std::cout << image.width << " x " << image.height << std::endl;
}

// One saved scene drawn big (AIGE_RENDER_ONE=<project>|<scene .aige>|<build id>|<out png>|<width>):
// framed on the place itself (its occupied cells), as wide as asked. The input an image model repaints.
TEST(LiveBuildTest, RenderOneScene) {
    const char* arg = std::getenv("AIGE_RENDER_ONE");
    if (arg == nullptr) GTEST_SKIP() << "set AIGE_RENDER_ONE=<project>|<scene>|<build id>|<out png>|<width>";
    std::vector<std::string> parts;
    {
        std::string part;
        std::istringstream in(arg);
        while (std::getline(in, part, '|')) parts.push_back(part);
    }
    ASSERT_EQ(parts.size(), 5u);
    const fs::path project = parts[0];
    Registry registry;
    ASSERT_TRUE(LoadScene(registry, parts[1]));
    const json solved = ReadFile(project / "places" / parts[2] / "layout.solved.json");
    const auto [x0, y0, x1, y1] = OccupiedBounds(solved);
    IsoParams iso;
    const ImageData image = RenderScenePreview(registry, Project{project}, iso, x0, y0, x1, y1, std::stof(parts[4]));
    ASSERT_TRUE(WritePng(parts[3], image));
    std::cout << image.width << " x " << image.height << std::endl;
}

// One place of a project's Planner built as the Planner builds it (docs/build-fixes.md), in a copy of
// the project, with every step checked by the vision model:
//
//   AIGE_LIVE_PLANNER="<project>|<place, e.g. location/ashen-vale>|<out dir>|<service port>"
//
// The copy leaves out what each build makes again (the places' folders, their scenes and painted
// ground), so each run starts from the same project. Its Brief and story are the Planner's; its art
// strategy the Brief's; the library is the AIGE library and the user's. Written into <out dir>:
// 2-layout.png and 2-inspect.json (② the zones), 3-plan.png and 3-inspect.json (③ the plan, coded),
// 4-assets.json (what each part got), 5-scene.png, 5-build-<n>.png and 5-inspect.json (⑤ the scene,
// in windows), scene.aige, and scores.json (every check's scores, to compare runs).
TEST(LiveBuildTest, APlannerPlaceBuild) {
    const char* arg = std::getenv("AIGE_LIVE_PLANNER");
    if (arg == nullptr) GTEST_SKIP() << "set AIGE_LIVE_PLANNER=<project>|<place>|<out dir>|<port>";
    std::vector<std::string> parts;
    {
        std::string part;
        std::istringstream in(arg);
        while (std::getline(in, part, '|')) parts.push_back(part);
    }
    ASSERT_GE(parts.size(), 4u);
    const fs::path source = parts[0];
    const std::string placeId = parts[1];
    const fs::path out = parts[2];
    const int port = std::stoi(parts[3]);
    // "inplace": built in the project itself (nothing copied or removed), the scene saved where the
    // Planner keeps it and taken into its game build (GameBuild::InstallBuilt): for the project's
    // own builds once they're good. Otherwise in a fresh copy under <out dir>.
    const bool inPlace = parts.size() > 4 && parts[4] == "inplace";
    const fs::path project = inPlace ? source : out / "project";
    fs::create_directories(out);
    if (!inPlace) {
        fs::remove_all(project);
        fs::create_directories(project);
    }
    if (!inPlace) {
        // The project without what a build makes again.
        auto skip = [](const fs::path& rel) {
            const std::string r = rel.generic_string();
            return r == "places" || r == "scenes" || r == "assets/generated/ground" || r.rfind("planner/build-backup", 0) == 0 || r == "planner/history";
        };
        for (auto it = fs::recursive_directory_iterator(source); it != fs::recursive_directory_iterator(); ++it) {
            const fs::path rel = fs::relative(it->path(), source);
            if (skip(rel)) {
                it.disable_recursion_pending();
                continue;
            }
            if (it->is_directory()) fs::create_directories(project / rel);
            else fs::copy_file(it->path(), project / rel, fs::copy_options::overwrite_existing);
        }
        fs::create_directories(project / "places");
        fs::create_directories(project / "scenes");
    }
    std::ofstream log(out / "run.log");
    const auto started = std::chrono::steady_clock::now();
    auto say = [&](const std::string& text) {
        const auto s = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count();
        log << "[" << s << "s] " << text << std::endl;
        std::cout << "[" << s << "s] " << text << std::endl;
    };
    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(5);
    client.set_read_timeout(900);
    {
        const auto r = client.Put("/project", json{{"root", fs::absolute(project).string()}}.dump(), "application/json");
        ASSERT_TRUE(r && r->status == 200) << "the AI service isn't answering on port " << port;
        // What draws the art, as the editor sets it (Preferences ▸ AI ▸ Art): PixelLab unless AIGE_ART says.
        const char* art = std::getenv("AIGE_ART");
        const auto a = client.Put("/art-provider", json{{"provider", art ? art : "pixellab"}}.dump(), "application/json");
        ASSERT_TRUE(a && a->status == 200 && json::parse(a->body, nullptr, false).value("ok", false)) << "the art provider couldn't be set: " << (a ? a->body : "");
    }
    // The row of the Planner's build plan: its prompt and Brief. "request:<words>" instead: a place
    // asked for in words (a kind the Planner hasn't made: a courtyard, a castle, a camp), no story.
    const bool fromRequest = placeId.rfind("request:", 0) == 0;
    json row;
    if (!fromRequest) {
        for (const auto& r : ReadFile(project / "planner" / "buildplan.json").value("rows", json::array())) {
            if (r.value("place", "") == placeId) row = r;
        }
        ASSERT_TRUE(row.is_object()) << placeId << " isn't in the build plan";
    } else {
        row = {{"place", placeId}, {"prompt", placeId.substr(8)}, {"brief", json::object()}};
        ASSERT_FALSE(inPlace) << "a request isn't one of the project's places";
    }

    Ontology ontology;
    ontology.Load({EnginePlacesDir()});
    PrefabLibrary prefabs(Project{project});
    AssetDatabase assets(Project{project});
    assets.Refresh();
    CatalogOptions catalogOptions;
    catalogOptions.isoGrid = true;
    std::deque<Call> queue;
    int nextId = 1;
    PlaceBuild::Host host;
    host.ontology = [&]() -> const Ontology& { return ontology; };
    host.catalog = [&] { return BuildCatalog(assets, &ontology, catalogOptions); };
    host.isoGrid = [] { return true; };
    host.request = [&](const std::string& path, const json& body, int timeout) {
        queue.push_back({nextId, path, body, timeout});
        return nextId++;
    };
    host.projectRoot = project;
    host.layoutStep = false; // as the Planner builds
    PlaceBuild build(host);
    struct Flight {
        Call call;
        std::chrono::steady_clock::time_point t0;
        std::future<json> answer;
    };
    auto post = [port](const std::string& path, const json& body) {
        httplib::Client c("127.0.0.1", port);
        c.set_connection_timeout(5);
        c.set_read_timeout(900);
        const auto r = c.Post(path, body.dump(), "application/json");
        json out = r && r->status == 200 ? json::parse(r->body, nullptr, false) : json{{"ok", false}, {"errors", {"no answer"}}};
        return out.is_discarded() ? json{{"ok", false}, {"errors", {"not JSON"}}} : out;
    };
    auto pump = [&] {
        std::vector<Flight> flying;
        while (!queue.empty() || !flying.empty()) {
            while (!queue.empty() && flying.size() < 4) {
                Call call = queue.front();
                queue.pop_front();
                auto answer = std::async(std::launch::async, post, call.path, call.body);
                flying.push_back({std::move(call), std::chrono::steady_clock::now(), std::move(answer)});
            }
            for (size_t i = 0; i < flying.size();) {
                if (flying[i].answer.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready) {
                    ++i;
                    continue;
                }
                Flight done = std::move(flying[i]);
                flying.erase(flying.begin() + static_cast<std::ptrdiff_t>(i));
                const json body = done.answer.get();
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - done.t0).count();
                say(done.call.path + " " + (body.value("ok", false) ? "ok" : "failed") + " (" + std::to_string(ms / 1000) + "s)");
                build.OnResponse(done.call.id, body);
            }
        }
    };
    json scores = {{"place", placeId}, {"buildType", row.value("brief", json::object()).value("buildType", "")}};
    auto inspect = [&](const std::string& step, const json& body, const std::string& file) {
        const json answer = post("/places/inspect", body);
        WriteText(out / file, answer.dump(2));
        scores[step] = {{"scores", answer.value("scores", json::object())}, {"summary", answer.value("summary", "")}};
        int errors = 0, warnings = 0;
        for (const auto& p : answer.value("problems", json::array())) (p.value("severity", "") == "error" ? errors : warnings)++;
        scores[step]["errors"] = errors;
        scores[step]["warnings"] = warnings;
        say(step + " check: " + InspectSummary(answer));
        return answer;
    };
    auto copy = [&](const fs::path& from, const std::string& name) {
        std::error_code ec;
        if (fs::exists(from, ec)) fs::copy_file(from, out / name, fs::copy_options::overwrite_existing, ec);
    };

    // ① to ③, as GameBuild drives a row.
    GameBuild::Host gameHost;
    gameHost.projectRoot = project;
    gameHost.places = [&]() -> const Ontology& { return ontology; };
    const GameBuild game(gameHost);
    if (fromRequest) {
        ASSERT_EQ(build.Start(row.value("prompt", "")), "");
    } else {
        ASSERT_EQ(build.StartFromBrief(row.value("prompt", ""), row.value("brief", json::object())), "");
        build.SetStory(game.StoryFor(placeId));
    }
    pump();
    int layoutRound = 0;
    json layoutRounds = json::array();
    auto noteLayoutCheck = [&] {
        // The build's own check of its layout (PlaceBuild::CheckLayout): kept, each round.
        const json& answer = build.LayoutCheck();
        if (!answer.is_object() || answer.empty()) return;
        WriteText(out / ("2-inspect-" + std::to_string(layoutRound) + ".json"), answer.dump(2));
        int errors = 0, warnings = 0;
        for (const auto& p : answer.value("problems", json::array())) (p.value("severity", "") == "error" ? errors : warnings)++;
        layoutRounds.push_back({{"round", layoutRound}, {"scores", answer.value("scores", json::object())}, {"errors", errors}, {"warnings", warnings},
                                {"summary", answer.value("summary", "")}});
        say("layout check " + std::to_string(layoutRound) + ": " + InspectSummary(answer));
    };
    for (int step = 0; step < 40; ++step) {
        const std::string stage = build.Stage();
        std::string why;
        if (stage == "request") why = build.Step("assets");
        else if (stage == "assets") why = build.Confirm();
        else if (stage == "layout" || stage == "plan") {
            // The zones checked first (PlaceBuild::CheckLayout, its errors revised), round by round;
            // then the plan's own check (measured here) and its approval.
            if (layoutRound > 0 && !build.LayoutChecked()) noteLayoutCheck();
            if (!build.LayoutChecked()) {
                const bool asked = build.CheckLayout().empty();
                copy(build.Folder() / "inspect-layout.png", "2-layout-" + std::to_string(++layoutRound) + ".png");
                if (!asked) why = stage == "layout" ? build.PlanFromLayout() : "";
            } else {
                if (!scores.contains("layoutRounds")) {
                    if (layoutRound > 0) noteLayoutCheck();
                    copy(build.Folder() / "inspect-layout.png", "2-layout.png");
                    scores["layout"] = layoutRounds.empty() ? json::object() : layoutRounds.back();
                    scores["layoutRounds"] = layoutRounds;
                }
                if (stage == "layout") {
                    why = build.PlanFromLayout();
                } else {
                    const json body = InspectPlanRequest(build, &ontology);
                    copy(build.Folder() / "inspect-plan.png", "3-plan.png");
                    WriteText(out / "3-inspect-request.json", body.dump(2));
            inspect("plan", body, "3-inspect.json");
                    why = build.Approve();
                }
            }
        } else if (stage == "planned") {
            break;
        } else {
            say("waiting at " + stage + ": " + build.State().value("status", ""));
        }
        ASSERT_EQ(why, "") << stage;
        pump();
    }
    ASSERT_EQ(build.Stage(), "planned") << build.State().value("status", "");
    WriteText(out / "1-brief.json", build.Brief().dump(2));
    WriteText(out / "3-spec.json", build.Spec().dump(2));
    say("planned");

    // The Painted method (AIGE_METHOD=painted), as the app runs it: the plan's block-out painted by the Paint
    // Provider (each model of AIGE_PAINT_MODELS in turn, as Preferences ▸ Paint Provider sets it), each painting
    // checked: against the layout and the plan (match), building by building, and for its looks.
    if (const char* method = std::getenv("AIGE_METHOD"); method && std::string(method) == "painted") {
        std::vector<std::string> models;
        {
            const char* list = std::getenv("AIGE_PAINT_MODELS");
            std::string part;
            std::istringstream in(list ? list : "flux-3-image");
            while (std::getline(in, part, ',')) models.push_back(part);
        }
        json table = json::array();
        for (const auto& model : models) {
            const auto r = client.Put("/paint-provider", json{{"provider", "bfl"}, {"model", model}}.dump(), "application/json");
            if (!r || r->status != 200 || !json::parse(r->body, nullptr, false).value("ok", false)) {
                say("paint provider " + model + " refused: " + (r ? r->body : "no answer"));
                continue;
            }
            const json requests = BlockoutPaintRequests(build, &ontology);
            for (const auto& q : requests) {
                copy(project / q.value("blockout", ""), fs::path(q.value("blockout", "")).filename().string());
                copy(project / q.value("legend", ""), fs::path(q.value("legend", "")).filename().string());
            }
            if (build.Paint(requests).empty()) pump();
            for (const auto& b : build.Backdrops()) {
                const std::string painting = b.value("backdrop", ""), level = b.value("level", "");
                const std::string name = model + (level.empty() ? "" : "-" + level);
                copy(project / painting, name + ".png");
                WriteText(out / (name + ".steps.json"), b.value("steps", json::array()).dump(2));
                json body = InspectPlanRequest(build, &ontology);
                body["kind"] = "match";
                body.erase("windows");
                body["images"] = json::array({"places/" + build.Id() + "/inspect-layout.png", "places/" + build.Id() + "/inspect-plan.png", painting});
                const json match = inspect("paint-" + name, body, name + ".match.json");
                const json legend = ReadFile(project / b.value("legend", ""));
                const json check = post("/places/check-painting", {{"painting", painting}, {"legend", legend}});
                WriteText(out / (name + ".check.json"), check.dump(2));
                table.push_back({{"model", name}, {"match", match.value("scores", json::object())}, {"buildings", check.value("buildings", json::array())},
                                 {"wrong", check.value("wrong", json::array())}, {"appeal", check.value("appeal", json::object())}});
                say("painted by " + name + ": " + InspectSummary(match) + "; buildings " + check.value("buildings", json::array()).dump());
            }
        }
        // The comparison: each model's scores in one table.
        std::ostringstream md;
        md << "# " << build.Brief().value("name", placeId) << ": the Paint Provider models\n\n"
           << "Planned by the Planner from the request (`1-brief.json`, `3-spec.json`), the layout and plan checked "
              "(`2-layout.png`, `3-plan.png`); the plan's block-out (`blockout.png`, its colour key from the ontology in "
              "`blockout.legend.json`) painted by each Paint Provider model, as the app does it. Layout, placement, fit and "
              "quality: the match check against the layout and the plan. Buildings right: each building of the plan cut from "
              "the painting and named. Appeal: how the painting looks. All 0-10.\n\n"
           << "| Model | Layout | Placement | Fit | Quality | Buildings right | Appeal | Modern | Polish | Readability | Average |\n"
           << "|---|---|---|---|---|---|---|---|---|---|---|\n";
        for (const auto& row : table) {
            const json m = row["match"], a = row["appeal"], b = row["buildings"];
            std::vector<double> values;
            auto cell = [&](const json& j, const char* k) {
                if (!j.contains(k) || !j[k].is_number()) return std::string("-");
                values.push_back(j[k].get<double>());
                std::ostringstream v;
                v << j[k].get<double>();
                return v.str();
            };
            std::string right = "-";
            if (b.is_array() && b.size() == 2 && b[1].get<int>() > 0) {
                right = std::to_string(b[0].get<int>()) + "/" + std::to_string(b[1].get<int>());
                values.push_back(10.0 * b[0].get<int>() / b[1].get<int>());
            }
            md << "| [" << row["model"].get<std::string>() << "](" << row["model"].get<std::string>() << ".png) | " << cell(m, "layout") << " | "
               << cell(m, "placement") << " | " << cell(m, "fit") << " | " << cell(m, "quality") << " | " << right << " | " << cell(a, "appeal")
               << " | " << cell(a, "modern") << " | " << cell(a, "polish") << " | " << cell(a, "readability") << " | ";
            double sum = 0;
            for (double v : values) sum += v;
            md << (values.empty() ? std::string("-") : std::to_string(sum / values.size()).substr(0, 3)) << " |\n";
        }
        md << "\n## Buildings drawn as the wrong kind (plan's kind -> what the painting shows)\n\n";
        for (const auto& row : table) {
            std::string wrong;
            for (const auto& w : row["wrong"]) wrong += (wrong.empty() ? "" : "; ") + w.get<std::string>();
            md << "- **" << row["model"].get<std::string>() << ":** " << (wrong.empty() ? "none" : wrong) << "\n";
        }
        WriteText(out / "comparison.md", md.str());
        scores["painted"] = table;
        WriteText(out / "scores.json", scores.dump(2));
        say("done");
        return;
    }

    // ④ Assets, as the editor finds them (FindAssets, AfterSourcing).
    PlaceSceneOptions options;
    options.place = build.Id();
    options.isoGrid = true;
    options.hills = HillsFor(build.Brief());
    SetMoodGrade(options, build.Brief(), build.Story());
    const char* local = std::getenv("LOCALAPPDATA");
    const fs::path userLibrary = local ? fs::path(local) / "AIGE" / "library" : fs::path();
    auto planAssets = [&](bool canGenerate) {
        TerrainSet terrains = ProjectTerrainSet(prefabs, Project{project});
        const Catalog catalog = BuildCatalog(assets, &ontology, catalogOptions);
        AssetSourcing sourcing;
        sourcing.brief = build.Brief();
        sourcing.settings = ReadSourcingSettings(project);
        sourcing.canGenerate = canGenerate;
        sourcing.strategyRules = true;
        sourcing.checkFound = true;
        for (const auto& [key, crop] : build.SketchCrops()) sourcing.sketched.insert(key);
        sourcing.tried = build.TriedSources();
        sourcing.library = LoadAssetLibrary(project);
        if (!userLibrary.empty()) {
            const UserLibrary library(userLibrary);
            for (auto& e : library.AsCatalog().entries) sourcing.library.entries.push_back(std::move(e));
            sourcing.importLibrary = [userLibrary, project](const std::string& item) { return UserLibrary(userLibrary).CopyToProject(item, project); };
        }
        return std::make_pair(PlanPlaceAssets(ontology, build.Spec(), build.Solved(), catalog, prefabs, assets, terrains, options, &sourcing),
                              sourcing.settings);
    };
    auto [plan, settings] = planAssets(true);
    // The place sketched first (docs/build-fixes.md): one painting (one per level) from the plan's words,
    // each piece found in it and cropped, the crop the reference its art is drawn from.
    if (std::getenv("AIGE_SKETCH") != nullptr) {
        const json requests = SketchRequests(build, plan, &ontology);
        WriteText(out / "4-sketch-requests.json", requests.dump(2));
        if (build.Sketch(requests).empty()) pump();
        fs::create_directories(out / "4-sketch");
        for (const auto& s : build.Sketches()) {
            if (!s.value("sketch", "").empty()) copy(project / s.value("sketch", ""), "4-sketch/" + fs::path(s.value("sketch", "")).filename().string());
        }
        for (const auto& [key, crop] : build.SketchCrops()) copy(project / crop, "4-sketch/" + fs::path(crop).filename().string());
        WriteText(out / "4-sketches.json", build.Sketches().dump(2));
        scores["sketch"] = {{"paintings", requests.size()}, {"crops", build.SketchCrops().size()}};
        say("sketched: " + std::to_string(requests.size()) + " painting(s), " + std::to_string(build.SketchCrops().size()) + " pieces found and cropped");
        std::tie(plan, settings) = planAssets(true); // the sketched pieces: made from their crops
    }
    auto sourceArt = [&] {
        // Rounds of finding, checking and making, as the editor's AfterSourcing runs them.
        build.MarkSourced(AssetNeedRows(plan));
        for (int round = 0; round < 6; ++round) {
            const json pending = plan.value("pending", json::array());
            if (pending.empty()) break;
            int checks = 0;
            for (const auto& p : pending) checks += p.value("action", "") == "check";
            say("assets round " + std::to_string(round + 1) + ": " + std::to_string(pending.size()) + " to find or make (" + std::to_string(checks) +
                " found ones to check)");
            if (!build.StartSourcing(pending, settings)) break;
            pump();
            if (!build.SourcingDone()) break;
            build.ClearSourcingDone();
            assets.Refresh();
            for (const auto& a : build.TakeNewAssets()) {
                std::map<std::string, std::string> fields;
                const json given = a.value("fields", json::object());
                for (const auto& [k, v] : given.items()) {
                    if (v.is_string()) fields[k] = v.get<std::string>();
                }
                SetCatalogFields(assets, a.value("path", ""), fields);
            }
            assets.Refresh();
            std::tie(plan, settings) = planAssets(true);
            build.MarkSourced(AssetNeedRows(plan));
        }
    };
    sourceArt();
    WriteText(out / "4-assets.json", AssetNeedRows(plan).dump(2));
    int placeholders = 0, refused = 0;
    for (const auto& n : AssetNeedRows(plan)) placeholders += n.value("source", "") == "placeholder";
    for (const auto& [key, t] : build.TriedSources()) refused += static_cast<int>(t.refused.size());
    scores["assets"] = {{"pieces", AssetNeedRows(plan).size()}, {"placeholders", placeholders}, {"refusedFound", refused}};
    say("assets: " + std::to_string(AssetNeedRows(plan).size()) + " pieces, " + std::to_string(placeholders) + " placeholders, " +
        std::to_string(refused) + " found pieces refused by the check");

    // ⑤ Result, as the editor builds it (StepBuildRun): the scene, its checks and repairs; then the
    // build's own vision check of it, in windows (PlaceBuild::InspectBuilt). Art it refuses is found
    // or made again and the place built again, once (6-*).
    const int side = build.Solved()["grid"]["bounds"][2].get<int>();
    auto buildAndCheck = [&](const std::string& tag) {
        std::tie(plan, settings) = planAssets(false);
        Registry registry;
        json report;
        const TerrainSet terrains = ProjectTerrainSet(prefabs, Project{project});
        build.StartBuilding();
        auto command = BuildPlaceScene(registry, prefabs, build.Solved(), plan, terrains, options, &report);
        json verify = VerifyPlaceScene(registry, build.Solved(), plan, options);
        bool failing = false;
        for (const auto& c : verify) failing = failing || (!c.value("ok", true) && !c.value("warning", false));
        if (failing) {
            RepairPlaceScene(registry, prefabs, build.Solved(), plan, verify, options);
            verify = VerifyPlaceScene(registry, build.Solved(), plan, options);
        }
        WriteText(out / (tag + "-verify.json"), verify.dump(2));
        WriteText(out / (tag + "-report.json"), report.dump(2));
        const auto [bx0, by0, bx1, by1] = OccupiedBounds(build.Solved()); // the place, not the empty site round it
        WritePng(out / (tag + "-scene.png"), RenderScenePreview(registry, Project{project}, options.iso, bx0, by0, bx1, by1, 1800));
        // Each level as its own scene shows it.
        for (const auto& l : PlaceLevelAreas(ontology, build.Spec(), build.Solved())) {
            WritePng(out / (tag + "-scene-" + l.id + ".png"), RenderScenePreview(registry, Project{project}, options.iso, l.x0 - 1, l.y0 - 1, l.x1 + 1, l.y1 + 1, 1600));
        }
        const int c = side / 2, r = std::max(8, side / 6);
        WritePng(out / (tag + "-scene-detail.png"), RenderScenePreview(registry, Project{project}, options.iso, c - r, c - r, c + r, c + r, 1600));
        SaveScene(registry, (out / "scene.aige").string());
        build.MarkSourced(AssetNeedRows(plan));
        build.Built({{"assets", plan}, {"report", report}, {"verify", verify}, {"after", verify}});
        const json body = InspectBuildRequest(registry, Project{project}, options.iso, build, 0, 0, 56, &ontology);
        for (size_t i = 0; i < body["images"].size(); ++i) copy(project / body["images"][i].get<std::string>(), tag + "-build-" + std::to_string(i + 1) + ".png");
        WriteText(out / (tag + "-inspect-request.json"), body.dump(2)); // what the layout (PLL) placed, window by window
        if (build.InspectBuilt(body).empty()) pump();
        const json& answer = build.BuildCheck();
        WriteText(out / (tag + "-inspect.json"), answer.dump(2));
        int errors = 0, warnings = 0, failed = 0;
        for (const auto& p : answer.value("problems", json::array())) (p.value("severity", "") == "error" ? errors : warnings)++;
        for (const auto& check : verify) failed += !check.value("ok", true) && !check.value("warning", false);
        scores[tag == "5" ? "build" : tag == "6" ? "rebuild" : "reverted"] = {{"scores", answer.value("scores", json::object())}, {"summary", answer.value("summary", "")},
                                                     {"errors", errors}, {"warnings", warnings}, {"verifyFailed", failed}, {"redo", build.ArtToRedo()}};
        say("build check (" + tag + "): " + InspectSummary(answer) + "; art to redo: " + std::to_string(build.ArtToRedo().size()));
    };
    buildAndCheck("5");
    // A redo (6), and if it made the place worse, the art from before put back (7).
    for (int tag = 6; tag <= 7 && !build.ArtToRedo().empty(); ++tag) {
        build.ArtRedone();
        std::tie(plan, settings) = planAssets(true);
        sourceArt();
        buildAndCheck(std::to_string(tag));
    }
    scores["buildType"] = build.Brief().value("buildType", scores.value("buildType", ""));
    WriteText(out / "scores.json", scores.dump(2));
    if (inPlace) {
        // The place into the project's game build, as the Build panel's are.
        const fs::path scene = project / row.value("scene", "");
        fs::create_directories(scene.parent_path());
        fs::copy_file(out / "scene.aige", scene, fs::copy_options::overwrite_existing);
        GameBuild::Host installHost;
        installHost.projectRoot = project;
        installHost.places = [&]() -> const Ontology& { return ontology; };
        // One install at a time (builds of the project's places run side by side; each install reads,
        // changes and writes its build state): a lock folder, made by one process only.
        const fs::path lock = project / "planner" / ".install-lock";
        for (int tries = 0; !fs::create_directory(lock) && tries < 3000; ++tries) std::this_thread::sleep_for(std::chrono::milliseconds(200));
        GameBuild installer(installHost);
        const std::string why = installer.InstallBuilt(placeId, build.Id());
        fs::remove(lock);
        say(why.empty() ? "installed as " + row.value("scene", "") : "couldn't install it: " + why);
        EXPECT_EQ(why, "");
    }
    say("done");
}

// The Painted build method (docs/build-fixes.md) from an earlier run's layout and plan: its place
// build opened as it was planned, painted whole (one backdrop per level, PlacePaint), the scene built
// from the painting by code, and the three checked together by the vision model (/places/inspect
// "match": do the layout, the plan and the final build agree, and does the build tell the story).
// AIGE_PAINT="<earlier run dir>|<out dir>|<port>". Written: 2-layout.png, 3-plan.png, backdrop*.png
// (the painting) and backdrop*.json (its map), 5-scene.png (the scene built from it), match.json,
// scores.json.
TEST(LiveBuildTest, PaintAnEarlierBuild) {
    const char* arg = std::getenv("AIGE_PAINT");
    if (arg == nullptr) GTEST_SKIP() << "set AIGE_PAINT=<earlier run dir>|<out dir>|<port>";
    std::vector<std::string> parts;
    {
        std::string part;
        std::istringstream in(arg);
        while (std::getline(in, part, '|')) parts.push_back(part);
    }
    ASSERT_EQ(parts.size(), 3u);
    const fs::path earlier = parts[0], out = parts[1];
    const int port = std::stoi(parts[2]);
    const fs::path project = out / "project";
    fs::remove_all(project);
    fs::create_directories(project);
    for (auto it = fs::recursive_directory_iterator(earlier / "project"); it != fs::recursive_directory_iterator(); ++it) {
        const fs::path rel = fs::relative(it->path(), earlier / "project");
        if (rel.generic_string() == ".cache") {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_directory()) fs::create_directories(project / rel);
        else fs::copy_file(it->path(), project / rel, fs::copy_options::overwrite_existing);
    }
    std::string buildId;
    for (const auto& e : fs::directory_iterator(project / "places")) {
        if (e.is_directory()) buildId = e.path().filename().string();
    }
    ASSERT_FALSE(buildId.empty()) << "no place build in " << earlier;
    const std::string placeId = ReadFile(earlier / "scores.json").value("place", "");
    std::ofstream log(out / "run.log");
    const auto started = std::chrono::steady_clock::now();
    auto say = [&](const std::string& text) {
        const auto t = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count();
        log << "[" << t << "s] " << text << std::endl;
        std::cout << "[" << t << "s] " << text << std::endl;
    };
    auto post = [port](const std::string& path, const json& body) {
        httplib::Client c("127.0.0.1", port);
        c.set_connection_timeout(5);
        c.set_read_timeout(900);
        const auto r = c.Post(path, body.dump(), "application/json");
        json o = r && r->status == 200 ? json::parse(r->body, nullptr, false) : json{{"ok", false}, {"errors", {"no answer"}}};
        return o.is_discarded() ? json{{"ok", false}, {"errors", {"not JSON"}}} : o;
    };
    {
        httplib::Client client("127.0.0.1", port);
        client.set_connection_timeout(5);
        const auto r = client.Put("/project", json{{"root", fs::absolute(project).string()}}.dump(), "application/json");
        ASSERT_TRUE(r && r->status == 200) << "the AI service isn't answering on port " << port;
    }
    Ontology ontology;
    ontology.Load({EnginePlacesDir()});
    std::deque<Call> queue;
    int nextId = 1;
    PlaceBuild::Host host;
    host.ontology = [&]() -> const Ontology& { return ontology; };
    host.isoGrid = [] { return true; };
    host.request = [&](const std::string& path, const json& body, int timeout) {
        queue.push_back({nextId, path, body, timeout});
        return nextId++;
    };
    host.projectRoot = project;
    PlaceBuild build(host);
    ASSERT_EQ(build.Open(buildId), "");
    ASSERT_TRUE(build.Solved().is_object() && build.Solved().contains("objects")) << "the earlier build has no plan";
    say("opened " + buildId + " (" + placeId + ")");
    auto copy = [&](const fs::path& from, const std::string& name) {
        std::error_code ec;
        if (fs::exists(from, ec)) fs::copy_file(from, out / name, fs::copy_options::overwrite_existing, ec);
    };
    // The layout and the plan, drawn again from the earlier build (as its checks draw them).
    const json layoutBody = InspectLayoutRequest(build, &ontology);
    copy(build.Folder() / "inspect-layout.png", "2-layout.png");
    const json planBody = InspectPlanRequest(build, &ontology);
    copy(build.Folder() / "inspect-plan.png", "3-plan.png");

    // Painted: the requests (the place, or each level), each painted and read.
    GameBuild::Host gameHost;
    gameHost.projectRoot = project;
    gameHost.places = [&]() -> const Ontology& { return ontology; };
    const GameBuild game(gameHost);
    const json anchors = placeId.empty() ? json::array() : game.StoryAnchors(placeId);
    const json requests = BackdropRequests(build, &ontology, anchors);
    WriteText(out / "4-backdrop-requests.json", requests.dump(2));
    ASSERT_EQ(build.Paint(requests), "");
    while (!queue.empty()) {
        std::vector<std::pair<int, std::future<json>>> flying;
        for (; !queue.empty(); queue.pop_front()) flying.push_back({queue.front().id, std::async(std::launch::async, post, queue.front().path, queue.front().body)});
        for (auto& [id, answer] : flying) {
            const json body = answer.get();
            say(std::string("painted: ") + (body.value("ok", false) ? "ok" : "failed " + body.dump().substr(0, 300)));
            build.OnResponse(id, body);
        }
    }
    ASSERT_TRUE(build.Painted());
    ASSERT_FALSE(build.Backdrops().empty()) << "nothing was painted";
    for (const auto& b : build.Backdrops()) {
        const std::string file = b.value("backdrop", "");
        copy(project / file, fs::path(file).filename().string());
        copy(fs::path(project / file).replace_extension(".json"), fs::path(file).stem().string() + ".json");
    }
    // The scene built from the paintings by code.
    PlaceSceneOptions options;
    options.place = build.Id();
    options.isoGrid = true;
    Registry registry;
    const json report = BuildBackdropScene(registry, build, build.Backdrops(), options);
    WriteText(out / "5-report.json", report.dump(2));
    say("scene: " + report.dump());
    const auto [bx0, by0, bx1, by1] = OccupiedBounds(build.Solved());
    WritePng(out / "5-scene.png", RenderScenePreview(registry, Project{project}, options.iso, bx0, by0, bx1, by1, 1800));
    SaveScene(registry, (out / "scene.aige").string());

    // Do the layout, the plan and the final build agree; does it tell the story?
    json scores = {{"place", placeId}, {"build", buildId}, {"method", "Painted"}, {"scene", report}};
    for (const auto& b : build.Backdrops()) {
        json body = planBody;
        body["kind"] = "match";
        body.erase("windows");
        body["images"] = json::array({layoutBody["images"][0], planBody["images"][0], b.value("backdrop", "")});
        if (body.contains("expected") && layoutBody.contains("expected")) body["expected"]["zones"] = layoutBody["expected"].value("zones", json::array());
        const std::string level = b.value("level", "");
        const json answer = post("/places/inspect", body);
        WriteText(out / (level.empty() ? "match.json" : "match-" + level + ".json"), answer.dump(2));
        scores[level.empty() ? "match" : "match-" + level] = {{"scores", answer.value("scores", json::object())}, {"summary", answer.value("summary", "")},
                                                               {"missing", answer.value("missing", json::array())}};
        say("match" + (level.empty() ? "" : " (" + level + ")") + ": " + InspectSummary(answer));
    }
    WriteText(out / "scores.json", scores.dump(2));
    say("done");
}

// How each prompt of a list is read (docs/places/ontology-eval.md): by the engine's own words, and
// for the starred ones once by the AI. AIGE_BRIEF_EVAL="<prompts>|<out json>|<port, 0: no AI>".
TEST(LiveBuildTest, ReadThePrompts) {
    const char* arg = std::getenv("AIGE_BRIEF_EVAL");
    if (arg == nullptr) GTEST_SKIP() << "set AIGE_BRIEF_EVAL=<prompts>|<out json>|<port>";
    std::vector<std::string> parts;
    {
        std::string part;
        std::istringstream in(arg);
        while (std::getline(in, part, '|')) parts.push_back(part);
    }
    ASSERT_EQ(parts.size(), 3u);
    const int port = std::stoi(parts[2]);
    Ontology ontology;
    ontology.Load({EnginePlacesDir()});
    struct Row {
        std::string what, prompt;
        bool ai = false;
        json out;
    };
    std::vector<Row> rows;
    std::ifstream in(parts[0]);
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line[0] == '#' || line.find('|') == std::string::npos) continue;
        Row r;
        r.ai = line[0] == '*';
        r.what = line.substr(r.ai ? 1 : 0, line.find('|') - (r.ai ? 1 : 0));
        r.prompt = line.substr(line.find('|') + 1);
        if (!r.prompt.empty() && r.prompt.back() == '\r') r.prompt.pop_back();
        rows.push_back(r);
    }
    auto summary = [&](const json& brief) {
        json enabled = json::array();
        const json components = brief.value("components", json::object());
        for (const auto& [id, c] : components.items()) {
            if (c.is_object() && c.value("enabled", true)) enabled.push_back(id);
        }
        return json{{"buildType", brief.value("buildType", "")}, {"class", BriefValue(brief, "scale.class")}, {"features", brief.value("features", json::array())},
                    {"components", enabled}};
    };
    std::vector<std::future<json>> answers;
    for (auto& r : rows) {
        r.out = {{"what", r.what}, {"prompt", r.prompt}, {"isBuild", LooksLikeBuildRequest(ontology, r.prompt)},
                 {"named", NamedBuildType(ontology, r.prompt)}, {"engine", summary(CompleteBrief(ontology, FallbackDraft(ontology, r.prompt).value("brief", json::object())))}};
        if (!r.ai || port <= 0) continue;
        const json body = {{"query", r.prompt}, {"context", InterpretContext(ontology, NamedBuildType(ontology, r.prompt))},
                           {"drafts", 1}, {"reading", "typical"}, {"choices", json::object()}};
        answers.push_back(std::async(std::launch::async, [port, body] {
            httplib::Client c("127.0.0.1", port);
            c.set_read_timeout(600);
            const auto res = c.Post("/places/interpret", body.dump(), "application/json");
            return res && res->status == 200 ? json::parse(res->body, nullptr, false) : json{{"ok", false}};
        }));
    }
    size_t k = 0;
    for (auto& r : rows) {
        if (!r.ai || port <= 0) continue;
        const json a = answers[k++].get();
        const json drafts = a.is_object() ? a.value("drafts", json::array()) : json::array();
        if (!drafts.empty() && drafts[0].contains("brief")) {
            r.out["ai"] = summary(drafts[0]["brief"]);
            r.out["ai"]["rationale"] = drafts[0].value("rationale", "");
        } else {
            r.out["ai"] = {{"error", a.is_object() ? a.value("errors", json::array()) : json::array()}};
        }
    }
    json all = json::array();
    for (const auto& r : rows) all.push_back(r.out);
    std::ofstream(parts[1]) << all.dump(2);
    std::cout << all.size() << " prompts read" << std::endl;
}
