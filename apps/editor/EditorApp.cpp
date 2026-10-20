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
#include "assets/ProjectTemplate.h"
#include "editing/Edits.h"
#include "editing/LevelBuild.h"
#include "editing/SceneTools.h"
#include "ai_bridge/RecentProjects.h"
#include "assets/Export.h"
#include "assets/PackImport.h"
#include "assets/TileEdges.h"
#include "assets/SceneFiles.h"
#include "assets/ProjectMove.h"
#include "rhi/ShaderUtil.h"
#include "modules/Modules.h"
#include "world/StoryReferences.h"
#include "editing/SceneEditing.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/CameraFollow.h"
#include "gameplay/Components.h"
#include "gameplay/Settings.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "scripting/Components.h"

#include <algorithm>
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
#include <sstream>
#include <limits>

#include <shellapi.h>

namespace aige {

namespace {

constexpr float kMinZoom = 0.25f;
constexpr float kMaxZoom = 4.0f;
constexpr double kPlayInspectorRefreshSeconds = 0.1;

// Ids shared with the UI's toolbar (apps/editor/ui/src/components/Toolbar.vue).
constexpr const char* kToolIds[] = {"select", "erase", "prefab"};

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
    return entry;
}

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
}

// --- IPC --------------------------------------------------------------------

