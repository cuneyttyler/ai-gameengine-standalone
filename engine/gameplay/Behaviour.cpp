#include "gameplay/Behaviour.h"

#include "gameplay/Combat.h"
#include "gameplay/Components.h"
#include "gameplay/GameSession.h"
#include "gameplay/Pathfinding.h"
#include "gameplay/Schedule.h"
#include "gameplay/Story.h"
#include "physics2d/Components.h"
#include "records/Records.h"
#include "renderer2d/Components.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace aige {

namespace {

constexpr float kPi = 3.14159265f;

// --- Built-in templates (ai-packages.md §5) ----------------------------------------------

const char* kTemplates = R"JSON({
  "travel": {"type": "move-to", "to": "@location"},
  "stay": {"type": "sequence", "children": [{"type": "move-to", "to": "@location"}, {"type": "idle"}]},
  "wander": {"type": "loop", "children": [{"type": "sequence", "children": [
      {"type": "wander", "around": "@location", "radius": "@data:radius"},
      {"type": "wait", "min": 2, "max": 5}]}]},
  "sandbox": {"type": "loop", "children": [{"type": "random", "weights": [3, 2, 1], "children": [
      {"type": "use-object", "affordance": "sit|eat|drink|read|warm|pray", "around": "@location", "radius": "@data:radius"},
      {"type": "sequence", "children": [{"type": "wander", "around": "@location", "radius": "@data:radius"},
                                          {"type": "wait", "min": 2, "max": 6}]},
      {"type": "sequence", "children": [{"type": "bark", "text": "@idle"}, {"type": "wait", "min": 3, "max": 6}]}]}]},
  "sleep": {"type": "selector", "children": [
      {"type": "use-object", "type_": "bed", "affordance": "sleep", "around": "@bed", "radius": 12, "forever": true},
      {"type": "sequence", "children": [{"type": "move-to", "to": "@location"}, {"type": "idle", "hidden": true}]}]},
  "eat": {"type": "selector", "children": [
      {"type": "use-object", "affordance": "eat", "around": "@location", "radius": 10},
      {"type": "sequence", "children": [{"type": "move-to", "to": "@location"}, {"type": "wait", "seconds": 10}]}]},
  "work": {"type": "loop", "children": [{"type": "selector", "children": [
      {"type": "use-object", "type_": "@data:object", "affordance": "work", "around": "@location", "radius": 10},
      {"type": "sequence", "children": [{"type": "move-to", "to": "@location"}, {"type": "wait", "seconds": 8}]}]}]},
  "vendor": {"type": "loop", "children": [{"type": "selector", "children": [
      {"type": "use-object", "type_": "counter", "affordance": "work", "around": "@location", "radius": 10, "forever": true},
      {"type": "sequence", "children": [{"type": "move-to", "to": "@location"}, {"type": "idle"}]}]}]},
  "patrol": {"type": "patrol", "points": "@data:path", "wait": "@data:duration"},
  "guard": {"type": "sequence", "children": [{"type": "move-to", "to": "@location"}, {"type": "idle"}]},
  "follow": {"type": "follow", "target": "@target", "distance": "@data:distance"},
  "accompany": {"type": "follow", "target": "@player", "distance": 2},
  "escort": {"type": "escort", "target": "@target", "to": "@location", "wait": "@data:distance"},
  "flee": {"type": "flee", "from": "@target", "distance": "@data:distance"},
  "hide": {"type": "sequence", "children": [{"type": "move-to", "to": "@location"}, {"type": "idle", "hidden": true}]},
  "ambush": {"type": "sequence", "children": [{"type": "move-to", "to": "@location"},
                                               {"type": "ambush", "radius": "@data:radius"}]},
  "wait": {"type": "idle"},
  "use-object": {"type": "use-object", "type_": "@data:object", "around": "@location", "radius": 12},
  "approach-player": {"type": "talk-to-player", "dialogue": "@data:dialogue"},
  "script": {"type": "script", "script": "@data:script"}
})JSON";

void Flatten(const nlohmann::json& node, Procedure& out, std::string* error) {
    const int index = static_cast<int>(out.nodes.size());
    out.nodes.push_back({});
    ProcedureNode built;
    built.type = node.value("type", std::string());
    built.params = node;
    built.params.erase("children");
    built.params.erase("type");
    if (built.params.contains("type_")) built.params["type"] = built.params["type_"]; // a smart object's type
    std::vector<int> children;
    for (const auto& child : node.value("children", nlohmann::json::array())) {
        children.push_back(static_cast<int>(out.nodes.size()));
        Flatten(child, out, error);
    }
    built.children = std::move(children);
    out.nodes[index] = std::move(built);
}

std::map<std::string, Procedure>& BuiltIns() {
    static std::map<std::string, Procedure> procedures = [] {
        std::map<std::string, Procedure> out;
        const nlohmann::json all = nlohmann::json::parse(kTemplates);
        for (const auto& [name, tree] : all.items()) {
            if (auto procedure = ParseProcedure(tree)) out[name] = *procedure;
        }
        return out;
    }();
    return procedures;
}

float Randf(std::mt19937* rng, float lo, float hi) {
    if (hi <= lo) return lo;
    static std::mt19937 fallback{0xA1u};
    return std::uniform_real_distribution<float>(lo, hi)(rng ? *rng : fallback);
}

