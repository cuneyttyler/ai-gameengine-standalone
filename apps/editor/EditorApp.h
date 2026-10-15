#pragma once

#include "ai_bridge/AiClient.h"
#include "ai_bridge/AiConnection.h"
#include "ai_bridge/Proposal.h"
#include "ai_bridge/ReviewPolicy.h"
#include "ai_bridge/TaskRunner.h"
#include "ai_bridge/UserPrefs.h"
#include "world/WorldBible.h"
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
#include "ui/GameUi.h"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <functional>
#include <map>
#include <memory>
#include <set>
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
    // Does nothing until the user has said yes to AI (a per-user preference,
    // see AiConnection); turning it on later starts the service then.
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
    std::vector<UiQuad> BuildUi(); // the game's own UI, in Play mode

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

    nlohmann::json AiStatusJson();
    void PollAi();
    void SendToAi(const std::string& text, const std::string& focus);
    void OnProposal(int requestId, const nlohmann::json& body);
    void ApplyProposal(int proposalId, const nlohmann::json& decisions);
    void RejectProposal(int proposalId);
    void LogAiDecision(int proposalId, const nlohmann::json& decision);

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
    void CreateScriptFor(Entity entity, const std::string& name);
    std::string LabelOf(Entity entity);
    std::vector<std::string> ListScripts();

    HWND mainWindow_;
    ViewportWindow& viewport_;
    Registry& registry_;
    LuaVM& lua_;
    AssetDatabase& assets_;
    PrefabLibrary prefabs_;
    std::pair<std::string, std::string> banner_; // Play: "Victory!" / "You died", drawn by the game
    WorldBible world_; // the project's canon, world/ (development_plan.md 3.1)
    StoryLibrary story_; // dialogue/ and quests/ (3.2b), reloaded when Play starts
    int worldSearchRequest_ = 0;
    nlohmann::json WorldJson();
    nlohmann::json StoryJson(); // the Story tab: dialogues, quests, problems, what can be referenced
    std::optional<std::string> SaveStoryFile(const std::string& folder, const std::string& id,
                                             const std::string& originalId, const nlohmann::json& content);
    // The AI Orchestration Service (started with the editor; optional — the
    // editor is fully usable without it).
    std::unique_ptr<UserPrefs> userPrefs_;
    std::unique_ptr<AiConnection> aiConnection_; // runs the service only if the user said yes
    AiClient* Ai();                              // null while AI is off or not set up
    void SetAiEnabled(bool enabled);
    struct PendingProposal {
        std::string instruction;
        nlohmann::json proposal; // as the service returned it
        bool decided = false;
        // Items applied without review (ReviewPolicy), and how to put them
        // back: each undo op restores `before` if the value is still `after`.
        std::vector<size_t> autoApplied;
        struct AutoUndo {
            nlohmann::json op;
            nlohmann::json after;
            std::string target;
        };
        std::vector<AutoUndo> autoUndo;
        bool autoUndone = false;
        std::shared_ptr<TaskRunner> task; // set when this is a finished task's combined result
    };
    ApplyOutcome ApplyAccepted(const std::vector<nlohmann::json>& operations, const TaskRunner* task = nullptr,
                               const std::vector<size_t>& indices = {});
    // The multi-step task in progress, if any (development_plan.md 2.9).
    std::shared_ptr<TaskRunner> task_;
    int taskId_ = 0;        // the request that started it (its card in the chat)
    int taskRequestId_ = 0; // the step request in flight
    std::set<int> cancelledRequests_;
    void StartTask(int requestId, const std::string& instruction, const nlohmann::json& firstAnswer,
                   const nlohmann::json& usage);
    void FeedTask(const nlohmann::json& answer, int inputTokens);
    void OnTaskAnswer(const nlohmann::json& body);
    void FinishTask();
    void StopTask(int id, const std::string& reason);
    void UndoAutoApplied(int proposalId);
    ReviewPolicy CurrentReviewPolicy() const;
    std::map<int, PendingProposal> proposals_; // by request id
    std::map<int, std::string> requestInstructions_;
    // The conversation so far, sent back as context for follow-ups ("make
    // them faster too"). Plain text turns, newest last.
    std::vector<std::pair<std::string, std::string>> conversation_;
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
