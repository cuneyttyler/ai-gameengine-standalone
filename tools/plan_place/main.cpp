// plan_place: a place planned from a request with the AI service, as the Planner plans it (PlaceBuild: the Brief from
// the request, the assets step, the plan tree, the layout), without the editor or a project.
//
//   plan_place <request> [--port 8765] [--out <folder>] [--choices <json>]
//
// The AI service must be running on the port (uv run uvicorn app.main:app --port <port> in ai_service/). Its project is
// set to <out>/project, where the build keeps its working files. --choices fixes Brief choices, as a JSON object
// (e.g. {"buildType": "settlement", "shape.layout": "radial"}).
//
// Writes to <out>: brief.json, spec.json (the plan tree), layout.solved.json (the solved plan), layout.png (its zones),
// plan.png (the plan map) and blockout.png (the plan in the game's view). The land alone for painting:
// plan_blockout --prepare <out>/layout.solved.json <folder> 48 --land.
#include "assets/TextureCook.h"
#include "editing/PlaceBlockout.h"
#include "editing/PlaceBuild.h"
#include "editing/PlaceInspect.h"
#include "editing/PlaceScene.h"
#include "places/Ontology.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>

using nlohmann::json;
namespace fs = std::filesystem;
using namespace aige;

namespace {

struct Call {
    int id;
    std::string path;
    json body;
    int timeout;
};

void WriteJson(const fs::path& file, const json& value) { std::ofstream(file) << value.dump(2); }

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "plan_place <request> [--port 8765] [--out <folder>] [--choices <json>]\n";
        return 2;
    }
    const std::string request = argv[1];
    std::string out = "builder_tests/plan_place";
    int port = 8765;
    json choices = json::object();
    for (int i = 2; i + 1 < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--port") port = std::atoi(argv[++i]);
        else if (a == "--out") out = argv[++i];
        else if (a == "--choices") {
            choices = json::parse(argv[++i], nullptr, false);
            if (!choices.is_object()) {
                std::cerr << "--choices: not a JSON object\n";
                return 2;
            }
        }
    }
    const fs::path folder = fs::absolute(out);
    const fs::path root = folder / "project";
    fs::remove_all(root);
    fs::create_directories(root / "places");

    Ontology ontology;
    for (const auto& e : ontology.Load({EnginePlacesDir()})) std::cout << "ontology: " << e << "\n";

    {
        httplib::Client client("127.0.0.1", port);
        client.set_connection_timeout(5);
        const auto r = client.Put("/project", json{{"root", root.string()}}.dump(), "application/json");
        if (!r || r->status != 200) {
            std::cerr << "the AI service isn't answering on port " << port << "\n";
            return 1;
        }
    }

    // The build's requests, answered by the service four at a time (as the Planner's pump).
    std::deque<Call> queue;
    int nextId = 1;
    PlaceBuild::Host host;
    host.ontology = [&]() -> const Ontology& { return ontology; };
    host.catalog = [] { return Catalog{}; };
    host.isoGrid = [] { return true; };
    host.request = [&](const std::string& path, const json& body, int timeout) {
        queue.push_back({nextId, path, body, timeout});
        return nextId++;
    };
    host.projectRoot = root;
    host.layoutStep = false; // as the Planner builds
    PlaceBuild build(host);
    auto pump = [&] {
        std::vector<std::pair<int, std::future<json>>> flying;
        while (!queue.empty() || !flying.empty()) {
            while (!queue.empty() && flying.size() < 4) {
                const Call call = queue.front();
                queue.pop_front();
                std::cout << "  asking " << call.path << "\n";
                flying.push_back({call.id, std::async(std::launch::async, [port, call] {
                                      httplib::Client c("127.0.0.1", port);
                                      c.set_connection_timeout(5);
                                      c.set_read_timeout(std::max(call.timeout, 600));
                                      const auto r = c.Post(call.path, call.body.dump(), "application/json");
                                      json o = r && r->status == 200 ? json::parse(r->body, nullptr, false)
                                                                     : json{{"ok", false}, {"errors", {"no answer"}}};
                                      return o.is_discarded() ? json{{"ok", false}, {"errors", {"not JSON"}}} : o;
                                  })});
            }
            for (size_t i = 0; i < flying.size();) {
                if (flying[i].second.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready) {
                    ++i;
                    continue;
                }
                const int id = flying[i].first;
                const json body = flying[i].second.get();
                flying.erase(flying.begin() + static_cast<std::ptrdiff_t>(i));
                build.OnResponse(id, body);
            }
        }
    };

    if (const std::string why = build.Start(request, choices); !why.empty()) {
        std::cerr << "can't start: " << why << "\n";
        return 1;
    }
    pump();
    // As the Planner drives a build: the request, the assets step, the plan.
    for (int step = 0; step < 40 && build.Stage() != "plan" && build.Stage() != "planned"; ++step) {
        const std::string stage = build.Stage();
        std::cout << "stage: " << stage << "\n";
        std::string why;
        if (stage == "request") why = build.Step("assets");
        else if (stage == "assets") why = build.Confirm();
        else if (stage == "layout") why = build.PlanFromLayout();
        if (!why.empty()) {
            std::cerr << "stopped at " << stage << ": " << why << "\n";
            return 1;
        }
        pump();
    }
    const json& solved = build.Solved();
    if (!solved.is_object() || !solved.contains("objects")) {
        std::cerr << "no plan: " << build.State().dump().substr(0, 800) << "\n";
        return 1;
    }

    WriteJson(folder / "brief.json", build.Brief());
    WriteJson(folder / "spec.json", build.Spec());
    WriteJson(folder / "layout.solved.json", solved);
    // The inspect requests draw the layout's and the plan's images in the build's folder.
    InspectLayoutRequest(build, &ontology);
    InspectPlanRequest(build, &ontology);
    std::error_code ec;
    fs::copy_file(build.Folder() / "inspect-layout.png", folder / "layout.png", fs::copy_options::overwrite_existing, ec);
    fs::copy_file(build.Folder() / "inspect-plan.png", folder / "plan.png", fs::copy_options::overwrite_existing, ec);
    const auto [x0, y0, x1, y1] = OccupiedBounds(solved);
    WritePng(folder / "blockout.png", RenderPlanBlockout(solved, x0, y0, x1, y1, 2000, &ontology));

    int buildings = 0;
    for (const auto& o : solved["objects"]) buildings += o["layer"] == "building" && o.value("primary", false);
    std::cout << build.Brief().value("name", "") << " (" << build.Brief().value("buildType", "") << "): " << buildings
              << " buildings, " << solved.value("stats", json::object()).dump() << "\nwritten: " << folder.string() << "\n";
    return 0;
}
