// Build ▸ Animate (docs/assets/animations.md §4.1): a place's moving parts found and animated after its art is made.
// Assembled: each building's door (found in its art by the vision model, opened from its own pixels by the service's
// app/doors.py, an AnimatedPart on its prefab: every copy) and
// each thing that's used (a chest, a gate, a lever: its prefab animated, opened when it's used). Paint: each painted
// building's door found in its part of the painting (kept in places/<id>/parts.json, put over the painting when the
// place is built), and the things that are used, as Assembled. One target at a time: find, animate, apply.

#include "EditorApp.h"

#include "assets/Prefab.h"
#include "assets/TextureCook.h"
#include "editing/PaintedPlace.h"
#include "editing/PlaceBuild.h"
#include "gameplay/Animation.h"
#include "gameplay/Components.h"
#include "gameplay/Interiors.h"
#include "gameplay/Settings.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace aige {

namespace {

// The things that are used, by role: how they move, the clip, the Interactable's verb (what triggers them: used, with
// the interact key; a building's door instead opens as someone comes near).
struct UsedThing {
    const char* role;
    const char* action;
    const char* clip;
    const char* verb;
};
const UsedThing kUsed[] = {
    {"chest", "the chest's lid swings open", "open", "Open"},
    {"door", "the door swings open", "open", "Open"},
    {"locked-door", "the door swings open", "open", "Open"},
    {"secret-door", "the hidden door slides open", "open", "Open"},
    {"gate", "the gate swings open", "open", "Open"},
    {"portcullis", "the portcullis rises", "open", "Raise"},
    {"drawbridge", "the drawbridge lowers", "open", "Lower"},
    {"lever", "the lever is pulled down", "pull", "Pull"},
    {"sarcophagus", "the stone lid slides open", "open", "Open"},
    {"cupboard", "its doors swing open", "open", "Open"},
    {"wardrobe", "its doors swing open", "open", "Open"},
    {"cage", "the cage door swings open", "open", "Open"},
};
const UsedThing* UsedFor(const std::string& role) {
    for (const auto& u : kUsed) {
        if (role == u.role) return &u;
    }
    return nullptr;
}

// How a building's door will open: the project's way (Project Settings ▸ Game ▸ doorsOpen; each door can say its own
// in the Inspector, AnimatedPart ▸ opens, locked, key).
std::string DoorTrigger() {
    const GameSettings& game = ActiveSettings().game;
    if (game.doorsOpen == "auto") return "by itself as someone comes near (Project Settings ▸ Game; each door can differ: Inspector ▸ AnimatedPart)";
    return "with " + game.doorActions + (game.doorDoubleClick ? " or a double click" : std::string()) +
           ", near it (Project Settings ▸ Game; each door can differ: Inspector ▸ AnimatedPart ▸ opens, locked)";
}

// /parts/door's request for a target's door: its picture, the box (found, or drawn by hand: `exact`, used as drawn).
nlohmann::json DoorRequest(const std::string& buildId, const nlohmann::json& target, const nlohmann::json& box, bool exact) {
    std::string name = buildId + "-door-" + target.value("key", std::string("door"));
    for (char& ch : name) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-') ch = '-';
    }
    return {{"path", target.value("texture", "")}, {"box", box}, {"name", name.substr(0, 60)}, {"exact", exact},
            {"leaves", target.value("leaves", 0)}};
}

nlohmann::json ReadJsonFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return in ? nlohmann::json::parse(in, nullptr, false) : nlohmann::json();
}

} // namespace

