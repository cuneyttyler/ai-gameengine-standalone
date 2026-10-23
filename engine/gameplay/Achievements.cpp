#include "gameplay/Achievements.h"

#include "records/Records.h"

#include <algorithm>
#include <fstream>

namespace aige {

namespace fs = std::filesystem;
using json = nlohmann::json;

AchievementDef AchievementFromJson(const std::string& id, const json& j) {
    AchievementDef d;
    d.id = id;
    d.name = j.value("name", id);
    d.description = j.value("description", "");
    d.icon = j.value("icon", "");
    d.hidden = j.value("hidden", false);
    d.points = j.value("points", 10);
    d.kind = j.value("kind", "event");
    d.quest = j.value("quest", "");
    d.state = j.value("state", "completed");
    d.flag = j.value("flag", "");
    d.event = j.value("event", "");
    d.target = j.value("target", "");
    d.count = std::max(1, j.value("count", 1));
    return d;
}

std::vector<std::string> ValidateAchievement(const AchievementDef& d) {
    std::vector<std::string> out;
    if (d.name.empty()) out.push_back("an achievement needs a name");
    if (d.kind == "quest" && d.quest.empty()) out.push_back("a quest achievement names its quest");
    if (d.kind == "flag" && d.flag.empty()) out.push_back("a flag achievement names its flag");
    if (d.kind == "event" && d.event.empty()) out.push_back("an event achievement names its event (death, pickup, trigger_enter...)");
    if (d.kind != "quest" && d.kind != "flag" && d.kind != "event" && d.kind != "script") out.push_back("kind is quest, flag, event or script");
    return out;
}

std::vector<AchievementDef> LoadAchievements() {
    std::vector<AchievementDef> out;
    for (const Record* r : ActiveRecords().OfType("achievement")) out.push_back(AchievementFromJson(r->id, r->resolved));
    std::sort(out.begin(), out.end(), [](const AchievementDef& a, const AchievementDef& b) { return a.id < b.id; });
    return out;
}

// --- The local platform ---------------------------------------------------------------------

LocalAchievements::LocalAchievements(fs::path file) : file_(std::move(file)) {
    std::ifstream in(file_, std::ios::binary);
    data_ = in ? json::parse(in, nullptr, false) : json::object();
    if (!data_.is_object()) data_ = json::object();
    if (!data_.contains("unlocked")) data_["unlocked"] = json::object();
    if (!data_.contains("counters")) data_["counters"] = json::object();
}

bool LocalAchievements::Unlocked(const std::string& id) const { return data_["unlocked"].contains(id); }

void LocalAchievements::Unlock(const std::string& id) {
    if (Unlocked(id)) return;
    data_["unlocked"][id] = true;
    Save();
}

int LocalAchievements::Counter(const std::string& id) const { return data_["counters"].value(id, 0); }

void LocalAchievements::SetCounter(const std::string& id, int value) {
    data_["counters"][id] = value;
    Save();
}

void LocalAchievements::Reset() {
    data_ = {{"unlocked", json::object()}, {"counters", json::object()}};
    Save();
}

void LocalAchievements::Save() const {
    if (file_.empty()) return;
    std::error_code ec;
    fs::create_directories(file_.parent_path(), ec);
    std::ofstream(file_, std::ios::binary) << data_.dump(2);
}

// --- The system -----------------------------------------------------------------------------

AchievementSystem::AchievementSystem() : platform_(std::make_shared<LocalAchievements>(fs::path())) {}

const AchievementDef* AchievementSystem::Find(const std::string& id) const {
    for (const auto& d : defs_) {
        if (d.id == id) return &d;
    }
    return nullptr;
}

bool AchievementSystem::Unlock(const std::string& id) {
    const AchievementDef* d = Find(id);
    if (!d || !platform_ || platform_->Unlocked(id)) return false;
    platform_->Unlock(id);
    fresh_.push_back(*d);
    return true;
}

bool AchievementSystem::Unlocked(const std::string& id) const { return platform_ && platform_->Unlocked(id); }

int AchievementSystem::Progress(const std::string& id) const { return platform_ ? platform_->Counter(id) : 0; }

void AchievementSystem::OnEvent(const std::string& event, const std::vector<std::string>& about) {
    if (!platform_) return;
    for (const auto& d : defs_) {
        if (d.kind != "event" || d.event != event || platform_->Unlocked(d.id)) continue;
        const bool matches = d.target.empty() || std::find(about.begin(), about.end(), d.target) != about.end();
        if (!matches) continue;
        const int n = platform_->Counter(d.id) + 1;
        platform_->SetCounter(d.id, n);
        if (n >= d.count) Unlock(d.id);
    }
}

void AchievementSystem::Check(const std::function<std::string(const std::string&)>& questState, const std::function<bool(const std::string&)>& flag) {
    if (!platform_) return;
    for (const auto& d : defs_) {
        if (platform_->Unlocked(d.id)) continue;
        if (d.kind == "quest" && questState && questState(d.quest) == d.state) Unlock(d.id);
        if (d.kind == "flag" && flag && flag(d.flag)) Unlock(d.id);
    }
}

std::vector<AchievementDef> AchievementSystem::TakeUnlocked() {
    auto out = std::move(fresh_);
    fresh_.clear();
    return out;
}

json AchievementSystem::View() const {
    json list = json::array();
    int points = 0, earned = 0;
    for (const auto& d : defs_) {
        const bool unlocked = Unlocked(d.id);
        points += d.points;
        if (unlocked) earned += d.points;
        const bool secret = d.hidden && !unlocked;
        list.push_back({{"id", d.id},
                        {"name", secret ? "???" : d.name},
                        {"description", secret ? "A secret." : d.description},
                        {"icon", d.icon},
                        {"unlocked", unlocked},
                        {"progress", Progress(d.id)},
                        {"count", d.kind == "event" ? d.count : 1},
                        {"points", d.points}});
    }
    return {{"achievements", list}, {"points", points}, {"earned", earned}, {"platform", platform_ ? platform_->Name() : ""}};
}

} // namespace aige