std::string PickAlternative(const std::string& text, std::mt19937* rng) {
    std::vector<std::string> options;
    std::stringstream in(text);
    std::string part;
    while (std::getline(in, part, '|')) {
        if (!part.empty()) options.push_back(part);
    }
    if (options.empty()) return {};
    const int i = static_cast<int>(Randf(rng, 0.0f, static_cast<float>(options.size()) - 0.001f));
    return options[std::clamp(i, 0, static_cast<int>(options.size()) - 1)];
}

std::pair<int, int> Cell(float x, float y) {
    return {static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y))};
}

const nlohmann::json* PackageRecord(const std::string& id) {
    const Record* record = ActiveRecords().Find("package", id);
    return record ? &record->resolved : nullptr;
}

std::optional<std::pair<float, float>> MarkerPos(Registry& registry, const std::string& name) {
    if (name.empty()) return std::nullopt;
    for (auto [entity, marker, world] : registry.view<Marker, WorldTransform2D>().each()) {
        if (marker.name == name) return std::pair{world.x, world.y};
    }
    return std::nullopt;
}

std::optional<Entity> EntityNamed(Registry& registry, const std::string& name) {
    for (auto [entity, n] : registry.view<Name>().each()) {
        if (n.value == name) return entity;
    }
    for (auto [entity, talk] : registry.view<Talk>().each()) {
        if (talk.character == name) return entity;
    }
    return std::nullopt;
}

struct Run {
    AiContext& ctx;
    Entity actor;
    BehaviourState& state;
    const nlohmann::json* package; // may be null (combat, script)
    float dt;

    Registry& R() { return ctx.registry; }
    WorldTransform2D& At() { return R().get<WorldTransform2D>(actor); }

    // A parameter: "@data:<name>" reads the package's data.
    nlohmann::json Param(const ProcedureNode& node, const char* key, nlohmann::json fallback = nullptr) {
        if (!node.params.contains(key)) return fallback;
        const nlohmann::json& v = node.params[key];
        if (v.is_string() && v.get<std::string>().rfind("@data:", 0) == 0) {
            const std::string name = v.get<std::string>().substr(6);
            if (package && package->contains("data") && (*package)["data"].contains(name)) {
                const nlohmann::json& d = (*package)["data"][name];
                if (!d.is_null() && !(d.is_string() && d.get<std::string>().empty()) &&
                    !(d.is_number() && d.get<double>() == 0.0) && !(d.is_array() && d.empty())) {
                    return d;
                }
            }
            return fallback;
        }
        return v;
    }
    float Num(const ProcedureNode& node, const char* key, float fallback) {
        const nlohmann::json v = Param(node, key, fallback);
        return v.is_number() ? v.get<float>() : fallback;
    }
    std::string Str(const ProcedureNode& node, const char* key, const std::string& fallback = {}) {
        const nlohmann::json v = Param(node, key, fallback);
        return v.is_string() ? v.get<std::string>() : fallback;
    }

    // A place: @location, @home, @work, @bed, @target, @player, @bb:<key>, a marker's name.
    std::optional<std::pair<float, float>> Point(const std::string& spec) {
        const auto& behaviour = R().get<Behaviour>(actor);
        if (spec == "@location" && package) {
            const nlohmann::json loc = package->value("location", nlohmann::json::object());
            const std::string alias = loc.value("alias", std::string("none"));
            if (alias == "home") return Point("@home");
            if (alias == "work") return Point("@work");
            if (alias == "bed") return Point("@bed");
            if (auto m = MarkerPos(R(), loc.value("marker", std::string()))) return m;
            if (loc.value("x", 0.0f) != 0.0f || loc.value("y", 0.0f) != 0.0f) return std::pair{loc.value("x", 0.0f), loc.value("y", 0.0f)};
            return Point("@home");
        }
        if (spec == "@home") return MarkerPos(R(), behaviour.home).value_or(std::pair{At().x, At().y});
        if (spec == "@work") return MarkerPos(R(), behaviour.work).value_or(std::pair{At().x, At().y});
        if (spec == "@bed") {
            if (auto m = MarkerPos(R(), behaviour.bed)) return m;
            return Point("@home");
        }
        if (spec == "@player" || (spec == "@target" && package && package->value("target", std::string("player")) == "player")) {
            if (auto player = FindPlayer(R())) {
                const auto& w = R().get<WorldTransform2D>(*player);
                return std::pair{w.x, w.y};
            }
            return std::nullopt;
        }
        if (spec == "@target" && package) {
            const std::string target = package->value("target", std::string());
            if (auto e = EntityNamed(R(), target); e && R().all_of<WorldTransform2D>(*e)) {
                const auto& w = R().get<WorldTransform2D>(*e);
                return std::pair{w.x, w.y};
            }
            return MarkerPos(R(), target);
        }
        if (spec.rfind("@bb:", 0) == 0) {
            auto it = state.blackboard.find(spec.substr(4));
            if (it != state.blackboard.end() && it->second.is_array() && it->second.size() == 2) {
                return std::pair{it->second[0].get<float>(), it->second[1].get<float>()};
            }
            return std::nullopt;
        }
        return MarkerPos(R(), spec);
    }

