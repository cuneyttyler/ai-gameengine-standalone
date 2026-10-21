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
            for (const auto& name : module.records) aige::EngineReflection().RemoveRecordTypeInfo(name);
        }
    }
    aige::ModuleLoadResult result;
};

void Write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << text;
}

} // namespace

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
