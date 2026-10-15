#include "gameplay/Schedule.h"

#include "gameplay/Components.h"
#include "gameplay/Pathfinding.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <set>

namespace aige {

namespace fs = std::filesystem;

namespace {

const std::set<std::string> kActivities = {"stay", "wander", "sleep"};

uint64_t NextRandom(uint64_t& state) {
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

std::pair<int, int> CellOf(float x, float y) {
    return {static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y))};
}

} // namespace

std::optional<int> ParseClockTime(const std::string& text) {
    int h = 0, m = 0;
    char colon = 0;
    if (text.size() < 4 || text.size() > 5) return std::nullopt;
    size_t pos = text.find(':');
    if (pos == std::string::npos || pos == 0 || pos + 3 != text.size()) return std::nullopt;
    for (size_t i = 0; i < text.size(); ++i) {
        if (i != pos && !std::isdigit(static_cast<unsigned char>(text[i]))) return std::nullopt;
    }
    h = std::stoi(text.substr(0, pos));
    m = std::stoi(text.substr(pos + 1));
    (void)colon;
    if (h < 0 || h > 23 || m < 0 || m > 59) return std::nullopt;
    return h * 60 + m;
}

std::string FormatClockTime(int minuteOfDay) {
    minuteOfDay = ((minuteOfDay % kMinutesPerDay) + kMinutesPerDay) % kMinutesPerDay;
    char text[6];
    std::snprintf(text, sizeof text, "%02d:%02d", minuteOfDay / 60, minuteOfDay % 60);
    return text;
}

size_t Schedule::IndexAt(float minuteOfDay) const {
    size_t index = entries.size() - 1; // before the first entry: yesterday's last
    for (size_t i = 0; i < entries.size(); ++i) {
        if (static_cast<float>(entries[i].minute) <= minuteOfDay) index = i;
    }
    return index;
}

const ScheduleEntry* Schedule::At(float minuteOfDay) const {
    return entries.empty() ? nullptr : &entries[IndexAt(minuteOfDay)];
}

std::optional<Schedule> ScheduleFromJson(const nlohmann::json& json, const std::string& id, std::string* error) {
    if (!json.is_object() || !json.contains("entries") || !json["entries"].is_array()) {
        if (error) *error = "needs an \"entries\" list";
        return std::nullopt;
    }
    Schedule schedule;
    schedule.id = id;
    schedule.name = json.value("name", "");
    for (const auto& item : json["entries"]) {
        if (!item.is_object()) {
            if (error) *error = "entries must be objects";
            return std::nullopt;
        }
        auto minute = ParseClockTime(item.value("from", ""));
        if (!minute) {
            if (error) *error = "'" + item.value("from", "") + "' isn't a time like 06:30";
            return std::nullopt;
        }
        ScheduleEntry entry;
        entry.minute = *minute;
        entry.activity = item.value("activity", "");
        entry.place = item.value("place", "");
        entry.radius = item.value("radius", 2.0f);
        schedule.entries.push_back(entry);
    }
    std::stable_sort(schedule.entries.begin(), schedule.entries.end(),
                     [](const ScheduleEntry& a, const ScheduleEntry& b) { return a.minute < b.minute; });
    return schedule;
}

nlohmann::json ScheduleToJson(const Schedule& schedule) {
    nlohmann::json entries = nlohmann::json::array();
    for (const auto& e : schedule.entries) {
        nlohmann::json item = {{"from", FormatClockTime(e.minute)}, {"activity", e.activity}, {"place", e.place}};
        if (e.activity == "wander") item["radius"] = e.radius;
        entries.push_back(item);
    }
    return {{"name", schedule.name}, {"entries", entries}};
}

std::vector<std::string> ValidateSchedule(const Schedule& schedule,
                                          const std::function<bool(const std::string&)>& markerExists) {
    std::vector<std::string> problems;
    if (schedule.entries.empty()) problems.push_back("has no entries");
    std::set<int> starts;
    for (const auto& e : schedule.entries) {
        const std::string where = "the " + FormatClockTime(e.minute) + " entry";
        if (!kActivities.contains(e.activity)) problems.push_back(where + ": activity should be stay, wander or sleep");
        if (e.place.empty()) problems.push_back(where + " has no place");
        else if (!markerExists(e.place)) problems.push_back(where + ": no marker called '" + e.place + "' in the scene");
        if (e.activity == "wander" && (e.radius <= 0.0f || e.radius > 20.0f))
            problems.push_back(where + ": wander radius should be between 0 and 20 cells");
        if (!starts.insert(e.minute).second) problems.push_back("two entries start at " + FormatClockTime(e.minute));
    }
    return problems;
}

std::optional<std::string> WriteScheduleFile(const fs::path& projectRoot, const std::string& id,
                                             const nlohmann::json& content) {
    bool safe = !id.empty() && std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (std::isalnum(c) && !std::isupper(c)) || c == '-' || c == '_';
    });
    if (!safe) return "file names are lowercase letters, digits, - and _";
    std::string error;
    auto schedule = ScheduleFromJson(content, id, &error);
    if (!schedule) return error;
    std::error_code ec;
    fs::create_directories(projectRoot / "schedules", ec);
    std::ofstream out(projectRoot / "schedules" / (id + ".json"), std::ios::binary | std::ios::trunc);
    if (!out) return "couldn't write schedules/" + id + ".json";
    out << ScheduleToJson(*schedule).dump(2) << "\n";
    return std::nullopt;
}

ScheduleLibrary::ScheduleLibrary(fs::path projectRoot) : root_(std::move(projectRoot)) {}

