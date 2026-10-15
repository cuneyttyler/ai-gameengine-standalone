#include "EditorApp.h"

#include "Win32Util.h"

#include "ai_bridge/ContextBuilder.h"
#include "ai_bridge/DryRun.h"
#include "ai_bridge/Proposal.h"

#include "gameplay/EngineReflection.h"
#include "assets/SceneIO.h"
#include "editing/Edits.h"
#include "editing/LevelBuild.h"
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
#include <chrono>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
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

void AddEntityOutline(std::vector<OverlayLine>& out, Registry& registry, Entity entity, const IsoParams& iso,
                      const float* rgb, float alpha, float thickness) {
    if (const auto* tile = registry.try_get<Tile>(entity)) {
        AddCellOutline(out, tile->gx, tile->gy, iso, rgb, alpha, thickness);
        return;
    }
    const auto* t = registry.try_get<ScreenTransform2D>(entity);
    const auto* s = registry.try_get<Sprite>(entity);
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
      audio_(audio),
      renderer_(renderer),
      scenePath_(assets.GetProject().MainScenePath().string()),
      post_(std::move(post)),
      iso_(BootstrapIso()),
      session_(registry, lua, iso_, assets.GetProject().root) {
    session_.SetSoundPlayer([this](const std::string& path) { audio_.Play(assets_.GetProject().Resolve(path)); });
    session_.SetSpawner([this](const std::string& prefab, float x, float y) {
        return prefabs_.Instantiate(registry_, prefab, x, y);
    });
    EnsureStarterPrefabs();
    prefabs_.Refresh();
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
            if (!framedOnce_) {
                framedOnce_ = true;
                FrameScene();
            }
        }
    } else if (type == "scene:request") {
        // The page (re)loaded: resend everything it needs.
        post_(AiStatusJson());
        sceneTreeStale_ = true;
        editorStateStale_ = true;
        Select(selected_);
    } else if (type == "entity:select") {
        auto entity = static_cast<Entity>(message.value("id", 0u));
        Select(registry_.valid(entity) ? std::optional<Entity>(entity) : std::nullopt);
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
        std::string id = message.value("tool", "");
        for (int i = 0; i < static_cast<int>(std::size(kToolIds)); ++i) {
            if (id == kToolIds[i]) {
                SetTool(static_cast<Tool>(i));
            }
        }
    } else if (type == "ai:setEnabled") {
        SetAiEnabled(message.value("enabled", true));
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
    } else if (type == "art:request") {
        post_(ArtJson());
    } else if (type == "art:generate") {
        // Takes a few seconds per image, on the AI client's worker thread.
        if (Ai() == nullptr || Ai()->GetState() != AiClient::State::Ready) {
            post_({{"type", "art:result"}, {"result", {{"ok", false}, {"errors", {"AI is off"}}}}});
        } else if (artRequest_ == 0) {
            artRequest_ = Ai()->Request("POST", "/assets/generate", message.value("request", nlohmann::json::object()), 300);
        }
    } else if (type == "art:discard") {
        // Only generated art, by its project-relative path.
        const std::string path = message.value("path", "");
        if (path.rfind("assets/textures/generated/", 0) == 0 && path.find("..") == std::string::npos) {
            std::error_code ec;
            for (const char* suffix : {"", ".gen.json", ".meta"}) {
                std::filesystem::remove(assets_.GetProject().Resolve(path + suffix), ec);
            }
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
    } else if (type == "ai:send") {
        SendToAi(message.value("text", ""), message.value("focus", ""));
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
    } else if (type == "prefab:createFromAsset") {
        CreatePrefabFromAsset(message.value("asset", ""), message.value("tile", 0), message.value("asTile", false),
                              message.value("solid", false), message.value("name", ""));
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
    } else if (type == "view:frame") {
        FrameScene();
    } else if (type == "play:start") {
        StartPlay();
    } else if (type == "play:stop") {
        StopPlay();
    }
}

// --- Viewport input ---------------------------------------------------------

void EditorApp::OnViewportMouse(const ViewportMouseEvent& event) {
    using Type = ViewportMouseEvent::Type;
    using Button = ViewportMouseEvent::Button;

    float sceneX = 0.0f;
    float sceneY = 0.0f;
    camera_.ViewportToScene(event.x, event.y, sceneX, sceneY);

    switch (event.type) {
        case Type::Wheel: {
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
            if (event.button == Button::Right || event.button == Button::Middle || event.button == Button::None) {
                panning_ = false;
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
            return;
    }
}

void EditorApp::OnViewportKey(int virtualKey, bool ctrl, bool shift) {
    if (playing_) {
        if (virtualKey == VK_ESCAPE) {
            StopPlay();
        }
        return; // every other key belongs to the game
    }

    if (ctrl) {
        switch (virtualKey) {
            case 'Z':
                shift ? Redo() : Undo();
                return;
            case 'Y':
                Redo();
                return;
            case 'S':
                Save();
                return;
            case 'D':
                DuplicateSelected();
                return;
            default:
                return;
        }
    }

    switch (virtualKey) {
        case VK_DELETE:
        case VK_BACK:
            DeleteSelected();
            return;
        case VK_ESCAPE:
            SetTool(Tool::Select);
            Select(std::nullopt);
            return;
        case 'F':
            FrameScene();
            return;
        case 'V':
            SetTool(Tool::Select);
            return;
        case 'E':
            SetTool(Tool::Erase);
            return;
        default:
            // 1-9 arm the prefab brush with the palette's first nine prefabs.
            if (virtualKey >= '1' && virtualKey <= '9') {
                size_t index = static_cast<size_t>(virtualKey - '1');
                if (index < prefabs_.Prefabs().size()) {
                    brushPrefab_ = prefabs_.Prefabs()[index].path;
                    SetTool(Tool::Prefab);
                }
            }
            return;
    }
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
                prefabs_.Instantiate(registry_, prefab->path, cellX, cellY);
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
            Select(prefabs_.Instantiate(registry_, prefab->path, cellX, cellY));
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
    } catch (const std::exception& e) {
        post_({{"type", "notify"}, {"text", std::string("Save failed: ") + e.what()}});
        return;
    }
    dirty_ = false;
    editorStateStale_ = true;
    post_({{"type", "notify"}, {"text", "Saved " + std::filesystem::path(scenePath_).filename().string()}});
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
    // Dialogue and quests as they are on disk now; a fresh run each Play.
    story_.Refresh();
    world_.Refresh();
    session_.SetStory(&story_);
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
        return ui;
    }
    int width = 0;
    int height = 0;
    viewport_.GetFramebufferSize(width, height);
    session_.BuildUi(ui, static_cast<float>(width), static_cast<float>(height));
    if (!banner_.first.empty()) {
        AddBanner(ui, banner_.first, banner_.second, static_cast<float>(width), static_cast<float>(height));
    }
    return ui;
}

