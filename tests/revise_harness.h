// A build run as the editor runs it, for the revision tests (tests/layout_tests, tests/plan_tests):
// a fresh project, the real AI service, and the same calls the Build panel's buttons make
// (EditorApp's build:* messages): the request, ① Confirm, Build, ② Layout and its revision box,
// Plan, ③ Plan's Approve and its revision box. Each case's pictures and numbers go to its folder.
#pragma once

#include "assets/AssetDatabase.h"
#include "assets/Catalog.h"
#include "assets/Prefab.h"
#include "assets/ProjectTemplate.h"
#include "editing/PlaceBuild.h"
#include "places/Ontology.h"
#include "places/Solver.h"

#include <gtest/gtest.h>
#include <httplib.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace aige::revise_tests {

using json = nlohmann::json;
namespace fs = std::filesystem;

inline fs::path SourceDir() { return fs::path(AIGE_SOURCE_DIR); }

inline json ReadJson(const fs::path& p) {
    std::ifstream in(p);
    return in ? json::parse(in, nullptr, false) : json();
}

inline void WriteText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

// AIGE_REVISE_PORT=<service port> runs them (uvicorn app.main:app --port <port>, no AIGE_SERVICE_TOKEN);
// AIGE_REVISE_CASES=<name>,<name> runs only those.
inline int ServicePort() {
    const char* p = std::getenv("AIGE_REVISE_PORT");
    return p ? std::atoi(p) : 0;
}
inline bool Chosen(const std::string& name) {
    const char* only = std::getenv("AIGE_REVISE_CASES");
    if (only == nullptr || *only == 0) return true;
    std::stringstream in(only);
    for (std::string part; std::getline(in, part, ',');) {
        if (part == name) return true;
    }
    return false;
}

// A crash said, not just an exit code: what was thrown, or the Windows exception and where.
inline void ReportCrashes() {
    static bool once = false;
    if (once) return;
    once = true;
    std::set_terminate([] {
        try {
            if (auto e = std::current_exception()) std::rethrow_exception(e);
            std::cerr << "terminate: no exception" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "terminate: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "terminate: an unknown exception" << std::endl;
        }
        std::cerr.flush();
        std::_Exit(3);
    });
#ifdef _WIN32
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* info) -> LONG {
        HMODULE module = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(info->ExceptionRecord->ExceptionAddress), &module);
        char name[MAX_PATH] = "?";
        if (module) GetModuleFileNameA(module, name, MAX_PATH);
        std::cerr << "crash: exception 0x" << std::hex << info->ExceptionRecord->ExceptionCode << " at offset 0x"
                  << (reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress) - reinterpret_cast<uintptr_t>(module))
                  << std::dec << " in " << name << std::endl;
        std::cerr.flush();
        std::_Exit(4);
    });
#endif
}

