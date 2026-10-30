#include "gameplay/Animation.h"

#include "gameplay/Components.h"
#include "gameplay/Interiors.h"
#include "gameplay/Items.h"
#include "gameplay/Settings.h"
#include "physics2d/Components.h"
#include "records/Records.h"
#include "renderer2d/Components.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <cmath>
#include <fstream>
#include <random>
#include <sstream>

namespace aige {

namespace fs = std::filesystem;

namespace {

const std::vector<std::string> kDirections = {"s", "se", "e", "ne", "n", "nw", "w", "sw"};

std::string Trim(std::string s) {
    s.erase(0, s.find_first_not_of(" \t"));
    s.erase(s.find_last_not_of(" \t") + 1);
    return s;
}

} // namespace

std::string DirectionName(float dx, float dy) {
    if (std::abs(dx) < 1e-4f && std::abs(dy) < 1e-4f) return "s";
    // Screen directions: +x east, +y south. 0 = south, counter-clockwise in eighths.
    const float angle = std::atan2(dx, dy); // 0 at south, +pi/2 at east
    int sector = static_cast<int>(std::lround(angle / (3.14159265f / 4.0f)));
    sector = (sector % 8 + 8) % 8;
    static const char* names[8] = {"s", "se", "e", "ne", "n", "nw", "w", "sw"};
    return names[sector];
}

// --- Clips and sheets ---------------------------------------------------------------------

const std::vector<AnimFrame>* AnimClip::FramesFor(const std::string& direction, bool* flip, std::string* used) const {
    if (flip) *flip = false;
    auto pick = [&](const std::string& d) -> const std::vector<AnimFrame>* {
        if (auto it = directions.find(d); it != directions.end() && !it->second.empty()) {
            if (used) *used = d;
            return &it->second;
        }
        if (auto m = mirrors.find(d); m != mirrors.end()) {
            if (auto it = directions.find(m->second); it != directions.end() && !it->second.empty()) {
                if (flip) *flip = true;
                if (used) *used = m->second;
                return &it->second;
            }
        }
        return nullptr;
    };
    if (auto* frames = pick(direction)) return frames;
    // The nearest direction it has, by steps around the compass.
    auto at = std::find(kDirections.begin(), kDirections.end(), direction);
    if (at != kDirections.end()) {
        const int i = static_cast<int>(at - kDirections.begin());
        for (int step = 1; step <= 4; ++step) {
            for (int sign : {1, -1}) {
                if (auto* frames = pick(kDirections[((i + sign * step) % 8 + 8) % 8])) return frames;
            }
        }
    }
    if (auto* frames = pick("")) return frames;
    for (const auto& [d, frames] : directions) {
        if (!frames.empty()) {
            if (used) *used = d;
            return &frames;
        }
    }
    return nullptr;
}

float AnimClip::Length(const std::string& direction) const {
    const auto* frames = FramesFor(direction, nullptr);
    float total = 0.0f;
    if (frames) {
        for (const auto& f : *frames) total += f.duration;
    }
    return total;
}

const AnimClip* AnimSheet::Clip(const std::string& name) const {
    auto it = clips.find(name);
    return it == clips.end() ? nullptr : &it->second;
}

bool AnimSheet::PointOn(const AnimClip& clip, const std::string& direction, int frame, bool flip, float frameWidth,
                        const std::string& name, AnimPoint* out) const {
    const AnimPoint* found = nullptr;
    if (auto named = clip.points.find(name); named != clip.points.end()) {
        auto list = named->second.find(direction);
        if (list == named->second.end()) list = named->second.find("");
        if (list != named->second.end() && !list->second.empty()) {
            found = &list->second[std::clamp(frame, 0, static_cast<int>(list->second.size()) - 1)];
        }
    }
    if (!found) {
        if (auto it = points.find(name); it != points.end()) found = &it->second;
    }
    if (!found) return false;
    *out = *found;
    if (flip) {
        out->x = frameWidth - out->x;
        out->angle = 180.0f - out->angle;
    }
    return true;
}

namespace {

// A point as [x, y, angle, front] or {"x", "y", "angle", "front"}.
AnimPoint PointFromJson(const nlohmann::json& j) {
    AnimPoint p;
    if (j.is_array()) {
        if (j.size() > 0 && j[0].is_number()) p.x = j[0].get<float>();
        if (j.size() > 1 && j[1].is_number()) p.y = j[1].get<float>();
        if (j.size() > 2 && j[2].is_number()) p.angle = j[2].get<float>();
        if (j.size() > 3) p.front = j[3].is_boolean() ? j[3].get<bool>() : j[3].is_number() && j[3].get<float>() != 0.0f;
    } else if (j.is_object()) {
        p.x = j.value("x", 0.0f), p.y = j.value("y", 0.0f), p.angle = j.value("angle", 0.0f), p.front = j.value("front", true);
    }
    return p;
}

// [[x, y, a, f], ...] (a point a frame), or one point for every frame.
std::vector<AnimPoint> PointsFromJson(const nlohmann::json& j) {
    std::vector<AnimPoint> out;
    if (j.is_array() && !j.empty() && (j[0].is_array() || j[0].is_object())) {
        for (const auto& p : j) out.push_back(PointFromJson(p));
    } else if (j.is_array() || j.is_object()) {
        out.push_back(PointFromJson(j));
    }
    return out;
}

std::vector<std::string> Words(const std::string& text) {
    std::vector<std::string> out;
    std::string word;
    for (char c : text + " ") {
        if (c == ' ' || c == ',' || c == '|' || c == ';') {
            if (!word.empty()) out.push_back(word);
            word.clear();
        } else {
            word += c;
        }
    }
    return out;
}

} // namespace

std::optional<AnimSheet> AnimSheetFromJson(const nlohmann::json& json, const std::string& path, std::string* error) {
    if (!json.is_object() || !json.contains("clips") || !json["clips"].is_object()) {
        if (error) *error = "needs a \"clips\" object";
        return std::nullopt;
    }
    AnimSheet sheet;
    sheet.path = path;
    sheet.texture = json.value("texture", "");
    if (json.contains("size") && json["size"].is_array() && json["size"].size() == 2) {
        sheet.textureWidth = json["size"][0].get<int>();
        sheet.textureHeight = json["size"][1].get<int>();
    }
    const auto grid = json.value("grid", nlohmann::json::object());
    const int gw = grid.value("width", 0), gh = grid.value("height", 0);
    const int columns = gw > 0 && sheet.textureWidth > 0 ? std::max(1, sheet.textureWidth / gw) : grid.value("columns", 1);
    const float fallbackFps = json.value("fps", 8.0f);
    auto frameOf = [&](const nlohmann::json& f, float duration) {
        AnimFrame frame;
        frame.duration = duration;
        if (f.is_number_integer()) {
            const int i = f.get<int>();
            frame.x = static_cast<float>((i % columns) * gw);
            frame.y = static_cast<float>((i / columns) * gh);
            frame.w = static_cast<float>(gw);
            frame.h = static_cast<float>(gh);
        } else if (f.is_object()) {
            const auto r = f.value("region", nlohmann::json::array());
            if (r.is_array() && r.size() == 4) {
                frame.x = r[0].get<float>();
                frame.y = r[1].get<float>();
                frame.w = r[2].get<float>();
                frame.h = r[3].get<float>();
            }
            frame.duration = f.value("duration", duration);
        }
        return frame;
    };
    for (const auto& [name, c] : json["clips"].items()) {
        if (!c.is_object()) continue;
        AnimClip clip;
        clip.name = name;
        clip.loop = c.value("loop", "loop");
        const float duration = 1.0f / std::max(0.5f, c.value("fps", fallbackFps));
        auto framesOf = [&](const nlohmann::json& list) {
            std::vector<AnimFrame> frames;
            if (list.is_array()) {
                for (const auto& f : list) frames.push_back(frameOf(f, duration));
            }
            return frames;
        };
        if (c.contains("directions") && c["directions"].is_object()) {
            for (const auto& [dir, list] : c["directions"].items()) {
                if (list.is_string() && list.get<std::string>().rfind("mirror:", 0) == 0) {
                    clip.mirrors[dir] = list.get<std::string>().substr(7);
                } else {
                    clip.directions[dir] = framesOf(list);
                }
            }
        } else {
            clip.directions[""] = framesOf(c.value("frames", nlohmann::json::array()));
        }
        for (const auto& e : c.value("events", nlohmann::json::array())) {
            if (e.is_object()) clip.events.push_back({e.value("frame", 0), e.value("event", "")});
        }
        if (c.contains("plays")) {
            const auto& plays = c["plays"];
            if (plays.is_string()) clip.plays = Words(plays.get<std::string>());
            else if (plays.is_array()) {
                for (const auto& w : plays) {
                    if (w.is_string()) clip.plays.push_back(w.get<std::string>());
                }
            }
        }
        if (c.contains("points") && c["points"].is_object()) {
            for (const auto& [point, byDirection] : c["points"].items()) {
                if (byDirection.is_object() && !byDirection.contains("x")) {
                    for (const auto& [dir, list] : byDirection.items()) clip.points[point][dir] = PointsFromJson(list);
                } else {
                    clip.points[point][""] = PointsFromJson(byDirection);
                }
            }
        }
        sheet.clips[name] = std::move(clip);
    }
    if (json.contains("points") && json["points"].is_object()) {
        for (const auto& [point, value] : json["points"].items()) sheet.points[point] = PointFromJson(value);
    }
    // Without a size, the frames' extent.
    if (sheet.textureWidth <= 0 || sheet.textureHeight <= 0) {
        float w = 1, h = 1;
        for (const auto& [n, clip] : sheet.clips) {
            for (const auto& [d, frames] : clip.directions) {
                for (const auto& f : frames) {
                    w = std::max(w, f.x + f.w);
                    h = std::max(h, f.y + f.h);
                }
            }
        }
        sheet.textureWidth = static_cast<int>(w);
        sheet.textureHeight = static_cast<int>(h);
    }
    return sheet;
}

std::vector<std::string> ValidateAnimSheet(const AnimSheet& sheet) {
    std::vector<std::string> problems;
    if (sheet.texture.empty()) problems.push_back("has no texture");
    if (sheet.clips.empty()) problems.push_back("has no clips");
    static const std::set<std::string> loops = {"loop", "once", "pingpong", "hold"};
    for (const auto& [name, clip] : sheet.clips) {
        if (!loops.contains(clip.loop)) problems.push_back("clip '" + name + "': loop is loop, once, pingpong or hold");
        bool any = false;
        for (const auto& [dir, frames] : clip.directions) {
            any = any || !frames.empty();
            if (!dir.empty() && std::find(kDirections.begin(), kDirections.end(), dir) == kDirections.end()) {
                problems.push_back("clip '" + name + "': no direction '" + dir + "' (s, se, e, ne, n, nw, w, sw)");
            }
            for (const auto& f : frames) {
                if (f.w <= 0 || f.h <= 0) {
                    problems.push_back("clip '" + name + "' has an empty frame (a grid index needs \"grid\" and \"size\")");
                    break;
                }
                if (f.x + f.w > sheet.textureWidth + 0.5f || f.y + f.h > sheet.textureHeight + 0.5f) {
                    problems.push_back("clip '" + name + "' has a frame outside the picture");
                    break;
                }
            }
        }
        for (const auto& [dir, of] : clip.mirrors) {
            if (!clip.directions.contains(of)) problems.push_back("clip '" + name + "': " + dir + " mirrors " + of + ", which it hasn't");
        }
        if (!any) problems.push_back("clip '" + name + "' has no frames");
    }
    return problems;
}

nlohmann::json AnimSheetFromAseprite(const nlohmann::json& a, const std::string& texture) {
    // Frames as a list in order (the array form, or the hash form's values in file order).
    std::vector<nlohmann::json> frames;
    if (a.contains("frames") && a["frames"].is_array()) {
        for (const auto& f : a["frames"]) frames.push_back(f);
    } else if (a.contains("frames") && a["frames"].is_object()) {
        for (const auto& [name, f] : a["frames"].items()) frames.push_back(f);
    }
    auto frameJson = [&](size_t i) {
        const auto& f = frames[i];
        const auto r = f.value("frame", nlohmann::json::object());
        return nlohmann::json{{"region", {r.value("x", 0), r.value("y", 0), r.value("w", 0), r.value("h", 0)}},
                              {"duration", f.value("duration", 100) / 1000.0}};
    };
    const auto meta = a.value("meta", nlohmann::json::object());
    nlohmann::json sheet = {{"schema", "aige.anim/1"}, {"texture", texture}, {"clips", nlohmann::json::object()}};
    if (meta.contains("size")) sheet["size"] = {meta["size"].value("w", 0), meta["size"].value("h", 0)};
    const auto tags = meta.value("frameTags", nlohmann::json::array());
    if (tags.empty() && !frames.empty()) {
        nlohmann::json list = nlohmann::json::array();
        for (size_t i = 0; i < frames.size(); ++i) list.push_back(frameJson(i));
        sheet["clips"]["idle"] = {{"frames", list}};
    }
    for (const auto& tag : tags) {
        const int from = tag.value("from", 0), to = tag.value("to", 0);
        const std::string direction = tag.value("direction", "forward");
        nlohmann::json list = nlohmann::json::array();
        for (int i = from; i <= to && i < static_cast<int>(frames.size()); ++i) list.push_back(frameJson(i));
        if (direction == "reverse") std::reverse(list.begin(), list.end());
        // "walk_e" (or "walk e") becomes the east direction of "walk".
        std::string name = tag.value("name", "clip");
        std::string dir;
        for (const char sep : {'_', ' ', '-'}) {
            const size_t at = name.rfind(sep);
            if (at != std::string::npos &&
                std::find(kDirections.begin(), kDirections.end(), name.substr(at + 1)) != kDirections.end()) {
                dir = name.substr(at + 1);
                name = name.substr(0, at);
                break;
            }
        }
        auto& clip = sheet["clips"][name];
        if (direction == "pingpong") clip["loop"] = "pingpong";
        if (dir.empty()) clip["frames"] = list;
        else clip["directions"][dir] = list;
    }
    return sheet;
}

nlohmann::json AnimSheetFromGrid(const std::string& texture, int frameWidth, int frameHeight, int columns,
                                 const std::vector<std::string>& rows, float fps) {
    nlohmann::json sheet = {{"schema", "aige.anim/1"},
                            {"texture", texture},
                            {"grid", {{"width", frameWidth}, {"height", frameHeight}}},
                            {"size", {frameWidth * columns, frameHeight * static_cast<int>(rows.size())}},
                            {"fps", fps},
                            {"clips", nlohmann::json::object()}};
    for (size_t r = 0; r < rows.size(); ++r) {
        if (rows[r].empty()) continue;
        nlohmann::json list = nlohmann::json::array();
        for (int c = 0; c < columns; ++c) list.push_back(static_cast<int>(r) * columns + c);
        std::string name = rows[r], dir;
        if (const size_t at = name.rfind('_'); at != std::string::npos &&
            std::find(kDirections.begin(), kDirections.end(), name.substr(at + 1)) != kDirections.end()) {
            dir = name.substr(at + 1);
            name = name.substr(0, at);
        }
        if (dir.empty()) sheet["clips"][name]["frames"] = list;
        else sheet["clips"][name]["directions"][dir] = list;
    }
    return sheet;
}

AnimLibrary::AnimLibrary(fs::path projectRoot) : root_(std::move(projectRoot)) {}

void AnimLibrary::Refresh() {
    sheets_.clear();
    problems_.clear();
    std::error_code ec;
    if (root_.empty() || !fs::is_directory(root_, ec)) return;
    for (const auto& file : fs::recursive_directory_iterator(root_, fs::directory_options::skip_permission_denied, ec)) {
        if (!file.is_regular_file(ec)) continue;
        const std::string name = file.path().filename().string();
        if (name.size() < 10 || name.substr(name.size() - 10) != ".anim.json") continue;
        const std::string relative = fs::relative(file.path(), root_, ec).generic_string();
        if (relative.rfind(".cache", 0) == 0 || relative.rfind("build", 0) == 0) continue;
        std::ifstream in(file.path(), std::ios::binary);
        const nlohmann::json json = nlohmann::json::parse(in, nullptr, false);
        std::string error;
        auto sheet = json.is_discarded() ? std::nullopt : AnimSheetFromJson(json, relative, &error);
        if (!sheet) {
            problems_.push_back(relative + ": " + (error.empty() ? "isn't JSON" : error));
            continue;
        }
        for (const auto& p : ValidateAnimSheet(*sheet)) problems_.push_back(relative + ": " + p);
        sheets_.push_back(std::move(*sheet));
    }
}

bool AnimLibrary::TextureSize(const std::string& path, int* width, int* height) const {
    if (auto it = sizes_.find(path); it != sizes_.end()) {
        *width = it->second.first, *height = it->second.second;
        return it->second.first > 0;
    }
    // A PNG's width and height are big-endian at bytes 16-23 (its IHDR chunk).
    unsigned char head[24] = {};
    std::ifstream in(root_ / fs::u8path(path), std::ios::binary);
    int w = 0, h = 0;
    if (in && in.read(reinterpret_cast<char*>(head), 24) && head[1] == 'P' && head[2] == 'N' && head[3] == 'G') {
        w = (head[16] << 24) | (head[17] << 16) | (head[18] << 8) | head[19];
        h = (head[20] << 24) | (head[21] << 16) | (head[22] << 8) | head[23];
    }
    sizes_[path] = {w, h};
    *width = w, *height = h;
    return w > 0;
}

const AnimSheet* AnimLibrary::Find(const std::string& path) const {
    for (const auto& sheet : sheets_) {
        if (sheet.path == path) return &sheet;
    }
    return nullptr;
}

// --- Animators -------------------------------------------------------------------------------

const AnimatorDef::State* AnimatorDef::Find(const std::string& state) const {
    for (const auto& s : states) {
        if (s.id == state) return &s;
    }
    return nullptr;
}

AnimatorDef AnimatorFromJson(const std::string& id, const nlohmann::json& json) {
    AnimatorDef def;
    def.id = id;
    for (const auto& s : json.value("states", nlohmann::json::array())) {
        if (!s.is_object()) continue;
        def.states.push_back({s.value("id", ""), s.value("clip", s.value("id", "")), s.value("exit", "") == "end",
                              s.value("speed", 1.0f)});
    }
    for (const auto& t : json.value("transitions", nlohmann::json::array())) {
        if (!t.is_object()) continue;
        def.transitions.push_back({t.value("from", "*"), t.value("to", ""), t.value("if", ""), t.value("priority", 0)});
    }
    def.defaultState = json.value("default", def.states.empty() ? std::string() : def.states.front().id);
    return def;
}

namespace {

AnimatorDef Make(const std::string& id, const nlohmann::json& json) { return AnimatorFromJson(id, json); }

const std::map<std::string, AnimatorDef>& BuiltIns() {
    static const std::map<std::string, AnimatorDef> builtIns = [] {
        std::map<std::string, AnimatorDef> m;
        m["humanoid"] = Make("humanoid", nlohmann::json::parse(R"({
            "states": [{"id": "idle"}, {"id": "walk"}, {"id": "attack", "exit": "end"}, {"id": "cast", "exit": "end"},
                       {"id": "use", "exit": "end"}, {"id": "hurt", "exit": "end"}, {"id": "die"}],
            "transitions": [
                {"from": "idle", "to": "walk", "if": "speed > 0.1"}, {"from": "walk", "to": "idle", "if": "speed <= 0.1"},
                {"from": "*", "to": "attack", "if": "attacking", "priority": 1},
                {"from": "*", "to": "cast", "if": "casting", "priority": 1},
                {"from": "*", "to": "use", "if": "using", "priority": 1},
                {"from": "*", "to": "hurt", "if": "hurt", "priority": 2},
                {"from": "*", "to": "die", "if": "dead", "priority": 9}]})"));
        m["quadruped"] = Make("quadruped", nlohmann::json::parse(R"({
            "states": [{"id": "idle"}, {"id": "walk"}, {"id": "attack", "exit": "end"}, {"id": "hurt", "exit": "end"}, {"id": "die"}],
            "transitions": [
                {"from": "idle", "to": "walk", "if": "speed > 0.1"}, {"from": "walk", "to": "idle", "if": "speed <= 0.1"},
                {"from": "*", "to": "attack", "if": "attacking", "priority": 1},
                {"from": "*", "to": "hurt", "if": "hurt", "priority": 2},
                {"from": "*", "to": "die", "if": "dead", "priority": 9}]})"));
        m["flyer"] = Make("flyer", nlohmann::json::parse(R"({
            "states": [{"id": "fly"}, {"id": "attack", "exit": "end"}, {"id": "hurt", "exit": "end"}, {"id": "die"}],
            "transitions": [{"from": "*", "to": "attack", "if": "attacking", "priority": 1},
                            {"from": "*", "to": "hurt", "if": "hurt", "priority": 2},
                            {"from": "*", "to": "die", "if": "dead", "priority": 9}]})"));
        m["prop"] = Make("prop", nlohmann::json::parse(R"({
            "states": [{"id": "idle"}, {"id": "open"}, {"id": "close"}, {"id": "break"}],
            "transitions": [{"from": "*", "to": "open", "if": "open && !closed"}, {"from": "open", "to": "close", "if": "closed"},
                            {"from": "*", "to": "break", "if": "dead", "priority": 9}]})"));
        return m;
    }();
    return builtIns;
}

} // namespace

const AnimatorDef* BuiltInAnimator(const std::string& id) {
    auto it = BuiltIns().find(id);
    return it == BuiltIns().end() ? nullptr : &it->second;
}

std::vector<std::string> BuiltInAnimatorIds() {
    std::vector<std::string> ids;
    for (const auto& [id, def] : BuiltIns()) ids.push_back(id);
    return ids;
}

bool AnimConditionHolds(const std::string& condition, const std::map<std::string, float>& p) {
    if (Trim(condition).empty()) return true;
    auto value = [&](const std::string& name) {
        auto it = p.find(name);
        return it == p.end() ? 0.0f : it->second;
    };
    size_t start = 0;
    while (start <= condition.size()) {
        size_t end = condition.find("&&", start);
        if (end == std::string::npos) end = condition.size();
        std::string term = Trim(condition.substr(start, end - start));
        start = end + 2;
        if (term.empty()) continue;
        bool holds = false;
        static const char* ops[] = {">=", "<=", "!=", "==", ">", "<"};
        const char* op = nullptr;
        size_t at = std::string::npos;
        for (const char* o : ops) {
            at = term.find(o);
            if (at != std::string::npos) {
                op = o;
                break;
            }
        }
        if (op) {
            const float left = value(Trim(term.substr(0, at)));
            const float right = std::strtof(Trim(term.substr(at + std::strlen(op))).c_str(), nullptr);
            const std::string o = op;
            holds = o == ">=" ? left >= right : o == "<=" ? left <= right : o == "!=" ? left != right
                    : o == "==" ? left == right : o == ">" ? left > right : left < right;
        } else if (term[0] == '!') {
            holds = value(Trim(term.substr(1))) == 0.0f;
        } else {
            holds = value(term) != 0.0f;
        }
        if (!holds) return false;
        if (end == condition.size()) break;
    }
    return true;
}

void TriggerAnimation(Registry& registry, Entity entity, const std::string& trigger) {
    if (!registry.valid(entity) || !registry.all_of<Animator>(entity) || trigger.empty()) return;
    auto& state = registry.get_or_emplace<AnimatorState>(entity);
    if (trigger == "attacking") state.attacking = true;
    else if (trigger == "hurt") state.hurt = true;
    else if (trigger == "casting") state.casting = true;
    else if (trigger == "using") state.using_ = true;
    else state.play = trigger; // a state, a variant or a clip, by name (read in UpdateAnimators)
}

void SetAnimationParameter(Registry& registry, Entity entity, const std::string& name, float value) {
    if (!registry.valid(entity) || !registry.all_of<Animator>(entity) || name.empty()) return;
    registry.get_or_emplace<AnimatorState>(entity).parameters[name] = value;
}

namespace {

// The clip `name` names on a sheet: itself, else one that plays on it.
const AnimClip* ClipNamed(const AnimSheet& sheet, const std::string& name) {
    if (const AnimClip* clip = sheet.Clip(name)) return clip;
    for (const auto& [n, clip] : sheet.clips) {
        if (std::find(clip.plays.begin(), clip.plays.end(), name) != clip.plays.end()) return &clip;
    }
    return nullptr;
}

// "attack=attack_punch|attack_kick; open=lid_up": the clips each state plays, by hand.
std::map<std::string, std::vector<std::string>> ClipOverrides(const std::string& text) {
    std::map<std::string, std::vector<std::string>> out;
    std::stringstream in(text);
    std::string part;
    while (std::getline(in, part, ';')) {
        const size_t eq = part.find('=');
        if (eq == std::string::npos) continue;
        const std::string state = Trim(part.substr(0, eq));
        if (!state.empty()) out[state] = Words(part.substr(eq + 1));
    }
    return out;
}

// The item in an entity's weapon slot, as its record (null without one).
const nlohmann::json* WeaponOf(Registry& registry, Entity entity) {
    const auto* equipment = registry.try_get<Equipment>(entity);
    if (!equipment || equipment->weapon.empty()) return nullptr;
    const Record* record = ActiveRecords().Find("item", equipment->weapon);
    return record ? &record->resolved : nullptr;
}

bool Contains(const std::string& text, const std::string& part) { return !part.empty() && text.find(part) != std::string::npos; }

// The clip a state plays on this entity (docs/assets/animations.md §2.1): the Animator's own list for it, else the
// sheet's clips for it (its name, its variants, those that play on it); among several, the weapon's, then `pick`.
std::string ChooseClip(Registry& registry, Entity entity, const Animator& animator, AnimatorState& state,
                       const AnimatorDef::State& current, const AnimSheet& sheet) {
    std::vector<std::string> candidates;
    const auto overrides = ClipOverrides(animator.clips);
    if (auto it = overrides.find(current.id); it != overrides.end()) {
        for (const auto& name : it->second) {
            if (sheet.Clip(name)) candidates.push_back(name);
        }
    }
    if (candidates.empty()) {
        const std::string& base = current.clip;
        if (sheet.Clip(base)) candidates.push_back(base);
        for (const auto& [name, clip] : sheet.clips) {
            if (name == base) continue;
            const bool variant = name.rfind(base + "_", 0) == 0;
            const bool plays = std::find(clip.plays.begin(), clip.plays.end(), current.id) != clip.plays.end() ||
                               std::find(clip.plays.begin(), clip.plays.end(), base) != clip.plays.end();
            if (variant || plays) candidates.push_back(name);
        }
    }
    if (candidates.size() <= 1) return candidates.empty() ? current.clip : candidates.front();
    if (animator.pick == "weapon") {
        // The weapon's own clip or variant, else its class's clips; unarmed, the unarmed ones.
        const nlohmann::json* weapon = WeaponOf(registry, entity);
        const std::string animation = weapon ? weapon->value("animation", "") : "";
        const std::string kind = !animator.attackClass.empty() ? animator.attackClass : weapon ? weapon->value("class", "") : "";
        std::vector<std::string> preferred;
        for (const auto& name : candidates) {
            const bool match = !animation.empty() ? (name == animation || name == current.clip + "_" + animation || Contains(name, animation))
                               : !kind.empty()    ? Contains(name, kind)
                               : !weapon          ? Contains(name, "unarmed") || Contains(name, "punch")
                                                  : false;
            if (match) preferred.push_back(name);
        }
        if (!preferred.empty()) candidates = preferred;
    }
    if (animator.pick == "first") return candidates.front();
    if (animator.pick == "order") return candidates[static_cast<size_t>(state.order++) % candidates.size()];
    static std::minstd_rand rng(20261028u);
    return candidates[rng() % candidates.size()];
}

} // namespace

float AnimationLength(Registry& registry, const AnimLibrary& library, Entity entity, const std::string& name) {
    if (!registry.valid(entity)) return 0.0f;
    const auto* animator = registry.try_get<Animator>(entity);
    const AnimSheet* sheet = animator ? library.Find(animator->sheet) : nullptr;
    const AnimClip* clip = sheet ? ClipNamed(*sheet, name) : nullptr;
    if (!clip) return 0.0f;
    const auto* state = registry.try_get<AnimatorState>(entity);
    return clip->Length(state ? state->direction : "s") / std::max(0.05f, animator->speed);
}

std::vector<AnimEventFired> UpdateAnimators(Registry& registry, const AnimLibrary& library, float dt) {
    std::vector<AnimEventFired> fired;
    const bool topDown = ActiveSettings().grid.style == GridStyle::TopDown;
    std::vector<Entity> entities;
    for (auto [entity, animator] : registry.view<Animator>().each()) entities.push_back(entity);
    for (Entity entity : entities) {
        const auto& animator = registry.get<Animator>(entity);
        const AnimSheet* sheet = library.Find(animator.sheet);
        auto* sprite = registry.try_get<Sprite>(entity);
        if (!sheet || !sprite) continue;
        const AnimatorDef* def = BuiltInAnimator(animator.animator);
        AnimatorDef fromRecord;
        if (!def) {
            if (const Record* record = ActiveRecords().Find("animator", animator.animator)) {
                fromRecord = AnimatorFromJson(animator.animator, record->resolved);
                def = &fromRecord;
            }
        }
        if (!def) def = BuiltInAnimator("humanoid");
        auto& state = registry.get_or_emplace<AnimatorState>(entity);
        // Parameters, from components and triggers.
        std::map<std::string, float> p;
        if (const auto* v = registry.try_get<Velocity2D>(entity)) p["speed"] = std::sqrt(v->x * v->x + v->y * v->y);
        p["dead"] = registry.all_of<Dead>(entity) ? 1.0f : 0.0f;
        p["attacking"] = state.attacking;
        p["hurt"] = state.hurt;
        p["casting"] = state.casting;
        p["using"] = state.using_;
        if (const auto* health = registry.try_get<Health>(entity); health && health->max > 0.0f) p["health"] = health->current / health->max;
        // Opened and closed: a door as it toggles (closed only once it's been open, so a shut door doesn't play its
        // closing at the start), a container once looted.
        if (const auto* door = registry.try_get<Door>(entity)) {
            if (door->open) state.parameters["~opened"] = 1.0f;
            p["open"] = door->open ? 1.0f : 0.0f;
            p["closed"] = !door->open && state.parameters.contains("~opened") ? 1.0f : 0.0f;
        }
        if (const auto* box = registry.try_get<Container>(entity); box && box->looted) p["open"] = 1.0f;
        for (const auto& [name, value] : state.parameters) p[name] = value;
        if (const auto* facing = registry.try_get<Facing>(entity)) {
            const float sx = topDown ? facing->x : facing->x - facing->y;
            const float sy = topDown ? facing->y : (facing->x + facing->y) * 0.5f;
            state.direction = DirectionName(sx, sy);
        }
        if (state.state.empty() || !def->Find(state.state)) {
            state.state = def->defaultState;
            state.time = 0.0f;
            state.frame = -1;
        }
        // A state, variant or clip asked for by name (play, an Interactable, a cutscene, an ability).
        if (!state.play.empty()) {
            const std::string want = state.play;
            state.play.clear();
            auto enter = [&](const std::string& id, const std::string& clip) {
                state.state = id, state.time = 0.0f, state.frame = -1, state.oneShot.clear();
                state.clip = clip, state.chosenFor = clip.empty() ? std::string() : id;
            };
            const size_t underscore = want.find('_');
            const std::string base = underscore == std::string::npos ? std::string() : want.substr(0, underscore);
            if (want == "stop") {
                enter(def->defaultState, "");
            } else if (def->Find(want)) {
                enter(want, "");
            } else if (!base.empty() && def->Find(base) && sheet->Clip(want)) {
                enter(base, want); // a variant: attack_kick in the attack state
            } else if (const AnimClip* named = ClipNamed(*sheet, want)) {
                for (const auto& s : def->states) {
                    if (std::find(named->plays.begin(), named->plays.end(), s.id) != named->plays.end()) {
                        enter(s.id, named->name);
                        break;
                    }
                }
                if (state.chosenFor.empty() || state.clip != named->name) {
                    state.oneShot = named->name, state.time = 0.0f, state.frame = -1;
                }
            }
        }
        // The highest-priority transition that holds (from this state or any).
        const AnimatorDef::Transition* best = nullptr;
        for (const auto& t : def->transitions) {
            if ((t.from != "*" && t.from != state.state) || t.to == state.state) continue;
            if (!AnimConditionHolds(t.condition, p)) continue;
            if (!best || t.priority > best->priority) best = &t;
        }
        // A state that lasts a clip isn't interrupted by a lower priority (a walk mid-swing).
        const AnimatorDef::State* current = def->Find(state.state);
        if (best && current && current->exitAtEnd) {
            int currentPriority = 0;
            for (const auto& t : def->transitions) {
                if (t.to == state.state) currentPriority = std::max(currentPriority, t.priority);
            }
            if (best->priority <= currentPriority && best->to != state.state) best = nullptr;
        }
        // A clip played by name holds against all but hurt and death.
        if (best && !state.oneShot.empty() && best->priority < 2) best = nullptr;
        if (best) {
            state.state = best->to;
            state.time = 0.0f;
            state.frame = -1;
            state.oneShot.clear();
            current = def->Find(state.state);
        }
        state.attacking = state.hurt = state.casting = state.using_ = false; // triggers are read once
        if (!current) continue;
        // The clip: one played by name, else the one chosen for this state (once, on entering it), else idle, else any.
        if (state.chosenFor != state.state) {
            state.clip = ChooseClip(registry, entity, animator, state, *current, *sheet);
            state.chosenFor = state.state;
        }
        const AnimClip* clip = !state.oneShot.empty() ? sheet->Clip(state.oneShot) : sheet->Clip(state.clip);
        if (!clip) clip = sheet->Clip(current->clip);
        if (!clip) clip = sheet->Clip("idle");
        if (!clip && !sheet->clips.empty()) clip = &sheet->clips.begin()->second;
        if (!clip) continue;
        bool flip = false;
        std::string used;
        const auto* frames = clip->FramesFor(state.direction, &flip, &used);
        if (!frames || frames->empty()) continue;
        state.time += dt * animator.speed * current->speed;
        const float length = clip->Length(state.direction);
        float t = state.time;
        bool ended = false;
        if (clip->loop == "loop" && length > 0.0f) {
            t = std::fmod(t, length);
        } else if (clip->loop == "pingpong" && length > 0.0f) {
            t = std::fmod(t, length * 2.0f);
            if (t > length) t = length * 2.0f - t;
        } else if (t >= length) {
            t = length - 1e-4f;
            ended = true;
        }
        if (!state.oneShot.empty() && state.time >= length && length > 0.0f) ended = true; // a looping clip played once
        int index = 0;
        for (float acc = 0.0f; index < static_cast<int>(frames->size()); ++index) {
            acc += (*frames)[index].duration;
            if (t < acc) break;
        }
        index = std::min(index, static_cast<int>(frames->size()) - 1);
        if (index != state.frame) {
            for (const auto& e : clip->events) {
                if (e.frame == index) fired.push_back({entity, e.event});
            }
            state.frame = index;
        }
        const AnimFrame& f = (*frames)[index];
        const float W = static_cast<float>(std::max(1, sheet->textureWidth));
        const float H = static_cast<float>(std::max(1, sheet->textureHeight));
        sprite->texture = sheet->texture;
        sprite->u0 = f.x / W;
        sprite->u1 = (f.x + f.w) / W;
        sprite->v0 = f.y / H;
        sprite->v1 = (f.y + f.h) / H;
        if (flip) std::swap(sprite->u0, sprite->u1);
        // This frame's points, for what's attached to it.
        state.frameWidth = f.w, state.frameHeight = f.h;
        state.points.clear();
        std::set<std::string> names;
        for (const auto& [name, byDirection] : clip->points) names.insert(name);
        for (const auto& [name, point] : sheet->points) names.insert(name);
        for (const auto& name : names) {
            AnimPoint point;
            if (sheet->PointOn(*clip, used, index, flip, f.w, name, &point)) state.points[name] = point;
        }
        if (ended && !state.oneShot.empty()) {
            state.oneShot.clear();
            state.state = def->defaultState;
            state.chosenFor.clear();
            state.time = 0.0f;
            state.frame = -1;
        } else if (ended && current->exitAtEnd) {
            state.state = def->defaultState;
            state.time = 0.0f;
            state.frame = -1;
        }
    }
    UpdateAttachments(registry, library);
    return fired;
}

// --- Attachments (docs/assets/animations.md §3) ------------------------------------------------

void UpdateAttachments(Registry& registry, const AnimLibrary& library) {
    // The scene's attachments: their parents, by Name.
    std::map<std::string, Entity> named;
    for (auto [entity, name] : registry.view<Name>().each()) named.emplace(name.value, entity);
    for (auto [entity, attachment] : registry.view<Attachment>(entt::exclude<Transient>).each()) {
        auto& link = registry.get_or_emplace<AttachmentLink>(entity);
        auto it = attachment.to.empty() ? named.end() : named.find(attachment.to);
        link.parent = it == named.end() || it->second == entity ? Entity{entt::null} : it->second;
    }
    // What Equipment shows: a transient entity a worn or wielded item with a sprite, made, kept or removed.
    std::map<std::pair<Entity, std::string>, Entity> shown;
    std::vector<Entity> stale;
    for (auto [entity, link] : registry.view<AttachmentLink, Transient>().each()) {
        if (!registry.valid(link.parent) || link.slot.empty()) stale.push_back(entity);
        else shown[{link.parent, link.slot}] = entity;
    }
    std::set<Entity> kept;
    for (auto [owner, equipment] : registry.view<Equipment>(entt::exclude<Transient>).each()) {
        if (!equipment.show) continue;
        for (const auto& slot : EquipmentSlots()) {
            const std::string* id = EquipmentSlot(equipment, slot);
            if (!id || id->empty()) continue;
            const Record* record = ActiveRecords().Find("item", *id);
            const nlohmann::json item = record ? record->resolved : nlohmann::json::object();
            const std::string picture = item.value("sprite", "");
            if (picture.empty()) continue;
            Entity made{entt::null};
            if (auto it = shown.find({owner, slot}); it != shown.end() && registry.get<AttachmentLink>(it->second).item == *id) {
                made = it->second;
            } else {
                made = registry.create();
                registry.emplace<Transient>(made);
                registry.emplace<ScreenTransform2D>(made);
                registry.emplace<AttachmentLink>(made, AttachmentLink{owner, slot, *id});
            }
            kept.insert(made);
            auto& attachment = registry.get_or_emplace<Attachment>(made);
            static const std::map<std::string, std::string> kPoints = {{"weapon", "hand"}, {"offhand", "offhand"}, {"head", "head"},
                                                                       {"body", "body"}, {"hands", "hand"}, {"feet", "feet"},
                                                                       {"amulet", "neck"}, {"ring", "hand"}};
            // A record's empty fields are its defaults: by slot, auto.
            const std::string point = item.value("point", std::string());
            attachment.point = !point.empty() ? point : kPoints.contains(slot) ? kPoints.at(slot) : slot;
            const auto grip = item.value("grip", nlohmann::json::array());
            attachment.pivotX = grip.is_array() && grip.size() == 2 && grip[0].is_number() ? grip[0].get<float>() : 0.5f;
            attachment.pivotY = grip.is_array() && grip.size() == 2 && grip[1].is_number() ? grip[1].get<float>() : 0.5f;
            attachment.pointing = item.value("pointing", 0.0f);
            attachment.size = item.value("size", 0.0f);
            attachment.layer = item.value("layer", std::string());
            if (attachment.layer.empty()) attachment.layer = "auto";
            attachment.turn = item.value("turn", true);
            registry.get_or_emplace<Sprite>(made).texture = picture;
        }
    }
    // Animated parts (a building's doors, §4.3): an overlay each, over the box it came from ("part", "part1", ...).
    for (auto [owner, whole, sprite] : registry.view<AnimatedPart, Sprite>(entt::exclude<Transient>).each()) {
      const std::vector<AnimatedPart> doors = DoorsOf(whole);
      for (size_t index = 0; index < doors.size(); ++index) {
        const AnimatedPart& part = doors[index];
        // An open doorway (§4.5): an overlay unseen, no animator, only so where its doorway is is known in play.
        const bool opening = part.sheet.empty() && part.opens == "open";
        if (part.sheet.empty() && !opening) continue;
        if (!opening && library.Find(part.sheet) == nullptr) continue; // its sheet deleted: nothing over the picture (it shows as it is)
        const std::string slot = index == 0 ? std::string("part") : "part" + std::to_string(index);
        Entity made{entt::null};
        if (auto it = shown.find({owner, slot}); it != shown.end() && registry.get<AttachmentLink>(it->second).item == part.sheet) {
            made = it->second;
        } else {
            made = registry.create();
            registry.emplace<Transient>(made);
            registry.emplace<ScreenTransform2D>(made);
            registry.emplace<AttachmentLink>(made, AttachmentLink{owner, slot, part.sheet});
            registry.emplace<Sprite>(made);
            if (!opening) registry.emplace<Animator>(made, part.sheet, "prop");
        }
        kept.insert(made);
        auto& attachment = registry.get_or_emplace<Attachment>(made);
        attachment.point.clear();
        // From the bottom middle of the thing's frame (an Attachment's point when it names none) to the box's middle.
        float frameW = 0.0f, frameH = 0.0f;
        if (int w = 0, h = 0; library.TextureSize(sprite.texture, &w, &h)) {
            frameW = static_cast<float>(w) * std::abs(sprite.u1 - sprite.u0);
            frameH = static_cast<float>(h) * std::abs(sprite.v1 - sprite.v0);
        }
        // In the scene's pixels (PlaceAttachments places a picture's attachments in its sprite's, not its texture's): the
        // box's place scaled as the building is drawn (a 598 px picture drawn 192 px wide put its door three times too far
        // out); a copy turned to face the other way (its picture mirrored), its door's place mirrored too.
        const float kx = frameW > 0.0f ? sprite.width / frameW : 1.0f, ky = frameH > 0.0f ? sprite.height / frameH : 1.0f;
        attachment.offsetX = (sprite.u0 > sprite.u1 ? frameW * 0.5f - part.x : part.x - frameW * 0.5f) * kx;
        attachment.offsetY = (part.y - frameH) * ky;
        attachment.pivotX = attachment.pivotY = 0.5f;
        attachment.layer = "front";
        attachment.turn = false;
        attachment.size = std::max(part.width, part.height);
        if (opening) {
            // Its box's size, unseen (no texture: the sizing below leaves it so).
            auto& unseen = registry.get<Sprite>(made);
            unseen.width = part.width * kx, unseen.height = part.height * ky, unseen.a = 0.0f;
            attachment.size = 0.0f;
        } else {
            // Shown on its own picture only: the outside's parts (its doors) while one is outside (inside, its front walls
            // and the doors in them are cut away; they still open and shut: play reads where they are), a floor's while
            // that floor shows.
            const auto* interior = registry.try_get<Interior>(owner);
            const bool inside = interior && interior->shown && interior->kind != "scene";
            const bool seen = part.floor <= 0 ? !inside : inside && interior->floor == part.floor;
            registry.get<Sprite>(made).a = seen ? 1.0f : 0.0f;
        }
      }
    }
    for (const auto& [key, entity] : shown) {
        if (!kept.contains(entity)) stale.push_back(entity);
    }
    for (Entity entity : stale) {
        if (registry.valid(entity)) registry.destroy(entity);
    }
    // Sizes: at the parent's scale (its sprite's size over its frame's), or `size` pixels of its frame long.
    for (auto [entity, attachment, link, sprite] : registry.view<Attachment, AttachmentLink, Sprite>().each()) {
        if (!registry.valid(link.parent)) continue;
        const auto* parent = registry.try_get<Sprite>(link.parent);
        if (!parent) continue;
        float frameWidth = 0.0f;
        if (const auto* state = registry.try_get<AnimatorState>(link.parent); state && state->frameWidth > 0.0f) {
            frameWidth = state->frameWidth;
        } else if (int w = 0, h = 0; library.TextureSize(parent->texture, &w, &h)) {
            frameWidth = static_cast<float>(w) * std::abs(parent->u1 - parent->u0);
        }
        const float scale = frameWidth > 0.0f ? parent->width / frameWidth : 1.0f;
        int w = 0, h = 0;
        if (const auto* own = registry.try_get<AnimatorState>(entity); own && own->frameWidth > 0.0f) {
            w = static_cast<int>(own->frameWidth), h = static_cast<int>(own->frameHeight); // animated: one frame of its sheet
        } else if (!library.TextureSize(sprite.texture, &w, &h)) {
            continue;
        }
        const float k = attachment.size > 0.0f ? attachment.size * scale / static_cast<float>(std::max(w, h)) : scale;
        sprite.width = static_cast<float>(w) * k;
        sprite.height = static_cast<float>(h) * k;
    }
}

void PlaceAttachments(Registry& registry) {
    for (auto [entity, attachment, link, screen, sprite] : registry.view<Attachment, AttachmentLink, ScreenTransform2D, Sprite>().each()) {
        if (!registry.valid(link.parent)) continue;
        const auto* at = registry.try_get<ScreenTransform2D>(link.parent);
        const auto* parent = registry.try_get<Sprite>(link.parent);
        if (!at || !parent) continue;
        // The parent's frame, and the point on it (else its bottom middle).
        const auto* state = registry.try_get<AnimatorState>(link.parent);
        float frameWidth = state && state->frameWidth > 0.0f ? state->frameWidth : parent->width;
        float frameHeight = state && state->frameHeight > 0.0f ? state->frameHeight : parent->height;
        AnimPoint point{frameWidth * 0.5f, frameHeight, 0.0f, true};
        if (state && !attachment.point.empty()) {
            if (auto it = state->points.find(attachment.point); it != state->points.end()) point = it->second;
        }
        if (!state && parent->u0 > parent->u1) point.x = frameWidth - point.x, point.angle = 180.0f - point.angle; // a picture turned to face its way
        const float sx = parent->width / std::max(1.0f, frameWidth), sy = parent->height / std::max(1.0f, frameHeight);
        const float px = at->x - parent->width * 0.5f + (point.x + attachment.offsetX) * sx;
        const float py = at->y - parent->height * 0.5f + (point.y + attachment.offsetY) * sy;
        // Turned so its art points the point's way, held at its pivot.
        const float turn = attachment.turn ? point.angle + attachment.angle - attachment.pointing : attachment.angle;
        const float a = turn * 3.14159265f / 180.0f, c = std::cos(a), s = std::sin(a);
        const float dx = (attachment.pivotX - 0.5f) * sprite.width, dy = (attachment.pivotY - 0.5f) * sprite.height;
        screen.x = px - (dx * c - dy * s);
        screen.y = py - (dx * s + dy * c);
        sprite.angle = turn;
        // A building's door on a mirrored copy: its frames mirrored as its picture is (the animator sets them unmirrored).
        if (link.slot.rfind("part", 0) == 0 && (parent->u0 > parent->u1) != (sprite.u0 > sprite.u1)) std::swap(sprite.u0, sprite.u1);
        if (attachment.layer == "own") continue; // its depth is where it stands (a painted door: over its building's occluder)
        const bool front = attachment.layer == "front" || (attachment.layer != "behind" && point.front);
        screen.depth = at->depth + (front ? 0.01f : -0.01f);
    }
}

} // namespace aige