    // Walks toward a cell; true on arrival (within `radius` cells).
    bool Walk(std::pair<float, float> to, float radius, float speedFactor = 1.0f) {
        auto& at = At();
        const float dxT = to.first - at.x, dyT = to.second - at.y;
        if (std::sqrt(dxT * dxT + dyT * dyT) <= radius) {
            state.path.clear();
            return true;
        }
        const auto goal = Cell(to.first, to.second);
        if (goal != state.goal || state.next >= state.path.size()) {
            state.goal = goal;
            state.path.clear();
            state.next = 0;
            if (auto path = FindPath(ctx.isSolid, Cell(at.x, at.y), goal, 6000)) state.path = *path;
            state.stuck = 0.0f;
            if (state.path.empty()) return false;
        }
        auto [gx, gy] = state.path[state.next];
        float tx = gx + 0.5f, ty = gy + 0.5f;
        if (state.next + 1 == state.path.size()) {
            tx = to.first;
            ty = to.second;
        }
        const float dx = tx - at.x, dy = ty - at.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (distance < 0.12f) {
            ++state.next;
            return false;
        }
        const float speed = R().get<Behaviour>(actor).speed * speedFactor;
        const float step = std::min(speed * dt, distance);
        Vec2 half{0.3f, 0.3f};
        if (const auto* c = R().try_get<Collider2D>(actor)) half = {c->halfWidth, c->halfHeight};
        const Vec2 moved = ResolveAabbMovement({at.x, at.y}, half, {dx / distance * step, dy / distance * step}, ctx.isSolid);
        const float progress = std::hypot(moved.x - at.x, moved.y - at.y);
        at.x = moved.x;
        at.y = moved.y;
        R().emplace_or_replace<Facing>(actor, dx / distance, dy / distance);
        state.stuck = progress < step * 0.25f ? state.stuck + dt : 0.0f;
        if (state.stuck > 1.0f) state.path.clear(); // blocked: plan again
        return false;
    }

    void Hide(bool hidden) {
        if (hidden == state.hidden) return;
        state.hidden = hidden;
        if (auto* sprite = R().try_get<Sprite>(actor)) {
            if (hidden) {
                state.savedAlpha = sprite->a;
                sprite->a = 0.0f;
            } else {
                sprite->a = state.savedAlpha;
            }
        }
        if (hidden) R().emplace_or_replace<Sleeping>(actor, state.savedAlpha);
        else R().remove<Sleeping>(actor);
    }

    void Release() {
        state.using_ = entt::null;
        Hide(false);
    }

    void Reset(int index, const Procedure& p) {
        state.nodeState.erase(index);
        for (int child : p.nodes[index].children) Reset(child, p);
    }

    bool ObjectFree(Entity object) {
        const auto* so = R().try_get<SmartObject>(object);
        if (!so) return false;
        int users = 0;
        for (auto [e, s] : R().view<BehaviourState>().each()) {
            if (e != actor && s.using_ == object) ++users;
        }
        if (users >= std::max(1, so->slots)) return false;
        if (!so->owner.empty()) {
            const auto* name = R().try_get<Name>(actor);
            const auto* talk = R().try_get<Talk>(actor);
            if (!(name && name->value == so->owner) && !(talk && talk->character == so->owner)) return false;
        }
        return true;
    }