nlohmann::json EditorApp::AnimateTargets() {
    // What can move in the open build: from its assets (Assembled and Paint alike) and its painting (Paint).
    nlohmann::json targets = nlohmann::json::array();
    if (!build_) return targets;
    std::set<std::string> seen;
    // The built plan (assets.json, written by Build), else the plan as it stands: the art made, not built yet.
    nlohmann::json plan = ReadJsonFile(build_->Folder() / "assets.json");
    if (!plan.is_object() || !plan.contains("needs")) plan = PlannedAssets();
    const bool paint = build_->Method() == "Paint";
    for (const auto& row : plan.value("needs", nlohmann::json::array())) {
        const std::string role = row.value("role", ""), prefab = row.value("prefab", "");
        const bool building = row.value("layer", "") == "building";
        const UsedThing* used = UsedFor(role);
        if ((!building && !used) || prefab.empty() || !seen.insert(prefab).second) continue;
        if (building && paint) continue; // painted: its door is found in the painting, below
        std::string texture;
        std::array<int, 4> region{0, 0, 0, 0};
        prefabs_.VisitTemplate(prefab, [&](Registry& r, Entity t) {
            if (const auto* s = r.try_get<Sprite>(t)) {
                texture = s->texture;
                if (const AssetInfo* a = assets_.Find(texture); a && a->width > 0) {
                    const float u0 = std::min(s->u0, s->u1), u1 = std::max(s->u0, s->u1);
                    region = {static_cast<int>(std::lround(u0 * a->width)), static_cast<int>(std::lround(s->v0 * a->height)),
                              static_cast<int>(std::lround((u1 - u0) * a->width)), static_cast<int>(std::lround((s->v1 - s->v0) * a->height))};
                }
            }
        });
        if (texture.empty() || region[2] <= 0) continue;
        int copies = 0;
        for (auto [e, link] : registry_.view<PrefabInstance>().each()) copies += link.path == prefab;
        targets.push_back({{"key", prefab}, {"kind", building ? "door" : "thing"}, {"role", role}, {"label", row.value("title", role)},
                           {"prefab", prefab}, {"texture", texture}, {"region", region},
                           {"trigger", building ? DoorTrigger() : std::string("used (") + used->verb + ")"},
                           {"where", std::to_string(copies) + (copies == 1 ? " copy" : " copies") + " in the scene"},
                           {"status", "pending"}});
    }
    if (paint && build_->Painted()) {
        // Each painted building: its part of the painting, where its door is looked for.
        const nlohmann::json painting = LoadPainting(build_->Folder());
        const PaintView view = PaintView::From(painting.value("view", nlohmann::json::object()));
        const int W = painting["size"][0].get<int>(), H = painting["size"][1].get<int>();
        const nlohmann::json painted = painting.value("objects", nlohmann::json::array());
        for (const auto& o : build_->Solved().value("objects", nlohmann::json::array())) {
            if (o.value("layer", "") != "building") continue;
            const std::string id = o.value("id", "");
            if (std::find(painted.begin(), painted.end(), id) == painted.end()) continue;
            const double x = o.value("x", 0), y = o.value("y", 0), w = std::max(1, o.value("w", 1)), h = std::max(1, o.value("h", 1));
            const auto left = view.Pixel(x, y + h), right = view.Pixel(x + w, y), bottom = view.Pixel(x + w, y + h), top = view.Pixel(x, y);
            const double tall = (w + h) * view.th * 1.4; // its walls and roof above its footprint
            const int x0 = std::clamp(static_cast<int>(left.first), 0, W - 1), x1 = std::clamp(static_cast<int>(right.first), 1, W);
            const int y0 = std::clamp(static_cast<int>(top.second - tall), 0, H - 1), y1 = std::clamp(static_cast<int>(bottom.second), 1, H);
            if (x1 - x0 < 8 || y1 - y0 < 8) continue;
            targets.push_back({{"key", "painting:" + id}, {"kind", "door"}, {"role", o.value("slot", "building")},
                               {"label", o.value("slot", std::string("building")) + " " + id.substr(id.rfind('/') + 1)},
                               {"texture", "places/" + build_->Id() + "/" + painting.value("painting", "painting.png")},
                               {"region", {x0, y0, x1 - x0, y1 - y0}},
                               {"trigger", "near: opens as someone comes to it, closes as they go"}, // painted: its overlay's Action
                               {"where", "cell " + std::to_string(static_cast<int>(x)) + "," + std::to_string(static_cast<int>(y))},
                               {"status", "pending"}});
        }
    }
    // What was done before (this build's animate.json) kept; a building's other doors (key "<its key>#<n>") too.
    const nlohmann::json before = ReadJsonFile(build_->Folder() / "animate.json");
    for (const auto& b : before.value("targets", nlohmann::json::array())) {
        const std::string key = b.value("key", "");
        const size_t hash = key.find('#');
        if (hash == std::string::npos) continue;
        auto base = std::find_if(targets.begin(), targets.end(), [&](const nlohmann::json& t) { return t.value("key", "") == key.substr(0, hash); });
        if (base == targets.end()) continue;
        nlohmann::json extra = b;
        if (extra.value("status", "") != "done" && extra.value("status", "") != "skipped") extra["status"] = "pending";
        targets.insert(std::find_if(base, targets.end(), [&](const nlohmann::json& t) {
                           return t.value("key", "").rfind(key.substr(0, hash), 0) != 0;
                       }),
                       extra);
    }
    for (auto& t : targets) {
        for (const auto& b : before.value("targets", nlohmann::json::array())) {
            if (b.value("key", "") == t.value("key", "") && (b.value("status", "") == "done" || b.value("status", "") == "skipped")) {
                t["status"] = b["status"];
                if (b.contains("box")) t["box"] = b["box"];
                if (b.contains("sheet")) t["sheet"] = b["sheet"];
                if (b.contains("preview")) t["preview"] = b["preview"];
            }
        }
    }
    return targets;
}

