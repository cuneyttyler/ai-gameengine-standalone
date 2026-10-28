#include <gtest/gtest.h>

#include "ai_bridge/Proposal.h"
#include "assets/Prefab.h"
#include "assets/Project.h"
#include "gameplay/Animation.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/Archetypes.h"
#include "gameplay/Combat.h"
#include "gameplay/Components.h"
#include "gameplay/Cutscene.h"
#include "gameplay/Effects.h"
#include "gameplay/GameSession.h"
#include "gameplay/Items.h"
#include "gameplay/Settings.h"
#include "physics2d/Components.h"
#include "records/Records.h"
#include "renderer2d/Components.h"
#include "scripting/LuaVM.h"

#include <filesystem>
#include <fstream>
#include <memory>

namespace fs = std::filesystem;

// development_plan.md Phase 18: clips and sheets, animators, tweens, particles,
// effects and juice, lights, day and night (effects-animation.md).
namespace {

// A 4x4 grid of 32x32 frames: row 0 idle, row 1 walk south, row 2 walk east, row 3 attack.
const char* kSheet = R"({"schema": "aige.anim/1", "texture": "assets/hero.png", "size": [128, 128],
    "grid": {"width": 32, "height": 32}, "fps": 10,
    "clips": {
        "idle": {"frames": [0, 1]},
        "walk": {"directions": {"s": [4, 5, 6, 7], "e": [8, 9, 10, 11], "w": "mirror:e"}},
        "attack": {"frames": [12, 13, 14], "loop": "once", "events": [{"frame": 1, "event": "hit"}]},
        "die": {"frames": [15], "loop": "hold"}}})";

struct FxProject {
    FxProject() {
        root = fs::temp_directory_path() /
               ("aige_fx_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root);
        fs::create_directories(root / "assets");
        std::ofstream(root / "assets" / "hero.anim.json") << kSheet;
    }
    ~FxProject() { fs::remove_all(root); }
    fs::path root;
};

} // namespace

TEST(FxTest, SheetsClipsAndDirections) {
    std::string error;
    auto sheet = aige::AnimSheetFromJson(nlohmann::json::parse(kSheet), "assets/hero.anim.json", &error);
    ASSERT_TRUE(sheet) << error;
    EXPECT_TRUE(aige::ValidateAnimSheet(*sheet).empty());
    const auto* walk = sheet->Clip("walk");
    ASSERT_NE(walk, nullptr);
    bool flip = false;
    const auto* east = walk->FramesFor("e", &flip);
    ASSERT_NE(east, nullptr);
    EXPECT_FALSE(flip);
    EXPECT_FLOAT_EQ((*east)[0].x, 0.0f);
    EXPECT_FLOAT_EQ((*east)[0].y, 64.0f);
    walk->FramesFor("w", &flip); // mirrored
    EXPECT_TRUE(flip);
    const auto* north = walk->FramesFor("n", &flip); // the nearest it has
    ASSERT_NE(north, nullptr);
    EXPECT_FLOAT_EQ(walk->Length("s"), 0.4f);
    EXPECT_EQ(aige::DirectionName(1, 0), "e");
    EXPECT_EQ(aige::DirectionName(0, -1), "n");
    EXPECT_EQ(aige::DirectionName(-1, 1), "sw");
    // Problems.
    auto bad = aige::AnimSheetFromJson(nlohmann::json::parse(R"({"texture": "", "clips": {"x": {"frames": [{"region": [500, 0, 32, 32]}], "loop": "forever"}}})"),
                                       "bad", &error);
    const auto problems = aige::ValidateAnimSheet(*bad);
    EXPECT_GE(problems.size(), 2u);
}

