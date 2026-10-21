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
    OfferDifficulties();
    ApplyAssists();
}

// Difficulty (16): the levels the menus offer, and the player's assists.
void GameRunner::OfferDifficulties() {
    std::vector<GameFlow::DifficultyChoice> levels;
    for (const auto& level : session_->Difficulty().Levels()) levels.push_back({level.id, level.name, level.description});
    const std::string current =
        session_->DifficultyLevelId().empty() ? session_->Difficulty().DefaultLevel() : session_->DifficultyLevelId();
    const auto& settings = ActiveSettings().difficulty;
    flow_.SetDifficulties(std::move(levels), current, settings.changeAnytime, settings.adaptive);
}

void GameRunner::ApplyAssists() {
    Assists& assists = ActiveAssists();
    assists.invincible = settings_.invincible;
    assists.damageReduction = settings_.damageReduction;
    assists.growingReduction = settings_.growingReduction;
    assists.gameSpeed = settings_.gameSpeed;
    session_->SetAdaptive(settings_.adaptive);
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
        case Kind::NewGame:
            NewGame();
            session_->SetDifficulty(command.level.empty() ? flow_.CurrentDifficulty() : command.level);
            ActiveAssists().deaths = 0;
            break;
        case Kind::DifficultyChanged:
            session_->SetDifficulty(command.level);
            log_.push_back("Difficulty: " + command.level);
            break;
        case Kind::Load:
            if (!LoadFrom(command.slot)) NewGame();
            OfferDifficulties(); // the save's level
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
            ApplyAssists();
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
    if (docs_) docs_->Update(seconds);
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
    NotifyByRules();
    TravelIfAsked();
    if (const auto player = FindPlayer(registry_)) {
        const bool free = !session_->Conversation().Active() && !session_->InCutscene();
        if (session_->Input().Pressed("inventory") && free) windows_.OpenInventory(*player);
        if (session_->Input().Pressed("spellbook") && free) windows_.OpenSpellbook(*player);
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
    const bool title = flow_.Current() == GameFlow::State::Title;
    if (!title) session_->BuildUi(out, viewportWidth, viewportHeight);
    if (docs_) {
        // The documents (17): the menu or window on screen, the dialogue, the HUD.
        docs_->SetViewport(static_cast<int>(viewportWidth), static_cast<int>(viewportHeight));
        auto view = windows_.View(registry_);
        if (!view) view = flow_.View();
        docs_->SetMenu(view, MenuScreen());
        docs_->SetDialogue(view ? std::nullopt : session_->CurrentDialogueView());
        HudData hud = session_->MakeHud(viewportWidth, viewportHeight);
        hud.visible = hud.visible && !title && flow_.InPlay();
        docs_->SetHud(hud);
        docs_->Render(out);
    } else {
        if (auto view = windows_.View(registry_)) AddMenu(out, *view, viewportWidth, viewportHeight);
        if (auto view = flow_.View()) AddMenu(out, *view, viewportWidth, viewportHeight);
    }
    AddScreenFade(out, static_cast<float>(fade_), viewportWidth, viewportHeight);
}

std::string GameRunner::MenuScreen() const {
    switch (windows_.Open()) {
        case PlayWindows::Kind::Inventory: return "inventory";
        case PlayWindows::Kind::Shop: return "shop";
        case PlayWindows::Kind::Journal: return "journal";
        case PlayWindows::Kind::Spellbook: return "spellbook";
        default: break;
    }
    switch (flow_.Current()) {
        case GameFlow::State::Title: return "title";
        case GameFlow::State::Paused: return "pause";
        case GameFlow::State::Load: return "load";
        case GameFlow::State::Save: return "save";
        case GameFlow::State::GameOver: return "game-over";
        case GameFlow::State::Victory: return "victory";
        case GameFlow::State::Credits: return "credits";
        case GameFlow::State::Settings: return "settings";
        case GameFlow::State::Difficulty: return "difficulty";
        default: return "";
    }
}

void GameRunner::EnableDocuments(DocumentUi::ImageLoader images) {
    if (!ActiveSettings().ui.documents) return; // the project keeps the built-in overlays
    docs_ = std::make_unique<DocumentUi>(project_.root, std::move(images));
    if (!docs_->Ready()) {
        for (const auto& p : docs_->TakeProblems()) log_.push_back("UI: " + p);
        docs_.reset();
        return;
    }
    // A click on an item picks it, through the same logic as the keys.
    docs_->onMenuChoose = [this](int index) {
        if (windows_.Open() != PlayWindows::Kind::None) {
            windows_.Choose(registry_, index);
            if (const std::string message = windows_.TakeMessage(); !message.empty()) session_->Toast(message);
        } else {
            HandleCommand(flow_.Choose(index));
        }
    };
    docs_->onDialogueChoose = [this](int index) { session_->ChooseDialogue(index); };
    docs_->onAction = [this](const std::string& action) {
        session_->Emit({"ui_action", entt::null, entt::null, 0.0, action}); // scripts: on("ui_action", fn)
    };
    session_->SetNoticeSink([this](const std::string& channel, const std::string& text, const std::string& key, double seconds) {
        docs_->Notify({channel, text, "", seconds, 1, key});
    });
    session_->SetDocumentHud(true);
    session_->SetScreenHook([this](const std::string& id, int open) {
        if (open < 0) return docs_->IsShown(id);
        docs_->ShowScreen(id, open == 1);
        return true;
    });
    LoadNotificationRules();
}

std::vector<DocumentUi::NewTexture> GameRunner::TakeUiTextures() {
    if (!docs_) return {};
    for (const auto& p : docs_->TakeProblems()) log_.push_back("UI: " + p);
    return docs_->TakeNewTextures();
}

void GameRunner::UiMouse(int x, int y, int button, bool down) {
    if (!docs_) return;
    if (button < 0) docs_->MouseMove(x, y);
    else docs_->MouseButton(button, down);
}

// ui/notifications.json (the project's, else the engine's): events -> notifications.
void GameRunner::LoadNotificationRules() {
    notificationRules_ = nlohmann::json::array();
    for (const auto& file : {project_.root / "ui" / "notifications.json", EngineUiDir() / "notifications.json"}) {
        std::ifstream in(file, std::ios::binary);
        if (!in) continue;
        const nlohmann::json json = nlohmann::json::parse(in, nullptr, false);
        if (json.is_object() && json.contains("rules") && json["rules"].is_array()) {
            notificationRules_ = json["rules"];
            return;
        }
    }
}

void GameRunner::NotifyByRules() {
    if (!docs_ || notificationRules_.empty()) return;
    for (const auto& event : session_->DeliveredEvents()) {
        for (const auto& rule : notificationRules_) {
            if (!rule.is_object() || rule.value("event", "") != event.name) continue;
            std::string text = Tr(rule.value("text", ""), rule.value("text", ""));
            auto put = [&](const std::string& name, const std::string& value) {
                for (size_t at = text.find(name); at != std::string::npos; at = text.find(name, at + value.size())) {
                    text.replace(at, name.size(), value);
                }
            };
            char amount[32];
            std::snprintf(amount, sizeof(amount), "%g", event.amount);
            put("{amount}", amount);
            put("{count}", event.amount > 0 ? amount : "1");
            put("{text}", event.text);
            const std::string coalesce = rule.value("coalesce", "");
            docs_->Notify({rule.value("channel", "toast"), text, rule.value("icon", ""), rule.value("seconds", 3.0),
                           std::max(1, static_cast<int>(event.amount)), coalesce == "text" ? event.text : std::string()});
        }
    }
}

} // namespace aige