// One build in its own project, driven like the Build panel.
class LiveBuild {
public:
    LiveBuild(const fs::path& out, int port) : out_(out), port_(port) {
        ReportCrashes();
        std::error_code ec;
        fs::remove_all(out_, ec);
        fs::create_directories(out_);
        log_.open(out_ / "run.log");
        started_ = std::chrono::steady_clock::now();
        // The project apart from the results, new each run: the AI service keeps the last one's world index open.
        project_ = fs::temp_directory_path() / "aige_revise_tests" /
                   (out_.parent_path().parent_path().filename().string() + "-" + out_.filename().string() + "-" +
                    std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
        Say("project: " + project_.string());
        error_ = CreateProject(project_, std::nullopt, false).value_or("");
        httplib::Client client("127.0.0.1", port_);
        client.set_connection_timeout(5);
        const auto r = client.Put("/project", json{{"root", fs::absolute(project_).string()}}.dump(), "application/json");
        if (!r || r->status != 200) error_ = "the AI service isn't answering on port " + std::to_string(port_);
        // The model the editor would send (SendProvider): Preferences ▸ AI, or AIGE_REVISE_MODEL=<provider>|<model>.
        json chosen = {{"provider", "mistral"}, {"model", ""}};
        if (const char* local = std::getenv("LOCALAPPDATA")) {
            const json prefs = ReadJson(fs::path(local) / "aige" / "editor.json");
            if (prefs.is_object()) chosen = {{"provider", prefs.value("aiProvider", "mistral")}, {"model", prefs.value("aiModel", "")}};
        }
        if (const char* model = std::getenv("AIGE_REVISE_MODEL")) {
            const std::string m = model;
            const size_t bar = m.find('|');
            chosen = {{"provider", m.substr(0, bar)}, {"model", bar == std::string::npos ? "" : m.substr(bar + 1)}};
        }
        if (error_.empty()) {
            const auto p = client.Put("/provider", chosen.dump(), "application/json");
            if (!p || p->status != 200) error_ = "the AI service didn't take the model " + chosen.dump();
        }
        model_ = chosen;
        ontology_.Load({EnginePlacesDir()});
        prefabs_ = std::make_unique<PrefabLibrary>(Project{project_});
        assets_ = std::make_unique<AssetDatabase>(Project{project_});
        assets_->Refresh();
        PlaceBuild::Host host;
        host.ontology = [this]() -> const Ontology& { return ontology_; };
        host.catalog = [this] {
            CatalogOptions options;
            options.isoGrid = true;
            return BuildCatalog(*assets_, &ontology_, options);
        };
        host.isoGrid = [] { return true; };
        host.request = [this](const std::string& path, const json& body, int timeout) {
            queue_.push_back({next_, path, body, timeout});
            return next_++;
        };
        host.projectRoot = project_;
        host.layoutStep = true; // as in the editor
        build_ = std::make_unique<PlaceBuild>(host);
    }

    const std::string& Error() const { return error_; }
    PlaceBuild& Build() { return *build_; }
    const Ontology& Places() const { return ontology_; }
    const fs::path& Out() const { return out_; }
    const json& Model() const { return model_; }

    void Say(const std::string& text) {
        const auto s = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started_).count();
        log_ << "[" << s << "s] " << text << std::endl;
        std::cout << "  [" << s << "s] " << text << std::endl;
    }