TEST(FxTest, AsepriteAndGridImport) {
    const auto aseprite = nlohmann::json::parse(R"({
        "frames": [
            {"frame": {"x": 0, "y": 0, "w": 16, "h": 16}, "duration": 100},
            {"frame": {"x": 16, "y": 0, "w": 16, "h": 16}, "duration": 150},
            {"frame": {"x": 32, "y": 0, "w": 16, "h": 16}, "duration": 100},
            {"frame": {"x": 48, "y": 0, "w": 16, "h": 16}, "duration": 100}],
        "meta": {"size": {"w": 64, "h": 16}, "frameTags": [
            {"name": "idle", "from": 0, "to": 1, "direction": "forward"},
            {"name": "walk_e", "from": 2, "to": 3, "direction": "pingpong"}]}})");
    const auto json = aige::AnimSheetFromAseprite(aseprite, "assets/slime.png");
    std::string error;
    auto sheet = aige::AnimSheetFromJson(json, "assets/slime.anim.json", &error);
    ASSERT_TRUE(sheet) << error;
    ASSERT_NE(sheet->Clip("idle"), nullptr);
    EXPECT_FLOAT_EQ(sheet->Clip("idle")->Length(""), 0.25f);
    ASSERT_NE(sheet->Clip("walk"), nullptr);
    EXPECT_EQ(sheet->Clip("walk")->loop, "pingpong");
    EXPECT_TRUE(sheet->Clip("walk")->directions.contains("e"));

    const auto grid = aige::AnimSheetFromGrid("assets/orc.png", 32, 48, 4, {"idle", "walk_s", "walk_e", "attack"}, 8);
    auto gridSheet = aige::AnimSheetFromJson(grid, "assets/orc.anim.json", &error);
    ASSERT_TRUE(gridSheet) << error;
    EXPECT_TRUE(aige::ValidateAnimSheet(*gridSheet).empty());
    EXPECT_EQ(gridSheet->Clip("walk")->directions.size(), 2u);
}

TEST(FxTest, ConditionsOfAnimators) {
    std::map<std::string, float> p = {{"speed", 0.5f}, {"dead", 0.0f}, {"attacking", 1.0f}};
    EXPECT_TRUE(aige::AnimConditionHolds("speed > 0.1", p));
    EXPECT_FALSE(aige::AnimConditionHolds("speed <= 0.1", p));
    EXPECT_TRUE(aige::AnimConditionHolds("attacking && !dead", p));
    EXPECT_FALSE(aige::AnimConditionHolds("dead", p));
    EXPECT_TRUE(aige::AnimConditionHolds("", p));
    EXPECT_NE(aige::BuiltInAnimator("humanoid"), nullptr);
    EXPECT_EQ(aige::BuiltInAnimatorIds().size(), 4u);
}

TEST(FxTest, AnimatorsDriveTheSprite) {
    FxProject p;
    aige::AnimLibrary library(p.root);
    library.Refresh();
    ASSERT_NE(library.Find("assets/hero.anim.json"), nullptr) << (library.Problems().empty() ? "" : library.Problems()[0]);
    const auto saved = aige::ActiveSettings();
    aige::ActiveSettings().grid.style = aige::GridStyle::TopDown; // facing = screen direction
    aige::Registry registry;
    auto hero = registry.create();
    registry.emplace<aige::Sprite>(hero);
    registry.emplace<aige::Velocity2D>(hero);
    registry.emplace<aige::Facing>(hero, 0.0f, 1.0f);
    registry.emplace<aige::Animator>(hero, "assets/hero.anim.json", "humanoid");
    aige::UpdateAnimators(registry, library, 0.01f);
    EXPECT_EQ(registry.get<aige::AnimatorState>(hero).state, "idle");
    EXPECT_EQ(registry.get<aige::Sprite>(hero).texture, "assets/hero.png");
    EXPECT_FLOAT_EQ(registry.get<aige::Sprite>(hero).u1, 0.25f);
    // Walking west: the east frames, flipped.
    registry.get<aige::Velocity2D>(hero) = {-2.0f, 0.0f};
    registry.get<aige::Facing>(hero) = {-1.0f, 0.0f};
    aige::UpdateAnimators(registry, library, 0.01f);
    const auto& state = registry.get<aige::AnimatorState>(hero);
    EXPECT_EQ(state.state, "walk");
    EXPECT_EQ(state.direction, "w");
    EXPECT_GT(registry.get<aige::Sprite>(hero).u0, registry.get<aige::Sprite>(hero).u1); // mirrored
    // An attack plays once, fires its frame event, and goes back.
    aige::TriggerAnimation(registry, hero, "attacking");
    registry.get<aige::Velocity2D>(hero) = {0.0f, 0.0f};
    std::vector<std::string> events;
    for (int i = 0; i < 40; ++i) {
        for (const auto& fired : aige::UpdateAnimators(registry, library, 0.02f)) events.push_back(fired.event);
        if (i == 0) EXPECT_EQ(registry.get<aige::AnimatorState>(hero).state, "attack");
    }
    EXPECT_EQ(events, std::vector<std::string>{"hit"});
    EXPECT_EQ(registry.get<aige::AnimatorState>(hero).state, "idle");
    // Dead wins over everything and holds.
    registry.emplace<aige::Dead>(hero);
    aige::UpdateAnimators(registry, library, 0.5f);
    EXPECT_EQ(registry.get<aige::AnimatorState>(hero).state, "die");
    aige::ActiveSettings() = saved;
}

