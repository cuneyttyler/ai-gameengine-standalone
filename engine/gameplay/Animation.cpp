#include "gameplay/Animation.h"

#include "gameplay/Components.h"
#include "gameplay/Settings.h"
#include "physics2d/Components.h"
#include "records/Records.h"
#include "renderer2d/Components.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <cmath>
#include <fstream>
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

const std::vector<AnimFrame>* AnimClip::FramesFor(const std::string& direction, bool* flip) const {
    if (flip) *flip = false;
    auto pick = [&](const std::string& d) -> const std::vector<AnimFrame>* {
        if (auto it = directions.find(d); it != directions.end() && !it->second.empty()) return &it->second;
        if (auto m = mirrors.find(d); m != mirrors.end()) {
            if (auto it = directions.find(m->second); it != directions.end() && !it->second.empty()) {
                if (flip) *flip = true;
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
        if (!frames.empty()) return &frames;
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
        sheet.clips[name] = std::move(clip);
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
    if (!registry.valid(entity) || !registry.all_of<Animator>(entity)) return;
    auto& state = registry.get_or_emplace<AnimatorState>(entity);
    if (trigger == "attacking") state.attacking = true;
    else if (trigger == "hurt") state.hurt = true;
    else if (trigger == "casting") state.casting = true;
    else if (trigger == "using") state.using_ = true;
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
        if (best) {
            state.state = best->to;
            state.time = 0.0f;
            state.frame = -1;
            current = def->Find(state.state);
        }
        state.attacking = state.hurt = state.casting = state.using_ = false; // triggers are read once
        if (!current) continue;
        // The clip: the state's in this sheet, else idle, else any.
        const AnimClip* clip = sheet->Clip(current->clip);
        if (!clip) clip = sheet->Clip("idle");
        if (!clip && !sheet->clips.empty()) clip = &sheet->clips.begin()->second;
        if (!clip) continue;
        state.clip = clip->name;
        bool flip = false;
        const auto* frames = clip->FramesFor(state.direction, &flip);
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
        if (ended && current->exitAtEnd) {
            state.state = def->defaultState;
            state.time = 0.0f;
            state.frame = -1;
        }
    }
    return fired;
}

} // namespace aige
