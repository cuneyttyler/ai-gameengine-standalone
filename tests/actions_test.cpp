#include <gtest/gtest.h>

#include "ecs/Registry.h"
#include "gameplay/Archetypes.h"
#include "gameplay/Components.h"
#include "gameplay/GameSession.h"
#include "gameplay/InputMap.h"
#include "gameplay/Items.h"
#include "physics2d/Components.h"
#include "scripting/LuaVM.h"

#include <filesystem>

// docs/systems/actions.md: Actions used by anyone, what they need, take and give, once and cooldowns; custom input
// actions' plays and event.
namespace {

struct ActionsWorld {
    ActionsWorld() {
        root = std::filesystem::temp_directory_path() /
               ("aige_actions_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        for (int gx = 0; gx < 8; ++gx) {
            for (int gy = 0; gy < 8; ++gy) aige::CreateTile(registry, gx, gy, aige::TileKind::Grass);
        }
        player = aige::CreatePlayer(registry, 3.5f, 3.5f);
        field = registry.create();
        registry.emplace<aige::WorldTransform2D>(field, 4.5f, 3.5f);
        auto& action = registry.emplace<aige::Action>(field);
        action.action = "farm";
        action.requiresItem = "seeds";
        action.takes = "seeds";
        action.gives = "wheat*3";
        action.emits = "farmed";
        action.once = true;
        session = std::make_unique<aige::GameSession>(registry, lua, aige::IsoParams{}, root);
    }
    ~ActionsWorld() {
        session.reset();
        std::filesystem::remove_all(root);
    }
    aige::LuaVM lua;
    aige::Registry registry;
    aige::Entity player{entt::null}, field{entt::null};
    std::unique_ptr<aige::GameSession> session;
    std::filesystem::path root;
};

} // namespace

TEST(ActionsTest, ActNeedsTakesAndGives) {
    ActionsWorld w;
    EXPECT_EQ(w.session->ActionTarget(w.player, "farm", 1.5f), w.field);
    EXPECT_FALSE(w.session->ActionTarget(w.player, "chop", 1.5f).has_value());
    // Without seeds: nothing.
    EXPECT_FALSE(w.session->Act(w.player, w.field));
    auto& pockets = w.registry.get_or_emplace<aige::Inventory>(w.player);
    aige::AddItem(pockets, "seeds", 1);
    EXPECT_TRUE(w.session->Act(w.player, w.field));
    EXPECT_EQ(aige::CountItem(pockets, "seeds"), 0);
    EXPECT_EQ(aige::CountItem(pockets, "wheat"), 3);
    // Once: used up, and no longer a target.
    aige::AddItem(pockets, "seeds", 1);
    EXPECT_FALSE(w.session->Act(w.player, w.field));
    EXPECT_FALSE(w.session->ActionTarget(w.player, "farm", 1.5f).has_value());
}

TEST(ActionsTest, AnyoneCanAct) {
    ActionsWorld w;
    // A character (not the player) with its own seeds, beside the field.
    auto farmer = w.registry.create();
    w.registry.emplace<aige::WorldTransform2D>(farmer, 5.5f, 3.5f);
    aige::AddItem(w.registry.emplace<aige::Inventory>(farmer), "seeds", 2);
    EXPECT_TRUE(w.session->Act(farmer, w.field));
    EXPECT_EQ(aige::CountItem(w.registry.get<aige::Inventory>(farmer), "wheat"), 3);
    // Out of range: no.
    auto distant = w.registry.create();
    w.registry.emplace<aige::WorldTransform2D>(distant, 0.5f, 0.5f);
    w.registry.get<aige::Action>(w.field).once = false;
    aige::AddItem(w.registry.emplace<aige::Inventory>(distant), "seeds", 1);
    EXPECT_FALSE(w.session->Act(distant, w.field));
}

TEST(ActionsTest, CooldownsAndTimers) {
    ActionsWorld w;
    auto& action = w.registry.get<aige::Action>(w.field);
    action.once = false;
    action.requiresItem.clear();
    action.takes.clear();
    action.cooldown = 1.0f;
    EXPECT_TRUE(w.session->Act(w.player, w.field));
    EXPECT_FALSE(w.session->Act(w.player, w.field)); // cooling down
    auto noKeys = [](aige::Key) { return false; };
    for (int i = 0; i < 70; ++i) w.session->Advance(aige::GameSession::kFixedDt, noKeys);
    EXPECT_TRUE(w.session->Act(w.player, w.field));
    // A timer acts by itself: a crop grows (gives nothing without a user; its event says so).
    auto crop = w.registry.create();
    w.registry.emplace<aige::WorldTransform2D>(crop, 6.5f, 6.5f);
    auto& grow = w.registry.emplace<aige::Action>(crop);
    grow.trigger = "timer";
    grow.seconds = 0.5f;
    grow.once = true;
    grow.setFlag = "crop_grown";
    for (int i = 0; i < 60; ++i) w.session->Advance(aige::GameSession::kFixedDt, noKeys);
    EXPECT_TRUE(w.registry.get<aige::ActionState>(crop).done);
}

TEST(ActionsTest, InputActionsKeepPlaysAndEvent) {
    aige::InputMap map = aige::InputMap::Defaults();
    aige::InputAction parry;
    parry.name = "parry";
    parry.plays = "parry";
    parry.event = "parried";
    parry.keys.push_back({aige::Key::MouseRight, 1.0f});
    map.actions.push_back(parry);
    std::vector<std::string> errors;
    const aige::InputMap back = aige::InputMap::FromJson(map.ToJson(), &errors);
    EXPECT_TRUE(errors.empty());
    const auto* found = back.Find("parry");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->plays, "parry");
    EXPECT_EQ(found->event, "parried");
    ASSERT_EQ(found->keys.size(), 1u);
    EXPECT_EQ(found->keys[0].key, aige::Key::MouseRight);
}