void EditorApp::PostAnimate() {
    int done = 0, failed = 0, total = 0;
    for (const auto& t : animate_.value("targets", nlohmann::json::array())) {
        if (t.value("status", "") == "skipped") continue;
        ++total;
        done += t.value("status", "") == "done";
        failed += t.value("status", "") == "failed";
    }
    animate_["done"] = done, animate_["failed"] = failed, animate_["total"] = total;
    if (build_) {
        std::ofstream(build_->Folder() / "animate.json") << animate_.dump(2) << "\n";
    }
    nlohmann::json message = animate_;
    message["type"] = "build:animate";
    post_(message);
}

bool EditorApp::HandleAnimateMessage(const std::string& type, const nlohmann::json& message) {
    if (type == "build:animateScan") {
        animate_ = {{"targets", AnimateTargets()}, {"running", animateRequest_ != 0}};
        PostAnimate();
        return true;
    }
    if (type == "build:animateSkip" || type == "build:animateUnskip") {
        for (auto& t : animate_["targets"]) {
            if (t.value("key", "") == message.value("key", "")) t["status"] = type == "build:animateSkip" ? "skipped" : "pending";
        }
        PostAnimate();
        return true;
    }
    if (type == "build:animateRun") {
        // All that's left (or one): in turn, each found, animated and applied.
        // Boxes drawn by hand round its doors (the user saw the found ones were off, or some missing): one door each,
        // animated from it as drawn, the building's other doors gone (§4.3).
        std::string only = message.value("key", "");
        nlohmann::json boxes = message.value("boxes", nlohmann::json::array());
        if (message.contains("box")) boxes.push_back(message["box"]);
        std::string onlyBase;
        if (!only.empty() && !boxes.empty()) {
            const std::string base = only.substr(0, only.find('#'));
            auto& list = animate_["targets"];
            auto first = std::find_if(list.begin(), list.end(), [&](const nlohmann::json& t) { return t.value("key", "") == base; });
            if (first != list.end()) {
                nlohmann::json model = *first;
                list.erase(std::remove_if(list.begin(), list.end(), [&](const nlohmann::json& t) {
                               return t.value("key", "").rfind(base + "#", 0) == 0;
                           }),
                           list.end());
                first = std::find_if(list.begin(), list.end(), [&](const nlohmann::json& t) { return t.value("key", "") == base; });
                size_t at = static_cast<size_t>(first - list.begin());
                for (size_t i = 0; i < boxes.size(); ++i) {
                    nlohmann::json t = i == 0 ? *first : model;
                    t["key"] = i == 0 ? base : base + "#" + std::to_string(i);
                    if (i > 0) t["label"] = model.value("label", std::string("Door")) + " · door " + std::to_string(i + 1);
                    t["door"] = i;
                    t["reset"] = i == 0;
                    t["drawn"] = boxes[i];
                    t["status"] = "pending";
                    for (const char* stale : {"preview", "sheet", "found", "error", "given"}) t.erase(stale);
                    if (i == 0) list[at] = t;
                    else list.insert(list.begin() + static_cast<std::ptrdiff_t>(at + i), t);
                }
                onlyBase = base;
                only.clear();
            }
        }
        for (auto& t : animate_["targets"]) {
            const std::string status = t.value("status", "");
            if (!only.empty() && t.value("key", "") != only) continue;
            if (!onlyBase.empty()) continue;
            if (status == "failed" || (!only.empty() && status != "skipped")) t["status"] = "pending";
        }
        animate_["running"] = true;
        animate_["only"] = only;
        animate_["onlyBase"] = onlyBase;
        AnimateNext();
        return true;
    }
    if (type == "build:animateStop") {
        animate_["running"] = false;
        PostAnimate();
        return true;
    }
    return false;
}

