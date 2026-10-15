#pragma once

#include "ecs/Registry.h"
#include "physics2d/Collision.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace aige {

// Daily routines (development_plan.md 3.5): schedules/<id>.json says where an
// NPC is and what it does through the day; an entity with a Routine follows
// one, walking (A*) between named Markers.
//
//   {"name": "Tamsin's day",
//    "entries": [{"from": "06:00", "activity": "wander", "place": "herb_garden", "radius": 2},
//                {"from": "12:00", "activity": "stay",   "place": "village_well"},
//                {"from": "21:00", "activity": "sleep",  "place": "tamsin_home"}]}
//
// Each entry lasts until the next one starts; the last runs past midnight
// into the first. Format documented in docs/story.md.

constexpr int kMinutesPerDay = 24 * 60;

// "06:30" -> 390; nullopt unless HH:MM with 0-23 / 0-59.
std::optional<int> ParseClockTime(const std::string& text);
std::string FormatClockTime(int minuteOfDay);

struct ScheduleEntry {
    int minute = 0;       // start, minutes after midnight
    std::string activity; // stay | wander | sleep
    std::string place;    // a Marker's name
    float radius = 2.0f;  // wander: how far from the place, in cells
};

struct Schedule {
    std::string id; // schedules/<id>.json
    std::string name;
    std::vector<ScheduleEntry> entries; // sorted by minute

    // The entry in effect at a time of day (the last one wraps past midnight).
    const ScheduleEntry* At(float minuteOfDay) const;
    size_t IndexAt(float minuteOfDay) const;
};

std::optional<Schedule> ScheduleFromJson(const nlohmann::json& json, const std::string& id, std::string* error);
nlohmann::json ScheduleToJson(const Schedule& schedule);
// Problems with a schedule: unknown activities, places that aren't markers
// (`markerExists`), two entries starting at the same time.
std::vector<std::string> ValidateSchedule(const Schedule& schedule,
                                          const std::function<bool(const std::string&)>& markerExists);
std::optional<std::string> WriteScheduleFile(const std::filesystem::path& projectRoot, const std::string& id,
                                             const nlohmann::json& content);

class ScheduleLibrary {
public:
    explicit ScheduleLibrary(std::filesystem::path projectRoot = {});
    void Refresh();
    const std::vector<Schedule>& Schedules() const { return schedules_; }
    // By id ("tamsin") or path ("schedules/tamsin.json").
    const Schedule* Find(const std::string& idOrPath) const;
    const std::vector<std::pair<std::string, std::string>>& LoadProblems() const { return loadProblems_; }

private:
    std::filesystem::path root_;
    std::vector<Schedule> schedules_;
    std::vector<std::pair<std::string, std::string>> loadProblems_; // file, message
};

// Where every Marker is: name -> world position.
std::vector<std::pair<std::string, std::pair<float, float>>> MarkerPositions(Registry& registry);

// Moves every Routine entity through its day: on a new entry it plans a path
// (A*) to the entry's place and walks it; `wander` then roams within the
// radius, `sleep` hides the entity (and makes it unavailable to talk to) until
// the next entry. `rng` is advanced for wandering.
void UpdateRoutines(Registry& registry, const ScheduleLibrary& schedules, float minuteOfDay, const SolidQueryFn& isSolid,
                    float dt, uint64_t& rng);

} // namespace aige