void ScheduleLibrary::Refresh() {
    schedules_.clear();
    loadProblems_.clear();
    std::error_code ec;
    std::vector<fs::path> files;
    if (fs::is_directory(root_ / "schedules", ec)) {
        for (const auto& file : fs::directory_iterator(root_ / "schedules", ec)) {
            if (file.is_regular_file() && file.path().extension() == ".json") files.push_back(file.path());
        }
    }
    std::sort(files.begin(), files.end());
    for (const auto& path : files) {
        std::ifstream in(path, std::ios::binary);
        nlohmann::json json = nlohmann::json::parse(in, nullptr, false);
        std::string error = json.is_discarded() ? "isn't JSON" : "";
        std::optional<Schedule> schedule;
        if (error.empty()) schedule = ScheduleFromJson(json, path.stem().string(), &error);
        if (schedule) schedules_.push_back(std::move(*schedule));
        else loadProblems_.push_back({"schedules/" + path.filename().string(), error});
    }
}

const Schedule* ScheduleLibrary::Find(const std::string& idOrPath) const {
    std::string id = idOrPath;
    if (id.rfind("schedules/", 0) == 0) id = fs::path(id).stem().string();
    for (const auto& s : schedules_) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

std::vector<std::pair<std::string, std::pair<float, float>>> MarkerPositions(Registry& registry) {
    std::vector<std::pair<std::string, std::pair<float, float>>> markers;
    for (auto [entity, marker, world] : registry.view<Marker, WorldTransform2D>().each()) {
        markers.push_back({marker.name, {world.x, world.y}});
    }
    return markers;
}

void UpdateRoutines(Registry& registry, const ScheduleLibrary& schedules, float minuteOfDay, const SolidQueryFn& isSolid,
                    float dt, uint64_t& rng) {
    auto markers = MarkerPositions(registry);
    auto markerAt = [&](const std::string& name) -> std::optional<std::pair<float, float>> {
        for (const auto& [n, pos] : markers)
            if (n == name) return pos;
        return std::nullopt;
    };
    auto view = registry.view<Routine, WorldTransform2D>(entt::exclude<Dead>);
    for (auto entity : view) {
        const Routine& routine = view.get<Routine>(entity);
        const Schedule* schedule = schedules.Find(routine.schedule);
        if (schedule == nullptr || schedule->entries.empty()) continue;
        auto& state = registry.get_or_emplace<RoutineState>(entity);
        auto& world = view.get<WorldTransform2D>(entity);
        const int index = static_cast<int>(schedule->IndexAt(minuteOfDay));
        const ScheduleEntry& entry = schedule->entries[static_cast<size_t>(index)];
        auto place = markerAt(entry.place);

        auto planTo = [&](std::pair<int, int> goal) {
            state.path.clear();
            state.next = 0;
            if (auto path = FindPath(isSolid, CellOf(world.x, world.y), goal)) state.path = *path;
            state.stuck = 0.0f;
        };

        if (index != state.entry) {
            // A new part of the day: wake up, and head for the place.
            if (auto* sleeping = registry.try_get<Sleeping>(entity)) {
                if (auto* sprite = registry.try_get<Sprite>(entity)) sprite->a = sleeping->alpha;
                registry.remove<Sleeping>(entity);
            }
            state.entry = index;
            state.arrived = false;
            state.idle = 0.0f;
            if (place) planTo(CellOf(place->first, place->second));
        }
        if (!place) continue;

        // Walk the path, waypoint by waypoint (cell centers).
        if (state.next < state.path.size()) {
            auto [gx, gy] = state.path[state.next];
            float tx = gx + 0.5f, ty = gy + 0.5f;
            float dx = tx - world.x, dy = ty - world.y;
            float distance = std::sqrt(dx * dx + dy * dy);
            if (distance < 0.12f) {
                state.next++;
            } else {
                float step = std::min(routine.speed * dt, distance);
                Vec2 half{0.3f, 0.3f};
                if (const auto* collider = registry.try_get<Collider2D>(entity)) half = {collider->halfWidth, collider->halfHeight};
                Vec2 moved = ResolveAabbMovement({world.x, world.y}, half, {dx / distance * step, dy / distance * step}, isSolid);
                float progress = std::hypot(moved.x - world.x, moved.y - world.y);
                world.x = moved.x;
                world.y = moved.y;
                registry.emplace_or_replace<Facing>(entity, dx / distance, dy / distance);
                state.stuck = progress < step * 0.25f ? state.stuck + dt : 0.0f;
                if (state.stuck > 1.0f) planTo(state.path.back()); // blocked: plan again from here
            }
            continue;
        }

        // There (or as close as the path goes).
        if (!state.arrived) {
            state.arrived = true;
            if (entry.activity == "sleep" && !registry.all_of<Sleeping>(entity)) {
                float alpha = 1.0f;
                if (auto* sprite = registry.try_get<Sprite>(entity)) {
                    alpha = sprite->a;
                    sprite->a = 0.0f; // indoors
                }
                registry.emplace<Sleeping>(entity, alpha);
            }
        }
        if (entry.activity == "wander") {
            state.idle -= dt;
            if (state.idle <= 0.0f) {
                state.idle = 2.0f + static_cast<float>(NextRandom(rng) % 300) / 100.0f; // 2-5 s between strolls
                int r = std::max(1, static_cast<int>(entry.radius));
                auto [cx, cy] = CellOf(place->first, place->second);
                for (int attempt = 0; attempt < 8; ++attempt) {
                    int gx = cx + static_cast<int>(NextRandom(rng) % (2 * r + 1)) - r;
                    int gy = cy + static_cast<int>(NextRandom(rng) % (2 * r + 1)) - r;
                    if ((gx - cx) * (gx - cx) + (gy - cy) * (gy - cy) <= r * r && !isSolid(gx, gy)) {
                        planTo({gx, gy});
                        break;
                    }
                }
            }
        }
    }
}

} // namespace aige
