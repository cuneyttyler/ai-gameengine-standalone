#include <gtest/gtest.h>

#include "gameplay/Archetypes.h"
#include "gameplay/Components.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/GameSession.h"
#include "modules/Modules.h"
#include "scripting/Components.h"
#include "scripting/LuaVM.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// development_plan.md 12.2: events, timers, coroutines and per-frame module systems.
namespace {

void Write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

class EventsTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("aige_events_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root_);
        fs::create_directories(root_);
    }
    void TearDown() override { fs::remove_all(root_); }

    // An entity running `source`, reporting through its Name.
    aige::Entity Scripted(const std::string& source) {
        Write(root_ / "scripts/test.lua", source);
        auto entity = registry_.create();
        registry_.emplace<aige::Name>(entity);
        registry_.emplace<aige::LuaScript>(entity).path = "scripts/test.lua";
        return entity;
    }
    void Run(aige::GameSession& session, double seconds) {
        for (double t = 0; t < seconds; t += aige::GameSession::kFixedDt) {
            session.Advance(aige::GameSession::kFixedDt, [](aige::Key) { return false; });
        }
    }
    std::string NameOf(aige::Entity e) { return registry_.get<aige::Name>(e).value; }

    fs::path root_;
    aige::LuaVM lua_;
    aige::Registry registry_;
};

} // namespace

TEST_F(EventsTest, HandlersReceiveCustomEventsWithTheirData) {
    auto entity = Scripted(R"(
        on("door_opened", function(e)
            set_field(self_id(), "Name", "value", e.name .. ":" .. e.text .. ":" .. e.amount)
        end)
        emit("door_opened", {text = "north", amount = 2})
    )");
    aige::GameSession session(registry_, lua_, aige::IsoParams{}, root_);
    Run(session, aige::GameSession::kFixedDt);
    EXPECT_TRUE(session.ScriptErrors().empty()) << session.ScriptErrors()[0].message;
    EXPECT_EQ(NameOf(entity), "door_opened:north:2.0");
    ASSERT_EQ(session.DeliveredEvents().size(), 1u);
    EXPECT_EQ(session.DeliveredEvents()[0].name, "door_opened");
}

TEST_F(EventsTest, CombatEventsReachScripts) {
    auto watcher = Scripted(R"(
        hits = 0
        on("hit", function(e) hits = hits + 1; set_field(self_id(), "Name", "value", "hits " .. hits) end)
        on("death", function(e) set_field(self_id(), "Name", "value", "died") end)
    )");
    auto player = aige::CreatePlayer(registry_, 2.5f, 2.5f);
    auto enemy = aige::CreateEnemy(registry_, 3.2f, 2.5f, player, "");
    registry_.get<aige::Health>(enemy).current = 1.0f;
    registry_.emplace_or_replace<aige::Facing>(player, 1.0f, 0.0f);
    aige::GameSession session(registry_, lua_, aige::IsoParams{}, root_);
    bool space = true;
    session.Advance(aige::GameSession::kFixedDt, [&](aige::Key key) { return key == aige::Key::Space && space; });
    space = false;
    Run(session, 0.5);
    EXPECT_EQ(NameOf(watcher), "died");
}

TEST_F(EventsTest, TimersAndCoroutinesWait) {
    auto entity = Scripted(R"(
        log = ""
        local function note(s) log = log .. s; set_field(self_id(), "Name", "value", log) end
        after(0.5, function() note("a") end)
        local ticks = every(0.25, function() note("t") end)
        start(function()
            note("1")
            wait(0.6)
            note("2")
            cancel(ticks)
        end)
    )");
    aige::GameSession session(registry_, lua_, aige::IsoParams{}, root_);
    Run(session, 0.3);
    EXPECT_EQ(NameOf(entity), "1t");
    Run(session, 0.35); // past 0.5 and 0.6
    std::string log = NameOf(entity);
    EXPECT_EQ(log.size(), 5u) << log; // "1", ticks at 0.25 and 0.5, "a" at 0.5, then "2" at 0.6
    EXPECT_EQ(std::count(log.begin(), log.end(), 't'), 2);
    EXPECT_EQ(std::count(log.begin(), log.end(), 'a'), 1);
    EXPECT_EQ(log.back(), '2');
    Run(session, 1.0); // the ticker was cancelled
    EXPECT_EQ(NameOf(entity), log);
    EXPECT_TRUE(session.ScriptErrors().empty());
    EXPECT_EQ(session.TimerCount(), 0u);
    EXPECT_EQ(session.TaskCount(), 0u);
}

TEST_F(EventsTest, AHandlerCanWaitAndAGoneEntityStopsListening) {
    auto entity = Scripted(R"(
        on("ping", function(e)
            wait(0.2)
            set_field(self_id(), "Name", "value", "ponged")
        end)
    )");
    aige::GameSession session(registry_, lua_, aige::IsoParams{}, root_);
    Run(session, aige::GameSession::kFixedDt);
    session.Emit({"ping"});
    Run(session, 0.1);
    EXPECT_EQ(NameOf(entity), "");
    Run(session, 0.2);
    EXPECT_EQ(NameOf(entity), "ponged");
    EXPECT_EQ(session.HandlerCount(), 1u);
    registry_.destroy(entity);
    Run(session, aige::GameSession::kFixedDt);
    EXPECT_EQ(session.HandlerCount(), 0u);
}

TEST_F(EventsTest, AScriptErrorInAHandlerIsReportedNotFatal) {
    Scripted(R"(
        on("boom", function(e) error("kaboom") end)
        emit("boom")
    )");
    aige::GameSession session(registry_, lua_, aige::IsoParams{}, root_);
    Run(session, aige::GameSession::kFixedDt * 2);
    ASSERT_FALSE(session.ScriptErrors().empty());
    EXPECT_NE(session.ScriptErrors()[0].message.find("kaboom"), std::string::npos);
}

TEST_F(EventsTest, ModuleSystemsRunEveryStep) {
    Write(root_ / "modules/spin/module.json", R"({
        "components": [{"name": "Spinner", "fields": [{"name": "turns", "type": "float"}]}],
        "systems": [{"function": "spin"}, {"function": "missing"}]})");
    Write(root_ / "modules/spin/module.lua", R"(
        function spin(dt)
            for _, id in ipairs(entities_with("Spinner")) do
                set_field(id, "Spinner", "turns", get_field(id, "Spinner", "turns") + dt)
            end
        end)");
    auto result = aige::LoadModules(aige::EngineReflection(), root_);
    ASSERT_EQ(result.modules.size(), 1u);
    EXPECT_EQ(result.modules[0].systems, (std::vector<std::string>{"spin"}));
    EXPECT_EQ(result.errors.size(), 1u); // "missing"
    auto entity = registry_.create();
    ASSERT_TRUE(aige::AddComponent(registry_, entity, "Spinner"));
    aige::GameSession session(registry_, lua_, aige::IsoParams{}, root_);
    Run(session, 1.0);
    const auto* spinner = aige::FindComponent("Spinner");
    EXPECT_NEAR(aige::ToFloat(spinner->Field("turns")->get(registry_, entity)), 1.0f, 0.05f);
    // Leave the engine registry as other tests expect it.
    aige::EngineReflection().RemoveComponentInfo("Spinner");
    aige::LoadModules(aige::EngineReflection(), root_ / "none");
    EXPECT_EQ(aige::ModuleSystemCount(), 0u);
}
