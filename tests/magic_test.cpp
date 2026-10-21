#include <gtest/gtest.h>

#include "gameplay/Archetypes.h"
#include "gameplay/Attributes.h"
#include "gameplay/Combat.h"
#include "gameplay/Components.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/GameSession.h"
#include "gameplay/Items.h"
#include "gameplay/Magic.h"
#include "physics2d/Components.h"
#include "records/Records.h"
#include "scripting/LuaVM.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// development_plan.md Phase 19: magic effects, abilities, items and perks, cost and
// balance, and character AI casting by hints (magic.md).
namespace {

class MagicTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("aige_magic_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root_);
        Write("records/item/fire-tome.json", R"({"name": "Tome of Fire", "kind": "book", "use": {"learn": "firebolt"}})");
        Write("records/item/scroll-nova.json", R"({"name": "Scroll of Frost Nova", "kind": "consumable",
                                                    "use": {"ability": "frost-nova"}})");
        Write("records/item/regen-potion.json", R"({"name": "Potion of Regeneration", "kind": "consumable",
                                                     "use": {"effects": ["regen"]}})");
        Write("records/item/flame-sword.json", R"({"name": "Flame sword", "kind": "weapon", "slot": "weapon",
                                                   "enchantment": {"effects": ["fire-weapon"], "on": "hit"}})");
        Write("records/item/iron-ring.json", R"({"name": "Iron ring", "kind": "armor", "slot": "ring",
                                                 "enchantment": {"effects": ["armour-up"], "on": "worn"}})");
        Write("records/perk/pyromancer.json", R"({"name": "Pyromancer",
            "abilityModifiers": [{"tag": "element.fire", "stat": "magnitude", "value": 2.0},
                                 {"tag": "element.fire", "stat": "cost", "value": 0.5}]})");
        aige::ActiveRecords().Open(aige::EngineReflection(), root_);
        for (const auto& p : aige::ActiveRecords().Problems()) ADD_FAILURE() << p.message;
        for (int x = 0; x < 16; ++x) {
            for (int y = 0; y < 16; ++y) aige::CreateTile(registry_, x, y, aige::TileKind::Grass);
        }
    }
    void TearDown() override {
        aige::ActiveRecords().Close();
        fs::remove_all(root_);
    }
    void Write(const std::string& relative, const std::string& text) {
        fs::create_directories((root_ / relative).parent_path());
        std::ofstream(root_ / relative, std::ios::binary) << text;
    }
    aige::Entity Mage(float x, float y, float mana = 100.0f) {
        auto e = aige::CreatePlayer(registry_, x, y);
        auto& r = registry_.emplace_or_replace<aige::Resources>(e);
        r.mana = r.maxMana = mana;
        r.stamina = r.maxStamina = 100.0f;
        auto& a = registry_.emplace_or_replace<aige::Attributes>(e);
        a.maxMana = mana;
        a.maxStamina = 100.0f;
        a.maxHealth = registry_.get<aige::Health>(e).max;
        return e;
    }
    aige::Registry registry_;
    fs::path root_;
};

} // namespace

TEST_F(MagicTest, PresetsAndFormulas) {
    EXPECT_GE(aige::MagicEffectPresets().size(), 15u);
    EXPECT_GE(aige::AbilityPresets().size(), 8u);
    auto firebolt = aige::FindAbility("firebolt");
    ASSERT_TRUE(firebolt);
    EXPECT_EQ(firebolt->delivery, "projectile");
    EXPECT_TRUE(aige::ValidateAbility(*firebolt).empty());
    EXPECT_FLOAT_EQ(aige::EvaluateFormula("8 + 2 * caster.level", {{"caster.level", 3}}), 14.0f);
    EXPECT_FLOAT_EQ(aige::EvaluateFormula("(1 + 2) * 3 - 4 / 2", {}), 7.0f);
    EXPECT_FLOAT_EQ(aige::EvaluateFormula("unknown + 1", {}), 1.0f);
    // Cost and balance.
    const auto figures = aige::FiguresOf(*firebolt);
    EXPECT_GT(figures.formulaCost, 0.0f);
    EXPECT_GT(figures.output, 0.0f);
    EXPECT_GT(figures.outputPerCost, 0.0f);
    aige::AbilityDef cheap = *firebolt;
    cheap.cost = 0.5f;
    EXPECT_TRUE(aige::FiguresOf(cheap).outlier);
    // Validation.
    aige::AbilityDef bad;
    bad.delivery = "telepathy";
    bad.effects = {{"no-such-effect", std::nullopt, std::nullopt}};
    bad.ai = aige::AbilityDef::AiHint{"attack", "luck > 3", 0};
    const auto problems = aige::ValidateAbility(bad);
    EXPECT_GE(problems.size(), 3u);
}

