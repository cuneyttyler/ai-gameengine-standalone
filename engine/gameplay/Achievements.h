#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace aige {

// Achievements (development_plan.md 24): records "achievement" (name, description, icon,
// hidden, points, and what unlocks it: a quest reaching a state, a story flag, a number of
// game events — deaths of a prefab, pickups, trigger entries — or a script's
// unlock_achievement). What's unlocked is kept by a platform layer: local (a file in the
// player's save folder) here; a store's (Steam...) plugs in behind the same interface.

struct AchievementDef {
    std::string id, name, description, icon;
    bool hidden = false;
    int points = 10;
    std::string kind;   // quest | flag | event | script
    std::string quest, state = "completed"; // quest
    std::string flag;                       // flag
    std::string event, target;              // event: its name (death, pickup, trigger_enter...) and what it's about
    int count = 1;
};

// From the records (and a project's records/achievement/), in id order.
std::vector<AchievementDef> LoadAchievements();
AchievementDef AchievementFromJson(const std::string& id, const nlohmann::json& json);
std::vector<std::string> ValidateAchievement(const AchievementDef& def);

// Where unlocks and counters live.
class AchievementPlatform {
public:
    virtual ~AchievementPlatform() = default;
    virtual std::string Name() const = 0;
    virtual bool Unlocked(const std::string& id) const = 0;
    virtual void Unlock(const std::string& id) = 0;
    virtual int Counter(const std::string& id) const = 0;
    virtual void SetCounter(const std::string& id, int value) = 0;
    virtual void Reset() = 0;
};

// The local platform: <save folder>/achievements.json.
class LocalAchievements : public AchievementPlatform {
public:
    explicit LocalAchievements(std::filesystem::path file);
    std::string Name() const override { return "local"; }
    bool Unlocked(const std::string& id) const override;
    void Unlock(const std::string& id) override;
    int Counter(const std::string& id) const override;
    void SetCounter(const std::string& id, int value) override;
    void Reset() override;
    nlohmann::json ToJson() const { return data_; }

private:
    std::filesystem::path file_;
    nlohmann::json data_;
    void Save() const;
};

// The game's achievements as they're played: events counted, conditions checked.
class AchievementSystem {
public:
    AchievementSystem();
    void SetPlatform(std::shared_ptr<AchievementPlatform> platform) { platform_ = std::move(platform); }
    AchievementPlatform* Platform() const { return platform_.get(); }
    void Reload() { defs_ = LoadAchievements(); }
    void SetDefs(std::vector<AchievementDef> defs) { defs_ = std::move(defs); }
    const std::vector<AchievementDef>& Defs() const { return defs_; }

    // A game event: counts toward "event" achievements. `about` holds what the event names
    // (the dead's prefab, the pickup's prefab, the trigger's name...).
    void OnEvent(const std::string& event, const std::vector<std::string>& about);
    // Quest and flag achievements: asked of the story.
    void Check(const std::function<std::string(const std::string&)>& questState, const std::function<bool(const std::string&)>& flag);
    // A script's or the console's.
    bool Unlock(const std::string& id);
    bool Unlocked(const std::string& id) const;
    int Progress(const std::string& id) const;
    // Newly unlocked since the last call: for notifications.
    std::vector<AchievementDef> TakeUnlocked();
    // For the Achievements window: each with unlocked, progress, hidden shown as "???".
    nlohmann::json View() const;

private:
    std::vector<AchievementDef> defs_;
    std::shared_ptr<AchievementPlatform> platform_;
    std::vector<AchievementDef> fresh_;
    const AchievementDef* Find(const std::string& id) const;
};

} // namespace aige
