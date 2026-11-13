// ⑫ Implement (development_plan.md 34.3, 34.4, 34.6): the requirements' implementation items built in order (art first,
// then prefabs, markers, quest effects and objectives, guidance, cutscenes and their triggers, scripts, dialogue), each
// ticked once what it made is there; then every requirement checked: statically against the files, and against what a
// scene's test recorded (who was where, who hurt whom, the stages, the cutscenes played). A failed one is fixed by
// building its items again with what failed.
#include "gamebuild/GameBuild.h"

#include "assets/Project.h"
#include "assets/SceneIO.h"
#include "gameplay/Animation.h"
#include "gameplay/Behaviour.h"
#include "gameplay/Components.h"
#include "gameplay/Cutscene.h"
#include "gameplay/GameSession.h"
#include "gameplay/Settings.h"
#include "gameplay/Story.h"
#include "physics2d/Components.h"
#include "planner/PlannerDocs.h"
#include "renderer2d/Components.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace aige {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string Str(const json& j, const char* key) {
    const auto it = j.is_object() ? j.find(key) : j.end();
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool Contains(const std::string& text, const std::string& part) { return !part.empty() && Lower(text).find(Lower(part)) != std::string::npos; }

// An asset's file name: lowercase letters, digits and - (up to 48).
std::string AssetSlug(const std::string& name) {
    std::string out;
    for (const unsigned char c : name) {
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    out = out.substr(0, 48);
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? std::string("asset") : out;
}

// The order items are built in: what's drawn, then what's placed, then what the story does with it.
int Rank(const std::string& how) {
    static const std::map<std::string, int> ranks = {{"art", 0},       {"animation", 1}, {"prefab", 2},    {"marker", 3},  {"spawn-point", 3},
                                                     {"stats", 4},     {"quest-effect", 5}, {"objective", 5}, {"package", 5}, {"guidance", 6}, {"cutscene", 7},
                                                     {"trigger", 8},   {"script", 9},    {"dialogue", 10}};
    const auto it = ranks.find(how);
    return it == ranks.end() ? 9 : it->second;
}

bool Finished(const json& item) {
    const std::string s = item.value("status", "");
    return s == "built" || s == "failed" || s == "skipped";
}

std::pair<int, int> PngSize(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    unsigned char h[24] = {};
    if (!in.read(reinterpret_cast<char*>(h), 24) || h[1] != 'P' || h[2] != 'N' || h[3] != 'G') return {0, 0};
    auto be = [&](int i) { return (h[i] << 24) | (h[i + 1] << 16) | (h[i + 2] << 8) | h[i + 3]; };
    return {be(16), be(20)};
}

// A picture standing on an entity, `height` pixels tall.
void StandArt(Registry& reg, Entity e, const std::string& art, float height, const fs::path& root) {
    const auto [w, h] = PngSize(root / art);
    auto& s = reg.get_or_emplace<Sprite>(e);
    s = Sprite{};
    s.texture = art;
    s.height = height;
    s.width = h > 0 ? height * static_cast<float>(w) / static_cast<float>(h) : height;
}

float HeightOf(const std::string& size) {
    if (size == "tiny") return 24.0f;
    if (size == "small") return 40.0f;
    if (size == "large") return 112.0f;
    if (size == "huge") return 160.0f;
    return 72.0f;
}

json ReadJson(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    // A file not there (yet: the quests before Quests writes them) or unreadable: an empty object, not null.
    const json j = in ? json::parse(in, nullptr, false) : json::object();
    return j.is_discarded() || j.is_null() ? json::object() : j;
}

std::optional<std::pair<float, float>> MarkerIn(Registry& reg, const std::string& name) {
    for (auto [e, m, at] : reg.view<Marker, WorldTransform2D>().each()) {
        if (m.name == name) return std::make_pair(at.x, at.y);
    }
    return std::nullopt;
}

std::string WhenText(const json& when) {
    if (!when.is_object()) return {};
    if (when.contains("quest")) return when.value("quest", "") + " stage " + std::to_string(when.value("stage", 0));
    if (when.contains("after")) return "after: " + when.value("after", "");
    if (when.contains("event")) return "on the event " + when.value("event", "");
    if (when.contains("reach")) return "on reaching " + when.value("reach", "");
    if (when.contains("leave")) return "on leaving " + when.value("leave", "");
    return {};
}

std::string ChecksText(const json& accept) {
    std::string out;
    for (const auto& a : accept) {
        std::string one;
        for (const auto& [k, v] : a.items()) one += (one.empty() ? "" : " ") + (k == "check" ? std::string() : k + "=") + (v.is_string() ? v.get<std::string>() : v.dump());
        out += (out.empty() ? "" : "; ") + one;
    }
    return out;
}

} // namespace

// --- Starting and driving ------------------------------------------------------------------------------------------

void GameBuild::StartImplement(bool fresh) {
    if (fresh) features_ = json::array(); // ⑫'s AI-panel tasks are the implementation items' now
    reqs_["implStarted"] = true;
    reqs_["implementing"] = true;
    reqs_["tab"] = ""; // every item, whichever tab it's in
    reqs_["onlyKind"] = "";
    status_ = "working";
    int todo = 0;
    for (const auto& i : reqs_["implementations"]) todo += !Finished(i);
    message_ = "⑫ Implementing: " + std::to_string(todo) + " items, the art first…";
    Record("implement", "requirements", std::to_string(todo) + " implementation items to build");
    SaveRequirements();
}

std::string GameBuild::ApproveAssetArt(bool draw) {
    if (!reqs_.contains("artWaiting")) return "no assets are waiting to be drawn";
    reqs_["artApproved"] = draw;
    reqs_.erase("artWaiting");
    if (!draw) {
        for (auto& i : reqs_["implementations"]) {
            if (i.value("status", "") == "todo" && (i.value("by", "") == "art")) i["status"] = "skipped", i["error"] = "the art was skipped";
        }
    }
    status_ = "working";
    Record("approve", "assets", draw ? "the assets' art approved" : "the assets' art skipped");
    SaveRequirements();
    Save();
    return {};
}

std::string GameBuild::ImplementNow() {
    if (reqs_.value("items", json::array()).empty()) return "make the requirements first";
    if (reqs_.value("status", "") == "making") return "the requirements are being made";
    if (stage_ != "done" && stage_ != "finish" && stage_ != "playtest" && stage_ != "result" && stage_ != "features") return "the Build tabs are built first (up to Visuals)";
    // What's left, again: failed and skipped items, and those a stop left half-way (their art or AI-panel task no longer
    // going on); built ones stay.
    auto going = [&](const json& i) {
        if (!i.value("feature", "").empty()) {
            for (const auto& f : features_) {
                if (f.value("id", "") == i.value("feature", "")) return f.value("status", "") == "queued" || f.value("status", "") == "running";
            }
            return false;
        }
        if (!i.value("art", "").empty()) {
            for (const auto& a : art_) {
                if (a.value("subject", "") == i.value("art", "")) return a.value("status", "") == "queued" || a.value("status", "") == "running";
            }
            return false;
        }
        return false; // waiting on an answer that a stop dropped
    };
    std::set<std::string> redraw;
    for (auto& i : reqs_["implementations"]) {
        const std::string st = i.value("status", "");
        if (st == "failed" || st == "skipped" || (st == "building" && !going(i))) {
            if (!i.value("art", "").empty()) redraw.insert(i.value("art", ""));
            i["status"] = "todo", i.erase("error"), i.erase("retried");
        }
    }
    // Their art that failed (no credits, the service down): drawn again when they come round (only theirs).
    for (auto& a : art_) {
        if (redraw.contains(a.value("subject", "")) && a.value("status", "") == "failed") a["status"] = "queued", a["error"] = "";
    }
    reqs_.erase("artWaiting");
    if (stage_ != "done") stage_ = "finish";
    StartImplement(false);
    Save();
    return {};
}

std::string GameBuild::CheckRequirementsNow() {
    if (reqs_.value("items", json::array()).empty()) return "make the requirements first";
    CheckRequirements();
    SaveRequirements();
    return {};
}

std::string GameBuild::FixRequirements(const std::string& id) {
    if (reqs_.value("items", json::array()).empty()) return "make the requirements first";
    int again = 0;
    for (const auto& r : reqs_["items"]) {
        if ((id == "*" || id.empty()) ? r.value("status", "") != "failed" : r.value("id", "") != id) continue;
        const std::string why = r.value("why", "");
        for (auto& i : reqs_["implementations"]) {
            if (i.value("req", "") != r.value("id", "")) continue;
            // Built and still failing: built again by the AI panel, told what failed; failed: tried again.
            if (i.value("status", "") == "built" && i.value("by", "") == "rule") i["by"] = "ai-panel";
            i["status"] = "todo";
            i["problem"] = why;
            i.erase("error");
            ++again;
        }
    }
    if (again == 0) return id == "*" ? "no requirement has failed" : "it has nothing to build again";
    StartImplement(false);
    Save();
    return {};
}

