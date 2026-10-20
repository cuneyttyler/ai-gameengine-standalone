#include "ai_bridge/AiClient.h"
#include "assets/Catalog.h"
#include "editing/PlaceBuild.h"
#include "gameplay/EngineReflection.h"
#include "places/Ontology.h"
#include "places/Planner.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <thread>

// A live run against the real AI service (opt-in, costs model calls):
//   AIGE_LIVE_PROVIDER=mistral|claude  [AIGE_LIVE_MODEL=...]  [AIGE_LIVE_REPORT=<file.json>]
// Plans a village from a request, then revises it as a user did ("put the
// residential ward below the docks near the river"), and reports time,
// tokens, Verify and whether the revision did what it was asked to.
using namespace aige;
using json = nlohmann::json;
namespace fs = std::filesystem;

#ifndef AIGE_SOURCE_DIR
#define AIGE_SOURCE_DIR "."
#endif

namespace {

const Ontology& Library() {
    static Ontology o = [] {
        Ontology ontology;
        ontology.Load({EnginePlacesDir()});
        return ontology;
    }();
    return o;
}

using Clock = std::chrono::steady_clock;

std::map<std::string, std::pair<int, int>> Seeds(const json& solved) {
    std::map<std::string, std::pair<int, int>> out;
    for (const auto& r : solved.value("regions", json::array())) {
        if (r.value("statement", "") == "site/zones" && r.contains("seed")) out[r["id"]] = {r["seed"][0].get<int>(), r["seed"][1].get<int>()};
    }
    return out;
}

double ToRiver(const json& solved, std::pair<int, int> at) {
    double best = 1e9;
    for (const auto& p : solved.value("paths", json::array())) {
        if (p.value("class", "") != "river") continue;
        for (const auto& c : p["cells"]) best = std::min(best, std::hypot(c[0].get<int>() - at.first, c[1].get<int>() - at.second));
    }
    return best;
}

} // namespace