    AiStatus Tick(const Procedure& p, int index) {
        const ProcedureNode& node = p.nodes[index];
        nlohmann::json& ns = state.nodeState[index];
        if (ns.is_null()) ns = nlohmann::json::object();
        const std::string& type = node.type;

        // --- Composites and decorators ---
        if (type == "sequence" || type == "selector" || type == "stack") {
            const bool sequence = type == "sequence";
            int i = ns.value("i", 0);
            while (i < static_cast<int>(node.children.size())) {
                const AiStatus status = Tick(p, node.children[i]);
                if (status == AiStatus::Running) {
                    ns["i"] = i;
                    return AiStatus::Running;
                }
                if ((status == AiStatus::Success) != sequence) {
                    Reset(index, p);
                    return status;
                }
                ++i;
            }
            Reset(index, p);
            return sequence ? AiStatus::Success : AiStatus::Failure;
        }
        if (type == "random") {
            if (!ns.contains("pick")) {
                const nlohmann::json weights = node.params.value("weights", nlohmann::json::array());
                float total = 0.0f;
                for (size_t i = 0; i < node.children.size(); ++i) total += i < weights.size() ? weights[i].get<float>() : 1.0f;
                float roll = Randf(ctx.rng, 0.0f, total);
                int pick = 0;
                for (size_t i = 0; i < node.children.size(); ++i) {
                    roll -= i < weights.size() ? weights[i].get<float>() : 1.0f;
                    if (roll <= 0.0f) {
                        pick = static_cast<int>(i);
                        break;
                    }
                }
                ns["pick"] = pick;
            }
            if (node.children.empty()) return AiStatus::Failure;
            const AiStatus status = Tick(p, node.children[ns.value("pick", 0)]);
            if (status != AiStatus::Running) Reset(index, p);
            return status;
        }
        if (type == "parallel") {
            AiStatus result = AiStatus::Running;
            for (int child : node.children) {
                const AiStatus s = Tick(p, child);
                if (s != AiStatus::Running) result = s; // any child finishing ends it
            }
            if (result != AiStatus::Running) Reset(index, p);
            return result;
        }
        if (type == "loop") {
            if (node.children.empty()) return AiStatus::Failure;
            const int count = static_cast<int>(Num(node, "count", 0.0f));
            const AiStatus s = Tick(p, node.children[0]);
            if (s == AiStatus::Running) return s;
            const int done = ns.value("done", 0) + 1;
            Reset(node.children[0], p);
            if (count > 0 && done >= count) {
                Reset(index, p);
                return AiStatus::Success;
            }
            state.nodeState[index]["done"] = done;
            return AiStatus::Running; // again next step
        }
        if (type == "invert") {
            if (node.children.empty()) return AiStatus::Failure;
            const AiStatus s = Tick(p, node.children[0]);
            return s == AiStatus::Running ? s : s == AiStatus::Success ? AiStatus::Failure : AiStatus::Success;
        }
        if (type == "condition") {
            if (node.children.empty()) return AiStatus::Failure;
            nlohmann::json check = {{"conditions", node.params.value("if", nlohmann::json::array())},
                                    {"schedule", node.params.value("time", nlohmann::json::object())}};
            if (!PackageHolds(check, ctx, actor)) return AiStatus::Failure;
            return Tick(p, node.children[0]);
        }
        if (type == "cooldown") {
            if (node.children.empty()) return AiStatus::Failure;
            float left = ns.value("left", 0.0f);
            if (left > 0.0f) {
                ns["left"] = left - dt;
                return AiStatus::Failure;
            }
            const AiStatus s = Tick(p, node.children[0]);
            if (s != AiStatus::Running) state.nodeState[index] = {{"left", Num(node, "seconds", 5.0f)}};
            return s;
        }

        // --- Leaves ---
        if (type == "move-to") {
            const auto to = Point(Str(node, "to", "@location"));
            if (!to) return AiStatus::Failure;
            return Walk(*to, Num(node, "radius", 0.5f)) ? AiStatus::Success : AiStatus::Running;
        }
        if (type == "wander") {
            if (!ns.contains("x")) {
                const auto around = Point(Str(node, "around", "@location")).value_or(std::pair{At().x, At().y});
                const float radius = Num(node, "radius", 3.0f);
                std::pair<float, float> pick = around;
                for (int attempt = 0; attempt < 10; ++attempt) {
                    const float a = Randf(ctx.rng, 0.0f, 2.0f * kPi), r = Randf(ctx.rng, 0.0f, radius);
                    const auto cell = Cell(around.first + std::cos(a) * r, around.second + std::sin(a) * r);
                    if (!ctx.isSolid(cell.first, cell.second)) {
                        pick = {cell.first + 0.5f, cell.second + 0.5f};
                        break;
                    }
                }
                ns["x"] = pick.first;
                ns["y"] = pick.second;
                ns["t"] = 0.0f;
            }
            ns["t"] = ns.value("t", 0.0f) + dt;
            if (Walk({ns.value("x", 0.0f), ns.value("y", 0.0f)}, 0.4f) || ns.value("t", 0.0f) > 12.0f) {
                Reset(index, p);
                return AiStatus::Success;
            }
            return AiStatus::Running;
        }
        if (type == "wait") {
            if (!ns.contains("left")) {
                const float seconds = Num(node, "seconds", -1.0f);
                ns["left"] = seconds >= 0.0f ? seconds : Randf(ctx.rng, Num(node, "min", 2.0f), Num(node, "max", 5.0f));
            }
            const float left = ns.value("left", 0.0f) - dt;
            if (left <= 0.0f) {
                Reset(index, p);
                return AiStatus::Success;
            }
            ns["left"] = left;
            return AiStatus::Running;
        }
        if (type == "idle") {
            Hide(node.params.value("hidden", false));
            return AiStatus::Running;
        }
        if (type == "face") {
            if (const auto to = Point(Str(node, "to", "@player"))) {
                const float dx = to->first - At().x, dy = to->second - At().y;
                const float l = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
                R().emplace_or_replace<Facing>(actor, dx / l, dy / l);
            }
            return AiStatus::Success;
        }
        if (type == "bark") {
            std::string text = Str(node, "text");
            if (text == "@idle") text = R().all_of<Barks>(actor) ? R().get<Barks>(actor).idle : std::string();
            if (!text.empty() && ctx.bark) {
                // Only near the player, so the world isn't full of chatter.
                if (auto player = FindPlayer(R())) {
                    const auto& pw = R().get<WorldTransform2D>(*player);
                    if (std::hypot(pw.x - At().x, pw.y - At().y) < 7.0f) ctx.bark(actor, PickAlternative(text, ctx.rng));
                }
            }
            return AiStatus::Success;
        }
        if (type == "use-object") {
            if (!ns.contains("object")) {
                const std::string wantType = Str(node, "type");
                const std::string affordance = Str(node, "affordance");
                const auto around = Point(Str(node, "around", "@location")).value_or(std::pair{At().x, At().y});
                const float radius = Num(node, "radius", 10.0f);
                std::optional<Entity> best;
                float bestD = 1e9f;
                for (auto [e, so, w] : R().view<SmartObject, WorldTransform2D>().each()) {
                    if (!wantType.empty() && wantType.find(so.type) == std::string::npos) continue;
                    if (!affordance.empty()) {
                        bool ok = false;
                        std::stringstream in(affordance);
                        std::string a;
                        while (std::getline(in, a, '|')) ok |= so.affordances.find(a) != std::string::npos;
                        if (!ok) continue;
                    }
                    const float d = std::hypot(w.x - around.first, w.y - around.second);
                    if (d > radius || !ObjectFree(e)) continue;
                    if (d < bestD) bestD = d, best = e;
                }
                if (!best) return AiStatus::Failure;
                ns["object"] = static_cast<uint32_t>(*best);
                ns["phase"] = 0;
            }
            const Entity object = static_cast<Entity>(ns.value("object", 0u));
            if (!R().valid(object) || !R().all_of<SmartObject, WorldTransform2D>(object)) {
                Release();
                Reset(index, p);
                return AiStatus::Failure;
            }
            const auto& so = R().get<SmartObject>(object);
            const auto& ow = R().get<WorldTransform2D>(object);
            if (ns.value("phase", 0) == 0) {
                state.using_ = object; // claim it on the way
                if (!Walk({ow.x, ow.y}, 1.1f)) return AiStatus::Running;
                ns["phase"] = 1;
                ns["left"] = node.params.value("forever", false) ? 1e9f : Randf(ctx.rng, so.minDuration, so.maxDuration);
                const float dx = ow.x - At().x, dy = ow.y - At().y;
                const float l = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
                R().emplace_or_replace<Facing>(actor, dx / l, dy / l);
                if (so.affordances.find("sleep") != std::string::npos) {
                    At().x = ow.x; // in the bed
                    At().y = ow.y;
                    Hide(true);
                }
            }
            const float left = ns.value("left", 0.0f) - dt;
            if (left <= 0.0f) {
                Release();
                Reset(index, p);
                return AiStatus::Success;
            }
            ns["left"] = left;
            return AiStatus::Running;
        }
        if (type == "talk-to-player") {
            auto player = FindPlayer(R());
            if (!player || (ns.value("done", false))) return AiStatus::Success;
            const auto& pw = R().get<WorldTransform2D>(*player);
            if (!Walk({pw.x, pw.y}, 1.3f)) return AiStatus::Running;
            const std::string dialogue = Str(node, "dialogue");
            if (!dialogue.empty() && ctx.startDialogue) ctx.startDialogue(actor, dialogue);
            ns["done"] = true;
            return AiStatus::Success;
        }
        if (type == "follow") {
            const auto to = Point(Str(node, "target", "@player"));
            if (!to) return AiStatus::Running;
            const float distance = Num(node, "distance", 2.0f);
            const float d = std::hypot(to->first - At().x, to->second - At().y);
            if (d > distance + 0.5f) Walk(*to, distance, d > distance + 4.0f ? 1.6f : 1.0f);
            else state.path.clear();
            return AiStatus::Running;
        }
        if (type == "flee") {
            const auto from = Point(Str(node, "from", "@player"));
            if (!from) return AiStatus::Running;
            const float distance = Num(node, "distance", 8.0f);
            const float dx = At().x - from->first, dy = At().y - from->second;
            const float d = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
            if (d < distance) Walk({At().x + dx / d * 3.0f, At().y + dy / d * 3.0f}, 0.4f, 1.4f);
            return AiStatus::Running;
        }
        if (type == "patrol") {
            std::vector<std::string> points;
            const nlohmann::json list = Param(node, "points", nlohmann::json::array());
            for (const auto& v : list) {
                if (v.is_string()) points.push_back(v.get<std::string>());
            }
            if (points.empty()) return AiStatus::Failure;
            int i = ns.value("i", 0) % static_cast<int>(points.size());
            const auto to = MarkerPos(R(), points[i]);
            if (!to) {
                ns["i"] = i + 1;
                return AiStatus::Running;
            }
            if (ns.value("waiting", 0.0f) > 0.0f) {
                ns["waiting"] = ns.value("waiting", 0.0f) - dt;
                return AiStatus::Running;
            }
            if (Walk(*to, 0.4f)) {
                ns["i"] = i + 1;
                ns["waiting"] = Num(node, "wait", 2.0f);
            }
            return AiStatus::Running;
        }
        if (type == "ambush") {
            Hide(false);
            if (auto* sprite = R().try_get<Sprite>(actor)) sprite->a = std::min(sprite->a, 0.35f); // lying low
            auto player = FindPlayer(R());
            if (player && AreHostile(R(), actor, *player)) {
                const auto& pw = R().get<WorldTransform2D>(*player);
                if (std::hypot(pw.x - At().x, pw.y - At().y) <= Num(node, "radius", 3.0f)) {
                    if (auto* sprite = R().try_get<Sprite>(actor)) sprite->a = state.savedAlpha > 0.0f ? state.savedAlpha : 1.0f;
                    state.awareness = 1.0f;
                    state.target = *player;
                    state.mode = BehaviourState::Mode::Combat;
                    return AiStatus::Success;
                }
            }
            return AiStatus::Running;
        }
        if (type == "escort") {
            const auto target = Point(Str(node, "target", "@player"));
            const auto to = Point(Str(node, "to", "@location"));
            if (!to) return AiStatus::Failure;
            if (target && std::hypot(target->first - At().x, target->second - At().y) > Num(node, "wait", 4.0f)) {
                state.path.clear(); // wait for them
                return AiStatus::Running;
            }
            return Walk(*to, 0.6f) ? AiStatus::Success : AiStatus::Running;
        }
        if (type == "set") {
            state.blackboard[Str(node, "key")] = Param(node, "value");
            return AiStatus::Success;
        }
        if (type == "emit") {
            if (ctx.emit) ctx.emit(Str(node, "event"), actor);
            return AiStatus::Success;
        }
        if (type == "script") {
            const std::string script = Str(node, "script");
            if (script.empty() || !ctx.runScript) return AiStatus::Failure;
            const bool running = ctx.runScript(actor, script, dt);
            // A script's move_to sets where to walk; the walking happens here.
            if (auto it = state.blackboard.find("scriptGoal"); it != state.blackboard.end() && it->second.is_array()) {
                const bool arrived = Walk({it->second[0].get<float>(), it->second[1].get<float>()}, 0.4f);
                state.blackboard["scriptArrived"] = arrived;
            }
            return running ? AiStatus::Running : AiStatus::Success;
        }
        return AiStatus::Failure; // unknown node
    }
};