bool GameBuild::DriveImplement() {
    bool did = AddAnimatedAssets(); // before the lists are walked (they grow)
    json& impl = reqs_["implementations"];
    json& items = reqs_["items"];
    std::map<std::string, json*> reqOf;
    for (auto& r : items) reqOf[r.value("id", "")] = &r;
    // Answers: a cutscene written.
    for (const auto& [itemId, body] : std::exchange(implAnswers_, {})) {
        for (auto& i : impl) {
            if (i.value("id", "") != itemId) continue;
            did = true;
            json* r = reqOf.contains(i.value("req", "")) ? reqOf[i.value("req", "")] : nullptr;
            if (!body.value("ok", false) || !r) {
                i["status"] = "failed";
                i["error"] = body.contains("errors") ? body["errors"].dump() : std::string("no answer");
                continue;
            }
            if (i.value("how", "") == "animation") ObjectAnimated(i, *r, body);
            else WriteCutsceneItem(i, *r, body.value("cutscene", json::object()));
        }
    }
    // What's being built elsewhere: the art (its pictures and clips), the AI panel's tasks.
    for (auto& i : impl) {
        if (i.value("status", "") != "building") continue;
        if (!i.value("art", "").empty()) {
            const json* a = nullptr;
            for (const auto& x : art_) a = x.value("subject", "") == i.value("art", "") ? &x : a;
            if (!a) {
                i["status"] = "failed", i["error"] = "its art entry is gone", did = true;
                continue;
            }
            const std::string st = a->value("status", "");
            if (st != "done" && st != "failed") continue;
            did = true;
            if (!i.value("action", "").empty()) {
                // An animation: there once its clip is on the sheet.
                const json actions = a->value("actions", json::array());
                int at = -1;
                for (int k = 0; k < static_cast<int>(actions.size()); ++k) at = actions[k].value("action", "") == i.value("action", "") ? k : at;
                const bool made = at >= 0 && at < a->value("next", 0) && !a->value("sheet", "").empty();
                i["status"] = made ? "built" : "failed";
                if (!made) i["error"] = a->value("error", std::string("its clip wasn't drawn"));
                else i["made"] = json::array({a->value("sheet", "")});
                continue;
            }
            const std::string path = a->value("art", a->value("sheet", ""));
            if (st == "done" && !path.empty()) {
                i["status"] = "built";
                i["made"] = json::array({path});
                if (json* r = reqOf.contains(i.value("req", "")) ? reqOf[i.value("req", "")] : nullptr; r && r->contains("asset")) {
                    (*r)["asset"][a->contains("sheet") && !a->value("sheet", "").empty() ? "sheet" : "art"] = path;
                }
            } else if (st == "done") {
                i["status"] = "built"; // a character drawn on its prefab by ⑪
            } else {
                i["status"] = "failed", i["error"] = a->value("error", std::string("it wasn't drawn"));
            }
            continue;
        }
        if (!i.value("feature", "").empty()) {
            std::string st, error;
            for (const auto& f : features_) {
                if (f.value("id", "") == i.value("feature", "")) st = f.value("status", ""), error = f.value("error", "");
            }
            if (st == "done") {
                i["status"] = "built", did = true;
            } else if (st == "failed" || st == "skipped") {
                did = true;
                json* r = reqOf.contains(i.value("req", "")) ? reqOf[i.value("req", "")] : nullptr;
                // A change that didn't apply (a bad entity id): asked once more, told to make it (not when a provider is out
                // of credits: that fails again). Not after searching for what isn't there: that fails, saying what was missing.
                const bool stuck = (error.find("valid change") != std::string::npos || error.find("doesn't exist") != std::string::npos) &&
                                   error.find("couldn't find") == std::string::npos && error.find("kept looking") == std::string::npos;
                if (stuck && r && !i.value("retried", false) && error.find("credit") == std::string::npos) {
                    i["retried"] = true;
                    ToPanel(i, *r, error + ". Don't look again for what isn't there: make it (create the entity at the "
                                           "marker, write the script) with what you know, and use only entities the scene has");
                } else {
                    i["status"] = "failed", i["error"] = error;
                }
            }
        }
    }
    // The next items whose turn it is: everything of their requirement ranked before them, and the requirements they
    // come after, finished.
    auto ready = [&](const json& item) {
        const int rank = Rank(item.value("how", ""));
        const json after = item.value("after", json::array());
        for (const auto& o : impl) {
            if (&o == &item || Finished(o)) continue;
            if (o.value("req", "") == item.value("req", "") && Rank(o.value("how", "")) < rank) return false;
            if (o.value("req", "") != item.value("req", "") && std::find(after.begin(), after.end(), o.value("req", "")) != after.end()) return false;
        }
        return true;
    };
    // A Build tab's own items only (its step builds them), else every item.
    const std::string tab = reqs_.value("tab", ""), onlyKind = reqs_.value("onlyKind", "");
    auto mine = [&](const json& i) {
        if (tab.empty()) return true;
        const auto r = reqOf.find(i.value("req", ""));
        return r != reqOf.end() && TabOf(i, *r->second) == tab && (onlyKind.empty() || r->second->value("kind", "") == onlyKind);
    };
    std::vector<json*> order;
    for (auto& i : impl) {
        if (i.value("status", "") == "todo" && mine(i)) order.push_back(&i);
    }
    std::stable_sort(order.begin(), order.end(), [](const json* a, const json* b) { return Rank(a->value("how", "")) < Rank(b->value("how", "")); });
    int ran = 0, waitingForArt = 0;
    for (json* ip : order) {
        if (ran >= 24) break;
        json& i = *ip;
        if (!ready(i)) continue;
        json* r = reqOf.contains(i.value("req", "")) ? reqOf[i.value("req", "")] : nullptr;
        if (!r) {
            i["status"] = "skipped", i["error"] = "its requirement is gone";
            continue;
        }
        // Paid art waits for the user (unless the Brief's plan.building is automatic).
        if (i.value("by", "") == "art" && !AutoApprove() && !reqs_.value("artApproved", false) && NeedsDrawing(i, *r)) {
            ++waitingForArt;
            continue;
        }
        try {
            ImplementItem(i, *r);
        } catch (const std::exception& e) {
            // One item that can't be built (a value of a shape it didn't expect): it fails, the others go on.
            i["status"] = "failed", i["error"] = std::string("building it stopped: ") + e.what();
        }
        ++ran;
        did = true;
    }
    if (waitingForArt > 0 && !reqs_.contains("artWaiting")) {
        reqs_["artWaiting"] = waitingForArt;
        message_ = "⑫ " + std::to_string(waitingForArt) + " assets and animations to draw: approve them in Requirements, or skip them";
        did = true;
    }
    const bool building = std::any_of(impl.begin(), impl.end(), [&](const json& i) { return i.value("status", "") == "building" && mine(i); });
    const bool todo = std::any_of(impl.begin(), impl.end(), [&](const json& i) { return i.value("status", "") == "todo" && mine(i); });
    if (waitingForArt > 0 && !building && ran == 0) status_ = "waiting"; // until the art is approved or skipped
    if (!tab.empty() && todo && !building && ran == 0 && waitingForArt == 0) {
        // A tab's items waiting on a later tab's: left for the last pass, which builds every item left.
        reqs_["implementing"] = false;
        MarkMetRequirements();
        message_ = "Build ▸ " + tab + ": its items built (some wait for later steps)";
        did = true;
    } else if (todo && !building && ran == 0 && waitingForArt == 0) {
        // Nothing can go on: what's left waits on something that won't be (a loop of afters).
        for (auto& i : impl) {
            if (i.value("status", "") == "todo") i["status"] = "failed", i["error"] = "it waits on items that can't be built first";
        }
        did = true;
    }
    if (!tab.empty() && !todo && !building && reqs_.value("implementing", false)) {
        // A tab's items built: what already holds verified (the rest is checked when everything is built).
        reqs_["implementing"] = false;
        MarkMetRequirements();
        message_ = "Build ▸ " + tab + ": its items built";
        did = true;
    } else if (!todo && !building && reqs_.value("implementing", false)) {
        reqs_["implementing"] = false;
        CheckRequirements();
        const json s = reqs_.value("summary", json::object());
        message_ = "⑫ Implemented: " + std::to_string(s.value("built", 0) + s.value("verified", 0)) + " requirements built (" +
                   std::to_string(s.value("verified", 0)) + " verified), " + std::to_string(s.value("failed", 0)) + " failed, " +
                   std::to_string(s.value("todo", 0)) + " to test";
        Record("implement", "requirements", message_);
        did = true;
    } else if (did) {
        int done = 0;
        for (const auto& i : impl) done += Finished(i);
        if (!reqs_.contains("artWaiting")) message_ = "⑫ Implementing: " + std::to_string(done) + " of " + std::to_string(impl.size()) + " items built…";
    }
    if (did) SaveRequirements();
    return did;
}

std::string GameBuild::TabOf(const json& item, const json& req) const {
    // Systems builds the Systems page's features (their records, screens, inputs), nothing else. Otherwise by how it's
    // built (art, a placement, an objective, a package or line, a cutscene); a script or a stage's effect by what it's
    // about: people's doings Behaviour's, a thing's use Props & objects', a moment's effect Scenes', the quests' Quests'.
    const std::string kind = req.value("kind", ""), how = item.value("how", "");
    if (kind == "system") return "systems";
    auto aboutCharacter = [&]() {
        if (req.contains("animation") && req["animation"].is_object()) {
            const std::string who = req["animation"].value("who", "");
            return who.rfind("character/", 0) == 0 || who.rfind("class:", 0) == 0;
        }
        if (req.contains("asset") && req["asset"].is_object()) return req["asset"].value("type", "") == "character";
        return false;
    };
    if (how == "art" || how == "animation") {
        if (req.contains("asset") && req["asset"].is_object() && req["asset"].value("type", "") == "ui") return "visuals";
        return aboutCharacter() ? "characters" : "objects";
    }
    if (how == "prefab" || how == "marker" || how == "spawn-point") return "placements";
    if (how == "objective" || how == "guidance") return "quests";
    if (how == "package" || how == "dialogue") return "behaviour";
    if (how == "cutscene" || how == "trigger") return "scenes";
    const bool people = kind == "presence" || kind == "whereabouts" || kind == "arrival" || kind == "movement";
    const bool things = kind == "prop" || kind == "item" || kind == "gateway";
    if (how == "quest-effect") {
        if (kind == "quest" || kind == "guidance") return "quests";
        if (kind == "character-state" || kind == "dialogue" || kind == "combat") return "behaviour";
        if (kind == "effect" || kind == "cutscene" || kind == "scene") return "scenes";
        return "placements"; // someone or something brought somewhere at a stage
    }
    if (kind == "asset" || kind == "animation") return aboutCharacter() ? "characters" : "objects";
    if (kind == "quest" || kind == "guidance") return "quests";
    if (how == "script" || how == "stats") {
        if (things) return "objects";
        if (kind == "effect" || kind == "cutscene" || kind == "scene") return "scenes";
        return "behaviour"; // a fight, someone's doings
    }
    if (people || things) return "placements";
    if (kind == "character-state" || kind == "dialogue" || kind == "combat") return "behaviour";
    if (kind == "cutscene" || kind == "scene" || kind == "effect") return "scenes";
    return "systems";
}

std::string GameBuild::BuildItems(const std::string& tab, const std::string& kind) {
    if (reqs_.value("items", json::array()).empty()) return "make the requirements first (Requirements tab)";
    if (reqs_.value("status", "") == "making") return "the requirements are being made";
    if (reqs_.value("implementing", false) && status_ == "working") return "items are being built: try again once they're done";
    std::map<std::string, const json*> reqOf;
    for (const auto& r : reqs_["items"]) reqOf[r.value("id", "")] = &r;
    int again = 0;
    for (auto& i : reqs_["implementations"]) {
        const auto r = reqOf.find(i.value("req", ""));
        if (r == reqOf.end() || TabOf(i, *r->second) != tab || (!kind.empty() && r->second->value("kind", "") != kind)) continue;
        const std::string st = i.value("status", "");
        if (st == "built") continue;
        if (st != "todo") i["status"] = "todo", i.erase("error");
        ++again;
    }
    if (again == 0) return kind.empty() ? "every item of it is built" : "every " + kind + " item is built";
    reqsOnly_ = status_ != "working"; // stopped: only these, then stopped again
    reqs_["tab"] = tab;
    reqs_["onlyKind"] = kind;
    reqs_["implStarted"] = true;
    reqs_["implementing"] = true;
    status_ = "working";
    message_ = "Building " + std::to_string(again) + (kind.empty() ? " items" : " " + kind + " items") + "…";
    Record("implement", tab, std::to_string(again) + " items to build", "the user: build all" + (kind.empty() ? std::string() : " (" + kind + ")"));
    SaveRequirements();
    Save();
    return {};
}

bool GameBuild::RunTab(const std::string& tab) {
    if (reqs_.value("items", json::array()).empty() || reqs_.value("status", "") == "making") return false;
    json& started = reqs_["tabsStarted"];
    if (!started.is_object()) started = json::object();
    if (!started.value(tab, false)) {
        started[tab] = true;
        reqs_["tab"] = tab;
        reqs_["onlyKind"] = "";
        reqs_["implStarted"] = true;
        reqs_["implementing"] = true;
        status_ = "working";
        SaveRequirements();
        return true;
    }
    return reqs_.value("implementing", false) && reqs_.value("tab", "") == tab;
}

// --- Each item -----------------------------------------------------------------------------------------------------

json* GameBuild::AssetFor(const json& req) {
    // The asset a requirement shows: itself, the part it needs, the gateway its guidance points to.
    if (req.value("kind", "") == "asset") {
        for (auto& r : reqs_["items"]) {
            if (r.value("id", "") == req.value("id", "")) return &r;
        }
    }
    const std::string kind = req.value("kind", "");
    const json accept = req.value("accept", json::array());
    const std::string part = accept.empty() ? std::string() : accept[0].value("name", "");
    const std::string place = req.value("where", json::object()).value("place", "");
    for (auto& r : reqs_["items"]) {
        if (r.value("kind", "") != "asset") continue;
        const json& a = r["asset"];
        const json places = a.value("places", json::array());
        const bool there = place.empty() || std::find(places.begin(), places.end(), place) != places.end();
        const json covers = r.value("source", json::object()).value("covers", json::array());
        for (const auto& c : covers) {
            if (c.is_string() && c.get<std::string>() == req.value("id", "")) return &r;
        }
        if (kind == "prop" && there && (Contains(a.value("name", ""), part) || Contains(part, a.value("name", "")))) return &r;
        if (kind == "guidance" && a.value("type", "") == "gateway" && a.value("to", "") == place) return &r;
    }
    return nullptr;
}

bool GameBuild::NeedsDrawing(const json& item, const json& req) const {
    if (item.value("how", "") == "animation") return true;
    const json a = req.value("asset", json::object());
    if (req.value("kind", "") == "asset" && a.value("type", "") == "character") {
        // A character ⑪ didn't draw (plan.art.characters) is drawn now: paid too.
        return std::none_of(art_.begin(), art_.end(), [&](const json& x) { return x.value("subject", "") == a.value("id", "") && x.value("status", "") == "done"; });
    }
    return req.value("kind", "") == "asset" && a.value("type", "") != "character" && a.value("separate", true) && a.value("art", "").empty();
}

