// Animate the interior (docs/places/interior-objects.md): a building's interior pictures (each floor's cutaway) looked
// at by the vision model for the interior objects the ontology says such a building holds (roles.json found_in, under
// interior-object), how each moves written by the LLM from the ontology's animations (animate), checked and edited by
// the user, then each animated (/animations/generate: its box cut from its floor's picture) and put on the building as
// a part on that floor (AnimatedPart, floor k: shown while that floor shows), on its prefab so every copy has it.

#include "EditorApp.h"

#include "assets/Prefab.h"
#include "assets/TextureCook.h"
#include "gameplay/Components.h"
#include "gameplay/Interiors.h"
#include "places/Ontology.h"
#include "renderer2d/Components.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>

namespace aige {

nlohmann::json EditorApp::InteriorPartCandidates(const std::string& buildingRole) {
    // The interior objects that move and that such a building holds; one whose kind isn't known: every one that moves.
    const Ontology& ontology = Places();
    const bool known = !buildingRole.empty() && buildingRole != "building";
    nlohmann::json out = nlohmann::json::array();
    for (const Role* r : ontology.InteriorObjects(known ? buildingRole : std::string(), true)) {
        out.push_back({{"id", r->id}, {"label", r->label}, {"draw", r->draw}, {"animate", ontology.RoleAnimations(r->id)}});
    }
    return out;
}

void EditorApp::PostInteriorParts() {
    int done = 0, failed = 0, chosen = 0;
    for (const auto& p : interiorParts_.value("parts", nlohmann::json::array())) {
        done += p.value("status", "") == "done";
        failed += p.value("status", "") == "failed";
        chosen += p.value("animate", false) && p.value("status", "") != "done";
    }
    interiorParts_["done"] = done, interiorParts_["failed"] = failed, interiorParts_["chosen"] = chosen;
    nlohmann::json message = interiorParts_;
    message["type"] = "interiorParts:state";
    post_(message);
}

bool EditorApp::HandleInteriorPartsMessage(const std::string& type, const nlohmann::json& message) {
    auto findPart = [&](const std::string& key) -> nlohmann::json* {
        for (auto& p : interiorParts_["parts"]) {
            if (p.value("key", "") == key) return &p;
        }
        return nullptr;
    };
    if (type == "interiorParts:open") {
        // The building's floors (each floor's picture, the building's frame in it), and the parts it has on them already.
        const Entity e = static_cast<Entity>(message.value("id", 0u));
        const auto* sprite = registry_.valid(e) ? registry_.try_get<Sprite>(e) : nullptr;
        const auto* inside = registry_.valid(e) ? registry_.try_get<Interior>(e) : nullptr;
        if (!sprite || !inside || InteriorFloors(*inside) == 0 || InteriorPicture(*inside, 1).empty()) {
            post_({{"type", "notify"}, {"text", "It has no cutaway interior to animate (Add interior: cutaway first)"}});
            post_({{"type", "interiorParts:state"}, {"id", nullptr}});
            return true;
        }
        const bool same = interiorParts_.value("id", 0u) == static_cast<uint32_t>(e) && interiorParts_.contains("parts");
        if (!same && interiorPartsRequest_ != 0) {
            // One building at a time: the one being worked on is shown until it's done.
            post_({{"type", "notify"}, {"text", "Another building's interior is being animated: " + interiorParts_.value("name", std::string("wait for it"))}});
            post_({{"type", "interiorParts:show"}, {"id", interiorParts_.value("id", 0u)}});
            PostInteriorParts();
            return true;
        }
        const std::string exterior = inside->shown ? inside->exteriorTexture : sprite->texture;
        const auto role = BuildingRoleOf(exterior);
        const Ontology& ontology = Places();
        const Role* r = role && !role->empty() ? ontology.FindRole(*role) : nullptr;
        nlohmann::json floors = nlohmann::json::array();
        const float u0 = std::min(sprite->u0, sprite->u1), u1 = std::max(sprite->u0, sprite->u1);
        for (int k = 1; k <= InteriorFloors(*inside); ++k) {
            const std::string texture = InteriorPicture(*inside, k);
            const AssetInfo* asset = assets_.Find(texture);
            if (!asset || asset->width == 0) continue;
            const float W = static_cast<float>(asset->width), H = static_cast<float>(asset->height);
            floors.push_back({{"floor", k}, {"texture", texture}, {"size", {asset->width, asset->height}},
                              {"region", {std::lround(u0 * W), std::lround(sprite->v0 * H), std::lround((u1 - u0) * W),
                                          std::lround((sprite->v1 - sprite->v0) * H)}}});
        }
        nlohmann::json parts = same ? interiorParts_["parts"] : nlohmann::json::array();
        if (!same) {
            // What it has on its floors already: listed as done (redone when a part's box is drawn over one).
            if (const auto* had = registry_.try_get<AnimatedPart>(e); had && (!had->sheet.empty() || !had->more.empty())) {
                int n = 0;
                for (const auto& d : DoorsOf(*had)) {
                    if (d.floor <= 0 || d.sheet.empty()) continue;
                    nlohmann::json region;
                    for (const auto& f : floors) {
                        if (f.value("floor", 0) == d.floor) region = f["region"];
                    }
                    if (region.is_null()) continue;
                    parts.push_back({{"key", "had-" + std::to_string(n++)}, {"floor", d.floor}, {"kind", ""}, {"label", "animated before (" + d.clip + ")"},
                                     {"box", {std::lround(region[0].get<float>() + d.x - d.width / 2), std::lround(region[1].get<float>() + d.y - d.height / 2),
                                              std::lround(d.width), std::lround(d.height)}},
                                     {"clip", d.clip}, {"opens", d.opens}, {"prompt", d.prompt}, {"duration", d.duration},
                                     {"motion", ""}, {"animate", false}, {"status", "done"}, {"had", true}});
                }
            }
        }
        const auto* name = registry_.try_get<Name>(e);
        interiorParts_ = {{"id", static_cast<uint32_t>(e)}, {"name", name ? name->value : std::string("the building")},
                          {"role", role ? *role : std::string()}, {"building", r ? r->label : std::string("building")},
                          {"description", role && !role->empty() ? ontology.RoleDescription(*role) : std::string()},
                          {"floors", floors}, {"parts", parts}, {"running", interiorPartsRequest_ != 0},
                          {"candidates", InteriorPartCandidates(role ? *role : std::string())},
                          {"status", same ? interiorParts_.value("status", std::string()) : std::string()}};
        PostInteriorParts();
        return true;
    }
    if (!interiorParts_.contains("parts")) return true;
    if (type == "interiorParts:find") {
        // ① Each floor looked at for the things such a building holds that move, ② how each moves written.
        if (interiorPartsRequest_ != 0 || !Ai() || Ai()->GetState() != AiClient::State::Ready) {
            post_({{"type", "notify"}, {"text", interiorPartsRequest_ ? "The interior is still being worked on" : "Finding an interior's things needs AI on"}});
            return true;
        }
        nlohmann::json floors = nlohmann::json::array(), rects = nlohmann::json::array();
        for (const auto& f : interiorParts_["floors"]) floors.push_back(f["texture"]), rects.push_back(f["region"]);
        if (floors.empty() || interiorParts_["candidates"].empty()) {
            post_({{"type", "notify"}, {"text", floors.empty() ? "Its interior pictures aren't in the project" : "The ontology names no moving things for it"}});
            return true;
        }
        interiorPartKey_.clear();
        interiorParts_["status"] = "finding";
        interiorParts_["running"] = true;
        interiorPartsRequest_ = Ai()->Request("POST", "/interiors/parts",
                                              {{"floors", floors}, {"rects", rects}, {"building", interiorParts_.value("building", "")},
                                               {"description", interiorParts_.value("description", "")}, {"candidates", interiorParts_["candidates"]}},
                                              600);
        PostInteriorParts();
        return true;
    }
    if (type == "interiorParts:set") {
        // A part's motion, clip, what plays it, its box, or whether it's animated: as the user changed it.
        if (auto* p = findPart(message.value("key", ""))) {
            for (const char* field : {"motion", "clip", "opens", "prompt", "duration", "animate", "box", "kind"}) {
                if (message.contains(field)) (*p)[field] = message[field];
            }
            if (p->value("status", "") == "failed" && message.contains("animate")) p->erase("error"), (*p)["status"] = "found";
        }
        PostInteriorParts();
        return true;
    }
    if (type == "interiorParts:add") {
        // A box drawn by hand on a floor: a part of that kind, moving as the ontology says (edited after).
        const std::string kind = message.value("kind", "");
        nlohmann::json motion = {{"clip", "idle"}, {"motion", "it moves gently in place"}, {"opens", "always"}};
        for (const auto& c : interiorParts_["candidates"]) {
            if (c.value("id", "") == kind && !c["animate"].empty()) motion = c["animate"][0];
        }
        static int added = 0;
        interiorParts_["parts"].push_back({{"key", "drawn-" + std::to_string(++added)}, {"floor", message.value("floor", 1)}, {"kind", kind},
                                           {"label", message.value("label", kind)}, {"box", message.value("box", nlohmann::json::array())},
                                           {"motion", motion.value("motion", "")}, {"clip", motion.value("clip", "idle")},
                                           {"opens", motion.value("opens", "always")}, {"prompt", motion.value("prompt", "")},
                                           {"duration", motion.value("duration", 0.0)}, {"animate", true}, {"status", "found"}, {"source", "drawn"}});
        PostInteriorParts();
        return true;
    }
    if (type == "interiorParts:remove") {
        auto& list = interiorParts_["parts"];
        list.erase(std::remove_if(list.begin(), list.end(), [&](const nlohmann::json& p) { return p.value("key", "") == message.value("key", ""); }),
                   list.end());
        PostInteriorParts();
        return true;
    }
    if (type == "interiorParts:run") {
        // ③ Each part chosen (or the one asked) animated, one at a time.
        const std::string only = message.value("key", "");
        for (auto& p : interiorParts_["parts"]) {
            if (p.value("had", false)) continue;
            const std::string status = p.value("status", "");
            if (!only.empty() ? p.value("key", "") == only : p.value("animate", false) && (status == "found" || status == "failed")) {
                p["status"] = "queued";
                p.erase("error");
            }
        }
        interiorParts_["running"] = true;
        if (interiorPartsRequest_ == 0) InteriorPartsNext();
        else PostInteriorParts();
        return true;
    }
    if (type == "interiorParts:stop") {
        for (auto& p : interiorParts_["parts"]) {
            if (p.value("status", "") == "queued") p["status"] = "found";
        }
        interiorParts_["running"] = interiorPartsRequest_ != 0; // the one in flight finishes
        PostInteriorParts();
        return true;
    }
    return true;
}

void EditorApp::InteriorPartsNext() {
    const Entity e = static_cast<Entity>(interiorParts_.value("id", 0u));
    if (!registry_.valid(e) || !Ai() || Ai()->GetState() != AiClient::State::Ready) {
        for (auto& p : interiorParts_["parts"]) {
            if (p.value("status", "") == "queued") p["status"] = "failed", p["error"] = registry_.valid(e) ? "AI is off" : "the building is gone";
        }
        interiorParts_["running"] = false;
        PostInteriorParts();
        return;
    }
    for (auto& p : interiorParts_["parts"]) {
        if (p.value("status", "") != "queued") continue;
        interiorPartKey_ = p.value("key", "");
        auto fail = [&](const std::string& why) {
            p["status"] = "failed", p["error"] = why;
        };
        // Its box cut out of its floor's picture.
        std::string texture;
        for (const auto& f : interiorParts_["floors"]) {
            if (f.value("floor", 0) == p.value("floor", 0)) texture = f.value("texture", "");
        }
        const auto image = texture.empty() ? std::nullopt : DecodeImage(assets_.GetProject().Resolve(texture));
        const nlohmann::json box = p.value("box", nlohmann::json::array());
        if (!image || box.size() != 4) {
            fail("its floor's picture can't be read");
            continue;
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
        std::string stem = std::filesystem::path(texture).stem().string() + "-" + p.value("kind", std::string("part"));
        for (char& c : stem) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-') c = '-';
        }
        int n = 1;
        std::string path;
        do {
            path = "assets/textures/generated/parts/" + stem + "-" + std::to_string(n++) + ".png";
        } while (std::filesystem::exists(assets_.GetProject().Resolve(path)));
        std::error_code ec;
        std::filesystem::create_directories(assets_.GetProject().Resolve("assets/textures/generated/parts"), ec);
        if (!WritePng(assets_.GetProject().Resolve(path), crop)) {
            fail("can't write its picture");
            continue;
        }
        RefreshAssets();
        p["crop"] = path;
        p["status"] = "animating";
        // A thing's animator (Preferences ▸ AI ▸ Non-humanoid animation: PixelLab, or FLUX video), from its motion.
        interiorPartsRequest_ = Ai()->Request("POST", "/animations/generate",
                                              {{"path", path}, {"action", p.value("motion", std::string("it moves gently in place"))},
                                               {"clip", p.value("clip", std::string("idle"))},
                                               {"description", p.value("label", p.value("kind", std::string("a thing")))},
                                               {"provider", ChosenAi("animate")},
                                               {"objectProvider", ChosenAi("objectAnimate")},
                                               {"thing", true}, {"name", std::filesystem::path(path).stem().string()}},
                                              900);
        PostInteriorParts();
        return;
    }
    interiorPartKey_.clear();
    interiorParts_["running"] = false; // nothing left
    const int done = static_cast<int>(std::count_if(interiorParts_["parts"].begin(), interiorParts_["parts"].end(),
                                                    [](const nlohmann::json& p) { return p.value("status", "") == "done" && !p.value("had", false); }));
    if (done > 0) interiorParts_["status"] = "animated";
    PostInteriorParts();
}

void EditorApp::OnInteriorPartsResult(const nlohmann::json& body) {
    interiorPartsRequest_ = 0;
    if (interiorPartKey_.empty()) {
        // ①② What the vision model found on each floor and how the LLM says each moves: shown to be checked.
        if (!body.value("ok", false)) {
            interiorParts_["status"] = "failed";
            interiorParts_["error"] = body.value("errors", nlohmann::json::array()).empty() ? std::string("no answer")
                                                                                              : body["errors"][0].get<std::string>();
        } else {
            auto& list = interiorParts_["parts"];
            // Found again: what was found before and not animated goes; what's done stays.
            list.erase(std::remove_if(list.begin(), list.end(), [](const nlohmann::json& p) { return p.value("status", "") != "done"; }), list.end());
            int n = 0;
            for (auto p : body.value("parts", nlohmann::json::array())) {
                p["key"] = "found-" + std::to_string(++n);
                p["status"] = "found";
                list.push_back(p);
            }
            interiorParts_["status"] = n ? "found" : "none";
            interiorParts_["warnings"] = body.value("warnings", nlohmann::json::array());
            interiorParts_.erase("error");
        }
        interiorParts_["running"] = false;
        PostInteriorParts();
        return;
    }
    nlohmann::json* part = nullptr;
    for (auto& p : interiorParts_["parts"]) {
        if (p.value("key", "") == interiorPartKey_) part = &p;
    }
    const Entity e = static_cast<Entity>(interiorParts_.value("id", 0u));
    auto next = [&] {
        if (interiorParts_.value("running", false)) InteriorPartsNext();
        else PostInteriorParts();
    };
    if (!part) return next();
    if (!body.value("ok", false)) {
        (*part)["status"] = "failed";
        (*part)["error"] = body.value("errors", nlohmann::json::array()).empty() ? std::string("no answer") : body["errors"][0].get<std::string>();
        return next();
    }
    RefreshAssets(); // the spritesheet the service just wrote
    std::string sheet;
    if (const std::string why = MakeAnimationSheet(body, &sheet); !why.empty()) {
        (*part)["status"] = "failed", (*part)["error"] = why;
        return next();
    }
    session_.ReloadAnimations();
    if (!registry_.valid(e)) {
        (*part)["status"] = "failed", (*part)["error"] = "the building is gone";
        return next();
    }
    // On its floor, the box in the building's frame (its region of the floor's picture).
    nlohmann::json region = {0, 0, 0, 0};
    for (const auto& f : interiorParts_["floors"]) {
        if (f.value("floor", 0) == part->value("floor", 0)) region = f["region"];
    }
    const nlohmann::json box = (*part)["box"];
    AnimatedPart made;
    made.sheet = sheet;
    made.floor = part->value("floor", 1);
    made.door = false; // a floor's things are no way in
    made.x = box[0].get<float>() - region[0].get<float>() + box[2].get<float>() / 2;
    made.y = box[1].get<float>() - region[1].get<float>() + box[3].get<float>() / 2;
    made.width = box[2].get<float>(), made.height = box[3].get<float>();
    made.clip = body.value("clip", part->value("clip", std::string("idle")));
    made.closeClip = made.clip == "open" ? "close" : "";
    const std::string opens = part->value("opens", std::string("always"));
    made.opens = opens == "use" || opens == "auto" ? opens : "always";
    if (made.opens == "use") {
        made.prompt = part->value("prompt", std::string());
        made.duration = part->value("duration", 0.0f);
    }
    PutPart(e, made);
    inspectorStale_ = true;
    (*part)["sheet"] = sheet;
    (*part)["preview"] = {{"sheet", body.value("sheet", std::string())}, {"count", body.value("frames", nlohmann::json::array()).size()},
                          {"fps", body.value("fps", 8.0)}};
    (*part)["status"] = "done";
    (*part)["animate"] = false;
    next();
}

} // namespace aige