TEST(FxTest, ParticlesSpawnMoveAndDie) {
    auto sparks = aige::FindParticles("sparks");
    ASSERT_TRUE(sparks);
    EXPECT_GE(aige::ParticlePresets().size(), 10u);
    aige::ParticleSystem system;
    aige::Registry registry;
    system.Emit(*sparks, 5.0f, 5.0f);
    EXPECT_EQ(system.Count(), static_cast<size_t>(sparks->burst));
    std::vector<aige::SpriteItem> sprites;
    system.AppendSprites(sprites, aige::IsoParams{});
    EXPECT_EQ(sprites.size(), system.Count());
    EXPECT_EQ(sprites[0].value.texture, "fx:dot");
    for (int i = 0; i < 60; ++i) system.Update(registry, 0.05f);
    EXPECT_EQ(system.Count(), 0u);
    // An emitter keeps going while it plays; the budget caps everything.
    auto torch = registry.create();
    registry.emplace<aige::WorldTransform2D>(torch, 2.5f, 2.5f);
    registry.emplace<aige::ParticleEmitter>(torch, "fire");
    for (int i = 0; i < 20; ++i) system.Update(registry, 0.05f);
    EXPECT_GT(system.Count(), 10u);
    system.budget = 5;
    system.Clear();
    system.Emit(*aige::FindParticles("explosion"), 0, 0);
    EXPECT_EQ(system.Count(), 5u);
}

TEST(FxTest, EffectsJuiceAndLights) {
    FxProject p;
    aige::Registry registry;
    for (int x = 0; x < 10; ++x) {
        for (int y = 0; y < 10; ++y) aige::CreateTile(registry, x, y, aige::TileKind::Grass);
    }
    auto hero = aige::CreatePlayer(registry, 1.5f, 1.5f);
    aige::LuaVM lua;
    aige::GameSession session(registry, lua, aige::IsoParams{}, p.root);
    ASSERT_TRUE(aige::FindEffect("hit-fire"));
    EXPECT_FALSE(session.PlayEffect("no-such-effect", 0, 0));
    ASSERT_TRUE(session.PlayEffect("hit-fire", 1.5f, 1.5f, hero));
    EXPECT_GT(session.Particles().Count(), 0u);
    EXPECT_GT(session.Trauma(), 0.0f);
    EXPECT_TRUE(session.Frozen()); // hit-stop
    EXPECT_TRUE(registry.all_of<aige::HitFlash>(hero));
    const auto shake = session.CameraShake();
    EXPECT_LT(std::abs(shake.first), 15.0f);
    // The world holds during the hit-stop, then the shake decays.
    auto step = [&](double s) { return session.Advance(s, [](aige::Key) { return false; }); };
    EXPECT_EQ(step(0.02), 0);
    for (int i = 0; i < 60; ++i) step(1.0 / 60.0);
    EXPECT_FALSE(session.Frozen());
    EXPECT_FLOAT_EQ(session.Trauma(), 0.0f);
    // Accessibility: less shake.
    aige::ActiveFxAccessibility().shakeScale = 0.0f;
    session.Shake(1.0f);
    EXPECT_FLOAT_EQ(session.Trauma(), 0.0f);
    aige::ActiveFxAccessibility() = {};

    // Lights and day and night.
    const auto saved = aige::ActiveSettings();
    aige::ActiveSettings().lighting.enabled = true;
    float noon[3], midnight[3], dark = 0;
    aige::AmbientAt(12 * 60, noon, &dark);
    EXPECT_FLOAT_EQ(dark, 0.0f);
    aige::AmbientAt(0, midnight, &dark);
    EXPECT_FLOAT_EQ(dark, 1.0f);
    EXPECT_LT(midnight[0], noon[0]);
    auto lamp = registry.create();
    registry.emplace<aige::WorldTransform2D>(lamp, 3.5f, 3.5f);
    registry.emplace<aige::Light2D>(lamp).nightOnly = true;
    session.SetClockMinutes(12 * 60);
    size_t dayLights = session.Lighting().lights.size(); // the fire's short light, no lamp
    session.SetClockMinutes(23 * 60);
    const auto night = session.Lighting();
    EXPECT_TRUE(night.enabled);
    EXPECT_EQ(night.lights.size(), dayLights + 1);
    aige::ActiveSettings() = saved;
    EXPECT_FALSE(session.Lighting().enabled);
}

