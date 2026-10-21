#include "gameplay/Cutscene.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>

namespace aige {

namespace fs = std::filesystem;

namespace {

const std::map<std::string, std::set<std::string>> kOps = {
    {"actor", {"move", "face", "show", "hide", "place", "tween", "animate"}},
    {"dialogue", {"line"}},
    {"camera", {"follow", "move", "shake", "zoom"}},
    {"audio", {"music", "sound"}},
    {"screen", {"fade_out", "fade_in", "title", "letterbox"}},
    {"logic", {"do", "emit", "wait_input"}},
    {"effect", {"play"}},
};

} // namespace

float Cutscene::Length() const {
    if (length > 0.0f) return length;
    float end = 0.0f;
    for (const auto& track : tracks) {
        for (const auto& key : track.keys) end = std::max(end, key.at + key.seconds);
    }
    return end;
}

std::optional<Cutscene> CutsceneFromJson(const nlohmann::json& json, const std::string& id, std::string* error) {
    if (!json.is_object() || !json.contains("tracks") || !json["tracks"].is_array()) {
        if (error) *error = "needs a \"tracks\" list";
        return std::nullopt;
    }
    Cutscene cutscene;
    cutscene.id = id;
    cutscene.name = json.value("name", "");
    cutscene.letterbox = json.value("letterbox", true);
    cutscene.skippable = json.value("skippable", true);
    cutscene.length = json.value("length", 0.0f);
    for (const auto& t : json["tracks"]) {
        if (!t.is_object()) continue;
        CutsceneTrack track;
        track.type = t.value("type", "");
        track.actor = t.value("actor", "");
        for (const auto& k : t.value("keys", nlohmann::json::array())) {
            if (!k.is_object()) continue;
            CutsceneKey key;
            key.at = std::max(0.0f, k.value("at", 0.0f));
            key.seconds = std::max(0.0f, k.value("seconds", 0.0f));
            key.op = k.value("op", track.type == "dialogue" ? "line" : track.type == "logic" && k.contains("do") ? "do" : "");
            key.data = k;
            key.data.erase("at");
            key.data.erase("seconds");
            key.data.erase("op");
            track.keys.push_back(std::move(key));
        }
        std::stable_sort(track.keys.begin(), track.keys.end(),
                         [](const CutsceneKey& a, const CutsceneKey& b) { return a.at < b.at; });
        cutscene.tracks.push_back(std::move(track));
    }
    return cutscene;
}

nlohmann::json CutsceneToJson(const Cutscene& cutscene) {
    nlohmann::json json = {{"name", cutscene.name}};
    if (!cutscene.letterbox) json["letterbox"] = false;
    if (!cutscene.skippable) json["skippable"] = false;
    if (cutscene.length > 0.0f) json["length"] = cutscene.length;
    nlohmann::json tracks = nlohmann::json::array();
    for (const auto& track : cutscene.tracks) {
        nlohmann::json t = {{"type", track.type}};
        if (!track.actor.empty()) t["actor"] = track.actor;
        nlohmann::json keys = nlohmann::json::array();
        for (const auto& key : track.keys) {
            nlohmann::json k = key.data.is_object() ? key.data : nlohmann::json::object();
            k["at"] = key.at;
            if (key.seconds > 0.0f) k["seconds"] = key.seconds;
            if (track.type != "dialogue") k["op"] = key.op;
            keys.push_back(k);
        }
        t["keys"] = keys;
        tracks.push_back(t);
    }
    json["tracks"] = tracks;
    return json;
}

std::vector<std::string> ValidateCutscene(const Cutscene& cutscene) {
    std::vector<std::string> problems;
    if (cutscene.tracks.empty()) problems.push_back("has no tracks");
    for (size_t i = 0; i < cutscene.tracks.size(); ++i) {
        const auto& track = cutscene.tracks[i];
        const std::string where = "track " + std::to_string(i + 1) + " (" + track.type + ")";
        auto ops = kOps.find(track.type);
        if (ops == kOps.end()) {
            problems.push_back(where + ": type should be actor, dialogue, camera, audio, screen, logic or effect");
            continue;
        }
        if (track.type == "actor" && track.actor.empty()) problems.push_back(where + " needs an actor");
        for (const auto& key : track.keys) {
            const std::string at = where + " at " + std::to_string(key.at).substr(0, std::to_string(key.at).find('.') + 2) + "s";
            if (!ops->second.contains(key.op)) {
                problems.push_back(at + ": unknown op '" + key.op + "'");
                continue;
            }
            const bool place = key.data.contains("marker") || (key.data.contains("x") && key.data.contains("y"));
            if ((key.op == "move" || key.op == "place") && !place) problems.push_back(at + ": " + key.op + " needs a marker or x and y");
            if (key.op == "line" && key.data.value("text", "").empty()) problems.push_back(at + ": a line needs text");
            if ((key.op == "sound" || key.op == "music") && !key.data.contains("path")) problems.push_back(at + ": needs a path");
            if (key.op == "emit" && key.data.value("event", "").empty()) problems.push_back(at + ": emit needs an event");
            if (key.op == "follow" && key.data.value("actor", "").empty()) problems.push_back(at + ": follow needs an actor");
        }
    }
    return problems;
}

std::optional<Cutscene> LoadCutscene(const fs::path& projectRoot, const std::string& id, std::string* error) {
    std::ifstream in(projectRoot / "cutscenes" / (id + ".json"), std::ios::binary);
    if (!in) {
        if (error) *error = "no cutscenes/" + id + ".json";
        return std::nullopt;
    }
    nlohmann::json json = nlohmann::json::parse(in, nullptr, false);
    if (json.is_discarded()) {
        if (error) *error = "cutscenes/" + id + ".json isn't JSON";
        return std::nullopt;
    }
    return CutsceneFromJson(json, id, error);
}

std::vector<std::string> ListCutscenes(const fs::path& projectRoot) {
    std::vector<std::string> ids;
    std::error_code ec;
    if (!fs::is_directory(projectRoot / "cutscenes", ec)) return ids;
    for (const auto& file : fs::directory_iterator(projectRoot / "cutscenes", ec)) {
        if (file.is_regular_file() && file.path().extension() == ".json") ids.push_back(file.path().stem().string());
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::optional<std::string> WriteCutsceneFile(const fs::path& projectRoot, const std::string& id,
                                             const nlohmann::json& content) {
    const bool safe = !id.empty() && std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
    if (!safe) return "file names are lowercase letters, digits, - and _";
    std::string error;
    auto cutscene = CutsceneFromJson(content, id, &error);
    if (!cutscene) return error;
    std::error_code ec;
    fs::create_directories(projectRoot / "cutscenes", ec);
    std::ofstream out(projectRoot / "cutscenes" / (id + ".json"), std::ios::binary | std::ios::trunc);
    if (!out) return "couldn't write cutscenes/" + id + ".json";
    out << CutsceneToJson(*cutscene).dump(2) << "\n";
    return std::nullopt;
}

// --- Player --------------------------------------------------------------------

void CutscenePlayer::Start(Cutscene cutscene) {
    cutscene_ = std::move(cutscene);
    order_.clear();
    for (size_t t = 0; t < cutscene_.tracks.size(); ++t) {
        for (size_t k = 0; k < cutscene_.tracks[t].keys.size(); ++k) order_.push_back({t, k});
    }
    std::stable_sort(order_.begin(), order_.end(), [this](const Entry& a, const Entry& b) {
        return cutscene_.tracks[a.track].keys[a.key].at < cutscene_.tracks[b.track].keys[b.key].at;
    });
    next_ = 0;
    time_ = 0.0f;
    waiting_ = false;
    active_ = true;
}

void CutscenePlayer::Update(float dt, const FireFn& fire) {
    if (!active_) return;
    if (!waiting_) time_ += dt;
    while (!waiting_ && next_ < order_.size()) {
        const Entry entry = order_[next_];
        const CutsceneTrack& track = cutscene_.tracks[entry.track];
        const CutsceneKey& key = track.keys[entry.key];
        if (key.at > time_) break;
        ++next_;
        if (key.op == "wait_input") {
            waiting_ = true;
            time_ = key.at; // the clock stops here
        }
        fire({&track, &key, false});
    }
    if (!waiting_ && next_ >= order_.size() && time_ >= cutscene_.Length()) active_ = false;
}

void CutscenePlayer::Skip(const FireFn& fire) {
    if (!active_) return;
    waiting_ = false;
    for (; next_ < order_.size(); ++next_) {
        const CutsceneTrack& track = cutscene_.tracks[order_[next_].track];
        fire({&track, &track.keys[order_[next_].key], true});
    }
    time_ = cutscene_.Length();
    active_ = false;
}

} // namespace aige
