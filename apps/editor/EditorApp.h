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
#include "ai_bridge/AiTranscript.h"
#include "editing/Autotile.h"
#include "editing/VillageBuild.h"
#include "ai_bridge/ArtHistory.h"
#include "editing/EditHistory.h"
#include "editor/ViewportWindow.h"
#include "gameplay/Archetypes.h"
#include "gameplay/GameSession.h"
#include "gameplay/Isometric.h"
#include "gameplay/TileLayer.h"
#include "assets/Catalog.h"
#include "places/Ontology.h"
#include "editing/PlaceBuild.h"
#include "editing/PlaceScene.h"
#include "editing/ScenePreview.h"
#include "renderer2d/Camera2D.h"
#include "renderer2d/Renderer2D.h"
#include "ui/GameUi.h"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <thread>
#include <memory>
#include <chrono>
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
    // Shortcuts that are characters rather than keys ([ ] + -), so they work
    // on any keyboard layout.
    void OnViewportChar(wchar_t character);

    // The editor window regained focus: rescan assets so files edited in
    // other programs hot-reload.
    void OnAppActivated();

    // Once per frame, before rendering: advances Play mode or re-projects the
    // edited scene, and flushes pending UI updates.
    void Tick(double frameSeconds);

    const Camera2D& Camera() const { return camera_; }
    const IsoParams& Iso() const { return iso_; }
    std::vector<OverlayLine> BuildOverlay();
    std::vector<UiQuad> BuildUi(); // the game's own UI, in Play mode
    Registry& Preview() { return preview_; } // drawn with the scene (Renderer2D extra)

    // True when the scene has unsaved edits (asked before closing).
    bool IsDirty() const { return dirty_ || settingsDirty_; }
    void Save();
    // The panel layout (6.8), per project: the UI sends every change; Save
    // writes it to <project>/editor-layout.json, which is sent back on open.
    nlohmann::json layout_;
    nlohmann::json savedLayout_;
    std::filesystem::path LayoutPath() const;
    // Saved games while playing (6.9): F6 saves, F9 (or Play ▸ Continue from
    // save) loads. Kept in the project's .cache, apart from players' saves.
    std::filesystem::path SaveGamePath() const;
    void SaveGame();
    void LoadGame();

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
    // `flat`: the art is top-down; it's mapped onto the grid's diamond (5.2).
    void CreatePrefabFromAsset(const std::string& asset, int tile, bool asTile, bool solid, const std::string& name,
                               bool flat = false);
    void SaveSelectedAsPrefab(const std::string& name);
    void ApplySelectedToPrefab();
    void RevertOverride(const std::string& key);

    nlohmann::json AiStatusJson();
    void PollAi();
    // A request to build a place ("a village by a river") starts a build
    // (7.4) unless `asChange`; anything else becomes a proposal.
    void SendToAi(const std::string& text, const std::string& focus, bool asChange = false);
    void StartBuild(const std::string& query);
    void ResumeBuild(const std::string& id); // a build of the project, from places/<id>/
    void BuildIntoScene();                   // ④ Build: the solved layout into the scene (7.7)
    bool HandleBuildMessage(const std::string& type, const nlohmann::json& message);
    // Editing after the build (7.8): the scene follows the parts an edit laid out again.
    void UpdateBuiltParts();
    // The part of the open build an entity belongs to ("ward/cottages#2"), or "".
    std::string PartOf(Entity entity) const;
    void PostSelectedPart();
    std::string placeBrush_;           // the module brush's module ("" = off)
    bool brushing_ = false;
    int brushFromX_ = 0, brushFromY_ = 0, brushToX_ = 0, brushToY_ = 0;
    std::string editingPart_;          // the built part a hand edit is changing (it gets locked)
    std::unique_ptr<PlaceBuild> MakeBuild();
    void PostBuild();
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
    // The asset catalog v2 (7.3) for the Assets panel's review: every entry, its
    // piece sets and problems.
    nlohmann::json CatalogJson();
    CatalogOptions CatalogOptionsNow();
    // The place ontology: the engine's library and the project's places/ (loaded on first use).
    const Ontology& Places();
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
    ScheduleLibrary schedules_; // schedules/ (3.5), reloaded when Play starts
    int worldSearchRequest_ = 0;
    // The model the AI service writes with (File ▸ Preferences ▸ AI): sent when it's ready.
    void SendProvider();
    int providerRequest_ = 0;
    nlohmann::json providerStatus_ = nlohmann::json::object(); // its answer: {ok, provider, model, available, errors}
    // Contradiction checks (development_plan.md 3.3) in flight: request id ->
    // the proposal they're for; and the World tab's "Check against canon".
    std::map<int, int> consistencyRequests_;
    int worldCheckRequest_ = 0;
    int artRequest_ = 0; // the Art tab's generation in flight (development_plan.md 3.6)
    // Asset analysis (6.2): border terrains now, descriptions from the AI service.
    int analyzeRequest_ = 0;
    void AnalyzeAssets(const std::vector<std::string>& paths);
    void AnalyzeTileEdges(const std::vector<std::string>& paths);
    // Tile prefabs whose images have no border edges yet get them from their
    // pixels (no AI needed), so Auto-fit, terrain commands and villages work
    // without pressing Analyze. Each folder is tried once per session.
    void AnalyzeMissingTileEdges();
    std::set<std::string> edgeFoldersTried_;
    void ApplySuggestedNames(const std::vector<std::string>& paths);
    // Importing a pack (6.3): the plan to review, then the prefabs.
    std::vector<std::string> packPaths_;
    void PostPackPlan(const std::vector<std::string>& paths);
    void ImportPack(const nlohmann::json& plans);
    // Frame stats overlay (4.6, F3): smoothed frame and update times.
    bool showStats_ = false;
    double frameMs_ = 0.0;
    double updateMs_ = 0.0;
    void AddStatsOverlay(std::vector<UiQuad>& ui);
    // Scene editing polish (4.3).
    bool showGrid_ = false;   // tile borders on empty ground
    float brushScale_ = 1.0f; // the prefab brush places copies at this size
    int brushRotation_ = 0;   // quarter turns given to flat things the brush places (5.2)
    // Auto-fit (6.4): painting a ground tile lays its terrain and the engine
    // picks the pieces; erasing turns it back into the background.
    bool autotile_ = true;
    TerrainSet TerrainTiles();
    // A see-through copy of what the brush would put under the cursor (5.6),
    // in its own registry: never part of the scene, its undo or its file.
    Registry preview_;
    std::string previewKey_;
    void UpdatePreview();
    // The pointer says what a click does (5.6).
    void UpdateCursor();
    // R / Shift+R / Shift+wheel (6.6): the brush (and its preview) while one
    // is armed, else the selection. Flat images turn a quarter (`direction`
    // +1 clockwise, -1 back); upright ones mirror (a picture has one side).
    void Rotate(int direction = 1);
    // Prefab menu (5.2): top-down art made before 5.2, laid onto the grid.
    void SetPrefabFlat(const std::string& path, bool flat);
    void ApplyBrushRotation(std::optional<Entity> placed);
    std::string clipboard_;   // Ctrl+C: the selection as scene text
    void ScaleSelected(float factor);
    // [ ] - + and Alt+wheel: with the brush armed, the brush (its ghost and
    // everything it places next); otherwise the selection.
    void Resize(float factor);
    // An entity's size as a % of its prefab's (the Inspector's Size field);
    // nothing for entities that aren't prefab copies with a sprite.
    std::optional<float> SizePercent(Entity entity);
    void NudgeSelected(float dx, float dy);
    void CopySelected();
    void PasteAtCursor(std::optional<std::pair<int, int>> cell = std::nullopt);
    void RemovePrefab(const std::string& path);
    // Organizing (4.4): moves or renames a prefab or asset, following every reference.
    // `report` false: no refresh or notice (a folder's worth of moves, then one).
    bool MoveProjectItem(const std::string& from, const std::string& to, bool report = true);
    void RefreshAfterMoves();
    // Folders (5.6): rename one, or remove it — its items moved up a level,
    // or deleted with it.
    void RenameFolder(const std::string& folder, const std::string& newFolder);
    void RemoveFolder(const std::string& folder, bool deleteContents);
    // Scenes (5.3): the Scene Manager, and gateways during Play.
    nlohmann::json ScenesJson();
    std::string OpenSceneRelative() const; // "scenes/main.aige"
    void OpenScene(const std::string& scene);
    bool AskToSaveChanges(const wchar_t* question); // false: cancelled (or the save failed)
    void HandleTravel();
    // While playing: the tile layers of `scene` (the edited one as it was at Play, others as saved).
    void PlayLayersFor(const std::string& scene);
    // Build & Export (5.1): the game as a folder (and .zip) that runs on its own.
    void ExportGameTo(const std::string& name, bool zip);
    // Projects (4.2): asks about unsaved changes, then reopens the editor on `root`.
    void SwitchProject(const std::filesystem::path& root, bool askAboutChanges = true);
    nlohmann::json ProjectInfoJson();
    void RememberRecentProject();
    // Where the viewport is on the page, and the open menus/dialogs over it (4.1).
    PixelRect viewportBounds_;
    std::vector<PixelRect> overlays_;
    void ApplyOcclusion();
    // Right-click: a press that doesn't move far is a context menu, not a pan.
    float rightDownX_ = 0.0f, rightDownY_ = 0.0f;
    bool rightMoved_ = false;
    nlohmann::json ArtJson();
    std::string worldCheckKey_;
    bool RequestConsistencyCheck(int proposalId, const nlohmann::json& operations);
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
        // Its undo step once applied (0: not applied, or nothing changed in
        // the scene), and what was applied, to apply it again after an undo
        // the redo can no longer bring back.
        uint64_t historyStep = 0;
        std::vector<nlohmann::json> applied;
        std::string lastState; // as last sent to the panel
    };
    // The AI panel's Undo / Apply after a proposal was applied: follows Ctrl+Z
    // and Ctrl+Y (each applied proposal is one undo step).
    void PostProposalStates();
    void UndoProposal(int proposalId);
    void ReapplyProposal(int proposalId);
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
    // Kept per project (4.5): the chat as the panel showed it, and the Art tab's requests.
    AiTranscript transcript_;
    ArtHistory artHistory_;
    int artHistoryId_ = 0;
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
    // A run of quick resizes of one thing (Alt+wheel, [ ]) is one undo step:
    // snapshotting the whole scene per notch made resizing lag in big scenes.
    std::optional<Entity> resizeRun_;
    std::chrono::steady_clock::time_point resizeRunAt_{};

    bool playing_ = false;
    std::string playSnapshot_;
    TileLayers playLayers_; // the edited scene's tile layers while playing (7.2)
    std::optional<Ontology> places_;
    std::unique_ptr<PlaceBuild> build_; // the build open in the Build panel (7.4)
    // The build's heavy steps (planning, laying out: half a minute for a city) run off
    // the UI thread, one at a time (7.10). Nothing else touches build_ meanwhile: its
    // AI answers wait in buildDeferred_, the panel shows the last state as busy.
    // Declared after build_, so a running job is joined before the build goes.
    struct BuildJob {
        std::thread thread;
        std::atomic<bool> done{false};
        std::string why, prefix;
        std::function<void()> after;
        ~BuildJob() {
            if (thread.joinable()) thread.join();
        }
    };
    std::unique_ptr<BuildJob> buildJob_;
    struct DeferredResult {
        int id;
        nlohmann::json body;
        bool response;
    };
    std::deque<DeferredResult> buildDeferred_;
    nlohmann::json buildShown_; // the state last posted, shown while a job runs
    void RunBuildJob(std::function<std::string(PlaceBuild&)> job, const std::string& prefix = {}, std::function<void()> after = {});
    void FinishBuildJob();
    void OnBuildResult(int requestId, const nlohmann::json& body);
    bool KnownRequest(int requestId) const;
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