TEST(FxTest, TweensEaseAField) {
    FxProject p;
    aige::Registry registry;
    for (int x = 0; x < 4; ++x) aige::CreateTile(registry, x, 0, aige::TileKind::Grass);
    auto ghost = aige::CreateNpc(registry, 1.5f, 0.5f);
    aige::LuaVM lua;
    aige::GameSession session(registry, lua, aige::IsoParams{}, p.root);
    const int id = session.StartTween(ghost, "Sprite.a", 0.0f, 0.5f, "linear");
    ASSERT_GT(id, 0);
    EXPECT_FALSE(session.TweenDone(id));
    for (int i = 0; i < 20; ++i) session.Advance(1.0 / 60.0, [](aige::Key) { return false; });
    EXPECT_NEAR(registry.get<aige::Sprite>(ghost).a, 1.0f - 20.0f / 60.0f / 0.5f, 0.06f);
    for (int i = 0; i < 20; ++i) session.Advance(1.0 / 60.0, [](aige::Key) { return false; });
    EXPECT_TRUE(session.TweenDone(id));
    EXPECT_FLOAT_EQ(registry.get<aige::Sprite>(ghost).a, 0.0f);
    EXPECT_EQ(session.StartTween(ghost, "nonsense", 1, 1, "linear"), 0);
    EXPECT_FLOAT_EQ(aige::Ease("ease_in", 0.5f), 0.25f);
    EXPECT_FLOAT_EQ(aige::Ease("ease_out", 0.5f), 0.75f);
    EXPECT_FLOAT_EQ(aige::Ease("bounce", 1.0f), 1.0f);
}

TEST(FxTest, CutscenesTweenAnimateAndPlayEffects) {
    std::string error;
    auto cutscene = aige::CutsceneFromJson(nlohmann::json::parse(R"({"tracks": [
        {"type": "actor", "actor": "player", "keys": [{"at": 0, "op": "tween", "field": "Sprite.a", "to": 0, "seconds": 1},
                                                      {"at": 1, "op": "animate", "trigger": "casting"}]},
        {"type": "effect", "keys": [{"at": 0.5, "op": "play", "effect": "fireball", "x": 2.5, "y": 2.5}]},
        {"type": "camera", "keys": [{"at": 0.5, "op": "shake", "trauma": 0.5}]}]})"), "spell", &error);
    ASSERT_TRUE(cutscene) << error;
    EXPECT_TRUE(aige::ValidateCutscene(*cutscene).empty());
}

TEST(FxTest, AnimatedDeadStayForTheirDeath) {
    aige::Registry registry;
    for (int x = 0; x < 6; ++x) aige::CreateTile(registry, x, 0, aige::TileKind::Grass);
    auto hero = aige::CreatePlayer(registry, 0.5f, 0.5f);
    auto foe = aige::CreateEnemy(registry, 3.5f, 0.5f, hero, "");
    registry.emplace<aige::Animator>(foe, "", "humanoid", 1.0f, 0.5f);
    registry.get<aige::Health>(foe).current = 0.0f;
    std::vector<aige::GameEvent> events;
    auto solid = [](int, int) { return false; };
    aige::UpdateCombat(registry, solid, 0.1f, events);
    ASSERT_TRUE(registry.valid(foe)); // still there, dead, for its death clip
    EXPECT_TRUE(registry.all_of<aige::Dead>(foe));
    for (int i = 0; i < 6; ++i) aige::UpdateCombat(registry, solid, 0.1f, events);
    EXPECT_FALSE(registry.valid(foe));
}

