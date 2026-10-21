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
#include "assets/ProjectTemplate.h"
#include "assets/Sourcing.h"
#include "assets/TextureCook.h"
#include "editing/Autotile.h"
#include "editing/EditHistory.h"
#include "editing/PlaceBuild.h"
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

// How each prompt of a list is read (docs/ontology-eval.md): by the engine's own words, and
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
        for (const auto& [id, c] : brief.value("components", json::object()).items()) {
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