void GameBuild::ToPanel(json& item, const json& req, const std::string& problem) {
    // The AI panel builds it, told exactly what and how to tell it's done (and what failed, when it did).
    const json where = req.value("where", json::object());
    std::string sceneFile = where.value("scene", "");
    if (sceneFile.empty() && !where.value("place", "").empty()) {
        if (const Row* row = FindRow(where.value("place", ""))) sceneFile = row->scene;
    }
    std::string request;
    const json source = req.value("source", json::object());
    if (req.value("kind", "") == "system" && source.contains("feature")) {
        // A step of ⑦'s system design: its request as written, the scripts already there.
        const json design = store_.Read("systemdesigns/" + PlanSlug(source.value("feature", ""))).value_or(json::object());
        const json steps = design.value("steps", json::array());
        const int k = source.value("step", 0);
        if (k >= 0 && k < static_cast<int>(steps.size())) {
            request = steps[k].value("request", "");
            const std::string place = steps[k].value("place", "");
            if (!place.empty()) {
                if (const Row* row = FindRow(place)) sceneFile = row->scene;
            }
        }
        for (const auto& sc : design.value("scripts", json::array())) {
            request += " (Already done: " + sc.value("path", "") + " is written and attached to " +
                       sc.value("on", json::object()).value("target", std::string("the game")) + "; don't write it again.)";
        }
    }
    if (request.empty()) {
        std::string who;
        for (const auto& w : req.value("who", json::array())) who += (who.empty() ? "" : ", ") + (w.is_string() ? w.get<std::string>() : std::string());
        request = "Make this true in the game (requirement " + req.value("id", "") + ", " + req.value("kind", "") + "): " + req.value("text", "") +
                  ". How: " + item.value("does", "") + "." + (WhenText(req.value("when", json())).empty() ? "" : " When: " + WhenText(req.value("when", json())) + ".") +
                  (sceneFile.empty() ? "" : " Where: " + sceneFile + (where.value("marker", "").empty() ? "" : ", at the marker " + where.value("marker", "")) +
                                                (where.value("from", "").empty() ? "" : ", coming from " + where.value("from", "")) + ".") +
                  (who.empty() ? "" : " Who: " + who + ".") + " It's done when: " + ChecksText(req.value("accept", json::array())) + ".";
    }
    if (!problem.empty()) request += " It failed before: " + problem + ". Make it hold.";
    // The characters it names are placed already, each once in its own scene (⑪): never a second copy, nor one in another
    // scene (the player is the player; a stage effect or a package takes a character elsewhere).
    {
        std::string placed;
        const std::string lower = Lower(request);
        for (const auto& act : bindings_.value("actors", json::array())) {
            const std::string key = act.value("character", "");
            const std::string name = Str(store_.Read("characters/" + PlanSlug(key)).value_or(json::object()), "name");
            const bool named = (!name.empty() && Contains(request, name)) || (!key.empty() && lower.find(Lower(key)) != std::string::npos);
            if (!named) continue;
            std::string player = "is the player";
            if (act.value("player", false)) {
                // Where the player is to be found: one entity, in the scene the game starts in (it goes with the player
                // to the others, so it isn't in them), by its name there and its prefab.
                Registry reg;
                const std::string scene = act.value("scene", "");
                if (!scene.empty() && LoadScene(reg, Project{host_.projectRoot}.Resolve(scene).string())) {
                    if (const auto e = FindPlayer(reg)) {
                        const auto* n = reg.try_get<Name>(*e);
                        const auto* p = reg.try_get<PrefabInstance>(*e);
                        player += ": the entity named \"" + (n ? n->value : std::string("?")) + "\" in " + scene +
                                  (p && !p->path.empty() ? " (its prefab " + p->path + ")" : std::string()) +
                                  ", the only player; it travels with the player into the other scenes, so it isn't in them. "
                                  "Change the player there or through its prefab";
                    }
                }
            }
            placed += (placed.empty() ? "" : "; ") + (name.empty() ? key : name) + " (" + key + ") " +
                      (act.value("player", false) ? player : "stands in " + act.value("scene", "") +
                                                             (act.value("marker", "").empty() ? "" : " at " + act.value("marker", "")));
        }
        if (!placed.empty()) {
            request += " Already placed, once each: " + placed + ". Don't add another copy of any of them or put one in another scene; "
                       "change the one that's there (its prefab prefabs/characters/<name>.prefab), and bring one elsewhere with a stage effect "
                       "(move_actor) or a package.";
        }
    }
    if (req.value("kind", "") == "character-state") {
        request += " This is the cast timeline's (the plan's ⑥b): decide how to make it so, and do it: a stage effect in the quest "
                   "(move_actor to the scene and marker, set_hostile, set_friendly, follow_player, stop_following, hide_actor, show_actor: "
                   "the quest's stage effects are yours to add, not its objectives), an AI package that starts at this stage (its "
                   "conditions on the quest stage; walking them from where they are to where they go), dialogue nodes offered from "
                   "this stage (their conditions), or a Lua script when nothing else can. Look at where they are now first.";
    }
    request += " (The quests in quests/ are built from the story and already have their stages, objectives and targets: don't create quests, "
               "add or change their stages or objectives, or set their stage; react to their stages and events.)";
    const std::string id = "impl-" + item.value("id", "");
    features_.erase(std::remove_if(features_.begin(), features_.end(), [&](const json& f) { return f.value("id", "") == id; }), features_.end());
    std::string name = req.value("text", "");
    if (name.size() > 80) name = Utf8Cut(name, 77) + "…";
    features_.push_back({{"id", id}, {"name", name}, {"place", where.value("place", "")}, {"scene", sceneFile}, {"request", request},
                         {"status", "queued"}, {"error", ""}, {"item", item.value("id", "")}, {"tab", TabOf(item, req)}});
    item["status"] = "building";
    item["feature"] = id;
    item["by"] = "ai-panel";
}

void GameBuild::ImplementItem(json& item, json& req) {
    const std::string how = item.value("how", ""), by = item.value("by", ""), problem = item.value("problem", "");
    auto built = [&](const std::string& note, json made = json::array()) {
        item["status"] = "built";
        item["note"] = note;
        item["made"] = made;
    };
    auto failed = [&](const std::string& why) {
        item["status"] = "failed";
        item["error"] = why;
    };
    // Where a character is from a time on (the placements' whereabouts): a stage effect by rule, else the AI panel.
    if (req.value("kind", "") == "whereabouts" && problem.empty()) {
        ImplementWhereabouts(item, req);
        return;
    }
    // A character's state (the cast timeline): there already (the quests' effects, their packages, their dialogue), else
    // the AI panel decides how to make it so.
    if (req.value("kind", "") == "character-state" && problem.empty()) {
        for (const auto& a : req.value("accept", json::array())) {
            if (a.value("check", "") != item.value("check", "")) continue;
            const json c = TimelineStatic(a);
            if (c.value("ok", json()) == json(false)) {
                ToPanel(item, req, c.value("why", ""));
                return;
            }
            built(c.value("ok", json()) == json(true) ? "the files have it" : "there: whether it holds from this stage, a test tells");
            return;
        }
    }
    // What the AI panel builds: scripts, dialogue, and anything asked again after it failed.
    if (by == "ai-panel" || how == "script" || how == "dialogue" || (!problem.empty() && by != "art" && how != "cutscene")) {
        ToPanel(item, req, problem);
        return;
    }
    if (how == "art") {
        ImplementArt(item, req);
        return;
    }
    if (how == "animation") {
        ImplementAnimation(item, req);
        return;
    }
    if (how == "marker") {
        // The requirement's marker in its scene: there, or made where the thing it names stands ("beneath the cracked grave
        // marker": that entity), else at the marker's own name without its quest ("hidden-box"); the rest go to the AI panel.
        const json where = req.value("where", json::object());
        const std::string sceneFile = where.value("scene", ""), marker = where.value("marker", "");
        Registry reg;
        const std::string path = Project{host_.projectRoot}.Resolve(sceneFile).string();
        if (marker.empty() || sceneFile.empty() || !LoadScene(reg, path)) {
            ToPanel(item, req, problem);
            return;
        }
        if (MarkerIn(reg, marker)) {
            built("the marker is in " + sceneFile);
            return;
        }
        const std::string text = Lower(item.value("does", "") + " " + req.value("text", ""));
        const std::string bare = marker.find('/') != std::string::npos ? marker.substr(marker.rfind('/') + 1) : marker;
        std::optional<std::pair<float, float>> at = MarkerIn(reg, bare);
        std::string by = at ? "the marker " + bare : std::string();
        size_t best = 0;
        if (!at) {
            for (auto [e, n, w] : reg.view<Name, WorldTransform2D>().each()) {
                const std::string name = Lower(n.value);
                if (name.size() < 4 || name.size() <= best || reg.all_of<Marker>(e) || text.find(name) == std::string::npos) continue;
                best = name.size(), at = std::make_pair(w.x, w.y), by = n.value;
            }
        }
        if (!at) {
            ToPanel(item, req, "the marker " + marker + " isn't in " + sceneFile + " and nothing there is named for it: add it where the requirement says");
            return;
        }
        const Entity e = reg.create();
        reg.emplace<WorldTransform2D>(e, at->first, at->second);
        reg.emplace<Marker>(e, marker);
        reg.emplace<Name>(e, "Marker " + marker);
        SaveScene(reg, path);
        built("the marker " + marker + " made at " + by, json::array({sceneFile}));
        return;
    }
    if (how == "prefab") {
        json* asset = AssetFor(req);
        if (!asset) {
            // Nothing drawn for it: what the place shows will do if the scene has it, else the AI panel makes it.
            const json checks = StaticChecks(req);
            if (std::none_of(checks.begin(), checks.end(), [](const json& c) { return c.value("ok", json()) == json(false); })) built("the place has it");
            else ToPanel(item, req, problem);
            return;
        }
        PlaceAsset(item, *asset, req);
        return;
    }
    if (how == "cutscene") {
        AskCutscene(item, req);
        return;
    }
    if (how == "trigger") {
        // A cutscene's trigger is placed with it.
        for (const auto& o : reqs_["implementations"]) {
            if (o.value("req", "") != req.value("id", "") || o.value("how", "") != "cutscene") continue;
            if (o.value("status", "") == "built") built("placed with its cutscene", o.value("made", json::array()));
            else failed("its cutscene wasn't made");
            return;
        }
        ToPanel(item, req, problem);
        return;
    }
    if (how == "guidance") {
        const std::string does = Lower(item.value("does", ""));
        if (does.find("journal") != std::string::npos) {
            NameThePlace(item, req);
            return;
        }
        if (does.find("marker") != std::string::npos) {
            if (!ActiveSettings().story.objectiveMarkers) {
                failed("objective markers are off (Project Settings ▸ Story)");
                return;
            }
            const json checks = StaticChecks(req);
            for (const auto& c : checks) {
                if (c.value("check", "") == "objective-marker" && c.value("ok", json()) == json(false)) {
                    failed(c.value("why", ""));
                    return;
                }
            }
            built("objective markers on; the stage's objective has its target");
            return;
        }
    }
    // By rule: ⑪ and ⑫'s writing made it (the characters placed, the quests' objectives and stage effects, the markers);
    // it holds when its requirement's checks do, else the AI panel makes it.
    const json checks = StaticChecks(req);
    for (const auto& c : checks) {
        if (c.value("ok", json()) == json(false)) {
            ToPanel(item, req, c.value("why", ""));
            return;
        }
    }
    built("made by ⑪ and ⑫'s writing; its checks hold");
}