void EditorApp::AnimateNext() {
    if (!animate_.value("running", false) || !build_ || !Ai() || Ai()->GetState() != AiClient::State::Ready) {
        animate_["running"] = false;
        PostAnimate();
        return;
    }
    const std::string only = animate_.value("only", ""), onlyBase = animate_.value("onlyBase", "");
    for (auto& t : animate_["targets"]) {
        if (t.value("status", "") != "pending" || (!only.empty() && t.value("key", "") != only)) continue;
        if (!onlyBase.empty() && t.value("key", "").substr(0, t.value("key", "").find('#')) != onlyBase) continue;
        animateKey_ = t.value("key", "");
        const auto region = t.value("region", nlohmann::json::array());
        if (t.value("kind", "") == "door" && t.contains("drawn")) {
            // Drawn by hand: opened from that box as drawn.
            t["found"] = t["drawn"];
            t["box"] = t["drawn"];
            t["status"] = "animating";
            animateRequest_ = Ai()->Request("POST", "/parts/door", DoorRequest(build_->Id(), t, t["drawn"], true), 120);
            t.erase("drawn");
        } else if (t.value("kind", "") == "door" && t.contains("given")) {
            // Another door the finder saw in the building's picture: fitted and opened.
            t["found"] = t["given"];
            t["box"] = t["given"];
            t["status"] = "animating";
            animateRequest_ = Ai()->Request("POST", "/parts/door", DoorRequest(build_->Id(), t, t["given"], false), 120);
        } else if (t.value("kind", "") == "door") {
            // Its door looked for in its picture (or its part of the painting).
            t["status"] = "finding";
            animateRequest_ = Ai()->Request("POST", "/parts/find", {{"path", t.value("texture", "")}, {"kinds", {"door", "gate"}}, {"rect", region}}, 180);
        } else {
            t["status"] = "animating";
            AnimateCrop(t, region);
        }
        PostAnimate();
        return;
    }
    animate_["running"] = false; // nothing left
    PostAnimate();
}