TEST(LiveTest, PlanAndReviseAVillage) {
    const char* provider = std::getenv("AIGE_LIVE_PROVIDER");
    if (provider == nullptr) GTEST_SKIP() << "set AIGE_LIVE_PROVIDER (mistral or claude): calls the real models";
    const char* modelEnv = std::getenv("AIGE_LIVE_MODEL");
    const fs::path root = fs::temp_directory_path() / ("aige_live_" + std::string(provider));
    fs::remove_all(root);
    fs::create_directories(root / "places");

    AiClient::Options options;
    options.serviceDir = fs::path(AIGE_SOURCE_DIR) / "ai_service";
    options.port = std::string(provider) == "claude" ? 8791 : 8790; // not the editor's, one per model (runs side by side)
    AiClient client(options);
    client.Start(EngineReflection().ExportSchema());
    std::map<int, json> responses;
    auto wait = [&](int id, int seconds) -> json {
        const auto until = Clock::now() + std::chrono::seconds(seconds);
        while (Clock::now() < until) {
            while (auto r = client.Poll()) {
                if (r->kind == AiClient::Result::Kind::Response) responses[r->requestId] = r->body;
            }
            if (auto it = responses.find(id); it != responses.end()) return it->second;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return json{{"ok", false}, {"errors", {"timed out"}}};
    };
    for (int i = 0; i < 1200 && client.GetState() != AiClient::State::Ready && client.GetState() != AiClient::State::Failed; ++i) {
        client.Poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ASSERT_EQ(client.GetState(), AiClient::State::Ready) << client.StatusText();
    const json chosen = wait(client.Request("PUT", "/provider", {{"provider", provider}, {"model", modelEnv ? modelEnv : ""}}), 30);
    ASSERT_TRUE(chosen.value("ok", false)) << chosen.dump();
    auto usage = [&] { return client.GetJson("/health", 5).value_or(json::object()).value("usage", json::object()); };

    PlaceBuild::Host host;
    host.ontology = []() -> const Ontology& { return Library(); };
    host.catalog = [] { return Catalog{}; };
    host.isoGrid = [] { return true; };
    host.request = [&](const std::string& path, const json& body, int timeout) { return client.Request("POST", path, body, timeout); };
    host.projectRoot = root;
    PlaceBuild build(host);
    // Runs the build until it waits on nobody (every AI answer delivered).
    auto drive = [&](int seconds) {
        const auto until = Clock::now() + std::chrono::seconds(seconds);
        while (Clock::now() < until) {
            bool any = false;
            while (auto r = client.Poll()) {
                if (r->kind != AiClient::Result::Kind::Response) continue;
                if (build.Owns(r->requestId)) {
                    build.OnResponse(r->requestId, r->body.is_object() ? r->body : json{{"ok", false}});
                    any = true;
                }
            }
            const std::string stage = build.Stage();
            if (!any && stage != "interpreting" && stage != "planning" && stage != "revising") return;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    };
    json report = {{"provider", chosen["provider"]}, {"model", chosen["model"]}};
    auto phase = [&](const std::string& name, const std::function<void()>& run) {
        const json before = usage();
        const auto t0 = Clock::now();
        run();
        const double seconds = std::chrono::duration<double>(Clock::now() - t0).count();
        const json after = usage();
        report[name]["seconds"] = std::round(seconds * 10) / 10;
        report[name]["inputTokens"] = after.value("inputTokens", 0) - before.value("inputTokens", 0);
        report[name]["outputTokens"] = after.value("outputTokens", 0) - before.value("outputTokens", 0);
        report[name]["requests"] = after.value("requests", 0) - before.value("requests", 0);
    };
    auto verify = [&] {
        json rows = json::array();
        int failing = 0;
        json spec = build.Spec();
        for (const auto& row : VerifyLayout(Library(), build.Brief(), spec, build.Solved())) {
            rows.push_back({{"id", row["id"]}, {"status", row["status"]}, {"planned", row.value("planned", json())}, {"built", row.value("built", json())}});
            failing += row["status"] != "ok";
        }
        return json{{"failing", failing}, {"rows", rows}};
    };

    // 1. Interpret the request (the Test1 village).
    phase("interpret", [&] {
        const char* place = std::getenv("AIGE_LIVE_PLACE");
        ASSERT_EQ(build.Start(place ? place : "Build a village by a river with docks and several districts"), "");
        drive(900);
    });
    ASSERT_EQ(build.Stage(), "request") << build.State().dump().substr(0, 2000);
    report["notices"] = build.Notices();
    ASSERT_EQ(build.Step("assets"), "") << build.State()["window"]["problems"].dump();
    // 2. Plan it (laid out and fitted at once).
    phase("plan", [&] {
        ASSERT_EQ(build.Confirm(), "");
        drive(1800);
    });
    ASSERT_EQ(build.Stage(), "plan") << build.State()["status"];
    json spec = build.Spec();
    report["plan"]["verify"] = verify();
    report["plan"]["zones"] = json::array();
    for (const auto& c : spec["root"]["children"]) report["plan"]["zones"].push_back(c.value("id", ""));
    report["plan"]["warnings"] = build.PlanProblems().size();
    report["notices"] = build.Notices();
    const json before = build.Solved();
    const auto seeds = Seeds(before);
    // The zones by what they are (each model names them its own way).
    auto zoneOf = [&](const std::string& module) {
        for (const auto& c : spec["root"]["children"]) {
            if (c.value("module", "") == module) return c.value("id", "");
        }
        return std::string();
    };
    const std::string wardId = zoneOf("residential-ward"), docksId = zoneOf("docks");
    fs::copy_file(build.Folder() / "layout.png", root / "planned.png", fs::copy_options::overwrite_existing);

    // 3. The user's revision.
    const char* reviseEnv = std::getenv("AIGE_LIVE_REVISE");
    const std::string request = reviseEnv ? reviseEnv : "Put the residential ward below the docks near the river";
    if (wardId.empty() || !seeds.contains(wardId) || (!reviseEnv && (docksId.empty() || !seeds.contains(docksId)))) {
        report["revise"] = {{"skipped", "the plan has no residential ward or no docks"}};
    } else {
        phase("revise", [&] {
            ASSERT_EQ(build.Revise(request), "");
            drive(1200);
        });
        const json after = build.Solved();
        const auto moved = Seeds(after);
        int kept = 0, others = 0;
        for (const auto& [id, seed] : seeds) {
            if (id == wardId) continue;
            ++others;
            kept += moved.contains(id) && moved.at(id) == seed;
        }
        const auto ward = moved.at(wardId);
        const auto docks = moved.contains(docksId) ? moved.at(docksId) : ward;
        const json& revision = build.Revisions().back();
        report["revise"]["request"] = request;
        report["revise"]["summary"] = revision.value("summary", "");
        report["revise"]["notes"] = revision.value("notes", json::array());
        report["revise"]["now"] = revision.value("now", "");
        report["revise"]["checks"] = revision.value("checks", json::array());
        report["revise"]["southOfDocks"] = ward.second > docks.second;
        report["revise"]["riverDistance"] = {{"before", ToRiver(before, seeds.at(wardId))}, {"after", ToRiver(after, ward)}};
        report["revise"]["othersKept"] = std::to_string(kept) + " of " + std::to_string(others);
        report["revise"]["verify"] = verify();
        // What it asked to change, from the log.
        std::ifstream log(build.Folder() / "log.jsonl");
        std::string line, lastAnswer;
        while (std::getline(log, line)) {
            if (line.find("\"step\":\"revise\"") != std::string::npos && line.find("\"answer\"") != std::string::npos) lastAnswer = line;
        }
        if (!lastAnswer.empty()) {
            json entry = json::parse(lastAnswer, nullptr, false);
            json ops = json::array();
            if (entry.is_object() && entry.contains("answer") && entry["answer"].is_object()) {
                for (const auto& c : entry["answer"].value("changes", json::array())) {
                    const json node = c.value("node", json());
                    ops.push_back(c.value("op", "") + " " + (node.is_string() ? node.get<std::string>() : node.is_object() ? node.value("id", "") : ""));
                }
            }
            report["revise"]["changes"] = ops;
        }
        fs::copy_file(build.Folder() / "layout.png", root / "revised.png", fs::copy_options::overwrite_existing);
    }
    const std::string text = report.dump(2);
    std::cout << text << "\n";
    if (const char* out = std::getenv("AIGE_LIVE_REPORT")) std::ofstream(out) << text << "\n";
}