void GameBuild::ImplementArt(json& item, json& req) {
    const json a = req.value("asset", json::object());
    const std::string type = a.value("type", ""), id = a.value("id", "");
    if (req.value("kind", "") != "asset") {
        item["status"] = "built", item["note"] = "nothing to draw";
        return;
    }
    if (type == "character") {
        // ⑪ draws the characters; one it didn't (plan.art.characters) is drawn now.
        auto entry = [&]() -> const json* {
            for (const auto& x : art_) {
                if (x.value("subject", "") == id) return &x;
            }
            return nullptr;
        };
        if (!entry()) QueueArt(true);
        if (!entry()) {
            item["status"] = "failed", item["error"] = "it has no appearance to draw from";
            return;
        }
        artApproved_ = true;
        item["status"] = "building";
        item["art"] = id;
        return;
    }
    if (!a.value("separate", true)) {
        bool placeBuilt = false;
        for (const auto& p : a.value("places", json::array())) {
            if (const Row* row = p.is_string() ? FindRow(p.get<std::string>()) : nullptr) placeBuilt = placeBuilt || row->status == "built";
        }
        item["status"] = placeBuilt ? "built" : "failed";
        if (placeBuilt) item["note"] = "painted with its place";
        else item["error"] = "its place isn't built";
        return;
    }
    if (!a.value("art", "").empty() || !a.value("sheet", "").empty()) {
        item["status"] = "built", item["made"] = json::array({a.value("art", a.value("sheet", ""))}), item["note"] = "drawn already";
        return;
    }
    if (!host_.request) {
        item["status"] = "failed", item["error"] = "the AI service isn't running";
        return;
    }
    const json brief = store_.Read("brief").value_or(json::object());
    const json style = BriefGet(brief, "artStyle.style");
    const std::string styled = style.is_string() ? " In a " + style.get<std::string>() + " style." : std::string();
    const std::string subject = "asset:" + id;
    art_.erase(std::remove_if(art_.begin(), art_.end(), [&](const json& x) { return x.value("subject", "") == subject && x.value("status", "") == "failed"; }), art_.end());
    if (type == "creature") {
        // Drawn as a character is, and animated doing what the story has it do.
        json actions = json::array({{{"action", "walking"}}});
        for (const auto& r : reqs_["items"]) {
            const json an = r.value("animation", json::object());
            if (r.value("kind", "") == "animation" && an.value("who", "") == subject) actions.push_back({{"action", an.value("action", "")}});
        }
        const bool iso = ActiveSettings().grid.style != GridStyle::TopDown;
        art_.push_back({{"subject", subject}, {"name", AssetSlug(a.value("name", id))}, {"description", a.value("name", id) + ": " + a.value("look", "") + styled},
                        {"target", ""}, {"directions", iso ? 8 : 4}, {"isometric", iso}, {"actions", actions}, {"sheet", ""}, {"next", 0},
                        {"status", "queued"}, {"error", ""}});
    } else {
        const std::string alone = type == "item" ? " Alone, whole, on a plain background, as a game item." : " One thing standing alone on the ground.";
        art_.push_back({{"subject", subject}, {"name", AssetSlug(a.value("name", id))}, {"kind", "asset"},
                        {"description", a.value("name", id) + ": " + a.value("look", "") + styled + alone},
                        {"height", type == "item" ? 32 : static_cast<int>(HeightOf(a.value("size", "medium")))}, {"actions", json::array()},
                        {"status", "queued"}, {"error", ""}});
    }
    artApproved_ = true;
    item["status"] = "building";
    item["art"] = subject;
}

json GameBuild::AnimationOf(const json& item, const json& req) const {
    // Its requirement's animation, else the item's own (one the AI planned for a prop or a quest step: "Draw the east gate
    // swinging open"). A plain name ("graveyard spirits") is the asset or the character it names.
    json an = item.value("animation", json::object());
    if (an.empty()) an = req.value("animation", json::object());
    auto norm = [](std::string s) {
        s = Lower(s);
        for (char& c : s) c = c == '-' || c == '_' || c == '/' ? ' ' : c;
        return s;
    };
    const json items = reqs_.value("items", json::array());
    // The asset this names (exactly, or within: the longest name found in the text).
    auto assetNamed = [&](const std::string& text, bool within) -> std::string {
        std::string best;
        size_t bestLen = 0;
        for (const auto& r : items) {
            if (Str(r, "kind") != "asset") continue;
            const json a = r.value("asset", json::object());
            for (const std::string& n : {Str(a, "id"), Str(a, "name")}) {
                if (n.size() < 3) continue;
                const bool hit = within ? norm(text).find(norm(n)) != std::string::npos : norm(text) == norm(n);
                if (hit && n.size() > bestLen) best = "asset:" + Str(a, "id"), bestLen = n.size();
            }
        }
        return best;
    };
    auto characterNamed = [&](const std::string& text) -> std::string {
        for (const auto& c : store_.Read("outline").value_or(json::object()).value("cast", json::array())) {
            std::string id = Str(c, "id");
            if (id.empty()) continue;
            if (id.rfind("character/", 0) != 0) id = "character/" + id;
            const std::string name = Str(store_.Read("characters/" + PlanSlug(id)).value_or(json::object()), "name");
            if (norm(text) == norm(PlanSlug(id)) || (!name.empty() && norm(text) == norm(name)) || norm(text) == norm(Str(c, "name"))) return id;
        }
        return {};
    };
    std::string who = Str(an, "who"), action = Str(an, "action");
    if (!who.empty() && who.rfind("asset:", 0) != 0 && who.rfind("character/", 0) != 0 && who.rfind("class:", 0) != 0) {
        std::string found = assetNamed(who, false);
        if (found.empty()) found = characterNamed(who);
        if (found.empty()) found = assetNamed(who, true);
        if (found.empty()) return {{"add", who}, {"action", action}, {"name", who}};
        an["name"] = Str(an, "name").empty() ? who : Str(an, "name");
        who = found;
    }
    if (who.empty()) {
        // Planned for a prop or a step: the thing it names (the requirement's prop, else its marker's words).
        const json accept = req.value("accept", json::array());
        std::string subject = accept.empty() ? std::string() : Str(accept[0], "name");
        if (subject.empty()) subject = norm(PlanSlug(Str(req.value("where", json::object()), "marker")));
        std::string does = item.value("does", "");
        for (const std::string lead : {"Draw ", "Animate ", "draw ", "animate "}) {
            if (does.rfind(lead, 0) == 0) does = does.substr(lead.size());
        }
        while (!does.empty() && (does.back() == '.' || does.back() == ' ')) does.pop_back();
        // Doing what: the words after its name ("swinging open as Ashen forces it"), else all of it.
        action = does;
        if (const size_t at = norm(does).find(norm(subject)); !subject.empty() && at != std::string::npos && does.size() - (at + subject.size()) >= 8) {
            action = does.substr(at + subject.size());
            while (!action.empty() && action.front() == ' ') action.erase(0, 1);
        }
        action = Utf8Cut(action, 120);
        who = assetNamed(item.value("does", ""), true);
        if (who.empty() && !subject.empty()) who = assetNamed(subject, false);
        if (who.empty()) return subject.empty() ? json{{"action", action}} : json{{"add", subject}, {"action", action}, {"name", subject}};
        an["name"] = subject;
    }
    return {{"who", who}, {"action", action}, {"name", Str(an, "name").empty() ? who : Str(an, "name")}, {"type", Str(an, "type")}};
}

bool GameBuild::AddAnimatedAssets() {
    // An animation of something nothing drawn is (the east gate the place painted, the binding-stones): an asset of it,
    // drawn, placed at the requirement's marker, then animated (the item comes after it).
    if (!reqs_.contains("implementations") || !reqs_["implementations"].is_array()) return false;
    std::vector<std::pair<json, json>> add; // requirement, its items
    for (auto& i : reqs_["implementations"]) {
        if (!i.contains("addAsset")) continue;
        const json want = i["addAsset"];
        i.erase("addAsset");
        const std::string name = Str(want, "name"), id = AssetSlug(name), rid = "v-" + id;
        if (id.empty()) continue;
        i["animation"] = {{"who", "asset:" + id}, {"action", Str(want, "action")}, {"name", name}};
        if (!i["after"].is_array()) i["after"] = json::array();
        if (std::find(i["after"].begin(), i["after"].end(), json(rid)) == i["after"].end()) i["after"].push_back(rid);
        i["status"] = "todo";
        i.erase("error");
        const bool known = std::any_of(reqs_["items"].begin(), reqs_["items"].end(), [&](const json& r) { return Str(r, "id") == rid; }) ||
                           std::any_of(add.begin(), add.end(), [&](const auto& p) { return Str(p.first, "id") == rid; });
        if (known) continue;
        std::string place = Str(want, "place");
        const json places = place.empty() ? json::array() : json::array({place});
        const json asset = {{"id", id}, {"name", name}, {"type", "prop"}, {"look", Str(want, "look")}, {"separate", true},
                            {"places", places}, {"animations", json::array({Str(want, "action")})}};
        json accept = json::array({{{"check", "asset"}, {"name", name}}});
        if (!place.empty()) accept.push_back({{"check", "placed"}, {"name", name}, {"place", place}});
        json req = {{"id", rid}, {"scene", Str(want, "scene")}, {"act", Str(want, "act")}, {"kind", "asset"}, {"by", "ai"}, {"status", "todo"},
                    {"text", name + ": " + Str(want, "look")}, {"scenes", json::array({Str(want, "scene")})}, {"when", json::object()},
                    {"where", {{"place", place}, {"marker", Str(want, "marker")}}}, {"who", json::array()},
                    {"source", {{"covers", json::array({Str(want, "req")})}, {"animation", i.value("id", "")}}}, {"asset", asset}, {"accept", accept}};
        json items = json::array();
        items.push_back({{"id", "i-" + rid + "-art"}, {"req", rid}, {"how", "art"}, {"does", name + " drawn: " + Str(want, "look")}, {"by", "art"},
                         {"after", json::array()}, {"status", "todo"}, {"made", json::array()}});
        if (!place.empty()) {
            items.push_back({{"id", "i-" + rid + "-prefab"}, {"req", rid}, {"how", "prefab"}, {"does", name + " made a prefab from its art and placed in " + place},
                             {"by", "rule"}, {"after", json::array()}, {"status", "todo"}, {"made", json::array()}});
        }
        add.push_back({req, items});
    }
    for (auto& [req, items] : add) {
        reqs_["items"].push_back(req);
        for (auto& x : items) reqs_["implementations"].push_back(x);
    }
    return !add.empty();
}