void EditorApp::AnimateCrop(nlohmann::json& target, const nlohmann::json& box) {
    // The box cut out of its picture and animated as a picture is (Generate ▸ Animate's provider).
    const auto image = DecodeImage(assets_.GetProject().Resolve(target.value("texture", "")));
    if (!image || !box.is_array() || box.size() != 4) {
        target["status"] = "failed", target["error"] = "its picture can't be read";
        AnimateNext();
        return;
    }
    const int W = static_cast<int>(image->width), H = static_cast<int>(image->height);
    const int bx = box[0].get<int>(), by = box[1].get<int>(), bw = std::max(1, box[2].get<int>()), bh = std::max(1, box[3].get<int>());
    ImageData crop;
    crop.width = static_cast<uint32_t>(bw), crop.height = static_cast<uint32_t>(bh);
    crop.rgba.assign(static_cast<size_t>(bw) * bh * 4, 0);
    for (int y = 0; y < bh; ++y) {
        for (int x = 0; x < bw; ++x) {
            const int sx = bx + x, sy = by + y;
            if (sx >= 0 && sy >= 0 && sx < W && sy < H) std::memcpy(&crop.rgba[(static_cast<size_t>(y) * bw + x) * 4], &image->rgba[(static_cast<size_t>(sy) * W + sx) * 4], 4);
        }
    }
    std::string stem;
    for (char c : target.value("key", std::string("part"))) stem += std::isalnum(static_cast<unsigned char>(c)) ? c : '-';
    const std::string path = "assets/textures/generated/parts/" + build_->Id() + "-" + stem.substr(std::max<int>(0, static_cast<int>(stem.size()) - 40)) + ".png";
    std::error_code ec;
    std::filesystem::create_directories(assets_.GetProject().Resolve("assets/textures/generated/parts"), ec);
    if (!WritePng(assets_.GetProject().Resolve(path), crop)) {
        target["status"] = "failed", target["error"] = "can't write its picture";
        AnimateNext();
        return;
    }
    RefreshAssets();
    target["crop"] = path;
    const UsedThing* used = UsedFor(target.value("role", ""));
    const bool door = target.value("kind", "") == "door";
    animateRequest_ = Ai()->Request("POST", "/animations/generate",
                                    {{"path", path},
                                     {"action", door ? "the door swings open" : used ? used->action : "it opens"},
                                     {"clip", door ? "open" : used ? used->clip : "open"},
                                     {"description", target.value("label", "")},
                                     {"provider", userPrefs_ ? userPrefs_->Get("animateProvider", "autosprite") : nlohmann::json("autosprite")},
                                     {"name", std::filesystem::path(path).stem().string()}},
                                    900);
}