// The AI panel (18): a sheet proposed and written; a bad one refused.
TEST(FxTest, TheAiWritesSheets) {
    FxProject p;
    std::ofstream(p.root / "assets" / "hero.png") << "png"; // the picture only needs to exist
    aige::Registry registry;
    aige::Project project{p.root};
    aige::PrefabLibrary prefabs(project);
    nlohmann::json ops = nlohmann::json::array();
    ops.push_back({{"op", "write_anim"}, {"path", "assets/knight.anim.json"}, {"sheet", nlohmann::json::parse(kSheet)}});
    auto resolved = aige::ResolveProposal({{"summary", "x"}, {"operations", ops}}, registry, aige::EngineReflection(), prefabs,
                                          project);
    ASSERT_EQ(resolved.items.size(), 1u);
    EXPECT_TRUE(resolved.items[0].error.empty()) << resolved.items[0].error;
    auto outcome = aige::ApplyOperations(ops.get<std::vector<nlohmann::json>>(), registry, aige::EngineReflection(), prefabs,
                                         project);
    ASSERT_TRUE(outcome.errors.empty()) << outcome.errors[0];
    aige::AnimLibrary library(p.root);
    library.Refresh();
    EXPECT_NE(library.Find("assets/knight.anim.json"), nullptr);
    nlohmann::json bad = nlohmann::json::array();
    bad.push_back({{"op", "write_anim"}, {"path", "sprites/x.json"}, {"sheet", {{"texture", "assets/none.png"}, {"clips", nlohmann::json::object()}}}});
    resolved = aige::ResolveProposal({{"summary", "x"}, {"operations", bad}}, registry, aige::EngineReflection(), prefabs, project);
    EXPECT_FALSE(resolved.items[0].error.empty());
}

namespace {

// docs/assets/animations.md: attack variants, a clip playing on a state, points, a prop's open.
const char* kActionsSheet = R"({"schema": "aige.anim/1", "texture": "assets/hero.png", "size": [128, 128],
    "grid": {"width": 32, "height": 32}, "fps": 10,
    "points": {"head": [16, 4, 0, 1]},
    "clips": {
        "idle": {"frames": [0]},
        "attack": {"frames": [12, 13], "loop": "once"},
        "attack_sword": {"frames": [4, 5], "loop": "once", "points": {"hand": [[20, 16, -45, 1], [24, 18, 0, 0]]}},
        "punch": {"frames": [8, 9], "loop": "once", "plays": "attack"},
        "dance": {"frames": [1, 2, 3]},
        "open": {"frames": [6, 7], "loop": "once"}}})";

// A PNG's first 24 bytes: enough for its size.
std::string PngHeader(int w, int h) {
    std::string s = "\x89PNG\r\n\x1a\n";
    s += std::string("\0\0\0\x0dIHDR", 8);
    for (int v : {w, h}) {
        for (int shift : {24, 16, 8, 0}) s += static_cast<char>((v >> shift) & 0xff);
    }
    return s;
}

struct ActionsProject {
    ActionsProject() {
        root = fs::temp_directory_path() /
               ("aige_actions_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root);
        fs::create_directories(root / "assets");
        fs::create_directories(root / "records" / "item");
        std::ofstream(root / "assets" / "hero.anim.json") << kActionsSheet;
        std::ofstream(root / "assets" / "hero.png", std::ios::binary) << PngHeader(128, 128);
        std::ofstream(root / "assets" / "sword.png", std::ios::binary) << PngHeader(8, 32);
        std::ofstream(root / "records" / "item" / "iron-sword.json")
            << R"({"name": "Iron Sword", "kind": "weapon", "slot": "weapon", "sprite": "assets/sword.png", "grip": [0.5, 0.9],
                  "pointing": -90, "class": "sword"})";
        aige::ActiveRecords().Open(aige::EngineReflection(), root);
        library = std::make_unique<aige::AnimLibrary>(root);
        library->Refresh();
    }
    ~ActionsProject() {
        aige::ActiveRecords().Close();
        fs::remove_all(root);
    }
    fs::path root;
    std::unique_ptr<aige::AnimLibrary> library;
};