TEST_F(MagicTest, SpellsFromABookAProjectileAHealAndAnAura) {
    auto hero = Mage(2.5f, 2.5f);
    auto& inventory = registry_.emplace<aige::Inventory>(hero);
    aige::AddItem(inventory, "fire-tome", 1);
    auto foe = aige::CreateEnemy(registry_, 6.5f, 2.5f, hero, "");
    registry_.get<aige::Health>(foe) = {100.0f, 100.0f};
    aige::LuaVM lua;
    aige::GameSession session(registry_, lua, aige::IsoParams{}, root_);
    aige::MagicSystem* magic = aige::MagicOf(registry_);
    ASSERT_NE(magic, nullptr);
    auto step = [&](int n = 1) {
        for (int i = 0; i < n; ++i) session.Advance(aige::GameSession::kFixedDt, [](aige::Key) { return false; });
    };
    // Unknown until the book teaches it.
    std::string why;
    EXPECT_FALSE(magic->Cast(hero, "firebolt", 6.5f, 2.5f, entt::null, &why));
    EXPECT_NE(why.find("doesn't know"), std::string::npos);
    EXPECT_EQ(aige::UseItem(registry_, hero, "fire-tome"), "");
    EXPECT_TRUE(magic->Knows(hero, "firebolt"));
    EXPECT_EQ(registry_.get<aige::Abilities>(hero).slots, "firebolt");
    // A fire projectile: mana spent, it flies and burns.
    EXPECT_TRUE(magic->Cast(hero, "firebolt", 6.5f, 2.5f, entt::null, &why)) << why;
    EXPECT_FLOAT_EQ(registry_.get<aige::Resources>(hero).mana, 88.0f);
    EXPECT_FALSE(magic->Cast(hero, "firebolt", 6.5f, 2.5f, entt::null, &why)); // cooldown
    step(60);
    EXPECT_LT(registry_.get<aige::Health>(foe).current, 100.0f);
    EXPECT_TRUE(magic->HasTag(foe, "state.burning") || registry_.get<aige::Health>(foe).current < 85.0f);
    // A heal (with its cast time).
    magic->Learn(hero, "heal");
    registry_.get<aige::Health>(hero).current = 1.0f;
    EXPECT_TRUE(magic->Cast(hero, "heal", 0, 0));
    step(60);
    EXPECT_GT(registry_.get<aige::Health>(hero).current, 5.0f);
    // An aura: those near take its effects while it lasts.
    magic->Learn(hero, "flame-aura");
    registry_.get<aige::WorldTransform2D>(foe) = {3.5f, 2.5f};
    EXPECT_TRUE(magic->Cast(hero, "flame-aura", 0, 0));
    step(150);
    EXPECT_TRUE(magic->HasTag(foe, "state.burning"));
    // Not enough mana.
    registry_.get<aige::Resources>(hero).mana = 1.0f;
    magic->Learn(hero, "stoneskin");
    EXPECT_FALSE(magic->Cast(hero, "stoneskin", 0, 0, entt::null, &why));
    EXPECT_NE(why.find("not enough mana"), std::string::npos);
}

TEST_F(MagicTest, EffectsStackStateAndExpire) {
    auto hero = Mage(2.5f, 2.5f);
    auto foe = aige::CreateEnemy(registry_, 4.5f, 2.5f, hero, "");
    registry_.get<aige::Health>(foe) = {50.0f, 50.0f};
    aige::MagicSystem magic(registry_);
    // Poison stacks to three and can't kill.
    for (int i = 0; i < 5; ++i) EXPECT_TRUE(magic.ApplyEffect(foe, "poison", hero));
    EXPECT_EQ(registry_.get<aige::MagicState>(foe).effects.size(), 1u);
    EXPECT_EQ(registry_.get<aige::MagicState>(foe).effects[0].stacks, 3);
    registry_.get<aige::Health>(foe).current = 2.0f;
    for (int i = 0; i < 70; ++i) magic.Update(0.1f);
    EXPECT_GT(registry_.get<aige::Health>(foe).current, 0.0f);
    EXPECT_FALSE(magic.HasTag(foe, "state.poisoned")); // expired
    // A stun stops moving and swinging, then wears off.
    EXPECT_TRUE(magic.ApplyEffect(foe, "stun", hero));
    EXPECT_TRUE(magic.HasTag(foe, "state.stunned"));
    std::string why;
    magic.Learn(foe, "power-strike");
    EXPECT_FALSE(magic.Cast(foe, "power-strike", 2.5f, 2.5f, entt::null, &why));
    for (int i = 0; i < 20; ++i) magic.Update(0.1f);
    EXPECT_FALSE(magic.HasTag(foe, "state.stunned"));
    // A buff modifies an attribute and comes off again.
    const float before = aige::EffectiveAttribute(registry_, hero, "attack");
    magic.ApplyEffect(hero, "strength-up", hero, 3.0f, 1.0f);
    EXPECT_FLOAT_EQ(aige::EffectiveAttribute(registry_, hero, "attack"), before + 3.0f);
    for (int i = 0; i < 15; ++i) magic.Update(0.1f);
    EXPECT_FLOAT_EQ(aige::EffectiveAttribute(registry_, hero, "attack"), before);
    // Dispel by tag.
    magic.ApplyEffect(foe, "poison", hero);
    magic.RemoveEffects(foe, "state.poisoned");
    EXPECT_FALSE(magic.HasTag(foe, "state.poisoned"));
}

