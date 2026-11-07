// brief_test: a request read into a Brief as the Builder reads it, and the Confirm window it shows (a test of the
// Brief code without the editor).
//
//   brief_test <request> [--type <build type>] [--port 8765] [--out <folder>]
//
// The AI service reads the request three ways (literal, typical, generous: /places/interpret, as PlaceBuild does);
// the drafts are merged (MergeBriefs), completed (CompleteBrief) and shown as the Confirm window (BriefWindow).
// Writes <out>/drafts.json, brief.json and window.json, and prints the window's main fields.
#include "editing/BuildWindow.h"
#include "places/Brief.h"
#include "places/Ontology.h"
#include "places/PlaceValidate.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>

using nlohmann::json;
namespace fs = std::filesystem;
using namespace aige;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "brief_test <request> [--type <build type>] [--port 8765] [--out <folder>]\n";
        return 2;
    }
    std::string request = argv[1], type, out = "builder_tests/brief_test";
    int port = 8765;
    for (int i = 2; i + 1 < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--type") type = argv[++i];
        else if (a == "--port") port = std::atoi(argv[++i]);
        else if (a == "--out") out = argv[++i];
    }
    Ontology ontology;
    for (const auto& e : ontology.Load({EnginePlacesDir()})) std::cout << "ontology: " << e << "\n";
    // --types <file>: each line a request; prints the build type its words name (no AI).
    if (request == "--sizes") { // each build type's sizes as loaded
        for (const auto& [id, b] : ontology.Builds()) {
            std::cout << id << ":";
            for (const auto& [size, cls] : b.sizes) std::cout << " " << size << "=" << cls;
            std::cout << "\n";
        }
        return 0;
    }
    if (request == "--types" && argc > 2) {
        std::ifstream in(argv[2]);
        for (std::string line; std::getline(in, line);) {
            if (line.empty()) continue;
            const std::string named = NamedBuildType(ontology, line);
            std::cout << (named.empty() ? "(none)" : named) << "\t" << line << "\n";
        }
        return 0;
    }
    if (type.empty()) type = NamedBuildType(ontology, request);
    const json context = InterpretContext(ontology, type);
    json choices = json::object();
    if (const BuildType* bt = ontology.Build(type); bt && bt->scope == "world") choices = {{"scope", "world"}, {"buildType", type}};
    std::cout << "build type: " << (type.empty() ? "(any)" : type) << "\n";

    // The three readings, side by side.
    std::vector<std::future<json>> asked;
    for (const char* reading : {"literal", "typical", "generous"}) {
        asked.push_back(std::async(std::launch::async, [&, reading] {
            httplib::Client client("127.0.0.1", port);
            client.set_read_timeout(600);
            const json body = {{"query", request}, {"context", context}, {"drafts", 1}, {"reading", reading}, {"choices", choices}};
            const auto r = client.Post("/places/interpret", body.dump(), "application/json");
            if (!r || r->status != 200) return json{{"ok", false}, {"errors", {"no answer"}}};
            return json::parse(r->body);
        }));
    }
    std::vector<json> drafts;
    json all = json::array();
    for (auto& f : asked) {
        const json answer = f.get();
        all.push_back(answer);
        for (const auto& d : answer.value("drafts", json::array())) drafts.push_back(d);
        if (!answer.value("ok", true)) std::cout << "a reading failed: " << answer.value("errors", json::array()).dump() << "\n";
    }
    if (drafts.empty()) drafts.push_back(FallbackDraft(ontology, request));
    const BriefMerge merge = MergeBriefs(drafts);
    const json brief = CompleteBrief(ontology, merge.brief);
    const json window = BriefWindow(ontology, brief, &merge, nullptr, true, 2);

    fs::create_directories(out);
    std::ofstream(fs::path(out) / "drafts.json") << all.dump(2);
    std::ofstream(fs::path(out) / "brief.json") << brief.dump(2);
    std::ofstream(fs::path(out) / "window.json") << window.dump(2);
    for (const auto& p : ValidateBrief(ontology, brief)) std::cout << "problem: " << p.where << ": " << p.message << "\n";
    std::cout << "written: " << out << "\n";
    return 0;
}