bool CanSee(AiContext& ctx, Entity actor, Entity target) {
    const auto* p = ctx.registry.try_get<Perception>(actor);
    const float sight = p ? p->sight : 6.0f;
    const float angle = p ? p->angle : 140.0f;
    const auto& a = ctx.registry.get<WorldTransform2D>(actor);
    const auto& b = ctx.registry.get<WorldTransform2D>(target);
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float d = std::sqrt(dx * dx + dy * dy);
    if (d > sight) return false;
    if (d > 1.0f) {
        const auto* facing = ctx.registry.try_get<Facing>(actor);
        if (facing) {
            const float cosA = (facing->x * dx + facing->y * dy) / (std::max(0.001f, std::hypot(facing->x, facing->y)) * d);
            if (cosA < std::cos(angle * 0.5f * kPi / 180.0f)) return false;
        }
    }
    for (float t = 0.5f; t < d; t += 0.5f) {
        if (ctx.isSolid(static_cast<int>(std::floor(a.x + dx / d * t)), static_cast<int>(std::floor(a.y + dy / d * t)))) return false;
    }
    return true;
}

nlohmann::json CombatStyle(const Behaviour& behaviour) {
    if (const Record* r = ActiveRecords().Find("combat-style", behaviour.combatStyle)) return r->resolved;
    return nlohmann::json::object();
}

