#pragma once

#include "ai_bridge/AiClient.h"
#include "assets/AssetDatabase.h"
#include "assets/Prefab.h"
#include "audio/Audio.h"
#include "ecs/Registry.h"
#include "editing/EditHistory.h"
#include "editor/ViewportWindow.h"
#include "gameplay/Archetypes.h"
#include "gameplay/GameSession.h"
#include "gameplay/Isometric.h"
#include "renderer2d/Camera2D.h"
#include "renderer2d/Renderer2D.h"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace aige {

class LuaVM;

// Everything the editor does between the Vue UI, the native viewport and
// the scene: tools, selection, undo, camera, save, Play mode. The host
// (main.cpp) owns windows and the frame loop and forwards input/IPC here.
//
// The C++ side is the single source of truth for editor state (selection,
// tool, dirty flag, play state); the UI only renders what it's told and
// sends requests. Message types are listed in docs/technical.md §9.
class EditorApp {
public:
    using PostFn = std::function<void(const nlohmann::json&)>;

    EditorApp(HWND mainWindow, ViewportWindow& viewport, Registry& registry, LuaVM& lua, AssetDatabase& assets,
              AudioEngine& audio, Renderer2D& renderer, PostFn post);

    // Starts the AI service from `serviceDir` (ai_service/); call once after
    // construction. Without it, AI features report themselves unavailable.
    void StartAi(const std::filesystem::path& serviceDir);

    void OnWebMessage(const nlohmann::json& message);
    void OnViewportMouse(const ViewportMouseEvent& event);
    void OnViewportKey(int virtualKey, bool ctrl, bool shift);

    // The editor window regained focus: rescan assets so files edited in
    // other programs hot-reload.
    void OnAppActivated();

    // Once per frame, before rendering: advances Play mode or re-projects the
    // edited scene, and flushes pending UI updates.
    void Tick(double frameSeconds);

    const Camera2D& Camera() const { return camera_; }
    std::vector<OverlayLine> BuildOverlay();

    // True when the scene has unsaved edits (asked before closing).
    bool IsDirty() const { return dirty_ || settingsDirty_; }
    void Save();

private:
    // Everything placeable is a prefab, so there are only three tools: pick,
    // erase, and the prefab brush (paints tile prefabs, places the others).
    enum class Tool { Select, Erase, Prefab };

    // Edit transactions: BeginEdit snapshots the scene; the snapshot only
    // becomes an undo step once MarkChanged reports a real change, so clicks
    // and drags that change nothing don't pollute the history.
    void BeginEdit();
    void MarkChanged();
    void EndEdit();
    void ApplyOneShotEdit(const std::function<bool()>& edit);

    void Select(std::optional<Entity> entity);
    void SetTool(Tool tool);
    void Undo();
    void Redo();
    void DeleteSelected();
    void DuplicateSelected();
    void FrameScene();

    void StartPlay();
    void StopPlay();

    // Re-resolves the selection after the registry was rebuilt from a
    // snapshot (undo/redo, Stop), using the entity's stable index.
    void RestoreSelection(std::optional<size_t> index);

    void ToolStroke(float sceneX, float sceneY, bool first);

    void RefreshAssets();
    void ImportAssets();
    void ApplyAssetToSelected(const std::string& path, int tile);

    // Prefabs.
    void RefreshPrefabs();
    void EnsureStarterPrefabs();
    nlohmann::json PrefabsJson();
    void CreatePrefabFromAsset(const std::string& asset, int tile, bool asTile, bool solid, const std::string& name);
    void SaveSelectedAsPrefab(const std::string& name);
    void ApplySelectedToPrefab();
    void RevertOverride(const std::string& key);
    // Records that `key` was changed on this copy (no-op for non-instances
    // and instance-local fields).
    void NoteOverride(Entity entity, const std::string& component, const std::string& field);

    nlohmann::json AiStatusJson();
    void PollAi();

    // Project settings (Renderer, Camera, ...), from the registry.
    nlohmann::json SettingsJson();
    std::optional<std::string> SetSetting(const std::string& section, const std::string& field,
                                          const nlohmann::json& value);

    // Script properties (a script's `properties = { ... }`).
    nlohmann::json ScriptPropertiesJson(Entity entity);
    void SetScriptProperty(Entity entity, const std::string& name, const nlohmann::json& value);
    void ResetScriptProperty(Entity entity, const std::string& name);
    // The style a tileset brush paints with (texture region, size, solidity).
    std::optional<TileStyle> TilesetStyle(const std::string& path, int tile, bool solid) const;
    nlohmann::json AssetsJson();
    void UpdateTitle();

    nlohmann::json SceneJson();
    nlohmann::json InspectorJson(Entity entity);
    nlohmann::json EditorStateJson();
    // Sets a field from its JSON form after validating it against the
    // registry; returns why not if it can't.
    std::optional<std::string> ApplySetField(Entity entity, const std::string& component, const std::string& field,
                                             const nlohmann::json& value);
    void CreateScriptFor(Entity entity, const std::string& name);
    std::string LabelOf(Entity entity);
    std::vector<std::string> ListScripts();

    HWND mainWindow_;
    ViewportWindow& viewport_;
    Registry& registry_;
    LuaVM& lua_;
    AssetDatabase& assets_;
    PrefabLibrary prefabs_;
    // The AI Orchestration Service (started with the editor; optional — the
    // editor is fully usable without it).
    std::unique_ptr<AiClient> ai_;
    AudioEngine& audio_;
    Renderer2D& renderer_;
    std::string scenePath_;
    PostFn post_;

    IsoParams iso_;
    GameSession session_;
    EditHistory history_;
    Camera2D camera_;
    bool framedOnce_ = false;

    Tool tool_ = Tool::Select;
    std::string brushPrefab_; // Tool::Prefab
    // Bumped when a texture is re-cooked, so the UI reloads its thumbnail.
    std::unordered_map<std::string, int> assetVersions_;
    std::optional<Entity> selected_;

    bool dirty_ = false;
    bool settingsDirty_ = false; // project settings edited since the last save
    std::optional<std::string> pendingSnapshot_;
    bool pendingPushed_ = false;

    bool playing_ = false;
    std::string playSnapshot_;
    // The selection when Play started, restored on Stop. Indices taken from
    // the live registry at Stop would be off once enemies die mid-play.
    std::optional<size_t> selectionIndexAtPlay_;
    Camera2D cameraBeforePlay_;

    // Pointer state, in scene pixels.
    bool hasHover_ = false;
    float hoverX_ = 0.0f;
    float hoverY_ = 0.0f;
    bool panning_ = false;
    float panLastX_ = 0.0f; // viewport pixels
    float panLastY_ = 0.0f;
    bool dragging_ = false;
    float dragFromX_ = 0.0f;
    float dragFromY_ = 0.0f;
    bool stroking_ = false;
    int lastStrokeGx_ = 0;
    int lastStrokeGy_ = 0;

    // Coalesced UI updates, flushed in Tick.
    bool sceneTreeStale_ = true;
    bool inspectorStale_ = false;
    bool editorStateStale_ = true;
    double sinceInspectorRefresh_ = 0.0;
};

} // namespace aige