TEST_F(MagicTest, ItemsAndPerks) {
    auto hero = Mage(2.5f, 2.5f);
    auto& inventory = registry_.emplace<aige::Inventory>(hero);
    aige::AddItem(inventory, "scroll-nova", 1);
    aige::AddItem(inventory, "regen-potion", 1);
    aige::AddItem(inventory, "flame-sword", 1);
    aige::AddItem(inventory, "iron-ring", 1);
    auto foe = aige::CreateEnemy(registry_, 3.5f, 2.5f, hero, "");
    registry_.get<aige::Health>(foe) = {100.0f, 100.0f};
    aige::MagicSystem magic(registry_);
    // A scroll casts what you don't know, once.
    EXPECT_EQ(aige::UseItem(registry_, hero, "scroll-nova"), "");
    EXPECT_LT(registry_.get<aige::Health>(foe).current, 100.0f);
    EXPECT_EQ(aige::CountItem(registry_.get<aige::Inventory>(hero), "scroll-nova"), 0);
    // A potion with effects.
    EXPECT_EQ(aige::UseItem(registry_, hero, "regen-potion"), "");
    EXPECT_TRUE(magic.HasTag(hero, "state.regenerating"));
    // Worn: the ring's armour while it's on.
    const float defense = aige::EffectiveAttribute(registry_, hero, "defense");
    EXPECT_EQ(aige::Equip(registry_, hero, "iron-ring"), "");
    magic.Update(0.1f);
    EXPECT_GT(aige::EffectiveAttribute(registry_, hero, "defense"), defense);
    EXPECT_EQ(aige::Unequip(registry_, hero, "ring"), "");
    magic.Update(0.1f);
    EXPECT_FLOAT_EQ(aige::EffectiveAttribute(registry_, hero, "defense"), defense);
    // On hit: the sword sets the target burning.
    EXPECT_EQ(aige::Equip(registry_, hero, "flame-sword"), "");
    magic.OnHit(hero, foe, "");
    EXPECT_TRUE(magic.HasTag(foe, "state.burning"));
    // A perk doubles fire's magnitude and halves its cost.
    magic.Learn(hero, "firebolt");
    registry_.emplace_or_replace<aige::Progression>(hero).perks = "pyromancer";
    const float mana = registry_.get<aige::Resources>(hero).mana;
    EXPECT_TRUE(magic.Cast(hero, "firebolt", 3.5f, 2.5f));
    EXPECT_FLOAT_EQ(registry_.get<aige::Resources>(hero).mana, mana - 6.0f);
}

TEST_F(MagicTest, AnEnemyMageHealsItselfWhenHurt) {
    auto hero = Mage(2.5f, 2.5f);
    auto mage = aige::CreateEnemy(registry_, 6.5f, 2.5f, hero, "");
    registry_.emplace<aige::Abilities>(mage, "firebolt, heal", "");
    auto& r = registry_.emplace_or_replace<aige::Resources>(mage);
    r.mana = r.maxMana = 100.0f;
    registry_.get<aige::Health>(mage) = {100.0f, 100.0f};
    aige::MagicSystem magic(registry_);
    // Healthy and in range: it attacks.
    EXPECT_TRUE(magic.ThinkFor(mage, hero));
    EXPECT_GT(magic.CooldownLeft(mage, "firebolt"), 0.0f);
    // Hurt: it heals itself (the higher priority).
    registry_.get<aige::Health>(mage).current = 20.0f;
    EXPECT_TRUE(magic.ThinkFor(mage, hero));
    EXPECT_EQ(registry_.get<aige::MagicState>(mage).casting, "heal");
    for (int i = 0; i < 10; ++i) magic.Update(0.1f);
    EXPECT_GT(registry_.get<aige::Health>(mage).current, 20.0f);
}