void GameBuild::ImplementAnimation(json& item, json& req) {
    const json an = AnimationOf(item, req);
    if (an.contains("add")) {
        // Nothing drawn is it yet: an asset of it first (added before the next turn: the lists aren't grown while walked).
        std::string place = Str(req.value("where", json::object()), "place");
        const std::string sceneFile = Str(req.value("where", json::object()), "scene");
        for (const auto& r : rows_) {
            if (place.empty() && !sceneFile.empty() && r.scene == sceneFile) place = r.place;
        }
        item["addAsset"] = {{"name", Str(an, "add")}, {"action", Str(an, "action")}, {"place", place}, {"marker", Str(req.value("where", json::object()), "marker")},
                            {"look", req.value("text", "")}, {"scene", req.value("scene", "")}, {"act", req.value("act", "")}, {"req", req.value("id", "")}};
        item["status"] = "todo";
        item["note"] = Str(an, "add") + " is drawn and placed first";
        return;
    }
    const std::string who = an.value("who", ""), action = an.value("action", "");
    if (who.empty()) {
        item["status"] = "failed", item["error"] = "it doesn't say what it animates";
        return;
    }
    if (who.rfind("asset:", 0) == 0) {
        // After its asset is drawn (and placed): named plainly, it wasn't put after it when planned.
        const std::string rid = "v-" + who.substr(6);
        const bool known = std::any_of(reqs_["items"].begin(), reqs_["items"].end(), [&](const json& r) { return Str(r, "id") == rid; });
        if (!item["after"].is_array()) item["after"] = json::array();
        if (known && rid != req.value("id", "") && std::find(item["after"].begin(), item["after"].end(), json(rid)) == item["after"].end()) {
            item["after"].push_back(rid);
            item["status"] = "todo";
            return;
        }
    }
    if (who.rfind("asset:", 0) == 0 && an.value("type", "") != "creature") {
        // A thing (a gate, a grave, a glow): animated from its own picture (the thing's animator, not a character's).
        const json* asset = nullptr;
        for (const auto& r : reqs_["items"]) {
            if (r.value("kind", "") == "asset" && "asset:" + r.value("asset", json::object()).value("id", "") == who) asset = &r;
        }
        const json a = asset ? asset->value("asset", json::object()) : json::object();
        if (a.value("type", "") == "creature") {
            // drawn as a character: on its sheet, below
        } else if (a.value("art", "").empty()) {
            item["status"] = "failed", item["error"] = "its picture wasn't drawn (" + an.value("name", who) + ")";
            return;
        } else if (!host_.request) {
            item["status"] = "failed", item["error"] = "the AI service isn't running";
            return;
        } else {
            json body = {{"path", a.value("art", "")}, {"action", action}, {"thing", true},
                         {"description", a.value("name", "") + ": " + a.value("look", "")},
                         {"name", AssetSlug(a.value("name", "") + "-" + action)}};
            if (host_.animateModel) body["provider"] = host_.animateModel();
            if (host_.objectAnimateModel) body["objectProvider"] = host_.objectAnimateModel();
            const int id = host_.request("/animations/generate", body, 900);
            if (id == 0) {
                item["status"] = "failed", item["error"] = "the AI service isn't running";
                return;
            }
            pending_[id] = "impl:" + item.value("id", "");
            item["status"] = "building";
            item["action"] = action;
            message_ = "⑫ Animating " + a.value("name", "") + ": " + action + "…";
            return;
        }
    }
    auto entry = [&]() -> json* {
        for (auto& x : art_) {
            if (x.value("subject", "") == who) return &x;
        }
        return nullptr;
    };
    if (!entry() && who.rfind("character/", 0) == 0) QueueArt(true);
    json* a = entry();
    if (!a) {
        item["status"] = "failed", item["error"] = "nothing is drawn to animate (" + who + ")";
        return;
    }
    json& actions = (*a)["actions"];
    if (!actions.is_array()) actions = json::array();
    const bool has = std::any_of(actions.begin(), actions.end(), [&](const json& x) { return x.value("action", "") == action; });
    if (!has) {
        actions.push_back({{"action", action}});
        // Drawn already: its new clip added to its sheet, then onto its prefab again.
        if ((*a).value("status", "") == "done" || ((*a).value("status", "") == "failed" && !(*a).value("sheet", "").empty())) (*a)["status"] = "queued";
    } else if ((*a).value("status", "") == "failed") {
        (*a)["status"] = "queued", (*a)["error"] = ""; // asked again (it failed: no credits, the service down)
    }
    artApproved_ = true;
    item["status"] = "building";
    item["art"] = who;
    item["action"] = action;
}

void GameBuild::PlaceAsset(json& item, json& asset, const json& req) {
    const Project project{host_.projectRoot};
    const json a = asset.value("asset", json::object());
    const std::string type = a.value("type", ""), name = a.value("name", "");
    if (!a.value("separate", true)) {
        item["status"] = "built", item["note"] = "painted with its place";
        return;
    }
    if (type == "character") {
        item["status"] = "built", item["note"] = "⑪ placed it";
        return;
    }
    if (type == "creature") {
        ToPanel(item, req, std::string("make an enemy prefab for ") + name + " drawn with the sheet " + a.value("sheet", "") +
                               ", and spawn it where and when the requirement says");
        return;
    }
    const std::string art = a.value("art", "");
    if (art.empty()) {
        item["status"] = "failed", item["error"] = "its art wasn't drawn";
        return;
    }
    const float height = HeightOf(a.value("size", "medium"));
    json made = json::array();
    std::string why;
    if (type == "gateway") {
        // The way from one place into another: its art stands on the gateway that leads there (34.6: seen, not a bare tile).
        const Row* from = nullptr;
        for (const auto& p : a.value("places", json::array())) from = from ? from : (p.is_string() ? FindRow(p.get<std::string>()) : nullptr);
        const Row* to = FindRow(a.value("to", ""));
        if (!from || !to) {
            item["status"] = "failed", item["error"] = "it doesn't say which places it joins";
            return;
        }
        Registry reg;
        const std::string path = project.Resolve(from->scene).string();
        if (!LoadScene(reg, path)) {
            item["status"] = "failed", item["error"] = "can't read " + from->scene;
            return;
        }
        int dressed = 0;
        for (auto [e, gw] : reg.view<Gateway>().each()) {
            if (gw.scene != to->scene) continue;
            StandArt(reg, e, art, height, project.root);
            reg.emplace_or_replace<Name>(e, "Gateway to " + to->brief.value("name", PlanSlug(to->place)) + " (" + name + ")");
            ++dressed;
        }
        if (dressed == 0) {
            item["status"] = "failed", item["error"] = "no gateway from " + from->scene + " leads to " + to->scene;
            return;
        }
        SaveScene(reg, path);
        // Kept for when the scenes are made again (⑪ again stands it on its gateway).
        json settled = ReadJson(store_.Dir() / "gateways.json");
        if (!settled.is_object()) settled = json::object();
        json& entry = settled[from->scene + ">" + to->scene];
        if (!entry.is_object()) entry = json::object();
        entry["art"] = art;
        entry["height"] = height;
        std::ofstream(store_.Dir() / "gateways.json", std::ios::binary) << settled.dump(1);
        item["status"] = "built", item["made"] = json::array({from->scene}), item["note"] = "stood on its gateway";
        return;
    }
    // A prop: stood at its marker in each of its places (the one already there is kept).
    const std::string slug = PlanSlug(AssetSlug(name));
    for (const auto& p : a.value("places", json::array())) {
        const Row* row = p.is_string() ? FindRow(p.get<std::string>()) : nullptr;
        if (!row) continue;
        Registry reg;
        const std::string path = project.Resolve(row->scene).string();
        if (!LoadScene(reg, path)) {
            why = "can't read " + row->scene;
            continue;
        }
        bool there = false;
        std::vector<Entity> same;
        for (auto [e, n] : reg.view<Name>().each()) {
            if (!reg.all_of<Marker>(e) && AssetSlug(n.value) == AssetSlug(name)) same.push_back(e); // Record Table is record-table
        }
        for (const Entity e : same) StandArt(reg, e, art, height, project.root), there = true;
        if (!there) {
            // Where: the requirement's marker, a marker named for it, the story's marker for its scene, else where one arrives.
            std::optional<std::pair<float, float>> at;
            const json where = req.value("where", json::object());
            // Where the placements put it (34.11: decided with the characters, by its story scene and its words).
            for (const auto& p : ReadPlacements(host_.projectRoot).value("props", json::array())) {
                const json spot = p.value("spot", json::object());
                const bool ours = p.value("id", "").rfind("asset:", 0) == 0 && Lower(p.value("name", "")) == Lower(name);
                if (!at && ours && spot.value("scene", "") == row->scene && spot.contains("x")) {
                    at = std::make_pair(spot.value("x", 0) + 0.5f, spot.value("y", 0) + 0.5f);
                }
            }
            for (const std::string m : {where.value("marker", std::string()), slug, "story/" + req.value("scene", "")}) {
                if (m.empty() || at) continue;
                at = MarkerIn(reg, m);
                if (!at) {
                    for (auto [e, mk, w] : reg.view<Marker, WorldTransform2D>().each()) {
                        if (!at && (Contains(mk.name, slug) || (m.rfind("story/", 0) == 0 && mk.name.rfind(m, 0) == 0))) at = std::make_pair(w.x + 1.0f, w.y);
                    }
                }
            }
            if (!at) at = MarkerIn(reg, "arrive");
            if (!at) at = std::make_pair(5.5f, 5.5f);
            const Entity e = reg.create();
            reg.emplace<Name>(e, name);
            reg.emplace<WorldTransform2D>(e, at->first, at->second);
            StandArt(reg, e, art, height, project.root);
        }
        SaveScene(reg, path);
        made.push_back(row->scene);
    }
    if (made.empty()) {
        item["status"] = "failed", item["error"] = why.empty() ? "none of its places is built" : why;
        return;
    }
    AnimateObjects(a);
    item["status"] = "built", item["made"] = made;
}

void GameBuild::ObjectAnimated(json& item, const json& req, const json& body) {
    // Its frames as an aige.anim/1 sheet beside them (one clip: idle loops; open, close, break play once and hold, the
    // other of open/close its frames backwards), kept on its asset and put on its placed copies.
    const Project project{host_.projectRoot};
    const std::string texture = body.value("sheet", "");
    const int frames = static_cast<int>(body.value("frames", json::array()).size());
    const auto [w, h] = PngSize(project.root / texture);
    if (texture.empty() || frames <= 0 || w <= 0) {
        item["status"] = "failed", item["error"] = "no frames came back";
        return;
    }
    const std::string clip = body.value("clip", std::string("idle"));
    json sheet = AnimSheetFromGrid(texture, w / frames, h, frames, {clip}, body.value("fps", 8.0f));
    const std::string other = clip == "open" ? "close" : clip == "close" ? "open" : "";
    if (!other.empty() && sheet["clips"].contains(clip)) {
        json reversed = sheet["clips"][clip];
        if (reversed.contains("frames") && reversed["frames"].is_array()) std::reverse(reversed["frames"].begin(), reversed["frames"].end());
        sheet["clips"][clip]["loop"] = "once";
        reversed["loop"] = "once";
        sheet["clips"][other] = reversed;
    } else if (clip == "break") {
        sheet["clips"][clip]["loop"] = "once";
    }
    const fs::path anim = fs::path(texture).parent_path() / (fs::path(texture).stem().string() + ".anim.json");
    std::ofstream(project.Resolve(anim.generic_string()), std::ios::binary) << sheet.dump(2) << "\n";
    files_[anim.generic_string()] = {{"planner", req.value("id", "")}};
    // Kept on its asset: each action's sheet and clip.
    const std::string who = AnimationOf(item, req).value("who", "");
    for (auto& r : reqs_["items"]) {
        if (r.value("kind", "") != "asset" || "asset:" + r.value("asset", json::object()).value("id", "") != who) continue;
        r["asset"]["sheets"][item.value("action", "")] = {{"sheet", anim.generic_string()}, {"clip", clip}};
        AnimateObjects(r["asset"]);
    }
    item["status"] = "built";
    item["made"] = json::array({anim.generic_string()});
}

void GameBuild::AnimateObjects(const json& asset) {
    // Its placed copies play its animation: the one that loops (a glow, a flag) first, else the first drawn (a gate
    // that opens: its clip played when it's used, or by a cutscene's animate).
    const json sheets = asset.value("sheets", json::object());
    if (sheets.empty() || !asset.value("separate", true)) return;
    std::string sheet, clip;
    for (const auto& [action, s] : sheets.items()) {
        if (sheet.empty() || (s.value("clip", "") == "idle" && clip != "idle")) sheet = s.value("sheet", ""), clip = s.value("clip", "");
    }
    const Project project{host_.projectRoot};
    for (const auto& p : asset.value("places", json::array())) {
        const Row* row = p.is_string() ? FindRow(p.get<std::string>()) : nullptr;
        if (!row) continue;
        Registry reg;
        const std::string path = project.Resolve(row->scene).string();
        if (!LoadScene(reg, path)) continue;
        bool changed = false;
        for (auto [e, n] : reg.view<Name>().each()) {
            if (reg.all_of<Marker>(e) || Lower(n.value) != Lower(asset.value("name", ""))) continue;
            auto& animator = reg.get_or_emplace<Animator>(e);
            animator.sheet = sheet;
            animator.animator = "prop";
            changed = true;
        }
        if (changed) SaveScene(reg, path);
    }
}