void EditorApp::Tick(double frameSeconds) {
    PollAi();
    if (playing_) {
        session_.Advance(frameSeconds, [this](Key key) { return viewport_.IsKeyDown(key); });
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
    }
}

std::vector<OverlayLine> EditorApp::BuildOverlay() {
    std::vector<OverlayLine> overlay;
    AppendHealthBars(registry_, overlay);
    if (playing_) {
        return overlay;
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
    std::wstring name = std::filesystem::path(scenePath_).filename().wstring();
    std::wstring title = name + (dirty_ ? L"*" : L"") + L" - AI-Native Game Engine Editor";
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
    aiConnection_ = std::make_unique<AiConnection>(*userPrefs_, [serviceDir, projectRoot]() {
        AiClient::Options options;
        options.serviceDir = serviceDir;
        auto client = std::make_unique<AiClient>(options);
        client->Start(EngineReflection().ExportSchema());
        client->Request("PUT", "/project", {{"root", projectRoot}}); // so it can index the World Bible
        return client;
    });
    aiConnection_->Start(); // only if the user has said yes to AI
    post_(AiStatusJson());
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
    return {{"type", "ai:status"},
            {"state", state},
            {"enabled", choice == AiChoice::On},
            {"choice", choice == AiChoice::On ? "on" : choice == AiChoice::Off ? "off" : "undecided"},
            {"reviewPolicy", ReviewPolicyName(CurrentReviewPolicy())},
            {"text", ai ? ai->StatusText() : text}};
}

void EditorApp::PollAi() {
    if (!Ai()) {
        return;
    }
    while (auto result = Ai()->Poll()) {
        if (result->kind == AiClient::Result::Kind::Status) {
            post_(AiStatusJson());
        } else if (result->kind == AiClient::Result::Kind::Response) {
            if (result->requestId == worldSearchRequest_) {
                post_({{"type", "world:searchResults"}, {"result", result->body}});
            } else if (result->requestId == artRequest_) {
                artRequest_ = 0;
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
        } else {
            OnProposal(result->requestId, result->body);
        }
    }
}

void EditorApp::SendToAi(const std::string& text, const std::string& focus) {
    if (!Ai() || text.empty()) {
        return;
    }
    if (playing_) {
        post_({{"type", "ai:message"}, {"role", "error"}, {"text", "Stop Play mode before asking for changes."}});
        return;
    }
    ContextOptions options;
    options.selection = selected_;
    options.focus = focus;
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
        StopTask(taskId_, FailureText(body, "The AI couldn't produce the next step."));
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
    ApplyOutcome outcome = ApplyAccepted(accepted, it->second.task.get(), indices); // one undo step (scene side)
    it->second.decided = true;
    it->second.task.reset(); // done with its working copy

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
    for (const auto& problem : ValidateStoryReferences(story_, world_, prefabPaths)) {
        problems.push_back({{"file", problem.file}, {"message", problem.message}});
    }
    schedules_.Refresh();
    nlohmann::json schedules = nlohmann::json::array();
    auto markerPositions = MarkerPositions(registry_);
    for (const auto& schedule : schedules_.Schedules()) {
        const std::string file = "schedules/" + schedule.id + ".json";
        schedules.push_back({{"id", schedule.id}, {"file", file}, {"content", ScheduleToJson(schedule)}});
        for (const auto& message : ValidateSchedule(schedule, [&](const std::string& name) {
                 return std::any_of(markerPositions.begin(), markerPositions.end(),
                                    [&](const auto& m) { return m.first == name; });
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
                                      const std::string& requestedName) {
    auto style = TilesetStyle(asset, tile, solid);
    if (!style) {
        post_({{"type", "notify"}, {"text", "Pick a texture (and tile) first"}});
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
    return {{"type", "art:list"}, {"items", list}, {"busy", artRequest_ != 0}};
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
        entities.push_back({{"id", static_cast<uint32_t>(entity)}, {"kind", kind}, {"label", LabelOf(entity)}});
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
            {"sceneName", std::filesystem::path(scenePath_).filename().string()}};
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

    return {{"type", "inspector:data"},
            {"id", static_cast<uint32_t>(entity)},
            {"label", LabelOf(entity)},
            {"prefab", prefab},
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
