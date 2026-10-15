#include "EditorApp.h"

#include "Win32Util.h"

#include "assets/SceneIO.h"
#include "editing/SceneEditing.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/Components.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "scripting/Components.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>

namespace aige {

namespace {

constexpr float kMinZoom = 0.25f;
constexpr float kMaxZoom = 4.0f;
constexpr double kPlayInspectorRefreshSeconds = 0.1;

// Ids shared with the UI's toolbar (apps/editor/ui/src/components/Toolbar.vue).
constexpr const char* kToolIds[] = {"select",    "tile:grass",   "tile:dirt", "tile:wall",   "place:npc",
                                    "place:player", "erase", "place:sprite", "tile:asset"};

// Accent colors for viewport overlays, matching the UI's accent.
constexpr float kAccent[3] = {0.231f, 0.510f, 0.965f};
constexpr float kErase[3] = {0.937f, 0.267f, 0.267f};

std::string DescribeKind(Registry& registry, Entity entity) {
    if (registry.all_of<Tile>(entity)) {
        return "Tile";
    }
    if (registry.all_of<Velocity2D, Collider2D>(entity)) {
        return "Player";
    }
    if (registry.all_of<LuaScript>(entity)) {
        return "Scripted NPC";
    }
    if (registry.all_of<WorldTransform2D>(entity)) {
        const auto* sprite = registry.try_get<Sprite>(entity);
        return sprite && !sprite->texture.empty() ? "Prop" : "NPC";
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

} // namespace

EditorApp::EditorApp(HWND mainWindow, ViewportWindow& viewport, Registry& registry, LuaVM& lua, AssetDatabase& assets,
                     AudioEngine& audio, Renderer2D& renderer, PostFn post)
    : mainWindow_(mainWindow),
      viewport_(viewport),
      registry_(registry),
      assets_(assets),
      audio_(audio),
      renderer_(renderer),
      scenePath_(assets.GetProject().MainScenePath().string()),
      post_(std::move(post)),
      iso_(BootstrapIso()),
      session_(registry, lua, iso_, assets.GetProject().root) {
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
        sceneTreeStale_ = true;
        editorStateStale_ = true;
        Select(selected_);
    } else if (type == "entity:select") {
        auto entity = static_cast<Entity>(message.value("id", 0u));
        Select(registry_.valid(entity) ? std::optional<Entity>(entity) : std::nullopt);
    } else if (type == "inspector:setField") {
        auto entity = static_cast<Entity>(message.value("id", 0u));
        if (registry_.valid(entity)) {
            std::string component = message.value("component", "");
            std::string field = message.value("field", "");
            float value = message.value("value", 0.0f);
            ApplyOneShotEdit([&] {
                ApplySetField(entity, component, field, value);
                return true;
            });
            inspectorStale_ = true;
            sceneTreeStale_ = true; // e.g. Tile.solid doesn't change kind, but cheap to resend
        }
    } else if (type == "tool:set") {
        // Asset brushes carry their asset (and tileset tile/solidity); other
        // tools keep the last brush so switching back restores it.
        if (message.contains("asset")) {
            brushAsset_ = message.value("asset", "");
            brushTile_ = message.value("tile", 0);
            brushSolid_ = message.value("solid", false);
        }
        std::string id = message.value("tool", "");
        for (int i = 0; i < static_cast<int>(std::size(kToolIds)); ++i) {
            if (id == kToolIds[i]) {
                SetTool(static_cast<Tool>(i));
            }
        }
    } else if (type == "assets:request") {
        post_(AssetsJson());
    } else if (type == "assets:refresh") {
        RefreshAssets();
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
            if (event.button != Button::Left || playing_) {
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
        case '1':
            SetTool(Tool::PaintGrass);
            return;
        case '2':
            SetTool(Tool::PaintDirt);
            return;
        case '3':
            SetTool(Tool::PaintWall);
            return;
        case 'N':
            SetTool(Tool::PlaceNpc);
            return;
        case 'P':
            SetTool(Tool::PlacePlayer);
            return;
        case 'E':
            SetTool(Tool::Erase);
            return;
        default:
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
        case Tool::PaintGrass:
        case Tool::PaintDirt:
        case Tool::PaintWall: {
            TileKind kind = tool_ == Tool::PaintWall  ? TileKind::Wall
                            : tool_ == Tool::PaintDirt ? TileKind::Dirt
                                                       : TileKind::Grass;
            // Painting replaces the tile entity; drop a selection pointing at it.
            if (selected_ && registry_.valid(*selected_) && registry_.all_of<Tile>(*selected_)) {
                const auto& tile = registry_.get<Tile>(*selected_);
                if (tile.gx == gx && tile.gy == gy) {
                    Select(std::nullopt);
                }
            }
            PaintTile(registry_, gx, gy, kind);
            break;
        }
        case Tool::PlaceNpc:
        case Tool::PlacePlayer:
            if (!first) {
                return; // one character per click, not a trail of them
            }
            Select(PlaceCharacter(registry_,
                                  tool_ == Tool::PlacePlayer ? CharacterKind::Player : CharacterKind::Npc, gx, gy));
            break;
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
        case Tool::PlaceSprite: {
            if (!first) {
                return;
            }
            const AssetInfo* asset = assets_.Find(brushAsset_);
            if (asset == nullptr || asset->type == AssetType::Audio || !asset->error.empty()) {
                post_({{"type", "notify"}, {"text", "Pick a texture in the Assets panel first"}});
                return;
            }
            float width = static_cast<float>(asset->type == AssetType::Tileset ? asset->tileWidth : asset->width);
            float height = static_cast<float>(asset->type == AssetType::Tileset ? asset->tileHeight : asset->height);
            Entity prop = CreateProp(registry_, static_cast<float>(gx) + 0.5f, static_cast<float>(gy) + 0.5f,
                                     brushAsset_, width, height);
            if (asset->type == AssetType::Tileset) {
                if (auto style = TilesetStyle(brushAsset_, brushTile_, false)) {
                    auto& sprite = registry_.get<Sprite>(prop);
                    sprite.u0 = style->u0;
                    sprite.v0 = style->v0;
                    sprite.u1 = style->u1;
                    sprite.v1 = style->v1;
                }
            }
            Select(prop);
            break;
        }
        case Tool::PaintTileset: {
            auto style = TilesetStyle(brushAsset_, brushTile_, brushSolid_);
            if (!style) {
                if (first) {
                    post_({{"type", "notify"}, {"text", "Pick a tile from a tileset in the Assets panel first"}});
                }
                return;
            }
            if (selected_ && registry_.valid(*selected_) && registry_.all_of<Tile>(*selected_)) {
                const auto& tile = registry_.get<Tile>(*selected_);
                if (tile.gx == gx && tile.gy == gy) {
                    Select(std::nullopt);
                }
            }
            PaintTile(registry_, gx, gy, *style);
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
    session_.ResetClock();
    playing_ = true;
    sinceInspectorRefresh_ = 0.0;
    viewport_.Focus(); // so WASD works without first clicking the viewport
    editorStateStale_ = true;
}

void EditorApp::StopPlay() {
    if (!playing_) {
        return;
    }
    auto index = selected_ ? IndexOfEntity(registry_, *selected_) : std::nullopt;
    DeserializeScene(registry_, playSnapshot_);
    playSnapshot_.clear();
    playing_ = false;
    RestoreSelection(index);
    sceneTreeStale_ = true;
    editorStateStale_ = true;
}

void EditorApp::RestoreSelection(std::optional<size_t> index) {
    Select(index ? EntityAtIndex(registry_, *index) : std::nullopt);
}

// --- Frame ------------------------------------------------------------------

void EditorApp::Tick(double frameSeconds) {
    if (playing_) {
        session_.Advance(frameSeconds, [this](Key key) { return viewport_.IsKeyDown(key); });
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
    if (playing_) {
        return overlay;
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
            case Tool::PaintGrass:
            case Tool::PaintDirt:
            case Tool::PaintWall:
            case Tool::PaintTileset:
                AddCellOutline(overlay, gx, gy, iso_, kWhite, 0.8f, 1.5f);
                break;
            case Tool::PlaceNpc:
            case Tool::PlacePlayer:
            case Tool::PlaceSprite:
                AddCellOutline(overlay, gx, gy, iso_, kAccent, 0.9f, 1.5f);
                break;
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
        list.push_back(entry);
    }
    return {{"type", "assets:list"}, {"assets", list}};
}

// --- JSON -------------------------------------------------------------------

nlohmann::json EditorApp::SceneJson() {
    nlohmann::json entities = nlohmann::json::array();
    for (auto entity : EntitiesInOrder(registry_)) {
        std::string kind = DescribeKind(registry_, entity);
        std::string label = kind;
        if (const auto* tile = registry_.try_get<Tile>(entity)) {
            label = "Tile " + std::to_string(tile->gx) + "," + std::to_string(tile->gy) + (tile->solid ? " (wall)" : "");
        }
        entities.push_back({{"id", static_cast<uint32_t>(entity)}, {"kind", kind}, {"label", label}});
    }
    return {{"type", "scene:update"}, {"entities", entities}};
}

nlohmann::json EditorApp::EditorStateJson() {
    return {{"type", "editor:state"},
            {"tool", kToolIds[static_cast<int>(tool_)]},
            {"brush", {{"asset", brushAsset_}, {"tile", brushTile_}, {"solid", brushSolid_}}},
            {"playing", playing_},
            {"dirty", dirty_},
            {"canUndo", history_.CanUndo() && !playing_},
            {"canRedo", history_.CanRedo() && !playing_},
            {"sceneName", std::filesystem::path(scenePath_).filename().string()}};
}

nlohmann::json EditorApp::InspectorJson(Entity entity) {
    nlohmann::json components = nlohmann::json::object();
    if (auto* c = registry_.try_get<Tile>(entity)) {
        components["Tile"] = {{"gx", c->gx}, {"gy", c->gy}, {"solid", c->solid ? 1 : 0}};
    }
    if (auto* c = registry_.try_get<WorldTransform2D>(entity)) {
        components["WorldTransform2D"] = {{"x", c->x}, {"y", c->y}};
    }
    if (auto* c = registry_.try_get<Velocity2D>(entity)) {
        components["Velocity2D"] = {{"x", c->x}, {"y", c->y}};
    }
    if (auto* c = registry_.try_get<Collider2D>(entity)) {
        components["Collider2D"] = {{"halfWidth", c->halfWidth}, {"halfHeight", c->halfHeight}};
    }
    if (auto* c = registry_.try_get<ScreenTransform2D>(entity)) {
        components["ScreenTransform2D"] = {{"x", c->x}, {"y", c->y}, {"depth", c->depth}};
    }
    if (auto* c = registry_.try_get<Sprite>(entity)) {
        components["Sprite"] = {{"texture", c->texture.empty() ? "(built-in)" : c->texture},
                                {"width", c->width},
                                {"height", c->height},
                                {"u0", c->u0},
                                {"v0", c->v0},
                                {"u1", c->u1},
                                {"v1", c->v1},
                                {"r", c->r},
                                {"g", c->g},
                                {"b", c->b},
                                {"a", c->a}};
    }
    if (auto* c = registry_.try_get<LuaScript>(entity)) {
        components["LuaScript"] = {{"path", c->path}};
    }
    return {{"type", "inspector:data"}, {"id", static_cast<uint32_t>(entity)}, {"components", components}};
}

// Hand-wired per component/field until milestone 1.4's component table.
void EditorApp::ApplySetField(Entity entity, const std::string& component, const std::string& field, float value) {
    if (component == "Tile") {
        if (auto* c = registry_.try_get<Tile>(entity)) {
            if (field == "gx") c->gx = static_cast<int>(value);
            else if (field == "gy") c->gy = static_cast<int>(value);
            else if (field == "solid") c->solid = value != 0.0f;
        }
    } else if (component == "WorldTransform2D") {
        if (auto* c = registry_.try_get<WorldTransform2D>(entity)) {
            if (field == "x") c->x = value;
            else if (field == "y") c->y = value;
        }
    } else if (component == "Velocity2D") {
        if (auto* c = registry_.try_get<Velocity2D>(entity)) {
            if (field == "x") c->x = value;
            else if (field == "y") c->y = value;
        }
    } else if (component == "Collider2D") {
        if (auto* c = registry_.try_get<Collider2D>(entity)) {
            if (field == "halfWidth") c->halfWidth = value;
            else if (field == "halfHeight") c->halfHeight = value;
        }
    } else if (component == "ScreenTransform2D") {
        if (auto* c = registry_.try_get<ScreenTransform2D>(entity)) {
            if (field == "x") c->x = value;
            else if (field == "y") c->y = value;
            else if (field == "depth") c->depth = value;
        }
    } else if (component == "Sprite") {
        if (auto* c = registry_.try_get<Sprite>(entity)) {
            if (field == "width") c->width = value;
            else if (field == "height") c->height = value;
            else if (field == "u0") c->u0 = value;
            else if (field == "v0") c->v0 = value;
            else if (field == "u1") c->u1 = value;
            else if (field == "v1") c->v1 = value;
            else if (field == "r") c->r = value;
            else if (field == "g") c->g = value;
            else if (field == "b") c->b = value;
            else if (field == "a") c->a = value;
        }
    }
}

} // namespace aige