void GameBuild::AskCutscene(json& item, const json& req) {
    // The cutscene's tracks written by the AI from the scene (its beats) and what the requirement says it shows.
    const std::string scene = req.value("scene", "");
    json traced = json::object();
    for (const auto& t : check_.value("trace", json::array()).empty() ? TraceScenes() : check_.value("trace", json::array())) {
        if (t.value("scene", "") == scene) traced = t;
    }
    const std::string sceneFile = req.value("where", json::object()).value("scene", traced.value("locationScene", ""));
    const std::string id = !req.value("cutsceneId", "").empty() ? req.value("cutsceneId", "")
                           : req.value("by", "") == "rule" ? scene : scene + "-" + PlanSlug(req.value("id", ""));
    if (!host_.request) {
        item["status"] = "failed", item["error"] = "the AI service isn't running";
        return;
    }
    const json doc = store_.Read("story/scenes/" + scene).value_or(json::object());
    json cast = json::array();
    for (const auto& c : traced.value("cast", json::array())) cast.push_back({{"key", c.value("who", "")}, {"name", c.value("name", "")}, {"player", c.value("player", false)}});
    json stage = json();
    const json when = req.value("when", json::object());
    if (when.contains("quest")) stage = {{"quest", when.value("quest", "")}, {"stage", when.value("stage", 0)}};
    const json body = {{"label", "Cutscene " + id},
                       {"scene", {{"id", scene}, {"goal", traced.value("goal", "")}, {"card", doc.value("card", json::object())},
                                  {"beats", doc.value("beats", json::array())}, {"events", doc.value("events", json::array())}}},
                       {"moment", req.contains("cutscene") ? req["cutscene"].dump() : req.value("by", "") == "rule" ? std::string("the whole scene") : req.value("text", "")},
                       {"does", item.value("does", "")},
                       {"cast", cast},
                       {"markers", SceneFacts(sceneFile).value("markers", json::array())},
                       {"stage", stage},
                       {"problems", item.value("problem", "").empty() ? json::array() : json::array({item.value("problem", "")})}};
    const int rid = host_.request("/planner/cutscene", body, 300);
    if (rid == 0) {
        item["status"] = "failed", item["error"] = "the AI service isn't running";
        return;
    }
    pending_[rid] = "impl:" + item.value("id", "");
    item["status"] = "building";
    item["cutscene"] = id;
    message_ = "⑫ Writing the cutscene " + id + "…";
}

void GameBuild::WriteCutsceneItem(json& item, const json& req, json cutscene) {
    const Project project{host_.projectRoot};
    const std::string id = item.value("cutscene", req.value("scene", ""));
    cutscene["madeBy"] = "implement"; // ⑫'s writing keeps it (it doesn't put its own from the beats over it)
    std::string error;
    const auto parsed = CutsceneFromJson(cutscene, id, &error);
    if (!parsed) {
        item["status"] = "failed", item["error"] = "the cutscene doesn't read: " + error;
        return;
    }
    if (const auto problems = ValidateCutscene(*parsed); !problems.empty()) {
        item["status"] = "failed", item["error"] = problems.front();
        return;
    }
    if (const auto failed = WriteCutsceneFile(host_.projectRoot, id, cutscene)) {
        item["status"] = "failed", item["error"] = *failed;
        return;
    }
    files_["cutscenes/" + id + ".json"] = {{"planner", req.value("id", "")}, {"decision", "implement"}};
    // A planned cutscene (Life ▸ Cutscenes) starts exactly as planned.
    if (const json planned = req.value("cutscene", json::object()); planned.is_object() && !planned.empty()) {
        const json where = req.value("where", json::object());
        std::string sceneFile = where.value("scene", "");
        if (sceneFile.empty()) {
            if (const Row* row = FindRow(Str(planned, "place"))) sceneFile = row->scene;
        }
        json made = json::array({"cutscenes/" + id + ".json"});
        std::string said;
        if (const std::string why = PlaceCutsceneStart(id, planned, sceneFile, made, said); !why.empty()) {
            item["status"] = "failed", item["error"] = "its cutscene is written, but it can't start " + said + ": " + why;
            return;
        }
        item["status"] = "built";
        item["made"] = made;
        item["starts"] = said;
        return;
    }
    // Its trigger, where it happens: on the way in from where it says it comes from, at its marker, at the scene's story
    // marker, else where one arrives; while its quest stage is on.
    json traced = json::object();
    for (const auto& t : check_.value("trace", json::array()).empty() ? TraceScenes() : check_.value("trace", json::array())) {
        if (t.value("scene", "") == req.value("scene", "")) traced = t;
    }
    const json where = req.value("where", json::object());
    const std::string sceneFile = where.value("scene", traced.value("locationScene", ""));
    Registry reg;
    const std::string path = project.Resolve(sceneFile).string();
    if (sceneFile.empty() || !LoadScene(reg, path)) {
        item["status"] = "failed", item["error"] = "its cutscene is written but its scene (" + sceneFile + ") can't be read for its trigger";
        return;
    }
    std::optional<std::pair<float, float>> at;
    // Where it's said to come from, or the place it plays on leaving ("after leaving the apothecary": where one arrives
    // coming out of it).
    auto placeSlug = [](std::string p) {
        if (p.rfind("scenes/", 0) == 0) p = p.substr(7);
        if (p.size() > 5 && p.compare(p.size() - 5, 5, ".aige") == 0) p = p.substr(0, p.size() - 5);
        return PlanSlug(p);
    };
    std::string from = where.value("from", "");
    const json when = req.value("when", json::object());
    if (from.empty() && when.contains("leave") && when["leave"].is_string()) from = when["leave"].get<std::string>();
    for (const std::string m : {from.empty() ? std::string() : "from-" + placeSlug(from), from, where.value("marker", std::string())}) {
        if (!m.empty() && !at) at = MarkerIn(reg, m);
    }
    if (!at) {
        for (auto [e, mk, w] : reg.view<Marker, WorldTransform2D>().each()) {
            if (!at && mk.name.rfind("story/" + req.value("scene", "") + "/", 0) == 0) at = std::make_pair(w.x, w.y);
        }
    }
    if (!at) at = MarkerIn(reg, "arrive");
    if (!at) at = std::make_pair(5.5f, 5.5f);
    std::vector<Entity> old;
    for (auto [e, t] : reg.view<CutsceneTrigger>().each()) {
        if (t.cutscene == id) old.push_back(e);
    }
    for (const Entity e : old) reg.destroy(e);
    json conditions = json::array();
    if (when.contains("quest")) conditions.push_back({{"quest", when.value("quest", "")}, {"is", "active"}, {"stage", when.value("stage", 0)}});
    const Entity e = reg.create();
    reg.emplace<Name>(e, "Cutscene " + id);
    reg.emplace<WorldTransform2D>(e, at->first, at->second);
    reg.emplace<CutsceneTrigger>(e, CutsceneTrigger{id, 2.5f, true, conditions.empty() ? std::string() : conditions.dump()});
    SaveScene(reg, path);
    item["status"] = "built";
    item["made"] = json::array({"cutscenes/" + id + ".json", sceneFile});
}

std::string GameBuild::CutsceneStartText(const json& planned) const {
    const json trig = planned.value("trigger", json::object());
    const std::string kind = trig.value("kind", std::string("stage")), quest = Str(planned, "quest"), on = planned.value("on", std::string("enter"));
    const std::string stage = quest.empty() ? std::string() : quest + " stage " + std::to_string(planned.value("stage", 0));
    auto placeName = [&](const std::string& place) {
        for (const auto& p : store_.Read("outline").value_or(json::object()).value("places", json::array())) {
            if (Str(p, "id") == place && !Str(p, "name").empty()) return Str(p, "name");
        }
        std::string s = place.rfind("location/", 0) == 0 ? place.substr(9) : place;
        return s;
    };
    auto who = [&](const std::string& key) {
        const std::string name = Str(store_.Read("characters/" + PlanSlug(key)).value_or(json::object()), "name");
        return name.empty() ? (key.rfind("character/", 0) == 0 ? key.substr(10) : key) : name;
    };
    const std::string place = placeName(Str(planned, "place"));
    const std::string during = stage.empty() ? std::string() : on == "complete" ? ", once " + stage + " is done" : ", while " + stage + " is on";
    if (kind == "reach") return "when the player reaches " + Str(trig, "spot") + " in " + place + during;
    if (kind == "leave") return "when the player comes out of " + placeName(Str(trig, "from")) + " into " + place + during;
    if (kind == "talk-end") return "when the player's conversation with " + who(Str(trig, "with")) + " ends" + during;
    if (kind == "defeat") return "when " + who(Str(trig, "who")) + " is defeated" + during;
    if (kind == "use") return "when the player uses the " + Str(trig, "thing") + " in " + place + during;
    if (kind == "event") return "on the event " + Str(trig, "name") + during;
    return stage.empty() ? std::string("as the game starts") : on == "complete" ? "as " + stage + " is done" : "as " + stage + " begins";
}

std::string GameBuild::PlaceCutsceneStart(const std::string& id, const json& planned, const std::string& sceneFile, json& made, std::string& said) {
    const Project project{host_.projectRoot};
    const json trig = planned.value("trigger", json::object());
    const std::string kind = trig.value("kind", std::string("stage")), quest = Str(planned, "quest"), on = planned.value("on", std::string("enter"));
    const int index = planned.value("stage", 0);
    const json play = {{"op", "play_cutscene"}, {"cutscene", id}};
    said = CutsceneStartText(planned);
    // What started it before is taken away first (a trigger, a rule's holder, a stage's effect), so it starts one way.
    Registry reg;
    const std::string path = project.Resolve(sceneFile).string();
    if (sceneFile.empty() || !LoadScene(reg, path)) return "its scene (" + sceneFile + ") can't be read";
    std::vector<Entity> old;
    for (auto [e, t] : reg.view<CutsceneTrigger>().each()) {
        if (t.cutscene == id) old.push_back(e);
    }
    for (auto [e, n] : reg.view<Name>().each()) {
        if (n.value == "Cutscene " + id && std::find(old.begin(), old.end(), e) == old.end()) old.push_back(e);
    }
    for (const Entity e : old) reg.destroy(e);
    if (!quest.empty()) {
        const fs::path qpath = project.Resolve("quests/" + quest + ".json");
        json q = ReadJson(qpath);
        bool changed = false;
        if (q.is_object() && q.contains("stages") && q["stages"].is_array()) {
            for (auto& st : q["stages"]) {
                for (const char* list : {"onEnter", "onComplete"}) {
                    if (!st.contains(list) || !st[list].is_array()) continue;
                    json kept = json::array();
                    for (const auto& e : st[list]) {
                        const bool plays = e.is_object() && e.value("op", "") == "play_cutscene" && (e.value("cutscene", "") == id || e.value("name", "") == id);
                        if (plays) changed = true;
                        else kept.push_back(e);
                    }
                    st[list] = kept;
                }
            }
        }
        if (kind == "stage") {
            bool placed = false;
            if (q.is_object() && q.contains("stages") && q["stages"].is_array()) {
                for (auto& st : q["stages"]) {
                    if (!st.is_object() || st.value("index", -1) != index) continue;
                    const char* list = on == "complete" ? "onComplete" : "onEnter";
                    if (!st.contains(list) || !st[list].is_array()) st[list] = json::array();
                    st[list].push_back(play);
                    placed = changed = true;
                }
            }
            if (!placed) return "quests/" + quest + ".json has no stage " + std::to_string(index) + " to start it";
        }
        if (changed) {
            if (const auto failed = WriteStoryFile(host_.projectRoot, "quests", quest, q)) return *failed;
            made.push_back("quests/" + quest + ".json");
        }
    } else if (kind == "stage") {
        return "it plays at a quest stage but names no quest";
    }
    json conditions = json::array();
    if (!quest.empty() && on != "complete") conditions.push_back({{"quest", quest}, {"is", "active"}, {"stage", index}});
    auto slug = [](std::string p) {
        if (p.rfind("location/", 0) == 0) p = p.substr(9);
        if (p.rfind("character/", 0) == 0) p = p.substr(10);
        return PlanSlug(p);
    };
    auto markerFor = [&](const std::vector<std::string>& names) -> std::optional<std::pair<float, float>> {
        for (const auto& m : names) {
            if (m.empty()) continue;
            if (const auto at = MarkerIn(reg, m)) return at;
        }
        return std::nullopt;
    };
    if (kind == "reach" || kind == "leave") {
        const std::string spot = kind == "reach" ? Str(trig, "spot") : Str(trig, "from");
        const std::string s = slug(spot);
        const auto at = kind == "reach" ? markerFor({spot, s, quest + "/" + s, "story/" + Str(planned, "scene") + "/" + s})
                                        : markerFor({"from-" + s, s, spot});
        if (!at) {
            return kind == "reach" ? "there's no marker \"" + s + "\" in " + sceneFile + " for its spot " + spot + " (add one, or name an anchor of the place)"
                                   : "there's no way out of " + spot + " in " + sceneFile + " (a marker from-" + s + ")";
        }
        const Entity e = reg.create();
        reg.emplace<Name>(e, "Cutscene " + id);
        reg.emplace<WorldTransform2D>(e, at->first, at->second);
        reg.emplace<CutsceneTrigger>(e, CutsceneTrigger{id, 2.5f, true, conditions.empty() ? std::string() : conditions.dump()});
    } else if (kind == "talk-end" || kind == "defeat" || kind == "use" || kind == "event") {
        // A rule that listens for it (about whom or what), held by an entity of its own in the scene.
        json about = json::array();
        std::string event = kind == "talk-end" ? "talk_ended" : kind == "defeat" ? "death" : kind == "use" ? "interact" : Str(trig, "name");
        if (kind == "talk-end" || kind == "defeat") {
            const std::string key = Str(trig, kind == "talk-end" ? "with" : "who");
            const std::string name = Str(store_.Read("characters/" + PlanSlug(key)).value_or(json::object()), "name");
            if (key.empty()) return "it names no one to " + std::string(kind == "talk-end" ? "talk to" : "defeat");
            about.push_back(key.rfind("character/", 0) == 0 ? key : "character/" + key);
            if (!name.empty()) about.push_back(name);
        } else if (kind == "use") {
            const std::string thing = Str(trig, "thing");
            bool there = false;
            for (auto [e, n] : reg.view<Name>().each()) {
                if (Lower(n.value) == Lower(thing)) there = true;
            }
            if (!there) return "nothing in " + sceneFile + " is called \"" + thing + "\" for the player to use";
            about.push_back(thing);
        } else if (event.empty()) {
            return "it starts on an event but names none";
        }
        json rule = {{"event", event}, {"once", true}, {"do", json::array({play})}};
        if (!about.empty()) rule["about"] = about;
        if (!conditions.empty()) rule["if"] = conditions;
        auto at = markerFor({"arrive"});
        const Entity e = reg.create();
        reg.emplace<Name>(e, "Cutscene " + id);
        reg.emplace<WorldTransform2D>(e, at ? at->first : 0.0f, at ? at->second : 0.0f);
        reg.emplace<Responses>(e, Responses{json::array({rule}).dump(), {}});
    }
    SaveScene(reg, path);
    made.push_back(sceneFile);
    return {};
}