void EditorApp::OnAnimateResult(const nlohmann::json& body) {
    animateRequest_ = 0;
    nlohmann::json* target = nullptr;
    for (auto& t : animate_["targets"]) {
        if (t.value("key", "") == animateKey_) target = &t;
    }
    if (!target || !build_) return;
    auto fail = [&](const std::string& why) {
        (*target)["status"] = "failed", (*target)["error"] = why;
        AnimateNext();
    };
    if ((*target)["status"] == "finding") {
        // Every door it saw (§4.3): the main one this target's, each other one a target of its own after it; an open
        // archway (no door in it) left out, and boxes over one another kept once.
        if (!body.value("ok", false)) return fail(body.value("errors", nlohmann::json::array()).dump());
        std::vector<nlohmann::json> seen;
        bool archway = false;
        for (const auto& p : body.value("parts", nlohmann::json::array())) {
            if (p.value("open", false)) {
                archway = true;
                continue;
            }
            if (p.value("confidence", 0.0) < 0.35 || !p.contains("box")) continue;
            const auto& b = p["box"];
            const bool overlaps = std::any_of(seen.begin(), seen.end(), [&](const nlohmann::json& q) {
                const auto& c = q["box"];
                const float ix = std::max(0.0f, std::min(b[0].get<float>() + b[2].get<float>(), c[0].get<float>() + c[2].get<float>()) -
                                                    std::max(b[0].get<float>(), c[0].get<float>()));
                const float iy = std::max(0.0f, std::min(b[1].get<float>() + b[3].get<float>(), c[1].get<float>() + c[3].get<float>()) -
                                                    std::max(b[1].get<float>(), c[1].get<float>()));
                return ix * iy > 0.3f * std::min(b[2].get<float>() * b[3].get<float>(), c[2].get<float>() * c[3].get<float>());
            });
            if (!overlaps) seen.push_back(p);
        }
        if (seen.empty()) {
            (*target)["status"] = archway ? "skipped" : "failed";
            (*target)["error"] = archway ? "an open archway: no door to animate" : "no door found in its picture: draw it (click its picture)";
            AnimateNext();
            return;
        }
        const nlohmann::json best = seen.front();
        (*target)["door"] = 0;
        (*target)["reset"] = true;
        (*target)["leaves"] = best.value("leaves", 0);
        {
            // The building's other doors this finding saw: targets of their own, after this one (earlier ones gone).
            auto& list = animate_["targets"];
            const std::string base = animateKey_;
            list.erase(std::remove_if(list.begin(), list.end(), [&](const nlohmann::json& t) { return t.value("key", "").rfind(base + "#", 0) == 0; }),
                       list.end());
            auto at = std::find_if(list.begin(), list.end(), [&](const nlohmann::json& t) { return t.value("key", "") == base; });
            const nlohmann::json model = *at;
            const std::ptrdiff_t index = at - list.begin();
            for (size_t i = 1; i < seen.size(); ++i) {
                nlohmann::json t = model;
                t["key"] = base + "#" + std::to_string(i);
                t["label"] = model.value("label", std::string("Door")) + " · " + seen[i].value("label", "door " + std::to_string(i + 1));
                t["door"] = i;
                t["reset"] = false;
                t["given"] = seen[i]["box"];
                t["leaves"] = seen[i].value("leaves", 0);
                t["status"] = "pending";
                for (const char* stale : {"preview", "sheet", "found", "error", "drawn", "box"}) t.erase(stale);
                list.insert(list.begin() + index + static_cast<std::ptrdiff_t>(i), t);
            }
            for (auto& t : list) {
                if (t.value("key", "") == base) target = &t; // the list moved under it
            }
        }
        // Opened from its own pixels (the service's app/doors.py: the doorway fitted to the picture, the panel swung
        // inward behind the wall), not drawn by an animator: it keeps the building's art and costs nothing.
        (*target)["found"] = best["box"];
        (*target)["box"] = best["box"];
        (*target)["status"] = "animating";
        PostAnimate();
        animateRequest_ = Ai()->Request("POST", "/parts/door", DoorRequest(build_->Id(), *target, best["box"], false), 120);
        return;
    }
    if (!body.value("ok", false)) return fail(body.value("errors", nlohmann::json::array()).dump());
    RefreshAssets(); // the spritesheet the service just wrote
    if (body.contains("doorway")) (*target)["box"] = body["doorway"]; // a door: its frames go over the doorway
    std::string sheet;
    if (const std::string why = MakeAnimationSheet(body, &sheet); !why.empty()) return fail(why);
    session_.ReloadAnimations();
    (*target)["sheet"] = sheet;
    const std::string clip = body.value("clip", std::string("open"));
    const std::string closeClip = clip == "open" ? "close" : "";
    const nlohmann::json region = (*target)["region"];
    if ((*target)["kind"] == "door" && target->contains("prefab")) {
        // A building's door: an AnimatedPart on its prefab, the box in its frame's pixels.
        const nlohmann::json box = (*target)["box"];
        AnimatedPart part;
        part.sheet = sheet;
        part.x = box[0].get<float>() - region[0].get<float>() + box[2].get<float>() / 2;
        part.y = box[1].get<float>() - region[1].get<float>() + box[3].get<float>() / 2;
        part.width = box[2].get<float>(), part.height = box[3].get<float>();
        part.clip = clip, part.closeClip = closeClip;
        ApplyDoorToPrefab((*target)["prefab"], target->value("door", 0), part, target->value("reset", true));
    } else if ((*target)["kind"] == "door") {
        // A painted door: kept with the build, put over the painting when the place is built (AddPaintingEntities).
        nlohmann::json parts = ReadJsonFile(build_->Folder() / "parts.json");
        if (!parts.is_object()) parts = {{"parts", nlohmann::json::array()}};
        auto& list = parts["parts"];
        // Its building's doors drawn anew (reset): the others it had gone too.
        const std::string base = animateKey_.substr(0, animateKey_.find('#'));
        const bool reset = target->value("reset", false);
        list.erase(std::remove_if(list.begin(), list.end(), [&](const nlohmann::json& p) {
                       const std::string key = p.value("key", "");
                       return key == animateKey_ || (reset && key.rfind(base + "#", 0) == 0);
                   }),
                   list.end());
        list.push_back({{"key", animateKey_}, {"box", (*target)["box"]}, {"sheet", sheet}, {"texture", body.value("sheet", "")},
                        {"clip", clip}, {"closeClip", closeClip}, {"label", (*target).value("label", "Door")}});
        std::ofstream(build_->Folder() / "parts.json") << parts.dump(2) << "\n";
    } else {
        // A thing that's used: its prefab plays the sheet (a prop), opened when used (its Interactable's verb).
        const std::string prefab = (*target)["prefab"];
        const UsedThing* used = UsedFor((*target).value("role", ""));
        prefabs_.VisitTemplate(prefab, [&](Registry& r, Entity t) { // added to its loaded template, then written out with it
            auto& animator = r.get_or_emplace<Animator>(t);
            animator.sheet = sheet, animator.animator = "prop";
            auto& thing = r.get_or_emplace<Interactable>(t);
            if (used) thing.verb = used->verb;
            thing.animation = clip;
        });
        if (const auto error = prefabs_.SetTemplateField(prefab, "Animator", "sheet", FieldValue{sheet})) return fail(*error);
        RefreshPrefabs();
    }
    // What the Animate tab plays for it: its spritesheet (the frames in a row), how many, how fast.
    (*target)["preview"] = {{"sheet", body.value("sheet", std::string())},
                            {"count", body.value("frames", nlohmann::json::array()).size()},
                            {"fps", body.value("fps", 8.0)}};
    (*target)["status"] = "done";
    AnimateNext();
}

