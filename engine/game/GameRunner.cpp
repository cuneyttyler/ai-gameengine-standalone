#include "game/GameRunner.h"

#include "assets/Prefab.h"
#include "assets/SceneFiles.h"
#include "assets/SceneIO.h"
#include "game/GameAudio.h"
#include "gameplay/Attributes.h"
#include "gameplay/Localization.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/Components.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/Isometric.h"
#include "gameplay/Settings.h"
#include "physics2d/Components.h"

#include <fstream>
#include <sstream>

namespace aige {

namespace {

constexpr double kFadeSeconds = 0.45;
constexpr double kDeathPause = 1.5;

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

} // namespace

GameRunner::GameRunner(const Project& project, Registry& registry, LuaVM& lua, PrefabLibrary& prefabs,
                       GameRunnerOptions options)
    : project_(project),
      registry_(registry),
      lua_(lua),
      prefabs_(prefabs),
      options_(std::move(options)),
      story_(project.root),
      schedules_(project.root),
      world_(project),
      saves_(options_.saveDir) {
    session_ = std::make_unique<GameSession>(registry_, lua_, BootstrapIso(), project_.root);
    session_->SetSpawner([this](const std::string& prefab, float x, float y) {
        return prefabs_.Instantiate(registry_, prefab, x, y);
    });
    story_.Refresh();
    schedules_.Refresh();
    world_.Refresh();
    session_->SetSchedules(&schedules_);
    session_->SetNameResolver([this](const std::string& key) {
        const WorldEntry* entry = world_.Find(key);
        return entry ? entry->name : std::string();
    });
    GameManifest manifest = LoadGameManifest(project_.root);
    if (!options_.titleScreen) manifest.titleScreen = false;
    flow_.SetManifest(std::move(manifest));
    flow_.SetSlots(saves_.List());
    // The player's settings and the languages the game has (12.7).
    settings_ = LoadPlayerSettings(options_.saveDir / "settings.json");
    ActiveLocalization().Load(project_.root, settings_.language);
    flow_.SetPlayerSettings(&settings_, ActiveLocalization().Available());
}

void GameRunner::ApplySettings() {
    ActiveLocalization().Load(project_.root, settings_.language);
    if (audio_) {
        audio_->SetBusVolume(AudioBus::Master, settings_.master);
        audio_->SetBusVolume(AudioBus::Music, settings_.music);
        audio_->SetBusVolume(AudioBus::Effects, settings_.effects);
        audio_->SetBusVolume(AudioBus::Voice, settings_.voice);
        audio_->SetBusVolume(AudioBus::Ambience, settings_.ambience);
        audio_->SetBusVolume(AudioBus::Ui, settings_.ui);
    }
    if (onFullscreen) onFullscreen(settings_.fullscreen);
}

std::vector<std::string> GameRunner::TakeLog() {
    std::vector<std::string> out;
    out.swap(log_);
    return out;
}

void GameRunner::Start() {
    const auto command = flow_.Begin();
    if (flow_.Current() == GameFlow::State::Title) {
        // The start scene stands behind the title screen, still.
        NewGame();
    }
    HandleCommand(command);
}

std::string GameRunner::PlaceName(const std::string& scene) const {
    if (const WorldEntry* place = world_.LocationOf(scene)) return place->name;
    return std::filesystem::path(scene).stem().string();
}

void GameRunner::NewGame() {
    windows_.Close();
    const std::filesystem::path start = StartScenePath(project_);
    if (!LoadScene(registry_, start.string())) {
        registry_.clear();
        BuildBootstrapScene(registry_, "scripts/npc_wander.lua");
        log_.push_back("No scene at " + start.string() + "; playing the bootstrap level");
    }
    prefabs_.SyncAll(registry_);
    worlds_.clear();
    playSeconds_ = 0.0;
    actors_.Clear();
    deathTimer_ = -1.0;
    session_->SetStory(&story_); // a fresh run: quests and flags start over
    session_->OnSceneChanged();
    session_->SetScene(project_.Relative(start));
    actors_.Enter(registry_, project_.Relative(start), ActorSpawner());
    session_->SetClockMinutes(-1.0);
    session_->ResetClock();
    snapCamera_ = true;
    fade_ = 1.0;
}

ActorRoster::SpawnFn GameRunner::ActorSpawner() {
    return [this](const std::string& prefab, float x, float y) { return prefabs_.Instantiate(registry_, prefab, x, y); };
}

void GameRunner::CaptureCurrentScene() {
    const std::string scene = session_->Scene();
    if (scene.empty()) return;
    Registry pristine;
    if (!LoadScene(pristine, project_.Resolve(scene).string())) return;
    worlds_[scene] = CaptureSceneState(registry_, pristine, EngineReflection());
}

void GameRunner::ApplyWorld(const std::string& scene) {
    if (auto it = worlds_.find(scene); it != worlds_.end()) ApplySceneState(registry_, it->second, EngineReflection());
}

bool GameRunner::SaveTo(const std::string& slot) {
    CaptureCurrentScene();
    nlohmann::json save;
    save["session"] = session_->MakeSave();
    if (auto player = FindPlayer(registry_)) save["player"] = SerializeEntities(registry_, {*player});
    nlohmann::json worlds = nlohmann::json::object();
    for (const auto& [scene, state] : worlds_) worlds[scene] = state;
    save["worlds"] = worlds;
    actors_.Leave(registry_, session_->Scene());
    save["actors"] = actors_.ToJson();
    save["meta"] = {{"place", PlaceName(session_->Scene())},
                    {"playSeconds", playSeconds_},
                    {"title", flow_.Manifest().title}};
    const bool ok = saves_.Write(slot, save);
    flow_.SetSlots(saves_.List());
    if (slot != "auto") session_->Toast(ok ? "Game saved" : "Couldn't save the game");
    log_.push_back(ok ? "Saved to " + slot : "Couldn't save to " + slot);
    return ok;
}

bool GameRunner::LoadFrom(const std::string& slot) {
    windows_.Close();
    auto save = saves_.Read(slot);
    const nlohmann::json session = save ? save->value("session", nlohmann::json::object()) : nlohmann::json();
    const std::string scene = session.is_object() ? session.value("scene", std::string()) : std::string();
    if (scene.empty() || !LoadScene(registry_, project_.Resolve(scene).string())) {
        session_->Toast("No saved game");
        return false;
    }
    prefabs_.SyncAll(registry_);
    worlds_.clear();
    const nlohmann::json worlds = save->value("worlds", nlohmann::json::object());
    for (const auto& [name, state] : worlds.items()) worlds_[name] = state;
    ApplyWorld(scene);
    actors_.FromJson(save->value("actors", nlohmann::json::object()));
    actors_.Enter(registry_, scene, ActorSpawner());
    if (save->contains("player") && (*save)["player"].is_string()) ReplacePlayer(registry_, (*save)["player"].get<std::string>());
    session_->SetStory(&story_);
    session_->OnSceneChanged();
    session_->ApplySave(session);
    session_->SetScene(scene);
    playSeconds_ = save->value("meta", nlohmann::json::object()).value("playSeconds", 0.0);
    deathTimer_ = -1.0;
    snapCamera_ = true;
    fade_ = 1.0;
    log_.push_back("Loaded " + slot);
    return true;
}

void GameRunner::TravelIfAsked() {
    auto travel = session_->TakeTravel();
    if (!travel) return;
    const auto path = project_.Resolve(travel->scene);
    if (!std::filesystem::exists(path)) {
        log_.push_back("Gateway to a missing scene: " + travel->scene);
        session_->OnSceneChanged();
        return;
    }
    actors_.Leave(registry_, session_->Scene()); // who was there, and who goes elsewhere (14.4)
    CaptureCurrentScene(); // the scene left behind is remembered as it is
    const std::string warning = TravelToScene(registry_, ReadText(path), travel->marker, path);
    if (!warning.empty()) log_.push_back(travel->scene + ": " + warning);
    prefabs_.SyncAll(registry_);
    ApplyWorld(travel->scene);
    actors_.Enter(registry_, travel->scene, ActorSpawner());
    session_->OnSceneChanged();
    session_->SetScene(travel->scene);
    snapCamera_ = true;
    fade_ = 1.0;
    log_.push_back("Now in " + travel->scene);
    if (flow_.Manifest().autosave) SaveTo("auto");
}

void GameRunner::HandleCommand(const GameFlow::Command& command) {
    using Kind = GameFlow::Command::Kind;
    switch (command.kind) {
        case Kind::NewGame: NewGame(); break;
        case Kind::Load:
            if (!LoadFrom(command.slot)) NewGame();
            break;
        case Kind::Save: SaveTo(command.slot); break;
        case Kind::Resume: session_->ResetClock(); break;
        case Kind::QuitToTitle:
            NewGame(); // the start scene behind the title again
                break;
        case Kind::Quit: quit_ = true; break;
        case Kind::SettingsChanged:
            SavePlayerSettings(options_.saveDir / "settings.json", settings_);
            ApplySettings();
            break;
        case Kind::None: break;
    }
}

void GameRunner::Frame(double seconds, const KeyQueryFn& keys, const AxisQueryFn& axes) {
    FrameLogic(seconds, keys, axes);
    if (audio_) {
        const bool title = flow_.Current() == GameFlow::State::Title;
        ApplyAudio(*audio_, project_, DirectAudio(registry_, *session_, flow_.Manifest(), title), registry_,
                   static_cast<float>(seconds));
    }
}

void GameRunner::FrameLogic(double seconds, const KeyQueryFn& keys, const AxisQueryFn& axes) {
    if (fade_ > 0.0) fade_ = std::max(0.0, fade_ - seconds / kFadeSeconds);
    if (!flow_.InPlay()) {
        menuInput_.SetContext("menu");
        menuInput_.Update(ActiveInputMap(), keys, axes);
        HandleCommand(flow_.HandleInput(menuInput_));
        if (flow_.InPlay()) session_->ResetClock(); // the held key isn't the game's
        return;
    }
    // An inventory or shop window over play (13.3-13.4): the game waits.
    if (windows_.Open() != PlayWindows::Kind::None) {
        menuInput_.SetContext("menu");
        menuInput_.Update(ActiveInputMap(), keys, axes);
        windows_.HandleInput(registry_, menuInput_);
        if (const std::string message = windows_.TakeMessage(); !message.empty()) session_->Toast(message);
        if (windows_.Open() == PlayWindows::Kind::None) session_->ResetClock();
        return;
    }
    menuInput_.SetContext("gameplay"); // so the key that closes the menu doesn't reopen it
    menuInput_.Update(ActiveInputMap(), keys, axes);
    if (options_.pauseMenu && menuInput_.Pressed("menu") && !session_->Conversation().Active() && !session_->InCutscene()) {
        flow_.SetSlots(saves_.List());
        flow_.Pause();
        return;
    }
    playSeconds_ += seconds;
    session_->Advance(seconds, keys);
    TravelIfAsked();
    if (const auto player = FindPlayer(registry_)) {
        const bool free = !session_->Conversation().Active() && !session_->InCutscene();
        if (session_->Input().Pressed("inventory") && free) windows_.OpenInventory(*player);
        if (session_->Input().Pressed("journal") && free && ActiveSettings().story.journal && session_->StoryLibraryInUse()) {
            windows_.OpenJournal(*player, session_->Story(), *session_->StoryLibraryInUse());
        }
        if (auto merchant = session_->TakeShopRequest()) {
            const auto* progression = registry_.try_get<Progression>(*player);
            windows_.OpenShop(registry_, *player, *merchant, session_->Day(), progression ? progression->level : 1, shopRng_);
        }
    }
    if (session_->Input().Pressed("quicksave")) SaveTo("quick");
    if (session_->Input().Pressed("quickload")) LoadFrom("quick");

    const auto player = FindPlayer(registry_);
    for (const auto& event : session_->Events()) {
        if (event.type == GameEvent::Type::Death) actors_.OnDeath(event.actor);
        if (event.type == GameEvent::Type::Death && player && event.entity == *player && deathTimer_ < 0.0) {
            deathTimer_ = kDeathPause;
            log_.push_back("You died");
        } else if (event.type == GameEvent::Type::Victory) {
            flow_.SetSlots(saves_.List());
            flow_.OnVictory();
            log_.push_back("Victory");
        }
    }
    if (deathTimer_ >= 0.0) {
        deathTimer_ -= seconds;
        if (deathTimer_ < 0.0) {
            flow_.SetSlots(saves_.List());
            flow_.OnPlayerDied();
        }
    }
}

void GameRunner::BuildUi(std::vector<UiQuad>& out, float viewportWidth, float viewportHeight) {
    if (flow_.Current() != GameFlow::State::Title) session_->BuildUi(out, viewportWidth, viewportHeight);
    if (auto view = windows_.View(registry_)) AddMenu(out, *view, viewportWidth, viewportHeight);
    if (auto view = flow_.View()) AddMenu(out, *view, viewportWidth, viewportHeight);
    AddScreenFade(out, static_cast<float>(fade_), viewportWidth, viewportHeight);
}

} // namespace aige
