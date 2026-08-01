#include <gtest/gtest.h>

#include "ecs/Registry.h"
#include "physics2d/Components.h"
#include "scripting/Components.h"
#include "scripting/LuaVM.h"
#include "scripting/ScriptSystem.h"

#include <filesystem>
#include <fstream>

namespace {

void WriteScript(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream file(path, std::ios::trunc);
    file << contents;
}

aige::ScriptBinder MakeBinder(aige::Registry& registry) {
    return [&registry](sol::environment& env, aige::Entity entity) {
        env["get_position"] = [&registry, entity]() {
            auto& world = registry.get<aige::WorldTransform2D>(entity);
            return std::make_tuple(world.x, world.y);
        };
        env["set_position"] = [&registry, entity](float x, float y) {
            auto& world = registry.get<aige::WorldTransform2D>(entity);
            world.x = x;
            world.y = y;
        };
    };
}

} // namespace

TEST(ScriptingTest, ScriptMovesEntity) {
    auto scriptPath = std::filesystem::temp_directory_path() / "aige_test_script_move.lua";
    WriteScript(scriptPath, "function update(dt) local x, y = get_position(); set_position(x + 1.0, y) end");

    // vm must outlive registry — see the same note in apps/sandbox/main.cpp.
    // LuaScript's sol::environment holds references into vm's Lua state, so
    // registry (and its components) must be destroyed first.
    aige::LuaVM vm;
    aige::Registry registry;
    auto entity = registry.create();
    registry.emplace<aige::WorldTransform2D>(entity, 0.0f, 0.0f);
    registry.emplace<aige::LuaScript>(entity).path = scriptPath.string();

    auto binder = MakeBinder(registry);

    aige::UpdateScripts(registry, vm, 1.0f / 60.0f, binder);
    aige::UpdateScripts(registry, vm, 1.0f / 60.0f, binder);

    const auto& world = registry.get<aige::WorldTransform2D>(entity);
    EXPECT_FLOAT_EQ(world.x, 2.0f);
    EXPECT_FLOAT_EQ(world.y, 0.0f);

    std::filesystem::remove(scriptPath);
}

TEST(ScriptingTest, HotReloadPicksUpChangedFile) {
    auto scriptPath = std::filesystem::temp_directory_path() / "aige_test_script_reload.lua";
    WriteScript(scriptPath, "function update(dt) local x, y = get_position(); set_position(x + 1.0, y) end");

    aige::LuaVM vm; // must outlive registry — see ScriptMovesEntity above.
    aige::Registry registry;
    auto entity = registry.create();
    registry.emplace<aige::WorldTransform2D>(entity, 0.0f, 0.0f);
    registry.emplace<aige::LuaScript>(entity).path = scriptPath.string();

    auto binder = MakeBinder(registry);

    aige::UpdateScripts(registry, vm, 1.0f / 60.0f, binder);
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(entity).x, 1.0f);

    // Rewrite with different behavior and force a distinct mtime so the
    // change is guaranteed to be detected regardless of filesystem
    // timestamp resolution.
    WriteScript(scriptPath, "function update(dt) local x, y = get_position(); set_position(x + 100.0, y) end");
    auto bumped = std::filesystem::last_write_time(scriptPath) + std::chrono::seconds(2);
    std::filesystem::last_write_time(scriptPath, bumped);

    aige::UpdateScripts(registry, vm, 1.0f / 60.0f, binder);
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(entity).x, 101.0f);

    std::filesystem::remove(scriptPath);
}