float FleeThreshold(const Behaviour& behaviour, const nlohmann::json& style) {
    float flee = style.value("fleeAt", 0.2f);
    switch (behaviour.confidence) {
        case Confidence::Cowardly: return std::min(1.0f, flee * 2.5f + 0.2f);
        case Confidence::Cautious: return flee * 1.5f;
        case Confidence::Average: return flee;
        case Confidence::Brave: return flee * 0.5f;
        case Confidence::Foolhardy: return 0.0f;
    }
    return flee;
}

void CallForHelp(AiContext& ctx, Entity actor, Entity enemy, float radius) {
    const auto& at = ctx.registry.get<WorldTransform2D>(actor);
    for (auto [other, behaviour, w] : ctx.registry.view<Behaviour, WorldTransform2D>(entt::exclude<Dead>).each()) {
        if (other == actor || behaviour.assistance == Assistance::Nobody) continue;
        if (std::hypot(w.x - at.x, w.y - at.y) > radius) continue;
        if (AreHostile(ctx.registry, other, actor) || !AreHostile(ctx.registry, other, enemy)) continue;
        auto& s = ctx.registry.get_or_emplace<BehaviourState>(other);
        if (s.mode == BehaviourState::Mode::Package && behaviour.aggression != Aggression::Peaceful) {
            s.mode = BehaviourState::Mode::Combat;
            s.target = enemy;
            s.awareness = 1.0f;
        }
    }
}

} // namespace

std::optional<Procedure> ParseProcedure(const nlohmann::json& tree, std::string* error) {
    if (!tree.is_object() || tree.value("type", std::string()).empty()) {
        if (error) *error = "a procedure node needs a \"type\"";
        return std::nullopt;
    }
    Procedure procedure;
    Flatten(tree, procedure, error);
    return procedure;
}

const Procedure* FindProcedure(const std::string& name) {
    // The project's own procedures (records/procedure/<id>: {"tree": {...}}) win.
    static std::map<std::string, std::pair<std::string, Procedure>> projectCache;
    if (const Record* record = ActiveRecords().Find("procedure", name)) {
        const std::string text = record->resolved.value("tree", std::string());
        auto& cached = projectCache[name];
        if (cached.first != text) {
            const nlohmann::json tree = nlohmann::json::parse(text, nullptr, false);
            if (auto parsed = ParseProcedure(tree)) cached = {text, *parsed};
            else cached = {text, Procedure{}};
        }
        if (!cached.second.nodes.empty()) return &cached.second;
    }
    auto& builtIns = BuiltIns();
    auto it = builtIns.find(name);
    return it == builtIns.end() ? nullptr : &it->second;
}

std::vector<std::string> ProcedureNames() {
    std::vector<std::string> names;
    for (const auto& [name, p] : BuiltIns()) names.push_back(name);
    return names;
}

bool PackageHolds(const nlohmann::json& package, const AiContext& context, Entity actor) {
    (void)actor;
    const nlohmann::json schedule = package.value("schedule", nlohmann::json::object());
    const auto from = ParseClockTime(schedule.value("from", std::string()));
    const auto to = ParseClockTime(schedule.value("to", std::string()));
    if (from && to && *from != *to) {
        const int now = static_cast<int>(context.minuteOfDay);
        const bool inside = *from < *to ? (now >= *from && now < *to) : (now >= *from || now < *to);
        if (!inside) return false;
    }
    const std::string days = schedule.value("days", std::string());
    if (!days.empty() && days != "all") {
        const int weekday = (context.day - 1) % 7 + 1;
        if (days.find(std::to_string(weekday)) == std::string::npos) return false;
    }
    if (context.story) {
        std::vector<StoryCondition> conditions;
        for (const auto& c : package.value("conditions", nlohmann::json::array())) {
            StoryCondition condition;
            condition.flag = c.value("flag", std::string());
            condition.flagValue = c.value("flagValue", true);
            condition.quest = c.value("quest", std::string());
            condition.questState = c.value("questState", std::string("active"));
            if (!condition.flag.empty() || !condition.quest.empty()) conditions.push_back(condition);
        }
        if (!context.story->Holds(conditions)) return false;
    }
    return true;
}