void GameBuild::NameThePlace(json& item, const json& req) {
    // 34.6: the journal and the objectives name where to go, in the player's words ("…in Haunted Graveyard").
    const Project project{host_.projectRoot};
    const json when = req.value("when", json::object());
    const std::string quest = when.value("quest", ""), place = req.value("where", json::object()).value("place", "");
    const int index = when.value("stage", 0);
    std::string name;
    for (const auto& p : store_.Read("outline").value_or(json::object()).value("places", json::array())) {
        if (Str(p, "id") == place) name = Str(p, "name");
    }
    if (quest.empty() || name.empty()) {
        item["status"] = "built", item["note"] = "nothing to name";
        return;
    }
    const fs::path path = project.Resolve("quests/" + quest + ".json");
    json q = ReadJson(path);
    if (!q.is_object() || !q.contains("stages")) {
        item["status"] = "failed", item["error"] = "quests/" + quest + ".json can't be read";
        return;
    }
    bool named = false, changed = false;
    for (auto& s : q["stages"]) {
        if (!s.is_object() || s.value("index", -1) != index) continue;
        named = Contains(s.value("journal", ""), name);
        for (const auto& o : s.value("objectives", json::array())) named = named || Contains(o.value("text", ""), name);
        if (named) break;
        // The first objective that goes somewhere (else the first) says where.
        json& objectives = s["objectives"];
        if (objectives.is_array() && !objectives.empty()) {
            json* pick = &objectives[0];
            for (auto& o : objectives) {
                const std::string type = o.value("type", "");
                if (type == "reach" || type == "goto" || type == "examine") {
                    pick = &o;
                    break;
                }
            }
            (*pick)["text"] = pick->value("text", std::string("Go there")) + " in " + name;
            changed = true;
        }
        if (!s.value("journal", "").empty()) s["journal"] = s.value("journal", "") + " (" + name + ")", changed = true;
    }
    if (named) {
        item["status"] = "built", item["note"] = "the journal names " + name;
        return;
    }
    if (!changed) {
        item["status"] = "failed", item["error"] = "stage " + std::to_string(index) + " of " + quest + " has no journal or objective to name it in";
        return;
    }
    if (const auto failed = WriteStoryFile(host_.projectRoot, "quests", quest, q)) {
        item["status"] = "failed", item["error"] = *failed;
        return;
    }
    item["status"] = "built", item["made"] = json::array({"quests/" + quest + ".json"}), item["note"] = "names " + name;
}

// --- Checks (34.4) -------------------------------------------------------------------------------------------------

json GameBuild::StaticChecks(const json& req) {
    // Each accept check read from the files: true (holds), false (doesn't: why), null (only a played test can tell).
    const Project project{host_.projectRoot};
    std::map<std::string, std::unique_ptr<Registry>>& scenes = checkScenes_;
    auto scene = [&](const std::string& file) -> Registry* {
        if (file.empty()) return nullptr;
        auto it = scenes.find(file);
        if (it == scenes.end()) {
            auto reg = std::make_unique<Registry>();
            if (!LoadScene(*reg, project.Resolve(file).string())) reg.reset();
            it = scenes.emplace(file, std::move(reg)).first;
        }
        return it->second.get();
    };
    auto sceneOfPlace = [&](const std::string& place) -> std::string {
        const Row* row = FindRow(place);
        return row ? row->scene : std::string();
    };
    auto quest = [&](const std::string& id) { return ReadJson(project.Resolve("quests/" + id + ".json")); };
    auto stageOf = [&](const json& q, int index) -> json {
        for (const auto& s : q.value("stages", json::array())) {
            if (s.value("index", -1) == index) return s;
        }
        return json();
    };
    auto nameOf = [&](const std::string& who) {
        const std::string name = Str(store_.Read("characters/" + PlanSlug(who)).value_or(json::object()), "name");
        return name.empty() ? who : name;
    };
    auto placeNameOf = [&](const std::string& place) {
        for (const auto& p : store_.Read("outline").value_or(json::object()).value("places", json::array())) {
            if (Str(p, "id") == place) return Str(p, "name");
        }
        return place;
    };
    json out = json::array();
    const json reqWhere = req.value("where", json::object());
    for (const auto& accepted : req.value("accept", json::array())) {
        json a = accepted;
        const std::string check = a.value("check", "");
        if (check == "animation") {
            // Its subject by what it is: a plain name ("graveyard spirits") is the asset or character it names.
            const std::string who = AnimationOf(json{{"animation", {{"who", a.value("who", "")}, {"action", a.value("action", "")}}}}, req).value("who", "");
            if (!who.empty()) a["who"] = who;
        }
        json r = {{"check", check}, {"ok", nullptr}};
        auto fail = [&](const std::string& why) { r["ok"] = false, r["why"] = why; };
        if (check == "present") {
            const std::string who = a.value("who", ""), file = a.value("scene", reqWhere.value("scene", ""));
            const std::string key = who.rfind("character/", 0) == 0 ? who : "character/" + PlanSlug(who);
            bool there = false;
            // The player is wherever the player goes: never placed again for it (a second copy of the player otherwise).
            for (const auto& act : bindings_.value("actors", json::array())) {
                there = there || (act.value("player", false) && (act.value("character", "") == who || act.value("character", "") == key));
            }
            // Brought there by their day or the cast timeline (a package whose place is that scene).
            for (const auto& [id, p] : packages_.items()) {
                there = there || (id.rfind(PlanSlug(key.substr(10)) + "-", 0) == 0 && p.value("location", json::object()).value("scene", "") == file);
            }
            if (Registry* reg = scene(file)) {
                for (auto [e, actor] : reg->view<Actor>().each()) there = there || actor.key == who || actor.key == key || Lower(actor.key) == Lower(nameOf(who));
                for (auto [e, n] : reg->view<Name>().each()) there = there || Lower(n.value) == Lower(nameOf(who));
            }
            // Or brought there by a quest stage (the story's staging).
            for (const auto& doc : store_.List("quests")) {
                const json q = quest(store_.Read(doc).value_or(json::object()).value("id", PlanSlug(doc)));
                for (const auto& s : q.value("stages", json::array())) {
                    for (const auto& e : s.value("onEnter", json::array())) {
                        there = there || (e.value("op", "") == "move_actor" && (e.value("who", "") == who || Lower(e.value("who", "")) == Lower(nameOf(who))) &&
                                          e.value("scene", "") == file);
                    }
                }
            }
            if (there) r["ok"] = true;
            else fail(nameOf(who) + " isn't in " + file + " and no stage brings them there");
        } else if (check == "prop" || check == "placed") {
            const std::string name = a.value("name", "");
            std::string file = a.value("scene", "");
            if (file.empty() && a.contains("place") && a["place"].is_string()) file = sceneOfPlace(a["place"].get<std::string>());
            if (file.empty()) file = reqWhere.value("scene", sceneOfPlace(reqWhere.value("place", "")));
            bool there = false;
            if (Registry* reg = scene(file)) {
                const std::string slug = PlanSlug(AssetSlug(name));
                for (auto [e, n] : reg->view<Name>().each()) there = there || Contains(n.value, name);
                for (auto [e, m] : reg->view<Marker>().each()) there = there || (check == "prop" && (Contains(m.name, slug) || Contains(m.name, name)));
                for (auto [e, gw] : reg->view<Gateway>().each()) {
                    if (auto* n = reg->try_get<Name>(e)) there = there || Contains(n->value, name);
                }
            }
            // Painted with its place: there.
            if (const json* asset = AssetFor(req); asset && !(*asset)["asset"].value("separate", true)) there = true;
            if (there) r["ok"] = true;
            else fail("no " + name + " in " + file);
        } else if (check == "asset" || check == "painted") {
            const json* asset = req.value("kind", "") == "asset" ? &req : AssetFor(req);
            const json av = asset ? (*asset).value("asset", json::object()) : json::object();
            if (check == "painted" || (asset && !av.value("separate", true))) {
                r["ok"] = true;
            } else if (asset && av.value("type", "") == "character") {
                const json* drawn = nullptr;
                for (const auto& x : art_) drawn = x.value("subject", "") == av.value("id", "") ? &x : drawn;
                if (drawn && drawn->value("status", "") == "done") r["ok"] = true;
                else fail(av.value("name", "") + " isn't drawn" + (drawn ? " (" + drawn->value("error", drawn->value("status", "")) + ")" : ""));
            } else if (asset && (!av.value("art", "").empty() || !av.value("sheet", "").empty()) &&
                       fs::exists(project.root / av.value("art", av.value("sheet", "")))) {
                r["ok"] = true;
            } else {
                fail(a.value("name", av.value("name", "it")) + " isn't drawn");
            }
        } else if (check == "animation" && a.value("who", "").rfind("asset:", 0) == 0 &&
                   std::none_of(art_.begin(), art_.end(), [&](const json& x) { return x.value("subject", "") == a.value("who", ""); })) {
            // A thing's: its sheet kept on its asset.
            bool drawn = false;
            for (const auto& q : reqs_["items"]) {
                const json av = q.value("asset", json::object());
                if (q.value("kind", "") == "asset" && "asset:" + av.value("id", "") == a.value("who", "")) {
                    drawn = av.value("sheets", json::object()).contains(a.value("action", ""));
                }
            }
            if (drawn) r["ok"] = true;
            else fail(a.value("who", "").substr(6) + " isn't animated " + a.value("action", ""));
        } else if (check == "animation") {
            const std::string who = a.value("who", ""), action = a.value("action", "");
            const json* drawn = nullptr;
            for (const auto& x : art_) drawn = x.value("subject", "") == who ? &x : drawn;
            int at = -1;
            const json actions = drawn ? drawn->value("actions", json::array()) : json::array();
            for (int k = 0; k < static_cast<int>(actions.size()); ++k) at = actions[k].value("action", "") == action ? k : at;
            if (drawn && at >= 0 && at < drawn->value("next", 0) && !drawn->value("sheet", "").empty()) r["ok"] = true;
            else fail(nameOf(who) + " isn't animated " + action);
        } else if (check == "objective") {
            const json s = stageOf(quest(a.value("quest", "")), a.value("stage", 0));
            bool has = false;
            for (const auto& o : s.value("objectives", json::array())) {
                const std::string target = o.value("target", "");
                has = has || target == "@" + a.value("target", "") || Contains(target, a.value("target", "")) || Contains(o.value("id", ""), a.value("target", ""));
            }
            if (has) r["ok"] = true;
            else fail(a.value("quest", "") + " stage " + std::to_string(a.value("stage", 0)) + " has no objective on " + a.value("target", ""));
        } else if (check == "journal-names") {
            const json s = stageOf(quest(a.value("quest", "")), a.value("stage", 0));
            const std::string name = a.value("place", "");
            bool named = Contains(s.value("journal", ""), name);
            for (const auto& o : s.value("objectives", json::array())) named = named || Contains(o.value("text", ""), name);
            if (named) r["ok"] = true;
            else fail("stage " + std::to_string(a.value("stage", 0)) + "'s journal and objectives don't name " + name);
        } else if (check == "gateway-visible") {
            const std::string to = a.value("to", "");
            const json settled = ReadJson(store_.Dir() / "gateways.json");
            bool seen = false, any = false;
            if (settled.is_object()) {
                for (const auto& [key, g] : settled.items()) {
                    if (key.size() <= to.size() || key.compare(key.size() - to.size(), to.size(), to) != 0) continue;
                    any = true;
                    seen = seen || !g.value("seen", "").empty() || !g.value("art", "").empty();
                }
            }
            // A building's own door shows itself (an interior is gone into by its door).
            for (const auto& row : rows_) {
                if (row.scene == to && row.buildType == "interior") seen = true;
            }
            if (seen) r["ok"] = true;
            else fail(any ? "the way into " + to + " is a bare tile: nothing shows it" : "no gateway leads into " + to);
        } else if (check == "objective-marker") {
            const json s = stageOf(quest(a.value("quest", "")), a.value("stage", 0));
            bool target = false;
            for (const auto& o : s.value("objectives", json::array())) target = target || !o.value("target", "").empty();
            if (!ActiveSettings().story.objectiveMarkers) fail("objective markers are off (Project Settings ▸ Story)");
            else if (!target) fail("the stage's objectives have no target to point to");
            else r["ok"] = true;
        } else if (check == "cutscene-cast") {
            const json c = ReadJson(project.root / "cutscenes" / (a.value("id", "") + ".json"));
            if (!c.is_object()) {
                fail("cutscenes/" + a.value("id", "") + ".json isn't written");
            } else {
                std::set<std::string> in;
                for (const auto& t : c.value("tracks", json::array())) {
                    if (!Str(t, "actor").empty()) in.insert(Str(t, "actor"));
                    for (const auto& k : t.value("keys", json::array())) {
                        for (const char* f : {"speaker", "with", "actor"}) {
                            if (!Str(k, f).empty()) in.insert(Str(k, f));
                        }
                    }
                }
                std::string missing;
                for (const auto& w : a.value("cast", json::array())) {
                    if (w.is_string() && w.get<std::string>() != "player" && !in.contains(w.get<std::string>())) missing += (missing.empty() ? "" : ", ") + w.get<std::string>();
                }
                if (missing.empty()) r["ok"] = true;
                else fail("not in the cutscene: " + missing);
            }
        } else if (check == "cutscene") {
            if (fs::exists(project.root / "cutscenes" / (a.value("id", "") + ".json"))) r["ok"] = true;
            else fail("cutscenes/" + a.value("id", "") + ".json isn't written");
        } else if (check == "feature-step" || check == "scene-played") {
            // Built by its items (the AI panel's task done); played: a test tells.
            const std::string id = req.value("id", "");
            bool all = true, any = false;
            for (const auto& i : reqs_.value("implementations", json::array())) {
                if (i.value("req", "") != id) continue;
                any = true;
                all = all && i.value("status", "") == "built";
            }
            if (check == "feature-step") {
                if (any && all) r["ok"] = true;
                else if (any) fail("its AI-panel task isn't done");
            }
        }
        if (check == "located" || check == "behaviour" || check == "package" || check == "dialogue") {
            out.push_back(TimelineStatic(a));
            continue;
        }
        if (check == "at" || check == "prop-at") { // the placements' whereabouts (Placements.cpp)
            out.push_back(PlacementStatic(a));
            continue;
        }
        // enters, damages, says, follows, event, cutscene-plays, stage-advances: only a played test tells (PlayedChecks).
        (void)placeNameOf;
        out.push_back(r);
    }
    return out;
}