aige::Entity Hero(aige::Registry& registry, const std::string& pick = "weapon") {
    auto hero = registry.create();
    registry.emplace<aige::Sprite>(hero);
    registry.emplace<aige::Velocity2D>(hero);
    auto& animator = registry.emplace<aige::Animator>(hero, "assets/hero.anim.json", "humanoid");
    animator.pick = pick;
    return hero;
}

} // namespace

TEST(FxTest, SheetsCarryPlaysOnAndPoints) {
    std::string error;
    auto sheet = aige::AnimSheetFromJson(nlohmann::json::parse(kActionsSheet), "assets/hero.anim.json", &error);
    ASSERT_TRUE(sheet) << error;
    EXPECT_EQ(sheet->Clip("punch")->plays, std::vector<std::string>{"attack"});
    const auto* sword = sheet->Clip("attack_sword");
    aige::AnimPoint p;
    ASSERT_TRUE(sheet->PointOn(*sword, "", 1, false, 32, "hand", &p));
    EXPECT_FLOAT_EQ(p.x, 24.0f);
    EXPECT_FALSE(p.front);
    ASSERT_TRUE(sheet->PointOn(*sword, "", 0, true, 32, "hand", &p)); // mirrored
    EXPECT_FLOAT_EQ(p.x, 12.0f);
    EXPECT_FLOAT_EQ(p.angle, 225.0f);
    ASSERT_TRUE(sheet->PointOn(*sheet->Clip("idle"), "", 0, false, 32, "head", &p)); // the sheet's
    EXPECT_FLOAT_EQ(p.y, 4.0f);
}

TEST(FxTest, AttacksChooseTheirClip) {
    ActionsProject project;
    aige::Registry registry;
    // In order: attack, attack_sword, punch.
    auto hero = Hero(registry, "order");
    std::vector<std::string> clips;
    for (int round = 0; round < 3; ++round) {
        aige::TriggerAnimation(registry, hero, "attacking");
        aige::UpdateAnimators(registry, *project.library, 0.01f);
        clips.push_back(registry.get<aige::AnimatorState>(hero).clip);
        for (int i = 0; i < 30; ++i) aige::UpdateAnimators(registry, *project.library, 0.02f);
        EXPECT_EQ(registry.get<aige::AnimatorState>(hero).state, "idle");
    }
    EXPECT_EQ(clips, (std::vector<std::string>{"attack", "attack_sword", "punch"}));
    // A sword wielded: its class's clip.
    auto knight = Hero(registry);
    registry.emplace<aige::Equipment>(knight).weapon = "iron-sword";
    aige::TriggerAnimation(registry, knight, "attacking");
    aige::UpdateAnimators(registry, *project.library, 0.01f);
    EXPECT_EQ(registry.get<aige::AnimatorState>(knight).clip, "attack_sword");
    // Unarmed: the punch.
    auto brawler = Hero(registry);
    aige::TriggerAnimation(registry, brawler, "attacking");
    aige::UpdateAnimators(registry, *project.library, 0.01f);
    EXPECT_EQ(registry.get<aige::AnimatorState>(brawler).clip, "punch");
    // By hand: the Animator's clips.
    auto monk = Hero(registry);
    registry.get<aige::Animator>(monk).clips = "attack=attack";
    aige::TriggerAnimation(registry, monk, "attacking");
    aige::UpdateAnimators(registry, *project.library, 0.01f);
    EXPECT_EQ(registry.get<aige::AnimatorState>(monk).clip, "attack");
}

