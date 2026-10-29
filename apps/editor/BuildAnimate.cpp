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
#include "editing/PlaceScene.h"
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
nlohmann::json DoorRequest(const std::string& buildId, const nlohmann::json& target, const nlohmann::json& box, bool exact,
                           const std::string& animator) {
    // Its own name: the build, its building (the prefab's or painted object's name) and which of its doors, kept from
    // the end when it's long (each door's frames in their own folder: cut at the front, every door shared one).
    const std::string key = target.value("key", std::string("door"));
    const size_t hash = key.find('#');
    std::string building = key.substr(0, hash);
    building = std::filesystem::path(building).stem().string();
    if (const size_t colon = building.rfind(':'); colon != std::string::npos) building = building.substr(colon + 1);
    std::string name = buildId + "-" + building + "-door" + (hash == std::string::npos ? std::string() : "-" + key.substr(hash + 1));
    for (char& ch : name) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-') ch = '-';
    }
    if (name.size() > 60) name = name.substr(name.size() - 60);
    return {{"path", target.value("texture", "")}, {"box", box}, {"name", name}, {"exact", exact},
            {"leaves", target.value("leaves", 0)}, {"animator", animator}};
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
        const std::string was = extra.value("status", "");
        if (was != "done" && was != "skipped") extra["status"] = extra.contains("drawn") || extra.contains("given") ? "identified" : "pending";
        targets.insert(std::find_if(base, targets.end(), [&](const nlohmann::json& t) {
                           return t.value("key", "").rfind(key.substr(0, hash), 0) != 0;
                       }),
                       extra);
    }
    for (auto& t : targets) {
        for (const auto& b : before.value("targets", nlohmann::json::array())) {
            if (b.value("key", "") != t.value("key", "")) continue;
            const std::string was = b.value("status", "");
            const bool identified = b.contains("drawn") || b.contains("given");
            if (was == "done" || was == "skipped") t["status"] = b["status"];
            else if (identified) t["status"] = "identified";
            else continue;
            for (const char* kept : {"box", "sheet", "preview", "drawn", "given", "door", "reset", "leaves", "error", "open"}) {
                if (b.contains(kept)) t[kept] = b[kept];
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
    // Build ▸ Animate (docs/assets/animations.md §4.1), in two steps: ① Identify doors (each building's doors found
    // in its picture, shown to be checked, drawn by hand where they're off), ② Animate (each door identified, and each
    // thing that's used, animated and applied).
    if (type == "build:animateScan") {
        animate_ = {{"targets", AnimateTargets()}, {"running", animateRequest_ != 0}};
        PostAnimate();
        return true;
    }
    if (type == "build:animateSkip" || type == "build:animateUnskip") {
        for (auto& t : animate_["targets"]) {
            if (t.value("key", "") != message.value("key", "")) continue;
            const bool ready = t.value("kind", "") == "door" && (t.contains("drawn") || t.contains("given"));
            t["status"] = type == "build:animateSkip" ? "skipped" : ready ? "identified" : "pending";
        }
        PostAnimate();
        return true;
    }
    if (type == "build:animateSetDoors") {
        // Boxes drawn by hand round a building's doors: its doors are those, identified, one target each (§4.3).
        const std::string base = message.value("key", std::string()).substr(0, message.value("key", std::string()).find('#'));
        const nlohmann::json boxes = message.value("boxes", nlohmann::json::array());
        const bool openings = message.value("open", false); // open doorways: entry points, nothing to animate
        auto& list = animate_["targets"];
        auto first = std::find_if(list.begin(), list.end(), [&](const nlohmann::json& t) { return t.value("key", "") == base; });
        if (first == list.end() || boxes.empty()) return true;
        const nlohmann::json model = *first;
        list.erase(std::remove_if(list.begin(), list.end(), [&](const nlohmann::json& t) { return t.value("key", "").rfind(base + "#", 0) == 0; }),
                   list.end());
        first = std::find_if(list.begin(), list.end(), [&](const nlohmann::json& t) { return t.value("key", "") == base; });
        const size_t at = static_cast<size_t>(first - list.begin());
        for (size_t i = 0; i < boxes.size(); ++i) {
            nlohmann::json t = model;
            t["key"] = i == 0 ? base : base + "#" + std::to_string(i);
            if (i > 0) t["label"] = model.value("label", std::string("Door")) + " · door " + std::to_string(i + 1);
            t["door"] = i;
            t["reset"] = i == 0;
            t["drawn"] = boxes[i];
            t["box"] = boxes[i];
            t["open"] = openings;
            t["status"] = "identified";
            for (const char* stale : {"preview", "sheet", "found", "error", "given"}) t.erase(stale);
            if (i == 0) list[at] = t;
            else list.insert(list.begin() + static_cast<std::ptrdiff_t>(at + i), t);
        }
        PostAnimate();
        return true;
    }
    if (type == "build:animateIdentify" || type == "build:animateRun") {
        // ① its doors looked for (all buildings, or one: found again), or ② what's ready animated (all, or one).
        const bool identify = type == "build:animateIdentify";
        const std::string only = message.value("key", "");
        const bool again = !identify && message.value("again", false); // everything done animated again too
        for (auto& t : animate_["targets"]) {
            const std::string key = t.value("key", ""), status = t.value("status", "");
            const bool door = t.value("kind", "") == "door";
            if (!only.empty() && key != only) continue;
            if (identify && door && (!only.empty() ? status != "skipped" : status == "pending" || status == "failed")) {
                t["status"] = "pending";
                for (const char* stale : {"drawn", "given", "box", "found", "error"}) t.erase(stale);
            }
            if (!identify && (!only.empty() || status == "failed" || (again && status == "done")) && status != "skipped") {
                // A door animated before it had an identified box: the doorway it was fitted to, used as drawn.
                if (door && !t.contains("drawn") && !t.contains("given") && t.contains("box")) t["drawn"] = t["box"];
                const bool ready = !door || t.contains("drawn") || t.contains("given");
                if (ready) t["status"] = door ? "identified" : "pending";
            }
        }
        animate_["running"] = true;
        animate_["mode"] = identify ? "identify" : "animate";
        animate_["only"] = only;
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
    const bool identify = animate_.value("mode", std::string("animate")) == "identify";
    const std::string only = animate_.value("only", "");
    for (auto& t : animate_["targets"]) {
        if (!only.empty() && t.value("key", "") != only) continue;
        const std::string status = t.value("status", "");
        const bool door = t.value("kind", "") == "door";
        animateKey_ = t.value("key", "");
        if (identify) {
            if (!door || status != "pending") continue;
            // ① Its doors looked for in its picture (or its part of the painting).
            t["status"] = "finding";
            animateRequest_ = Ai()->Request("POST", "/parts/find",
                                            {{"path", t.value("texture", "")}, {"kinds", {"door", "gate"}}, {"rect", t.value("region", nlohmann::json::array())}}, 180);
        } else if (door && status == "identified" && t.value("open", false)) {
            // ② An open doorway: its entry point on the building (always passable), nothing to animate.
            const bool drawn = t.contains("drawn");
            t["box"] = drawn ? t["drawn"] : t["given"];
            const nlohmann::json region = t["region"], box = t["box"];
            AnimatedPart opening;
            opening.opens = "open";
            opening.x = box[0].get<float>() - region[0].get<float>() + box[2].get<float>() / 2;
            opening.y = box[1].get<float>() - region[1].get<float>() + box[3].get<float>() / 2;
            opening.width = box[2].get<float>(), opening.height = box[3].get<float>();
            if (t.contains("prefab")) ApplyDoorToPrefab(t["prefab"], t.value("door", 0), opening, t.value("reset", true), false);
            t["status"] = "done";
            t.erase("preview");
            continue;
        } else if (door && status == "identified") {
            // ② Opened: from its box as drawn by hand, else fitted to the picture round the one found.
            const bool drawn = t.contains("drawn");
            t["box"] = drawn ? t["drawn"] : t["given"];
            t["status"] = "animating";
            // Preferences ▸ AI ▸ Non-humanoid animation: PixelLab or FLUX video.
            const std::string animator = userPrefs_ ? userPrefs_->Get("objectAnimateProvider", "pixellab").get<std::string>() : "pixellab";
            animateRequest_ = Ai()->Request("POST", "/parts/door", DoorRequest(build_->Id(), t, t["box"], drawn, animator), 900);
        } else if (!door && status == "pending") {
            t["status"] = "animating";
            AnimateCrop(t, t.value("region", nlohmann::json::array()));
        } else {
            continue;
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
                                     {"objectProvider", userPrefs_ ? userPrefs_->Get("objectAnimateProvider", "pixellab") : nlohmann::json("pixellab")},
                                     {"thing", true}, // a chest, a gate, a lever: the thing's animator (PixelLab), not a character's
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
        for (const auto& p : body.value("parts", nlohmann::json::array())) {
            // An open doorway (an archway, no door in it) is kept too: its entry point, nothing to animate (§4.5).
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
            (*target)["status"] = "failed";
            (*target)["error"] = "no door found in its picture: draw its doors (click its picture)";
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
                t["box"] = seen[i]["box"];
                t["leaves"] = seen[i].value("leaves", 0);
                t["open"] = seen[i].value("open", false);
                t["status"] = "identified";
                for (const char* stale : {"preview", "sheet", "found", "error", "drawn"}) t.erase(stale);
                list.insert(list.begin() + index + static_cast<std::ptrdiff_t>(i), t);
            }
            for (auto& t : list) {
                if (t.value("key", "") == base) target = &t; // the list moved under it
            }
        }
        // Shown to be checked (② Animate opens it): its box as found.
        (*target)["given"] = best["box"];
        (*target)["box"] = best["box"];
        (*target)["open"] = best.value("open", false);
        (*target)["status"] = "identified";
        for (const char* stale : {"preview", "sheet", "error", "drawn"}) target->erase(stale);
        AnimateNext();
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
        if (const auto* now = r.try_get<AnimatedPart>(t); now && !reset && (!now->sheet.empty() || now->opens == "open" || !now->more.empty())) {
            doors = DoorsOf(*now);
        }
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
    // Its picture keeps them too (§4.4): every prefab of it, in this project or another (through a library), opens so.
    std::string texture;
    std::vector<AnimatedPart> doors;
    bool whole = false;
    prefabs_.VisitTemplate(prefab, [&](Registry& r, Entity t) {
        if (const auto* s = r.try_get<Sprite>(t)) {
            texture = s->texture;
            whole = std::min(s->u0, s->u1) <= 0.001f && s->v0 <= 0.001f && std::max(s->u0, s->u1) >= 0.999f && s->v1 >= 0.999f;
        }
        if (const auto* part = r.try_get<AnimatedPart>(t)) doors = DoorsOf(*part);
    });
    if (whole && !texture.empty()) SetAssetDoors(assets_.GetProject(), texture, doors);
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