    // The AI's answers, up to four requests at a time as the editor sends them, each to the build as it comes.
    void Pump() {
        struct Flight {
            int id;
            std::string path;
            std::chrono::steady_clock::time_point t0;
            std::future<json> answer;
        };
        std::vector<Flight> flying;
        while (!queue_.empty() || !flying.empty()) {
            while (!queue_.empty() && flying.size() < 4) {
                Call call = queue_.front();
                queue_.pop_front();
                auto answer = std::async(std::launch::async, [port = port_, path = call.path, body = call.body.dump()] {
                    httplib::Client c("127.0.0.1", port);
                    c.set_connection_timeout(5);
                    c.set_read_timeout(1800);
                    const auto r = c.Post(path, body, "application/json");
                    json out = r && r->status == 200 ? json::parse(r->body, nullptr, false) : json{{"ok", false}, {"errors", {"no answer"}}};
                    return out.is_discarded() ? json{{"ok", false}, {"errors", {"not JSON"}}} : out;
                });
                flying.push_back({call.id, call.path, std::chrono::steady_clock::now(), std::move(answer)});
            }
            for (size_t i = 0; i < flying.size();) {
                if (flying[i].answer.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready) {
                    ++i;
                    continue;
                }
                Flight done = std::move(flying[i]);
                flying.erase(flying.begin() + static_cast<std::ptrdiff_t>(i));
                const json body = done.answer.get();
                const auto s = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - done.t0).count();
                Say(done.path + " " + (body.value("ok", false) ? "ok" : "failed") + " (" + std::to_string(s) + "s)");
                try {
                    build_->OnResponse(done.id, body);
                } catch (const std::exception& e) {
                    Say("the build threw on the answer to " + done.path + ": " + e.what());
                    throw;
                }
            }
        }
    }

    // The request typed and built to ② Layout: ① Confirm's Next, then Build (the zones planned and laid out).
    std::string ToLayout(const std::string& request) {
        Say("model: " + model_.dump() + "; request: " + request);
        if (auto why = build_->Start(request); !why.empty()) return why;
        Pump();
        for (int step = 0; step < 10 && build_->Stage() != "layout"; ++step) {
            const std::string stage = build_->Stage();
            std::string why;
            if (stage == "request") why = build_->Step("assets");
            else if (stage == "assets") why = build_->Confirm();
            else return "stuck at " + stage + ": " + build_->State().value("status", "");
            if (!why.empty()) return stage + ": " + why;
            Pump();
        }
        if (build_->Stage() != "layout") return "never reached the layout: " + build_->State().value("status", "");
        Say("layout: " + std::to_string(Zones().size()) + " zones");
        return {};
    }

    // ② Layout's Plan, then ③ Plan's Approve: what's inside each zone planned, solved and approved.
    std::string ToPlanned() {
        if (auto why = build_->PlanFromLayout(); !why.empty()) return "plan: " + why;
        Pump();
        for (int step = 0; step < 6 && build_->Stage() != "planned"; ++step) {
            const std::string stage = build_->Stage();
            if (stage != "plan") return "stuck at " + stage + ": " + build_->State().value("status", "");
            if (auto why = build_->Approve(); !why.empty()) return "approve: " + why;
            Pump();
        }
        if (build_->Stage() != "planned") return "never planned: " + build_->State().value("status", "");
        Say("planned");
        return {};
    }

    // The revision box: the user's words, sent; back at the stage it was asked at.
    std::string Revise(const std::string& text) {
        const std::string was = build_->Stage();
        Say("revise at " + was + ": " + text);
        std::string why;
        try {
            why = build_->Revise(text);
        } catch (const std::exception& e) {
            Say(std::string("Revise threw: ") + e.what());
            return std::string("Revise threw: ") + e.what();
        }
        if (!why.empty()) return why;
        Pump();
        if (build_->Stage() != was) return "after the revision the build is at " + build_->Stage() + ": " + build_->State().value("status", "");
        const json& revisions = build_->Revisions();
        if (!revisions.empty()) Say("revised: " + revisions.back().value("summary", revisions.back().dump().substr(0, 300)));
        return {};
    }

    // The zones the plan has: id -> module.
    std::map<std::string, std::string> Zones() const {
        std::map<std::string, std::string> out;
        const json spec = build_->Spec();
        if (!spec.contains("root")) return out;
        for (const auto& c : spec["root"].value("children", json::array())) {
            const ModuleDef* m = ontology_.Module(c.value("module", ""));
            if (m != nullptr && m->level == PlaceLevel::Zone) out[c.value("id", "")] = c.value("module", "");
        }
        return out;
    }

    // A zone named by any of `words` (in its id or its module): the first that matches, else "".
    std::string FindZone(const json& words, const std::set<std::string>& not_ = {}) const {
        const auto zones = Zones();
        for (const auto& w : words) {
            const std::string word = w.get<std::string>();
            for (const auto& [id, module] : zones) {
                if (not_.contains(id)) continue;
                if (id.find(word) != std::string::npos || module.find(word) != std::string::npos) return id;
            }
        }
        return {};
    }

    void Picture(const std::string& from, const std::string& as) {
        std::error_code ec;
        if (fs::exists(build_->Folder() / from, ec)) fs::copy_file(build_->Folder() / from, out_ / as, fs::copy_options::overwrite_existing, ec);
    }

private:
    struct Call {
        int id;
        std::string path;
        json body;
        int timeout;
    };
    fs::path out_, project_;
    int port_;
    std::string error_;
    json model_;
    std::ofstream log_;
    std::chrono::steady_clock::time_point started_;
    Ontology ontology_;
    std::unique_ptr<PrefabLibrary> prefabs_;
    std::unique_ptr<AssetDatabase> assets_;
    std::deque<Call> queue_;
    int next_ = 1;
    std::unique_ptr<PlaceBuild> build_;
};