void AlertActor(Registry& registry, Entity actor, Entity attacker) {
    if (!registry.valid(actor) || !registry.valid(attacker) || !registry.all_of<Behaviour>(actor)) return;
    auto& state = registry.get_or_emplace<BehaviourState>(actor);
    const auto& behaviour = registry.get<Behaviour>(actor);
    state.target = attacker;
    state.awareness = 1.0f;
    if (const auto* w = registry.try_get<WorldTransform2D>(attacker)) {
        state.lastX = w->x;
        state.lastY = w->y;
    }
    state.lostFor = 0.0f;
    state.mode = behaviour.aggression == Aggression::Peaceful || behaviour.confidence == Confidence::Cowardly
                     ? BehaviourState::Mode::Flee
                     : BehaviourState::Mode::Combat;
}

void PushPackage(Registry& registry, Entity actor, const std::string& package) {
    auto& state = registry.get_or_emplace<BehaviourState>(actor);
    if (std::find(state.pushed.begin(), state.pushed.end(), package) == state.pushed.end()) state.pushed.push_back(package);
    state.reconsider = 0.0f;
}

void PopPackage(Registry& registry, Entity actor, const std::string& package) {
    if (auto* state = registry.try_get<BehaviourState>(actor)) {
        std::erase(state->pushed, package);
        state->reconsider = 0.0f;
    }
}

std::string AffordancesForRole(const std::string& role) {
    static const std::map<std::string, std::string> table = {
        {"bed", "sleep"},        {"chair", "sit"},        {"stool", "sit"},       {"bench", "sit"},
        {"pew", "sit,pray"},     {"throne", "sit"},       {"table", "eat"},       {"counter", "work"},
        {"workbench", "work"},   {"anvil", "work"},       {"stall", "work"},      {"desk", "work,read"},
        {"fireplace", "warm"},   {"stove", "work,warm"},  {"campfire", "warm,eat"}, {"well", "drink"},
        {"fountain", "drink"},   {"trough", "drink"},     {"bookshelf", "read"},  {"lectern", "read"},
        {"altar", "pray"},       {"shrine", "pray"},      {"training-dummy", "work"}, {"weapon-rack", "work"},
    };
    auto it = table.find(role);
    return it == table.end() ? std::string() : it->second;
}

std::string DescribeBehaviour(Registry& registry, Entity actor) {
    const auto* state = registry.try_get<BehaviourState>(actor);
    if (!state) return {};
    switch (state->mode) {
        case BehaviourState::Mode::Combat: return "combat";
        case BehaviourState::Mode::Flee: return "fleeing";
        case BehaviourState::Mode::Search: return "searching";
        case BehaviourState::Mode::Package: break;
    }
    return state->package.empty() ? "idle" : state->package;
}