void GameBuild::PlayedChecks(const json& req, json& checks) {
    if (req.value("kind", "") == "whereabouts") {
        // Where they were as the stage began, in any scene's test (the placements' whereabouts).
        const json tests = check_.value("tests", json::object());
        const json accept = req.value("accept", json::array());
        for (const auto& [scene, t] : tests.items()) {
            const json record = t.value("log", json::object()).value("record", json::object());
            if (record.empty()) continue;
            for (size_t k = 0; k < accept.size() && k < checks.size(); ++k) PlacementPlayed(accept[k], record, checks[k]);
        }
        return;
    }
    if (req.value("kind", "") == "character-state") {
        // The cast timeline's: as its stage began in any scene's test (the latest that saw it).
        const json tests = check_.value("tests", json::object());
        const json accept = req.value("accept", json::array());
        for (const auto& [scene, t] : tests.items()) {
            const json record = t.value("log", json::object()).value("record", json::object());
            if (record.empty()) continue;
            for (size_t k = 0; k < accept.size() && k < checks.size(); ++k) TimelinePlayed(accept[k], record, checks[k]);
        }
        return;
    }
    // What the scene's test recorded (SceneAutopilot's record): who was where, who hurt whom, what was said, the stages,
    // the cutscenes, the events, who kept by the player.
    const json tests = check_.value("tests", json::object());
    const json test = tests.value(req.value("scene", ""), json::object());
    const json log = test.value("log", json::object());
    if (!log.contains("record")) return;
    const json rec = log["record"];
    auto nameOf = [&](const std::string& who) {
        if (who == "player") return std::string("player");
        const std::string name = Str(store_.Read("characters/" + PlanSlug(who)).value_or(json::object()), "name");
        return name.empty() ? who : name;
    };
    auto same = [&](const std::string& recorded, const std::string& who) {
        return Lower(recorded) == Lower(nameOf(who)) || Lower(recorded) == Lower(who) || Contains(recorded, nameOf(who));
    };
    const json accept = req.value("accept", json::array());
    for (size_t k = 0; k < accept.size() && k < checks.size(); ++k) {
        const json& a = accept[k];
        json& r = checks[k];
        const std::string check = a.value("check", "");
        auto verdict = [&](bool ok, const std::string& why) {
            r["ok"] = ok;
            r["played"] = true;
            if (ok) r.erase("why");
            else r["why"] = why;
        };
        if (check == "present" || check == "enters") {
            const std::string who = a.value("who", ""), file = a.value("scene", req.value("where", json::object()).value("scene", ""));
            bool seen = false;
            for (const auto& n : rec.value("present", json::object()).value(file, json::array())) seen = seen || (n.is_string() && same(n.get<std::string>(), who));
            if (seen || r.value("ok", json()) != json(true)) verdict(seen, nameOf(who) + " wasn't seen in " + file + " in the test");
        } else if (check == "damages") {
            bool hurt = false;
            for (const auto& d : rec.value("damage", json::array())) hurt = hurt || (same(d.value("by", ""), a.value("who", "")) && same(d.value("target", ""), a.value("target", "")));
            verdict(hurt, nameOf(a.value("who", "")) + " didn't hurt " + nameOf(a.value("target", "")) + " in the test");
        } else if (check == "says") {
            bool said = false;
            for (const auto& l : log.value("dialogue", json::array())) {
                if (!same(l.value("speaker", ""), a.value("who", ""))) continue;
                const std::string words = a.value("words", "");
                int found = 0, asked = 0;
                std::istringstream in(words);
                for (std::string w; in >> w;) {
                    if (w.size() < 4) continue;
                    ++asked;
                    found += Contains(l.value("text", ""), w);
                }
                said = said || asked == 0 || found * 2 >= asked;
            }
            for (const auto& s : rec.value("said", json::array())) said = said || same(s.value("who", ""), a.value("who", ""));
            verdict(said, nameOf(a.value("who", "")) + " didn't say it in the test");
        } else if (check == "cutscene-plays") {
            const json played = rec.value("cutscenes", json::array());
            const bool ok = std::any_of(played.begin(), played.end(), [&](const json& c) { return c.is_string() && (a.value("id", "").empty() || c.get<std::string>() == a.value("id", "")); });
            verdict(ok, "the cutscene " + a.value("id", "") + " didn't play in the test");
        } else if (check == "stage-advances") {
            bool moved = false;
            for (const auto& s : rec.value("stages", json::array())) moved = moved || (s.value("quest", "") == a.value("quest", "") && s.value("stage", 0) > a.value("from", 0));
            verdict(moved, a.value("quest", "") + " didn't move on from stage " + std::to_string(a.value("from", 0)) + " in the test");
        } else if (check == "event") {
            const json events = rec.value("events", json::array());
            verdict(std::find(events.begin(), events.end(), a.value("name", "")) != events.end(), "the event " + a.value("name", "") + " didn't happen in the test");
        } else if (check == "follows") {
            double closest = 0.0;
            const json nearBy = rec.value("near", json::object()); // held: items() only refers to it
            for (const auto& [n, s] : nearBy.items()) {
                if (same(n, a.value("who", ""))) closest = std::max(closest, s.get<double>());
            }
            verdict(closest >= 8.0, nameOf(a.value("who", "")) + " didn't keep by the player in the test");
        } else if (check == "scene-played") {
            verdict(test.value("passed", false), "its test didn't pass: " + test.value("summary", ""));
        }
    }
}

void GameBuild::CheckRequirements() {
    checkScenes_.clear();
    json summary = {{"todo", 0}, {"built", 0}, {"verified", 0}, {"failed", 0}};
    for (auto& r : reqs_["items"]) {
        json checks = StaticChecks(r);
        PlayedChecks(r, checks);
        r["checks"] = checks;
        bool all = !checks.empty(), anyFalse = false;
        std::string why;
        for (const auto& c : checks) {
            if (c.value("ok", json()) == json(false)) anyFalse = true, why = why.empty() ? c.value("why", "") : why;
            all = all && c.value("ok", json()) == json(true);
        }
        bool itemsBuilt = true, itemsFailed = false, anyItem = false;
        std::string itemWhy;
        for (const auto& i : reqs_["implementations"]) {
            if (i.value("req", "") != r.value("id", "")) continue;
            anyItem = true;
            itemsBuilt = itemsBuilt && i.value("status", "") == "built";
            if (i.value("status", "") == "failed") itemsFailed = true, itemWhy = itemWhy.empty() ? i.value("error", "") : itemWhy;
        }
        std::string status = "todo";
        if (anyFalse) status = "failed";
        else if (all) status = "verified";
        else if (itemsFailed) status = "failed", why = itemWhy;
        else if (anyItem && itemsBuilt) status = "built";
        r["status"] = status;
        if (status == "failed") r["why"] = why;
        else r.erase("why");
        summary[status] = summary.value(status, 0) + 1;
    }
    reqs_["summary"] = summary;
    checkScenes_.clear();
}

} // namespace aige