// What the map shows of a zone: its id, spaced ("residential-ward" -> "residential ward").
inline std::string Shown(std::string id) {
    std::replace(id.begin(), id.end(), '-', ' ');
    return id;
}

// The case's words with each {A} put as the zone the map shows.
inline std::string Fill(std::string text, const std::map<std::string, std::string>& zones) {
    for (const auto& [key, id] : zones) {
        const std::string mark = "{" + key + "}";
        for (size_t at; (at = text.find(mark)) != std::string::npos;) text.replace(at, mark.size(), Shown(id));
    }
    return text;
}

// The zones' land: id -> its cells.
inline std::map<std::string, std::set<int>> ZoneLand(const json& solved) {
    std::map<std::string, std::set<int>> out;
    if (!solved.contains("grid")) return out;
    const int w = solved["grid"]["bounds"][2].get<int>();
    for (const auto& r : solved.value("regions", json::array())) {
        if (r.value("statement", "") != "site/zones") continue;
        for (int c : DecodeCells(r.value("cells", json::array()), w)) out[r.value("id", "")].insert(c);
    }
    return out;
}

inline int Width(const json& solved) { return solved.contains("grid") ? solved["grid"]["bounds"][2].get<int>() : 1; }

inline std::pair<double, double> Middle(const std::set<int>& cells, int w) {
    double x = 0, y = 0;
    for (int c : cells) x += c % w, y += c / w;
    const double n = static_cast<double>(std::max<size_t>(1, cells.size()));
    return {x / n, y / n};
}

// How much of `a` is still in `b`.
inline double Kept(const std::set<int>& a, const std::set<int>& b) {
    int same = 0;
    for (int c : a) same += b.contains(c);
    return static_cast<double>(same) / static_cast<double>(std::max<size_t>(1, a.size()));
}

inline bool InZone(const std::string& node, const std::string& zone) {
    return node == zone || node.rfind(zone + "/", 0) == 0 || node.rfind(zone + "#", 0) == 0;
}

// The things a zone holds, where they stand: "id@x,y".
inline std::set<std::string> ThingsIn(const json& solved, const std::string& zone) {
    std::set<std::string> out;
    for (const auto& o : solved.value("objects", json::array())) {
        if (InZone(o.value("node", ""), zone)) {
            out.insert(o.value("id", "") + "@" + std::to_string(o.value("x", 0)) + "," + std::to_string(o.value("y", 0)));
        }
    }
    return out;
}

// A zone's buildings (each parcel's main one).
inline int Buildings(const json& solved, const std::string& zone) {
    int n = 0;
    for (const auto& o : solved.value("objects", json::array())) {
        n += InZone(o.value("node", ""), zone) && o.value("layer", "") == "building" && o.value("primary", false);
    }
    return n;
}

// The cases of a folder (cases.json), and where their results go.
inline json Cases(const std::string& folder) { return ReadJson(SourceDir() / "tests" / folder / "cases.json"); }
inline fs::path ResultsOf(const std::string& folder, const std::string& name) { return SourceDir() / "tests" / folder / "results" / name; }

// Every case's verdict, side by side (results/summary.json).
inline void WriteSummary(const std::string& folder, const json& verdicts) {
    WriteText(SourceDir() / "tests" / folder / "results" / "summary.json", verdicts.dump(2));
    std::cout << "\n" << folder << ":\n";
    for (const auto& v : verdicts) {
        std::cout << "  " << (v.value("passed", false) ? "PASS " : "FAIL ") << v.value("name", "");
        for (const auto& f : v.value("failures", json::array())) std::cout << "\n        - " << f.get<std::string>();
        std::cout << "\n";
    }
}

} // namespace aige::revise_tests