void EditorApp::ApplyDoorToPrefab(const std::string& prefab, size_t index, const AnimatedPart& door, bool reset, bool keepWays) {
    // Its door `index` (the rest kept, or none with `reset`), on the prefab: every copy, here and in the other scenes.
    const auto failed = prefabs_.EditTemplate(prefab, [&](Registry& r, Entity t) {
        std::vector<AnimatedPart> doors;
        if (const auto* now = r.try_get<AnimatedPart>(t); now && !reset && !now->sheet.empty()) doors = DoorsOf(*now);
        AnimatedPart made = door;
        if (index < doors.size() && !keepWays) {
            doors[index] = made;
        } else if (index < doors.size()) {
            // Redone: its ways (how it opens, its lock) kept.
            made.opens = doors[index].opens, made.locked = doors[index].locked, made.key = doors[index].key;
            made.consumeKey = doors[index].consumeKey, made.range = doors[index].range;
            doors[index] = made;
        } else {
            doors.push_back(made);
        }
        SetDoors(r.get_or_emplace<AnimatedPart>(t), doors);
    });
    if (failed) {
        post_({{"type", "notify"}, {"text", "Couldn't change " + prefab + ": " + *failed}});
        return;
    }
    prefabs_.SyncAll(registry_, prefab);
    RefreshPrefabs();
}

void EditorApp::ApplyPartToPrefab(const std::string& prefab, const AnimatedPart& part) {
    // Every copy of the prefab gets it (here and in the other scenes, as they're opened).
    prefabs_.VisitTemplate(prefab, [&](Registry& r, Entity t) { r.emplace_or_replace<AnimatedPart>(t, part); });
    if (const auto error = prefabs_.SetTemplateField(prefab, "AnimatedPart", "sheet", FieldValue{part.sheet})) {
        post_({{"type", "notify"}, {"text", "Couldn't change " + prefab + ": " + *error}});
        return;
    }
    RefreshPrefabs();
}

} // namespace aige
