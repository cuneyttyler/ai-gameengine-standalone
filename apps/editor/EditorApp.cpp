#include "game/Accessibility.h"
#include "game/Templates.h"
#include "gameplay/Behaviour.h"
#include "planner/GamePlan.h"
#include "story/StoryGraph.h"
#include "story/StoryRules.h"
#include "EditorApp.h"

#include "assets/TextureCook.h"

#include "Win32Util.h"

#include "ai_bridge/ContextBuilder.h"
#include "ai_bridge/DryRun.h"
#include "ai_bridge/Proposal.h"

#include "gameplay/EngineReflection.h"
#include "assets/SceneIO.h"
#include "assets/TileLayerIO.h"
#include "places/Planner.h"
#include "editing/BuildWindow.h"
#include "assets/ProjectTemplate.h"
#include "places/OntologyEdit.h"
#include "assets/UserLibrary.h"
#include "editing/Edits.h"
#include "editing/LevelBuild.h"
#include "editing/PlaceInspect.h"
#include "editing/PlaceSketch.h"
#include "editing/MatchedArt.h"
#include "editing/SceneTools.h"
#include "ai_bridge/RecentProjects.h"
#include "assets/Export.h"
#include "assets/PackImport.h"
#include "assets/TileEdges.h"
#include "assets/SceneFiles.h"
#include "assets/ProjectMove.h"
#include "rhi/ShaderUtil.h"
#include "modules/Modules.h"
#include "records/Records.h"
#include "game/SaveGames.h"
#include "game/GameAudio.h"
#include "gameplay/InputMap.h"
#include "world/StoryReferences.h"
#include "editing/SceneEditing.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/CameraFollow.h"
#include "gameplay/Components.h"
#include "gameplay/Attributes.h"
#include "gameplay/Combat.h"
#include "gameplay/Difficulty.h"
#include "gameplay/Encounters.h"
#include "gameplay/Settings.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "scripting/Components.h"

#include <algorithm>
#include <tuple>
#include <climits>
#include <variant>
#include <chrono>
#include <ctime>
#include <cwctype>
#include <cstdio>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <limits>

#include <shellapi.h>

namespace aige {


namespace {

// A step of the UI thread that took long (a message, a part of a frame): the editor froze for it. Written to
// editor-errors.log with its name and how long, so a freeze says what it was.
class SlowWatch {
public:
    explicit SlowWatch(std::string what) : what_(std::move(what)), start_(std::chrono::steady_clock::now()) {}
    ~SlowWatch() {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_).count();
        if (ms < 500) return;
        const std::filesystem::path prefs = UserPrefs::DefaultPath();
        if (prefs.empty()) return;
        std::ofstream log(prefs.parent_path() / "editor-errors.log", std::ios::app);
        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm local{};
        localtime_s(&local, &now);
        char stamp[32] = {};
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
        log << stamp << " [slow] " << what_ << " took " << ms << " ms\n";
    }

private:
    std::string what_;
    std::chrono::steady_clock::time_point start_;
};

// "Build the game", "build it all": the planned game, built (23).
bool LooksLikeBuildGameRequest(const std::string& text) {
    std::string t;
    for (char c : text) t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char* phrase : {"build the game", "build my game", "build the whole game", "build it all", "build the plan", "build everything"}) {
        if (t.find(phrase) != std::string::npos) return true;
    }
    return false;
}

// "Plan a game about...", "make me a game where...", "design an RPG...": the Planner's, not a proposal's.
bool LooksLikeGameRequest(const std::string& text) {
    std::string t;
    for (char c : text) t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char* phrase : {"plan a game", "plan the game", "design a game", "design the game", "make a game", "make me a game",
                               "create a game", "make an rpg", "make an arpg", "design an rpg", "plan an rpg", "make a life-sim",
                               "make a life sim"}) {
        if (t.find(phrase) != std::string::npos) return true;
    }
    return false;
}

} // namespace

namespace fs = std::filesystem;

namespace {

constexpr float kMinZoom = 0.25f;
constexpr float kMaxZoom = 4.0f;
constexpr double kPlayInspectorRefreshSeconds = 0.1;

// Ids shared with the UI's toolbar (apps/editor/ui/src/components/Toolbar.vue).
constexpr const char* kToolIds[] = {"select", "erase", "prefab", "height"};

// Accent colors for viewport overlays, matching the UI's accent.
constexpr float kAccent[3] = {0.231f, 0.510f, 0.965f};
constexpr float kErase[3] = {0.937f, 0.267f, 0.267f};

std::string DescribeKind(Registry& registry, Entity entity) {
    if (registry.all_of<Tile>(entity)) {
        return "Tile";
    }
    if (registry.all_of<Pickup>(entity)) {
        return "Pickup";
    }
    if (registry.all_of<Velocity2D, Collider2D>(entity)) {
        return "Player";
    }
    if (const auto* faction = registry.try_get<Faction>(entity); faction && faction->team == Team::Enemy) {
        return "Enemy";
    }
    if (registry.all_of<LuaScript>(entity)) {
        return "Scripted NPC";
    }
    // Characters belong to a side; props and scenery don't.
    if (registry.all_of<WorldTransform2D, Faction>(entity)) {
        return "NPC";
    }
    if (registry.all_of<WorldTransform2D>(entity)) {
        return "Prop";
    }
    return "Decoration";
}

void AddRectOutline(std::vector<OverlayLine>& out, float x0, float y0, float x1, float y1, const float* rgb,
                    float alpha, float thickness) {
    auto line = [&](float ax, float ay, float bx, float by) {
        out.push_back({ax, ay, bx, by, thickness, rgb[0], rgb[1], rgb[2], alpha});
    };
    line(x0, y0, x1, y0);
    line(x1, y0, x1, y1);
    line(x1, y1, x0, y1);
    line(x0, y1, x0, y0);
}

void AddCellOutline(std::vector<OverlayLine>& out, int gx, int gy, const IsoParams& iso, const float* rgb, float alpha,
                    float thickness) {
    ScreenPose a = ProjectIso(static_cast<float>(gx), static_cast<float>(gy), iso);
    ScreenPose b = ProjectIso(static_cast<float>(gx + 1), static_cast<float>(gy), iso);
    ScreenPose c = ProjectIso(static_cast<float>(gx + 1), static_cast<float>(gy + 1), iso);
    ScreenPose d = ProjectIso(static_cast<float>(gx), static_cast<float>(gy + 1), iso);
    auto line = [&](const ScreenPose& p, const ScreenPose& q) {
        out.push_back({p.x, p.y, q.x, q.y, thickness, rgb[0], rgb[1], rgb[2], alpha});
    };
    line(a, b);
    line(b, c);
    line(c, d);
    line(d, a);
}

// A rectangle of cells [x, x + w) x [y, y + h), outlined.
void AddGridRect(std::vector<OverlayLine>& out, float x, float y, float w, float h, const IsoParams& iso, const float* rgb,
                 float alpha, float thickness) {
    const ScreenPose a = ProjectIso(x, y, iso), b = ProjectIso(x + w, y, iso), c = ProjectIso(x + w, y + h, iso),
                     d = ProjectIso(x, y + h, iso);
    for (const auto& [p, q] : {std::pair{a, b}, std::pair{b, c}, std::pair{c, d}, std::pair{d, a}}) {
        out.push_back({p.x, p.y, q.x, q.y, thickness, rgb[0], rgb[1], rgb[2], alpha});
    }
}

void AddEntityOutline(std::vector<OverlayLine>& out, Registry& registry, Entity entity, const IsoParams& iso,
                      const float* rgb, float alpha, float thickness) {
    if (const auto* tile = registry.try_get<Tile>(entity)) {
        AddCellOutline(out, tile->gx, tile->gy, iso, rgb, alpha, thickness);
        return;
    }
    const auto* t = registry.try_get<ScreenTransform2D>(entity);
    const auto* s = registry.try_get<Sprite>(entity);
    if (t != nullptr && s != nullptr && s->flat) {
        const SpriteQuad quad = SpriteQuadOf(*t, *s, !iso.topDown); // the cell area it lies on (5.2)
        for (int i = 0; i < 4; ++i) {
            const int j = (i + 1) % 4;
            out.push_back({quad.x[i], quad.y[i], quad.x[j], quad.y[j], thickness, rgb[0], rgb[1], rgb[2], alpha});
        }
        return;
    }
    if (t != nullptr && s != nullptr) {
        AddRectOutline(out, t->x - s->width * 0.5f, t->y - s->height * 0.5f, t->x + s->width * 0.5f,
                       t->y + s->height * 0.5f, rgb, alpha, thickness);
    }
}

// A field's metadata and current value, as the Inspector (and the AI
// context) presents it.
nlohmann::json FieldJson(const FieldMeta& meta, const FieldValue& value) {
    nlohmann::json entry = {{"name", meta.name},
                            {"type", FieldTypeName(meta.type)},
                            {"description", meta.description},
                            {"value", ValueToJson(meta, value)}};
    if (!meta.options.empty()) {
        entry["options"] = meta.options;
    }
    if (meta.min) {
        entry["min"] = *meta.min;
    }
    if (meta.max) {
        entry["max"] = *meta.max;
    }
    if (!meta.scalable.empty()) {
        entry["scalable"] = meta.scalable; // the Inspector offers "by difficulty" (16.2)
    }
    return entry;
}

nlohmann::json AssetRows(const nlohmann::json& assets) { return AssetNeedRows(assets); }

} // namespace

EditorApp::EditorApp(HWND mainWindow, ViewportWindow& viewport, Registry& registry, LuaVM& lua, AssetDatabase& assets,
                     AudioEngine& audio, Renderer2D& renderer, PostFn post)
    : mainWindow_(mainWindow),
      viewport_(viewport),
      registry_(registry),
      lua_(lua),
      assets_(assets),
      prefabs_(assets.GetProject()),
      world_(assets.GetProject()),
      story_(assets.GetProject().root),
      schedules_(assets.GetProject().root),
      transcript_(assets.GetProject().CacheDir() / "ai_conversation.json"),
      artHistory_(assets.GetProject().CacheDir() / "art_history.json"),
      audio_(audio),
      renderer_(renderer),
      scenePath_(StartScenePath(assets.GetProject()).string()),
      post_(std::move(post)),
      iso_(BootstrapIso()),
      session_(registry, lua, iso_, assets.GetProject().root) {
    session_.SetSoundPlayer([this](const std::string& path) { audio_.Play(assets_.GetProject().Resolve(path)); });
    session_.SetAxisQuery([this](PadAxis axis) { return viewport_.PadAxisValue(axis); });
    // The AI chat and Art history come back with the project (4.5): every chat
    // message the panel is sent is also kept in .cache.
    transcript_.Load();
    conversation_ = transcript_.Context();
    artHistory_.Load();
    post_ = [this, raw = std::move(post_)](const nlohmann::json& message) {
        if (message.is_object() && AiTranscript::Records(message.value("type", ""))) {
            transcript_.Record(message, conversation_);
        }
        raw(message);
    };
    session_.SetSpawner([this](const std::string& prefab, float x, float y) {
        return prefabs_.Instantiate(registry_, prefab, x, y);
    });
    EnsureStarterPrefabs();
    prefabs_.Refresh();
    AnalyzeMissingTileEdges();
    // Scenes store full copies of every instance; syncing on open applies any
    // prefab edits made since the scene was saved.
    prefabs_.SyncAll(registry_);
    UpdateTitle();
    StartRecovery();
}

EditorApp::~EditorApp() {
    // A clean exit: no crash to recover from next time (what wasn't saved was the user's choice). Ended by a fatal
    // error (unwinding), the snapshot stays.
    if (std::uncaught_exceptions() > 0) return;
    std::error_code ec;
    fs::remove_all(RecoveryDir(), ec);
}

// --- Crash recovery -----------------------------------------------------------------------
// While the editor runs, .cache/recovery/running.json says so, and the open scene's unsaved state is written there
// every two minutes. Opened again with running.json still there (the editor didn't close: a crash), a snapshot newer
// than its scene file is offered back.

std::filesystem::path EditorApp::RecoveryDir() const {
    return assets_.GetProject().CacheDir() / "recovery";
}

void EditorApp::StartRecovery() {
    const fs::path dir = RecoveryDir();
    std::error_code ec;
    if (fs::exists(dir / "running.json", ec)) {
        const nlohmann::json meta = [&] {
            std::ifstream in(dir / "snapshot.json");
            return in ? nlohmann::json::parse(in, nullptr, false) : nlohmann::json();
        }();
        if (meta.is_object() && fs::exists(dir / "snapshot.scene", ec)) {
            const fs::path scene = assets_.GetProject().Resolve(meta.value("scene", ""));
            const auto snapshotTime = fs::last_write_time(dir / "snapshot.scene", ec);
            const bool newer = !fs::exists(scene, ec) || fs::last_write_time(scene, ec) < snapshotTime;
            if (newer) recoveryOffer_ = {{"type", "recovery:offer"}, {"scene", meta.value("scene", "")}, {"time", meta.value("time", 0LL)}};
        }
    }
    if (recoveryOffer_.is_null()) fs::remove(dir / "snapshot.scene", ec), fs::remove(dir / "snapshot.json", ec);
    fs::create_directories(dir, ec);
    const long long now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::ofstream(dir / "running.json") << nlohmann::json{{"started", now}}.dump() << "\n";
    recoverySaved_ = std::chrono::steady_clock::now();
}

void EditorApp::WriteRecovery() {
    // Every two minutes, while the open scene has changes not saved (never during Play: the scene is the game's then).
    const auto now = std::chrono::steady_clock::now();
    if (now - recoverySaved_ < std::chrono::minutes(2)) return;
    recoverySaved_ = now;
    if (!dirty_ || playing_ || !recoveryOffer_.is_null()) return; // an offer not answered yet keeps its snapshot
    const fs::path dir = RecoveryDir();
    std::error_code ec;
    fs::create_directories(dir, ec);
    {
        std::ofstream out(dir / "snapshot.scene.tmp", std::ios::binary | std::ios::trunc);
        out << SerializeScene(registry_);
    }
    fs::rename(dir / "snapshot.scene.tmp", dir / "snapshot.scene", ec);
    const long long time = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::ofstream(dir / "snapshot.json") << nlohmann::json{{"scene", OpenSceneRelative()}, {"time", time}}.dump() << "\n";
}

void EditorApp::AnswerRecovery(bool restore) {
    const fs::path dir = RecoveryDir();
    const std::string scene = recoveryOffer_.value("scene", "");
    recoveryOffer_ = nlohmann::json();
    std::error_code ec;
    if (restore) {
        std::ifstream in(dir / "snapshot.scene", std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        if (scene != OpenSceneRelative()) OpenScene(scene);
        if (scene == OpenSceneRelative() && !text.str().empty()) {
            registry_.clear();
            DeserializeScene(registry_, text.str());
            prefabs_.SyncAll(registry_);
            history_.Clear();
            Select(std::nullopt);
            dirty_ = true; // restored, not saved: Save keeps it
            UpdateTitle();
            UpdateWorldToScreen(registry_, iso_);
            sceneTreeStale_ = true;
            inspectorStale_ = true;
            editorStateStale_ = true;
            post_({{"type", "notify"}, {"text", "Restored the unsaved changes to " + scene + ": save to keep them"}});
        } else {
            post_({{"type", "notify"}, {"text", "Couldn't restore " + scene}});
        }
    }
    fs::remove(dir / "snapshot.scene", ec);
    fs::remove(dir / "snapshot.json", ec);
}

// --- IPC --------------------------------------------------------------------

// The Build panel's messages (7.4-7.9), apart: one chain of them all is too deep for the compiler.
bool EditorApp::HandleBuildMessage(const std::string& type, const nlohmann::json& message) {
    // While the build works off the UI thread, only what doesn't touch it; a call's usage waits for it (PostBuild).
    if (buildJob_ && type == "build:usage") {
        usageQueue_.push_back(message);
        return true;
    }
    if (buildJob_ && type != "build:request" && type != "build:list" && type != "build:sourcingRequest" && type != "build:sourcing" &&
        type != "build:choicesRequest" && type != "build:advanced") {
        post_({{"type", "notify"}, {"text", "The build is still working: a moment"}});
        PostBuild();
        return true;
    }
    if (type == "build:start") {
        StartBuild(message.value("query", ""), message.value("choices", nlohmann::json::object()));
    } else if (type == "build:choicesRequest") {
        nlohmann::json options = BuildChoices(Places());
        options["type"] = "build:choices";
        post_(options);
    } else if (type == "build:request") {
        PostBuild();
    } else if (type == "build:sourcingRequest") {
        const SourcingSettings settings = ReadSourcingSettings(assets_.GetProject().root);
        post_({{"type", "build:sourcing"}, {"webSearch", settings.webSearch}, {"shareAlike", settings.licences.shareAlike},
               {"defaultSource", settings.defaultSource}});
    } else if (type == "build:nested") {
        // A region's site, built as a place of its own where the region put it.
        if (!build_) return true;
        nlohmann::json site;
        for (const auto& s : build_->Sites()) {
            if (s.value("id", "") == message.value("id", "")) site = s;
        }
        if (site.is_null()) return true;
        const auto [ox, oy] = build_->Origin();
        const nlohmann::json bounds = site.value("bounds", nlohmann::json::array({0, 0, 0, 0}));
        std::string scale = site.value("scale", "");
        std::replace(scale.begin(), scale.end(), '-', ' ');
        std::string kind = site.value("buildType", "settlement");
        if (kind == "settlement") kind = scale.empty() ? "village" : scale;
        else if (!scale.empty() && scale != "village" && scale != "town" && scale != "city" && scale != "hamlet") kind = scale + " " + kind;
        const std::string name = site.value("name", "");
        StartBuild("Build a " + kind + (name.empty() ? "" : " called " + name));
        if (build_ && bounds.is_array() && bounds.size() == 4) build_->SetOrigin(ox + bounds[0].get<int>(), oy + bounds[1].get<int>());
        PostBuild();
    } else if (type == "build:list") {
        post_({{"type", "build:list"}, {"builds", ListBuilds(assets_.GetProject().root)}});
    } else if (type == "build:removeOlder") {
        // Every build but the open one (the panel asked first).
        const int removed = RemoveBuilds(assets_.GetProject().root, build_ ? build_->Id() : std::string());
        post_({{"type", "notify"}, {"text", "Removed " + std::to_string(removed) + " older build(s)"}});
        post_({{"type", "build:list"}, {"builds", ListBuilds(assets_.GetProject().root)}});
    } else if (type == "build:remove") {
        // One build, from Your builds (the panel asked first); the open one is closed first.
        const std::string id = message.value("id", "");
        if (build_ && build_->Id() == id) {
            if (buildRun_ || build_->Stage() == "sourcing" || build_->MakingBackground()) {
                post_({{"type", "notify"}, {"text", "The build is still working: stop it first"}});
                return true;
            }
            build_.reset();
            buildShown_ = nlohmann::json();
            afterBackground_.clear();
        }
        post_({{"type", "notify"}, {"text", RemoveBuild(assets_.GetProject().root, id) ? "Removed the build" : "Couldn't remove that build"}});
        post_({{"type", "build:list"}, {"builds", ListBuilds(assets_.GetProject().root)}});
        PostBuild();
    } else if (type == "build:advanced") {
        if (build_) {
            nlohmann::json doc = build_->Advanced(message.value("what", ""));
            doc["type"] = "build:advanced";
            post_(doc);
        }
    } else if (type == "build:resume") {
        ResumeBuild(message.value("id", ""));
    } else if (type == "build:edit") {
        if (build_) {
            const std::string path = message.value("path", "");
            build_->Edit(path, message.value("value", nlohmann::json()));
            // A part's source changed in ③ Assets: what it gets is decided again.
            if (path.ends_with(".assets.source") &&
                (build_->Stage() == "planned" || build_->Stage() == "sourced" || build_->Stage() == "built")) {
                PreviewAssets();
            }
        }
        PostBuild();
    } else if (type == "build:step" || type == "build:confirm") {
        // Planning (without the AI, all of it here) and laying out: off the UI thread.
        const std::string step = message.value("step", "");
        const bool confirm = type == "build:confirm";
        RunBuildJob([confirm, step](PlaceBuild& build) { return confirm ? build.Confirm() : build.Step(step); }, "Not yet: ");
    } else if (type == "build:decision") {
        if (build_) build_->AnswerDecision(message.value("question", ""), message.value("answer", ""));
        PostBuild();
    } else if (type == "build:directions") {
        if (build_) build_->SetDirections(message.value("text", ""));
    } else if (type == "build:replanLayout") {
        RunBuildJob([](PlaceBuild& build) { return build.ReplanLayout(); }, "Not now: ");
    } else if (type == "build:zoneKind") {
        const std::string module = message.value("module", ""), kind = message.value("kind", "");
        const nlohmann::json value = message.value("value", nlohmann::json());
        RunBuildJob([module, kind, value](PlaceBuild& build) { return build.SetZoneKind(module, kind, value); }, "Can't set it: ");
    } else if (type == "build:zoneField") {
        const std::string zone = message.value("zone", ""), field = message.value("field", "");
        const nlohmann::json value = message.value("value", nlohmann::json());
        RunBuildJob([zone, field, value](PlaceBuild& build) { return build.SetZoneField(zone, field, value); }, "Can't set it: ");
    } else if (type == "build:rootField") {
        // The layout form (the Layout section): one of the place's layout fields, and the place laid out again.
        const std::string field = message.value("field", "");
        const nlohmann::json value = message.value("value", nlohmann::json());
        RunBuildJob([field, value](PlaceBuild& build) { return build.SetRootField(field, value); }, "Can't set it: ");
    } else if (type == "build:paintZone") {
        const std::string zone = message.value("zone", "");
        const nlohmann::json cells = message.value("cells", nlohmann::json::array());
        const bool erase = message.value("erase", false);
        RunBuildJob([zone, cells, erase](PlaceBuild& build) { return build.PaintZone(zone, cells, erase); }, "Can't paint it: ");
    } else if (type == "build:drawZone") {
        const std::string zone = message.value("zone", "");
        const nlohmann::json rect = message.value("rect", nlohmann::json());
        RunBuildJob([zone, rect](PlaceBuild& build) { return build.DrawZone(zone, rect); }, "Can't draw it: ");
    } else if (type == "build:planFromLayout") {
        RunBuildJob([](PlaceBuild& build) { return build.PlanFromLayout(); }, "Not yet: ");
    } else if (type == "build:approve") {
        RunBuildJob([](PlaceBuild& build) { return build.Approve(); }, "Not yet: ");
    } else if (type == "build:revise" || type == "build:review" || type == "build:undo" || type == "build:redo") {
        const std::string text = message.value("text", "");
        RunBuildJob([type, text](PlaceBuild& build) {
            return type == "build:revise" ? build.Revise(text)
                   : type == "build:review" ? build.Review()
                   : type == "build:undo"   ? build.Undo()
                                            : build.Redo();
        }, "Not now: ");
    } else if (type == "build:relayout") {
        RunBuildJob([](PlaceBuild& build) { return build.Relayout(); }, "Not now: ");
    } else if (type == "build:editNode" || type == "build:regenerate") {
        // A part laid out again (a partial solve), then the scene follows, on the UI thread.
        const std::string node = message.value("node", ""), field = message.value("field", "");
        const nlohmann::json value = message.value("value", nlohmann::json());
        RunBuildJob([type, node, field, value](PlaceBuild& build) {
            return type == "build:editNode" ? build.EditNode(node, field, value) : build.Regenerate(node);
        }, "Not now: ", [this] {
            UpdateBuiltParts();
            PostSelectedPart();
        });
    } else if (type == "build:lock" || type == "build:brush") {
        if (build_) {
            if (type == "build:lock") build_->Lock(message.value("node", ""), message.value("locked", true));
            else placeBrush_ = message.value("module", "");
            UpdateBuiltParts();
            PostSelectedPart();
        }
        PostBuild();
    } else if (type == "build:sourcing") {
        SourcingSettings settings = ReadSourcingSettings(assets_.GetProject().root);
        if (message.contains("webSearch")) settings.webSearch = message.value("webSearch", false);
        if (message.contains("shareAlike")) settings.licences.shareAlike = message.value("shareAlike", false);
        if (message.contains("defaultSource") && message["defaultSource"].is_string()) settings.defaultSource = message["defaultSource"].get<std::string>();
        WriteSourcingSettings(assets_.GetProject().root, settings);
        post_({{"type", "build:sourcing"}, {"webSearch", settings.webSearch}, {"shareAlike", settings.licences.shareAlike},
               {"defaultSource", settings.defaultSource}});
        // The default changed: what each part would get is shown again (nothing is found or made until asked).
        if (message.contains("defaultSource") && build_ && !buildJob_) {
            const std::string stage = build_->Stage();
            if (stage == "planned" || stage == "sourced" || stage == "built") {
                PreviewAssets();
                PostBuild();
            }
        }
    } else if (type == "scene:backgroundImage") {
        // Edit ▸ Background Image…: a top-down picture of your own as the place's ground (the whole site), copied into
        // the project; the place's scene is laid with it again at once.
        if (!build_ || build_->Background().is_null() && build_->Stage() != "built" && build_->Stage() != "sourced" && build_->Stage() != "planned") {
            post_({{"type", "notify"}, {"text", "Open a place's build (Build panel) to give its scene a background image"}});
            return true;
        }
        const auto file = ShowOpenFileDialog(mainWindow_, L"Background image (top-down, the whole site)", L"Images", L"*.png;*.jpg;*.jpeg");
        if (!file) return true;
        const std::filesystem::path root = assets_.GetProject().root;
        const std::string rel = "assets/backgrounds/" + WideToUtf8(file->filename().wstring());
        std::error_code ec;
        std::filesystem::create_directories(root / "assets" / "backgrounds", ec);
        std::filesystem::copy_file(*file, root / rel, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            post_({{"type", "notify"}, {"text", "Couldn't copy the picture: " + ec.message()}});
            return true;
        }
        if (const std::string why = build_->SetBackground(rel); !why.empty()) {
            post_({{"type", "notify"}, {"text", "No background: " + why}});
            return true;
        }
        if (build_->Stage() == "built") BuildIntoScene(); // the scene laid with it now
        PostBuild();
    } else if (type == "build:background") {
        // The place's background painted from its plan (docs/places/background.md), from its description.
        if (!build_) return true;
        if (message.contains("description") && message["description"].is_string()) build_->Edit("background.description", message["description"]);
        if (const std::string why = build_->MakeBackground(); !why.empty()) post_({{"type", "notify"}, {"text", "No background: " + why}});
        PostBuild();
    } else if (type == "build:matchedArt") {
        // ③ Matched art (docs/places/matched-art.md): every need made by the art provider in one style, then cut
        // (CutMatchedArt).
        MakeMatched();
        PostBuild();
    } else if (type == "build:findAssets") {
        // ③ The art: found (each part from its source) or made in one style ("then": "matched"). The background
        // first (the whole place, then its detail windows) when there's none yet: the art starts when it's in
        // (OnBuildResult). No background to paint (no plan laid out, the AI off): the art starts now.
        if (!build_ || buildJob_ || buildRun_) return true;
        const std::string then = message.value("then", "find") == "matched" ? "matched" : "find";
        if (then == "find") {
            build_->ForgetTried(); // asked again: what's still missing is made again
            autoRounds_ = 0;
        }
        afterBackground_.clear();
        const std::string stage = build_->Stage();
        if (build_->MakingBackground() ||
            (build_->Background().is_null() && (stage == "planned" || stage == "sourced" || stage == "built") && build_->MakeBackground().empty())) {
            afterBackground_ = then;
            PostBuild();
            return true;
        }
        if (then == "matched") MakeMatched();
        else FindAssets();
        PostBuild();
    } else if (type == "build:needSource" || type == "build:allSources" || type == "build:pickArt") {
        // ④ Assets: a need's own source (or every need's), or a piece of art picked for it.
        if (!build_) return true;
        if (type == "build:allSources") {
            const std::string source = message.value("source", "");
            for (const auto& n : build_->State().value("assetNeeds", nlohmann::json::array())) {
                const std::string key = n.value("key", "");
                if (key.empty() || key.rfind("terrain|", 0) == 0) continue;
                SetNeedChoice(key, source.empty() || source == "auto" ? nlohmann::json() : nlohmann::json{{"source", source}});
            }
        } else if (type == "build:needSource") {
            const std::string source = message.value("source", "");
            SetNeedChoice(message.value("key", ""), source.empty() || source == "auto" ? nlohmann::json() : nlohmann::json{{"source", source}});
        } else {
            // From the project (a path), or the user's library (copied into the project first).
            std::string path = message.value("path", "");
            if (const std::string item = message.value("libraryItem", ""); !item.empty()) {
                path = UserLibrary(UserLibraryDir()).CopyToProject(item, assets_.GetProject().root);
                if (!path.empty()) RefreshAssets();
            }
            if (path.empty() || !Project::IsInside(path)) {
                post_({{"type", "notify"}, {"text", "Couldn't use that art"}});
                return true;
            }
            if (message.value("key", "") == "background") {
                // The place's background: a picture of the whole ground, laid cell by cell.
                if (const std::string why = build_->SetBackground(path); !why.empty()) post_({{"type", "notify"}, {"text", "No background: " + why}});
                PostBuild();
                return true;
            }
            SetNeedChoice(message.value("key", ""), {{"entry", path}});
        }
        PreviewAssets();
        PostBuild();
    } else if (type == "build:build") {
        BuildIntoScene();
        PostBuild();
    } else if (type == "build:usage") {
        // A finished AI call of the build (the panel saw it end on the progress strip): its step's totals.
        if (build_) build_->AddUsage(message.value("step", ""), message.value("inputTokens", 0), message.value("outputTokens", 0), message.value("seconds", 0.0));
        return true; // the panel shows it with the next state
    } else if (type == "build:inspectBuilt") {
        // ⑤ Result, when asked: the vision model looks at the built place as the game draws it, in windows
        // (docs/build-fixes.md). Minutes for a big place, so never on its own.
        if (!build_ || buildRun_ || build_->Stage() != "built" || build_->Inspecting()) return true;
        if (!Ai() || Ai()->GetState() != AiClient::State::Ready) {
            post_({{"type", "notify"}, {"text", "The AI isn't there to look at it"}});
            return true;
        }
        const auto [ox, oy] = build_->Origin();
        if (const std::string why = build_->InspectBuilt(InspectBuildRequest(registry_, assets_.GetProject(), iso_, *build_, ox, oy, 56, &Places()));
            !why.empty()) {
            post_({{"type", "notify"}, {"text", "Can't look at it: " + why}});
        }
        PostBuild();
    } else if (type == "build:finish") {
        // The place stays in the scene: saved with it, and the build closed (Your builds reopens it).
        if (build_ && build_->Stage() == "built") {
            const std::string name = build_->Brief().value("name", build_->Id());
            Save();
            build_.reset();
            buildShown_ = nlohmann::json();
            post_({{"type", "notify"}, {"text", name + " is in the scene, and the scene is saved"}});
        }
        PostBuild();
    } else if (type == "build:artVerdict") {
        // ④ Assets: the user's own check of a piece the Art tool made. Rejected, it's no longer art
        // to choose (the catalog forgets what it's for), and the need goes to its other sources.
        if (build_) {
            std::vector<std::string> paths;
            const std::string verdict = message.value("verdict", "");
            const std::string why = build_->SetArtVerdict(message.value("key", ""), verdict, &paths);
            if (!why.empty()) {
                post_({{"type", "notify"}, {"text", "Can't: " + why}});
            } else if (verdict == "rejected") {
                for (const auto& path : paths) SetCatalogFields(assets_, path, {{"roles", ""}, {"rejected", "1"}});
                assets_.Refresh();
                post_({{"type", "notify"}, {"text", "Rejected: the next round finds it another way (Find the art again)"}});
            }
        }
        PostBuild();
    } else if (type == "build:refineArt") {
        if (build_) {
            const std::string why = build_->RefineArt(message.value("key", ""), message.value("text", ""));
            if (!why.empty()) post_({{"type", "notify"}, {"text", "Can't refine it: " + why}});
        }
        PostBuild();
    } else if (type == "build:skipSourcing") {
        if (build_) {
            build_->SkipSourcing();
            AfterSourcing();
        }
    } else if (type == "build:cancel") {
        if (build_) build_->Cancel();
        PostBuild();
    } else {
        return false;
    }
    return true;
}

// Difficulty (16.5): the Difficulty tab's requests.
// New Project's messages: its folder picked, the project made with its choices.
bool EditorApp::HandleProjectMessage(const std::string& type, const nlohmann::json& message) {
    if (type == "project:location") {
        // New Project opened: the folder the last project was created in (the user's prefs, read fresh).
        post_({{"type", "project:location"}, {"path", UserPrefs(UserPrefs::DefaultPath()).Get("newProjectFolder", "")}});
    } else if (type == "project:browse") {
        // New Project's Browse…: the folder back to its window.
        if (auto folder = ShowFolderDialog(mainWindow_, Utf8ToWide(message.value("title", "Where to create the project")))) {
            post_({{"type", "project:browsed"}, {"path", WideToUtf8(folder->wstring())}});
        }
    } else if (type == "project:new") {
        // New Project's window: its name, where, its grid, a template, and its own AI (Project Settings ▸ AI).
        const std::string name = message.value("name", ""), location = message.value("location", "");
        if (name.empty() || location.empty()) {
            return true;
        }
        std::filesystem::path root = std::filesystem::path(Utf8ToWide(location)) / Utf8ToWide(name);
        UserPrefs(UserPrefs::DefaultPath()).Set("newProjectFolder", location); // offered next time
        const std::string templateId = message.value("template", "");
        if (!templateId.empty()) {
            // A template project (24): it plays as its genre from the start.
            if (auto error = CreateProjectFromTemplate(root, templateId)) {
                post_({{"type", "notify"}, {"text", *error}});
                return true;
            }
            SwitchProject(root);
            return true;
        }
        std::map<std::string, std::string> ai;
        for (const auto& [k, v] : message.value("ai", nlohmann::json::object()).items()) {
            if (v.is_string()) ai[k] = v.get<std::string>();
        }
        if (auto error = CreateProject(root, std::nullopt, message.value("topDown", false), ai)) {
            post_({{"type", "notify"}, {"text", *error}});
            return true;
        }
        SwitchProject(root);
    } else {
        return false;
    }
    return true;
}

bool EditorApp::HandleDifficultyMessage(const std::string& type, const nlohmann::json& message) {
    if (type == "difficulty:request") {
        ActiveRecords().Refresh();
        session_.ReloadDifficulty();
        post_(DifficultyJson());
    } else if (type == "difficulty:preview") {
        // Every scalable field of an entity at every level (the Inspector's "by difficulty").
        const auto entity = static_cast<Entity>(message.value("id", 0u));
        post_({{"type", "difficulty:preview"},
               {"id", message.value("id", 0u)},
               {"fields", registry_.valid(entity) ? DifficultyPreview(registry_, EngineReflection(), entity, session_.Difficulty())
                                                  : nlohmann::json::array()}});
    } else if (type == "difficulty:override") {
        // {id, field, level, value|null}: a hand-set value for one level, or back to the computed one.
        const auto entity = static_cast<Entity>(message.value("id", 0u));
        if (registry_.valid(entity) && !playing_) {
            nlohmann::json values = nlohmann::json::object();
            if (const auto* o = registry_.try_get<DifficultyOverrides>(entity); o && !o->values.empty()) {
                values = nlohmann::json::parse(o->values, nullptr, false);
                if (!values.is_object()) values = nlohmann::json::object();
            }
            const std::string field = message.value("field", ""), level = message.value("level", "");
            if (message.contains("value") && message["value"].is_number()) {
                values[field][level] = message["value"];
            } else if (values.contains(field)) {
                values[field].erase(level);
                if (values[field].empty()) values.erase(field);
            }
            const std::string text = values.empty() ? std::string() : values.dump();
            // One undoable edit: the component comes and goes with its values.
            ApplyOneShotEdit([&] {
                std::optional<std::string> failure;
                if (text.empty()) {
                    if (registry_.all_of<DifficultyOverrides>(entity)) {
                        failure = EditRemoveComponent(EngineReflection(), registry_, entity, "DifficultyOverrides");
                    }
                } else {
                    if (!registry_.all_of<DifficultyOverrides>(entity)) {
                        failure = EditAddComponent(EngineReflection(), registry_, &prefabs_, entity, "DifficultyOverrides");
                    }
                    if (!failure) failure = EditField(EngineReflection(), registry_, entity, "DifficultyOverrides", "values", text);
                }
                return !failure.has_value();
            });
            inspectorStale_ = true;
            post_({{"type", "difficulty:preview"},
                   {"id", message.value("id", 0u)},
                   {"fields", DifficultyPreview(registry_, EngineReflection(), entity, session_.Difficulty())}});
        }
    } else if (type == "difficulty:combat") {
        // An enemy (the selection) against the scene's player at each level.
        const auto enemy = static_cast<Entity>(message.value("id", 0u));
        const auto player = FindPlayer(registry_);
        nlohmann::json rows = nlohmann::json::array();
        if (registry_.valid(enemy) && player) {
            for (const auto& r : CombatPreview(registry_, EngineReflection(), enemy, *player, session_.Difficulty())) {
                rows.push_back({{"level", r.level}, {"enemyHealth", r.enemyHealth}, {"enemyDamage", r.enemyDamage},
                                {"playerHealth", r.playerHealth}, {"playerDamage", r.playerDamage},
                                {"hitsToKillEnemy", r.hitsToKillEnemy}, {"hitsToKillPlayer", r.hitsToKillPlayer},
                                {"secondsToKillEnemy", r.secondsToKillEnemy}, {"secondsToKillPlayer", r.secondsToKillPlayer}});
            }
        }
        post_({{"type", "difficulty:combat"}, {"rows", rows}, {"error", !player ? "The scene has no player." :
                                                                       !registry_.valid(enemy) ? "Select an enemy." : ""}});
    } else if (type == "difficulty:curve") {
        post_(DifficultyCurveJson());
    } else if (type == "difficulty:play") {
        playDifficulty_ = message.value("level", "");
        if (playing_ && !playDifficulty_.empty()) session_.SetDifficulty(playDifficulty_);
    } else {
        return false;
    }
    return true;
}

void EditorApp::OnWebMessage(const nlohmann::json& message) {
    const std::string type = message.value("type", "");
    const SlowWatch watch("message " + type);
    if (type.rfind("build:", 0) == 0 && HandleBuildMessage(type, message)) return;
    if (type == "recovery:request") {
        // The page is up: a crash's unsaved changes offered back (StartRecovery).
        if (!recoveryOffer_.is_null()) post_(recoveryOffer_);
        return;
    }
    if (type == "recovery:restore" || type == "recovery:discard") {
        if (!recoveryOffer_.is_null()) AnswerRecovery(type == "recovery:restore");
        return;
    }
    if (type == "entity:replacePrefab") {
        // Replace prefab (the viewport's right-click menu, the Inspector): this thing a copy of another, as one undo step.
        if (playing_) return;
        const ApplyOutcome outcome = ApplyAccepted({{{"op", "command"},
                                                     {"name", "entity.replacePrefab"},
                                                     {"args", {{"entity", message.value("id", 0u)}, {"to", message.value("prefab", "")}}}}});
        if (!outcome.errors.empty()) post_({{"type", "notify"}, {"text", outcome.errors.front()}});
        return;
    }
    if (type.rfind("ontology:", 0) == 0 && HandleOntologyMessage(type, message)) return;
    if (type.rfind("library:", 0) == 0 && HandleLibraryMessage(type, message)) return;
    if (type.rfind("difficulty:", 0) == 0 && HandleDifficultyMessage(type, message)) return;
    if (type.rfind("ui:", 0) == 0 && HandleUiMessage(type, message)) return;
    if ((type == "project:location" || type == "project:browse" || type == "project:new") && HandleProjectMessage(type, message)) return;
    if (type.rfind("fx:", 0) == 0 && HandleFxMessage(type, message)) return;
    if (type.rfind("magic:", 0) == 0 && HandleMagicMessage(type, message)) return;
    if (type.rfind("storygraph:", 0) == 0 && HandleStoryGraphMessage(type, message)) return;
    if (type.rfind("planner:", 0) == 0 && HandlePlannerMessage(type, message)) return;

    if (type == "viewport:bounds") {
        int width = message.value("width", 0);
        int height = message.value("height", 0);
        if (width > 0 && height > 0) {
            viewport_.SetBounds(message.value("x", 0), message.value("y", 0), width, height);
            viewportBounds_ = {message.value("x", 0), message.value("y", 0), width, height};
            ApplyOcclusion();
            if (!framedOnce_) {
                framedOnce_ = true;
                FrameScene();
            }
        }
    } else if (type == "edit:scale") {
        Resize(message.value("factor", 1.0f));
    } else if (type == "edit:nudge") {
        NudgeSelected(message.value("dx", 0.0f), message.value("dy", 0.0f));
    } else if (type == "edit:copy") {
        CopySelected();
    } else if (type == "edit:paste") {
        if (message.contains("gx") && message.contains("gy")) {
            PasteAtCursor(std::pair{message.value("gx", 0), message.value("gy", 0)});
        } else {
            PasteAtCursor();
        }
    } else if (type == "view:grid") {
        showGrid_ = message.value("on", !showGrid_);
        editorStateStale_ = true;
    } else if (type == "ui:error") {
        if (message.value("fatal", false)) {
            MessageBoxW(mainWindow_, Utf8ToWide("The editor's interface failed to start:\n\n" + message.value("text", "")).c_str(),
                        L"Editor", MB_ICONERROR);
        }
    } else if (type == "keys:request") {
        // The user's own shortcuts (5.6), on top of the defaults in the UI.
        post_({{"type", "keys:map"},
               {"overrides", userPrefs_ ? userPrefs_->Get("keymap", nlohmann::json::object()) : nlohmann::json::object()}});
    } else if (type == "keys:set") {
        if (userPrefs_ && message.contains("overrides") && message["overrides"].is_object()) {
            userPrefs_->Set("keymap", message["overrides"]);
        }
    } else if (type == "viewport:visible") {
        // Ctrl+M (5.5): the UI lets the bottom panel take the space.
        ShowWindow(viewport_.Handle(), message.value("on", true) ? SW_SHOWNA : SW_HIDE);
    } else if (type == "view:stats") {
        showStats_ = !showStats_;
    } else if (type == "view:physics") {
        // Colliders, triggers and bodies over the scene (20.4); F5 in play.
        session_.SetPhysicsDebug(message.value("on", !session_.PhysicsDebug()));
    } else if (type == "play:save") {
        SaveGame();
    } else if (type == "play:load") {
        LoadGame();
    } else if (type == "play:toggle") {
        playing_ ? StopPlay() : StartPlay();
    } else if (type == "scenes:request") {
        post_(ScenesJson());
    } else if (type == "scene:open") {
        OpenScene(message.value("path", ""));
    } else if (type == "scene:new") {
        SceneResult created = CreateScene(assets_.GetProject(), message.value("name", ""));
        if (!created.error.empty()) {
            post_({{"type", "notify"}, {"text", "Couldn't create the scene: " + created.error}});
        } else {
            OpenScene(created.path);
        }
        post_(ScenesJson());
    } else if (type == "scene:duplicate") {
        const std::string from = message.value("path", "");
        if (from == OpenSceneRelative() && dirty_) {
            Save(); // the copy is of the scene as it is now
        }
        SceneResult copy = DuplicateScene(assets_.GetProject(), from, message.value("name", ""));
        post_({{"type", "notify"},
               {"text", copy.error.empty() ? "Copied to " + copy.path : "Couldn't copy: " + copy.error}});
        post_(ScenesJson());
    } else if (type == "scene:rename") {
        const std::string to = ScenePathFor(message.value("name", ""));
        if (to.empty()) {
            post_({{"type", "notify"}, {"text", "A scene needs a name with letters or digits"}});
        } else {
            MoveProjectItem(message.value("path", ""), to);
        }
        post_(ScenesJson());
    } else if (type == "scene:delete") {
        const std::string scene = message.value("path", "");
        const std::string error = scene == OpenSceneRelative() ? "it's open; open another scene first"
                                                               : DeleteScene(assets_.GetProject(), scene);
        post_({{"type", "notify"}, {"text", error.empty() ? "Deleted " + scene : "Couldn't delete " + scene + ": " + error}});
        post_(ScenesJson());
    } else if (type == "scene:setStart") {
        const std::string scene = message.value("path", "");
        if (auto error = EditSetting(EngineReflection(), "Game", "startScene", scene)) {
            post_({{"type", "notify"}, {"text", *error}});
        } else {
            SaveSettings(EngineReflection(), assets_.GetProject().SettingsPath().string());
            post_({{"type", "notify"}, {"text", scene + " is now where the game starts"}});
        }
        post_(ScenesJson());
    } else if (type == "game:export") {
        ExportGameTo(message.value("name", ""), message.value("zip", false), message.value("debugConsole", false));
    } else if (type == "game:reveal") {
        const std::wstring folder = Utf8ToWide(message.value("path", ""));
        ShellExecuteW(mainWindow_, nullptr, L"explorer.exe", (L"\"" + folder + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
    } else if (type == "folder:rename") {
        if (Project::IsInside(message.value("folder", "")) && Project::IsInside(message.value("to", ""))) {
            RenameFolder(message.value("folder", ""), message.value("to", ""));
        }
    } else if (type == "folder:remove") {
        if (Project::IsInside(message.value("folder", ""))) RemoveFolder(message.value("folder", ""), message.value("deleteContents", false));
    } else if (type == "file:move") {
        MoveProjectItem(message.value("from", ""), message.value("to", ""));
    } else if (type == "prefab:remove") {
        RemovePrefab(message.value("path", ""));
    } else if (type == "project:request") {
        post_(ProjectInfoJson());
    } else if (type == "project:open") {
        std::filesystem::path root;
        if (message.contains("path") && message["path"].is_string()) {
            root = std::filesystem::path(Utf8ToWide(message["path"].get<std::string>()));
        } else if (auto picked = ShowFolderDialog(mainWindow_, L"Open project")) {
            root = *picked;
        } else {
            return;
        }
        if (!LooksLikeProject(root)) {
            post_({{"type", "notify"}, {"text", WideToUtf8(root.wstring()) + " isn't an AIGE project"}});
            return;
        }
        SwitchProject(root);
    } else if (type == "project:close") {
        // Back to the welcome window (5.4), after the usual question.
        if (playing_) {
            StopPlay();
        }
        if (AskToSaveChanges(L"Save changes to the scene before closing the project?")) {
            if (StartEditorProcess(L"--welcome")) {
                DestroyWindow(mainWindow_);
            }
        }
    } else if (type == "prefs:openLast") {
        if (userPrefs_) {
            userPrefs_->Set("openLastProject", message.value("on", false));
        }
        post_(ProjectInfoJson());
    } else if (type == "project:saveAs") {
        // The copy gets the scene as it is now, saved or not; this project's
        // own files are left as they are.
        const std::string name = message.value("name", "");
        auto parent = ShowFolderDialog(mainWindow_, L"Where to save \"" + Utf8ToWide(name) + L"\"");
        if (!parent || name.empty() || playing_) {
            return;
        }
        std::filesystem::path to = *parent / Utf8ToWide(name);
        if (auto error = CopyProject(assets_.GetProject().root, to)) {
            post_({{"type", "notify"}, {"text", *error}});
            return;
        }
        Project copy{to};
        SaveScene(registry_, copy.MainScenePath().string());
        SaveSettings(EngineReflection(), copy.SettingsPath().string());
        SwitchProject(to, /*askAboutChanges=*/false);
    } else if (type == "app:quit") {
        PostMessageW(mainWindow_, WM_CLOSE, 0, 0); // asks about unsaved changes first
    } else if (type == "web:open") {
        // A free-art site's search, in the browser (the Web Search tab's Sources): web links only.
        const std::string url = message.value("url", "");
        if (url.rfind("https://", 0) == 0 && url.find_first_of(" \"\r\n") == std::string::npos) {
            ShellExecuteW(mainWindow_, L"open", Utf8ToWide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    } else if (type == "help:docs") {
        ShellExecuteW(mainWindow_, L"open", Utf8ToWide(AIGE_EDITOR_DOCS_DIR).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else if (type == "scene:placeAt" || type == "scene:addMarker") {
        // From the viewport's right-click menu, as one undo step.
        if (playing_) {
            return;
        }
        nlohmann::json op;
        if (type == "scene:placeAt") {
            const std::string prefab = message.value("prefab", "");
            const PrefabInfo* info = prefabs_.Find(prefab);
            if (info == nullptr) {
                return;
            }
            float x = message.value("gx", 0) + 0.5f;
            float y = message.value("gy", 0) + 0.5f;
            op = info->isTile ? nlohmann::json{{"op", "command"},
                                               {"name", "scene.paintRect"},
                                               {"args",
                                                {{"prefab", prefab},
                                                 {"x0", message.value("gx", 0)},
                                                 {"y0", message.value("gy", 0)},
                                                 {"x1", message.value("gx", 0)},
                                                 {"y1", message.value("gy", 0)}}}}
                              : nlohmann::json{{"op", "command"},
                                               {"name", "scene.spawn"},
                                               {"args", {{"prefab", prefab}, {"x", x}, {"y", y}}}};
        } else {
            op = {{"op", "command"},
                  {"name", "scene.addMarker"},
                  {"args",
                   {{"name", message.value("name", "")},
                    {"x", message.value("gx", 0) + 0.5f},
                    {"y", message.value("gy", 0) + 0.5f}}}};
        }
        ApplyOutcome outcome = ApplyAccepted({op});
        if (!outcome.errors.empty()) {
            post_({{"type", "notify"}, {"text", outcome.errors.front()}});
        }
    } else if (type == "asset:reveal") {
        std::wstring target = L"/select,\"" + assets_.GetProject().Resolve(message.value("path", "")).wstring() + L"\"";
        ShellExecuteW(mainWindow_, nullptr, L"explorer.exe", target.c_str(), nullptr, SW_SHOWNORMAL);
    } else if (type == "asset:delete") {
        const std::string path = message.value("path", "");
        if (path.rfind("assets/", 0) == 0 && path.find("..") == std::string::npos) {
            std::error_code ec;
            for (const char* suffix : {"", ".meta", ".gen.json"}) {
                std::filesystem::remove(assets_.GetProject().Resolve(path + suffix), ec);
            }
            RefreshAssets();
        }
    } else if (type == "viewport:occlude") {
        // Open menus and dialogs, in page pixels: cut out of the viewport.
        overlays_.clear();
        for (const auto& r : message.value("rects", nlohmann::json::array())) {
            if (r.is_array() && r.size() == 4) {
                overlays_.push_back({r[0].get<int>(), r[1].get<int>(), r[2].get<int>(), r[3].get<int>()});
            }
        }
        ApplyOcclusion();
    } else if (type == "scene:request") {
        // The page (re)loaded: resend everything it needs.
        post_(AiStatusJson());
        sceneTreeStale_ = true;
        editorStateStale_ = true;
        Select(selected_);
    } else if (type == "entity:select") {
        // id null (or missing) deselects: the Inspector then shows Project Settings.
        std::optional<Entity> entity;
        if (message.contains("id") && message["id"].is_number_unsigned()) {
            Entity e = static_cast<Entity>(message["id"].get<uint32_t>());
            if (registry_.valid(e)) entity = e;
        }
        Select(entity);
    } else if (type == "entity:setSize") {
        // The Inspector's Size %: the sprite and its collision together,
        // as [ and ] do, so a copy stays the prefab scaled.
        auto entity = static_cast<Entity>(message.value("id", 0u));
        const float target = message.value("percent", 0.0f);
        const auto current = registry_.valid(entity) ? SizePercent(entity) : std::nullopt;
        if (!playing_ && current && *current > 0.0f && target > 0.0f && std::abs(target - *current) > 0.05f) {
            std::optional<std::string> problem;
            ApplyOneShotEdit([&] {
                problem = ScaleEntity(EngineReflection(), registry_, entity, target / *current);
                return !problem.has_value();
            });
            if (problem) {
                post_({{"type", "notify"}, {"text", *problem}});
            }
        }
        inspectorStale_ = true;
    } else if (type == "inspector:setField") {
        auto entity = static_cast<Entity>(message.value("id", 0u));
        if (registry_.valid(entity) && message.contains("value")) {
            std::string component = message.value("component", "");
            std::string field = message.value("field", "");
            std::optional<std::string> error;
            ApplyOneShotEdit([&] {
                // The same write path an applied AI proposal uses (editing/Edits).
                error = EditField(EngineReflection(), registry_, entity, component, field, message["value"]);
                return !error.has_value();
            });
            // The Inspector shows the reason next to the field; the refresh
            // below puts the field back to its real value.
            post_({{"type", "inspector:fieldError"},
                   {"component", component},
                   {"field", field},
                   {"error", error ? nlohmann::json(*error) : nlohmann::json(nullptr)}});
            inspectorStale_ = true;
            sceneTreeStale_ = true; // a Name edit changes the tree's label
        }
    } else if (type == "component:add" || type == "component:remove") {
        auto entity = static_cast<Entity>(message.value("id", 0u));
        std::string component = message.value("component", "");
        if (!playing_ && registry_.valid(entity)) {
            std::optional<std::string> failure;
            ApplyOneShotEdit([&] {
                failure = type == "component:add"
                              ? EditAddComponent(EngineReflection(), registry_, &prefabs_, entity, component)
                              : EditRemoveComponent(EngineReflection(), registry_, entity, component);
                return !failure.has_value();
            });
            if (failure) {
                post_({{"type", "notify"}, {"text", "Can't change " + component + ": " + *failure}});
            }
            inspectorStale_ = true;
            sceneTreeStale_ = true;
        }
    } else if (type == "script:create") {
        auto entity = static_cast<Entity>(message.value("id", 0u));
        if (!playing_ && registry_.valid(entity)) {
            CreateScriptFor(entity, message.value("name", ""));
        }
    } else if (type == "script:open") {
        // Opens in whatever the user has associated with .lua files.
        std::filesystem::path path = assets_.GetProject().Resolve(message.value("path", ""));
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(mainWindow_, L"open", path.c_str(), nullptr, nullptr,
                                                    SW_SHOWNORMAL)) <= 32) {
            ShellExecuteW(mainWindow_, L"open", L"notepad.exe", path.c_str(), nullptr, SW_SHOWNORMAL);
        }
    } else if (type == "tool:set") {
        // The prefab brush carries its prefab; other tools keep the last one
        // so switching back restores it.
        if (message.contains("prefab")) {
            brushPrefab_ = message.value("prefab", "");
        }
        if (message.contains("scale") && message["scale"].is_number()) {
            brushScale_ = std::clamp(message["scale"].get<float>(), 0.1f, 10.0f);
        }
        if (message.contains("autotile") && message["autotile"].is_boolean()) {
            autotile_ = message["autotile"].get<bool>();
            editorStateStale_ = true;
        }
        if (message.contains("heightMode") && message["heightMode"].is_string()) {
            const std::string mode = message["heightMode"].get<std::string>();
            if (mode == "raise" || mode == "lower" || mode == "flatten") heightMode_ = mode;
            editorStateStale_ = true;
        }
        if (message.contains("heightRadius") && message["heightRadius"].is_number()) {
            heightRadius_ = std::clamp(message["heightRadius"].get<int>(), 0, 12);
            editorStateStale_ = true;
        }
        std::string id = message.value("tool", "");
        for (int i = 0; i < static_cast<int>(std::size(kToolIds)); ++i) {
            if (id == kToolIds[i]) {
                SetTool(static_cast<Tool>(i));
            }
        }
    } else if (type == "ai:setEnabled") {
        SetAiEnabled(message.value("enabled", true));
    } else if (type == "ai:setProvider") {
        if (userPrefs_) {
            userPrefs_->Set("aiProvider", message.value("provider", "mistral"));
            userPrefs_->Set("aiModel", message.value("model", ""));
        }
        // A new model: the service starts again with it (and says so once it's up).
        providerStatus_ = nlohmann::json::object();
        providerRequest_ = 0;
        if (aiConnection_) aiConnection_->Restart();
        post_(AiStatusJson());
    } else if (type == "ai:setArtProvider") {
        // What draws the prefabs' art: auto (follows the LLM), mistral, pixellab or bfl:<model>. Applied at once,
        // unless the project has its own (Project Settings ▸ AI).
        if (userPrefs_) userPrefs_->Set("artProvider", message.value("provider", "auto"));
        SendProvider();
        post_(AiStatusJson());
    } else if (type == "ai:setBackgroundProvider") {
        // What paints backgrounds (Preferences ▸ AI ▸ Background). Applied at once, unless the project has its own.
        if (userPrefs_) userPrefs_->Set("backgroundProvider", message.value("provider", "bfl:flux-3-image"));
        SendProvider();
        post_(AiStatusJson());
    } else if (type == "ai:listModels") {
        // The models the provider offers (its own list), for Preferences ▸ AI ▸ Model.
        const std::string provider = message.value("provider", "mistral");
        if (Ai() && Ai()->GetState() == AiClient::State::Ready && (provider == "mistral" || provider == "claude")) {
            modelsRequest_ = Ai()->Request("GET", "/models?provider=" + provider, nlohmann::json::object(), 30);
        } else {
            post_({{"type", "ai:models"}, {"provider", provider}, {"models", nlohmann::json::array()}, {"errors", {"the AI service isn't running"}}});
        }
    } else if (type == "ai:setReviewPolicy") {
        if (userPrefs_) {
            userPrefs_->Set("reviewPolicy", ReviewPolicyName(ParseReviewPolicy(message.value("policy", ""))));
        }
        post_(AiStatusJson());
    } else if (type == "world:request") {
        world_.Refresh();
        post_(WorldJson());
    } else if (type == "world:save") {
        // The panel sends the whole entry; a changed kind or id is a rename.
        const nlohmann::json& in = message["entry"];
        WorldEntry entry;
        entry.kind = in.value("kind", "");
        entry.id = in.value("id", "");
        entry.name = in.value("name", "");
        if (entry.id.empty()) {
            entry.id = WorldIdFromName(entry.name);
        }
        entry.summary = in.value("summary", "");
        entry.body = in.value("body", "");
        for (const auto& tag : in.value("tags", nlohmann::json::array())) {
            entry.tags.push_back(tag.get<std::string>());
        }
        for (const auto& relation : in.value("relations", nlohmann::json::array())) {
            entry.relations.push_back({relation.value("type", ""), relation.value("target", "")});
        }
        if (entry.kind == "location") entry.scene = in.value("scene", "");
        world_.Refresh();
        const std::string original = message.value("originalKey", "");
        std::optional<std::string> error;
        if (!original.empty() && original != entry.Key()) {
            error = world_.Rename(original, entry.Key());
        }
        if (const WorldEntry* existing = world_.Find(entry.Key()); existing && !error) {
            entry.extra = existing->extra;
        }
        if (!error) {
            error = world_.Save(entry);
        }
        post_({{"type", "world:saved"},
               {"key", entry.Key()},
               {"error", error ? nlohmann::json(*error) : nlohmann::json(nullptr)}});
        post_(WorldJson());
    } else if (type == "input:request" || type == "input:reset" || type == "input:save") {
        // Project Settings ▸ Input (12.3): the project's actions and bindings, input.json.
        const auto root = assets_.GetProject().root;
        std::vector<std::string> errors;
        std::optional<std::string> error;
        if (type == "input:reset") {
            ActiveInputMap() = InputMap::Defaults();
            if (!SaveInputMap(root, ActiveInputMap())) error = "can't write input.json";
        } else if (type == "input:save") {
            InputMap map = InputMap::FromJson(message.value("map", nlohmann::json::object()), &errors);
            if (errors.empty()) {
                ActiveInputMap() = map;
                if (!SaveInputMap(root, map)) error = "can't write input.json";
            } else {
                error = errors.front();
            }
        } else {
            ActiveInputMap() = LoadInputMap(root, &errors);
        }
        nlohmann::json keys = nlohmann::json::array();
        for (int i = 1; i < static_cast<int>(Key::Count); ++i) keys.push_back(KeyName(static_cast<Key>(i)));
        nlohmann::json axes = nlohmann::json::array();
        for (int i = 0; i <= static_cast<int>(PadAxis::RightTrigger); ++i) axes.push_back(PadAxisName(static_cast<PadAxis>(i)));
        if (type == "input:save") {
            post_({{"type", "input:saved"}, {"error", error ? nlohmann::json(*error) : nlohmann::json(nullptr)}});
        }
        post_({{"type", "input:data"}, {"map", ActiveInputMap().ToJson()}, {"keys", keys}, {"axes", axes}, {"errors", errors}});
    } else if (type == "records:request") {
        ActiveRecords().Refresh();
        post_(RecordsJson());
    } else if (type == "records:save") {
        // The panel sends the whole record; a changed id is a rename (references follow).
        const std::string recordType = message.value("recordType", "");
        const std::string id = message.value("id", "");
        const std::string original = message.value("originalId", "");
        ActiveRecords().Refresh();
        std::optional<std::string> error;
        if (!original.empty() && original != id) {
            error = ActiveRecords().Rename(recordType, original, id);
        }
        if (!error) {
            error = ActiveRecords().Write(recordType, id, message.value("data", nlohmann::json::object()));
        }
        post_({{"type", "records:saved"},
               {"recordType", recordType},
               {"id", id},
               {"error", error ? nlohmann::json(*error) : nlohmann::json(nullptr)}});
        post_(RecordsJson());
    } else if (type == "records:delete") {
        ActiveRecords().Refresh();
        if (auto error = ActiveRecords().Remove(message.value("recordType", ""), message.value("id", ""))) {
            post_({{"type", "notify"}, {"text", *error}});
        }
        post_(RecordsJson());
    } else if (type == "world:delete") {
        world_.Refresh();
        if (auto error = world_.Remove(message.value("key", ""))) {
            post_({{"type", "notify"}, {"text", *error}});
        }
        post_(WorldJson());
    } else if (type == "ai:history") {
        post_({{"type", "ai:restore"}, {"events", transcript_.RestoreEvents()}});
    } else if (type == "ai:clear") {
        transcript_.Clear();
        conversation_.clear();
        post_({{"type", "ai:restore"}, {"events", nlohmann::json::array()}});
    } else if (type == "art:clearHistory") {
        artHistory_.Clear();
        post_(ArtJson());
    } else if (type == "art:request") {
        post_(ArtJson());
    } else if (type == "art:generate") {
        // Takes a few seconds per image, on the AI client's worker thread.
        if (Ai() == nullptr || Ai()->GetState() != AiClient::State::Ready) {
            post_({{"type", "art:result"}, {"result", {{"ok", false}, {"errors", {"AI is off"}}}}});
        } else if (artRequest_ == 0) {
            const nlohmann::json request = message.value("request", nlohmann::json::object());
            artRequest_ = Ai()->Request("POST", "/assets/generate", request, 300);
            artHistoryId_ = artHistory_.Begin(request);
            post_(ArtJson());
        }
    } else if (type == "character:generate" || type == "animation:generate") {
        // PixelLab: a character from 4 or 8 directions, or an image animated. A minute or two.
        const bool character = type == "character:generate";
        const char* reply = character ? "character:result" : "animation:result";
        int& inFlight = character ? characterRequest_ : animationRequest_;
        if (Ai() == nullptr || Ai()->GetState() != AiClient::State::Ready) {
            post_({{"type", reply}, {"result", {{"ok", false}, {"errors", {"AI is off"}}}}});
        } else if (inFlight == 0) {
            inFlight = Ai()->Request("POST", character ? "/characters/generate" : "/animations/generate",
                                     message.value("request", nlohmann::json::object()), 420);
        }
    } else if (type == "art:discard") {
        // Only generated art, by its project-relative path.
        const std::string path = message.value("path", "");
        if (path.rfind("assets/textures/generated/", 0) == 0 && path.find("..") == std::string::npos) {
            std::error_code ec;
            for (const char* suffix : {"", ".gen.json", ".meta"}) {
                std::filesystem::remove(assets_.GetProject().Resolve(path + suffix), ec);
            }
            artHistory_.Discarded(path);
            RefreshAssets();
        }
        post_(ArtJson());
    } else if (type == "level:defaults") {
        // A free area to suggest: just east of everything already in the scene.
        int maxX = 0, minY = 0;
        bool any = false;
        for (auto [entity, tile] : registry_.view<Tile>().each()) {
            maxX = any ? std::max(maxX, tile.gx) : tile.gx;
            minY = any ? std::min(minY, tile.gy) : tile.gy;
            any = true;
        }
        std::vector<std::string> tiles, things;
        for (const auto& info : prefabs_.Prefabs()) {
            (info.isTile ? tiles : things).push_back(info.path);
        }
        post_({{"type", "level:defaults"},
               {"x0", any ? maxX + 4 : 0},
               {"y0", minY},
               {"tiles", tiles},
               {"things", things}});
    } else if (type == "level:preview") {
        LevelParams params = LevelParamsFromJson(message.value("args", nlohmann::json::object()));
        std::string error;
        auto layout = GenerateLevel(params, &error);
        post_({{"type", "level:preview"},
               {"text", layout ? DescribeLevel(params, *layout) : std::string()},
               {"error", error}});
    } else if (type == "level:generate") {
        // The same operation the AI proposes, applied as one undo step.
        nlohmann::json op = {{"op", "command"}, {"name", "level.generate"}, {"args", message.value("args", nlohmann::json::object())}};
        if (playing_) {
            return;
        }
        ApplyOutcome outcome = ApplyAccepted({op});
        post_({{"type", "notify"},
               {"text", outcome.errors.empty() ? "Level generated (Ctrl+Z undoes it)" : outcome.errors.front()}});
        if (outcome.errors.empty()) {
            FrameScene();
        }
    } else if (type == "village:preview") {
        // The Level tab's Village (6.5): the plan as review would show it.
        const VillageRequest request = VillageRequestFromJson(message.value("args", nlohmann::json::object()));
        std::string error;
        auto plan = PlanVillage(prefabs_, assets_.GetProject(), request, &error);
        post_({{"type", "village:preview"}, {"text", plan ? DescribeVillage(*plan) : std::string()}, {"error", error}});
    } else if (type == "village:generate") {
        nlohmann::json op = {{"op", "command"}, {"name", "level.village"}, {"args", message.value("args", nlohmann::json::object())}};
        if (playing_) {
            return;
        }
        ApplyOutcome outcome = ApplyAccepted({op});
        post_({{"type", "notify"},
               {"text", outcome.errors.empty() ? "Village built (Ctrl+Z undoes it)" : outcome.errors.front()}});
        if (outcome.errors.empty()) {
            FrameScene();
        }
    } else if (type == "story:request") {
        post_(StoryJson());
    } else if (type == "story:save") {
        // {kind: dialogue|quest, id, originalId, content}: a changed id is a rename.
        const std::string kind = message.value("kind", "");
        const std::string folder = kind == "quest" ? "quests" : kind == "schedule" ? "schedules" : "dialogue";
        auto error = SaveStoryFile(folder, message.value("id", ""), message.value("originalId", ""),
                                   message.value("content", nlohmann::json::object()));
        post_({{"type", "story:saved"},
               {"file", folder + "/" + message.value("id", "") + ".json"},
               {"error", error ? nlohmann::json(*error) : nlohmann::json(nullptr)}});
        post_(StoryJson());
    } else if (type == "story:delete") {
        const std::string file = message.value("file", "");
        std::error_code ec;
        if ((file.rfind("dialogue/", 0) == 0 || file.rfind("quests/", 0) == 0 || file.rfind("schedules/", 0) == 0) &&
            file.find("..") == std::string::npos) {
            std::filesystem::remove(assets_.GetProject().Resolve(file), ec);
        }
        post_(StoryJson());
    } else if (type == "world:check") {
        // "Check against canon" for a hand-written entry: as if it were being
        // written now, compared with the rest of the World Bible.
        world_.Refresh();
        const WorldEntry* entry = world_.Find(message.value("key", ""));
        if (entry == nullptr || Ai() == nullptr || Ai()->GetState() != AiClient::State::Ready) {
            post_({{"type", "world:checkResult"},
                   {"key", message.value("key", "")},
                   {"result", {{"ok", false}, {"errors", {"AI isn't available"}}}}});
        } else {
            std::string relations;
            for (const auto& relation : entry->relations) {
                relations += (relations.empty() ? "" : "; ") + relation.type + " " + relation.target;
            }
            std::string tags;
            for (const auto& tag : entry->tags) {
                tags += (tags.empty() ? "" : ", ") + tag;
            }
            nlohmann::json op = {{"op", "command"},
                                 {"name", "world.write"},
                                 {"args",
                                  {{"kind", entry->kind},
                                   {"id", entry->id},
                                   {"name", entry->name},
                                   {"summary", entry->summary},
                                   {"tags", tags},
                                   {"relations", relations},
                                   {"body", entry->body}}}};
            worldCheckKey_ = entry->Key();
            worldCheckRequest_ = Ai()->Request("POST", "/consistency",
                                               {{"operations", nlohmann::json::array({op})}, {"exclude", {entry->Key()}}},
                                               90);
        }
    } else if (type == "world:search") {
        if (Ai() != nullptr && Ai()->GetState() == AiClient::State::Ready) {
            worldSearchRequest_ =
                Ai()->Request("POST", "/world/search", {{"query", message.value("query", "")}, {"k", 8}});
        } else {
            post_({{"type", "world:searchResults"}, {"result", {{"ok", false}}}});
        }
    } else if (type == "ai:taskStop") {
        StopTask(message.value("id", 0), "Stopped. Nothing was changed.");
    } else if (type == "ai:undoAuto") {
        UndoAutoApplied(message.value("id", 0));
    } else if (type == "ai:undoApplied") {
        UndoProposal(message.value("id", 0));
    } else if (type == "ai:reapply") {
        ReapplyProposal(message.value("id", 0));
    } else if (type == "ai:send") {
        SendToAi(message.value("text", ""), message.value("focus", ""), message.value("asChange", false));
    } else if (type == "ai:apply") {
        ApplyProposal(message.value("id", 0), message.value("items", nlohmann::json::array()));
    } else if (type == "ai:reject") {
        RejectProposal(message.value("id", 0));
    } else if (type == "settings:request") {
        post_(SettingsJson());
    } else if (type == "settings:set") {
        std::string sectionName = message.value("section", "");
        std::string fieldName = message.value("field", "");
        std::optional<std::string> error = SetSetting(sectionName, fieldName, message.value("value", nlohmann::json()));
        // The project's AI changed: the service told at once (a new LLM swaps live, no restart).
        if (!error && sectionName == "AI") {
            SendProvider();
            post_(AiStatusJson());
        }
        post_({{"type", "inspector:fieldError"},
               {"component", sectionName},
               {"field", fieldName},
               {"error", error ? nlohmann::json(*error) : nlohmann::json(nullptr)}});
        post_(SettingsJson());
    } else if (type == "prefabs:request") {
        post_(PrefabsJson());
        // Sent once the UI is up: say what the project's modules (4.8) couldn't load.
        static bool moduleErrorsShown = false;
        if (!moduleErrorsShown && !LastModuleLoad().errors.empty()) {
            moduleErrorsShown = true;
            const auto& errors = LastModuleLoad().errors;
            post_({{"type", "notify"},
                   {"text", "Module problem: " + errors.front() +
                                  (errors.size() > 1 ? " (+" + std::to_string(errors.size() - 1) + " more in the console)" : "")}});
        }
    } else if (type == "prefab:createFromAsset") {
        CreatePrefabFromAsset(message.value("asset", ""), message.value("tile", 0), message.value("asTile", false),
                              message.value("solid", false), message.value("name", ""), message.value("flat", false));
    } else if (type == "edit:rotate") {
        Rotate(message.value("direction", 1) < 0 ? -1 : 1);
    } else if (type == "prefab:setFlat") {
        SetPrefabFlat(message.value("path", ""), message.value("flat", true));
    } else if (type == "prefab:saveSelected") {
        SaveSelectedAsPrefab(message.value("name", ""));
    } else if (type == "prefab:applySelected") {
        ApplySelectedToPrefab();
    } else if (type == "prefab:revert") {
        RevertOverride(message.value("key", ""));
    } else if (type == "script:setProperty" || type == "script:resetProperty") {
        auto entity = static_cast<Entity>(message.value("id", 0u));
        if (registry_.valid(entity) && registry_.all_of<LuaScript>(entity)) {
            if (type == "script:setProperty" && message.contains("value")) {
                SetScriptProperty(entity, message.value("name", ""), message["value"]);
            } else {
                ResetScriptProperty(entity, message.value("name", ""));
            }
        }
    } else if (type == "assets:request") {
        post_(AssetsJson());
    } else if (type == "catalog:request") {
        post_(CatalogJson());
    } else if (type == "catalog:set") {
        std::map<std::string, std::string> fields;
        const auto fieldsSent = message.value("fields", nlohmann::json::object());
        for (const auto& [key, value] : fieldsSent.items()) {
            if (value.is_string()) fields[key] = value.get<std::string>();
        }
        if (SetCatalogFields(assets_, message.value("id", ""), fields)) {
            post_(CatalogJson());
            post_(AssetsJson());
        }
    } else if (type == "assets:analyze") {
        AnalyzeAssets(message.value("paths", std::vector<std::string>{}));
    } else if (type == "pack:plan") {
        PostPackPlan(message.value("paths", std::vector<std::string>{}));
    } else if (type == "pack:setScale") {
        const std::string folder = message.value("folder", "");
        if (folder.rfind("assets/", 0) == 0 && folder.find("..") == std::string::npos) {
            WritePackScale(assets_.GetProject(), folder, message.value("pixelsPerCell", 0.0f));
        }
        PostPackPlan(packPaths_);
    } else if (type == "pack:import") {
        ImportPack(message.value("plans", nlohmann::json::array()));
    } else if (type == "assets:applyNames") {
        ApplySuggestedNames(message.value("paths", std::vector<std::string>{}));
    } else if (type == "assets:refresh") {
        RefreshAssets();
        RefreshPrefabs();
    } else if (type == "asset:import") {
        ImportAssets();
    } else if (type == "asset:settings") {
        std::string path = message.value("path", "");
        AssetType assetType = message.value("assetType", "texture") == "tileset" ? AssetType::Tileset
                                                                                 : AssetType::Texture;
        if (assets_.SetImageSettings(path, assetType, message.value("tileWidth", 0u),
                                     message.value("tileHeight", 0u))) {
            post_(AssetsJson());
        } else {
            post_({{"type", "notify"}, {"text", "Couldn't change settings for " + path}});
        }
    } else if (type == "asset:apply") {
        ApplyAssetToSelected(message.value("path", ""), message.value("tile", 0));
    } else if (type == "asset:preview") {
        std::string path = message.value("path", "");
        if (!audio_.Play(assets_.GetProject().Resolve(path))) {
            post_({{"type", "notify"}, {"text", audio_.IsAvailable() ? "Couldn't play " + path
                                                                     : "No audio device available"}});
        }
    } else if (type == "edit:undo") {
        Undo();
    } else if (type == "edit:redo") {
        Redo();
    } else if (type == "web:search") {
        // The Assets panel's Web Search: ranked results with previews, nothing downloaded yet.
        if (!Ai() || Ai()->GetState() != AiClient::State::Ready) {
            post_({{"type", "web:results"}, {"ok", false}, {"errors", {"the AI service isn't running (Preferences ▸ AI)"}}});
            return;
        }
        const SourcingSettings settings = ReadSourcingSettings(assets_.GetProject().root);
        webPreviewRequest_ = Ai()->Request("POST", "/assets/web-preview",
                                           {{"query", message.value("query", "")}, {"maxResults", 18}, {"shareAlike", settings.licences.shareAlike}}, 180);
        post_({{"type", "web:searching"}, {"query", message.value("query", "")}});
    } else if (type == "web:add") {
        // One result into the project (assets/web/<folder>), described, and into the user's library.
        if (!Ai() || Ai()->GetState() != AiClient::State::Ready) return;
        const nlohmann::json result = message.value("result", nlohmann::json::object());
        std::string folder = message.value("folder", "");
        if (folder.empty() || !Project::IsInside(folder) || folder.rfind("assets/", 0) != 0) folder = "assets/web/search";
        const int id = Ai()->Request("POST", "/assets/web-fetch", {{"url", result.value("file", "")}, {"folder", folder}}, 120);
        webFetchRequests_[id] = {{"result", result}, {"libraryFolder", message.value("libraryFolder", "")}};
    } else if (type == "scene:replacePrefab") {
        // Every copy of a prefab, replaced (Edit ▸ Replace prefab…): the same command the AI uses.
        if (playing_) return;
        const bool tile = message.value("tile", false);
        const std::string command = tile ? "terrain.replace" : "prefab.replace";
        std::vector<FieldValue> values = {message.value("from", std::string()), message.value("to", std::string())};
        if (!tile) values.push_back(message.value("orient", std::string("keep")));
        std::vector<TileLayerEdit> layerEdits;
        CommandResult result;
        ApplyOneShotEdit([&] {
            CommandContext context{registry_, &prefabs_, &assets_.GetProject()};
            context.layerEdits = &layerEdits;
            result = EngineReflection().Run(context, command, values);
            return result.ok;
        });
        if (!layerEdits.empty()) history_.PushCommand(std::make_unique<TileLayerCommand>(std::move(layerEdits), "Replace a ground tile"));
        post_({{"type", "notify"}, {"text", result.ok ? "Replaced every copy of " + message.value("from", std::string())
                                                      : "Couldn't replace it: " + result.error}});
        sceneTreeStale_ = true;
        editorStateStale_ = true;
        dirty_ = true;
    } else if (type == "edit:delete") {
        if (!selectedMany_.empty() || !selectedCells_.empty()) DeleteBoxSelection();
        else if (selectedCell_ && !selected_) EraseSelectedCell();
        else DeleteSelected();
    } else if (type == "edit:duplicate") {
        DuplicateSelected();
    } else if (type == "scene:save") {
        Save();
    } else if (type == "layout:request") {
        // The project's saved panel layout, or null for a project that has none
        // (the UI then keeps the layout last used anywhere).
        nlohmann::json saved = nullptr;
        std::ifstream in(LayoutPath());
        if (in) {
            saved = nlohmann::json::parse(in, nullptr, false);
            if (saved.is_discarded()) saved = nullptr;
        }
        layout_ = saved;
        savedLayout_ = saved;
        post_({{"type", "layout:state"}, {"layout", saved}});
    } else if (type == "layout:set") {
        layout_ = message.value("layout", nlohmann::json(nullptr));
        if (!layout_.is_null() && layout_ != savedLayout_ && !dirty_) {
            dirty_ = true; // a moved panel is an unsaved change, kept by Save
            editorStateStale_ = true;
        }
    } else if (type == "view:frame") {
        FrameScene();
    } else if (type == "play:start") {
        StartPlay();
    } else if (type == "play:stop") {
        StopPlay();
    }
}

// --- Scene editing polish (development_plan.md 4.3) -------------------------

void EditorApp::UpdatePreview() {
    const PrefabInfo* prefab = tool_ == Tool::Prefab ? prefabs_.Find(brushPrefab_) : nullptr;
    if (playing_ || !hasHover_ || dragging_ || prefab == nullptr || !prefab->error.empty()) {
        if (!previewKey_.empty()) {
            preview_.clear();
            previewKey_.clear();
        }
        return;
    }
    int gx = 0, gy = 0;
    CellAt(hoverX_, hoverY_, iso_, gx, gy);
    const std::string key = prefab->path + "|" + std::to_string(gx) + "|" + std::to_string(gy) + "|" +
                            std::to_string(brushScale_) + "|" + std::to_string(brushRotation_);
    if (key == previewKey_) {
        return;
    }
    previewKey_ = key;
    preview_.clear();
    auto ghost = prefabs_.Instantiate(preview_, prefab->path, static_cast<float>(gx) + 0.5f, static_cast<float>(gy) + 0.5f);
    if (!ghost) {
        return;
    }
    // As it would be placed: the brush's size and turn, then see-through.
    if (std::abs(brushScale_ - 1.0f) > 1e-3f) {
        ScaleEntity(EngineReflection(), preview_, *ghost, brushScale_);
    }
    if (auto* sprite = preview_.try_get<Sprite>(*ghost)) {
        if (sprite->flat) {
            sprite->rotation = (sprite->rotation + brushRotation_) % 4;
        } else if (brushRotation_ % 2 == 1) {
            std::swap(sprite->u0, sprite->u1); // upright: mirrored
        }
        sprite->a *= 0.55f;
    }
    UpdateWorldToScreen(preview_, iso_);
}

void EditorApp::UpdateCursor() {
    using Cursor = ViewportWindow::Cursor;
    Cursor cursor = Cursor::Arrow;
    if (panning_) {
        cursor = Cursor::Pan;
    } else if (!playing_) {
        switch (tool_) {
            case Tool::Prefab:
                cursor = Cursor::Brush;
                break;
            case Tool::Erase:
                cursor = Cursor::Erase;
                break;
            case Tool::Height:
                cursor = Cursor::Brush;
                break;
            case Tool::Select:
                // The Select tool's hand; over something that can be picked up
                // and moved (not the ground itself): the move pointer.
                cursor = Cursor::Select;
                if (dragging_) {
                    cursor = Cursor::Move;
                } else if (hasHover_) {
                    for (auto [entity, t, s] : registry_.view<ScreenTransform2D, Sprite>(entt::exclude<Tile>).each()) {
                        if (std::abs(hoverX_ - t.x) <= s.width * 0.5f && std::abs(hoverY_ - t.y) <= s.height * 0.5f) {
                            cursor = Cursor::Move;
                            break;
                        }
                    }
                }
                break;
        }
    }
    viewport_.SetCursor(cursor);
}

TerrainSet EditorApp::TerrainTiles() {
    return BuildTerrainSet(prefabs_, [this](const std::string& asset) {
        const AssetInfo* info = assets_.Find(asset);
        return info ? info->analysis : std::map<std::string, std::string>{};
    });
}

void EditorApp::Rotate(int direction) {
    if (playing_) {
        return;
    }
    if (tool_ == Tool::Prefab) {
        const PrefabInfo* prefab = prefabs_.Find(brushPrefab_);
        const bool flat = prefab != nullptr && prefab->flat;
        // Upright brushes only have two states: as drawn and mirrored.
        brushRotation_ = flat ? (brushRotation_ + direction + 4) % 4 : (brushRotation_ % 2 == 1 ? 0 : 1);
        editorStateStale_ = true;
        post_({{"type", "notify"},
               {"text", flat ? "Brush turned to " + std::to_string(brushRotation_ * 90) + "°"
                             : std::string(brushRotation_ == 1 ? "Brush mirrored" : "Brush facing as drawn")}});
        return;
    }
    if (!selected_ || !registry_.valid(*selected_)) {
        post_({{"type", "notify"}, {"text", "Select something to turn first"}});
        return;
    }
    const Entity entity = *selected_;
    std::optional<std::string> problem;
    ApplyOneShotEdit([&] {
        problem = TurnEntity(EngineReflection(), registry_, entity, direction);
        return !problem.has_value();
    });
    if (problem) {
        post_({{"type", "notify"}, {"text", "Can't turn it: " + *problem}});
    }
    inspectorStale_ = true;
}

void EditorApp::SetPrefabFlat(const std::string& path, bool flat) {
    if (playing_ || prefabs_.Find(path) == nullptr) {
        return;
    }
    std::optional<std::string> error = prefabs_.SetTemplateField(path, "Sprite", "flat", flat);
    if (!error && flat) {
        // One cell's diamond; [ and ] still resize copies.
        error = prefabs_.SetTemplateField(path, "Sprite", "width", iso_.tileWidth);
        if (!error) error = prefabs_.SetTemplateField(path, "Sprite", "height", iso_.tileHeight);
    }
    if (error) {
        post_({{"type", "notify"}, {"text", "Couldn't change " + path + ": " + *error}});
        return;
    }
    RefreshPrefabs(); // every copy in the scene follows
    dirty_ = true;
    editorStateStale_ = true;
    post_({{"type", "notify"},
           {"text", path + (flat ? " now lies flat on the grid (R turns it)" : " now stands upright")}});
}

void EditorApp::ApplyBrushRotation(std::optional<Entity> placed) {
    if (!placed || brushRotation_ == 0 || !registry_.valid(*placed)) {
        return;
    }
    // Through the edit path, so a prefab copy keeps it as an override.
    TurnEntity(EngineReflection(), registry_, *placed, brushRotation_);
}

std::optional<float> EditorApp::SizePercent(Entity entity) {
    const auto* sprite = registry_.try_get<Sprite>(entity);
    const auto* link = registry_.try_get<PrefabInstance>(entity);
    const PrefabInfo* info = link ? prefabs_.Find(link->path) : nullptr;
    if (sprite == nullptr || info == nullptr || info->width <= 0.0f) {
        return std::nullopt;
    }
    return sprite->width / info->width * 100.0f;
}

void EditorApp::Resize(float factor) {
    if (playing_ || factor <= 0.0f) {
        return;
    }
    if (tool_ == Tool::Prefab && prefabs_.Find(brushPrefab_) != nullptr) {
        // The brush: the ghost shows the new size at once (UpdatePreview).
        brushScale_ = std::clamp(brushScale_ * factor, 0.1f, 10.0f);
        if (std::abs(brushScale_ - 1.0f) < 0.02f) {
            brushScale_ = 1.0f; // land on 100% exactly when passing it
        }
        editorStateStale_ = true;
        post_({{"type", "notify"},
               {"text", "Brush size " + std::to_string(static_cast<int>(std::lround(brushScale_ * 100.0f))) + "%"}});
        return;
    }
    ScaleSelected(factor);
}

void EditorApp::ScaleSelected(float factor) {
    if (playing_) {
        return;
    }
    if (!selected_ || !registry_.valid(*selected_)) {
        post_({{"type", "notify"}, {"text", "Select something to resize first"}});
        return;
    }
    std::optional<std::string> problem;
    const auto now = std::chrono::steady_clock::now();
    const Entity entity = *selected_;
    if (resizeRun_ == entity && now - resizeRunAt_ < std::chrono::milliseconds(800)) {
        // Still the same gesture: the undo step taken at its start covers it.
        problem = ScaleEntity(EngineReflection(), registry_, entity, factor);
        if (!problem) {
            dirty_ = true;
            editorStateStale_ = true;
        }
    } else {
        ApplyOneShotEdit([&] {
            problem = ScaleEntity(EngineReflection(), registry_, entity, factor);
            return !problem.has_value();
        });
        resizeRun_ = entity;
    }
    resizeRunAt_ = now;
    if (problem) {
        post_({{"type", "notify"}, {"text", *problem}});
    }
    inspectorStale_ = true;
}

void EditorApp::NudgeSelected(float dx, float dy) {
    if (playing_ || !selected_ || !registry_.valid(*selected_)) {
        return;
    }
    ApplyOneShotEdit([&] { return NudgeEntity(registry_, *selected_, dx, dy, iso_); });
    inspectorStale_ = true;
}

void EditorApp::CopySelected() {
    if (!selected_ || !registry_.valid(*selected_)) {
        return;
    }
    clipboard_ = CopyEntities(registry_, {*selected_});
    post_({{"type", "notify"}, {"text", "Copied — Ctrl+V pastes at the cursor"}});
}

void EditorApp::PasteAtCursor(std::optional<std::pair<int, int>> cell) {
    if (playing_ || clipboard_.empty()) {
        return;
    }
    int gx = 0, gy = 0;
    if (cell) {
        gx = cell->first;
        gy = cell->second;
    } else if (hasHover_) {
        CellAt(hoverX_, hoverY_, iso_, gx, gy);
    } else if (selected_ && registry_.valid(*selected_)) {
        if (const auto* world = registry_.try_get<WorldTransform2D>(*selected_)) {
            gx = static_cast<int>(std::floor(world->x)) + 1;
            gy = static_cast<int>(std::floor(world->y));
        }
    }
    std::vector<Entity> pasted;
    ApplyOneShotEdit([&] {
        pasted = PasteEntities(registry_, clipboard_, gx, gy);
        return !pasted.empty();
    });
    if (!pasted.empty()) {
        Select(pasted.front());
    }
    sceneTreeStale_ = true;
}

void EditorApp::RemovePrefab(const std::string& path) {
    if (playing_) {
        return;
    }
    // The scene side (copies unlinked) is one undo step; the file is gone.
    std::variant<int, std::string> result;
    ApplyOneShotEdit([&] {
        result = prefabs_.Remove(registry_, path);
        return std::holds_alternative<int>(result) && std::get<int>(result) > 0;
    });
    if (auto* error = std::get_if<std::string>(&result)) {
        post_({{"type", "notify"}, {"text", *error}});
        return;
    }
    if (brushPrefab_ == path) {
        brushPrefab_.clear();
        SetTool(Tool::Select);
    }
    post_(PrefabsJson());
    sceneTreeStale_ = true;
    inspectorStale_ = true;
    post_({{"type", "notify"},
           {"text", "Deleted " + path + " (" + std::to_string(std::get<int>(result)) + " copies kept as ordinary entities)"}});
}

bool EditorApp::MoveProjectItem(const std::string& from, const std::string& to, bool report) {
    if (playing_) {
        post_({{"type", "notify"}, {"text", "Stop playing first"}});
        return false;
    }
    MoveResult result = MoveProjectFile(assets_.GetProject(), from, to);
    post_({{"type", "file:moved"}, {"from", from}, {"to", to}, {"error", result.error}});
    if (!result.error.empty()) {
        if (report) {
            post_({{"type", "notify"}, {"text", "Couldn't move " + from + ": " + result.error}});
        }
        return false;
    }
    // The open scene and its undo history follow the file too; neither is
    // an undo step of its own (the file has moved for good).
    auto rewrite = [&](const std::string& text) { return RewriteSceneText(text, from, to); };
    RewriteScene(registry_, rewrite);
    history_.RewriteSnapshots(rewrite);
    if (brushPrefab_ == from) {
        brushPrefab_ = to;
    }
    artHistory_.Moved(from, to);
    if (OpenSceneRelative() == from) {
        scenePath_ = assets_.GetProject().Resolve(to).string();
        UpdateTitle();
    }
    if (ActiveSettings().game.startScene == from) {
        ActiveSettings().game.startScene = to; // the file was rewritten; keep memory in step
    }
    renderer_.InvalidateTexture(from);
    if (!report) {
        return true;
    }
    RefreshAfterMoves();
    post_({{"type", "notify"},
           {"text", "Moved to " + to + " — " + std::to_string(result.references) + " reference(s) in " +
                        std::to_string(result.rewritten.size()) + " file(s) updated"}});
    return true;
}

void EditorApp::RefreshAfterMoves() {
    RefreshAssets();
    RefreshPrefabs();
    world_.Refresh();
    post_(WorldJson());
    post_(StoryJson());
    sceneTreeStale_ = true;
    inspectorStale_ = true;
    editorStateStale_ = true;
}

namespace {

bool IsOrganizedFolder(const std::string& folder) {
    // Inside prefabs/ or assets/, never the roots themselves, no tricks.
    return (folder.rfind("prefabs/", 0) == 0 || folder.rfind("assets/", 0) == 0) &&
           folder.find("..") == std::string::npos && folder.back() != '/';
}

} // namespace

void EditorApp::RenameFolder(const std::string& folder, const std::string& newFolder) {
    if (playing_ || !IsOrganizedFolder(folder) || !IsOrganizedFolder(newFolder) || folder == newFolder) {
        return;
    }
    const Project& project = assets_.GetProject();
    int moved = 0;
    std::vector<std::string> failed;
    for (const std::string& item : FolderContents(project, folder)) {
        if (MoveProjectItem(item, PathAfterFolderChange(item, folder, newFolder), false)) {
            ++moved;
        } else {
            failed.push_back(item);
        }
    }
    RemoveEmptyFolders(project, folder);
    RefreshAfterMoves();
    post_({{"type", "notify"},
           {"text", "Renamed to " + newFolder + " (" + std::to_string(moved) + " moved, references updated" +
                        (failed.empty() ? ")" : "; " + std::to_string(failed.size()) + " couldn't move: " + failed.front() + ")")}});
}

void EditorApp::RemoveFolder(const std::string& folder, bool deleteContents) {
    if (playing_ || !IsOrganizedFolder(folder)) {
        return;
    }
    const Project& project = assets_.GetProject();
    const std::vector<std::string> items = FolderContents(project, folder);
    int done = 0;
    std::vector<std::string> failed;
    if (deleteContents) {
        const bool prefabs = folder.rfind("prefabs/", 0) == 0;
        ApplyOneShotEdit([&] { // prefab copies in the scene are unlinked: one undo step
            for (const std::string& item : items) {
                if (prefabs) {
                    if (std::holds_alternative<int>(prefabs_.Remove(registry_, item))) {
                        ++done;
                    } else {
                        failed.push_back(item);
                    }
                    if (brushPrefab_ == item) brushPrefab_.clear();
                } else {
                    std::error_code ec;
                    for (const char* suffix : {"", ".meta", ".gen.json"}) {
                        std::filesystem::remove(project.Resolve(item + suffix), ec);
                    }
                    artHistory_.Discarded(item);
                    ++done;
                }
            }
            return prefabs && done > 0;
        });
    } else {
        for (const std::string& item : items) {
            if (MoveProjectItem(item, PathAfterFolderChange(item, folder, ""), false)) {
                ++done;
            } else {
                failed.push_back(item);
            }
        }
    }
    const bool gone = RemoveEmptyFolders(project, folder);
    RefreshAfterMoves();
    std::string text = deleteContents ? "Removed " + folder + " and " + std::to_string(done) + " item(s) in it"
                                      : "Removed " + folder + "; " + std::to_string(done) + " item(s) moved up a level";
    if (!failed.empty()) {
        text += "; " + std::to_string(failed.size()) + " couldn't (" + failed.front() + ")";
    }
    if (!gone) {
        text += "; the folder still holds other files, so it stays";
    }
    post_({{"type", "notify"}, {"text", text}});
}

// --- Asset analysis (development_plan.md 6.2) ---------------------------------

void EditorApp::AnalyzeAssets(const std::vector<std::string>& paths) {
    std::vector<std::string> images;
    for (const std::string& path : paths) {
        const AssetInfo* asset = assets_.Find(path);
        if (asset != nullptr && asset->type != AssetType::Audio && asset->error.empty()) {
            images.push_back(path);
        }
    }
    if (images.empty()) {
        post_({{"type", "notify"}, {"text", "Pick images to analyze"}});
        return;
    }
    // Border terrains and shapes (footprint, anchor, sheets cut into objects)
    // need no AI: right away.
    AnalyzeTileEdges(images);
    for (const auto& path : images) AnalyzeArtPixels(assets_, path);
    // Descriptions come from the AI service's vision model.
    if (Ai() == nullptr || Ai()->GetState() != AiClient::State::Ready) {
        post_({{"type", "notify"}, {"text", "Shapes and tile borders analyzed. Turn AI on to have the images described too."}});
        post_(AssetsJson());
        post_(CatalogJson());
        return;
    }
    if (analyzeRequest_ != 0) {
        post_({{"type", "notify"}, {"text", "Still analyzing the previous images"}});
        return;
    }
    // A sheet is described object by object; the model picks each one's role
    // from the ontology's.
    std::vector<std::string> whole;
    nlohmann::json items = nlohmann::json::array();
    for (const auto& path : images) {
        const AssetInfo* asset = assets_.Find(path);
        const auto objects = asset ? nlohmann::json::parse(asset->analysis.contains(kObjectsKey) ? asset->analysis.at(kObjectsKey) : "",
                                                           nullptr, false)
                                   : nlohmann::json();
        if (objects.is_array() && !objects.empty()) {
            for (size_t i = 0; i < objects.size(); ++i) {
                items.push_back({{"id", path + "#" + std::to_string(i)}, {"path", path}, {"rect", objects[i].value("rect", nlohmann::json())}});
            }
        } else {
            whole.push_back(path);
        }
    }
    std::vector<std::string> roles;
    for (const auto& [id, role] : Places().Roles()) roles.push_back(id);
    analyzeRequest_ = Ai()->Request("POST", "/assets/describe", {{"paths", whole}, {"items", items}, {"roles", roles}}, 900);
    post_({{"type", "assets:analyzing"}, {"count", images.size()}});
    post_({{"type", "notify"},
           {"text", "Describing " + std::to_string(images.size()) + " image(s) — about a second each"}});
}

void EditorApp::AnalyzeTileEdges(const std::vector<std::string>& paths) {
    // Ground tiles: square, fully opaque at the border. One terrain clustering
    // per folder (an art pack's ground shares its terrains).
    std::map<std::string, std::vector<std::pair<std::string, BorderSamples>>> byFolder;
    for (const std::string& path : paths) {
        const AssetInfo* asset = assets_.Find(path);
        if (asset == nullptr || asset->type != AssetType::Texture || asset->width == 0 || asset->height == 0) continue;
        const float aspect = static_cast<float>(asset->width) / static_cast<float>(asset->height);
        if (aspect < 0.95f || aspect > 1.05f) continue;
        auto image = assets_.LoadTexture(path);
        if (!image) continue;
        BorderSamples samples = SampleBorder(*image);
        if (!samples.opaque) continue; // a prop with a transparent background
        byFolder[std::filesystem::path(path).parent_path().generic_string()].push_back({path, samples});
    }
    for (const auto& [folder, tiles] : byFolder) {
        std::vector<BorderSamples> samples;
        for (const auto& tile : tiles) samples.push_back(tile.second);
        const EdgeClassification result = ClassifyEdges(samples);
        for (size_t i = 0; i < tiles.size(); ++i) {
            std::map<std::string, std::string> fields{{"edges", FormatSignature(result.signatures[i])},
                                                      {"center", result.centers[i]}};
            const AssetInfo* asset = assets_.Find(tiles[i].first);
            if (asset != nullptr && !asset->analysis.contains("kind")) fields["kind"] = "ground";
            assets_.SetAnalysis(tiles[i].first, fields);
        }
    }
}

void EditorApp::AnalyzeMissingTileEdges() {
    // Every tile image of a folder is re-read together, so its terrains are
    // named the same way across the folder (one clustering per folder).
    std::map<std::string, std::vector<std::string>> byFolder;
    std::set<std::string> missing;
    for (const PrefabInfo& info : prefabs_.Prefabs()) {
        if (!info.isTile || info.texture.empty()) continue;
        const std::string folder = std::filesystem::path(info.texture).parent_path().generic_string();
        auto& list = byFolder[folder];
        if (std::find(list.begin(), list.end(), info.texture) == list.end()) list.push_back(info.texture);
        const AssetInfo* asset = assets_.Find(info.texture);
        if (asset != nullptr && (!asset->analysis.contains("edges") || !asset->analysis.contains("center"))) {
            missing.insert(folder);
        }
    }
    std::vector<std::string> paths;
    for (const auto& folder : missing) {
        if (!edgeFoldersTried_.insert(folder).second) continue;
        paths.insert(paths.end(), byFolder[folder].begin(), byFolder[folder].end());
    }
    if (!paths.empty()) {
        AnalyzeTileEdges(paths);
        post_(AssetsJson());
    }
}

void EditorApp::PostPackPlan(const std::vector<std::string>& paths) {
    packPaths_ = paths;
    std::map<std::string, PackScale> scales;
    auto scaleFor = [&](const std::string& folder) {
        auto it = scales.find(folder);
        if (it == scales.end()) {
            it = scales.emplace(folder, PackScaleFor(assets_.GetProject(), assets_, folder, iso_)).first;
        }
        return it->second.pixelsPerCell;
    };
    const auto plans = PlanPackImport(assets_, paths, prefabs_, iso_, scaleFor);
    nlohmann::json list = nlohmann::json::array();
    for (const PrefabPlan& plan : plans) {
        list.push_back({{"asset", plan.asset},
                        {"path", plan.path},
                        {"name", plan.name},
                        {"kind", plan.kind},
                        {"tile", plan.tile},
                        {"solid", plan.solid},
                        {"cells", std::round(plan.width / iso_.tileWidth * 100.0f) / 100.0f},
                        {"aspect", plan.width > 0.0f ? plan.height / plan.width : 1.0f}});
    }
    nlohmann::json folders = nlohmann::json::array();
    for (const auto& [folder, scale] : scales) {
        folders.push_back({{"folder", folder}, {"pixelsPerCell", scale.pixelsPerCell}, {"source", scale.source}});
    }
    post_({{"type", "pack:plan"}, {"plans", list}, {"scales", folders}});
}

void EditorApp::ImportPack(const nlohmann::json& plans) {
    if (playing_) {
        return;
    }
    int made = 0;
    std::vector<std::string> failed;
    for (const auto& p : plans) {
        if (!p.is_object() || !p.value("include", true)) continue;
        PrefabPlan plan;
        plan.asset = p.value("asset", "");
        plan.tile = p.value("tile", false);
        plan.solid = p.value("solid", false);
        // The name may have been edited: same folder, the new name.
        const std::string planned = p.value("path", "");
        plan.name = PrefabSlug(p.value("name", ""));
        if (plan.name.empty() || plan.asset.empty()) continue;
        plan.path = std::filesystem::path(planned).parent_path().generic_string() + "/" + plan.name + ".prefab";
        if (plan.tile) {
            plan.width = iso_.tileWidth;
            plan.height = iso_.tileHeight;
        } else {
            plan.width = std::clamp(p.value("cells", 1.0f), 0.05f, 40.0f) * iso_.tileWidth;
            plan.height = plan.width * p.value("aspect", 1.0f);
        }
        if (std::string error = CreatePrefabFromPlan(prefabs_, plan, iso_); error.empty()) {
            ++made;
        } else {
            failed.push_back(plan.name + ": " + error);
        }
    }
    RefreshPrefabs();
    post_({{"type", "notify"},
           {"text", "Made " + std::to_string(made) + " prefab(s)" +
                        (failed.empty() ? "" : "; " + std::to_string(failed.size()) + " failed (" + failed.front() + ")")}});
}

void EditorApp::ApplySuggestedNames(const std::vector<std::string>& paths) {
    if (playing_) {
        return;
    }
    int renamed = 0;
    std::vector<std::string> failed;
    std::set<std::string> taken;
    for (const std::string& path : paths) {
        const AssetInfo* asset = assets_.Find(path);
        if (asset == nullptr || !asset->analysis.contains("name")) continue;
        const std::filesystem::path from(path);
        const std::string base = asset->analysis.at("name");
        // Two images can suggest the same name: number the later ones.
        std::string to;
        for (int n = 1; n < 100; ++n) {
            to = from.parent_path().generic_string() + "/" + base + (n > 1 ? "-" + std::to_string(n) : "") +
                 from.extension().string();
            std::error_code ec;
            if (to == path || (!taken.contains(to) && !std::filesystem::exists(assets_.GetProject().Resolve(to), ec))) break;
        }
        if (to == path) continue;
        taken.insert(to);
        if (MoveProjectItem(path, to, false)) {
            ++renamed;
        } else {
            failed.push_back(path);
        }
    }
    RefreshAfterMoves();
    post_({{"type", "notify"},
           {"text", "Renamed " + std::to_string(renamed) + " asset(s), references updated" +
                        (failed.empty() ? "" : "; " + std::to_string(failed.size()) + " couldn't be")}});
}

// --- Scenes (development_plan.md 5.3) -----------------------------------------

std::string EditorApp::OpenSceneRelative() const {
    return assets_.GetProject().Relative(std::filesystem::path(scenePath_));
}

nlohmann::json EditorApp::ScenesJson() {
    const Project& project = assets_.GetProject();
    nlohmann::json scenes = nlohmann::json::array();
    world_.Refresh();
    for (const std::string& scene : ListScenes(project)) {
        nlohmann::json entry = {{"path", scene},
                                {"name", std::filesystem::path(scene).stem().string()},
                                {"start", scene == StartScene()},
                                {"open", scene == OpenSceneRelative()},
                                {"markers", SceneMarkers(project, scene)},
                                {"location", nullptr}};
        // The World Bible place this scene shows (6.9).
        if (const WorldEntry* place = world_.LocationOf(scene)) {
            entry["location"] = {{"key", place->Key()}, {"name", place->name}, {"summary", place->summary}};
        }
        scenes.push_back(entry);
    }
    return {{"type", "scenes:list"}, {"scenes", scenes}, {"problems", GatewayProblems(project)}};
}

bool EditorApp::AskToSaveChanges(const wchar_t* question) {
    if (!IsDirty()) {
        return true;
    }
    int answer = MessageBoxW(mainWindow_, question, L"Unsaved changes", MB_YESNOCANCEL | MB_ICONWARNING);
    if (answer == IDCANCEL) {
        return false;
    }
    if (answer == IDYES) {
        Save();
        return !IsDirty(); // the save failed: stay
    }
    return true;
}

void EditorApp::OpenScene(const std::string& scene) {
    if (scene.empty() || scene == OpenSceneRelative()) {
        return;
    }
    if (playing_) {
        StopPlay();
    }
    if (!AskToSaveChanges(L"Save changes to this scene before opening another?")) {
        return;
    }
    const std::string path = assets_.GetProject().Resolve(scene).string();
    Registry loaded;
    if (!LoadScene(loaded, path)) {
        post_({{"type", "notify"}, {"text", "Couldn't open " + scene}});
        return;
    }
    LoadScene(registry_, path);
    scenePath_ = path;
    prefabs_.SyncAll(registry_); // prefab edits since it was saved
    history_.Clear();
    // Proposals applied to the scene we left can't be undone from here.
    for (auto& [id, pending] : proposals_) {
        if (pending.historyStep == 0) continue;
        pending.historyStep = 0;
        post_({{"type", "ai:applyState"}, {"id", id}, {"state", "settled"}, {"canUndo", false}, {"canApply", false}});
    }
    Select(std::nullopt);
    dirty_ = false;
    UpdateTitle();
    UpdateWorldToScreen(registry_, iso_);
    FrameScene();
    sceneTreeStale_ = true;
    inspectorStale_ = true;
    editorStateStale_ = true;
    post_(ScenesJson());
}

void EditorApp::PlayLayersFor(const std::string& scene) {
    if (scene == OpenSceneRelative()) {
        LayersOf(registry_) = playLayers_;
    } else {
        LoadTileLayers(registry_, assets_.GetProject().Resolve(scene));
    }
}

void EditorApp::HandleTravel() {
    auto travel = session_.TakeTravel();
    if (!travel) {
        return;
    }
    // The scene being edited plays as it is now; others as saved.
    std::string text;
    if (travel->scene == OpenSceneRelative()) {
        text = playSnapshot_;
    } else {
        std::ifstream in(assets_.GetProject().Resolve(travel->scene), std::ios::binary);
        if (in) {
            std::ostringstream buffer;
            buffer << in.rdbuf();
            text = buffer.str();
        }
    }
    if (text.empty()) {
        post_({{"type", "notify"}, {"text", "The gateway leads to " + travel->scene + ", which doesn't exist"}});
        session_.OnSceneChanged();
        return;
    }
    Select(std::nullopt);
    CapturePlayWorld(); // the scene left behind is remembered as it is (12.5)
    const std::string warning = TravelToScene(registry_, text, travel->marker);
    PlayLayersFor(travel->scene);
    prefabs_.SyncAll(registry_);
    ApplyPlayWorld(travel->scene);
    session_.OnSceneChanged();
    session_.SetScene(travel->scene);
    UpdateWorldToScreen(registry_, iso_);
    if (auto player = FindPlayer(registry_)) {
        int width = 0, height = 0;
        viewport_.GetFramebufferSize(width, height);
        cameraRig_.MeasureLevel(registry_, iso_); // the level's edges for the play camera (12.7)
        SnapCameraTo(camera_, registry_, *player, static_cast<float>(width), static_cast<float>(height));
    }
    sceneTreeStale_ = true;
    inspectorStale_ = true;
    post_({{"type", "notify"},
           {"text", "Now in " + travel->scene + (warning.empty() ? "" : " (" + warning + ")") +
                        " — Stop returns to the scene you're editing"}});
}

// --- Build & Export (development_plan.md 5.1) --------------------------------

namespace {

#ifndef AIGE_PLAYER_DIR
#define AIGE_PLAYER_DIR ""
#endif

// The player to ship: one installed next to the editor, else the build
// tree's, preferring a Release build. Returns the folder and its build name.
std::optional<std::pair<std::filesystem::path, std::string>> FindPlayerBuild() {
    std::vector<std::pair<std::filesystem::path, std::string>> candidates = {{ExecutableDir() / "player", "installed"}};
    const std::filesystem::path built = std::filesystem::u8path(AIGE_PLAYER_DIR);
    if (!built.empty()) {
        candidates.push_back({built.parent_path() / "Release", "Release"});
        candidates.push_back({built, built.filename().string()});
    }
    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(candidate.first / "sandbox.exe", ec)) return candidate;
    }
    return std::nullopt;
}

} // namespace

void EditorApp::ExportGameTo(const std::string& name, bool zip, bool debugConsole) {
    auto fail = [&](const std::string& error) {
        post_({{"type", "game:exported"}, {"ok", false}, {"error", error}});
    };
    if (playing_) {
        return fail("Stop playing first.");
    }
    auto player = FindPlayerBuild();
    if (!player) {
        return fail("No built player found (build the sandbox target).");
    }
    if (dirty_ || settingsDirty_) {
        Save(); // the export plays what's on screen
    }
    auto destination = ShowFolderDialog(mainWindow_, L"Export \"" + Utf8ToWide(name) + L"\" into…");
    if (!destination) {
        return; // cancelled
    }
    ExportOptions options;
    options.project = assets_.GetProject().root;
    options.playerDir = player->first;
    options.destination = *destination;
    options.name = name;
    options.zip = zip;
    options.version = ActiveSettings().game.version;
    options.debugConsole = debugConsole;
    options.startScene = StartScene();
    options.accessibility = AccessibilityReport(ProjectThemeTokens(assets_.GetProject().root), ActiveInputMap());
    ExportResult result = ExportGame(options);
    if (!result.error.empty()) {
        return fail(result.error);
    }
    post_({{"type", "game:exported"},
           {"ok", true},
           {"folder", WideToUtf8(result.folder.wstring())},
           {"zip", result.zip.empty() ? "" : WideToUtf8(result.zip.wstring())},
           {"files", result.files.size()},
           {"build", player->second},
           {"accessibility", options.accessibility}});
}

// --- Projects (development_plan.md 4.2) -----------------------------------

void EditorApp::RememberRecentProject() {
    if (!userPrefs_) {
        return;
    }
    const std::string current = WideToUtf8(std::filesystem::absolute(assets_.GetProject().root).wstring());
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
    gmtime_s(&utc, &now);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &utc);
    RememberProject(*userPrefs_, current, stamp);
}

nlohmann::json EditorApp::ProjectInfoJson() {
    nlohmann::json recent = userPrefs_ ? userPrefs_->Get("recentProjects", nlohmann::json::array())
                                       : nlohmann::json::array();
    return {{"type", "project:info"},
            {"current", WideToUtf8(std::filesystem::absolute(assets_.GetProject().root).wstring())},
            {"recent", recent},
            {"openLast", userPrefs_ ? userPrefs_->Get("openLastProject", false) : nlohmann::json(false)}};
}

void EditorApp::SwitchProject(const std::filesystem::path& root, bool askAboutChanges) {
    if (playing_) {
        StopPlay();
    }
    if (askAboutChanges && IsDirty()) {
        int answer = MessageBoxW(mainWindow_, L"Save changes to the scene before switching projects?",
                                 L"Unsaved changes", MB_YESNOCANCEL | MB_ICONWARNING);
        if (answer == IDCANCEL) {
            return;
        }
        if (answer == IDYES) {
            Save();
            if (IsDirty()) {
                return; // the save failed: stay
            }
        }
    }
    // A fresh editor process on the other project: every system (assets,
    // prefabs, AI service, undo) starts clean, exactly as when launched.
    if (!StartEditorProcess(L"--project \"" + std::filesystem::absolute(root).wstring() + L"\"")) {
        post_({{"type", "notify"}, {"text", "Couldn't start the editor for that project"}});
        return;
    }
    DestroyWindow(mainWindow_); // past WM_CLOSE: we've already asked
}

// --- Viewport input ---------------------------------------------------------

void EditorApp::ApplyOcclusion() {
    viewport_.SetOcclusion(LocalOcclusion(viewportBounds_, overlays_));
}

void EditorApp::OnViewportMouse(const ViewportMouseEvent& event) {
    using Type = ViewportMouseEvent::Type;
    using Button = ViewportMouseEvent::Button;

    float sceneX = 0.0f;
    float sceneY = 0.0f;
    camera_.ViewportToScene(event.x, event.y, sceneX, sceneY);

    switch (event.type) {
        case Type::Wheel: {
            // Alt held: resize instead of zoom. The physical key state, since
            // Alt may have been pressed while a panel (another process's
            // window) had focus, which this thread's key state never sees.
            if ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0 && !playing_) {
                Resize(event.wheelSteps > 0 ? 1.1f : 1.0f / 1.1f);
                return;
            }
            // Shift held: turn (6.6), the same way.
            if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 && !playing_) {
                Rotate(event.wheelSteps > 0 ? 1 : -1);
                return;
            }
            float zoom = std::clamp(camera_.zoom * std::pow(1.15f, event.wheelSteps), kMinZoom, kMaxZoom);
            camera_.ZoomAt(event.x, event.y, zoom);
            return;
        }
        case Type::Leave:
            hasHover_ = false;
            return;
        case Type::Down:
            post_({{"type", "panel:focused"}, {"id", "viewport"}}); // the maximise shortcut's panel (8.1)
            if (event.button == Button::Right || event.button == Button::Middle) {
                panning_ = true;
                panLastX_ = event.x;
                panLastY_ = event.y;
                if (event.button == Button::Right) {
                    rightDownX_ = event.x;
                    rightDownY_ = event.y;
                    rightMoved_ = false;
                }
                return;
            }
            if (event.button != Button::Left) {
                return;
            }
            if (playing_) {
                // Diablo-style: click to attack toward the cursor.
                float worldX = 0.0f;
                float worldY = 0.0f;
                UnprojectIso(sceneX, sceneY, iso_, worldX, worldY);
                session_.RequestPlayerAttack(worldX, worldY);
                return;
            }
            if (!placeBrush_.empty() && build_ && !buildJob_) {
                CellAt(sceneX, sceneY, iso_, brushFromX_, brushFromY_);
                brushToX_ = brushFromX_, brushToY_ = brushFromY_;
                brushing_ = true;
                return;
            }
            if (tool_ == Tool::Select) {
                auto picked = PickEntity(registry_, sceneX, sceneY, iso_);
                Select(picked);
                if (picked) {
                    dragging_ = true;
                    dragFromX_ = sceneX;
                    dragFromY_ = sceneY;
                    BeginEdit();
                } else {
                    boxing_ = true;
                    boxFromX_ = boxToX_ = sceneX;
                    boxFromY_ = boxToY_ = sceneY;
                }
            } else {
                stroking_ = true;
                BeginEdit();
                ToolStroke(sceneX, sceneY, true);
            }
            return;
        case Type::Up:
            if (event.button == Button::Right && !rightMoved_ && !playing_) {
                // A right-click: the menu for what's under the cursor (the UI draws it).
                auto picked = PickEntity(registry_, sceneX, sceneY, iso_);
                Select(picked);
                int gx = 0, gy = 0;
                CellAt(sceneX, sceneY, iso_, gx, gy);
                nlohmann::json entity = nullptr;
                if (picked) {
                    const auto* name = registry_.try_get<Name>(*picked);
                    entity = {{"id", static_cast<uint32_t>(*picked)},
                              {"name", name ? name->value : std::string()},
                              {"prefab", registry_.all_of<PrefabInstance>(*picked)
                                             ? nlohmann::json(registry_.get<PrefabInstance>(*picked).path)
                                             : nlohmann::json(nullptr)},
                              {"tile", registry_.all_of<Tile>(*picked)}};
                }
                // A ground tile there (a tile layer's cell, a built place's ground): its prefab, to Draw with.
                std::string cellPrefab;
                if (!picked) {
                    if (const auto cell = PickLayerCell(sceneX, sceneY)) {
                        const TileLayers* layers = FindLayers(registry_);
                        const TileLayer* layer = layers ? layers->Find(cell->layer) : nullptr;
                        if (const TileDef* def = layer ? layer->Def(layer->Get(cell->gx, cell->gy)) : nullptr) cellPrefab = def->prefab;
                    }
                }
                post_({{"type", "viewport:contextMenu"},
                       {"x", viewportBounds_.x + static_cast<int>(event.x)},
                       {"y", viewportBounds_.y + static_cast<int>(event.y)},
                       {"entity", entity},
                       {"cell", {gx, gy}},
                       {"cellPrefab", cellPrefab},
                       {"brush", tool_ == Tool::Prefab ? brushPrefab_ : std::string()}});
            }
            if (event.button == Button::Right || event.button == Button::Middle || event.button == Button::None) {
                panning_ = false;
            }
            if (brushing_ && (event.button == Button::Left || event.button == Button::None)) {
                brushing_ = false;
                if (build_ && !buildJob_) {
                    const auto [ox, oy] = build_->Origin();
                    const int x0 = std::min(brushFromX_, brushToX_) - ox, y0 = std::min(brushFromY_, brushToY_) - oy;
                    const int x1 = std::max(brushFromX_, brushToX_) + 1 - ox, y1 = std::max(brushFromY_, brushToY_) + 1 - oy;
                    const std::string module = placeBrush_;
                    // Laid out again where it was painted: off the UI thread, then the scene follows.
                    RunBuildJob([module, x0, y0, x1, y1](PlaceBuild& build) { return build.Paint(module, x0, y0, x1, y1); },
                                "Can't paint here: ", [this] { UpdateBuiltParts(); });
                }
                return;
            }
            if (event.button == Button::Left || event.button == Button::None) {
                if (dragging_ || stroking_) {
                    EndEdit();
                }
                if (stroking_) FlushStrokeLayerEdits();
                dragging_ = false;
                stroking_ = false;
                if (boxing_) {
                    boxing_ = false;
                    // A click, not a drag: the ground cell under it.
                    if (std::hypot(boxToX_ - boxFromX_, boxToY_ - boxFromY_) * camera_.zoom < 6.0f) {
                        if (auto cell = PickLayerCell(boxFromX_, boxFromY_)) SelectCell(*cell);
                    } else {
                        SelectBox();
                    }
                }
            }
            return;
        case Type::Move:
            hasHover_ = true;
            hoverX_ = sceneX;
            hoverY_ = sceneY;
            if (panning_ && std::hypot(event.x - rightDownX_, event.y - rightDownY_) > 4.0f) {
                rightMoved_ = true;
            }
            if (panning_) {
                camera_.offsetX -= (event.x - panLastX_) / camera_.zoom;
                camera_.offsetY -= (event.y - panLastY_) / camera_.zoom;
                panLastX_ = event.x;
                panLastY_ = event.y;
            }
            if (dragging_ && selected_ && DragEntity(registry_, *selected_, dragFromX_, dragFromY_, sceneX, sceneY, iso_)) {
                dragFromX_ = sceneX;
                dragFromY_ = sceneY;
                MarkChanged();
                inspectorStale_ = true;
            }
            if (stroking_) {
                ToolStroke(sceneX, sceneY, false);
            }
            if (brushing_) CellAt(sceneX, sceneY, iso_, brushToX_, brushToY_);
            if (boxing_) {
                boxToX_ = sceneX;
                boxToY_ = sceneY;
            }
            return;
    }
}

// Keys with the viewport focused (development_plan.md 5.6): the shortcuts are
// the user's keymap, which lives in the UI (keys.ts), so the viewport
// forwards each key as a combo ("Ctrl+S", "F5", "[") and the UI runs whatever
// it's bound to — the same code as when the panels have focus. Only what
// isn't rebindable stays here: arrows nudge, 1-9 pick a prefab, Esc stops
// Play. In Play, keys are forwarded too (the UI only acts on Play-time
// actions); the game itself reads the held keys (ViewportWindow::IsKeyDown).

void EditorApp::OnViewportChar(wchar_t character) {
    // Symbols, named by the character typed, so any layout works ('[' is
    // AltGr+8 on some); letters, digits and controls come as keys instead.
    if (character < 0x21 || character == 0x7F || std::iswalnum(character)) {
        return;
    }
    post_({{"type", "viewport:key"}, {"combo", WideToUtf8(std::wstring(1, character))}});
}

namespace {

// The keymap's name for a key ("A", "F5", "Delete"); empty for symbols,
// which arrive as characters (OnViewportChar).
std::string KeyName(int virtualKey) {
    if ((virtualKey >= 'A' && virtualKey <= 'Z') || (virtualKey >= '0' && virtualKey <= '9')) {
        return std::string(1, static_cast<char>(virtualKey));
    }
    if (virtualKey >= VK_F1 && virtualKey <= VK_F24) {
        return "F" + std::to_string(virtualKey - VK_F1 + 1);
    }
    switch (virtualKey) {
        case VK_DELETE: return "Delete";
        case VK_BACK: return "Backspace";
        case VK_ESCAPE: return "Escape";
        case VK_RETURN: return "Enter";
        case VK_TAB: return "Tab";
        case VK_SPACE: return "Space";
        case VK_INSERT: return "Insert";
        case VK_HOME: return "Home";
        case VK_END: return "End";
        case VK_PRIOR: return "PageUp";
        case VK_NEXT: return "PageDown";
        case VK_LEFT: return "ArrowLeft";
        case VK_RIGHT: return "ArrowRight";
        case VK_UP: return "ArrowUp";
        case VK_DOWN: return "ArrowDown";
        default: return {};
    }
}

} // namespace

void EditorApp::OnViewportKey(int virtualKey, bool ctrl, bool shift) {
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    if (playing_) {
        if (virtualKey == VK_ESCAPE) {
            StopPlay();
            return;
        }
    } else if (!ctrl && !alt) {
        switch (virtualKey) {
            case VK_LEFT:
            case VK_RIGHT:
            case VK_UP:
            case VK_DOWN: {
                // Along the grid's axes; Shift: a tenth of a cell.
                const float step = shift ? 0.1f : 1.0f;
                NudgeSelected(virtualKey == VK_LEFT ? -step : virtualKey == VK_RIGHT ? step : 0.0f,
                              virtualKey == VK_UP ? -step : virtualKey == VK_DOWN ? step : 0.0f);
                return;
            }
            default:
                // 1-9 arm the prefab brush with the palette's first nine prefabs.
                if (virtualKey >= '1' && virtualKey <= '9' && !shift) {
                    size_t index = static_cast<size_t>(virtualKey - '1');
                    if (index < prefabs_.Prefabs().size()) {
                        brushPrefab_ = prefabs_.Prefabs()[index].path;
                        SetTool(Tool::Prefab);
                    }
                    return;
                }
        }
    }
    const std::string key = KeyName(virtualKey);
    if (key.empty()) {
        return;
    }
    // AltGr reports Ctrl+Alt; with a letter that's still a real combo.
    std::string combo = std::string(ctrl ? "Ctrl+" : "") + (alt ? "Alt+" : "") + (shift ? "Shift+" : "") + key;
    post_({{"type", "viewport:key"}, {"combo", combo}});
}

void EditorApp::ToolStroke(float sceneX, float sceneY, bool first) {
    int gx = 0;
    int gy = 0;
    CellAt(sceneX, sceneY, iso_, gx, gy);
    if (!first && gx == lastStrokeGx_ && gy == lastStrokeGy_) {
        return;
    }
    lastStrokeGx_ = gx;
    lastStrokeGy_ = gy;

    switch (tool_) {
        case Tool::Prefab: {
            const PrefabInfo* prefab = prefabs_.Find(brushPrefab_);
            if (prefab == nullptr || !prefab->error.empty()) {
                if (first) {
                    post_({{"type", "notify"}, {"text", "Pick a prefab in the Prefabs panel first"}});
                }
                return;
            }
            const float cellX = static_cast<float>(gx) + 0.5f;
            const float cellY = static_cast<float>(gy) + 0.5f;
            if (prefab->isTile) {
                // Tile prefabs paint: every cell the stroke crosses.
                if (selected_ && registry_.valid(*selected_) && registry_.all_of<Tile>(*selected_)) {
                    const auto& tile = registry_.get<Tile>(*selected_);
                    if (tile.gx == gx && tile.gy == gy) {
                        Select(std::nullopt); // that tile is about to be replaced
                    }
                }
                if (autotile_) {
                    const TerrainSet set = TerrainTiles();
                    if (auto terrain = set.TerrainOf(prefab->path)) {
                        PaintTerrain(registry_, prefabs_, set, {{gx, gy}}, *terrain, DefaultBackground(set, *terrain));
                        break;
                    }
                }
                auto painted = prefabs_.Instantiate(registry_, prefab->path, cellX, cellY);
                if (painted && std::abs(brushScale_ - 1.0f) > 1e-3f) {
                    ScaleEntity(EngineReflection(), registry_, *painted, brushScale_);
                }
                ApplyBrushRotation(painted);
                break;
            }
            if (!first) {
                return; // everything else: one per click, not a trail
            }
            // There's only ever one player: placing the player prefab again
            // moves the existing one.
            if (prefab->isPlayer) {
                if (auto player = FindPlayer(registry_)) {
                    registry_.get<WorldTransform2D>(*player) = {cellX, cellY};
                    Select(*player);
                    break;
                }
            }
            {
                auto placed = prefabs_.Instantiate(registry_, prefab->path, cellX, cellY);
                if (placed && std::abs(brushScale_ - 1.0f) > 1e-3f) {
                    ScaleEntity(EngineReflection(), registry_, *placed, brushScale_);
                }
                ApplyBrushRotation(placed);
                Select(placed);
            }
            break;
        }
        case Tool::Height:
            ShapeGround(gx, gy);
            break;
        case Tool::Erase: {
            auto picked = PickEntity(registry_, sceneX, sceneY, iso_);
            if (!picked) {
                if (auto cell = PickLayerCell(sceneX, sceneY)) EraseLayerCell(*cell);
                return;
            }
            if (selected_ == picked) {
                Select(std::nullopt);
            }
            if (autotile_ && registry_.all_of<Tile, PrefabInstance>(*picked)) {
                const TerrainSet set = TerrainTiles();
                if (auto terrain = set.TerrainOf(registry_.get<PrefabInstance>(*picked).path)) {
                    const std::string background = DefaultBackground(set, *terrain);
                    if (set.PlainTile(background)) {
                        const auto& tile = registry_.get<Tile>(*picked);
                        PaintTerrain(registry_, prefabs_, set, {{tile.gx, tile.gy}}, background, background);
                        break;
                    }
                }
            }
            DeleteEntity(registry_, *picked);
            break;
        }
        case Tool::Select:
            return;
    }
    MarkChanged();
    sceneTreeStale_ = true;
}

// --- Edit transactions ------------------------------------------------------

void EditorApp::BeginEdit() {
    resizeRun_.reset(); // any other edit ends a run of resizes
    editingPart_ = selected_ ? PartOf(*selected_) : std::string();
    pendingSnapshot_ = SerializeScene(registry_);
    pendingPushed_ = false;
}

void EditorApp::MarkChanged() {
    if (pendingSnapshot_ && !pendingPushed_) {
        history_.Push(*pendingSnapshot_);
        pendingPushed_ = true;
    }
    dirty_ = true;
    editorStateStale_ = true;
    // A built part changed by hand keeps the change: it's locked (7.8).
    if (!editingPart_.empty() && build_ && !buildJob_) {
        build_->Lock(editingPart_, true);
        editingPart_.clear();
        PostSelectedPart();
    }
}

void EditorApp::EndEdit() {
    pendingSnapshot_.reset();
    pendingPushed_ = false;
}

void EditorApp::ApplyOneShotEdit(const std::function<bool()>& edit) {
    BeginEdit();
    if (edit()) {
        MarkChanged();
    }
    EndEdit();
}

// --- Commands ---------------------------------------------------------------

std::optional<EditorApp::CellPick> EditorApp::PickLayerCell(float sceneX, float sceneY) const {
    // The ground of a built place is tile layers, not entities: the topmost layer with a tile there.
    const TileLayers* layers = FindLayers(registry_);
    if (layers == nullptr) return std::nullopt;
    int gx = 0, gy = 0;
    CellAt(sceneX, sceneY, iso_, gx, gy);
    const auto ordered = layers->Ordered();
    for (auto it = ordered.rbegin(); it != ordered.rend(); ++it) {
        if ((*it)->Get(gx, gy) != TileLayer::kEmpty) return CellPick{(*it)->name, gx, gy};
    }
    return std::nullopt;
}

void EditorApp::SelectCell(const CellPick& cell) {
    selectedCell_ = cell;
    const TileLayers* layers = FindLayers(registry_);
    const TileLayer* layer = layers ? layers->Find(cell.layer) : nullptr;
    const TileDef* def = layer ? layer->Def(layer->Get(cell.gx, cell.gy)) : nullptr;
    post_({{"type", "inspector:cell"},
           {"layer", cell.layer},
           {"x", cell.gx},
           {"y", cell.gy},
           {"prefab", def ? def->prefab : std::string()},
           {"texture", def ? def->sprite.texture : std::string()},
           {"solid", def ? def->solid : false}});
}

void EditorApp::EraseLayerCell(const CellPick& cell) {
    TileLayer* layer = LayersOf(registry_).Find(cell.layer);
    if (layer == nullptr || layer->Get(cell.gx, cell.gy) == TileLayer::kEmpty) return;
    auto it = std::find_if(strokeLayerEdits_.begin(), strokeLayerEdits_.end(), [&](const TileLayerEdit& e) { return e.layer == cell.layer; });
    if (it == strokeLayerEdits_.end()) {
        strokeLayerEdits_.emplace_back(cell.layer);
        it = std::prev(strokeLayerEdits_.end());
    }
    it->Set(*layer, cell.gx, cell.gy, TileLayer::kEmpty);
    if (selectedCell_ && selectedCell_->layer == cell.layer && selectedCell_->gx == cell.gx && selectedCell_->gy == cell.gy) {
        selectedCell_.reset();
        post_({{"type", "inspector:clear"}});
    }
    dirty_ = true;
}

void EditorApp::ShapeGround(int gx, int gy) {
    strokeLabel_ = heightMode_ == "raise" ? "Raise ground" : heightMode_ == "lower" ? "Lower ground" : "Level ground";
    TileLayers& layers = LayersOf(registry_);
    std::vector<std::string> names;
    for (const TileLayer* l : layers.Ordered()) names.push_back(l->name);
    const float cx = gx + 0.5f, cy = gy + 0.5f, reach = heightRadius_ + 0.75f;
    const float step = iso_.tileHeight * 0.25f; // a quarter of a cell's height each time the brush passes
    auto liftOf = [](const TileLayer& l, int x, int y) {
        const TileLayer::CornerLift* p = l.Lift(x, y);
        return p ? *p : TileLayer::CornerLift{};
    };
    // Levelling: towards the height under the brush's middle (the topmost tile there).
    float target = 0.0f;
    for (auto n = names.rbegin(); n != names.rend(); ++n) {
        const TileLayer* l = layers.Find(*n);
        if (l && l->Get(gx, gy) != TileLayer::kEmpty) {
            const auto c = liftOf(*l, gx, gy);
            target = (c[0] + c[1] + c[2] + c[3]) / 4;
            break;
        }
    }
    auto edited = [&](TileLayer& layer) -> TileLayerEdit& {
        auto it = std::find_if(strokeLayerEdits_.begin(), strokeLayerEdits_.end(), [&](const TileLayerEdit& e) { return e.layer == layer.name; });
        if (it == strokeLayerEdits_.end()) {
            strokeLayerEdits_.emplace_back(layer.name);
            it = std::prev(strokeLayerEdits_.end());
        }
        return *it;
    };
    const int r = heightRadius_ + 1;
    for (const auto& name : names) {
        TileLayer* layer = layers.Find(name);
        if (layer == nullptr) continue;
        for (int y = gy - r; y <= gy + r; ++y) {
            for (int x = gx - r; x <= gx + r; ++x) {
                const TileDef* def = layer->Def(layer->Get(x, y));
                if (def == nullptr || !def->sprite.flat) continue;
                // Each corner (top, right, bottom, left) by its own distance: a shared corner moves
                // the same in every cell round it, so the ground stays whole.
                TileLayer::CornerLift lift = liftOf(*layer, x, y);
                const float px[4] = {0, 1, 1, 0}, py[4] = {0, 0, 1, 1};
                bool changed = false;
                for (int i = 0; i < 4; ++i) {
                    const float d = std::hypot(x + px[i] - cx, y + py[i] - cy);
                    if (d >= reach) continue;
                    const float t = 1.0f - d / reach, f = t * t * (3 - 2 * t);
                    const float before = lift[i];
                    if (heightMode_ == "raise") lift[i] += step * f;
                    else if (heightMode_ == "lower") lift[i] -= step * f;
                    else lift[i] += (target - lift[i]) * f * 0.6f;
                    lift[i] = std::clamp(lift[i], -iso_.tileHeight, iso_.tileHeight * 4);
                    changed = changed || std::abs(lift[i] - before) > 0.01f;
                }
                if (changed) edited(*layer).SetLift(*layer, x, y, lift);
            }
        }
    }
    // What stands there rides with the ground: its lift, the height under its foot.
    auto heightAt = [&](float wx, float wy) -> std::optional<float> {
        const int x = static_cast<int>(std::floor(wx)), y = static_cast<int>(std::floor(wy));
        for (auto n = names.rbegin(); n != names.rend(); ++n) {
            const TileLayer* l = layers.Find(*n);
            if (l == nullptr || l->Get(x, y) == TileLayer::kEmpty) continue;
            const float fx = wx - x, fy = wy - y;
            const auto c = liftOf(*l, x, y); // top (x,y), right (x+1,y), bottom (x+1,y+1), left (x,y+1)
            return (c[0] * (1 - fx) + c[1] * fx) * (1 - fy) + (c[3] * (1 - fx) + c[2] * fx) * fy;
        }
        return std::nullopt;
    };
    std::vector<Entity> riders;
    for (auto [e, at, sprite] : registry_.view<WorldTransform2D, Sprite>().each()) {
        if (registry_.all_of<Tile>(e) || std::abs(at.x - cx) > r + 1 || std::abs(at.y - cy) > r + 1) continue;
        riders.push_back(e);
    }
    for (Entity e : riders) {
        const auto& at = registry_.get<WorldTransform2D>(e);
        if (auto h = heightAt(at.x, at.y); h && std::abs(registry_.get<Sprite>(e).lift - *h) > 0.1f) {
            EditField(EngineReflection(), registry_, e, "Sprite", "lift", *h);
        }
    }
    dirty_ = true;
}

void EditorApp::FlushStrokeLayerEdits() {
    if (strokeLayerEdits_.empty()) return;
    history_.PushCommand(std::make_unique<TileLayerCommand>(std::move(strokeLayerEdits_), strokeLabel_));
    strokeLayerEdits_.clear();
    strokeLabel_ = "Erase ground";
    dirty_ = true;
    editorStateStale_ = true;
}

void EditorApp::ClearBoxSelection() {
    selectedMany_.clear();
    selectedCells_.clear();
}

void EditorApp::SelectBox() {
    // What the dragged rectangle covers on screen: the things standing in it, and (where no
    // thing is) the ground cells of the tile layers whose middle is in it.
    ClearBoxSelection();
    const float x0 = std::min(boxFromX_, boxToX_), x1 = std::max(boxFromX_, boxToX_);
    const float y0 = std::min(boxFromY_, boxToY_), y1 = std::max(boxFromY_, boxToY_);
    for (auto [entity, t] : registry_.view<ScreenTransform2D>(entt::exclude<Tile>).each()) {
        if (!registry_.all_of<Sprite>(entity)) continue;
        if (t.x >= x0 && t.x <= x1 && t.y >= y0 && t.y <= y1) selectedMany_.push_back(entity);
    }
    for (auto [entity, t, tile] : registry_.view<ScreenTransform2D, Tile>().each()) {
        if (t.x >= x0 && t.x <= x1 && t.y >= y0 && t.y <= y1) selectedMany_.push_back(entity);
    }
    if (const TileLayers* layers = FindLayers(registry_)) {
        // The world cells the rectangle can reach (its corners unprojected), each tested on screen.
        float wx[4], wy[4];
        UnprojectIso(x0, y0, iso_, wx[0], wy[0]);
        UnprojectIso(x1, y0, iso_, wx[1], wy[1]);
        UnprojectIso(x0, y1, iso_, wx[2], wy[2]);
        UnprojectIso(x1, y1, iso_, wx[3], wy[3]);
        const int gx0 = static_cast<int>(std::floor(*std::min_element(wx, wx + 4))) - 1, gx1 = static_cast<int>(std::ceil(*std::max_element(wx, wx + 4))) + 1;
        const int gy0 = static_cast<int>(std::floor(*std::min_element(wy, wy + 4))) - 1, gy1 = static_cast<int>(std::ceil(*std::max_element(wy, wy + 4))) + 1;
        if (static_cast<long>(gx1 - gx0) * (gy1 - gy0) <= 400000) {
            const auto ordered = layers->Ordered();
            for (int gy = gy0; gy <= gy1; ++gy) {
                for (int gx = gx0; gx <= gx1; ++gx) {
                    const ScreenPose p = ProjectIso(gx + 0.5f, gy + 0.5f, iso_);
                    if (p.x < x0 || p.x > x1 || p.y < y0 || p.y > y1) continue;
                    for (auto it = ordered.rbegin(); it != ordered.rend(); ++it) {
                        if ((*it)->Get(gx, gy) != TileLayer::kEmpty) {
                            selectedCells_.push_back({(*it)->name, gx, gy});
                            break;
                        }
                    }
                }
            }
        }
    }
    post_({{"type", "inspector:many"}, {"entities", selectedMany_.size()}, {"cells", selectedCells_.size()}});
}

void EditorApp::DeleteBoxSelection() {
    if (playing_) return;
    if (!selectedMany_.empty()) {
        ApplyOneShotEdit([&] {
            bool any = false;
            for (Entity e : selectedMany_) {
                if (registry_.valid(e)) {
                    DeleteEntity(registry_, e);
                    any = true;
                }
            }
            return any;
        });
        sceneTreeStale_ = true;
    }
    for (const auto& cell : selectedCells_) EraseLayerCell(cell);
    FlushStrokeLayerEdits();
    ClearBoxSelection();
    post_({{"type", "inspector:clear"}});
}

void EditorApp::EraseSelectedCell() {
    if (!selectedCell_ || playing_) return;
    TileLayer* layer = LayersOf(registry_).Find(selectedCell_->layer);
    if (layer == nullptr) return;
    TileLayerEdit edit(selectedCell_->layer);
    edit.Set(*layer, selectedCell_->gx, selectedCell_->gy, TileLayer::kEmpty);
    history_.PushCommand(std::make_unique<TileLayerCommand>(std::vector<TileLayerEdit>{edit}, "Erase a ground cell"));
    dirty_ = true;
    editorStateStale_ = true;
    selectedCell_.reset();
    post_({{"type", "inspector:clear"}});
}

void EditorApp::Select(std::optional<Entity> entity) {
    if (entity && !registry_.valid(*entity)) {
        entity.reset();
    }
    selectedCell_.reset(); // a cell or an entity, not both
    ClearBoxSelection();
    selected_ = entity;
    // Its prefab too: Assets ▸ Prefabs selects it there.
    const auto* instance = entity ? registry_.try_get<PrefabInstance>(*entity) : nullptr;
    post_({{"type", "selection:changed"},
           {"id", entity ? nlohmann::json(static_cast<uint32_t>(*entity)) : nlohmann::json(nullptr)},
           {"prefab", instance ? instance->path : std::string()}});
    if (entity) {
        post_(InspectorJson(*entity));
    } else {
        post_({{"type", "inspector:clear"}});
    }
    PostSelectedPart();
}

std::string EditorApp::PartOf(Entity entity) const {
    if (!build_ || buildJob_ || !registry_.valid(entity)) return {};
    const auto* node = registry_.try_get<PlaceNode>(entity);
    if (node == nullptr || node->place != build_->Id()) return {};
    // One instance of a template: up to its "#k" ("ward/cottages#2/house" -> "ward/cottages#2").
    const std::string& s = node->statement;
    if (const size_t hash = s.find('#'); hash != std::string::npos) return s.substr(0, s.find('/', hash));
    return node->node;
}

void EditorApp::PostSelectedPart() {
    const std::string part = selected_ ? PartOf(*selected_) : std::string();
    nlohmann::json info = part.empty() || !build_ || buildJob_ ? nlohmann::json() : build_->NodeInfo(part);
    post_({{"type", "build:part"}, {"part", info}, {"brush", placeBrush_}});
}

void EditorApp::UpdateBuiltParts() {
    if (!build_ || buildJob_) return;
    const std::set<std::string> changed = build_->TakeChanged();
    if (changed.empty() || build_->Stage() != "built" || !build_->Result().is_object()) return;
    // The scene follows: only those parts are replaced; locked ones stay as edited.
    PlaceSceneOptions options;
    options.place = build_->Id();
    std::tie(options.originX, options.originY) = build_->Origin();
    options.iso = iso_;
    options.isoGrid = ActiveSettings().grid.style != GridStyle::TopDown;
    options.hills = HillsFor(build_->Brief());
    options.only = changed;
    options.locked = build_->Locks();
    ApplyBackground(options);
    const TerrainSet terrains = ProjectTerrainSet(prefabs_, assets_.GetProject());
    const Catalog catalog = BuildCatalog(assets_, &Places(), CatalogOptionsNow());
    const nlohmann::json assets = PlanPlaceAssets(Places(), build_->Spec(), build_->Solved(), catalog, prefabs_, assets_, terrains, options);
    nlohmann::json report;
    auto command = BuildPlaceScene(registry_, prefabs_, build_->Solved(), assets, terrains, options, &report);
    history_.PushCommand(std::move(command));
    for (const auto& picture : report.value("groundPictures", nlohmann::json::array())) {
        if (picture.is_string()) renderer_.InvalidateTexture(picture.get<std::string>());
    }
    for (const auto& picture : report.value("groundPictures", nlohmann::json::array())) {
        if (picture.is_string()) renderer_.InvalidateTexture(picture.get<std::string>());
    }
    prefabs_.SyncAll(registry_);
    UpdateWorldToScreen(registry_, iso_);
    dirty_ = true;
    editorStateStale_ = true;
    sceneTreeStale_ = true;
}

void EditorApp::SetTool(Tool tool) {
    tool_ = tool;
    editorStateStale_ = true;
}

void EditorApp::Undo() {
    if (playing_) {
        return;
    }
    EndEdit();
    resizeRun_.reset();
    auto index = selected_ ? IndexOfEntity(registry_, *selected_) : std::nullopt;
    if (history_.Undo(registry_)) {
        dirty_ = true;
        RestoreSelection(index);
        sceneTreeStale_ = true;
        editorStateStale_ = true;
    }
}

void EditorApp::Redo() {
    if (playing_) {
        return;
    }
    EndEdit();
    resizeRun_.reset();
    auto index = selected_ ? IndexOfEntity(registry_, *selected_) : std::nullopt;
    if (history_.Redo(registry_)) {
        dirty_ = true;
        RestoreSelection(index);
        sceneTreeStale_ = true;
        editorStateStale_ = true;
    }
}

void EditorApp::DeleteSelected() {
    if (playing_ || !selected_) {
        return;
    }
    Entity entity = *selected_;
    ApplyOneShotEdit([&] {
        DeleteEntity(registry_, entity);
        return true;
    });
    Select(std::nullopt);
    sceneTreeStale_ = true;
}

void EditorApp::DuplicateSelected() {
    if (playing_ || !selected_) {
        return;
    }
    std::optional<Entity> copy;
    ApplyOneShotEdit([&] {
        copy = DuplicateEntity(registry_, *selected_);
        return copy.has_value();
    });
    if (copy) {
        Select(copy);
        sceneTreeStale_ = true;
    } else {
        post_({{"type", "notify"}, {"text", "Tiles and the player can't be duplicated"}});
    }
}

void EditorApp::FrameScene() {
    UpdateWorldToScreen(registry_, iso_);

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    auto view = registry_.view<ScreenTransform2D, Sprite>();
    for (auto entity : view) {
        const auto& t = view.get<ScreenTransform2D>(entity);
        const auto& s = view.get<Sprite>(entity);
        minX = std::min(minX, t.x - s.width * 0.5f);
        minY = std::min(minY, t.y - s.height * 0.5f);
        maxX = std::max(maxX, t.x + s.width * 0.5f);
        maxY = std::max(maxY, t.y + s.height * 0.5f);
    }
    if (minX > maxX) {
        camera_ = Camera2D{};
        return;
    }

    int width = 0;
    int height = 0;
    viewport_.GetFramebufferSize(width, height);
    if (width <= 0 || height <= 0) {
        return;
    }
    constexpr float kMargin = 48.0f;
    float zoomX = (static_cast<float>(width) - 2.0f * kMargin) / std::max(maxX - minX, 1.0f);
    float zoomY = (static_cast<float>(height) - 2.0f * kMargin) / std::max(maxY - minY, 1.0f);
    camera_.zoom = std::clamp(std::min(zoomX, zoomY), kMinZoom, kMaxZoom);
    camera_.offsetX = (minX + maxX) * 0.5f - static_cast<float>(width) * 0.5f / camera_.zoom;
    camera_.offsetY = (minY + maxY) * 0.5f - static_cast<float>(height) * 0.5f / camera_.zoom;
}

void EditorApp::Save() {
    if (playing_) {
        post_({{"type", "notify"}, {"text", "Stop Play mode before saving"}});
        return;
    }
    try {
        SaveScene(registry_, scenePath_);
        if (settingsDirty_) {
            SaveSettings(EngineReflection(), assets_.GetProject().SettingsPath().string());
            settingsDirty_ = false;
        }
        if (!layout_.is_null()) {
            std::ofstream out(LayoutPath(), std::ios::trunc | std::ios::binary);
            if (out) {
                out << layout_.dump(2) << "\n";
                savedLayout_ = layout_;
            }
        }
    } catch (const std::exception& e) {
        post_({{"type", "notify"}, {"text", std::string("Save failed: ") + e.what()}});
        return;
    }
    dirty_ = false;
    editorStateStale_ = true;
    if (recoveryOffer_.is_null()) {
        std::error_code ec; // saved: nothing of it to recover
        fs::remove(RecoveryDir() / "snapshot.scene", ec);
        fs::remove(RecoveryDir() / "snapshot.json", ec);
    }
    post_({{"type", "notify"}, {"text", "Saved " + std::filesystem::path(scenePath_).filename().string()}});
}

std::filesystem::path EditorApp::SaveGamePath() const {
    return assets_.GetProject().CacheDir() / "saves";
}

// The text a scene starts from in Play: the one being edited as it is now, others as saved.
std::string EditorApp::SceneTextForPlay(const std::string& scene) const {
    if (scene == OpenSceneRelative()) return playSnapshot_;
    std::ifstream in(assets_.GetProject().Resolve(scene), std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void EditorApp::CapturePlayWorld() {
    const std::string scene = session_.Scene();
    const std::string text = SceneTextForPlay(scene);
    if (scene.empty() || text.empty()) return;
    Registry pristine;
    DeserializeScene(pristine, text);
    playWorlds_[scene] = CaptureSceneState(registry_, pristine, EngineReflection());
}

void EditorApp::ApplyPlayWorld(const std::string& scene) {
    if (auto it = playWorlds_.find(scene); it != playWorlds_.end()) {
        ApplySceneState(registry_, it->second, EngineReflection());
    }
}

void EditorApp::SaveGame() {
    if (!playing_) {
        post_({{"type", "notify"}, {"text", "Saving the game works while playing (F5, then F6)"}});
        return;
    }
    // A v2 save (12.5): the session, the player whole, and what changed in every scene visited.
    CapturePlayWorld();
    nlohmann::json save;
    save["session"] = session_.MakeSave();
    if (auto player = FindPlayer(registry_)) save["player"] = SerializeEntities(registry_, {*player});
    nlohmann::json worlds = nlohmann::json::object();
    for (const auto& [scene, state] : playWorlds_) worlds[scene] = state;
    save["worlds"] = worlds;
    save["meta"] = {{"place", std::filesystem::path(session_.Scene()).stem().string()}};
    if (!SaveGames(SaveGamePath()).Write("quick", save)) {
        post_({{"type", "notify"}, {"text", "Couldn't write the saved game"}});
        return;
    }
    session_.Toast("Game saved");
    editorStateStale_ = true;
}

void EditorApp::LoadGame() {
    auto read = SaveGames(SaveGamePath()).Read("quick");
    if (!read) {
        post_({{"type", "notify"}, {"text", "No saved game yet: press F6 while playing to save one"}});
        return;
    }
    if (!playing_) {
        StartPlay(); // Play ▸ Continue from save
    }
    // The saved scene as it starts in Play, what changed in it, then the player,
    // the clock and the story as they were saved.
    const nlohmann::json save = read->value("session", nlohmann::json::object());
    const std::string scene = save.value("scene", OpenSceneRelative());
    const std::string text = SceneTextForPlay(scene);
    if (text.empty()) {
        post_({{"type", "notify"}, {"text", "The saved game is in " + scene + ", which doesn't exist any more"}});
        return;
    }
    Select(std::nullopt);
    DeserializeScene(registry_, text);
    PlayLayersFor(scene);
    prefabs_.SyncAll(registry_);
    playWorlds_.clear();
    const nlohmann::json worlds = read->value("worlds", nlohmann::json::object());
    for (const auto& [name, state] : worlds.items()) playWorlds_[name] = state;
    ApplyPlayWorld(scene);
    if (read->contains("player") && (*read)["player"].is_string()) ReplacePlayer(registry_, (*read)["player"].get<std::string>());
    session_.OnSceneChanged();
    session_.ApplySave(save);
    session_.SetScene(scene);
    UpdateWorldToScreen(registry_, iso_);
    if (auto player = FindPlayer(registry_)) {
        int width = 0, height = 0;
        viewport_.GetFramebufferSize(width, height);
        cameraRig_.MeasureLevel(registry_, iso_); // the level's edges for the play camera (12.7)
        SnapCameraTo(camera_, registry_, *player, static_cast<float>(width), static_cast<float>(height));
    }
    sceneTreeStale_ = true;
}

std::filesystem::path EditorApp::LayoutPath() const {
    return assets_.GetProject().root / "editor-layout.json";
}

// --- Play mode --------------------------------------------------------------
//
// The scene is snapshotted on Play and restored on Stop, so a playtest (and
// any inspector edits made during it) never changes the edited scene.

void EditorApp::StartPlay() {
    if (playing_) {
        return;
    }
    EndEdit();
    dragging_ = false;
    stroking_ = false;
    playSnapshot_ = SerializeScene(registry_);
    TagSceneOrigins(registry_); // which entity of the scene each is, for the world's memory (12.5)
    playWorlds_.clear();
    playManifest_ = LoadGameManifest(assets_.GetProject().root); // its music (12.6)
    selectionIndexAtPlay_ = selected_ ? IndexOfEntity(registry_, *selected_) : std::nullopt;
    session_.ResetClock();
    session_.OnSceneChanged(); // no gateway fires under a player who starts on one
    // Dialogue and quests as they are on disk now; a fresh run each Play.
    story_.Refresh();
    world_.Refresh();
    session_.SetStory(&story_);
    session_.SetScene(OpenSceneRelative());
    // The game's UI as documents (17), as a shipped game shows it.
    if (ActiveSettings().ui.documents) {
        UiDocs().Reload();
        session_.SetDocumentHud(true);
        session_.SetNoticeSink([this](const std::string& channel, const std::string& text, const std::string& key, double seconds) {
            UiDocs().Notify({channel, text, "", seconds, 1, key});
        });
    }
    // Play at the chosen level (16.5), else the game's default.
    session_.ReloadDifficulty();
    session_.SetDifficulty(playDifficulty_.empty() ? session_.Difficulty().DefaultLevel() : playDifficulty_);
    schedules_.Refresh();
    session_.SetSchedules(&schedules_);
    session_.SetClockMinutes(-1.0); // each Play starts the day at Time.startHour
    session_.SetNameResolver([this](const std::string& key) {
        const WorldEntry* entry = world_.Find(key);
        return entry ? entry->name : std::string();
    });
    playing_ = true;
    sinceInspectorRefresh_ = 0.0;
    // Play with a camera that follows the player; the editing view comes
    // back on Stop.
    cameraBeforePlay_ = camera_;
    camera_.zoom = ActiveSettings().camera.playZoom;
    if (auto player = FindPlayer(registry_)) {
        int width = 0;
        int height = 0;
        viewport_.GetFramebufferSize(width, height);
        UpdateWorldToScreen(registry_, iso_);
        cameraRig_.MeasureLevel(registry_, iso_); // the level's edges for the play camera (12.7)
        SnapCameraTo(camera_, registry_, *player, static_cast<float>(width), static_cast<float>(height));
    }
    viewport_.Focus(); // so WASD works without first clicking the viewport
    editorStateStale_ = true;
}

void EditorApp::StopPlay() {
    if (!playing_) {
        return;
    }
    session_.SetDocumentHud(false);
    session_.SetNoticeSink({});
    if (uiDocs_) {
        uiDocs_->SetHud(HudData{false});
        uiDocs_->SetDialogue(std::nullopt);
    }
    DeserializeScene(registry_, playSnapshot_);
    LayersOf(registry_) = std::move(playLayers_);
    playLayers_ = {};
    playSnapshot_.clear();
    playing_ = false;
    banner_ = {};
    audio_.PlayMusic({}, 0.5f); // Play's music fades out
    audio_.PlayAmbience({}, 0.5f);
    RestoreSelection(selectionIndexAtPlay_);
    camera_ = cameraBeforePlay_;
    sceneTreeStale_ = true;
    editorStateStale_ = true;
}

void EditorApp::RestoreSelection(std::optional<size_t> index) {
    Select(index ? EntityAtIndex(registry_, *index) : std::nullopt);
}

// --- Frame ------------------------------------------------------------------

// The game's own UI while playing (ui/GameUi): what a shipped game would show.
std::vector<UiQuad> EditorApp::BuildUi() {
    std::vector<UiQuad> ui;
    if (!playing_) {
        // Markers (development_plan.md 3.5) are invisible in play; while
        // editing they're labelled where they stand.
        const Font& font = Font::Builtin(15.0f);
        for (auto [entity, marker, screen] : registry_.view<Marker, ScreenTransform2D>().each()) {
            float vx = 0.0f, vy = 0.0f;
            camera_.SceneToViewport(screen.x, screen.y, vx, vy);
            const std::string text = "@ " + marker.name;
            float w = font.Measure(text) + 10.0f;
            AddUiRect(ui, vx - w * 0.5f, vy - 28.0f, vx + w * 0.5f, vy - 28.0f + font.LineHeight() + 4.0f,
                      {0.05f, 0.05f, 0.09f, 0.8f});
            font.Draw(ui, text, vx - w * 0.5f + 5.0f, vy - 26.0f, {0.99f, 0.86f, 0.45f, 1.0f});
        }
        if (!uiPreview_.empty()) {
            int width = 0;
            int height = 0;
            viewport_.GetFramebufferSize(width, height);
            ShowUiPreview(ui, width, height);
        }
        AddStatsOverlay(ui);
        return ui;
    }
    int width = 0;
    int height = 0;
    viewport_.GetFramebufferSize(width, height);
    session_.BuildUi(ui, static_cast<float>(width), static_cast<float>(height));
    if (uiDocs_ && ActiveSettings().ui.documents) {
        uiDocs_->SetViewport(width, height);
        uiDocs_->SetMenu(std::nullopt, "");
        uiDocs_->SetDialogue(session_.CurrentDialogueView());
        uiDocs_->SetHud(session_.MakeHud(static_cast<float>(width), static_cast<float>(height)));
        uiDocs_->Render(ui);
        for (const auto& texture : uiDocs_->TakeNewTextures()) renderer_.RegisterTexture(texture.name, texture.image);
    }
    if (!banner_.first.empty()) {
        AddBanner(ui, banner_.first, banner_.second, static_cast<float>(width), static_cast<float>(height));
    }
    AddStatsOverlay(ui);
    return ui;
}

DocumentUi& EditorApp::UiDocs() {
    if (!uiDocs_) {
        uiDocs_ = std::make_unique<DocumentUi>(assets_.GetProject().root,
                                               [this](const std::string& path) { return assets_.LoadTexture(path); });
    }
    return *uiDocs_;
}

namespace {

// Sample data for previewing a screen in the UI tab (17.7).
MenuView MockMenu(const std::string& id) {
    MenuView v;
    auto items = [&](std::initializer_list<const char*> labels) {
        for (const char* label : labels) v.items.push_back({label, true});
    };
    if (id == "title") {
        v.title = "The Game";
        v.subtitle = "a subtitle";
        v.dimBackground = false;
        items({"Continue", "New game", "Load game", "Settings", "Credits", "Quit"});
    } else if (id == "pause") {
        v.title = "Paused";
        items({"Resume", "Save game", "Load game", "Settings", "Quit to title", "Quit game"});
    } else if (id == "settings") {
        v.title = "Settings";
        items({"Master volume  100%", "Music volume  80%", "Subtitles  On", "Difficulty  Normal", "Back"});
    } else if (id == "inventory") {
        v.title = "Inventory";
        v.body = "Gold 120   Level 3 (40/100)   Health 7/10";
        items({"[weapon] Iron sword", "Potion x3", "Leather armour", "Close"});
    } else if (id == "journal") {
        v.title = "Journal";
        v.body = "Someone has been stealing from the market.\n- The guard told me about a thief.\n[ ] Find Mira";
        items({"The thief", "The hunt (done)", "Close"});
    } else if (id == "shop") {
        v.title = "Blacksmith";
        v.body = "Your gold 120   Their gold 500";
        items({"Buy Iron sword  40", "Buy Shield  25", "Sell Potion  4", "Close"});
    } else {
        v.title = id.empty() ? "Menu" : id;
        items({"First", "Second", "Back"});
    }
    v.footer = "Up/Down to choose, Enter to pick, Esc to go back";
    return v;
}

} // namespace

void EditorApp::ShowUiPreview(std::vector<UiQuad>& ui, int width, int height) {
    DocumentUi& docs = UiDocs();
    docs.SetViewport(width, height);
    HudData hud;
    hud.health = 7;
    hud.healthMax = 10;
    hud.mana = 4;
    hud.manaMax = 10;
    hud.stamina = 8;
    hud.staminaMax = 10;
    hud.level = 3;
    hud.xp = 40;
    hud.xpNext = 100;
    hud.gold = 120;
    hud.quest = "The thief";
    hud.objective = "Find Mira";
    hud.clock = "Day 2  14:30";
    hud.promptKey = "E";
    hud.promptText = "Talk to Old Tamsin";
    hud.marker = "12 m";
    hud.markerX = width * 0.8f;
    hud.markerY = height * 0.5f;
    hud.visible = uiPreview_ == "hud";
    docs.SetHud(hud);
    if (uiPreview_ == "dialogue") {
        DialogueView talk;
        talk.speaker = "Old Tamsin";
        talk.text = "Another Warden? The last one came back in a sack.";
        talk.choices = {"I'm here about the keep.", "[Speech 5] Surely the keep can pay more.", "Goodbye."};
        docs.SetDialogue(talk);
    } else {
        docs.SetDialogue(std::nullopt);
    }
    docs.SetMenu(uiPreview_ == "hud" || uiPreview_ == "dialogue" ? std::nullopt : std::optional<MenuView>(MockMenu(uiPreview_)),
                 uiPreview_);
    docs.Render(ui);
    for (const auto& texture : docs.TakeNewTextures()) renderer_.RegisterTexture(texture.name, texture.image);
}

nlohmann::json EditorApp::UiJson() {
    DocumentUi& docs = UiDocs();
    const fs::path root = assets_.GetProject().root;
    // Every screen the engine knows, with where its document comes from.
    nlohmann::json screens = nlohmann::json::array();
    std::set<std::string> ids;
    for (const auto& s : docs.Screens()) {
        ids.insert(s.id);
        screens.push_back({{"id", s.id}, {"document", s.document}, {"layer", s.layer}, {"pauses", s.pauses},
                           {"source", docs.SourceOf(s.id)}});
    }
    std::error_code ec;
    for (const auto& file : fs::directory_iterator(root / "ui" / "screens", ec)) {
        const std::string id = file.path().stem().string();
        if (file.path().extension() == ".rml" && !ids.contains(id)) {
            screens.push_back({{"id", id}, {"document", "screens/" + id + ".rml"}, {"layer", "window"}, {"pauses", true},
                               {"source", "project"}, {"custom", true}});
        }
    }
    nlohmann::json theme = nlohmann::json::object();
    if (std::ifstream in(root / "ui" / "theme.json", std::ios::binary); in) theme = nlohmann::json::parse(in, nullptr, false);
    if (!theme.is_object()) theme = nlohmann::json::object();
    nlohmann::json rules;
    for (const fs::path& file : {root / "ui" / "notifications.json", EngineUiDir() / "notifications.json"}) {
        if (std::ifstream in(file, std::ios::binary); in) {
            std::stringstream text;
            text << in.rdbuf();
            rules = {{"text", text.str()}, {"project", file.parent_path().parent_path() == root}};
            break;
        }
    }
    nlohmann::json problems = nlohmann::json::array();
    for (auto& p : docs.TakeProblems()) problems.push_back(p);
    return {{"type", "ui:model"},
            {"screens", screens},
            {"themes", docs.Themes()},
            {"theme", theme},
            {"tokens", docs.ThemeTokens()},
            {"notifications", rules},
            {"problems", problems},
            {"preview", uiPreview_},
            {"documents", ActiveSettings().ui.documents}};
}

bool EditorApp::HandleUiMessage(const std::string& type, const nlohmann::json& message) {
    const fs::path root = assets_.GetProject().root;
    // A file the tab may write: under ui/, a document, style or settings file.
    auto safe = [](const std::string& path) {
        const fs::path p = fs::path(path).lexically_normal();
        const std::string ext = p.extension().string();
        return !p.is_absolute() && p.generic_string().rfind("ui/", 0) == 0 && p.generic_string().find("..") == std::string::npos &&
               (ext == ".rml" || ext == ".rcss" || ext == ".json");
    };
    auto write = [&](const std::string& path, const std::string& text) -> std::optional<std::string> {
        if (!safe(path)) return "UI files live under ui/ (.rml, .rcss, .json)";
        if (fs::path(path).extension() == ".json" && nlohmann::json::parse(text, nullptr, false).is_discarded()) {
            return path + " isn't valid JSON";
        }
        std::error_code ec;
        fs::create_directories((root / path).parent_path(), ec);
        std::ofstream out(root / path, std::ios::binary | std::ios::trunc);
        if (!out) return "couldn't write " + path;
        out << text;
        return std::nullopt;
    };
    auto reload = [&] {
        UiDocs().Reload();
        post_(UiJson());
    };
    if (type == "ui:request") {
        post_(UiJson());
    } else if (type == "ui:source") {
        // The document a screen uses: the project's copy, else the engine's.
        const std::string document = message.value("document", "");
        const fs::path project = root / "ui" / document, engine = EngineUiDir() / document;
        std::error_code ec;
        const bool own = fs::exists(project, ec);
        std::ifstream in(own ? project : engine, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        post_({{"type", "ui:source"}, {"document", document}, {"text", text.str()}, {"project", own}});
    } else if (type == "ui:customise") {
        // Copy the engine's document into the project (it then wins) and point the manifest at it.
        const std::string id = message.value("id", "");
        const std::string document = message.value("document", "screens/menu.rml");
        const std::string target = "screens/" + id + ".rml";
        std::error_code ec;
        if (!fs::exists(root / "ui" / target, ec)) {
            fs::create_directories(root / "ui" / "screens", ec);
            fs::copy_file(fs::exists(root / "ui" / document, ec) ? root / "ui" / document : EngineUiDir() / document,
                          root / "ui" / target, ec);
        }
        nlohmann::json manifest = {{"schema", "aige.ui-screens/1"}, {"screens", nlohmann::json::array()}};
        if (std::ifstream in(root / "ui" / "screens.json", std::ios::binary); in) {
            const auto json = nlohmann::json::parse(in, nullptr, false);
            if (json.is_object() && json.contains("screens")) manifest = json;
        }
        bool found = false;
        for (auto& s : manifest["screens"]) {
            if (s.value("id", "") == id) {
                s["document"] = target;
                found = true;
            }
        }
        if (!found) manifest["screens"].push_back({{"id", id}, {"document", target}});
        if (auto error = write("ui/screens.json", manifest.dump(2) + "\n")) post_({{"type", "notify"}, {"text", *error}});
        reload();
    } else if (type == "ui:save") {
        if (auto error = write(message.value("path", ""), message.value("text", ""))) {
            post_({{"type", "notify"}, {"text", *error}});
        }
        reload();
    } else if (type == "ui:theme") {
        nlohmann::json theme = {{"base", message.value("base", "stone")},
                                {"tokens", message.value("tokens", nlohmann::json::object())}};
        if (auto error = write("ui/theme.json", theme.dump(2) + "\n")) post_({{"type", "notify"}, {"text", *error}});
        reload();
    } else if (type == "ui:preview") {
        uiPreview_ = playing_ ? std::string() : message.value("id", "");
        if (uiPreview_.empty() && uiDocs_) {
            uiDocs_->SetMenu(std::nullopt, "");
            uiDocs_->SetHud(HudData{false});
            uiDocs_->SetDialogue(std::nullopt);
        }
    } else if (type == "ui:check") {
        // Show every screen once with sample data and collect what RmlUi reports.
        DocumentUi& docs = UiDocs();
        docs.Reload();
        docs.TakeProblems();
        nlohmann::json results = nlohmann::json::array();
        std::vector<std::string> ids;
        for (const auto& s : docs.Screens()) ids.push_back(s.id);
        for (const auto& id : ids) {
            std::vector<UiQuad> scratch;
            const std::string saved = uiPreview_;
            uiPreview_ = id;
            ShowUiPreview(scratch, 1280, 720);
            uiPreview_ = saved;
            for (const auto& p : docs.TakeProblems()) results.push_back({{"screen", id}, {"message", p}});
        }
        docs.SetMenu(std::nullopt, "");
        docs.SetHud(HudData{false});
        docs.SetDialogue(std::nullopt);
        post_({{"type", "ui:checked"}, {"problems", results}});
    } else {
        return false;
    }
    return true;
}

nlohmann::json EditorApp::MagicJson() {
    ActiveRecords().Refresh();
    auto merged = [](std::vector<std::string> presets, const char* type) {
        std::vector<std::pair<std::string, std::string>> out;
        std::set<std::string> own;
        for (const Record* r : ActiveRecords().OfType(type)) {
            out.push_back({r->id, "project"});
            own.insert(r->id);
        }
        for (const auto& id : presets) {
            if (!own.contains(id)) out.push_back({id, "engine"});
        }
        return out;
    };
    nlohmann::json abilities = nlohmann::json::array();
    for (const auto& [id, source] : merged(AbilityPresets(), "ability")) {
        auto a = FindAbility(id);
        if (!a) continue;
        const auto f = FiguresOf(*a);
        nlohmann::json effects = nlohmann::json::array();
        for (const auto& use : a->effects) effects.push_back(use.effect);
        abilities.push_back({{"id", id}, {"name", a->name}, {"source", source}, {"kind", a->kind}, {"school", a->school},
                             {"delivery", a->delivery}, {"activation", a->activation}, {"cost", a->cost},
                             {"costAttribute", a->costAttribute}, {"cooldown", a->cooldown}, {"effects", effects},
                             {"formulaCost", f.formulaCost}, {"output", f.output}, {"outputPerCost", f.outputPerCost},
                             {"outputPerSecond", f.outputPerSecond}, {"outlier", f.outlier},
                             {"ai", a->ai ? a->ai->use + (a->ai->when.empty() ? "" : " when " + a->ai->when) : ""},
                             {"problems", ValidateAbility(*a)}});
    }
    nlohmann::json effects = nlohmann::json::array();
    for (const auto& [id, source] : merged(MagicEffectPresets(), "magic-effect")) {
        auto e = FindMagicEffect(id);
        if (!e) continue;
        effects.push_back({{"id", id}, {"source", source}, {"archetype", e->archetype}, {"magnitude", e->magnitude},
                           {"duration", e->duration}, {"tags", e->tags}});
    }
    return {{"type", "magic:model"}, {"abilities", abilities}, {"effects", effects}};
}

nlohmann::json EditorApp::StoryGraphJson(const std::string& notice) {
    const fs::path root = assets_.GetProject().root;
    const StoryOntology ontology = StoryOntology::ForProject(root);
    auto graph = StoryGraph::Load(StoryGraph::PathIn(root));
    nlohmann::json nodes = nlohmann::json::array();
    nlohmann::json issues = nlohmann::json::array();
    if (graph) {
        std::vector<const StoryNode*> sorted;
        for (const auto& n : graph->nodes) sorted.push_back(&n);
        std::sort(sorted.begin(), sorted.end(), [&](const StoryNode* a, const StoryNode* b) {
            const auto* ca = ontology.Class(a->cls);
            const auto* cb = ontology.Class(b->cls);
            const std::string la = ca ? ca->layer : "", lb = cb ? cb->layer : "";
            return std::tie(la, a->cls, a->id) < std::tie(lb, b->cls, b->id);
        });
        for (const auto* n : sorted) {
            const auto* cls = ontology.Class(n->cls);
            std::string label = n->Text("name");
            if (label.empty()) label = n->id;
            nodes.push_back({{"id", n->id}, {"class", n->cls}, {"label", label}, {"layer", cls ? cls->layer : "?"}});
        }
        issues = IssuesJson(CheckStory(*graph, ontology));
    }
    return {{"type", "storygraph:model"}, {"exists", graph.has_value()}, {"revision", graph ? graph->revision : 0},
            {"nodes", nodes}, {"issues", issues}, {"notice", notice}};
}

bool EditorApp::HandleStoryGraphMessage(const std::string& type, const nlohmann::json& message) {
    const fs::path root = assets_.GetProject().root;
    if (type == "storygraph:request") {
        post_(StoryGraphJson());
    } else if (type == "storygraph:node") {
        auto graph = StoryGraph::Load(StoryGraph::PathIn(root));
        const std::string id = message.value("id", "");
        const StoryNode* node = graph ? graph->Find(id) : nullptr;
        if (!node) return true;
        nlohmann::json detail = {{"type", "storygraph:detail"}, {"id", id}, {"class", node->cls}, {"fields", node->fields},
                                 {"timeline", graph->Timeline(id)}, {"dependents", graph->Dependents(id)}, {"madeBy", graph->MadeBy(id)}};
        if (node->cls == "StoryScene") detail["subgraph"] = graph->Subgraph(id);
        post_(detail);
    } else if (type == "storygraph:sync") {
        // Hand-written rel: lines in, the graph's end-of-story relations out (story-ontology.md §6.2).
        const auto path = StoryGraph::PathIn(root);
        auto graph = StoryGraph::Load(path);
        if (!graph) {
            post_(StoryGraphJson("No story graph to sync."));
            return true;
        }
        world_.Refresh();
        const int in = graph->ReadRelationsFrom(world_);
        if (in) graph->Save(path);
        const int out = graph->WriteRelationsTo(world_);
        post_(StoryGraphJson(std::to_string(in) + " World Bible relation(s) read in, " + std::to_string(out) + " entr" +
                             (out == 1 ? "y" : "ies") + " updated."));
    } else {
        return false;
    }
    return true;
}

std::unique_ptr<GamePlan> EditorApp::MakePlan() {
    GamePlan::Host host;
    host.request = [this](const std::string& path, const nlohmann::json& body, int timeout) {
        return Ai() && Ai()->GetState() == AiClient::State::Ready ? Ai()->Request("POST", path, body, timeout) : 0;
    };
    host.projectRoot = assets_.GetProject().root;
    host.places = [this]() -> const Ontology& { return Places(); };
    nlohmann::json themes = nlohmann::json::array(), buildTypes = nlohmann::json::array(), recordTypes = nlohmann::json::object();
    std::error_code ec;
    for (const auto& file : fs::directory_iterator(EngineUiDir() / "themes", ec)) {
        if (file.path().extension() == ".json") themes.push_back(file.path().stem().string());
    }
    for (const auto& [id, b] : Places().Builds()) buildTypes.push_back(id);
    for (const auto& t : EngineReflection().RecordTypes()) {
        nlohmann::json fields = nlohmann::json::array();
        for (const auto& f : t.fields) fields.push_back(f.name);
        recordTypes[t.name] = fields;
    }
    host.catalog = {{"packageTemplates", ProcedureNames()}, {"themes", themes}, {"buildTypes", buildTypes},
                    {"recordTypes", recordTypes}, {"abilities", AbilityPresets()}, {"effects", MagicEffectPresets()}};
    host.validateRecord = [](const std::string& type, const nlohmann::json& record) -> std::vector<std::string> {
        for (const auto& t : EngineReflection().RecordTypes()) {
            if (t.name == type) return ValidateRecord(t, record, nullptr);
        }
        return {"there's no record type '" + type + "'"};
    };
    return std::make_unique<GamePlan>(std::move(host));
}

int EditorApp::PlannerBusy() const {
    int busy = plan_ ? plan_->Busy() : 0;
    if (gameBuildJob_ || (gameBuild_ && gameBuild_->Status() == "working")) ++busy; // ⑩–⑬ building the game
    std::lock_guard lock(plannerBuildMutex_);
    if (plannerBuild_ && plannerBuild_->status != "built" && plannerBuild_->status.rfind("failed", 0) != 0) ++busy;
    return busy;
}

void EditorApp::StepPlannerPlaceBuild() {
    std::string id, scene, status;
    {
        std::lock_guard lock(plannerBuildMutex_);
        if (!plannerBuild_) return;
        id = plannerBuild_->buildId, scene = plannerBuild_->scene, status = plannerBuild_->status;
    }
    auto set = [&](const std::string& next) {
        std::lock_guard lock(plannerBuildMutex_);
        if (plannerBuild_ && plannerBuild_->buildId == id) plannerBuild_->status = next;
        if (next == "built" || next.rfind("failed", 0) == 0) gameBuildDirty_ = true; // the game build goes on
    };
    if (status == "queued") {
        if (playing_ || buildJob_ || buildRun_) return; // the editor is busy: a moment
        // Its own scene, empty: the game build's (an earlier try's place in it would stay beside
        // the new one). The open scene is saved first; one already open is closed and made anew.
        if (dirty_) Save();
        const std::filesystem::path full = assets_.GetProject().Resolve(scene);
        std::error_code ec;
        std::filesystem::create_directories(full.parent_path(), ec);
        {
            Registry empty;
            SaveScene(empty, full.string());
        }
        if (OpenSceneRelative() == scene) {
            LoadScene(registry_, full.string());
            history_.Clear();
            dirty_ = false;
            sceneTreeStale_ = editorStateStale_ = true;
        }
        OpenScene(scene);
        if (OpenSceneRelative() != scene) return set("failed: couldn't open " + scene);
        // The place's build, in the Build panel (where it shows while it works).
        ResumeBuild(id);
        if (!build_ || build_->Id() != id) return set("failed: couldn't open the place's build");
        post_({{"type", "build:open"}}); // the Build panel shows, so it can be watched
        // The place sketched first (docs/build-fixes.md): one painting from the plan's words, each piece
        // found in it and cropped, the crop the reference its art is drawn from.
        if (!build_->Sketched() && Ai() && Ai()->GetState() == AiClient::State::Ready) {
            PlaceSceneOptions options;
            options.place = build_->Id();
            options.iso = iso_;
            options.isoGrid = ActiveSettings().grid.style != GridStyle::TopDown;
            const AssetSourcing sourcing = SourcingNow();
            const nlohmann::json plan = PlanPlaceAssets(Places(), build_->Spec(), build_->Solved(), BuildCatalog(assets_, &Places(), CatalogOptionsNow()),
                                                        prefabs_, assets_, ProjectTerrainSet(prefabs_, assets_.GetProject()), options, &sourcing);
            if (build_->Sketch(SketchRequests(*build_, plan, &Places())).empty()) return set("sketching");
        }
        // ③ Assets: each part's art found, or made by the Art tool.
        FindAssets();
        return set("sourcing");
    }
    if (!build_ || build_->Id() != id) return set("failed: its build was closed in the Build panel");
    if (status == "sketching") {
        if (build_->Sketching()) return; // the place is still being painted
        FindAssets();
        return set("sourcing");
    }
    if (status == "sourcing") {
        if (build_->Stage() == "sourcing" || buildJob_) return; // art still coming
        BuildIntoScene(); // ④ Build: the scene, checked and repaired
        if (!buildRun_) return set("failed: the Build panel couldn't start building it");
        return set("building");
    }
    if (status == "building") {
        if (buildRun_) return;
        if (build_->Stage() != "built") return set("failed: " + build_->State().value("status", std::string("its build stopped")));
        if (build_->Inspecting()) return; // the vision model is looking at it
        if (!build_->ArtToRedo().empty()) {
            // Art the check refused: found or made again (told why), then built again.
            build_->ArtRedone();
            FindAssets();
            return set("sourcing");
        }
        Save();
        // Its art (made or fetched) into the user's library, if it isn't there yet.
        if (const int kept = UserLibrary(UserLibraryDir()).KeepGenerated(assets_.GetProject().root); kept > 0) {
            post_({{"type", "notify"}, {"text", std::to_string(kept) + " new piece(s) of art kept in your library"}});
        }
        return set("built");
    }
}

std::unique_ptr<GameBuild> EditorApp::MakeGameBuild() {
    GameBuild::Host host;
    host.buildInEditor = [this](const std::string& buildId, const std::string& scene) -> std::string {
        std::lock_guard lock(plannerBuildMutex_);
        if (plannerBuild_ && plannerBuild_->status != "built" && plannerBuild_->status.rfind("failed", 0) != 0 && plannerBuild_->buildId != buildId)
            return "the Build panel is still building another place";
        plannerBuild_ = PlannerPlaceBuild{buildId, scene, "queued"};
        return {};
    };
    host.editorBuildStatus = [this](const std::string& buildId) -> std::string {
        std::lock_guard lock(plannerBuildMutex_);
        if (!plannerBuild_ || plannerBuild_->buildId != buildId) return "failed: the editor has no such build";
        const std::string s = plannerBuild_->status;
        return s == "built" || s.rfind("failed", 0) == 0 ? s : "working";
    };
    host.request = [this](const std::string& path, const nlohmann::json& body, int timeout) {
        return Ai() && Ai()->GetState() == AiClient::State::Ready ? Ai()->Request("POST", path, body, timeout) : 0;
    };
    host.projectRoot = assets_.GetProject().root;
    host.places = [this]() -> const Ontology& { return Places(); };
    return std::make_unique<GameBuild>(std::move(host));
}

void EditorApp::ApplyArtFromBrief() {
    if (!plan_) return;
    const nlohmann::json brief = plan_->Document("brief");
    const nlohmann::json art = BriefGet(brief, "plan.art");
    if (!art.is_object()) return;
    SourcingSettings settings = ReadSourcingSettings(assets_.GetProject().root);
    if (art.value("source", nlohmann::json()).is_string()) settings.defaultSource = art["source"].get<std::string>();
    if (art.value("webSearch", nlohmann::json()).is_boolean()) settings.webSearch = art["webSearch"].get<bool>();
    if (art.value("shareAlike", nlohmann::json()).is_boolean()) settings.licences.shareAlike = art["shareAlike"].get<bool>();
    WriteSourcingSettings(assets_.GetProject().root, settings);
}

void EditorApp::StopGameBuildNow() {
    stopGameBuild_ = false;
    {
        std::lock_guard lock(plannerBuildMutex_);
        plannerBuild_.reset(); // the place in the Build panel: let go (what it's doing now just finishes)
    }
    if (gameBuild_) gameBuild_->Stop();
    post_({{"type", "notify"}, {"text", "Builds stopped"}});
}

void EditorApp::RestartGameBuildNow() {
    restartGameBuild_ = false;
    {
        std::lock_guard lock(plannerBuildMutex_);
        plannerBuild_.reset();
    }
    // Every place again, from the plan, with the art setting as it is now.
    gameBuild_ = MakeGameBuild();
    if (const std::string why = gameBuild_->Start(); !why.empty()) {
        post_({{"type", "notify"}, {"text", "Can't restart the builds: " + why}});
        return;
    }
    gameBuildDirty_ = true;
    post_({{"type", "notify"}, {"text", "Building every place again"}});
}

void EditorApp::StepGameBuild() {
    if (gameBuildJob_ && gameBuildJob_->done) {
        gameBuildJob_->thread.join();
        const std::string error = gameBuildJob_->error;
        gameBuildJob_.reset();
        if (!error.empty()) post_({{"type", "notify"}, {"text", "Building the game: " + error}});
        RefreshAssets();
        PostPlanner();
    }
    if (!gameBuildJob_ && stopGameBuild_) {
        StopGameBuildNow();
        PostPlanner();
    }
    if (!gameBuildJob_ && restartGameBuild_) {
        RestartGameBuildNow();
        PostPlanner();
    }
    if (!gameBuild_ || gameBuildJob_) return;
    while (!gameBuildAnswers_.empty()) {
        const auto [id, body] = gameBuildAnswers_.front();
        gameBuildAnswers_.pop_front();
        gameBuild_->OnResponse(id, body);
    }
    if (!gameBuildDirty_ || gameBuild_->Status() != "working") return;
    gameBuildDirty_ = false;
    gameBuildJob_ = std::make_unique<GameBuildJob>();
    GameBuildJob* job = gameBuildJob_.get();
    GameBuild* build = gameBuild_.get();
    job->thread = std::thread([job, build] {
        try {
            for (int i = 0; i < 64 && build->Pump(); ++i) {
            }
        } catch (const std::exception& e) {
            job->error = e.what();
        }
        job->done = true;
    });
    // Whatever it does next (an answer, a user action) pumps again.
}

bool EditorApp::ArchivePlan() {
    // The project's plan kept as an earlier one (planner-archive/<when>/), out of the way of a new or reopened plan.
    const fs::path root = assets_.GetProject().root;
    if (!PlannerStore(root).HasPlan()) return false;
    plan_.reset();
    gameBuild_.reset();
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    localtime_s(&tm, &now);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
    std::error_code ec;
    fs::create_directories(root / "planner-archive", ec);
    fs::rename(root / "planner", root / "planner-archive" / stamp, ec);
    return !ec;
}

nlohmann::json EditorApp::ListPlans() const {
    // [{id ("" the current), request, stage, updated (seconds)}]: the current first, then the earlier ones, latest first.
    const fs::path root = assets_.GetProject().root;
    nlohmann::json plans = nlohmann::json::array();
    auto add = [&](const std::string& id, const fs::path& dir) {
        std::ifstream in(dir / "state.json");
        const nlohmann::json s = in ? nlohmann::json::parse(in, nullptr, false) : nlohmann::json();
        if (!s.is_object()) return;
        std::error_code ec;
        const auto time = fs::last_write_time(dir / "state.json", ec);
        const long long updated = ec ? 0 : std::chrono::duration_cast<std::chrono::seconds>(std::chrono::clock_cast<std::chrono::system_clock>(time).time_since_epoch()).count();
        plans.push_back({{"id", id}, {"request", s.value("request", "")}, {"stage", s.value("stage", "")}, {"updated", updated},
                         {"name", s.value("concept", nlohmann::json::object()).value("title", "")}});
    };
    if (PlannerStore(root).HasPlan()) add("", root / "planner");
    std::vector<std::string> archived;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(root / "planner-archive", ec)) {
        if (e.is_directory()) archived.push_back(e.path().filename().string());
    }
    std::sort(archived.rbegin(), archived.rend());
    for (const auto& id : archived) add(id, root / "planner-archive" / id);
    return plans;
}

void EditorApp::StartPlan(const std::string& request, bool replace, const nlohmann::json& choices) {
    if (PlannerStore(assets_.GetProject().root).HasPlan() && !replace) {
        post_({{"type", "planner:open"}});
        post_({{"type", "planner:askReplace"}, {"request", request}, {"choices", choices}});
        return;
    }
    ArchivePlan(); // the plan it replaces is kept, on the home page
    plan_ = MakePlan();
    const std::string why = plan_->Start(request, choices);
    post_({{"type", "ai:message"}, {"role", "assistant"},
           {"text", why.empty() ? "Planning the game in the Planner panel: confirm the Game Brief there when it's ready."
                                : "Couldn't start the plan: " + why}});
    post_({{"type", "planner:open"}});
    PostPlanner();
}

void EditorApp::PostPlanner() {
    nlohmann::json state;
    try {
        state = plan_ ? plan_->State() : nlohmann::json{{"active", false}};
    } catch (const std::exception& error) {
        // A document of the wrong shape: say so (Retry, Regenerate stay possible), don't crash.
        state = {{"active", true}, {"status", "failed"}, {"message", std::string("The plan couldn't be shown: ") + error.what()},
                 {"stages", nlohmann::json::array()}};
    }
    state["type"] = "planner:state";
    if (!gameBuild_ && fs::exists(assets_.GetProject().root / "planner" / "buildstate.json")) {
        gameBuild_ = MakeGameBuild();
        if (gameBuild_->Open().empty()) gameBuildDirty_ = true;
    }
    if (gameBuild_ && !gameBuildJob_) state["build"] = gameBuild_->State();
    else if (gameBuildJob_) state["build"] = {{"active", true}, {"status", "working"}, {"message", "Working (building places, writing files)…"}};
    state["hasPlan"] = PlannerStore(assets_.GetProject().root).HasPlan();
    state["plans"] = ListPlans(); // the home page: this plan and the earlier ones
    if (planChoices_.is_null()) planChoices_ = BriefChoices(StoryOntology::ForProject(assets_.GetProject().root));
    state["choices"] = planChoices_; // and its Choices
    // How the places' builds find their art (the project's setting, the Build panel's too).
    const SourcingSettings sourcing = ReadSourcingSettings(assets_.GetProject().root);
    state["sourcing"] = {{"webSearch", sourcing.webSearch}, {"shareAlike", sourcing.licences.shareAlike}, {"defaultSource", sourcing.defaultSource}};
    post_(state);
    nlohmann::json card;
    try {
        card = plan_ ? plan_->Card() : nlohmann::json();
    } catch (const std::exception&) {
        card = nlohmann::json(); // the AI panel's card goes without; the panel's state went already
    }
    post_({{"type", "planner:card"}, {"card", card}});
}

bool EditorApp::HandlePlannerMessage(const std::string& type, const nlohmann::json& message) {
    const fs::path root = assets_.GetProject().root;
    if (type == "planner:start") {
        StartPlan(message.value("request", ""), message.value("replace", false), message.value("choices", nlohmann::json::object()));
        return true;
    }
    if (type == "planner:show") {
        post_({{"type", "planner:open"}});
        PostPlanner();
        return true;
    }
    if (type == "planner:openPlan" || type == "planner:removePlan") {
        // The home page: an earlier plan opened (the current one kept as earlier in its place), or removed.
        const std::string id = message.value("id", "");
        if (id.empty() || id.find_first_of("/\\.") != std::string::npos || !fs::is_directory(root / "planner-archive" / id)) return true;
        if (gameBuildJob_) {
            post_({{"type", "notify"}, {"text", "The game is still being built: stop it first"}});
            return true;
        }
        std::error_code ec;
        if (type == "planner:removePlan") {
            fs::remove_all(root / "planner-archive" / id, ec);
            post_({{"type", "notify"}, {"text", ec ? "Couldn't remove the plan: " + ec.message() : "Removed the plan"}});
        } else {
            ArchivePlan();
            fs::rename(root / "planner-archive" / id, root / "planner", ec);
            if (ec) {
                post_({{"type", "notify"}, {"text", "Couldn't open the plan: " + ec.message()}});
            } else {
                plan_ = MakePlan();
                if (const std::string why = plan_->Open(); !why.empty()) post_({{"type", "notify"}, {"text", "Can't open the plan: " + why}});
                post_({{"type", "planner:open"}});
            }
        }
        PostPlanner();
        return true;
    }
    if (type == "planner:remove") {
        // The project's plan, from the Planner's start page (the panel asked first): its documents and history.
        if (gameBuildJob_) {
            post_({{"type", "notify"}, {"text", "The game is still being built: stop it first"}});
            return true;
        }
        plan_.reset();
        gameBuild_.reset();
        std::error_code ec;
        fs::remove_all(PlannerStore(root).Dir(), ec);
        post_({{"type", "notify"}, {"text", ec ? "Couldn't remove the plan: " + ec.message() : "Removed the plan"}});
        PostPlanner();
        return true;
    }
    // A plan left in the project carries on when the panel asks for it.
    if (!plan_ && PlannerStore(root).HasPlan()) {
        plan_ = MakePlan();
        if (const std::string why = plan_->Open(); !why.empty()) post_({{"type", "notify"}, {"text", "Can't open the plan: " + why}});
    }
    if (type == "planner:request") {
        PostPlanner();
        return true;
    }
    if (!plan_) {
        post_({{"type", "notify"}, {"text", "There's no plan yet: describe the game in the Planner panel."}});
        return true;
    }
    std::string why;
    if (type == "planner:editBrief") {
        const std::string path = message.value("path", "");
        plan_->EditBrief(path, message.value("value", nlohmann::json()));
        if (path.rfind("plan.art", 0) == 0) ApplyArtFromBrief(); // the builds' art strategy
    }
    else if (type == "planner:confirm") {
        why = plan_->Confirm();
        if (why.empty()) ApplyArtFromBrief(); // the art strategy set at the start: the builds' from the first
    }
    else if (type == "planner:approve" && gameBuild_ && gameBuild_->Active() && gameBuild_->State().value("stage", "") != "done") {
        // The places are being built: Continue goes on with them (not back to ⑨ and its fixes).
        if (gameBuildJob_) return true;
        why = gameBuild_->Resume();
        gameBuildDirty_ = why.empty();
    } else if (type == "planner:approve") why = plan_->Approve();
    else if (type == "planner:pause") plan_->Pause(message.value("on", true));
    else if (type == "planner:edit") {
        why = plan_->Edit(message.value("doc", ""), message.value("pointer", ""), message.value("value", nlohmann::json()), message.value("reason", ""));
    } else if (type == "planner:lock") why = plan_->Lock(message.value("doc", ""), message.value("on", true));
    else if (type == "planner:regenerate") why = plan_->Regenerate(message.value("doc", ""));
    else if (type == "planner:regenerateStale") why = plan_->RegenerateStale();
    else if (type == "planner:retryJob") why = plan_->RetryJob(message.value("id", ""));
    else if (type == "planner:retryFailed") why = plan_->RetryFailed();
    else if (type == "planner:stopFixes") why = plan_->StopFixes();
    else if (type == "planner:sourcing") {
        // The art strategy for the places' builds: the project's sourcing setting (the Build panel's).
        SourcingSettings settings = ReadSourcingSettings(root);
        if (message.contains("webSearch")) settings.webSearch = message.value("webSearch", false);
        if (message.contains("shareAlike")) settings.licences.shareAlike = message.value("shareAlike", false);
        if (message.contains("defaultSource") && message["defaultSource"].is_string()) settings.defaultSource = message["defaultSource"].get<std::string>();
        WriteSourcingSettings(root, settings);
        post_({{"type", "build:sourcing"}, {"webSearch", settings.webSearch}, {"shareAlike", settings.licences.shareAlike},
               {"defaultSource", settings.defaultSource}}); // the Build panel shows it too
    }
    else if (type == "planner:fixFindings") {
        std::vector<std::string> ids;
        for (const auto& id : message.value("ids", nlohmann::json::array())) {
            if (id.is_string()) ids.push_back(id.get<std::string>());
        }
        why = plan_->FixNow(ids);
    }
    else if (type == "planner:accept") why = plan_->AcceptFinding(message.value("finding", ""), message.value("reason", ""));
    else if (type == "planner:restore") why = plan_->Restore(message.value("n", 0));
    else if (type == "planner:why") {
        post_({{"type", "planner:why"}, {"why", plan_->Why(message.value("doc", ""))}});
        return true;
    } else if (type == "planner:document") {
        const std::string doc = message.value("doc", "");
        post_({{"type", "planner:document"}, {"doc", doc}, {"content", plan_->Document(doc)}});
        return true;
    } else if (type == "planner:buildGame") {
        // ⑩–⑬: build every place, bind the quests, write the files, playtest.
        if (gameBuildJob_) {
            post_({{"type", "notify"}, {"text", "The game is still being built: a moment"}});
            return true;
        }
        gameBuild_ = MakeGameBuild();
        why = gameBuild_->Start(message.value("mode", ""));
        gameBuildDirty_ = why.empty();
    } else if (type == "planner:stopBuilds") {
        if (gameBuildJob_) stopGameBuild_ = true; // as soon as its thread is through
        else StopGameBuildNow();
        PostPlanner();
        return true;
    } else if (type == "planner:continueBuilds") {
        // On from the next place that isn't built (those that are stay).
        if (gameBuild_) {
            if (const std::string why = gameBuild_->Resume(); !why.empty()) post_({{"type", "notify"}, {"text", why}});
            else gameBuildDirty_ = true;
        }
        PostPlanner();
        return true;
    } else if (type == "planner:restartBuilds") {
        if (gameBuildJob_) restartGameBuild_ = true;
        else RestartGameBuildNow();
        PostPlanner();
        return true;
    } else if (type == "planner:buildRow") {
        if (gameBuildJob_) {
            post_({{"type", "notify"}, {"text", "The game is still being built: a moment"}});
            return true;
        }
        if (!gameBuild_) return true;
        const std::string place = message.value("place", ""), action = message.value("action", "");
        if (action == "retry") why = gameBuild_->Retry(place);
        else if (action == "rebuild") why = gameBuild_->Rebuild(place);
        else if (action == "placeholder") why = gameBuild_->UsePlaceholder(place);
        else if (action == "manual" || action == "automatic") why = gameBuild_->SetMode(place, action);
        else if (action == "done") why = gameBuild_->ManualDone(place);
        else if (action == "open") {
            const nlohmann::json state = gameBuild_->State();
            for (const auto& row : state["rows"]) {
                if (row.value("place", "") == place && !row.value("build", "").empty()) ResumeBuild(row.value("build", ""));
            }
        }
        gameBuildDirty_ = true;
    } else if (type == "planner:play") {
        // ⑬ Play from the start: the start scene, then Play.
        const std::string start = gameBuild_ ? gameBuild_->State().value("result", nlohmann::json::object()).value("startScene", "") : "";
        if (!start.empty()) {
            OpenScene(start);
            if (!playing_) StartPlay();
        }
    } else if (type == "planner:buildPlace") {
        // 22.10: the place's draft Brief, straight into the Build panel's confirmation window.
        const nlohmann::json place = plan_->Document(message.value("doc", ""));
        if (buildJob_) {
            post_({{"type", "notify"}, {"text", "The build is still working: a moment"}});
            return true;
        }
        build_ = MakeBuild();
        const nlohmann::json brief = place.value("brief", nlohmann::json::object());
        why = build_->StartFromBrief(brief.value("name", place.value("id", "")), brief);
        post_({{"type", "build:open"}});
        PostBuild();
    } else {
        return false;
    }
    if (!why.empty()) post_({{"type", "notify"}, {"text", why}});
    PostPlanner();
    // A user's edit to a document open in the panel: show it as it is now.
    if (type == "planner:edit" || type == "planner:lock") {
        const std::string doc = message.value("doc", "");
        post_({{"type", "planner:document"}, {"doc", doc}, {"content", plan_->Document(doc)}});
    }
    return true;
}

bool EditorApp::HandleMagicMessage(const std::string& type, const nlohmann::json& message) {
    if (type == "magic:request") {
        post_(MagicJson());
    } else if (type == "magic:customise") {
        // The engine's preset as the project's record (to change in Records).
        const std::string kind = message.value("kind", "ability");
        const std::string id = message.value("id", "");
        const fs::path preset = fs::path(AIGE_MAGIC_PRESETS) / (kind == "ability" ? "abilities" : "effects") / (id + ".json");
        std::ifstream in(preset, std::ios::binary);
        nlohmann::json data = nlohmann::json::parse(in, nullptr, false);
        if (data.is_discarded()) data = nlohmann::json::object();
        data.erase("schema");
        ActiveRecords().Refresh();
        if (auto error = ActiveRecords().Write(kind, id, data)) post_({{"type", "notify"}, {"text", *error}});
        post_(MagicJson());
    } else if (type == "magic:try") {
        // Play, learn it and cast it at the nearest enemy (or straight ahead).
        if (!playing_) StartPlay();
        if (!playing_) return true;
        const auto player = FindPlayer(registry_);
        if (!player) {
            post_({{"type", "notify"}, {"text", "The scene has no player."}});
            return true;
        }
        const std::string id = message.value("id", "");
        auto& resources = registry_.get_or_emplace<Resources>(*player);
        resources.mana = std::max(resources.mana, 999.0f); // enough to try anything
        resources.stamina = std::max(resources.stamina, 999.0f);
        session_.Magic().Learn(*player, id);
        auto& abilities = registry_.get<Abilities>(*player);
        auto slots = SplitList(abilities.slots);
        slots.erase(std::remove(slots.begin(), slots.end(), id), slots.end());
        slots.insert(slots.begin(), id);
        std::string joined;
        for (const auto& s : slots) joined += (joined.empty() ? "" : ", ") + s;
        abilities.slots = joined;
        std::string why;
        if (!session_.CastSlot(0, &why)) post_({{"type", "notify"}, {"text", "Couldn't cast it: " + why}});
    } else {
        return false;
    }
    return true;
}

nlohmann::json EditorApp::FxJson() {
    session_.ReloadAnimations();
    const AnimLibrary& library = session_.Animations();
    nlohmann::json sheets = nlohmann::json::array();
    for (const auto& sheet : library.Sheets()) {
        nlohmann::json clips = nlohmann::json::array();
        for (const auto& [name, clip] : sheet.clips) {
            nlohmann::json directions = nlohmann::json::object();
            for (const auto& [dir, frames] : clip.directions) {
                nlohmann::json list = nlohmann::json::array();
                for (const auto& f : frames) list.push_back({{"x", f.x}, {"y", f.y}, {"w", f.w}, {"h", f.h}, {"duration", f.duration}});
                directions[dir.empty() ? "-" : dir] = list;
            }
            clips.push_back({{"name", name}, {"loop", clip.loop}, {"directions", directions}, {"mirrors", clip.mirrors},
                             {"events", clip.events.size()}});
        }
        sheets.push_back({{"path", sheet.path}, {"texture", sheet.texture}, {"size", {sheet.textureWidth, sheet.textureHeight}},
                          {"clips", clips}});
    }
    auto listOf = [](const std::vector<std::string>& presets, const std::string& type) {
        nlohmann::json out = nlohmann::json::array();
        std::set<std::string> seen;
        for (const Record* r : ActiveRecords().OfType(type)) {
            out.push_back({{"id", r->id}, {"source", "project"}});
            seen.insert(r->id);
        }
        for (const auto& id : presets) {
            if (!seen.contains(id)) out.push_back({{"id", id}, {"source", "engine"}});
        }
        return out;
    };
    nlohmann::json cutscenes = nlohmann::json::array();
    for (const auto& id : ListCutscenes(assets_.GetProject().root)) {
        std::string error;
        auto c = LoadCutscene(assets_.GetProject().root, id, &error);
        cutscenes.push_back({{"id", id}, {"name", c ? c->name : std::string()}, {"seconds", c ? c->Length() : 0.0f},
                             {"problems", c ? ValidateCutscene(*c) : std::vector<std::string>{error}}});
    }
    return {{"type", "fx:model"},
            {"sheets", sheets},
            {"problems", library.Problems()},
            {"animators", BuiltInAnimatorIds()},
            {"particles", listOf(ParticlePresets(), "particles")},
            {"effects", listOf(EffectPresets(), "effect")},
            {"cutscenes", cutscenes}};
}

bool EditorApp::HandleFxMessage(const std::string& type, const nlohmann::json& message) {
    const fs::path root = assets_.GetProject().root;
    auto writeJson = [&](const std::string& relative, const nlohmann::json& json) -> std::optional<std::string> {
        std::error_code ec;
        fs::create_directories((root / relative).parent_path(), ec);
        std::ofstream out(root / relative, std::ios::binary | std::ios::trunc);
        if (!out) return "couldn't write " + relative;
        out << json.dump(2) << "\n";
        return std::nullopt;
    };
    if (type == "fx:request") {
        ActiveRecords().Refresh();
        post_(FxJson());
    } else if (type == "fx:import-aseprite") {
        // An Aseprite JSON export in the project: a sheet beside it (<name>.anim.json).
        const std::string path = message.value("path", "");
        std::ifstream in(root / path, std::ios::binary);
        const nlohmann::json aseprite = nlohmann::json::parse(in, nullptr, false);
        if (aseprite.is_discarded()) {
            post_({{"type", "notify"}, {"text", path + " isn't JSON"}});
            return true;
        }
        std::string texture = message.value("texture", "");
        if (texture.empty()) {
            const std::string image = aseprite.value("meta", nlohmann::json::object()).value("image", "");
            texture = (fs::path(path).parent_path() / image).generic_string();
        }
        const std::string out = (fs::path(path).parent_path() / (fs::path(path).stem().string() + ".anim.json")).generic_string();
        if (auto error = writeJson(out, AnimSheetFromAseprite(aseprite, texture))) post_({{"type", "notify"}, {"text", *error}});
        post_(FxJson());
    } else if (type == "fx:import-grid") {
        const std::string texture = message.value("texture", "");
        std::vector<std::string> rows;
        for (const auto& r : message.value("rows", nlohmann::json::array())) rows.push_back(r.get<std::string>());
        const std::string out = message.value("out", (fs::path(texture).parent_path() / (fs::path(texture).stem().string() + ".anim.json")).generic_string());
        const auto sheet = AnimSheetFromGrid(texture, message.value("width", 32), message.value("height", 32),
                                             message.value("columns", 4), rows, message.value("fps", 8.0f));
        if (auto error = writeJson(out, sheet)) post_({{"type", "notify"}, {"text", *error}});
        post_(FxJson());
    } else if (type == "fx:try") {
        // An effect or particles at the middle of the viewport, to see them while editing.
        int width = 0, height = 0;
        viewport_.GetFramebufferSize(width, height);
        float sceneX = 0, sceneY = 0, worldX = 0, worldY = 0;
        camera_.ViewportToScene(width * 0.5f, height * 0.5f, sceneX, sceneY);
        UnprojectIso(sceneX, sceneY, iso_, worldX, worldY);
        const std::string id = message.value("id", "");
        bool ok = false;
        if (message.value("kind", "effect") == "effect") {
            ok = session_.PlayEffect(id, worldX, worldY);
        } else if (auto def = FindParticles(id)) {
            session_.Particles().Emit(*def, worldX, worldY, def->burst > 0 ? 0.0f : 2.0f);
            ok = true;
        }
        if (!ok) post_({{"type", "notify"}, {"text", "No " + message.value("kind", "effect") + " '" + id + "'"}});
    } else if (type == "fx:customise") {
        // The engine's preset as the project's own record, to change in Records.
        const std::string kind = message.value("kind", "particles");
        const std::string id = message.value("id", "");
        std::ifstream in(FxPresetPath(kind == "effect" ? "effects" : "particles", id), std::ios::binary);
        nlohmann::json data = nlohmann::json::parse(in, nullptr, false);
        if (data.is_discarded()) data = nlohmann::json::object();
        data.erase("schema");
        ActiveRecords().Refresh();
        if (auto error = ActiveRecords().Write(kind == "effect" ? "effect" : "particles", id, data)) {
            post_({{"type", "notify"}, {"text", *error}});
        }
        post_(FxJson());
    } else if (type == "fx:cutscene") {
        std::string error;
        auto c = LoadCutscene(root, message.value("id", ""), &error);
        post_({{"type", "fx:cutscene"}, {"id", message.value("id", "")},
               {"cutscene", c ? CutsceneToJson(*c) : nlohmann::json(nullptr)}, {"error", error}});
    } else if (type == "fx:cutscene-save") {
        const std::string id = message.value("id", "");
        if (auto error = WriteCutsceneFile(root, id, message.value("cutscene", nlohmann::json::object()))) {
            post_({{"type", "notify"}, {"text", *error}});
        }
        post_(FxJson());
    } else if (type == "fx:cutscene-play") {
        // Play the game from here with the cutscene running (the timeline editor's Play).
        if (!playing_) StartPlay();
        if (playing_ && !session_.PlayCutscene(message.value("id", ""))) {
            post_({{"type", "notify"}, {"text", "The cutscene didn't load."}});
        }
    } else {
        return false;
    }
    return true;
}

Camera2D EditorApp::FxFrame(std::vector<SpriteItem>& items) {
    static bool registered = false;
    if (!registered) {
        for (const auto& [name, image] : FxTextures()) renderer_.RegisterTexture(name, image);
        registered = true;
    }
    session_.AppendFxSprites(items); // also an effect being tried while editing
    if (session_.PhysicsDebug()) session_.AppendPhysicsDebugSprites(items); // collider gizmos (20.4)
    if (!playing_) {
        renderer_.SetLighting(false, 1, 1, 1, {});
        Camera2D view = camera_;
        const auto [x, y] = session_.CameraShake();
        view.offsetX += x / view.zoom;
        view.offsetY += y / view.zoom;
        return view;
    }
    const LightingFrame lighting = session_.Lighting();
    std::vector<Renderer2D::SceneLight> lights;
    for (const auto& l : lighting.lights) lights.push_back({l.x, l.y, l.radius, l.r, l.g, l.b, l.intensity});
    renderer_.SetLighting(lighting.enabled, lighting.ambient[0], lighting.ambient[1], lighting.ambient[2], std::move(lights));
    Camera2D view = camera_;
    const auto [x, y] = session_.CameraShake();
    view.offsetX += x / view.zoom;
    view.offsetY += y / view.zoom;
    return view;
}

void EditorApp::AddStatsOverlay(std::vector<UiQuad>& ui) {
    if (!showStats_) {
        return;
    }
    const RenderStats& stats = renderer_.LastStats();
    char lines[3][96];
    std::snprintf(lines[0], sizeof(lines[0]), "frame %.1f ms (%.0f fps)", frameMs_, frameMs_ > 0.0 ? 1000.0 / frameMs_ : 0.0);
    std::snprintf(lines[1], sizeof(lines[1]), "update %.2f ms%s", updateMs_, playing_ ? "" : " (not playing)");
    std::snprintf(lines[2], sizeof(lines[2]), "sprites %zu drawn, %zu culled - %zu draw calls", stats.sprites,
                  stats.culled, stats.drawCalls);
    const Font& font = Font::Builtin(14.0f);
    float width = 0.0f;
    for (const char* line : lines) {
        width = std::max(width, font.Measure(line));
    }
    const float lineHeight = font.LineHeight();
    AddUiRect(ui, 8.0f, 8.0f, 20.0f + width, 14.0f + lineHeight * 3.0f, {0.0f, 0.0f, 0.0f, 0.65f});
    for (int i = 0; i < 3; ++i) {
        font.Draw(ui, lines[i], 14.0f, 11.0f + lineHeight * static_cast<float>(i), {0.85f, 0.95f, 0.85f, 1.0f});
    }
}

void EditorApp::Tick(double frameSeconds) {
    const SlowWatch whole("frame (all of Tick: if only this is logged, it was the rest of the frame's work)");
    { const SlowWatch w("frame: the AI's answers"); PollAi(); }
    { const SlowWatch w("frame: a build job ending"); FinishBuildJob(); }
    { const SlowWatch w("frame: a build step"); StepBuildRun(); }
    { const SlowWatch w("frame: the recovery snapshot"); WriteRecovery(); } // the unsaved scene, every two minutes
    { const SlowWatch w("frame: the planner's place build"); StepPlannerPlaceBuild(); }
    { const SlowWatch w("frame: the game build"); StepGameBuild(); }
    // The Grid setting changed (Project Settings, an AI proposal, undo): the
    // whole scene re-lays on the other grid (6.1).
    if (const IsoParams grid = BootstrapIso(); grid.topDown != iso_.topDown) {
        iso_ = grid;
        session_.SetIso(iso_);
        previewKey_.clear();
        UpdateWorldToScreen(registry_, iso_);
        FrameScene();
    }
    frameMs_ = frameMs_ * 0.9 + frameSeconds * 1000.0 * 0.1;
    if (uiDocs_) uiDocs_->Update(frameSeconds);
    if (!playing_) session_.StepPreviewFx(static_cast<float>(frameSeconds)); // effects tried while editing
    if (playing_) {
        const auto updateStart = std::chrono::steady_clock::now();
        session_.Advance(frameSeconds, [this](Key key) { return viewport_.IsKeyDown(key); });
        HandleTravel();
        ApplyAudio(audio_, assets_.GetProject(), DirectAudio(registry_, session_, playManifest_, false), registry_,
                   static_cast<float>(frameSeconds));
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - updateStart).count();
        updateMs_ = updateMs_ * 0.9 + ms * 0.1;
        auto player = FindPlayer(registry_);
        for (const auto& event : session_.Events()) {
            if (event.type == GameEvent::Type::Death && player && event.entity == *player) {
                post_({{"type", "notify"}, {"text", "You died. Press Esc or Stop to return to editing."}});
                banner_ = {"You died", "Esc or Stop to return to editing"};
            } else if (event.type == GameEvent::Type::Victory) {
                post_({{"type", "notify"}, {"text", "Victory! All enemies defeated."}});
                banner_ = {"Victory!", "All enemies defeated"};
            }
        }
        if (const auto target = session_.CameraTarget(); target && !panning_) {
            int width = 0;
            int height = 0;
            viewport_.GetFramebufferSize(width, height);
            UpdatePlayCamera(camera_, cameraRig_, registry_, *target, static_cast<float>(width), static_cast<float>(height),
                             static_cast<float>(frameSeconds), ActiveSettings().camera); // a cutscene's, or the player
        }
        sinceInspectorRefresh_ += frameSeconds;
        if (sinceInspectorRefresh_ >= kPlayInspectorRefreshSeconds) {
            sinceInspectorRefresh_ = 0.0;
            inspectorStale_ = true; // live values while things move
        }
    } else {
        // Edit mode runs no simulation, only the projection, so edits to
        // WorldTransform2D or Tile show up immediately.
        UpdateWorldToScreen(registry_, iso_);
        audio_.Update(static_cast<float>(frameSeconds)); // fades, finished sounds
    }
    UpdatePreview();
    UpdateCursor();

    if (sceneTreeStale_) {
        sceneTreeStale_ = false;
        post_(SceneJson());
        // The tree drops a selection whose id vanished; re-assert ours.
        post_({{"type", "selection:changed"},
               {"id", selected_ ? nlohmann::json(static_cast<uint32_t>(*selected_)) : nlohmann::json(nullptr)}});
    }
    if (inspectorStale_) {
        inspectorStale_ = false;
        if (selected_ && registry_.valid(*selected_)) {
            post_(InspectorJson(*selected_));
        }
    }
    if (editorStateStale_) {
        editorStateStale_ = false;
        post_(EditorStateJson());
        UpdateTitle();
        PostProposalStates(); // an undo or a new edit may have changed them
    }
}

std::vector<OverlayLine> EditorApp::BuildOverlay() {
    std::vector<OverlayLine> overlay;
    AppendHealthBars(registry_, overlay);
    if (playing_) {
        return overlay;
    }
    // The plan view (7.5): the site and a sketch of its zones, before anything is built.
    if (build_ && !buildJob_ && (build_->Stage() == "plan" || build_->Stage() == "planned")) {
        static const float kSite[3] = {0.54f, 0.71f, 0.98f};
        static const float kZones[6][3] = {{0.65f, 0.89f, 0.63f}, {0.98f, 0.70f, 0.53f}, {0.80f, 0.65f, 0.97f},
                                           {0.98f, 0.89f, 0.69f}, {0.58f, 0.89f, 0.84f}, {0.95f, 0.55f, 0.66f}};
        const float side = static_cast<float>(build_->SiteSide());
        const auto [ox, oy] = build_->Origin();
        const float fx = static_cast<float>(ox), fy = static_cast<float>(oy);
        AddGridRect(overlay, fx, fy, side, side, iso_, kSite, 0.9f, 2.0f);
        const nlohmann::json& solved = build_->Solved();
        if (solved.is_object() && solved.contains("objects")) {
            // Solved (7.6): the zones' bounds and every building's footprint.
            static const float kBuilding[3] = {0.95f, 0.85f, 0.60f};
            int n = 0;
            for (const auto& r : solved.value("regions", nlohmann::json::array())) {
                if (r.value("statement", "") != "site/zones") continue;
                const auto& b = r["bounds"];
                AddGridRect(overlay, fx + b[0].get<float>(), fy + b[1].get<float>(), b[2].get<float>(), b[3].get<float>(), iso_,
                            kZones[n++ % 6], 0.6f, 1.0f);
            }
            for (const auto& o : solved["objects"]) {
                if (o.value("layer", "") != "building") continue;
                AddGridRect(overlay, fx + o["x"].get<float>(), fy + o["y"].get<float>(), o["w"].get<float>(), o["h"].get<float>(), iso_,
                            kBuilding, 0.95f, 1.5f);
            }
        } else {
            const nlohmann::json sketch = PlanSketch(Places(), build_->Spec(), build_->SiteSide());
            int n = 0;
            for (const auto& r : sketch.value("rects", nlohmann::json::array())) {
                AddGridRect(overlay, fx + r.value("x", 0.0f), fy + r.value("y", 0.0f), r.value("w", 0.0f), r.value("h", 0.0f), iso_,
                            kZones[n++ % 6], 0.85f, 1.5f);
            }
        }
    }
    if (brushing_) {
        static const float kBrush[3] = {0.98f, 0.89f, 0.69f};
        const int x0 = std::min(brushFromX_, brushToX_), y0 = std::min(brushFromY_, brushToY_);
        const int x1 = std::max(brushFromX_, brushToX_) + 1, y1 = std::max(brushFromY_, brushToY_) + 1;
        AddGridRect(overlay, static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x1 - x0), static_cast<float>(y1 - y0), iso_,
                    kBrush, 0.95f, 2.0f);
    }
    if (showGrid_) {
        // Tile borders on empty ground (4.3), over the visible part of the scene.
        int width = 0, height = 0;
        viewport_.GetFramebufferSize(width, height);
        int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
        for (auto [vx, vy] : {std::pair{0.0f, 0.0f}, std::pair{static_cast<float>(width), 0.0f},
                              std::pair{0.0f, static_cast<float>(height)},
                              std::pair{static_cast<float>(width), static_cast<float>(height)}}) {
            float sx = 0.0f, sy = 0.0f;
            camera_.ViewportToScene(vx, vy, sx, sy);
            int gx = 0, gy = 0;
            CellAt(sx, sy, iso_, gx, gy);
            minX = std::min(minX, gx);
            minY = std::min(minY, gy);
            maxX = std::max(maxX, gx);
            maxY = std::max(maxY, gy);
        }
        if (static_cast<long long>(maxX - minX + 1) * (maxY - minY + 1) <= 12000) {
            std::set<std::pair<int, int>> tiled;
            for (auto [entity, tile] : registry_.view<Tile>().each()) tiled.insert({tile.gx, tile.gy});
            static constexpr float kGridColor[3] = {0.6f, 0.65f, 0.8f};
            for (int gx = minX - 1; gx <= maxX + 1; ++gx)
                for (int gy = minY - 1; gy <= maxY + 1; ++gy)
                    if (!tiled.contains({gx, gy})) AddCellOutline(overlay, gx, gy, iso_, kGridColor, 0.25f, 1.0f);
        }
    }
    static constexpr float kMarkerColor[3] = {0.99f, 0.86f, 0.45f};
    for (auto [entity, marker, world] : registry_.view<Marker, WorldTransform2D>().each()) {
        AddCellOutline(overlay, static_cast<int>(std::floor(world.x)), static_cast<int>(std::floor(world.y)), iso_,
                       kMarkerColor, 0.9f, 1.5f);
    }

    if (selectedCell_) AddCellOutline(overlay, selectedCell_->gx, selectedCell_->gy, iso_, kAccent, 0.95f, 2.0f);
    if (boxing_) AddRectOutline(overlay, boxFromX_, boxFromY_, boxToX_, boxToY_, kAccent, 0.9f, 1.5f);
    {
        size_t drawn = 0; // at most a few thousand outlines a frame
        for (Entity e : selectedMany_) {
            if (registry_.valid(e) && ++drawn < 3000) AddEntityOutline(overlay, registry_, e, iso_, kAccent, 0.8f, 1.5f);
        }
        for (const auto& c : selectedCells_) {
            if (++drawn < 3000) AddCellOutline(overlay, c.gx, c.gy, iso_, kAccent, 0.6f, 1.0f);
        }
    }
    if (hasHover_ && !panning_ && !dragging_) {
        int gx = 0;
        int gy = 0;
        CellAt(hoverX_, hoverY_, iso_, gx, gy);
        static constexpr float kWhite[3] = {1.0f, 1.0f, 1.0f};
        switch (tool_) {
            case Tool::Select:
                if (auto hovered = PickEntity(registry_, hoverX_, hoverY_, iso_); hovered && hovered != selected_) {
                    AddEntityOutline(overlay, registry_, *hovered, iso_, kWhite, 0.35f, 1.0f);
                } else if (!hovered) {
                    if (auto cell = PickLayerCell(hoverX_, hoverY_)) AddCellOutline(overlay, cell->gx, cell->gy, iso_, kWhite, 0.35f, 1.0f);
                }
                break;
            case Tool::Prefab: {
                // White for painting tiles, accent for placing things.
                const PrefabInfo* prefab = prefabs_.Find(brushPrefab_);
                bool isTile = prefab != nullptr && prefab->isTile;
                AddCellOutline(overlay, gx, gy, iso_, isTile ? kWhite : kAccent, isTile ? 0.8f : 0.9f, 1.5f);
                break;
            }
            case Tool::Erase:
                if (auto hovered = PickEntity(registry_, hoverX_, hoverY_, iso_)) {
                    AddEntityOutline(overlay, registry_, *hovered, iso_, kErase, 0.9f, 2.0f);
                }
                break;
            case Tool::Height:
                // The brush's reach: the cells it moves, fainter towards its edge.
                for (int y = gy - heightRadius_; y <= gy + heightRadius_; ++y) {
                    for (int x = gx - heightRadius_; x <= gx + heightRadius_; ++x) {
                        const float d = std::hypot(static_cast<float>(x - gx), static_cast<float>(y - gy));
                        if (d > heightRadius_ + 0.5f) continue;
                        AddCellOutline(overlay, x, y, iso_, kWhite, 0.85f - 0.5f * d / (heightRadius_ + 1.0f), 1.0f);
                    }
                }
                break;
        }
    }

    if (selected_ && registry_.valid(*selected_)) {
        AddEntityOutline(overlay, registry_, *selected_, iso_, kAccent, 1.0f, 2.0f);
    }
    return overlay;
}

void EditorApp::UpdateTitle() {
    // The project first: switching scenes stays in the same project and window.
    const std::wstring project = assets_.GetProject().root.filename().wstring();
    const std::wstring scene = std::filesystem::path(scenePath_).stem().wstring();
    std::wstring title = project + L" — " + scene + (dirty_ ? L"*" : L"") + L" - AI-Native Game Engine Editor";
    SetWindowTextW(mainWindow_, title.c_str());
}

// --- Assets -----------------------------------------------------------------

void EditorApp::OnAppActivated() {
    RefreshAssets();
    RefreshPrefabs();
    world_.Refresh(); // entries edited in a text editor
    post_(WorldJson());
}

// --- AI ----------------------------------------------------------------------

void EditorApp::StartAi(const std::filesystem::path& serviceDir) {
    userPrefs_ = std::make_unique<UserPrefs>(UserPrefs::DefaultPath());
    std::string projectRoot = std::filesystem::absolute(assets_.GetProject().root).string();
    aiConnection_ = std::make_unique<AiConnection>(*userPrefs_, [this, serviceDir, projectRoot]() {
        // The project's LLM (Project Settings ▸ AI), else the one chosen in Preferences, for the service to start with.
        const std::string chosen = ChosenAi("provider");
        _putenv_s("AIGE_PROVIDER", chosen.empty() ? "mistral" : chosen.c_str());
        AiClient::Options options;
        options.serviceDir = serviceDir;
        auto client = std::make_unique<AiClient>(options);
        client->Start(EngineReflection().ExportSchema());
        client->Request("PUT", "/project", {{"root", projectRoot}}); // so it can index the World Bible
        return client;
    });
    aiConnection_->Start(); // only if the user has said yes to AI
    RememberRecentProject();
    post_(ProjectInfoJson());
    post_(AiStatusJson());
}

std::string EditorApp::ChosenAi(const std::string& which) {
    const AiSettings& project = ActiveSettings().ai;
    auto pref = [&](const char* key, const char* fallback) {
        const nlohmann::json v = userPrefs_ ? userPrefs_->Get(key, fallback) : nlohmann::json(fallback);
        return v.is_string() ? v.get<std::string>() : std::string(fallback);
    };
    if (which == "provider") return !project.provider.empty() ? project.provider : pref("aiProvider", "mistral");
    if (which == "model") {
        // The project's model; with the project's own provider and no model, that provider's default; else the app's.
        if (!project.model.empty()) return project.model;
        const std::string provider = ChosenAi("provider");
        if (!project.provider.empty() && project.provider != pref("aiProvider", "mistral")) return provider == "mistral" ? "ministral-8b-latest" : "";
        return pref("aiModel", provider == "mistral" ? "ministral-8b-latest" : "");
    }
    if (which == "art") return !project.art.empty() ? project.art : pref("artProvider", "auto");
    if (which == "background") return !project.background.empty() ? project.background : pref("backgroundProvider", "bfl:flux-3-image");
    return {};
}

void EditorApp::SendProvider() {
    if (!Ai() || Ai()->GetState() != AiClient::State::Ready || !userPrefs_) return;
    // Each the project's choice (Project Settings ▸ AI), else the app's (Preferences ▸ AI); Mistral's small model unless
    // another was chosen (fast and cheap).
    artProviderRequest_ = Ai()->Request("PUT", "/art-provider", {{"provider", ChosenAi("art")}});
    backgroundProviderRequest_ = Ai()->Request("PUT", "/background-provider", {{"provider", ChosenAi("background")}});
    providerRequest_ = Ai()->Request("PUT", "/provider", {{"provider", ChosenAi("provider")}, {"model", ChosenAi("model")}});
}

AiClient* EditorApp::Ai() {
    return aiConnection_ ? aiConnection_->Client() : nullptr;
}

void EditorApp::SetAiEnabled(bool enabled) {
    if (!aiConnection_) {
        return;
    }
    if (!enabled) {
        StopTask(taskId_, "AI was turned off. Nothing was changed.");
    }
    aiConnection_->SetEnabled(enabled);
    if (!enabled) {
        requestInstructions_.clear();
        post_({{"type", "ai:busy"}, {"busy", false}});
    }
    post_(AiStatusJson());
}

nlohmann::json EditorApp::AiStatusJson() {
    AiClient* ai = Ai();
    std::string state = "off";
    if (ai) {
        switch (ai->GetState()) {
            case AiClient::State::Starting: state = "starting"; break;
            case AiClient::State::Ready: state = "ready"; break;
            case AiClient::State::Failed: state = "failed"; break;
            case AiClient::State::Stopped: state = "off"; break;
        }
    }
    AiChoice choice = aiConnection_ ? aiConnection_->Choice() : AiChoice::Off;
    const char* text = choice == AiChoice::Undecided ? "AI not set up yet" : "AI is turned off";
    // Ready: the model it writes with now (the one chosen in Preferences), not the one it started with.
    std::string status = ai ? ai->StatusText() : text;
    if (state == "ready" && providerStatus_.value("ok", false) && providerStatus_.contains("model")) {
        status = "AI ready (" + providerStatus_.value("model", std::string()) + ")";
    }
    return {{"type", "ai:status"},
            {"state", state},
            {"enabled", choice == AiChoice::On},
            {"choice", choice == AiChoice::On ? "on" : choice == AiChoice::Off ? "off" : "undecided"},
            {"reviewPolicy", ReviewPolicyName(CurrentReviewPolicy())},
            {"provider", userPrefs_ ? userPrefs_->Get("aiProvider", "mistral") : nlohmann::json("mistral")},
            {"model", providerStatus_.value("model", "")},
            {"providerError", providerStatus_.value("ok", true) ? "" : (providerStatus_.contains("errors") && !providerStatus_["errors"].empty()
                                                                       ? providerStatus_["errors"][0].get<std::string>() : "")},
            {"available", providerStatus_.value("available", nlohmann::json::object())},
            {"artProvider", userPrefs_ ? userPrefs_->Get("artProvider", "auto") : nlohmann::json("auto")},
            {"art", artStatus_.value("art", "")},
            {"backgroundProvider", userPrefs_ ? userPrefs_->Get("backgroundProvider", "bfl:flux-3-image") : nlohmann::json("bfl:flux-3-image")},
            {"backgroundError", backgroundStatus_.value("ok", true) ? "" : (backgroundStatus_.contains("errors") && !backgroundStatus_["errors"].empty()
                                                                            ? backgroundStatus_["errors"][0].get<std::string>() : "")},
            // What this project uses instead of the app's, where it has its own (Project Settings ▸ AI).
            {"project", {{"provider", ActiveSettings().ai.provider}, {"model", ActiveSettings().ai.model}, {"art", ActiveSettings().ai.art},
                         {"background", ActiveSettings().ai.background}}},
            {"artError", artStatus_.value("ok", true) ? "" : (artStatus_.contains("errors") && !artStatus_["errors"].empty()
                                                              ? artStatus_["errors"][0].get<std::string>() : "")},
            {"text", status}};
}

void EditorApp::PollAi() {
    if (!Ai()) {
        return;
    }
    // The model calls' progress, while there are any (and once more after, to clear the strips).
    const auto now = std::chrono::steady_clock::now();
    if (progressRequest_ == 0 && Ai()->GetState() == AiClient::State::Ready && (Ai()->InFlight() > 0 || progressShown_) &&
        now - progressPolled_ >= std::chrono::milliseconds(800)) {
        progressPolled_ = now;
        progressRequest_ = Ai()->Request("GET", "/progress", nlohmann::json(), 3);
    }
    while (auto result = Ai()->Poll()) {
        if (result->kind == AiClient::Result::Kind::Status) {
            if (Ai()->GetState() == AiClient::State::Ready) {
                SendProvider();
                if (plan_) {
                    plan_->Kick(); // what waited for the service (re-applied answers) goes now
                    PostPlanner();
                }
            }
            post_(AiStatusJson());
        } else if (result->kind == AiClient::Result::Kind::Response) {
            if (result->requestId != 0 && result->requestId == progressRequest_) {
                progressRequest_ = 0;
                nlohmann::json shown = result->body.is_object() && result->body.contains("calls") ? result->body
                                                                                                    : nlohmann::json{{"calls", nlohmann::json::array()}};
                progressShown_ = !shown["calls"].empty();
                shown["type"] = "ai:progress";
                post_(shown);
            } else if (result->requestId != 0 && result->requestId == providerRequest_) {
                providerRequest_ = 0;
                providerStatus_ = result->body.is_object() ? result->body : nlohmann::json{{"ok", false}};
                post_(AiStatusJson());
            } else if (result->requestId != 0 && result->requestId == artProviderRequest_) {
                artProviderRequest_ = 0;
                artStatus_ = result->body.is_object() ? result->body : nlohmann::json{{"ok", false}};
                post_(AiStatusJson());
            } else if (result->requestId != 0 && result->requestId == backgroundProviderRequest_) {
                backgroundProviderRequest_ = 0;
                backgroundStatus_ = result->body.is_object() ? result->body : nlohmann::json{{"ok", false}};
                post_(AiStatusJson());
            } else if (result->requestId != 0 && result->requestId == modelsRequest_) {
                modelsRequest_ = 0;
                nlohmann::json shown = result->body.is_object() ? result->body : nlohmann::json{{"ok", false}};
                shown["type"] = "ai:models";
                post_(shown);
            } else if (gameBuild_ && gameBuild_->Owns(result->requestId)) {
                gameBuildAnswers_.push_back({result->requestId, result->body}); // applied between pumps
                gameBuildDirty_ = true;
            } else if (plan_ && plan_->Owns(result->requestId)) {
                plan_->OnResponse(result->requestId, result->body);
                PostPlanner();
            } else if (buildJob_ && !KnownRequest(result->requestId)) {
                buildDeferred_.push_back({result->requestId, result->body, true}); // the build is busy: after its job
            } else if (build_ && build_->Owns(result->requestId)) {
                OnBuildResult(result->requestId, result->body);
            } else if (result->requestId == worldSearchRequest_) {
                post_({{"type", "world:searchResults"}, {"result", result->body}});
            } else if (result->requestId == analyzeRequest_) {
                analyzeRequest_ = 0;
                const nlohmann::json& body = result->body;
                int described = 0;
                for (const auto& a : body.value("assets", nlohmann::json::array())) {
                    std::string tags;
                    for (const auto& t : a.value("tags", nlohmann::json::array())) {
                        if (t.is_string()) tags += (tags.empty() ? "" : ", ") + t.get<std::string>();
                    }
                    char cells[32];
                    std::snprintf(cells, sizeof(cells), "%.2f", a.value("sizeCells", 1.0));
                    std::map<std::string, std::string> fields{{"description", a.value("description", "")},
                                                              {"kind", a.value("kind", "other")},
                                                              {"tags", tags},
                                                              {"direction", a.value("direction", "none")},
                                                              {"cells", cells},
                                                              {"blocks", a.value("blocks", false) ? "1" : "0"},
                                                              {"name", a.value("name", "")},
                                                              {"accessible", a.value("accessible", false) ? "1" : ""}};
                    // Catalog v2 (7.3): what was understood; empty values leave the field unset.
                    const std::string perspective = a.value("perspective", "unknown");
                    fields["perspective"] = perspective == "unknown" ? "" : perspective;
                    fields["roles"] = a.value("role", "");
                    const auto anchors = a.value("anchors", nlohmann::json::object());
                    fields["anchors"] = anchors.empty() ? "" : anchors.dump();
                    const std::string id = a.value("id", "").empty() ? a.value("path", "") : a.value("id", "");
                    if (SetCatalogFields(assets_, id, fields)) {
                        ++described;
                    }
                }
                const auto errors = body.value("errors", nlohmann::json::array());
                post_(AssetsJson());
                post_({{"type", "assets:analyzed"}, {"described", described}, {"errors", errors}});
                post_({{"type", "notify"},
                       {"text", "Analyzed " + std::to_string(described) + " asset(s)" +
                                    (errors.empty() ? "" : "; " + std::to_string(errors.size()) + " problem(s): " +
                                                               errors.front().get<std::string>())}});
            } else if (result->requestId == webPreviewRequest_) {
                webPreviewRequest_ = 0;
                nlohmann::json shown = result->body;
                shown["type"] = "web:results";
                post_(shown);
            } else if (auto web = webFetchRequests_.find(result->requestId); web != webFetchRequests_.end()) {
                const nlohmann::json asked = web->second;
                webFetchRequests_.erase(web);
                const std::string path = result->body.value("path", "");
                if (!result->body.value("ok", false) || path.empty()) {
                    const auto errors = result->body.value("errors", nlohmann::json::array());
                    post_({{"type", "notify"}, {"text", "Couldn't add it: " + (errors.empty() ? std::string("no answer") : errors.front().get<std::string>())}});
                } else {
                    // What it is and whose: the catalog's fields (credits come from them), then the library.
                    const nlohmann::json r = asked.value("result", nlohmann::json::object());
                    RefreshAssets();
                    std::map<std::string, std::string> fields = {{"source", "web"},
                                                                 {"licence", r.value("licence", "")},
                                                                 {"author", r.value("author", "")},
                                                                 {"url", r.value("page", "")},
                                                                 {"name", r.value("title", "")}};
                    SetCatalogFields(assets_, path, fields);
                    RefreshAssets();
                    UserLibrary(UserLibraryDir()).Add(assets_.GetProject().Resolve(path),
                                                      {{"source", "web"}, {"licence", r.value("licence", "")}, {"author", r.value("author", "")},
                                                       {"url", r.value("page", "")}, {"name", r.value("title", "")},
                                                       {"origin", assets_.GetProject().root.filename().string()}},
                                                      asked.value("libraryFolder", ""));
                    PostLibrary();
                    post_({{"type", "web:added"}, {"path", path}, {"file", r.value("file", "")}});
                    post_({{"type", "notify"}, {"text", "Added " + path + " (" + r.value("licence", "") + ", " + r.value("author", "") + ")"}});
                }
            } else if (result->requestId == characterRequest_ || result->requestId == animationRequest_) {
                const bool character = result->requestId == characterRequest_;
                (character ? characterRequest_ : animationRequest_) = 0;
                RefreshAssets(); // the new textures
                post_({{"type", character ? "character:result" : "animation:result"}, {"result", result->body}});
            } else if (result->requestId == artRequest_) {
                artRequest_ = 0;
                artHistory_.Finish(artHistoryId_, result->body);
                RefreshAssets(); // the new textures
                post_({{"type", "art:result"}, {"result", result->body}});
                post_(ArtJson());
            } else if (result->requestId == worldCheckRequest_) {
                post_({{"type", "world:checkResult"}, {"key", worldCheckKey_}, {"result", result->body}});
            } else if (auto it = consistencyRequests_.find(result->requestId); it != consistencyRequests_.end()) {
                post_({{"type", "ai:consistency"}, {"id", it->second}, {"result", result->body}});
                if (proposals_.contains(it->second)) {
                    LogAiDecision(it->second, {{"decision", "consistency-check"},
                                               {"issues", result->body.value("issues", nlohmann::json::array())}});
                }
                consistencyRequests_.erase(it);
            }
        } else if (buildJob_) {
            buildDeferred_.push_back({result->requestId, result->body, false});
        } else if (build_ && build_->Owns(result->requestId)) {
            OnBuildResult(result->requestId, result->body.is_object() ? result->body : nlohmann::json{{"ok", false}});
        } else {
            OnProposal(result->requestId, result->body);
        }
    }
}

bool EditorApp::KnownRequest(int requestId) const {
    return requestId == worldSearchRequest_ || requestId == analyzeRequest_ || requestId == artRequest_ ||
           (requestId != 0 && (requestId == characterRequest_ || requestId == animationRequest_)) ||
           requestId == worldCheckRequest_ || consistencyRequests_.contains(requestId) ||
           (requestId != 0 && requestId == webPreviewRequest_) || webFetchRequests_.contains(requestId);
}

void EditorApp::OnBuildResult(int requestId, const nlohmann::json& body) {
    if (build_->Stage() == "sourcing") {
        // The art as it comes (light): then, when it's all there, the build goes on into the scene.
        build_->OnResponse(requestId, body);
        AfterSourcing();
        return;
    }
    // An answer may finish the plan and lay it out: off the UI thread.
    RunBuildJob([requestId, body](PlaceBuild& build) {
        build.OnResponse(requestId, body);
        return std::string();
    }, "", [this] {
        // The art waited for the background: it's painted (or failed), the art is found or made now.
        if (!afterBackground_.empty() && build_ && !build_->MakingBackground()) {
            const std::string then = afterBackground_;
            afterBackground_.clear();
            if (then == "matched") MakeMatched();
            else FindAssets();
        }
    });
}

void EditorApp::AfterSourcing() {
    {
        if (build_->SourcingDone()) {
            build_->ClearSourcingDone();
            RefreshAssets();
            int added = 0;
            for (const auto& a : build_->TakeNewAssets()) {
                std::map<std::string, std::string> fields;
                const nlohmann::json given = a.value("fields", nlohmann::json::object());
                for (const auto& [k, v] : given.items()) {
                    if (v.is_string()) fields[k] = v.get<std::string>();
                }
                added += SetCatalogFields(assets_, a.value("path", ""), fields) ? 1 : 0;
            }
            post_(AssetsJson());
            post_({{"type", "notify"}, {"text", "Added " + std::to_string(added) + " new asset(s) for the build"}});
            // What each part now gets, with the new art in the catalog.
            const TerrainSet terrains = ProjectTerrainSet(prefabs_, assets_.GetProject());
            const Catalog catalog = BuildCatalog(assets_, &Places(), CatalogOptionsNow());
            AssetSourcing sourcing = SourcingNow(); // what was tried is known: only other methods are left
            PlaceSceneOptions options;
            options.place = build_->Id();
            std::tie(options.originX, options.originY) = build_->Origin();
            options.iso = iso_;
            options.isoGrid = ActiveSettings().grid.style != GridStyle::TopDown;
            options.hills = HillsFor(build_->Brief());
            ApplyBackground(options); // a painted background is the ground: no tiles asked for
            const nlohmann::json assets = PlanPlaceAssets(Places(), build_->Spec(), build_->Solved(), catalog, prefabs_, assets_, terrains, options, &sourcing);
            build_->MarkSourced(AssetRows(assets));
            // Some still missing, and a method not tried for them yet (the web failed: the Art tool;
            // the Art tool failed: the web): another round, on its own.
            const nlohmann::json more = assets.value("pending", nlohmann::json::array());
            if (!more.empty() && autoRounds_ < 6) { // a round per refused candidate, then the Art tool
                ++autoRounds_;
                if (build_->StartSourcing(more, sourcing.settings)) {
                    post_({{"type", "notify"}, {"text", "Trying another way for " + std::to_string(more.size()) + " piece(s) still missing"}});
                }
            }
        }
        PostBuild();
    }
}

void EditorApp::RunBuildJob(std::function<std::string(PlaceBuild&)> job, const std::string& prefix, std::function<void()> after) {
    if (!build_) return;
    if (buildJob_) {
        post_({{"type", "notify"}, {"text", "The build is still working: a moment"}});
        return;
    }
    PostBuild(); // its state before, then shown as busy
    buildJob_ = std::make_unique<BuildJob>();
    buildJob_->prefix = prefix;
    buildJob_->after = std::move(after);
    BuildJob* running = buildJob_.get();
    PlaceBuild* build = build_.get();
    PostBuild();
    running->thread = std::thread([running, build, job = std::move(job)] {
        try {
            running->why = job(*build);
        } catch (const std::exception& e) {
            running->why = std::string("it failed: ") + e.what();
        }
        running->done = true;
    });
}

void EditorApp::FinishBuildJob() {
    if (buildJob_ && buildJob_->done) {
        buildJob_->thread.join();
        const std::unique_ptr<BuildJob> finished = std::move(buildJob_);
        if (!finished->why.empty()) post_({{"type", "notify"}, {"text", finished->prefix + finished->why}});
        if (finished->after) finished->after();
        PostBuild();
    }
    // What came for the build while it worked, in order (one may start the next job).
    while (!buildJob_ && !buildDeferred_.empty()) {
        const DeferredResult next = buildDeferred_.front();
        buildDeferred_.pop_front();
        if (build_ && build_->Owns(next.id)) {
            OnBuildResult(next.id, next.response || next.body.is_object() ? next.body : nlohmann::json{{"ok", false}});
        } else if (!next.response) {
            OnProposal(next.id, next.body);
        }
    }
}

void EditorApp::StartBuild(const std::string& query, const nlohmann::json& choices) {
    if (buildJob_) {
        post_({{"type", "notify"}, {"text", "The build is still working: a moment"}});
        return;
    }
    if (build_ && build_->Stage() == "interpreting") {
        post_({{"type", "notify"}, {"text", "A build is still reading its request"}});
        return;
    }
    build_ = MakeBuild();
    const std::string why = build_->Start(query, choices);
    if (!why.empty()) post_({{"type", "notify"}, {"text", "Can't build: " + why}});
    post_({{"type", "build:open"}});
    PostBuild();
}

void EditorApp::ResumeBuild(const std::string& id) {
    if (buildJob_) {
        post_({{"type", "notify"}, {"text", "The build is still working: a moment"}});
        return;
    }
    auto build = MakeBuild();
    const std::string why = build->Open(id);
    if (!why.empty()) {
        post_({{"type", "notify"}, {"text", "Can't open the build: " + why}});
        return;
    }
    build_ = std::move(build);
    post_({{"type", "build:open"}});
    PostBuild();
}

std::unique_ptr<PlaceBuild> EditorApp::MakeBuild() {
    PlaceBuild::Host host;
    host.ontology = [this]() -> const Ontology& { return Places(); };
    host.catalog = [this] { return BuildCatalog(assets_, &Places(), CatalogOptionsNow()); };
    host.isoGrid = [] { return ActiveSettings().grid.style != GridStyle::TopDown; };
    host.request = [this](const std::string& path, const nlohmann::json& body, int timeout) {
        return Ai() && Ai()->GetState() == AiClient::State::Ready ? Ai()->Request("POST", path, body, timeout) : 0;
    };
    host.projectRoot = assets_.GetProject().root;
    host.layoutStep = true; // ③ Layout: the zones placed and revised before what's inside them
    return std::make_unique<PlaceBuild>(std::move(host));
}

nlohmann::json EditorApp::PlanForMatchedArt() {
    // What each part needs, as the Assets step decides it (the need rows the art is made for).
    if (!build_->HasOrigin()) {
        const auto [x, y] = FreeOrigin(registry_, build_->Id(), build_->SiteSide());
        build_->SetOrigin(x, y);
    }
    PlaceSceneOptions options;
    options.place = build_->Id();
    std::tie(options.originX, options.originY) = build_->Origin();
    options.iso = iso_;
    options.isoGrid = ActiveSettings().grid.style != GridStyle::TopDown;
    options.hills = HillsFor(build_->Brief());
    ApplyBackground(options); // a painted background is the ground: no tiles asked for
    const AssetSourcing sourcing = SourcingNow();
    return PlanPlaceAssets(Places(), build_->Spec(), build_->Solved(), BuildCatalog(assets_, &Places(), CatalogOptionsNow()), prefabs_, assets_,
                           ProjectTerrainSet(prefabs_, assets_.GetProject()), options, &sourcing);
}

void EditorApp::ApplyBackground(PlaceSceneOptions& options) const {
    // The place's background (painted by Matched art, or picked): its ground, cell by cell.
    if (!build_) return;
    if (const nlohmann::json bg = build_->Background(); bg.is_object() && bg.value("cellPx", 0) > 0) {
        options.groundPicture = bg.value("path", "");
        options.groundPictureCellPx = bg.value("cellPx", 0);
    }
}

void EditorApp::CutMatched() {
    // The art is made: each piece cut, fitted and checked, then what each part gets with them.
    const nlohmann::json info = build_->MatchedArtInfo();
    const fs::path root = assets_.GetProject().root, dir = root / info.value("dir", "");
    nlohmann::json sheets;
    if (std::ifstream in(dir / "sheets.json"); in) sheets = nlohmann::json::parse(in, nullptr, false);
    if (!sheets.is_object()) {
        build_->MatchedArtDone({{"skipped", nlohmann::json::array({{{"key", "*"}, {"why", "the sheets couldn't be read"}}})}});
        return;
    }
    const nlohmann::json report = CutMatchedArt(assets_, assets_.GetProject(), sheets, dir, &Places(), build_->Id());
    RefreshAssets();
    post_(AssetsJson());
    build_->MarkSourced(AssetRows(PlanForMatchedArt()));
    build_->MatchedArtDone(report);
    post_({{"type", "notify"}, {"text", "Made " + std::to_string(report.value("pieces", nlohmann::json::array()).size()) + " pieces and " +
                                            std::to_string(report.value("grounds", nlohmann::json::array()).size()) + " grounds in one style"}});
}

void EditorApp::PostBuild() {
    if (!build_) {
        post_({{"type", "build:state"}, {"stage", ""}});
        return;
    }
    if (!buildJob_ && build_->MatchedArtMade()) CutMatched(); // on the UI thread: it changes the project's assets
    if (buildJob_) {
        // Working (off the UI thread): what it was, marked busy; the build itself isn't touched.
        nlohmann::json shown = buildShown_.is_object() ? buildShown_ : nlohmann::json{{"type", "build:state"}};
        shown["busy"] = true;
        shown["status"] = "Working: planning and laying the place out…";
        post_(shown);
        return;
    }
    for (const auto& u : usageQueue_) {
        build_->AddUsage(u.value("step", ""), u.value("inputTokens", 0), u.value("outputTokens", 0), u.value("seconds", 0.0));
    }
    usageQueue_.clear();
    const std::string stage = build_->Stage();
    if (!build_->HasOrigin() && (stage == "plan" || stage == "planned" || stage == "built")) {
        const auto [x, y] = FreeOrigin(registry_, build_->Id(), build_->SiteSide());
        build_->SetOrigin(x, y);
    }
    // ③ Assets: the decisions, as soon as the plan is approved.
    if ((stage == "planned" || stage == "sourced") && !build_->HasAssetNeeds() && !buildJob_ && !buildRun_) PreviewAssets();
    buildShown_ = build_->State();
    buildShown_["afterBackground"] = afterBackground_;
    post_(buildShown_);
    post_(build_->Card());
}

AssetSourcing EditorApp::SourcingNow() {
    const std::filesystem::path root = assets_.GetProject().root;
    AssetSourcing sourcing;
    sourcing.brief = build_->Brief();
    sourcing.settings = ReadSourcingSettings(root);
    sourcing.canGenerate = Ai() && Ai()->GetState() == AiClient::State::Ready;
    sourcing.strategyRules = build_->FromPlanner(); // the plan's art strategy for every part
    sourcing.checkFound = true;                      // found art looked at against the scene before it's used
    for (const auto& [key, crop] : build_->SketchCrops()) sourcing.sketched.insert(key); // made from the place's sketch
    sourcing.tried = build_->TriedSources();
    sourcing.library = LoadAssetLibrary(root);
    // The user's own library too, before anything is made or searched for.
    {
        const UserLibrary library(UserLibraryDir());
        for (auto& e : library.AsCatalog().entries) sourcing.library.entries.push_back(std::move(e));
        sourcing.importLibrary = [dir = UserLibraryDir(), root](const std::string& item) { return UserLibrary(dir).CopyToProject(item, root); };
    }
    std::ifstream in(build_->Folder() / "assets.json");
    const nlohmann::json saved = in ? nlohmann::json::parse(in, nullptr, false) : nlohmann::json();
    if (saved.is_object()) sourcing.overrides = saved.value("overrides", nlohmann::json::object());
    return sourcing;
}

void EditorApp::PreviewAssets() {
    if (!build_ || buildJob_ || buildRun_) return;
    PlaceSceneOptions options;
    options.place = build_->Id();
    std::tie(options.originX, options.originY) = build_->Origin();
    options.iso = iso_;
    options.isoGrid = ActiveSettings().grid.style != GridStyle::TopDown;
    options.hills = HillsFor(build_->Brief());
    ApplyBackground(options); // a painted background is the ground: no tiles asked for
    const TerrainSet terrains = ProjectTerrainSet(prefabs_, assets_.GetProject());
    const Catalog catalog = BuildCatalog(assets_, &Places(), CatalogOptionsNow());
    const AssetSourcing sourcing = SourcingNow();
    const nlohmann::json assets = PlanPlaceAssets(Places(), build_->Spec(), build_->Solved(), catalog, prefabs_, assets_, terrains, options, &sourcing);
    build_->SetAssetNeeds(AssetRows(assets));
}

void EditorApp::SetNeedChoice(const std::string& key, const nlohmann::json& choice) {
    const std::filesystem::path file = build_->Folder() / "assets.json";
    nlohmann::json saved;
    {
        std::ifstream in(file);
        saved = in ? nlohmann::json::parse(in, nullptr, false) : nlohmann::json();
    }
    if (!saved.is_object()) saved = nlohmann::json::object();
    if (!saved.contains("overrides") || !saved["overrides"].is_object()) saved["overrides"] = nlohmann::json::object();
    if (choice.is_null() || (choice.is_object() && choice.empty())) saved["overrides"].erase(key);
    else saved["overrides"][key] = choice;
    std::ofstream(file) << saved.dump(2) << "\n";
}

void EditorApp::MakeMatched() {
    // ③ Matched art (docs/places/matched-art.md): every need made by the art provider in one style, then cut
    // (CutMatchedArt).
    if (!build_ || buildJob_ || buildRun_) return;
    const std::string stage = build_->Stage();
    if (stage != "planned" && stage != "sourced" && stage != "built") {
        post_({{"type", "notify"}, {"text", "Not yet: approve the plan first"}});
        return;
    }
    if (const std::string why = build_->MakeMatchedArt(PlanForMatchedArt(), &Places()); !why.empty()) {
        post_({{"type", "notify"}, {"text", "No matched art: " + why}});
    }
}

void EditorApp::FindAssets() {
    // ③ Assets: each part's art decided (the project, the library, the web, the Art tool); what's
    // to be made or fetched starts, and the panel shows it as it comes. Build waits for the user.
    if (!build_ || buildJob_ || buildRun_) return;
    const std::string stage = build_->Stage();
    if (stage != "planned" && stage != "sourced" && stage != "built") {
        post_({{"type", "notify"}, {"text", "Not yet: approve the plan first"}});
        return;
    }
    if (!build_->HasOrigin()) {
        const auto [x, y] = FreeOrigin(registry_, build_->Id(), build_->SiteSide());
        build_->SetOrigin(x, y);
    }
    PlaceSceneOptions options;
    options.place = build_->Id();
    std::tie(options.originX, options.originY) = build_->Origin();
    options.iso = iso_;
    options.isoGrid = ActiveSettings().grid.style != GridStyle::TopDown;
    options.hills = HillsFor(build_->Brief());
    ApplyBackground(options); // a painted background is the ground: no tiles asked for
    const TerrainSet terrains = ProjectTerrainSet(prefabs_, assets_.GetProject());
    const Catalog catalog = BuildCatalog(assets_, &Places(), CatalogOptionsNow());
    const AssetSourcing sourcing = SourcingNow();
    const nlohmann::json assets = PlanPlaceAssets(Places(), build_->Spec(), build_->Solved(), catalog, prefabs_, assets_, terrains, options, &sourcing);
    build_->MarkSourced(AssetRows(assets));
    if (!assets.value("pending", nlohmann::json::array()).empty()) build_->StartSourcing(assets["pending"], sourcing.settings);
    PostBuild();
}

void EditorApp::BuildIntoScene() {
    if (!build_ || buildJob_ || buildRun_) return;
    if (playing_) {
        post_({{"type", "notify"}, {"text", "Stop Play mode before building."}});
        return;
    }
    if (const std::string why = build_->StartBuilding(); !why.empty()) {
        post_({{"type", "notify"}, {"text", "Not yet: " + why}});
        return;
    }
    if (!build_->HasOrigin()) {
        const auto [x, y] = FreeOrigin(registry_, build_->Id(), build_->SiteSide());
        build_->SetOrigin(x, y);
    }
    buildRun_ = std::make_unique<BuildRun>();
    buildRun_->options.place = build_->Id();
    std::tie(buildRun_->options.originX, buildRun_->options.originY) = build_->Origin();
    buildRun_->options.iso = iso_;
    buildRun_->options.isoGrid = ActiveSettings().grid.style != GridStyle::TopDown;
    buildRun_->options.hills = HillsFor(build_->Brief());
    ApplyBackground(buildRun_->options);
    SetMoodGrade(buildRun_->options, build_->Brief(), build_->Story()); // the place's mood, as one grade
    build_->SetBuildProgress(0, 5, "Choosing the art for each part…");
    PostBuild(); // ④ Build shows while it works
}

void EditorApp::StepBuildRun() {
    if (!buildRun_ || !build_) return;
    BuildRun& run = *buildRun_;
    try {
        switch (run.phase) {
        case 0: {
            // The art for each part: what ③ Assets found and made (nothing is fetched now: what's
            // still missing is a placeholder).
            run.terrains = ProjectTerrainSet(prefabs_, assets_.GetProject());
            const Catalog catalog = BuildCatalog(assets_, &Places(), CatalogOptionsNow());
            run.sourcing = SourcingNow();
            run.sourcing.canGenerate = false;
            run.assets = PlanPlaceAssets(Places(), build_->Spec(), build_->Solved(), catalog, prefabs_, assets_, run.terrains, run.options, &run.sourcing);
            build_->SetBuildProgress(1, 5, "Placing the ground, the buildings and what's around them…");
            break;
        }
        case 1:
            run.command = BuildPlaceScene(registry_, prefabs_, build_->Solved(), run.assets, run.terrains, run.options, &run.report);
            build_->SetBuildProgress(2, 5, "Checking the place: every door reachable, nothing overlapping, the art in place…");
            break;
        case 2: {
            run.verify = VerifyPlaceScene(registry_, build_->Solved(), run.assets, run.options);
            run.repairs = nlohmann::json::array();
            run.after = run.verify;
            int failing = 0;
            for (const auto& c : run.verify) failing += !c.value("ok", true) && !c.value("warning", false);
            build_->SetBuildProgress(3, 5, failing ? "Fixing " + std::to_string(failing) + " check(s) that failed…" : "All checks passed.");
            break;
        }
        case 3: {
            bool failing = false;
            for (const auto& c : run.verify) failing = failing || (!c.value("ok", true) && !c.value("warning", false));
            if (failing) {
                run.repairs = RepairPlaceScene(registry_, prefabs_, build_->Solved(), run.assets, run.verify, run.options);
                run.after = VerifyPlaceScene(registry_, build_->Solved(), run.assets, run.options);
            }
            build_->SetBuildProgress(4, 5, "Saving it as one undo step and drawing the result…");
            break;
        }
        default: {
            history_.PushCommand(std::move(run.command));
            // The ground's pictures were painted again under the same names: drawn anew.
            for (const auto& picture : run.report.value("groundPictures", nlohmann::json::array())) {
                if (picture.is_string()) renderer_.InvalidateTexture(picture.get<std::string>());
            }
            prefabs_.SyncAll(registry_);
            UpdateWorldToScreen(registry_, iso_);
            dirty_ = true;
            editorStateStale_ = true;
            sceneTreeStale_ = true;
            const std::filesystem::path root = assets_.GetProject().root;
            // The decided plan (with the user's overrides kept), and the credits for what needs attribution.
            {
                nlohmann::json saved = {{"needs", run.assets["needs"]}, {"overrides", run.sourcing.overrides}};
                std::ofstream(build_->Folder() / "assets.json") << saved.dump(2) << "\n";
                nlohmann::json used = nlohmann::json::array();
                for (const auto& n : run.assets["needs"]) {
                    if (n.value("source", "") == "web" || n.value("source", "") == "library") {
                        used.push_back({{"path", n.value("entry", "")}, {"licence", n.value("licence", "")}, {"author", n.value("author", "")},
                                        {"url", n.value("url", "")}, {"source", n.value("source", "")}});
                    }
                }
                if (!used.empty()) WriteCredits(root, used, run.sourcing.settings.licences);
            }
            // What it looks like in the scene, drawn as the renderer draws it: for the Result step.
            // A few of its pieces kept as the project's style references (place-refine.md §15): its
            // buildings, its ground, a few things; later art is briefed and ranked to match them.
            if (Ai() && Ai()->GetState() == AiClient::State::Ready) {
                nlohmann::json pieces = nlohmann::json::array();
                auto keep = [&](const nlohmann::json& n, int most) {
                    int taken = 0;
                    for (const auto& p : pieces) taken += p.value("layer", "") == n.value("layer", "");
                    const std::string entry = n.value("entry", "");
                    if (taken >= most || entry.empty() || entry.find('#') != std::string::npos || n.value("source", "") == "placeholder") return;
                    pieces.push_back({{"path", entry}, {"role", n.value("role", "")}, {"name", n.value("title", "")}, {"layer", n.value("layer", "")}});
                };
                for (const auto& n : run.assets.value("needs", nlohmann::json::array())) {
                    if (n.value("layer", "") == "building") keep(n, 3);
                }
                for (const auto& n : run.assets.value("needs", nlohmann::json::array())) {
                    if (n.value("layer", "") == "prop") keep(n, 3);
                }
                const nlohmann::json terrains = run.assets.value("terrains", nlohmann::json::object());
                for (const auto& [t, g] : terrains.items()) {
                    if (g.value("source", "") == "generated" && !g.value("entry", "").empty()) {
                        pieces.push_back({{"path", g.value("entry", "")}, {"role", t + " ground"}, {"layer", "ground"}});
                        break;
                    }
                }
                if (!pieces.empty()) Ai()->Request("POST", "/assets/references", {{"pieces", pieces}}, 60);
            }
            const int built = build_->SiteSide();
            const ImageData picture = RenderScenePreview(registry_, assets_.GetProject(), iso_, run.options.originX, run.options.originY,
                                                         run.options.originX + built, run.options.originY + built, 1600);
            WritePng(build_->Folder() / "scene.png", picture);
            static int pictures = 0;
            build_->MarkSourced(AssetRows(run.assets));
            build_->Built({{"assets", run.assets}, {"report", run.report}, {"verify", run.verify}, {"repairs", run.repairs}, {"after", run.after},
                           {"origin", {run.options.originX, run.options.originY}},
                           {"image", "places/" + build_->Id() + "/scene.png?v=" + std::to_string(++pictures)}});
            // Look at it.
            const int side = build_->SiteSide();
            const ScreenPose centre = ProjectIso(run.options.originX + side / 2.0f, run.options.originY + side / 2.0f, iso_);
            int width = 0, height = 0;
            viewport_.GetFramebufferSize(width, height);
            camera_.offsetX = centre.x - width / (2.0f * camera_.zoom);
            camera_.offsetY = centre.y - height / (2.0f * camera_.zoom);
            buildRun_.reset();
            PostBuild();
            return;
        }
        }
        ++run.phase;
    } catch (const std::exception& e) {
        // Never a crash: what failed is said, and the build can be tried again.
        post_({{"type", "notify"}, {"text", std::string("The build failed: ") + e.what()}});
        build_->BuildFailed(e.what());
        buildRun_.reset();
    }
    PostBuild();
}

void EditorApp::SendToAi(const std::string& text, const std::string& focus, bool asChange) {
    if (!Ai() || text.empty()) {
        return;
    }
    if (playing_) {
        post_({{"type", "ai:message"}, {"role", "error"}, {"text", "Stop Play mode before asking for changes."}});
        return;
    }
    // A change to the place the Build panel has open goes there, as a revision (7.8), while it's
    // being planned. Once it's built it's the scene's: the AI panel edits it (adds things, changes
    // them) like any other part of the scene.
    if (!asChange && build_ && !buildJob_ && (build_->Stage() == "plan" || build_->Stage() == "planned") &&
        build_->AboutThisPlace(text)) {
        conversation_.emplace_back("user", text);
        post_({{"type", "ai:message"}, {"role", "user"}, {"text", text}});
        const std::string why = build_->Revise(text);
        post_({{"type", "ai:message"}, {"role", "assistant"},
               {"text", why.empty() ? "That's about " + build_->Brief().value("name", build_->Id()) +
                                          ", so it went to the Build panel as a revision (see Revise there)."
                                    : "Couldn't revise the place: " + why}});
        post_({{"type", "build:open"}});
        PostBuild();
        return;
    }
    // "Build the game": the validated plan's places built, quests bound, files written, playtested (23).
    if (!asChange && plan_ && LooksLikeBuildGameRequest(text)) {
        conversation_.emplace_back("user", text);
        post_({{"type", "ai:message"}, {"role", "user"}, {"text", text}});
        HandlePlannerMessage("planner:buildGame", {{"mode", ""}});
        const bool started = gameBuild_ && gameBuild_->Active();
        post_({{"type", "ai:message"}, {"role", "assistant"},
               {"text", started ? "Building the game from the plan: follow it in the Planner panel (⑩–⑬)."
                                : "The plan has to be validated first (⑨ in the Planner panel)."}});
        post_({{"type", "planner:open"}});
        return;
    }
    // A change to the plan that's open ("make Aldric the mentor's son") goes to it as an edit (22).
    if (!asChange && plan_ && plan_->AboutThePlan(text) && !LooksLikeGameRequest(text)) {
        conversation_.emplace_back("user", text);
        post_({{"type", "ai:message"}, {"role", "user"}, {"text", text}});
        const std::string why = plan_->Revise(text);
        post_({{"type", "ai:message"}, {"role", "assistant"},
               {"text", why.empty() ? "That's about the plan, so it went to the Planner panel as a revision."
                                    : "Couldn't revise the plan: " + why}});
        post_({{"type", "planner:open"}});
        PostPlanner();
        return;
    }
    // Designing a whole game goes to the Planner panel (22).
    if (!asChange && LooksLikeGameRequest(text)) {
        conversation_.emplace_back("user", text);
        post_({{"type", "ai:message"}, {"role", "user"}, {"text", text}});
        StartPlan(text, false);
        return;
    }
    // Building a place goes to the Build panel, not a proposal card.
    const bool aboutBuilt = build_ && build_->Stage() == "built" && build_->AboutThisPlace(text); // "add a well to the village"
    if (!asChange && !aboutBuilt && LooksLikeBuildRequest(Places(), text)) {
        conversation_.emplace_back("user", text);
        post_({{"type", "ai:message"}, {"role", "user"}, {"text", text}});
        StartBuild(text);
        return;
    }
    ContextOptions options;
    options.selection = selected_;
    options.focus = focus;
    options.scene = OpenSceneRelative();
    nlohmann::json context =
        BuildEditorContext(registry_, EngineReflection(), prefabs_, assets_, assets_.GetProject(), options);

    // The last few turns, so follow-ups like "now make them faster" work.
    nlohmann::json history = nlohmann::json::array();
    size_t first = conversation_.size() > 8 ? conversation_.size() - 8 : 0;
    for (size_t i = first; i < conversation_.size(); ++i) {
        history.push_back({{"role", conversation_[i].first}, {"content", conversation_[i].second}});
    }

    int id = Ai()->Propose(text, context, history);
    requestInstructions_[id] = text;
    conversation_.emplace_back("user", text);
    post_({{"type", "ai:message"}, {"role", "user"}, {"text", text}, {"requestId", id}});
    post_({{"type", "ai:busy"}, {"busy", true}});
}

namespace {

nlohmann::json ItemsJson(const std::vector<ChangeItem>& changes) {
    nlohmann::json items = nlohmann::json::array();
    for (size_t i = 0; i < changes.size(); ++i) {
        const ChangeItem& item = changes[i];
        items.push_back({{"index", i},
                         {"kind", item.kind},
                         {"target", item.target},
                         {"before", item.before},
                         {"after", item.after},
                         {"field", item.field},
                         {"detail", item.detail},
                         {"touchesFiles", item.touchesFiles},
                         {"error", item.error}});
    }
    return items;
}

std::string FailureText(const nlohmann::json& body, const std::string& lead) {
    std::string text = lead;
    for (const auto& error : body.value("errors", nlohmann::json::array())) {
        text += "\n• " + error.get<std::string>();
    }
    return text;
}

} // namespace

void EditorApp::OnProposal(int requestId, const nlohmann::json& body) {
    if (cancelledRequests_.erase(requestId) > 0) {
        return; // a step of a task the user stopped
    }
    if (task_ && requestId == taskRequestId_) {
        OnTaskAnswer(body);
        return;
    }
    post_({{"type", "ai:busy"}, {"busy", false}});
    std::string instruction = requestInstructions_[requestId];
    requestInstructions_.erase(requestId);

    if (!body.value("ok", false) || !body.contains("proposal") || body["proposal"].is_null()) {
        post_({{"type", "ai:message"}, {"role", "error"}, {"text", FailureText(body, "The AI couldn't produce a valid change.")}});
        conversation_.emplace_back("assistant", "(failed: no valid proposal)");
        return;
    }

    const nlohmann::json& proposal = body["proposal"];
    const std::string scope = proposal.value("scope", "small");
    if (scope == "split") {
        // Too big for one go: the AI suggests smaller requests instead.
        proposals_[requestId] = {instruction, proposal, true};
        conversation_.emplace_back("assistant", proposal.value("notes", "") + " Suggested parts: " +
                                                    proposal.value("split", nlohmann::json::array()).dump());
        post_({{"type", "ai:split"},
               {"id", requestId},
               {"summary", proposal.value("summary", "")},
               {"notes", proposal.value("notes", "")},
               {"parts", proposal.value("split", nlohmann::json::array())}});
        LogAiDecision(requestId, {{"decision", "split"}});
        return;
    }
    if (scope == "task") {
        StartTask(requestId, instruction, proposal, body.value("usage", nlohmann::json::object()));
        return;
    }

    ResolvedProposal resolved = ResolveProposal(proposal, registry_, EngineReflection(), prefabs_, assets_.GetProject());
    nlohmann::json items = ItemsJson(resolved.items);
    PendingProposal& pending = proposals_[requestId];
    pending.instruction = instruction;
    pending.proposal = proposal;
    conversation_.emplace_back("assistant", resolved.summary + (resolved.notes.empty() ? "" : " " + resolved.notes));

    // Small tweaks the user's review policy lets through are applied now, as
    // one undo step; the rest waits for review as usual.
    ReviewPolicy policy = CurrentReviewPolicy();
    std::vector<nlohmann::json> autoOps;
    for (size_t i = 0; i < resolved.items.size(); ++i) {
        const ChangeItem& item = resolved.items[i];
        if (AppliesWithoutReview(item, policy)) {
            pending.autoApplied.push_back(i);
            pending.autoUndo.push_back({*InverseOperation(item), item.after, item.target});
            autoOps.push_back(item.op);
            items[i]["autoApplied"] = true;
        }
    }
    ApplyOutcome autoOutcome;
    if (!autoOps.empty() && !playing_) {
        autoOutcome = ApplyAccepted(autoOps);
        pending.decided = pending.autoApplied.size() == resolved.items.size();
    } else {
        pending.autoApplied.clear();
        pending.autoUndo.clear();
    }

    nlohmann::json usage = body.value("usage", nlohmann::json::object());
    post_({{"type", "ai:proposal"},
           {"id", requestId},
           {"instruction", instruction},
           {"summary", resolved.summary},
           {"notes", resolved.notes},
           {"items", items},
           {"autoApplied", pending.autoApplied},
           {"decided", pending.decided},
           {"autoErrors", autoOutcome.errors},
           {"usage", usage},
           {"model", body.value("model", "")}});
    if (RequestConsistencyCheck(requestId, proposal.value("operations", nlohmann::json::array()))) {
        post_({{"type", "ai:consistency"}, {"id", requestId}, {"result", {{"checking", true}}}});
    }
    if (!pending.autoApplied.empty()) {
        LogAiDecision(requestId, {{"decision", "auto-applied"},
                                  {"policy", ReviewPolicyName(policy)},
                                  {"accepted", pending.autoApplied},
                                  {"appliedCount", autoOutcome.applied},
                                  {"errors", autoOutcome.errors},
                                  {"operations", autoOps}});
    }
}

// Proposals that write canon, dialogue or quests are checked against the
// World Bible by a second model (development_plan.md 3.3). The answer comes
// later, as ai:consistency; it flags, it doesn't block.
bool EditorApp::RequestConsistencyCheck(int proposalId, const nlohmann::json& operations) {
    bool makesClaims = false;
    for (const auto& op : operations) {
        const std::string kind = op.value("op", "");
        makesClaims |= kind == "write_dialogue" || kind == "write_quest" ||
                       (kind == "command" && op.value("name", "") == "world.write");
    }
    if (!makesClaims || Ai() == nullptr || Ai()->GetState() != AiClient::State::Ready) {
        return false;
    }
    int id = Ai()->Request("POST", "/consistency", {{"operations", operations}}, 90);
    consistencyRequests_[id] = proposalId;
    return true;
}

ReviewPolicy EditorApp::CurrentReviewPolicy() const {
    return userPrefs_ ? ParseReviewPolicy(userPrefs_->Get("reviewPolicy", "").get<std::string>())
                      : ReviewPolicy::ReviewAll;
}

// Applies proposal operations as one undo step (scene side) and refreshes
// whatever they touched.
ApplyOutcome EditorApp::ApplyAccepted(const std::vector<nlohmann::json>& operations, const TaskRunner* task,
                                      const std::vector<size_t>& indices) {
    ApplyOutcome outcome;
    ApplyOneShotEdit([&] {
        if (task != nullptr) {
            // A task's result: replay it, translating the entities it spawned.
            std::vector<std::pair<size_t, nlohmann::json>> accepted;
            for (size_t i = 0; i < operations.size(); ++i) {
                accepted.emplace_back(indices[i], operations[i]);
            }
            outcome = task->Replay(accepted, registry_, prefabs_, assets_.GetProject());
        } else {
            outcome = ApplyOperations(operations, registry_, EngineReflection(), prefabs_, assets_.GetProject());
        }
        return outcome.applied > 0;
    });
    // The ground it changed (a built place's tile layers): its own undo step, the cells as they were.
    if (!outcome.layerEdits.empty()) {
        history_.PushCommand(std::make_unique<TileLayerCommand>(std::move(outcome.layerEdits), "AI: the ground"));
        dirty_ = true;
    }
    if (outcome.settingsChanged) {
        settingsDirty_ = true;
        post_(SettingsJson());
    }
    if (outcome.prefabsChanged) {
        post_(PrefabsJson());
    }
    sceneTreeStale_ = true;
    inspectorStale_ = true;
    editorStateStale_ = true;
    return outcome;
}

// --- Bigger tasks (development_plan.md 2.9) ---------------------------------
// The AI answered a request with scope "task": it's carried out step by step
// on a working copy (TaskRunner), with each step's results sent back for the
// next, and the combined result is reviewed like any other proposal.

void EditorApp::StartTask(int requestId, const std::string& instruction, const nlohmann::json& firstAnswer,
                          const nlohmann::json& usage) {
    task_ = std::make_shared<TaskRunner>(registry_, assets_.GetProject(), EngineReflection(), instruction);
    taskId_ = requestId;
    proposals_[requestId] = {instruction, firstAnswer, false};
    FeedTask(firstAnswer, usage.value("inputTokens", 0));
}

void EditorApp::FeedTask(const nlohmann::json& answer, int inputTokens) {
    TaskRunner::State state = task_->OnAnswer(answer, inputTokens);
    const TaskStep& step = task_->Steps().back();
    nlohmann::json problems = nlohmann::json::array();
    for (const auto& problem : step.problems) {
        problems.push_back({{"path", problem.path}, {"message", problem.message}, {"count", problem.count}});
    }
    post_({{"type", "ai:taskProgress"},
           {"id", taskId_},
           {"instruction", task_->Instruction()},
           {"plan", task_->Plan()},
           {"step", step.number},
           {"summary", step.summary},
           {"applyErrors", step.applyErrors},
           {"problems", problems},
           {"state", state == TaskRunner::State::Working ? "working" : "finished"}});
    if (state != TaskRunner::State::Working) {
        FinishTask();
        return;
    }
    if (Ai() == nullptr) {
        StopTask(taskId_, "AI was turned off.");
        return;
    }
    taskRequestId_ = Ai()->Propose(task_->NextInstruction(), task_->Context(assets_), task_->History());
    post_({{"type", "ai:busy"}, {"busy", true}});
}

void EditorApp::OnTaskAnswer(const nlohmann::json& body) {
    if (!body.value("ok", false) || !body.contains("proposal") || body["proposal"].is_null()) {
        const std::string reason = FailureText(body, "The AI couldn't produce the next step.");
        if (task_ && !task_->Steps().empty()) {
            // Keep what the earlier steps did (a whole village, say): it goes
            // up for review with the reason the task ended there.
            task_->GiveUp(reason);
            FinishTask();
            return;
        }
        StopTask(taskId_, reason);
        return;
    }
    FeedTask(body["proposal"], body.value("usage", nlohmann::json::object()).value("inputTokens", 0));
}

// The task is over (finished, or at its limits): its combined result goes up
// for review. Accepting replays it on the real scene (ApplyProposal).
void EditorApp::FinishTask() {
    post_({{"type", "ai:busy"}, {"busy", false}});
    std::shared_ptr<TaskRunner> task = std::move(task_);
    PendingProposal& pending = proposals_[taskId_];
    pending.proposal = task->CombinedProposal();
    pending.task = task;
    nlohmann::json steps = nlohmann::json::array();
    for (const auto& step : task->Steps()) {
        steps.push_back({{"number", step.number},
                         {"summary", step.summary},
                         {"problems", step.problems.size()},
                         {"applyErrors", step.applyErrors.size()}});
    }
    conversation_.emplace_back("assistant", "Carried out in " + std::to_string(task->Steps().size()) +
                                                " steps; result: " + pending.proposal["operations"].dump());
    post_({{"type", "ai:proposal"},
           {"id", taskId_},
           {"instruction", task->Instruction()},
           {"summary", task->Instruction()},
           {"notes", task->StopReason()},
           {"items", ItemsJson(task->CombinedItems())},
           {"task", {{"plan", task->Plan()}, {"steps", steps}, {"stopped", task->StopReason()}}},
           {"usage", nlohmann::json::object()}});
    if (RequestConsistencyCheck(taskId_, pending.proposal.value("operations", nlohmann::json::array()))) {
        post_({{"type", "ai:consistency"}, {"id", taskId_}, {"result", {{"checking", true}}}});
    }
    LogAiDecision(taskId_, {{"decision", "task-finished"},
                            {"steps", task->Steps().size()},
                            {"stopReason", task->StopReason()},
                            {"history", task->History()}});
}

void EditorApp::PostProposalStates() {
    for (auto& [id, pending] : proposals_) {
        if (pending.historyStep == 0) continue;
        const uint64_t step = pending.historyStep;
        // Neither done nor undone: undone, then a new edit replaced its redo
        // (it can still be applied afresh), or it fell off the end of the
        // history (it stays, but can't be undone from here).
        std::string state = history_.IsDone(step)                                    ? "applied"
                            : history_.IsUndone(step) || pending.lastState.rfind("undone", 0) == 0 ? "undone"
                                                                                                  : "settled";
        const bool canUndo = history_.NextUndo() == step;
        const bool canApply = history_.NextRedo() == step || (!history_.IsDone(step) && !pending.applied.empty());
        const std::string key = state + (canUndo ? "u" : "") + (canApply ? "a" : "");
        if (key == pending.lastState) continue;
        pending.lastState = key;
        post_({{"type", "ai:applyState"}, {"id", id}, {"state", state}, {"canUndo", canUndo}, {"canApply", canApply}});
    }
}

void EditorApp::UndoProposal(int proposalId) {
    auto it = proposals_.find(proposalId);
    if (it == proposals_.end() || it->second.historyStep == 0 || history_.NextUndo() != it->second.historyStep) {
        return; // later changes come first (Ctrl+Z them)
    }
    Undo();
}

void EditorApp::ReapplyProposal(int proposalId) {
    auto it = proposals_.find(proposalId);
    if (it == proposals_.end() || it->second.historyStep == 0 || playing_) {
        return;
    }
    if (history_.NextRedo() == it->second.historyStep) {
        Redo();
        return;
    }
    if (history_.IsDone(it->second.historyStep) || it->second.applied.empty()) {
        return;
    }
    // Undone, and edits since then replaced its redo: apply it again.
    const uint64_t before = history_.NextUndo();
    ApplyOutcome outcome = ApplyAccepted(it->second.applied);
    if (history_.NextUndo() != before) it->second.historyStep = history_.NextUndo();
    if (!outcome.errors.empty()) {
        post_({{"type", "notify"}, {"text", "Applied again, with problems: " + outcome.errors.front()}});
    }
    editorStateStale_ = true;
}

// Stop: the working copy is discarded and nothing reaches the project.
void EditorApp::StopTask(int id, const std::string& reason) {
    if (!task_ || id != taskId_) {
        return;
    }
    if (taskRequestId_ != 0) {
        cancelledRequests_.insert(taskRequestId_);
    }
    LogAiDecision(taskId_, {{"decision", "task-stopped"}, {"reason", reason}, {"steps", task_->Steps().size()}});
    task_.reset();
    taskRequestId_ = 0;
    proposals_[id].decided = true;
    post_({{"type", "ai:busy"}, {"busy", false}});
    post_({{"type", "ai:taskStopped"}, {"id", id}, {"reason", reason}});
}

// "Undo" on auto-applied changes: puts each value back, unless it has been
// changed again since (then the newer value wins and it's reported).
void EditorApp::UndoAutoApplied(int proposalId) {
    auto it = proposals_.find(proposalId);
    if (it == proposals_.end() || it->second.autoApplied.empty() || it->second.autoUndone || playing_) {
        return;
    }
    PendingProposal& pending = it->second;
    std::vector<nlohmann::json> restore;
    nlohmann::json skipped = nlohmann::json::array();
    for (const auto& undo : pending.autoUndo) {
        // Resolving the inverse reports the current value as its "before".
        nlohmann::json single = {{"operations", nlohmann::json::array({undo.op})}};
        ResolvedProposal check = ResolveProposal(single, registry_, EngineReflection(), prefabs_, assets_.GetProject());
        const ChangeItem& item = check.items.front();
        if (item.error.empty() && item.before == undo.after) {
            restore.push_back(undo.op);
        } else {
            skipped.push_back(undo.target);
        }
    }
    ApplyOutcome outcome = restore.empty() ? ApplyOutcome{} : ApplyAccepted(restore);
    pending.autoUndone = true;
    post_({{"type", "ai:autoUndone"}, {"id", proposalId}, {"restored", outcome.applied}, {"skipped", skipped}});
    LogAiDecision(proposalId, {{"decision", "auto-undone"}, {"operations", restore}, {"skipped", skipped}});
}

void EditorApp::ApplyProposal(int proposalId, const nlohmann::json& decisions) {
    auto it = proposals_.find(proposalId);
    if (it == proposals_.end() || it->second.decided || playing_) {
        return;
    }
    const nlohmann::json operations = it->second.proposal.value("operations", nlohmann::json::array());

    // Accepted operations, with any edits made in review (a changed value, a
    // tweaked script) folded in.
    std::vector<nlohmann::json> accepted;
    nlohmann::json acceptedIndices = nlohmann::json::array();
    for (const auto& decision : decisions) {
        size_t index = decision.value("index", static_cast<size_t>(-1));
        if (index >= operations.size() || !decision.value("accepted", false)) {
            continue;
        }
        const auto& autoApplied = it->second.autoApplied;
        if (std::find(autoApplied.begin(), autoApplied.end(), index) != autoApplied.end()) {
            continue; // already applied without review
        }
        nlohmann::json op = operations[index];
        if (decision.contains("value")) {
            op["value"] = decision["value"];
        }
        if (decision.contains("source") && op.value("name", "") == "script.write") {
            op["args"]["source"] = decision["source"];
        }
        accepted.push_back(op);
        acceptedIndices.push_back(index);
    }

    std::vector<size_t> indices = acceptedIndices.get<std::vector<size_t>>();
    const uint64_t before = history_.NextUndo();
    ApplyOutcome outcome = ApplyAccepted(accepted, it->second.task.get(), indices); // one undo step (scene side)
    it->second.decided = true;
    // A task's operations refer to its working copy's entities: only plain
    // proposals can be applied afresh (a task's result comes back by redo).
    if (!it->second.task) it->second.applied = accepted;
    it->second.task.reset(); // done with its working copy
    if (history_.NextUndo() != before) {
        it->second.historyStep = history_.NextUndo();
        it->second.lastState = "applied";
    }

    post_({{"type", "ai:decided"},
           {"id", proposalId},
           {"status", outcome.errors.empty() ? "applied" : "partial"},
           {"applied", outcome.applied},
           {"errors", outcome.errors}});

    // Schema-valid isn't the same as working: play the scene headless for a
    // moment and report errors from any script this proposal wrote.
    std::vector<std::string> writtenScripts;
    for (const auto& op : accepted) {
        if (op.value("op", "") == "command" && op.value("name", "") == "script.write") {
            writtenScripts.push_back(op["args"].value("path", ""));
        }
    }
    if (!writtenScripts.empty()) {
        auto problems = DryRunScripts(SerializeScene(registry_), assets_.GetProject(), 2.0, writtenScripts);
        nlohmann::json list = nlohmann::json::array();
        for (const auto& problem : problems) {
            list.push_back({{"path", problem.path}, {"message", problem.message}, {"count", problem.count}});
        }
        post_({{"type", "ai:scriptCheck"}, {"id", proposalId}, {"scripts", writtenScripts}, {"problems", list}});
    }
    LogAiDecision(proposalId, {{"decision", "applied"},
                               {"accepted", acceptedIndices},
                               {"appliedCount", outcome.applied},
                               {"errors", outcome.errors},
                               {"operations", accepted}});
}

void EditorApp::RejectProposal(int proposalId) {
    auto it = proposals_.find(proposalId);
    if (it == proposals_.end() || it->second.decided) {
        return;
    }
    it->second.decided = true;
    it->second.task.reset();
    post_({{"type", "ai:decided"}, {"id", proposalId}, {"status", "rejected"}});
    LogAiDecision(proposalId, {{"decision", "rejected"}});
}

// Every AI proposal and what was decided, appended to the project's
// ai_log.jsonl — the audit trail product.md §7 asks for.
void EditorApp::LogAiDecision(int proposalId, const nlohmann::json& decision) {
    const auto& pending = proposals_[proposalId];
    auto now = std::chrono::system_clock::now();
    nlohmann::json entry = {{"time", std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count()},
                            {"instruction", pending.instruction},
                            {"summary", pending.proposal.value("summary", "")},
                            {"proposed", pending.proposal.value("operations", nlohmann::json::array())}};
    entry.update(decision);
    std::ofstream log(assets_.GetProject().root / "ai_log.jsonl", std::ios::app);
    log << entry.dump() << "\n";
}

// --- Prefabs ----------------------------------------------------------------

void EditorApp::RefreshPrefabs() {
    // Prefab files edited outside the editor (or by "Apply to prefab")
    // update every copy in the open scene.
    for (const auto& path : prefabs_.Refresh()) {
        prefabs_.SyncAll(registry_, path);
        sceneTreeStale_ = true;
        inspectorStale_ = true;
    }
    AnalyzeMissingTileEdges();
    post_(PrefabsJson());
}

void EditorApp::EnsureStarterPrefabs() {
    // A project with no prefabs yet gets the built-in placeholder set, so the
    // palette is never empty. They're ordinary files: edit, restyle or delete.
    std::error_code ec;
    std::filesystem::path dir = assets_.GetProject().root / "prefabs";
    if (std::filesystem::exists(dir, ec)) {
        return;
    }
    Registry scratch;
    auto save = [&](Entity entity, const std::string& name) {
        prefabs_.SaveAsPrefab(scratch, entity, "prefabs/" + name + ".prefab");
    };
    save(CreateTile(scratch, 0, 0, TileKind::Grass), "grass");
    save(CreateTile(scratch, 0, 0, TileKind::Dirt), "dirt");
    save(CreateTile(scratch, 0, 0, TileKind::Wall), "wall");
    save(CreatePlayer(scratch, 0.0f, 0.0f), "player");
    save(CreateNpc(scratch, 0.0f, 0.0f), "npc");
    save(CreateEnemy(scratch, 0.0f, 0.0f, entt::null), "enemy");
    save(CreateHealthPickup(scratch, 0.0f, 0.0f), "potion");
    save(CreateGateway(scratch, 0.0f, 0.0f), "gateway");
}

// Saves a file from the Story tab; `originalId` set and different means a
// rename (the old file goes).
std::optional<std::string> EditorApp::SaveStoryFile(const std::string& folder, const std::string& id,
                                                    const std::string& originalId, const nlohmann::json& content) {
    const std::filesystem::path root = assets_.GetProject().root;
    std::error_code ec;
    if (!originalId.empty() && originalId != id && std::filesystem::exists(root / folder / (id + ".json"), ec)) {
        return folder + "/" + id + ".json already exists";
    }
    if (auto error = folder == "schedules" ? WriteScheduleFile(root, id, content) : WriteStoryFile(root, folder, id, content)) {
        return error;
    }
    if (!originalId.empty() && originalId != id) {
        std::filesystem::remove(root / folder / (originalId + ".json"), ec);
    }
    return std::nullopt;
}

nlohmann::json EditorApp::StoryJson() {
    story_.Refresh();
    world_.Refresh();
    nlohmann::json dialogues = nlohmann::json::array();
    for (const auto& dialogue : story_.Dialogues()) {
        std::string id = std::filesystem::path(dialogue.path).stem().string();
        dialogues.push_back({{"id", id}, {"file", dialogue.path}, {"content", DialogueToJson(dialogue)}});
    }
    nlohmann::json quests = nlohmann::json::array();
    for (const auto& quest : story_.Quests()) {
        quests.push_back({{"id", quest.id}, {"file", "quests/" + quest.id + ".json"}, {"content", QuestToJson(quest)}});
    }
    std::vector<std::string> prefabPaths;
    for (const auto& info : prefabs_.Prefabs()) {
        prefabPaths.push_back(info.path);
    }
    nlohmann::json problems = nlohmann::json::array();
    for (const auto& problem : story_.Validate()) {
        problems.push_back({{"file", problem.file}, {"message", problem.message}});
    }
    // Every scene's markers; the open scene's as they are now (maybe unsaved).
    auto markerPositions = MarkerPositions(registry_);
    SceneMarkerMap sceneMarkers;
    for (const std::string& scene : ListScenes(assets_.GetProject())) {
        sceneMarkers[scene] = SceneMarkers(assets_.GetProject(), scene);
    }
    {
        std::vector<std::string> open;
        for (const auto& [name, pos] : markerPositions) open.push_back(name);
        sceneMarkers[OpenSceneRelative()] = open;
    }
    for (const auto& problem : ValidateStoryReferences(story_, world_, prefabPaths, &sceneMarkers)) {
        problems.push_back({{"file", problem.file}, {"message", problem.message}});
    }
    schedules_.Refresh();
    nlohmann::json schedules = nlohmann::json::array();
    for (const auto& schedule : schedules_.Schedules()) {
        const std::string file = "schedules/" + schedule.id + ".json";
        schedules.push_back({{"id", schedule.id}, {"file", file}, {"content", ScheduleToJson(schedule)}});
        for (const auto& message : ValidateSchedule(schedule, [&](const std::string& scene, const std::string& name) {
                 const auto it = sceneMarkers.find(scene.empty() ? OpenSceneRelative() : scene);
                 return it != sceneMarkers.end() && std::find(it->second.begin(), it->second.end(), name) != it->second.end();
             })) {
            problems.push_back({{"file", file}, {"message", message}});
        }
    }
    for (const auto& [file, message] : schedules_.LoadProblems()) {
        problems.push_back({{"file", file}, {"message", message}});
    }
    nlohmann::json markerNames = nlohmann::json::array();
    for (const auto& [name, pos] : markerPositions) {
        markerNames.push_back(name);
    }
    nlohmann::json characters = nlohmann::json::array();
    for (const auto& entry : world_.Entries()) {
        characters.push_back({{"key", entry.Key()}, {"name", entry.name}, {"kind", entry.kind}});
    }
    // Who uses each dialogue in the open scene (so the panel can say "Talk on Villager").
    nlohmann::json usedBy = nlohmann::json::object();
    for (auto [entity, talk] : registry_.view<Talk>().each()) {
        const auto* name = registry_.try_get<Name>(entity);
        usedBy[talk.dialogue].push_back(name ? name->value : "Entity #" + std::to_string(static_cast<uint32_t>(entity)));
    }
    // ... and each schedule ("Routine on Villager").
    for (auto [entity, routine] : registry_.view<Routine>().each()) {
        const auto* name = registry_.try_get<Name>(entity);
        usedBy[routine.schedule].push_back(name ? name->value : "Entity #" + std::to_string(static_cast<uint32_t>(entity)));
    }
    return {{"type", "story:list"},
            {"dialogues", dialogues},
            {"quests", quests},
            {"schedules", schedules},
            {"markers", markerNames},
            {"problems", problems},
            {"world", characters},
            {"prefabs", prefabPaths},
            {"usedBy", usedBy}};
}

// The Records tab (12.1): every record type with its fields (for the forms),
// every record (as written, and resolved with its base), what refers to each,
// and the problems to fix.
nlohmann::json EditorApp::DifficultyJson() {
    const DifficultyModel& model = session_.Difficulty();
    nlohmann::json levels = nlohmann::json::array();
    for (const auto& level : model.Levels()) {
        nlohmann::json modifiers = nlohmann::json::array();
        for (const auto& m : level.modifiers) {
            modifiers.push_back({{"target", m.target}, {"owner", m.owner}, {"tag", m.tag}, {"op", m.op}, {"value", m.value}});
        }
        levels.push_back({{"id", level.id}, {"name", level.name}, {"description", level.description}, {"order", level.order},
                          {"categories", level.steps}, {"modifiers", modifiers}, {"xp", level.xp},
                          {"permadeath", level.permadeath}});
    }
    // Every scalable field, for the modifier editor's targets.
    nlohmann::json fields = nlohmann::json::array();
    for (const auto& component : EngineReflection().Components()) {
        for (const auto& field : component.fields) {
            if (!field.scalable.empty()) fields.push_back({{"path", component.name + "." + field.name}, {"kind", field.scalable}});
        }
    }
    const auto& settings = ActiveSettings().difficulty;
    return {{"type", "difficulty:model"},
            {"levels", levels},
            {"builtIn", model.BuiltInLevels()},
            {"table", model.Table().ToJson()["rows"]},
            {"categories", DifficultyCategories()},
            {"fields", fields},
            {"defaultLevel", model.DefaultLevel()},
            {"playLevel", playDifficulty_},
            {"settings", {{"changeAnytime", settings.changeAnytime}, {"adaptive", settings.adaptive},
                          {"scaling", static_cast<int>(settings.scaling)}, {"levelOffset", settings.levelOffset}}}};
}

// The scenes in the order the gateways lead from the start scene, each with its
// Zone band and its enemies' levels, checked as a curve (difficulty.md §6).
nlohmann::json EditorApp::DifficultyCurveJson() {
    const Project& project = assets_.GetProject();
    const std::string open = OpenSceneRelative();
    std::vector<std::string> order;
    std::vector<std::string> queue = {StartScene().empty() ? open : StartScene()};
    std::map<std::string, CurveChapter> chapters;
    while (!queue.empty()) {
        const std::string scene = queue.front();
        queue.erase(queue.begin());
        if (scene.empty() || chapters.contains(scene)) continue;
        Registry loaded;
        Registry* r = &registry_;
        if (scene != open) {
            if (!LoadScene(loaded, project.Resolve(scene).string())) continue;
            r = &loaded;
        }
        CurveChapter chapter;
        chapter.scene = scene;
        for (auto [entity, zone] : r->view<Zone>().each()) {
            chapter.minLevel = zone.minLevel;
            chapter.maxLevel = std::max(zone.minLevel, zone.maxLevel);
            break;
        }
        for (auto [entity, spawner] : r->view<Spawner>().each()) chapter.enemyLevels.push_back(spawner.level);
        for (auto [entity, progression] : r->view<Progression>().each()) {
            if (TeamOf(*r, entity) == Team::Enemy) chapter.enemyLevels.push_back(progression.level);
        }
        for (auto [entity, gateway] : r->view<Gateway>().each()) {
            if (!gateway.scene.empty()) queue.push_back(gateway.scene);
        }
        chapters[scene] = chapter;
        order.push_back(scene);
    }
    std::vector<CurveChapter> path;
    for (const auto& scene : order) path.push_back(chapters[scene]);
    nlohmann::json list = nlohmann::json::array();
    for (const auto& c : path) {
        list.push_back({{"scene", c.scene}, {"minLevel", c.minLevel}, {"maxLevel", c.maxLevel}, {"enemyLevels", c.enemyLevels}});
    }
    nlohmann::json problems = nlohmann::json::array();
    for (const auto& p : CheckDifficultyCurve(path)) problems.push_back({{"scene", p.scene}, {"message", p.message}});
    return {{"type", "difficulty:curve"}, {"chapters", list}, {"problems", problems}};
}

nlohmann::json EditorApp::RecordsJson() {
    RecordDatabase& records = ActiveRecords();
    nlohmann::json types = nlohmann::json::array();
    for (const auto& type : EngineReflection().RecordTypes()) {
        nlohmann::json fields = nlohmann::json::array();
        for (const auto& field : type.fields) fields.push_back(RecordFieldToJson(field));
        types.push_back({{"name", type.name},
                         {"description", type.description},
                         {"source", type.source},
                         {"fields", fields},
                         {"defaults", DefaultRecord(type)}});
    }
    nlohmann::json list = nlohmann::json::array();
    for (const auto& type : EngineReflection().RecordTypes()) {
        for (const Record* record : records.OfType(type.name)) {
            list.push_back({{"type", record->type},
                            {"id", record->id},
                            {"path", record->path},
                            {"data", record->data},
                            {"resolved", record->resolved},
                            {"usedBy", records.ReferencesTo(record->type, record->id)}});
        }
    }
    nlohmann::json problems = nlohmann::json::array();
    for (const auto& problem : records.Problems()) {
        problems.push_back({{"key", problem.key}, {"message", problem.message}});
    }
    return {{"type", "records:list"}, {"types", types}, {"records", list}, {"problems", problems}};
}

// The World Bible for the World panel: every entry, plus problems to fix.
nlohmann::json EditorApp::WorldJson() {
    nlohmann::json entries = nlohmann::json::array();
    for (const auto& entry : world_.Entries()) {
        nlohmann::json relations = nlohmann::json::array();
        for (const auto& relation : entry.relations) {
            relations.push_back({{"type", relation.type}, {"target", relation.target}});
        }
        entries.push_back({{"key", entry.Key()},
                           {"kind", entry.kind},
                           {"id", entry.id},
                           {"name", entry.name},
                           {"summary", entry.summary},
                           {"tags", entry.tags},
                           {"relations", relations},
                           {"scene", entry.scene},
                           {"body", entry.body}});
    }
    nlohmann::json problems = nlohmann::json::array();
    for (const auto& problem : world_.Validate()) {
        problems.push_back({{"key", problem.key}, {"message", problem.message}});
    }
    return {{"type", "world:list"}, {"kinds", WorldKinds()}, {"entries", entries}, {"problems", problems}};
}

nlohmann::json EditorApp::PrefabsJson() {
    nlohmann::json list = nlohmann::json::array();
    for (const auto& prefab : prefabs_.Prefabs()) {
        list.push_back({{"path", prefab.path},
                        {"name", prefab.name},
                        {"kind", prefab.kind},
                        {"flat", prefab.flat},
                        {"terrain", [&]() -> nlohmann::json {
                             const AssetInfo* asset = prefab.isTile ? assets_.Find(prefab.texture) : nullptr;
                             if (asset == nullptr || !asset->analysis.contains("center") || !asset->analysis.contains("edges")) {
                                 return nullptr;
                             }
                             return asset->analysis.at("center");
                         }()},
                        {"error", prefab.error},
                        {"isTile", prefab.isTile},
                        {"isPlayer", prefab.isPlayer},
                        {"texture", prefab.texture},
                        {"uv", {prefab.u0, prefab.v0, prefab.u1, prefab.v1}},
                        {"size", {prefab.width, prefab.height}}});
    }
    return {{"type", "prefabs:list"}, {"prefabs", list}};
}

void EditorApp::CreatePrefabFromAsset(const std::string& asset, int tile, bool asTile, bool solid,
                                      const std::string& requestedName, bool flat) {
    auto style = TilesetStyle(asset, tile, solid);
    if (!style) {
        post_({{"type", "notify"}, {"text", "Pick a texture (and tile) first"}});
        return;
    }
    if (flat) {
        // Top-down art (5.2): mapped onto one cell's diamond, whatever the
        // image's own size.
        style->width = iso_.tileWidth;
        style->height = iso_.tileHeight;
        style->flat = true;
    }
    std::string name;
    for (char c : requestedName) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') {
            name += c;
        }
    }
    if (name.empty()) {
        post_({{"type", "notify"}, {"text", "Prefab names can use letters, digits, - and _"}});
        return;
    }
    std::string path = "prefabs/" + name + ".prefab";
    if (prefabs_.Find(path) != nullptr) {
        post_({{"type", "notify"}, {"text", path + " already exists"}});
        return;
    }

    Registry scratch;
    Entity entity = entt::null;
    if (asTile) {
        entity = CreateTile(scratch, 0, 0, *style);
    } else {
        entity = CreateProp(scratch, 0.0f, 0.0f, style->texture, style->width, style->height);
        auto& sprite = scratch.get<Sprite>(entity);
        sprite.u0 = style->u0;
        sprite.v0 = style->v0;
        sprite.u1 = style->u1;
        sprite.v1 = style->v1;
        sprite.flat = style->flat;
        scratch.emplace<Name>(entity, name);
        if (solid) {
            scratch.emplace<Obstacle>(entity);
        }
    }
    std::string error;
    if (!prefabs_.SaveAsPrefab(scratch, entity, path, &error)) {
        post_({{"type", "notify"}, {"text", "Couldn't create prefab: " + error}});
        return;
    }
    brushPrefab_ = path;
    SetTool(Tool::Prefab);
    post_(PrefabsJson());
    post_({{"type", "notify"}, {"text", "Created " + path + " — click in the viewport to place it"}});
}

void EditorApp::SaveSelectedAsPrefab(const std::string& requestedName) {
    if (playing_ || !selected_ || !registry_.valid(*selected_)) {
        return;
    }
    std::string name;
    for (char c : requestedName) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') {
            name += c;
        }
    }
    if (name.empty()) {
        post_({{"type", "notify"}, {"text", "Prefab names can use letters, digits, - and _"}});
        return;
    }
    std::string path = "prefabs/" + name + ".prefab";
    if (prefabs_.Find(path) != nullptr) {
        post_({{"type", "notify"}, {"text", path + " already exists — select one of its copies and use Apply"}});
        return;
    }
    Entity entity = *selected_;
    std::string error;
    bool saved = false;
    ApplyOneShotEdit([&] { // the entity becomes an instance: undoable on the scene side
        saved = prefabs_.SaveAsPrefab(registry_, entity, path, &error);
        return saved;
    });
    if (!saved) {
        post_({{"type", "notify"}, {"text", "Couldn't save prefab: " + error}});
        return;
    }
    post_(PrefabsJson());
    post_({{"type", "notify"}, {"text", "Saved " + path}});
    inspectorStale_ = true;
}

void EditorApp::ApplySelectedToPrefab() {
    if (playing_ || !selected_ || !registry_.valid(*selected_)) {
        return;
    }
    const auto* link = registry_.try_get<PrefabInstance>(*selected_);
    if (link == nullptr) {
        return;
    }
    std::string path = link->path;
    Entity entity = *selected_;
    std::string error;
    bool saved = false;
    ApplyOneShotEdit([&] {
        // This copy's values become the prefab; every other copy follows,
        // keeping its own overrides.
        saved = prefabs_.SaveAsPrefab(registry_, entity, path, &error);
        if (saved) {
            prefabs_.SyncAll(registry_, path);
        }
        return saved;
    });
    if (!saved) {
        post_({{"type", "notify"}, {"text", "Couldn't update prefab: " + error}});
        return;
    }
    post_(PrefabsJson());
    post_({{"type", "notify"}, {"text", "Updated " + path + " and its copies"}});
    inspectorStale_ = true;
    sceneTreeStale_ = true;
}

void EditorApp::RevertOverride(const std::string& key) {
    if (playing_ || !selected_ || !registry_.valid(*selected_) || !registry_.all_of<PrefabInstance>(*selected_)) {
        return;
    }
    Entity entity = *selected_;
    ApplyOneShotEdit([&] {
        if (key.empty()) {
            prefabs_.RevertAll(registry_, entity);
        } else {
            prefabs_.Revert(registry_, entity, key);
        }
        return true;
    });
    inspectorStale_ = true;
    sceneTreeStale_ = true;
}

// --- Project settings --------------------------------------------------------

nlohmann::json EditorApp::SettingsJson() {
    nlohmann::json sections = nlohmann::json::array();
    // The app's choice for each AI field, for its "Use app default (…)" option.
    const std::map<std::string, std::pair<const char*, const char*>> appKey = {
        {"provider", {"aiProvider", "mistral"}}, {"model", {"aiModel", ""}}, {"art", {"artProvider", "auto"}}, {"background", {"backgroundProvider", "bfl:flux-3-image"}}};
    for (const auto& section : EngineReflection().AllSettings()) {
        nlohmann::json fields = nlohmann::json::array();
        for (const auto& field : section.fields) {
            fields.push_back(FieldJson(field, field.get()));
            if (section.name == "AI" && appKey.contains(field.name)) {
                const auto& [key, fallback] = appKey.at(field.name);
                const nlohmann::json value = userPrefs_ ? userPrefs_->Get(key, fallback) : nlohmann::json(fallback);
                fields.back()["appDefault"] = value.is_string() && !value.get<std::string>().empty() ? value : nlohmann::json(field.name == "model" ? "the provider's default" : fallback);
            }
        }
        sections.push_back({{"name", section.name}, {"description", section.description}, {"fields", fields}});
    }
    return {{"type", "settings:data"}, {"sections", sections}};
}

std::optional<std::string> EditorApp::SetSetting(const std::string& sectionName, const std::string& fieldName,
                                                 const nlohmann::json& value) {
    if (auto error = EditSetting(EngineReflection(), sectionName, fieldName, value)) {
        return error;
    }
    // Settings live in settings.aige, outside the scene's undo history;
    // they're saved with the scene.
    settingsDirty_ = true;
    editorStateStale_ = true;
    return std::nullopt;
}

// --- Script properties --------------------------------------------------------

nlohmann::json EditorApp::ScriptPropertiesJson(Entity entity) {
    const auto& script = registry_.get<LuaScript>(entity);
    nlohmann::json result = {{"properties", nlohmann::json::array()}, {"error", ""}};
    if (script.path.empty()) {
        return result;
    }
    std::string error;
    auto declared = ReadScriptProperties(lua_, assets_.GetProject().Resolve(script.path), &error);
    result["error"] = error;
    for (const auto& property : declared) {
        auto it = script.properties.find(property.name);
        const ScriptValue& value = it != script.properties.end() ? it->second : property.value;
        nlohmann::json entry = {{"name", property.name},
                                {"set", it != script.properties.end()},
                                {"overridden", HasOverride(registry_, entity, "LuaScript.prop." + property.name)}};
        std::visit(
            [&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                entry["type"] = std::is_same_v<T, double> ? "float" : std::is_same_v<T, bool> ? "bool" : "string";
                entry["value"] = v;
            },
            value);
        // The editor offers the default's type even if the stored value
        // drifted (e.g. the script changed its default from 3 to "fast").
        std::visit([&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            entry["type"] = std::is_same_v<T, double> ? "float" : std::is_same_v<T, bool> ? "bool" : "string";
        }, property.value);
        result["properties"].push_back(entry);
    }
    return result;
}

void EditorApp::SetScriptProperty(Entity entity, const std::string& name, const nlohmann::json& value) {
    ApplyOneShotEdit([&] {
        if (EditScriptProperty(registry_, entity, name, value)) {
            return false;
        }
        auto& script = registry_.get<LuaScript>(entity);
        if (script.loaded) {
            ApplyScriptProperties(lua_, script); // live while playing
        }
        return true;
    });
    inspectorStale_ = true;
}

void EditorApp::ResetScriptProperty(Entity entity, const std::string& name) {
    ApplyOneShotEdit([&] {
        registry_.get<LuaScript>(entity).properties.erase(name);
        if (HasOverride(registry_, entity, "LuaScript.prop." + name)) {
            prefabs_.Revert(registry_, entity, "LuaScript.prop." + name); // back to the prefab's value
        }
        auto& script = registry_.get<LuaScript>(entity);
        if (script.loaded) {
            script.Unload(); // reload so the script's own default applies again
        }
        return true;
    });
    inspectorStale_ = true;
}

void EditorApp::RefreshAssets() {
    for (const auto& path : assets_.Refresh()) {
        renderer_.InvalidateTexture(path);
        ++assetVersions_[path];
    }
    post_(AssetsJson());
    // What the Art tool made, kept outside the project too (development_plan.md 10).
    if (UserLibrary(UserLibraryDir()).KeepGenerated(assets_.GetProject().root) > 0) PostLibrary();
}

void EditorApp::ImportAssets() {
    std::vector<std::filesystem::path> files = ShowImportDialog(mainWindow_);
    if (files.empty()) {
        return;
    }
    int imported = 0;
    std::string lastError;
    std::string lastPath;
    for (const auto& file : files) {
        std::string error;
        if (auto path = assets_.Import(file, &error)) {
            ++imported;
            lastPath = *path;
        } else {
            lastError = WideToUtf8(file.filename().wstring()) + ": " + error;
        }
    }
    post_(AssetsJson());
    if (!lastError.empty()) {
        post_({{"type", "notify"}, {"text", "Import failed — " + lastError}});
    } else {
        post_({{"type", "notify"},
               {"text", imported == 1 ? "Imported " + lastPath : "Imported " + std::to_string(imported) + " assets"}});
    }
    if (!lastPath.empty()) {
        post_({{"type", "assets:select"}, {"path", lastPath}});
    }
}

std::optional<TileStyle> EditorApp::TilesetStyle(const std::string& path, int tile, bool solid) const {
    const AssetInfo* asset = assets_.Find(path);
    if (asset == nullptr || asset->type == AssetType::Audio || !asset->error.empty() || asset->width == 0 ||
        asset->height == 0) {
        return std::nullopt;
    }
    TileStyle style;
    style.texture = path;
    style.solid = solid;
    if (asset->type == AssetType::Texture) {
        // A plain texture used as a tile covers the whole image.
        style.width = static_cast<float>(asset->width);
        style.height = static_cast<float>(asset->height);
        return style;
    }
    uint32_t columns = asset->TileColumns();
    if (columns == 0 || tile < 0 || static_cast<uint32_t>(tile) >= asset->TileCount()) {
        return std::nullopt;
    }
    uint32_t col = static_cast<uint32_t>(tile) % columns;
    uint32_t row = static_cast<uint32_t>(tile) / columns;
    float w = static_cast<float>(asset->width);
    float h = static_cast<float>(asset->height);
    style.u0 = static_cast<float>(col * asset->tileWidth) / w;
    style.v0 = static_cast<float>(row * asset->tileHeight) / h;
    style.u1 = static_cast<float>((col + 1) * asset->tileWidth) / w;
    style.v1 = static_cast<float>((row + 1) * asset->tileHeight) / h;
    style.width = static_cast<float>(asset->tileWidth);
    style.height = static_cast<float>(asset->tileHeight);
    return style;
}

void EditorApp::ApplyAssetToSelected(const std::string& path, int tile) {
    if (playing_) {
        return;
    }
    if (!selected_ || !registry_.valid(*selected_) || !registry_.all_of<Sprite>(*selected_)) {
        post_({{"type", "notify"}, {"text", "Select an entity with a Sprite to apply a texture to"}});
        return;
    }
    auto style = TilesetStyle(path, tile, false);
    if (!style) {
        post_({{"type", "notify"}, {"text", "Can't apply " + path}});
        return;
    }
    Entity entity = *selected_;
    ApplyOneShotEdit([&] {
        auto& sprite = registry_.get<Sprite>(entity);
        sprite.texture = style->texture;
        sprite.u0 = style->u0;
        sprite.v0 = style->v0;
        sprite.u1 = style->u1;
        sprite.v1 = style->v1;
        sprite.width = style->width;
        sprite.height = style->height;
        return true;
    });
    inspectorStale_ = true;
    sceneTreeStale_ = true; // an NPC with a texture now lists as a Prop
}

// Generated art (assets/textures/generated, development_plan.md 3.6), newest
// first, with where each came from.
nlohmann::json EditorApp::ArtJson() {
    std::vector<std::pair<std::string, nlohmann::json>> found;
    std::error_code ec;
    const std::filesystem::path folder = assets_.GetProject().Resolve("assets/textures/generated");
    for (const auto& file : std::filesystem::directory_iterator(folder, ec)) {
        if (file.path().extension() != ".png") continue;
        std::ifstream in(std::filesystem::path(file.path()).concat(".gen.json"));
        nlohmann::json meta = in ? nlohmann::json::parse(in, nullptr, false) : nlohmann::json::object();
        if (!meta.is_object()) meta = nlohmann::json::object();
        found.push_back({"assets/textures/generated/" + file.path().filename().string(), meta});
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        return a.second.value("created", "") > b.second.value("created", "");
    });
    nlohmann::json list = nlohmann::json::array();
    for (const auto& [path, meta] : found) {
        list.push_back({{"path", path}, {"version", assetVersions_[path]}, {"meta", meta}});
    }
    return {{"type", "art:list"}, {"items", list}, {"busy", artRequest_ != 0}, {"history", artHistory_.Entries()}};
}

const Ontology& EditorApp::Places() {
    if (!places_) {
        // The engine's library, then the user's own (every project: development_plan.md 9), then the project's.
        places_.emplace();
        places_->Load({EnginePlacesDir(), UserOntologyDir(), assets_.GetProject().root / "places"});
    }
    return *places_;
}

std::filesystem::path EditorApp::UserLibraryDir() {
    const std::filesystem::path prefs = UserPrefs::DefaultPath();
    return prefs.empty() ? std::filesystem::path() : prefs.parent_path() / "library";
}

void EditorApp::PostLibrary() {
    nlohmann::json state = UserLibrary(UserLibraryDir()).State(assets_.GetProject().root);
    state["type"] = "library:state";
    post_(state);
}

bool EditorApp::HandleLibraryMessage(const std::string& type, const nlohmann::json& message) {
    // The Library tab (development_plan.md 10, docs/systems/asset_management.md).
    UserLibrary library(UserLibraryDir());
    const std::string item = message.value("item", ""), folder = message.value("folder", "");
    std::string error;
    if (type == "library:get") {
        library.KeepGenerated(assets_.GetProject().root);
    } else if (type == "library:newFolder") {
        error = library.NewFolder(message.value("name", ""), message.value("parent", ""));
    } else if (type == "library:renameFolder") {
        error = library.RenameFolder(folder, message.value("name", ""));
    } else if (type == "library:deleteFolder") {
        error = library.DeleteFolder(folder);
    } else if (type == "library:move") {
        error = library.Move(item, folder);
    } else if (type == "library:rename") {
        error = library.Rename(item, message.value("name", ""));
    } else if (type == "library:remove") {
        error = library.Remove(item);
    } else if (type == "library:setShared") {
        error = library.SetShared(item, message.value("shared", ""));
    } else if (type == "library:setSharing") {
        error = library.SetSharing(message.value("choice", ""));
    } else if (type == "library:toProject") {
        const std::string path = library.CopyToProject(item, assets_.GetProject().root);
        if (path.empty()) {
            error = "couldn't copy it into the project";
        } else {
            RefreshAssets();
            post_({{"type", "notify"}, {"text", "Added " + path + " to the project"}});
        }
    } else if (type == "library:addFromProject") {
        // Assets in the project, into the library (from the Assets tab's menu).
        int added = 0;
        const nlohmann::json paths = message.value("paths", nlohmann::json::array());
        for (const auto& p : paths) {
            if (!p.is_string() || p.get<std::string>().find("..") != std::string::npos) continue;
            const nlohmann::json info = {{"origin", assets_.GetProject().root.filename().string()}};
            added += !library.Add(assets_.GetProject().Resolve(p.get<std::string>()), info, folder).empty();
        }
        post_({{"type", "notify"}, {"text", "Kept " + std::to_string(added) + " asset(s) in your library"}});
    } else if (type == "library:export") {
        const auto to = ShowFolderDialog(mainWindow_, L"Where to export the library");
        if (!to) return true;
        error = library.Export(folder, *to);
        if (error.empty()) post_({{"type", "notify"}, {"text", "Exported to " + WideToUtf8(to->wstring())}});
    } else if (type == "library:import") {
        const auto from = ShowFolderDialog(mainWindow_, L"Pick an exported library (a .aige-library folder)");
        if (!from) return true;
        error = library.Import(*from);
    } else {
        return false;
    }
    if (!error.empty()) post_({{"type", "notify"}, {"text", "Library: " + error}});
    PostLibrary();
    return true;
}

std::filesystem::path EditorApp::UserOntologyDir() {
    const std::filesystem::path prefs = UserPrefs::DefaultPath();
    return prefs.empty() ? std::filesystem::path() : prefs.parent_path() / "ontology";
}

void EditorApp::PostOntology(const std::string& error) {
    nlohmann::json state = OntologyJson(Places());
    state["type"] = "ontology:state";
    state["userDir"] = WideToUtf8(UserOntologyDir().wstring());
    state["extensions"] = UserExtensions(UserOntologyDir());
    state["userModules"] = ExportUserOntology(UserOntologyDir()).value("modules", nlohmann::json::array()); // as written, for editing
    if (!error.empty()) state["error"] = error;
    post_(state);
}

bool EditorApp::HandleOntologyMessage(const std::string& type, const nlohmann::json& message) {
    // The ontology panel (development_plan.md 9): the user's modules and extensions, kept in
    // their folder; after each change the ontology is loaded again (the Build panel and the AI
    // work from it).
    const std::filesystem::path dir = UserOntologyDir();
    auto changed = [&](const std::string& error) {
        if (error.empty()) {
            places_.reset();
            nlohmann::json options = BuildChoices(Places());
            options["type"] = "build:choices";
            post_(options);
        }
        PostOntology(error);
    };
    if (type == "ontology:get") {
        PostOntology();
    } else if (type == "ontology:saveModule") {
        changed(SaveUserModule(dir, EnginePlacesDir(), message.value("module", nlohmann::json::object())));
    } else if (type == "ontology:deleteModule") {
        changed(DeleteUserModule(dir, message.value("id", "")));
    } else if (type == "ontology:saveExtensions") {
        changed(SaveUserExtensions(dir, EnginePlacesDir(), message.value("extensions", nlohmann::json::object())));
    } else if (type == "ontology:export") {
        const auto path = ShowSaveFileDialog(mainWindow_, L"Export your ontology", L"AIGE ontology", L"*.json", L"my-ontology.aige-ontology.json");
        if (!path) return true;
        std::ofstream(*path, std::ios::binary) << ExportUserOntology(dir).dump(2) << "\n";
        post_({{"type", "notify"}, {"text", "Exported to " + WideToUtf8(path->wstring())}});
    } else if (type == "ontology:import") {
        const auto path = ShowOpenFileDialog(mainWindow_, L"Import an ontology", L"AIGE ontology", L"*.json");
        if (!path) return true;
        std::ifstream in(*path);
        const nlohmann::json bundle = in ? nlohmann::json::parse(in, nullptr, false) : nlohmann::json();
        const std::string error = ImportUserOntology(dir, EnginePlacesDir(), bundle);
        places_.reset();
        changed(error);
        if (error.empty()) post_({{"type", "notify"}, {"text", "Imported " + WideToUtf8(path->filename().wstring())}});
    } else {
        return false;
    }
    return true;
}

CatalogOptions EditorApp::CatalogOptionsNow() {
    CatalogOptions options;
    options.isoGrid = ActiveSettings().grid.style != GridStyle::TopDown;
    options.pixelsPerCell = [this](const std::string& folder) {
        const PackScale scale = PackScaleFor(assets_.GetProject(), assets_, folder, iso_);
        return scale.source == "default" ? 0.0f : scale.pixelsPerCell;
    };
    return options;
}

nlohmann::json EditorApp::CatalogJson() {
    const CatalogOptions options = CatalogOptionsNow();
    const Catalog catalog = BuildCatalog(assets_, &Places(), options);
    auto rect = [](const ImageRect& r) { return nlohmann::json::array({r.x, r.y, r.w, r.h}); };
    nlohmann::json entries = nlohmann::json::array();
    for (const CatalogEntry& e : catalog.entries) {
        nlohmann::json anchors = nlohmann::json::object();
        for (const auto& [name, uv] : e.anchors) anchors[name] = {uv[0], uv[1]};
        entries.push_back({{"id", e.id},
                           {"path", e.path},
                           {"rect", rect(e.rect)},
                           {"base", rect(e.shape.base)},
                           {"opaque", rect(e.shape.opaque)},
                           {"analysed", !e.shape.region.Empty()},
                           {"name", e.name},
                           {"kind", e.kind},
                           {"roles", e.roles},
                           {"concepts", e.concepts},
                           {"affordance", e.affordance},
                           {"perspective", e.perspective},
                           {"flat", e.flat},
                           {"tileType", e.tileType},
                           {"layer", e.layer},
                           {"footprint", {{"w", e.footprint.w}, {"h", e.footprint.h}, {"cellsW", e.footprint.cellsW}, {"cellsH", e.footprint.cellsH}}},
                           {"anchor", {e.anchorU, e.anchorV}},
                           {"sprite", {e.spriteW, e.spriteH}},
                           {"pixelsPerCell", e.pixelsPerCell},
                           {"facings", e.facings},
                           {"variantOf", e.variantOf},
                           {"anchors", anchors},
                           {"passable", e.passable},
                           {"accessible", e.accessible},
                           {"pieceSet", e.pieceSet},
                           {"piece", e.piece},
                           {"pieceGrid", e.pieceGrid},
                           {"palette", e.shape.palette},
                           {"shadow", e.shape.shadow},
                           {"source", e.source},
                           {"problems", e.problems}});
    }
    nlohmann::json sets = nlohmann::json::array();
    for (const PieceSet& s : catalog.pieceSets) {
        sets.push_back({{"id", s.id}, {"role", s.role}, {"grid", s.grid}, {"pieces", s.pieces}, {"missing", s.missing}, {"usable", s.usable}});
    }
    std::vector<std::string> roles;
    for (const auto& [id, role] : Places().Roles()) roles.push_back(id);
    return {{"type", "catalog:state"},
            {"grid", options.isoGrid ? "iso" : "square"},
            {"entries", entries},
            {"pieceSets", sets},
            {"roles", roles}};
}

nlohmann::json EditorApp::AssetsJson() {
    nlohmann::json list = nlohmann::json::array();
    for (const auto& asset : assets_.Assets()) {
        nlohmann::json entry = {
            {"path", asset.path},
            {"name", WideToUtf8(std::filesystem::path(Utf8ToWide(asset.path)).filename().wstring())},
            {"type", AssetTypeName(asset.type)},
            {"error", asset.error},
            {"version", assetVersions_[asset.path]},
        };
        if (asset.type == AssetType::Audio) {
            entry["duration"] = asset.durationSeconds;
        } else {
            entry["width"] = asset.width;
            entry["height"] = asset.height;
            if (asset.type == AssetType::Tileset) {
                entry["tileWidth"] = asset.tileWidth;
                entry["tileHeight"] = asset.tileHeight;
                entry["columns"] = asset.TileColumns();
                entry["rows"] = asset.TileRows();
            }
        }
        // What the art is (6.2).
        if (!asset.analysis.empty()) {
            entry["analysis"] = asset.analysis;
        }
        // Generated art (3.6) says where it came from.
        std::ifstream sidecar(assets_.GetProject().Resolve(asset.path + ".gen.json"));
        if (sidecar) {
            nlohmann::json meta = nlohmann::json::parse(sidecar, nullptr, false);
            if (meta.is_object()) {
                entry["generation"] = {{"description", meta.value("description", "")},
                                       {"variant", meta.value("variant", nlohmann::json())},
                                       {"model", meta.value("model", "")},
                                       {"created", meta.value("created", "")}};
            }
        }
        list.push_back(entry);
    }
    return {{"type", "assets:list"}, {"assets", list}};
}

// --- JSON -------------------------------------------------------------------

nlohmann::json EditorApp::SceneJson() {
    nlohmann::json entities = nlohmann::json::array();
    for (auto entity : EntitiesInOrder(registry_)) {
        std::string kind = DescribeKind(registry_, entity);
        const auto* instance = registry_.try_get<PrefabInstance>(entity);
        entities.push_back({{"id", static_cast<uint32_t>(entity)},
                            {"kind", kind},
                            {"label", LabelOf(entity)},
                            {"prefab", instance ? nlohmann::json(instance->path) : nlohmann::json(nullptr)}});
    }
    return {{"type", "scene:update"}, {"entities", entities}};
}

nlohmann::json EditorApp::EditorStateJson() {
    return {{"type", "editor:state"},
            {"tool", kToolIds[static_cast<int>(tool_)]},
            {"heightMode", heightMode_},
            {"heightRadius", heightRadius_},
            {"brush", brushPrefab_},
            {"playing", playing_},
            {"dirty", dirty_ || settingsDirty_},
            {"canUndo", history_.CanUndo() && !playing_},
            {"canRedo", history_.CanRedo() && !playing_},
            {"sceneName", std::filesystem::path(scenePath_).filename().string()},
            {"grid", showGrid_},
            {"brushScale", brushScale_},
            {"brushRotation", brushRotation_},
            {"autotile", autotile_},
            {"hasSave", std::filesystem::exists(SaveGamePath() / "quick.json") || std::filesystem::exists(SaveGamePath() / "quicksave.json")}};
}

nlohmann::json EditorApp::InspectorJson(Entity entity) {
    nlohmann::json components = nlohmann::json::array();
    nlohmann::json addable = nlohmann::json::array();

    for (const auto& component : Components()) {
        if (!component.has(registry_, entity)) {
            if (component.userEditable) {
                addable.push_back({{"name", component.name}, {"description", component.description}});
            }
            continue;
        }
        if (!component.inspectorVisible) {
            continue;
        }
        nlohmann::json fields = nlohmann::json::array();
        for (const auto& field : component.fields) {
            nlohmann::json entry = FieldJson(field, field.get(registry_, entity));
            if (field.type == FieldType::EntityRef && entry["value"].is_number() &&
                !registry_.valid(static_cast<Entity>(entry["value"].get<uint32_t>()))) {
                entry["value"] = nullptr; // a reference to something deleted
            }
            entry["overridden"] = HasOverride(registry_, entity, component.name + "." + field.name);
            fields.push_back(entry);
        }
        nlohmann::json section = {{"name", component.name},
                                  {"description", component.description},
                                  {"removable", component.userEditable},
                                  {"derived", component.derived(registry_, entity)},
                                  {"fields", fields}};
        if (component.name == "LuaScript") {
            section["script"] = ScriptPropertiesJson(entity);
        }
        components.push_back(section);
    }

    // Choices for reference-typed fields.
    nlohmann::json entities = nlohmann::json::array();
    for (auto other : EntitiesInOrder(registry_)) {
        if (other != entity && !registry_.all_of<Tile>(other)) {
            entities.push_back({{"id", static_cast<uint32_t>(other)}, {"label", LabelOf(other)}});
        }
    }
    nlohmann::json textures = nlohmann::json::array();
    nlohmann::json audio = nlohmann::json::array();
    for (const auto& asset : assets_.Assets()) {
        if (asset.error.empty()) {
            (asset.type == AssetType::Audio ? audio : textures).push_back(asset.path);
        }
    }

    nlohmann::json prefab = nullptr;
    if (const auto* link = registry_.try_get<PrefabInstance>(entity)) {
        auto overrides = ParseOverrides(link->overrides);
        const PrefabInfo* info = prefabs_.Find(link->path);
        prefab = {{"path", link->path},
                  {"name", info ? info->name : link->path},
                  {"missing", info == nullptr},
                  {"overrides", std::vector<std::string>(overrides.begin(), overrides.end())}};
    }

    const auto size = SizePercent(entity);
    return {{"type", "inspector:data"},
            {"id", static_cast<uint32_t>(entity)},
            {"label", LabelOf(entity)},
            {"prefab", prefab},
            {"size", size ? nlohmann::json(std::round(*size * 10.0f) / 10.0f) : nlohmann::json(nullptr)},
            {"components", components},
            {"addable", addable},
            {"choices", {{"entities", entities}, {"textures", textures}, {"audio", audio}, {"scripts", ListScripts()}}}};
}

std::string EditorApp::LabelOf(Entity entity) {
    if (const auto* name = registry_.try_get<Name>(entity); name != nullptr && !name->value.empty()) {
        return name->value;
    }
    if (const auto* tile = registry_.try_get<Tile>(entity)) {
        return "Tile " + std::to_string(tile->gx) + "," + std::to_string(tile->gy) + (tile->solid ? " (wall)" : "");
    }
    return DescribeKind(registry_, entity);
}

std::vector<std::string> EditorApp::ListScripts() {
    std::vector<std::string> scripts;
    const Project& project = assets_.GetProject();
    std::error_code ec;
    std::filesystem::path dir = project.root / "scripts";
    if (std::filesystem::exists(dir, ec)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".lua") {
                scripts.push_back(project.Relative(entry.path()));
            }
        }
    }
    std::sort(scripts.begin(), scripts.end());
    return scripts;
}

void EditorApp::CreateScriptFor(Entity entity, const std::string& requestedName) {
    // Keep script names to a safe, portable character set.
    std::string name;
    for (char c : requestedName) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') {
            name += c;
        }
    }
    if (name.empty()) {
        post_({{"type", "notify"}, {"text", "Script names can use letters, digits, - and _"}});
        return;
    }
    const Project& project = assets_.GetProject();
    std::string relative = "scripts/" + name + ".lua";
    std::filesystem::path path = project.Resolve(relative);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream out(path);
        out << "-- " << name << ".lua\n"
            << "-- Runs once when loaded (and again on every save while the game runs).\n"
            << "-- get_position() / set_position(x, y) move this entity in world grid units.\n\n"
            << "function update(dt)\n"
            << "    -- called 60 times per second while playing\n"
            << "end\n";
    }
    ApplyOneShotEdit([&] {
        if (!registry_.all_of<LuaScript>(entity)) {
            AddComponent(registry_, entity, "LuaScript");
        }
        registry_.get<LuaScript>(entity) = LuaScript{relative};
        return true;
    });
    inspectorStale_ = true;
    sceneTreeStale_ = true;
    post_({{"type", "notify"}, {"text", "Created " + relative}});
}

} // namespace aige