void EditorApp::OnWebMessage(const nlohmann::json& message) {
    const std::string type = message.value("type", "");

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
        ExportGameTo(message.value("name", ""), message.value("zip", false));
    } else if (type == "game:reveal") {
        const std::wstring folder = Utf8ToWide(message.value("path", ""));
        ShellExecuteW(mainWindow_, nullptr, L"explorer.exe", (L"\"" + folder + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
    } else if (type == "folder:rename") {
        RenameFolder(message.value("folder", ""), message.value("to", ""));
    } else if (type == "folder:remove") {
        RemoveFolder(message.value("folder", ""), message.value("deleteContents", false));
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
    } else if (type == "project:new") {
        const std::string name = message.value("name", "");
        auto parent = ShowFolderDialog(mainWindow_, L"Where to create \"" + Utf8ToWide(name) + L"\"");
        if (!parent || name.empty()) {
            return;
        }
        std::filesystem::path root = *parent / Utf8ToWide(name);
        std::optional<std::filesystem::path> starter;
        if (message.value("starter", false)) {
            starter = std::filesystem::path(AIGE_EDITOR_DEFAULT_PROJECT);
        }
        if (auto error = CreateProject(root, starter, message.value("topDown", false))) {
            post_({{"type", "notify"}, {"text", *error}});
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
    } else if (type == "build:start") {
        StartBuild(message.value("query", ""));
    } else if (type == "build:request") {
        PostBuild();
    } else if (type == "build:list") {
        post_({{"type", "build:list"}, {"builds", ListBuilds(assets_.GetProject().root)}});
    } else if (type == "build:resume") {
        ResumeBuild(message.value("id", ""));
    } else if (type == "build:edit") {
        if (build_) build_->Edit(message.value("path", ""), message.value("value", nlohmann::json()));
        PostBuild();
    } else if (type == "build:step" || type == "build:confirm") {
        if (build_) {
            const std::string why = type == "build:confirm" ? build_->Confirm() : build_->Step(message.value("step", ""));
            if (!why.empty()) post_({{"type", "notify"}, {"text", "Not yet: " + why}});
        }
        PostBuild();
    } else if (type == "build:approve") {
        if (build_) {
            const std::string why = build_->Approve();
            if (!why.empty()) post_({{"type", "notify"}, {"text", "Not yet: " + why}});
        }
        PostBuild();
    } else if (type == "build:revise" || type == "build:review" || type == "build:undo") {
        if (build_) {
            const std::string why = type == "build:revise" ? build_->Revise(message.value("text", ""))
                                    : type == "build:review" ? build_->Review()
                                                             : build_->Undo();
            if (!why.empty()) post_({{"type", "notify"}, {"text", "Not now: " + why}});
        }
        PostBuild();
    } else if (type == "build:relayout") {
        if (build_) {
            const std::string why = build_->Relayout();
            if (!why.empty()) post_({{"type", "notify"}, {"text", "Not now: " + why}});
        }
        PostBuild();
    } else if (type == "build:editNode" || type == "build:regenerate" || type == "build:lock" || type == "build:brush") {
        if (build_) {
            std::string why;
            const std::string node = message.value("node", "");
            if (type == "build:editNode") why = build_->EditNode(node, message.value("field", ""), message.value("value", nlohmann::json()));
            else if (type == "build:regenerate") why = build_->Regenerate(node);
            else if (type == "build:lock") build_->Lock(node, message.value("locked", true));
            else placeBrush_ = message.value("module", "");
            if (!why.empty()) post_({{"type", "notify"}, {"text", "Not now: " + why}});
            UpdateBuiltParts();
            PostSelectedPart();
        }
        PostBuild();
    } else if (type == "build:build") {
        BuildIntoScene();
        PostBuild();
    } else if (type == "build:cancel") {
        if (build_) build_->Cancel();
        PostBuild();
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
    } else if (type == "edit:delete") {
        DeleteSelected();
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
            case Tool::Select:
                // Over something that can be picked up and moved (not the
                // ground itself): the move pointer.
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
    const std::string warning = TravelToScene(registry_, text, travel->marker);
    PlayLayersFor(travel->scene);
    prefabs_.SyncAll(registry_);
    session_.OnSceneChanged();
    session_.SetScene(travel->scene);
    UpdateWorldToScreen(registry_, iso_);
    if (auto player = FindPlayer(registry_)) {
        int width = 0, height = 0;
        viewport_.GetFramebufferSize(width, height);
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

void EditorApp::ExportGameTo(const std::string& name, bool zip) {
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
    ExportResult result = ExportGame(options);
    if (!result.error.empty()) {
        return fail(result.error);
    }
    post_({{"type", "game:exported"},
           {"ok", true},
           {"folder", WideToUtf8(result.folder.wstring())},
           {"zip", result.zip.empty() ? "" : WideToUtf8(result.zip.wstring())},
           {"files", result.files.size()},
           {"build", player->second}});
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
            if (!placeBrush_.empty() && build_) {
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
                post_({{"type", "viewport:contextMenu"},
                       {"x", viewportBounds_.x + static_cast<int>(event.x)},
                       {"y", viewportBounds_.y + static_cast<int>(event.y)},
                       {"entity", entity},
                       {"cell", {gx, gy}},
                       {"brush", tool_ == Tool::Prefab ? brushPrefab_ : std::string()}});
            }
            if (event.button == Button::Right || event.button == Button::Middle || event.button == Button::None) {
                panning_ = false;
            }
            if (brushing_ && (event.button == Button::Left || event.button == Button::None)) {
                brushing_ = false;
                const auto [ox, oy] = build_ ? build_->Origin() : std::pair{0, 0};
                const int x0 = std::min(brushFromX_, brushToX_) - ox, y0 = std::min(brushFromY_, brushToY_) - oy;
                const int x1 = std::max(brushFromX_, brushToX_) + 1 - ox, y1 = std::max(brushFromY_, brushToY_) + 1 - oy;
                if (build_) {
                    if (const std::string why = build_->Paint(placeBrush_, x0, y0, x1, y1); !why.empty()) {
                        post_({{"type", "notify"}, {"text", "Can't paint here: " + why}});
                    }
                    UpdateBuiltParts();
                    PostBuild();
                }
                return;
            }
            if (event.button == Button::Left || event.button == Button::None) {
                if (dragging_ || stroking_) {
                    EndEdit();
                }
                dragging_ = false;
                stroking_ = false;
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
        case Tool::Erase: {
            auto picked = PickEntity(registry_, sceneX, sceneY, iso_);
            if (!picked) {
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
    if (!editingPart_.empty() && build_) {
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

void EditorApp::Select(std::optional<Entity> entity) {
    if (entity && !registry_.valid(*entity)) {
        entity.reset();
    }
    selected_ = entity;
    post_({{"type", "selection:changed"},
           {"id", entity ? nlohmann::json(static_cast<uint32_t>(*entity)) : nlohmann::json(nullptr)}});
    if (entity) {
        post_(InspectorJson(*entity));
    } else {
        post_({{"type", "inspector:clear"}});
    }
    PostSelectedPart();
}

std::string EditorApp::PartOf(Entity entity) const {
    if (!build_ || !registry_.valid(entity)) return {};
    const auto* node = registry_.try_get<PlaceNode>(entity);
    if (node == nullptr || node->place != build_->Id()) return {};
    // One instance of a template: up to its "#k" ("ward/cottages#2/house" -> "ward/cottages#2").
    const std::string& s = node->statement;
    if (const size_t hash = s.find('#'); hash != std::string::npos) return s.substr(0, s.find('/', hash));
    return node->node;
}

void EditorApp::PostSelectedPart() {
    const std::string part = selected_ ? PartOf(*selected_) : std::string();
    nlohmann::json info = part.empty() || !build_ ? nlohmann::json() : build_->NodeInfo(part);
    post_({{"type", "build:part"}, {"part", info}, {"brush", placeBrush_}});
}

void EditorApp::UpdateBuiltParts() {
    if (!build_) return;
    const std::set<std::string> changed = build_->TakeChanged();
    if (changed.empty() || build_->Stage() != "built" || !build_->Result().is_object()) return;
    // The scene follows: only those parts are replaced; locked ones stay as edited.
    PlaceSceneOptions options;
    options.place = build_->Id();
    std::tie(options.originX, options.originY) = build_->Origin();
    options.iso = iso_;
    options.isoGrid = ActiveSettings().grid.style != GridStyle::TopDown;
    options.only = changed;
    options.locked = build_->Locks();
    const TerrainSet terrains = ProjectTerrainSet(prefabs_, assets_.GetProject());
    const Catalog catalog = BuildCatalog(assets_, &Places(), CatalogOptionsNow());
    const nlohmann::json assets = PlanPlaceAssets(Places(), build_->Spec(), build_->Solved(), catalog, prefabs_, assets_, terrains, options);
    nlohmann::json report;
    auto command = BuildPlaceScene(registry_, prefabs_, build_->Solved(), assets, terrains, options, &report);
    history_.PushCommand(std::move(command));
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
    post_({{"type", "notify"}, {"text", "Saved " + std::filesystem::path(scenePath_).filename().string()}});
}

std::filesystem::path EditorApp::SaveGamePath() const {
    return assets_.GetProject().CacheDir() / "saves" / "quicksave.json";
}

void EditorApp::SaveGame() {
    if (!playing_) {
        post_({{"type", "notify"}, {"text", "Saving the game works while playing (F5, then F6)"}});
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(SaveGamePath().parent_path(), ec);
    std::ofstream out(SaveGamePath(), std::ios::trunc | std::ios::binary);
    if (!out) {
        post_({{"type", "notify"}, {"text", "Couldn't write the saved game"}});
        return;
    }
    out << session_.MakeSave().dump(2) << "\n";
    session_.Toast("Game saved");
    editorStateStale_ = true;
}

void EditorApp::LoadGame() {
    std::ifstream in(SaveGamePath(), std::ios::binary);
    nlohmann::json save = in ? nlohmann::json::parse(in, nullptr, false) : nlohmann::json();
    if (!in || save.is_discarded() || !save.is_object()) {
        post_({{"type", "notify"}, {"text", "No saved game yet: press F6 while playing to save one"}});
        return;
    }
    if (!playing_) {
        StartPlay(); // Play ▸ Continue from save
    }
    // The saved scene as it is on disk (the one being edited: as it is now),
    // then the player, the clock and the story as they were saved.
    const std::string scene = save.value("scene", OpenSceneRelative());
    std::string text;
    if (scene == OpenSceneRelative()) {
        text = playSnapshot_;
    } else {
        std::ifstream sceneIn(assets_.GetProject().Resolve(scene), std::ios::binary);
        std::ostringstream buffer;
        buffer << sceneIn.rdbuf();
        text = buffer.str();
    }
    if (text.empty()) {
        post_({{"type", "notify"}, {"text", "The saved game is in " + scene + ", which doesn't exist any more"}});
        return;
    }
    Select(std::nullopt);
    DeserializeScene(registry_, text);
    PlayLayersFor(scene);
    prefabs_.SyncAll(registry_);
    session_.OnSceneChanged();
    session_.ApplySave(save);
    session_.SetScene(scene);
    UpdateWorldToScreen(registry_, iso_);
    if (auto player = FindPlayer(registry_)) {
        int width = 0, height = 0;
        viewport_.GetFramebufferSize(width, height);
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
    selectionIndexAtPlay_ = selected_ ? IndexOfEntity(registry_, *selected_) : std::nullopt;
    session_.ResetClock();
    session_.OnSceneChanged(); // no gateway fires under a player who starts on one
    // Dialogue and quests as they are on disk now; a fresh run each Play.
    story_.Refresh();
    world_.Refresh();
    session_.SetStory(&story_);
    session_.SetScene(OpenSceneRelative());
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
        SnapCameraTo(camera_, registry_, *player, static_cast<float>(width), static_cast<float>(height));
    }
    viewport_.Focus(); // so WASD works without first clicking the viewport
    editorStateStale_ = true;
}

void EditorApp::StopPlay() {
    if (!playing_) {
        return;
    }
    DeserializeScene(registry_, playSnapshot_);
    LayersOf(registry_) = std::move(playLayers_);
    playLayers_ = {};
    playSnapshot_.clear();
    playing_ = false;
    banner_ = {};
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
        AddStatsOverlay(ui);
        return ui;
    }
    int width = 0;
    int height = 0;
    viewport_.GetFramebufferSize(width, height);
    session_.BuildUi(ui, static_cast<float>(width), static_cast<float>(height));
    if (!banner_.first.empty()) {
        AddBanner(ui, banner_.first, banner_.second, static_cast<float>(width), static_cast<float>(height));
    }
    AddStatsOverlay(ui);
    return ui;
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
    PollAi();
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
    if (playing_) {
        const auto updateStart = std::chrono::steady_clock::now();
        session_.Advance(frameSeconds, [this](Key key) { return viewport_.IsKeyDown(key); });
        HandleTravel();
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
        if (player && !panning_) {
            int width = 0;
            int height = 0;
            viewport_.GetFramebufferSize(width, height);
            FollowWithCamera(camera_, registry_, *player, static_cast<float>(width), static_cast<float>(height),
                             static_cast<float>(frameSeconds), ActiveSettings().camera.followStiffness);
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
    if (build_ && (build_->Stage() == "plan" || build_->Stage() == "planned")) {
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

    if (hasHover_ && !panning_ && !dragging_) {
        int gx = 0;
        int gy = 0;
        CellAt(hoverX_, hoverY_, iso_, gx, gy);
        static constexpr float kWhite[3] = {1.0f, 1.0f, 1.0f};
        switch (tool_) {
            case Tool::Select:
                if (auto hovered = PickEntity(registry_, hoverX_, hoverY_, iso_); hovered && hovered != selected_) {
                    AddEntityOutline(overlay, registry_, *hovered, iso_, kWhite, 0.35f, 1.0f);
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
        // The model chosen in Preferences, for the service to start with.
        const nlohmann::json chosen = userPrefs_ ? userPrefs_->Get("aiProvider", "mistral") : nlohmann::json("mistral");
        _putenv_s("AIGE_PROVIDER", chosen.is_string() ? chosen.get<std::string>().c_str() : "mistral");
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

void EditorApp::SendProvider() {
    if (!Ai() || Ai()->GetState() != AiClient::State::Ready || !userPrefs_) return;
    const nlohmann::json provider = userPrefs_->Get("aiProvider", "mistral");
    const nlohmann::json model = userPrefs_->Get("aiModel", "");
    providerRequest_ = Ai()->Request("PUT", "/provider",
                                     {{"provider", provider.is_string() ? provider : nlohmann::json("mistral")},
                                      {"model", model.is_string() ? model : nlohmann::json("")}});
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
            {"text", status}};
}

void EditorApp::PollAi() {
    if (!Ai()) {
        return;
    }
    while (auto result = Ai()->Poll()) {
        if (result->kind == AiClient::Result::Kind::Status) {
            if (Ai()->GetState() == AiClient::State::Ready) SendProvider();
            post_(AiStatusJson());
        } else if (result->kind == AiClient::Result::Kind::Response) {
            if (result->requestId != 0 && result->requestId == providerRequest_) {
                providerRequest_ = 0;
                providerStatus_ = result->body.is_object() ? result->body : nlohmann::json{{"ok", false}};
                post_(AiStatusJson());
            } else if (build_ && build_->Owns(result->requestId)) {
                build_->OnResponse(result->requestId, result->body);
                PostBuild();
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
        } else if (build_ && build_->Owns(result->requestId)) {
            build_->OnResponse(result->requestId, result->body.is_object() ? result->body : nlohmann::json{{"ok", false}});
            PostBuild();
        } else {
            OnProposal(result->requestId, result->body);
        }
    }
}

void EditorApp::StartBuild(const std::string& query) {
    if (build_ && build_->Stage() == "interpreting") {
        post_({{"type", "notify"}, {"text", "A build is still reading its request"}});
        return;
    }
    build_ = MakeBuild();
    const std::string why = build_->Start(query);
    if (!why.empty()) post_({{"type", "notify"}, {"text", "Can't build: " + why}});
    post_({{"type", "build:open"}});
    PostBuild();
}

void EditorApp::ResumeBuild(const std::string& id) {
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
    return std::make_unique<PlaceBuild>(std::move(host));
}

void EditorApp::PostBuild() {
    if (!build_) {
        post_({{"type", "build:state"}, {"stage", ""}});
        return;
    }
    const std::string stage = build_->Stage();
    if (!build_->HasOrigin() && (stage == "plan" || stage == "planned" || stage == "built")) {
        const auto [x, y] = FreeOrigin(registry_, build_->Id(), build_->SiteSide());
        build_->SetOrigin(x, y);
    }
    post_(build_->State());
    post_(build_->Card());
}

void EditorApp::BuildIntoScene() {
    if (!build_) return;
    if (playing_) {
        post_({{"type", "notify"}, {"text", "Stop Play mode before building."}});
        return;
    }
    if (const std::string why = build_->StartBuilding(); !why.empty()) {
        post_({{"type", "notify"}, {"text", "Not yet: " + why}});
        return;
    }
    PostBuild(); // ④ Build shows while it works
    if (!build_->HasOrigin()) {
        const auto [x, y] = FreeOrigin(registry_, build_->Id(), build_->SiteSide());
        build_->SetOrigin(x, y);
    }
    PlaceSceneOptions options;
    options.place = build_->Id();
    std::tie(options.originX, options.originY) = build_->Origin();
    options.iso = iso_;
    options.isoGrid = ActiveSettings().grid.style != GridStyle::TopDown;
    const TerrainSet terrains = ProjectTerrainSet(prefabs_, assets_.GetProject());
    const Catalog catalog = BuildCatalog(assets_, &Places(), CatalogOptionsNow());
    // The asset plan, the build, then verify and repair before it becomes one undo step.
    const nlohmann::json assets = PlanPlaceAssets(Places(), build_->Spec(), build_->Solved(), catalog, prefabs_, assets_, terrains, options);
    nlohmann::json report;
    auto command = BuildPlaceScene(registry_, prefabs_, build_->Solved(), assets, terrains, options, &report);
    const nlohmann::json verify = VerifyPlaceScene(registry_, build_->Solved(), assets, options);
    bool failing = false;
    for (const auto& c : verify) failing = failing || (!c.value("ok", true) && !c.value("warning", false));
    nlohmann::json repairs = nlohmann::json::array();
    nlohmann::json after = verify;
    if (failing) {
        repairs = RepairPlaceScene(registry_, prefabs_, build_->Solved(), assets, verify, options);
        after = VerifyPlaceScene(registry_, build_->Solved(), assets, options);
    }
    history_.PushCommand(std::move(command));
    prefabs_.SyncAll(registry_);
    UpdateWorldToScreen(registry_, iso_);
    dirty_ = true;
    editorStateStale_ = true;
    sceneTreeStale_ = true;
    // What it looks like in the scene, drawn as the renderer draws it: for the Result step.
    const int built = build_->SiteSide();
    const ImageData picture = RenderScenePreview(registry_, assets_.GetProject(), iso_, options.originX, options.originY,
                                                 options.originX + built, options.originY + built, 1600);
    WritePng(build_->Folder() / "scene.png", picture);
    static int pictures = 0;
    build_->Built({{"assets", assets}, {"report", report}, {"verify", verify}, {"repairs", repairs}, {"after", after},
                   {"origin", {options.originX, options.originY}},
                   {"image", "places/" + build_->Id() + "/scene.png?v=" + std::to_string(++pictures)}});
    // Look at it.
    const int side = build_->SiteSide();
    const ScreenPose centre = ProjectIso(options.originX + side / 2.0f, options.originY + side / 2.0f, iso_);
    int width = 0, height = 0;
    viewport_.GetFramebufferSize(width, height);
    camera_.offsetX = centre.x - width / (2.0f * camera_.zoom);
    camera_.offsetY = centre.y - height / (2.0f * camera_.zoom);
}

void EditorApp::SendToAi(const std::string& text, const std::string& focus, bool asChange) {
    if (!Ai() || text.empty()) {
        return;
    }
    if (playing_) {
        post_({{"type", "ai:message"}, {"role", "error"}, {"text", "Stop Play mode before asking for changes."}});
        return;
    }
    // A change to the place the Build panel has open goes there, as a revision (7.8).
    if (!asChange && build_ && (build_->Stage() == "plan" || build_->Stage() == "planned" || build_->Stage() == "built") &&
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
    // Building a place goes to the Build panel, not a proposal card.
    if (!asChange && LooksLikeBuildRequest(Places(), text)) {
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
    for (const auto& section : EngineReflection().AllSettings()) {
        nlohmann::json fields = nlohmann::json::array();
        for (const auto& field : section.fields) {
            fields.push_back(FieldJson(field, field.get()));
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
        places_.emplace();
        places_->Load({EnginePlacesDir(), assets_.GetProject().root / "places"});
    }
    return *places_;
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
            {"hasSave", std::filesystem::exists(SaveGamePath())}};
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