TEST(FxTest, ClipsPlayByName) {
    ActionsProject project;
    aige::Registry registry;
    auto hero = Hero(registry);
    aige::UpdateAnimators(registry, *project.library, 0.01f);
    // A clip outside the animator's states: once, then back to idle.
    aige::TriggerAnimation(registry, hero, "dance");
    aige::UpdateAnimators(registry, *project.library, 0.01f);
    EXPECT_EQ(registry.get<aige::AnimatorState>(hero).oneShot, "dance");
    EXPECT_NEAR(aige::AnimationLength(registry, *project.library, hero, "dance"), 0.3f, 1e-4f);
    for (int i = 0; i < 20; ++i) aige::UpdateAnimators(registry, *project.library, 0.02f);
    EXPECT_TRUE(registry.get<aige::AnimatorState>(hero).oneShot.empty());
    EXPECT_EQ(registry.get<aige::AnimatorState>(hero).state, "idle");
    // A variant by name, and a clip that plays on a state.
    aige::TriggerAnimation(registry, hero, "attack_sword");
    aige::UpdateAnimators(registry, *project.library, 0.01f);
    EXPECT_EQ(registry.get<aige::AnimatorState>(hero).state, "attack");
    EXPECT_EQ(registry.get<aige::AnimatorState>(hero).clip, "attack_sword");
    for (int i = 0; i < 20; ++i) aige::UpdateAnimators(registry, *project.library, 0.02f);
    aige::TriggerAnimation(registry, hero, "punch");
    aige::UpdateAnimators(registry, *project.library, 0.01f);
    EXPECT_EQ(registry.get<aige::AnimatorState>(hero).state, "attack");
    EXPECT_EQ(registry.get<aige::AnimatorState>(hero).clip, "punch");
    // A prop opens and stays open.
    auto chest = registry.create();
    registry.emplace<aige::Sprite>(chest);
    registry.emplace<aige::Animator>(chest, "assets/hero.anim.json", "prop");
    aige::UpdateAnimators(registry, *project.library, 0.01f);
    aige::TriggerAnimation(registry, chest, "open");
    for (int i = 0; i < 20; ++i) aige::UpdateAnimators(registry, *project.library, 0.02f);
    EXPECT_EQ(registry.get<aige::AnimatorState>(chest).state, "open");
}

TEST(FxTest, EquipmentShowsOnTheHand) {
    ActionsProject project;
    aige::Registry registry;
    auto knight = Hero(registry);
    registry.emplace<aige::ScreenTransform2D>(knight, 100.0f, 100.0f, 5.0f);
    registry.get<aige::Sprite>(knight).width = 64.0f; // twice its 32 px frames
    registry.get<aige::Sprite>(knight).height = 64.0f;
    registry.emplace<aige::Equipment>(knight).weapon = "iron-sword";
    aige::TriggerAnimation(registry, knight, "attack_sword");
    aige::UpdateAnimators(registry, *project.library, 0.01f);
    // A transient sword: not in the scene's order, sized at the knight's scale.
    aige::Entity sword{entt::null};
    for (auto [e, link] : registry.view<aige::AttachmentLink>().each()) sword = e;
    ASSERT_TRUE(registry.valid(sword));
    EXPECT_TRUE(registry.all_of<aige::Transient>(sword));
    EXPECT_EQ(aige::EntitiesInOrder(registry).size(), 1u);
    EXPECT_FLOAT_EQ(registry.get<aige::Sprite>(sword).height, 64.0f);
    // On the hand (20, 16) of the frame: the knight's top-left (68, 68) + 2x = (108, 100); the grip there, its art
    // turned from pointing up to the hand's -45.
    aige::PlaceAttachments(registry);
    const auto& at = registry.get<aige::ScreenTransform2D>(sword);
    EXPECT_FLOAT_EQ(registry.get<aige::Sprite>(sword).angle, 45.0f);
    const float a = 45.0f * 3.14159265f / 180.0f, dy = 0.4f * 64.0f;
    EXPECT_NEAR(at.x, 108.0f + dy * std::sin(a), 0.01f);
    EXPECT_NEAR(at.y, 100.0f - dy * std::cos(a), 0.01f);
    EXPECT_GT(at.depth, 5.0f); // in front
    // Unequipped: gone.
    registry.get<aige::Equipment>(knight).weapon.clear();
    aige::UpdateAnimators(registry, *project.library, 0.01f);
    EXPECT_FALSE(registry.valid(sword));
    // An Attachment of the scene's, by Name, on the bottom middle.
    registry.emplace<aige::Name>(knight, "knight");
    auto hat = registry.create();
    registry.emplace<aige::Sprite>(hat).texture = "assets/sword.png";
    registry.emplace<aige::ScreenTransform2D>(hat);
    registry.emplace<aige::Attachment>(hat).to = "knight";
    aige::UpdateAnimators(registry, *project.library, 0.01f);
    aige::PlaceAttachments(registry);
    EXPECT_EQ(registry.get<aige::AttachmentLink>(hat).parent, knight);
    EXPECT_NEAR(registry.get<aige::ScreenTransform2D>(hat).y, 132.0f, 0.01f);
}
