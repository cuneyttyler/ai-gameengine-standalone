#include <gtest/gtest.h>

#include "gameplay/Archetypes.h"
#include "gameplay/Components.h"
#include "gameplay/GameSession.h"
#include "gameplay/InputMap.h"
#include "physics2d/Components.h"
#include "scripting/Components.h"
#include "scripting/LuaVM.h"

#include <filesystem>
#include <fstream>
#include <set>

namespace fs = std::filesystem;

// development_plan.md 12.3: input actions, bindings, contexts, gamepad axes.

TEST(InputKeysTest, NamesRoundTrip) {
    for (int i = 1; i < static_cast<int>(aige::Key::Count); ++i) {
        auto key = static_cast<aige::Key>(i);
        auto back = aige::KeyFromName(aige::KeyName(key));
        ASSERT_TRUE(back.has_value()) << aige::KeyName(key);
        EXPECT_EQ(*back, key);
    }
    EXPECT_FALSE(aige::KeyFromName("Banana").has_value());
    EXPECT_EQ(aige::PadAxisFromName("LeftTrigger"), aige::PadAxis::LeftTrigger);
    EXPECT_TRUE(aige::IsPadKey(aige::Key::PadStart));
    EXPECT_TRUE(aige::IsMouseKey(aige::Key::MouseRight));
}

TEST(InputMapTest, DefaultsCoverTheGameAndRoundTripThroughJson) {
    auto map = aige::InputMap::Defaults();
    for (const char* name : {"move_x", "move_y", "attack", "interact", "confirm", "choice_1", "menu", "quicksave"}) {
        EXPECT_NE(map.Find(name), nullptr) << name;
    }
    EXPECT_TRUE(map.Find("move_x")->axis);
    auto back = aige::InputMap::FromJson(map.ToJson());
    ASSERT_EQ(back.actions.size(), map.actions.size());
    EXPECT_EQ(back.Find("move_y")->keys.size(), map.Find("move_y")->keys.size());
    EXPECT_EQ(back.Find("move_y")->keys[0].scale, -1.0f);
    EXPECT_EQ(back.Find("attack")->keys[2].key, aige::Key::PadX); /* MouseLeft, Q, PadX */

    std::vector<std::string> errors;
    aige::InputMap::FromJson(nlohmann::json::parse(R"({"actions": [{"name": "x", "context": "space", "keys": ["Q", "Nope"]}]})"),
                             &errors);
    EXPECT_EQ(errors.size(), 2u);
}

TEST(InputMapTest, AProjectFileKeepsItsBindingsAndGainsNewDefaults) {
    const fs::path root = fs::temp_directory_path() / "aige_input_project";
    fs::remove_all(root);
    fs::create_directories(root);
    std::ofstream(root / "input.json")
        << R"({"actions": [{"name": "attack", "context": "gameplay", "keys": ["J"]}]})";
    auto map = aige::LoadInputMap(root);
    ASSERT_NE(map.Find("attack"), nullptr);
    ASSERT_EQ(map.Find("attack")->keys.size(), 1u);
    EXPECT_EQ(map.Find("attack")->keys[0].key, aige::Key::J);
    EXPECT_NE(map.Find("interact"), nullptr); // added from the defaults
    ASSERT_TRUE(aige::SaveInputMap(root, map));
    EXPECT_EQ(aige::LoadInputMap(root).Find("attack")->keys[0].key, aige::Key::J);
    fs::remove_all(root);
}

TEST(InputStateTest, PressesContextsAndAxes) {
    auto map = aige::InputMap::Defaults();
    std::set<aige::Key> held;
    auto keys = [&](aige::Key k) { return held.contains(k); };
    float stickX = 0.0f;
    auto axes = [&](aige::PadAxis a) { return a == aige::PadAxis::LeftX ? stickX : 0.0f; };
    aige::InputState input;

    held = {aige::Key::MouseLeft}; // attack's default (Space is interact's)
    input.Update(map, keys, axes);
    EXPECT_TRUE(input.Pressed("attack"));
    input.Update(map, keys, axes);
    EXPECT_TRUE(input.Down("attack"));
    EXPECT_FALSE(input.Pressed("attack")); // held, not pressed again
    held.clear();
    input.Update(map, keys, axes);
    EXPECT_TRUE(input.Released("attack"));

    // Axes: keys add up and clamp; a stick inside its deadzone counts as centred.
    held = {aige::Key::D, aige::Key::Right};
    input.Update(map, keys, axes);
    EXPECT_EQ(input.Value("move_x"), 1.0f);
    held.clear();
    stickX = 0.1f;
    input.Update(map, keys, axes);
    EXPECT_EQ(input.Value("move_x"), 0.0f);
    stickX = -0.6f;
    input.Update(map, keys, axes);
    EXPECT_FLOAT_EQ(input.Value("move_x"), -0.6f);
    stickX = 0.0f;

    // Contexts: dialogue actions don't answer in gameplay; switching context
    // doesn't turn a held key into a press.
    held = {aige::Key::Enter};
    input.Update(map, keys, axes);
    EXPECT_FALSE(input.Down("confirm"));
    input.SetContext("dialogue");
    input.Update(map, keys, axes);
    EXPECT_TRUE(input.Down("confirm"));
    EXPECT_FALSE(input.Pressed("confirm"));
    EXPECT_FALSE(input.Down("attack"));
    held = {aige::Key::Escape};
    input.Update(map, keys, axes);
    EXPECT_TRUE(input.Pressed("menu")); // "any" works everywhere
}

TEST(InputStateTest, TheGameReadsActionsRebindingAndTheStick) {
    aige::InputMap saved = aige::ActiveInputMap();
    aige::ActiveInputMap() = aige::InputMap::Defaults();
    aige::ActiveInputMap().Find("attack")->keys = {{aige::Key::J, 1.0f}};

    aige::LuaVM lua;
    aige::Registry registry;
    for (int x = 0; x < 12; ++x) {
        for (int y = 0; y < 12; ++y) aige::CreateTile(registry, x, y, aige::TileKind::Grass); // ground to walk on
    }
    auto player = aige::CreatePlayer(registry, 2.5f, 2.5f);
    registry.emplace<aige::MeleeAttack>(player, 3.0f, 0.7f, 0.5f, 0.35f, 0.5f); // characters come without an attack
    aige::GameSession session(registry, lua, aige::IsoParams{});
    float stickX = 1.0f;
    session.SetAxisQuery([&](aige::PadAxis a) { return a == aige::PadAxis::LeftX ? stickX : 0.0f; });
    const float startX = registry.get<aige::WorldTransform2D>(player).x;
    std::set<aige::Key> held;
    for (int i = 0; i < 30; ++i) {
        session.Advance(aige::GameSession::kFixedDt, [&](aige::Key k) { return held.contains(k); });
    }
    EXPECT_NE(registry.get<aige::WorldTransform2D>(player).x, startX); // the stick walks

    // Space no longer attacks; J does.
    bool attacked = false;
    held = {aige::Key::Space};
    session.Advance(aige::GameSession::kFixedDt, [&](aige::Key k) { return held.contains(k); });
    for (const auto& e : session.Events()) attacked |= e.type == aige::GameEvent::Type::Attack;
    EXPECT_FALSE(attacked);
    held = {aige::Key::J};
    for (int i = 0; i < 2 && !attacked; ++i) {
        session.Advance(aige::GameSession::kFixedDt, [&](aige::Key k) { return held.contains(k); });
        for (const auto& e : session.Events()) attacked |= e.type == aige::GameEvent::Type::Attack;
    }
    EXPECT_TRUE(attacked);
    aige::ActiveInputMap() = saved;
}