void UpdateBehaviours(AiContext& ctx, float dt) {
    Registry& registry = ctx.registry;
    const auto player = FindPlayer(registry);
    std::vector<Entity> actors;
    for (auto entity : registry.view<Behaviour, WorldTransform2D>(entt::exclude<Dead>)) actors.push_back(entity);
    for (Entity actor : actors) {
        if (!registry.valid(actor)) continue;
        auto& behaviour = registry.get<Behaviour>(actor);
        auto& state = registry.get_or_emplace<BehaviourState>(actor);
        auto& at = registry.get<WorldTransform2D>(actor);
        state.barkCooldown = std::max(0.0f, state.barkCooldown - dt);

        // --- Perception (14.5): sight and hearing of the player, when it's an enemy ---
        const bool hostileToPlayer = player && !registry.all_of<Dead>(*player) && AreHostile(registry, actor, *player);
        if (hostileToPlayer && state.mode == BehaviourState::Mode::Package) {
            const auto* p = registry.try_get<Perception>(actor);
            bool noticed = false;
            if (CanSee(ctx, actor, *player)) {
                const auto& pw = registry.get<WorldTransform2D>(*player);
                const float d = std::hypot(pw.x - at.x, pw.y - at.y);
                state.awareness += dt * (1.2f + 3.0f * std::max(0.0f, 1.0f - d / (p ? p->sight : 6.0f)));
                state.lastX = pw.x;
                state.lastY = pw.y;
                noticed = true;
            }
            for (const auto& noise : ctx.noises) {
                const float range = (p ? p->hearing : 6.0f) * noise.loudness;
                if (std::hypot(noise.x - at.x, noise.y - at.y) <= range) {
                    state.awareness = std::max(state.awareness, 0.6f);
                    state.lastX = noise.x;
                    state.lastY = noise.y;
                    noticed = true;
                }
            }
            if (!noticed) state.awareness = std::max(0.0f, state.awareness - dt * 0.15f);
            if (state.awareness >= 1.0f &&
                (behaviour.aggression == Aggression::Aggressive || behaviour.aggression == Aggression::Frenzied)) {
                state.mode = BehaviourState::Mode::Combat;
                state.target = *player;
                state.lostFor = 0.0f;
                if (const auto* barks = registry.try_get<Barks>(actor); barks && ctx.bark && state.barkCooldown <= 0.0f) {
                    ctx.bark(actor, PickAlternative(barks->alert, ctx.rng));
                    state.barkCooldown = 6.0f;
                }
                CallForHelp(ctx, actor, *player, CombatStyle(behaviour).value("callForHelp", 6.0f));
            }
        }

        // --- Combat and fleeing ---
        Run run{ctx, actor, state, nullptr, dt};
        if (state.mode == BehaviourState::Mode::Combat || state.mode == BehaviourState::Mode::Flee) {
            run.Release();
            if (!registry.valid(state.target) || registry.all_of<Dead>(state.target) ||
                !registry.all_of<WorldTransform2D>(state.target)) {
                state.mode = BehaviourState::Mode::Package;
                state.target = entt::null;
                state.awareness = 0.0f;
                continue;
            }
            const auto& tw = registry.get<WorldTransform2D>(state.target);
            if (CanSee(ctx, actor, state.target) || std::hypot(tw.x - at.x, tw.y - at.y) < 2.0f) {
                state.lastX = tw.x;
                state.lastY = tw.y;
                state.lostFor = 0.0f;
            } else {
                state.lostFor += dt;
            }
            const nlohmann::json style = CombatStyle(behaviour);
            const auto* health = registry.try_get<Health>(actor);
            const float fraction = health && health->max > 0.0f ? health->current / health->max : 1.0f;
            if (state.mode == BehaviourState::Mode::Combat && fraction < FleeThreshold(behaviour, style)) {
                state.mode = BehaviourState::Mode::Flee;
                if (const auto* barks = registry.try_get<Barks>(actor); barks && ctx.bark) ctx.bark(actor, PickAlternative(barks->flee, ctx.rng));
            }
            if (state.lostFor > 6.0f) {
                state.mode = BehaviourState::Mode::Search; // look where it was last seen
                state.reconsider = 4.0f;
                continue;
            }
            const float dx = tw.x - at.x, dy = tw.y - at.y;
            const float d = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
            if (state.mode == BehaviourState::Mode::Flee) {
                if (d > 10.0f) {
                    state.mode = BehaviourState::Mode::Package;
                    state.awareness = 0.4f;
                } else {
                    run.Walk({at.x - dx / d * 3.0f, at.y - dy / d * 3.0f}, 0.4f, 1.4f);
                }
                continue;
            }
            const std::string range = style.value("range", std::string("melee"));
            if (range == "melee" || !registry.all_of<RangedAttack>(actor)) {
                const auto* melee = registry.try_get<MeleeAttack>(actor);
                const float reach = melee ? melee->reach + melee->radius + 0.2f : 1.0f;
                if (d > reach) run.Walk({tw.x, tw.y}, reach * 0.8f, 1.3f);
                else {
                    registry.emplace_or_replace<Facing>(actor, dx / d, dy / d);
                    if (melee && melee->cooldownRemaining <= 0.0f) TryMeleeAttack(registry, actor, dx, dy);
                }
            } else {
                // Ranged and kiting: keep at a distance; shots come from the archers' step.
                const float keep = style.value("keepDistance", registry.get<RangedAttack>(actor).range * 0.6f);
                if (d < keep - 1.0f) run.Walk({at.x - dx / d * 2.0f, at.y - dy / d * 2.0f}, 0.4f, 1.2f);
                else if (d > keep + 1.0f) run.Walk({tw.x, tw.y}, keep, 1.2f);
                else registry.emplace_or_replace<Facing>(actor, dx / d, dy / d);
            }
            continue;
        }
        if (state.mode == BehaviourState::Mode::Search) {
            state.reconsider -= dt;
            if (run.Walk({state.lastX, state.lastY}, 0.6f) && state.reconsider < 0.0f) {
                state.mode = BehaviourState::Mode::Package;
                state.awareness = 0.3f;
            }
            continue;
        }

        // --- The package stack ---
        state.reconsider -= dt;
        if (state.reconsider <= 0.0f || state.package.empty()) {
            state.reconsider = 1.0f;
            std::vector<std::string> stack(state.pushed.rbegin(), state.pushed.rend());
            std::stringstream in(behaviour.packages);
            std::string id;
            while (std::getline(in, id, ',')) {
                id.erase(0, id.find_first_not_of(' '));
                id.erase(id.find_last_not_of(' ') + 1);
                if (!id.empty()) stack.push_back(id);
            }
            std::string chosen;
            for (const auto& candidate : stack) {
                if (const nlohmann::json* package = PackageRecord(candidate); package && PackageHolds(*package, ctx, actor)) {
                    chosen = candidate;
                    break;
                }
            }
            if (chosen != state.package) {
                const nlohmann::json* current = state.package.empty() ? nullptr : PackageRecord(state.package);
                const bool mustComplete = current && current->value("flags", nlohmann::json::object()).value("mustComplete", false);
                if (!mustComplete || state.nodeState.empty()) {
                    run.Release();
                    state.package = chosen;
                    state.nodeState.clear();
                    state.path.clear();
                    if (ctx.emit) ctx.emit("package_started", actor);
                }
            }
        }
        if (state.package.empty()) continue;
        const nlohmann::json* package = PackageRecord(state.package);
        if (!package) continue;
        // Elsewhere: a package in another scene takes the actor away (14.4).
        const std::string where = package->value("location", nlohmann::json::object()).value("scene", std::string());
        if (!where.empty() && !ctx.scene.empty() && where != ctx.scene) {
            run.Hide(true);
            continue;
        }
        const Procedure* procedure = FindProcedure(package->value("template", std::string("stay")));
        if (!procedure || procedure->nodes.empty()) continue;
        run.package = package;
        const AiStatus status = run.Tick(*procedure, 0);
        if (status != AiStatus::Running) state.nodeState.clear(); // start it again next step

        // Idle remarks near the player.
        if (const auto* barks = registry.try_get<Barks>(actor); barks && !barks->idle.empty() && ctx.bark && player) {
            state.idleBarkIn -= dt;
            const auto& pw = registry.get<WorldTransform2D>(*player);
            if (state.idleBarkIn <= 0.0f && std::hypot(pw.x - at.x, pw.y - at.y) < 5.0f) {
                ctx.bark(actor, PickAlternative(barks->idle, ctx.rng));
                state.idleBarkIn = Randf(ctx.rng, 25.0f, 50.0f);
            }
        }
    }
}

} // namespace aige
