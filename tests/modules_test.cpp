#include <gtest/gtest.h>

#include "assets/SceneIO.h"
#include "gameplay/EngineReflection.h"
#include "modules/Modules.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// development_plan.md 4.8: a project adds components and commands in Lua.
namespace {

const fs::path kDemo = fs::path(AIGE_SOURCE_DIR) / "projects" / "demo";

// Registers the demo's modules into the engine registry for one test, and
// takes them out again so other tests (the schema snapshot) see the engine alone.
struct DemoModules {
    DemoModules() { result = aige::LoadModules(aige::EngineReflection(), kDemo); }
    ~DemoModules() {
        for (const auto& module : result.modules) {
            for (const auto& name : module.components) aige::EngineReflection().RemoveComponentInfo(name);
            for (const auto& name : module.commands) aige::EngineReflection().RemoveCommandInfo(name);
        }
    }
    aige::ModuleLoadResult result;
};

void Write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << text;
}

} // namespace

TEST(ModulesTest, TheLanternModuleJoinsTheSchemaLikeABuiltIn) {
    DemoModules modules;
    ASSERT_TRUE(modules.result.errors.empty()) << modules.result.errors.front();
    ASSERT_EQ(modules.result.modules.size(), 1u);
    EXPECT_EQ(modules.result.modules[0].name, "lantern");

    const aige::ComponentInfo* lantern = aige::EngineReflection().FindComponent("Lantern");
    ASSERT_NE(lantern, nullptr);
    const aige::FieldInfo* radius = lantern->Field("radius");
    ASSERT_NE(radius, nullptr);
    EXPECT_EQ(radius->type, aige::FieldType::Float);
    EXPECT_EQ(radius->min, 0.5);
    EXPECT_EQ(radius->max, 20.0);
    EXPECT_EQ(lantern->Field("color")->options.size(), 3u);

    // What the AI is told.
    const auto schema = aige::EngineReflection().ExportSchema().dump();
    EXPECT_NE(schema.find("\"Lantern\""), std::string::npos);
    EXPECT_NE(schema.find("lantern.setAll"), std::string::npos);
}

TEST(ModulesTest, ModuleComponentsRoundTripThroughAScene) {
    DemoModules modules;
    aige::Registry registry;
    aige::Entity entity = registry.create();
    ASSERT_TRUE(aige::AddComponent(registry, entity, "Lantern"));
    const aige::ComponentInfo* lantern = aige::FindComponent("Lantern");
    EXPECT_FLOAT_EQ(aige::ToFloat(lantern->Field("radius")->get(registry, entity)), 3.0f); // the default
    lantern->Field("radius")->set(registry, entity, 7.5f);
    lantern->Field("color")->set(registry, entity, 2);
    lantern->Field("lit")->set(registry, entity, false);

    const std::string text = aige::SerializeScene(registry);
    EXPECT_NE(text.find("Lantern radius=7.5 lit=0 color=green fuel=100"), std::string::npos) << text;

    aige::Registry loaded;
    aige::DeserializeScene(loaded, text);
    std::vector<aige::Entity> all;
    for (auto e : loaded.view<entt::entity>()) all.push_back(e);
    ASSERT_EQ(all.size(), 1u);
    aige::Entity copy = all.front();
    ASSERT_TRUE(lantern->has(loaded, copy));
    EXPECT_FLOAT_EQ(aige::ToFloat(lantern->Field("radius")->get(loaded, copy)), 7.5f);
    EXPECT_EQ(aige::ToInt(lantern->Field("color")->get(loaded, copy)), 2);
    EXPECT_FALSE(aige::ToBool(lantern->Field("lit")->get(loaded, copy)));

    lantern->remove(loaded, copy);
    EXPECT_FALSE(lantern->has(loaded, copy));
}

TEST(ModulesTest, CommandsRunTheirLua) {
    DemoModules modules;
    aige::Registry registry;
    std::vector<aige::Entity> lanterns;
    for (int i = 0; i < 3; ++i) {
        lanterns.push_back(registry.create());
        aige::AddComponent(registry, lanterns.back(), "Lantern");
    }
    registry.create(); // no lantern

    aige::CommandContext context{registry};
    auto result = aige::EngineReflection().Run(context, "lantern.setAll", {false});
    ASSERT_TRUE(result.ok) << result.error;
    const aige::ComponentInfo* lantern = aige::FindComponent("Lantern");
    for (aige::Entity e : lanterns) EXPECT_FALSE(aige::ToBool(lantern->Field("lit")->get(registry, e)));

    result = aige::EngineReflection().Run(context, "lantern.scaleRadius", {10.0f});
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_FLOAT_EQ(aige::ToFloat(lantern->Field("radius")->get(registry, lanterns[0])), 20.0f); // clamped

    // Parameters are checked before the Lua runs; the Lua can refuse.
    EXPECT_FALSE(aige::EngineReflection().Run(context, "lantern.scaleRadius", {100.0f}).ok);
    aige::Registry empty;
    aige::CommandContext emptyContext{empty};
    result = aige::EngineReflection().Run(emptyContext, "lantern.scaleRadius", {2.0f});
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, "there are no lanterns in the scene");
}

TEST(ModulesTest, BrokenModulesReportWhyAndTheRestStillLoads) {
    const fs::path root = fs::temp_directory_path() / "aige_modules_broken";
    fs::remove_all(root);
    Write(root / "modules/a_bad_json/module.json", "{ nope");
    Write(root / "modules/b_bad_type/module.json",
          R"({"components": [{"name": "Weird", "fields": [{"name": "x", "type": "quaternion"}]}]})");
    Write(root / "modules/c_clash/module.json", R"({"components": [{"name": "Health", "fields": []}]})");
    Write(root / "modules/d_lua_error/module.json",
          R"({"commands": [{"name": "d.go", "function": "go"}]})");
    Write(root / "modules/d_lua_error/module.lua", "function go( -- syntax error");
    Write(root / "modules/e_good/module.json",
          R"({"components": [{"name": "Glow", "fields": [{"name": "level", "type": "int", "default": 2, "min": 0, "max": 5}]}],
              "commands": [{"name": "e.fail", "function": "fail"}, {"name": "e.missing", "function": "nope"}]})");
    Write(root / "modules/e_good/module.lua", "function fail(args) error('boom') end");

    aige::ReflectionRegistry reflection;
    aige::RegisterGameplayTypes(reflection);
    auto result = aige::LoadModules(reflection, root);
    EXPECT_EQ(result.errors.size(), 6u); // d: its Lua, then its command with nothing to run
    auto mentions = [&](const std::string& what) {
        for (const auto& e : result.errors) if (e.find(what) != std::string::npos) return true;
        return false;
    };
    EXPECT_TRUE(mentions("a_bad_json/module.json: not valid JSON"));
    EXPECT_TRUE(mentions("unknown type \"quaternion\""));
    EXPECT_TRUE(mentions("component Health already exists"));
    EXPECT_TRUE(mentions("d_lua_error"));
    EXPECT_TRUE(mentions("no function nope"));
    ASSERT_NE(reflection.FindComponent("Glow"), nullptr);
    EXPECT_EQ(reflection.FindComponent("Weird"), nullptr);

    aige::Registry registry;
    aige::CommandContext context{registry};
    auto failed = reflection.Run(context, "e.fail", {});
    EXPECT_FALSE(failed.ok);
    EXPECT_NE(failed.error.find("boom"), std::string::npos);
    fs::remove_all(root);
}
