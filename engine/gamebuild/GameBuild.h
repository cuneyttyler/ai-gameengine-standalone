#pragma once

#include "ecs/Registry.h"
#include "editing/PlaceBuild.h"
#include "planner/PlannerStore.h"
#include "story/StoryGraph.h"

#include <nlohmann/json.hpp>

#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace aige {

class Ontology;

// The AI Planner, building the game it designed (development_plan.md 23, docs/ai/ai-planner.md
// §5.10-5.13, §8-§11). It starts from the plan's baseline:
//
//   build      ⑩ the build plan (planner/buildplan.json): one row per place, parents first;
//              each place built through a PlaceBuild started from its draft Brief, driven
//              automatically (Confirm, Plan, Approve: each continue recorded as the
//              planner's) or by the user in the Build panel (manual rows); the solved
//              layout becomes the place's scene with its story anchors as markers; a
//              build that fails gets a placeholder place; then the world graph's
//              connections become gateways with arrival markers on both sides
//   populate   ⑪ characters placed in their homes (name, talk, behaviour), quest aliases
//              bound — fixed, by story anchor, by query among the scene's markers, or made
//              — the boss and quest items placed, reach objectives given markers
//              (planner/bindings.json)
//   write      ⑫ the game's files: quests (quests v2), dialogue trees (the AI, against what
//              each speaker knows; validated), cutscenes from cutscene scenes, records,
//              packages, the theme, game.json; a manifest with each file's provenance
//   playtest   ⑫ every scene loads, gateways lead somewhere, quests and dialogues
//              validate, every quest walked: each objective's target exists in a scene
//              reachable through the gateways and is reachable on foot there
//   result     ⑬ planner/result.json: the headline, asked vs made, places, quests,
//              checks, placeholders, what next
//
// The heavy steps (planning, solving, building scenes) run in Pump(), which the editor
// calls off the UI thread. State is in planner/buildstate.json, so it resumes.
// A building or thing of a built place, where it stands (its plan's, or for a freely painted place where the vision model
// found it in the painting): its cells, the side it faces (its door is the cell in front of that side).
struct PlannedThing {
    std::string id, slot, facing;
    int x = 0, y = 0, w = 1, h = 1;
    std::string kind; // what the vision model called it ("house", "market stall"); empty: the plan's
    int area = 0;     // how big it is: its cells (the plan's), or its box in the painting's pixels / 1000
    bool hasDoor = false; // its door's cell given (a painted one: in front of its foot), not from its side's middle
    int doorX = 0, doorY = 0;
};
// A land area of a place's plan that stories happen at: a square (or plaza, green), a graveyard, a field (farmland,
// pasture, orchard). Its box in the scene's cells; painted: where the painting shows it (a square: its stalls and wells).
struct PlannedArea {
    std::string id, module, kind;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool painted = false;
};
class GameSession;
class StoryLibrary;
class SceneAutopilot;
// A scene's test start (GameBuild::TestStart) on a session: each quest started, its stages entered in turn up to the
// one it's at (their effects done, what they had the player find in its pockets), or completed.
void ApplyTestStart(GameSession& session, Registry& registry, const StoryLibrary& library, const nlohmann::json& start);

// Placements (development_plan.md 34.11, Placements.cpp): planner/placements.json, every character's and story prop's decided
// home, work and spot, and where they are over time. Read as it is on disk ({} when there's none).
nlohmann::json ReadPlacements(const std::filesystem::path& projectRoot);
// The user's (or the AI panel's) edit of one entry ({kind: character | prop, id, patch: home, work, spot {scene, building? |
// marker? | x, y}, whereabouts, locked}): saved (locked unless the patch says otherwise); a spot moved moves the entity and
// its marker in its scene: in `open` when that's the scene open in the editor (`openScene`), else in the scene's file.
// Why not, or "". `movedScene`: the scene that changed (to reload), when it did.
std::string EditPlacement(const std::filesystem::path& projectRoot, const std::string& kind, const std::string& id, const nlohmann::json& patch,
                          Registry* open, const std::string& openScene, std::string* movedScene = nullptr);

class GameBuild {
public:
    struct Host {
        std::function<int(const std::string& path, const nlohmann::json& body, int timeoutSeconds)> request;
        std::filesystem::path projectRoot;
        std::function<const Ontology&()> places;
        // The editor builds a place as the Build panel does (its art found and made, its scene built,
        // checked and saved to `scene`): it starts and returns; editorBuildStatus says how it goes
        // ("working", "built", "failed: why"). Without them the scene is built here, with the art
        // the project has.
        std::function<std::string(const std::string& buildId, const std::string& scene)> buildInEditor;
        std::function<std::string(const std::string& buildId)> editorBuildStatus;
        // The plan's build method for its places (Assembled or Paint).
        std::function<std::string()> buildMethod;
        // The model places are painted with (the project's, else Preferences); without it FLUX-3-Image.
        std::function<std::string()> paintModel;
        // The model characters are animated with (Project Settings ▸ AI, else Preferences): /characters/animate's provider.
        std::function<std::string()> animateModel;
        // The model a thing is animated with (a gate opening, a glow pulsing: Project Settings ▸ AI ▸ Non-humanoid animation).
        std::function<std::string()> objectAnimateModel;
        // True: paid AI work (painting, characters' art) goes on without asking (tests, scripted builds).
        std::function<bool()> autoApprove;
        // A gameplay feature's request carried out by the AI panel's machinery, unreviewed: it starts and returns why not;
        // featureStatus says how it goes ("working", "done", "failed: why"). Without them features are noted, not built.
        // `scene`: opened from disk first and saved after (empty: what the whole project shares).
        std::function<std::string(const std::string& request, const std::string& scene)> startFeature;
        std::function<std::string()> featureStatus;
        std::function<void()> featuresDone; // the scene open before them, open again
    };

    explicit GameBuild(Host host);
    ~GameBuild();

    // Starts building from the plan's baseline. `mode`: automatic | manual (every row) |
    // the Brief's plan.building. Returns why not, or "".
    std::string Start(const std::string& mode = {});
    std::string Open(); // resumes planner/buildstate.json
    // Stops building: nothing more is started; a place being built goes back to waiting. Start
    // (Restart builds) builds every place again; Resume (Continue building) goes on from the next
    // place that isn't built, keeping those that are.
    void Stop();
    std::string Resume();
    bool Active() const { return !stage_.empty(); }
    const std::string& Stage() const { return stage_; }
    const std::string& Status() const { return status_; }

    bool Owns(int requestId) const;
    void OnResponse(int requestId, const nlohmann::json& body);
    // Moves everything on as far as it can without waiting: drives builds, builds scenes,
    // binds, writes, plays. Heavy: the editor runs it off the UI thread. True when it did
    // something.
    bool Pump();

    // ⑩ rows: switch a row to manual or automatic; try a failed row again; give it a
    // placeholder place; a manual row's build (for the Build panel), and its completion.
    std::string SetMode(const std::string& place, const std::string& mode);
    std::string Retry(const std::string& place);
    // Builds the place again from the start (a new build of it, built or not); the game build goes
    // on with it, then what follows (binding, files, playtest) again.
    std::string Rebuild(const std::string& place);
    std::string UsePlaceholder(const std::string& place);
    // A place built outside the game build (the build test, docs/build-fixes.md), its scene already
    // saved at the row's scene path: taken in as the Build panel's are (story markers added, the row
    // built, its file recorded). The rest of the game build goes on from there.
    std::string InstallBuilt(const std::string& place, const std::string& buildId);
    PlaceBuild* BuildOf(const std::string& place);
    nlohmann::json StoryFor(const std::string& place) const;
    // The place's story anchors, for a painting to show and find ([{id, about}]: BackdropRequests).
    nlohmann::json StoryAnchors(const std::string& place) const;
    std::string ManualDone(const std::string& place); // the user built it in the Build panel
    // Paid AI work waits for the user unless the Brief's plan.building is automatic (development_plan.md 32.8):
    // a painted place's painting (its tiles and calls shown), and the characters' art (who, and the calls).
    std::string ApprovePaint(const std::string& place);
    std::string ApproveArt(bool draw = true); // false: skipped, they keep their placeholders
    // Every character not drawn yet (the supporting and minor ones when the plan drew the main ones) added to the art and
    // drawn, whatever step the build is at once the characters are placed. Why not, or "".
    std::string DrawOthers();
    // What isn't drawn yet, drawn: every drawing that failed or was left part-way (a character's clips not made, an item's
    // icon), queued again; what's drawn stays. `part`: "characters" (and the enemies' kinds), "objects" (the items, the
    // ways through), "interface", or "" for all. Empty: under way; else why not.
    std::string DrawMissing(const std::string& part);
    // A drawing's art taken from the library instead of drawn (`path`: the project's copy). A character: its sheet and
    // animations (path: its .character.anim.json), its clips as it has them, onto its prefab; an item, an asset (a
    // prop's picture): its picture. Empty: done; else why not.
    std::string UseLibraryArt(const std::string& subject, const std::string& path);
    // A Build tab's items built now (every one not built yet: not started, failed, left half-way), `kind` only those of
    // that requirement kind ("cutscene"), "" all of the tab's. Built while the build is stopped, only they are: it stops
    // again after. Empty: under way; else why not.
    std::string BuildItems(const std::string& tab, const std::string& kind);
    // ⑪ again in the scenes as they are: what it placed taken out (characters, markers, quest items, spawners), then the
    // characters placed again (each at its anchor in its home), the quests' roles bound again and the quests written
    // again; the places, gateways and drawings kept. Each line said through `say`. Why not, or "".
    std::string PlaceCharactersAgain(const std::function<void(const std::string&)>& say);
    // Every place whose build is done (built, or at least sourced) built into its scene again, as the Build panel's
    // Build does it (the art its Assets found, the scene placed, checked and repaired; a painting with its marks: its
    // occluders and blocked ground), then its story markers. Without the editor (tools/build_scenes). `only`: these
    // places (ids or names), empty: all. Each place's line said through `say`. The places it built.
    std::vector<std::string> RebuildScenes(const std::vector<std::string>& only, const std::function<void(const std::string&)>& say);

    // Placements (34.11, Placements.cpp): every building of the built places found (position, kind, door, its drawn interior),
    // whose home and work each one is, and where each character and story prop stands and is over time, decided in one AI
    // call (the rules without the AI, and for what it leaves out), kept in planner/placements.json (entries the user locked
    // stay); then the characters and props placed again by it and its whereabouts requirements made again. Queued: Pump does
    // it. `why`: what started it (said in the log). Why not, or "".
    std::string ReplaceCharacters(const std::string& why = "Replace Characters");
    // A Build tab's revision in the user's words (placements, requirements, cast, gameplay, quests): its entries as
    // documents, the AI finds which the words are about and edits those; applied (the tab's own way: placed again, saved),
    // in the tab's history (planner/build-revisions.json). Empty: asked; else why not.
    std::string ReviseTab(const std::string& tab, const std::string& text);
    // A Build tab's revision taken back (each edit's value put back as it was, an item it added taken out), the tab's
    // own way (placed again, saved). Empty: done; else why not.
    std::string RevertTabRevision(const std::string& tab, const std::string& id);
    bool RevisionWaiting() const { return !reviseAnswers_.empty() || !visualAnswers_.empty() || !objectAnswers_.empty(); }
    // Build ▸ Visuals (⑧'s panels made): each panel's images generated (ui/art/), then its document written as designed
    // (ui/screens/<id>.rml, in the project's screens manifest). "all", or one panel's id. Empty: under way; else why not.
    std::string BuildVisuals(const std::string& panel);
    // Build ▸ Props & objects: each described thing made in its scene (its entity, a SmartObject for characters' days,
    // a Reaction for each use, each state's picture as a prefab its Reactions become, its users' animations and their
    // quest uses as requirements). "all" or one id. Empty: done (states' pictures still drawing); else why not.
    std::string BuildObjects(const std::string& id);
    // Build ▸ Small scenes: each written as an ambient cutscene (cutscenes/small-<id>.json) and a player-near trigger in
    // its place, for each quest stage it plays in. "all" or one id.
    std::string BuildSmallScenes(const std::string& id);
    // The same, now, by the rules alone (no AI: build_scenes); each line said through `say`.
    std::string ReplaceCharactersNow(const std::function<void(const std::string&)>& say);
    // What happened to the placements since asked last ({progress} while working, {done, placements, error?} once): the
    // editor posts it to the AI Planner's Placements tab. Null: nothing new.
    nlohmann::json TakePlacementNews();
    // A character moved to another scene by an edit: its binding there (bindings: the scene it lives in).
    void PlacementMoved(const std::string& character, const std::string& scene);

    // ⑬ Validate (development_plan.md 33, BuildCheck.cpp): the built game against its plan. Each story scene traced to what
    // was built for it; rules, then the AI scene by scene (planner/buildcheck.json); then the findings fixed: edits of the
    // plan's documents (⑪ again), requests for the AI panel (as ⑫'s steps), the playtest and result again. Why not, or "".
    std::string CheckBuild();
    std::string FixFindings(const nlohmann::json& ids); // their ids; empty: every one with a fix
    // ⑬ Test a story scene (development_plan.md 33.3-33.4, SceneTest.cpp). Where the game starts for it: {start (the
    // start scene), scene (its place's), marker, quests [{id, stage} | {id, complete}]}, or {error}.
    nlohmann::json TestStart(const std::string& storyScene) const;
    // Played headless by an autopilot (to each objective of its stage, doing it), then judged by the AI against the
    // scene; what didn't happen joins Validate's findings. Queued; Pump plays it. Why not, or "".
    std::string TestScene(const std::string& storyScene);
    nlohmann::json RunSceneTest(const std::string& storyScene); // the autopilot's run: its steps, events, errors, the quest's stage
    // The autopilot for a story scene (SceneAutopilot.h), to drive a game a frame at a time; nullptr and why when it can't.
    std::unique_ptr<SceneAutopilot> MakeAutopilot(const std::string& storyScene, std::string* why = nullptr) const;
    // ⑬ Test, watched: the scene's test marked as being played in the editor's Play; its log, once played, to the AI judge.
    std::string WatchScene(const std::string& storyScene);
    // The story's written scenes before this one, in the story's order (Play from here plays them first).
    std::vector<std::string> ScenesBefore(const std::string& storyScene) const;
    // Requirements (Requirements.cpp, 34.1): what the plan says must be true in the game, by rule then the AI scene by
    // scene; planner/requirements.json.
    std::string MakeRequirements();
    // 34.2: each requirement's implementation items planned again (the requirements kept): by rule, then the AI scene by
    // scene for the ones it read from the scenes.
    std::string PlanImplementations();
    // ⑫ Implement (Implement.cpp, 34.3-34.4): the assets' art approved (or skipped: they keep what the places show); every
    // requirement checked now (the files, and what the scenes' tests recorded); a failed one's items built again, told
    // what failed ("*": every failed one). Why not, or "".
    std::string ApproveAssetArt(bool draw);
    std::string CheckRequirementsNow();
    std::string FixRequirements(const std::string& id);
    // The implementation items not built yet built now (once the game is built: after Plan the implementations).
    std::string ImplementNow();
    void SubmitTestLog(const std::string& storyScene, const nlohmann::json& log);

    nlohmann::json State() const;
    nlohmann::json Card() const;

private:
    struct Row {
        std::string place, scene, buildType, prompt, mode = "automatic", status = "waiting", build, error;
        std::vector<std::string> dependsOn;
        nlohmann::json brief = nlohmann::json::object();
        nlohmann::json checks = nlohmann::json::object();
        int steps = 0;      // drive steps taken (a stuck build is stopped)
        std::string lastStage;
        bool inEditor = false; // handed to the editor's Build panel: waiting for it to finish
        bool resumeInEditor = false; // reopened past its plan: handed back to the Build panel to finish
        std::vector<std::string> sections; // an open world's: the places that are its sections (built in its scene)
        bool paintApproved = false;    // painting it (the Paint method) was approved
        bool awaitingApproval = false; // planned: waits for its painting to be approved
    };
    bool AutoApprove() const; // the Brief's plan.building is automatic: no asking before paid AI work
    bool artApproved_ = false;
    // A project's characters kept as they're drawn and animated (buildstate "keepArt", set by hand): ⑪ puts each one's
    // sheet back on its prefab with the clips it has, and draws nothing new (what wasn't drawn keeps its placeholder).
    bool keepArt_ = false;
    // The requirements made again, or a tab's items built, while the build is stopped: only they go on; it stops after.
    bool reqsOnly_ = false;
    std::string subStep_; // where a Build tab's step is ("" as it begins): what it has done already
    // The story's markers (its anchors, where one arrives, the player in the first scene) on a built
    // place's scene; the number of anchors that had no part showing them.
    int AddStoryMarkers(Registry& registry, const Row& row, const nlohmann::json& solved, int originX, int originY);
    // The start scene's player: MeleeAttack only when the plan has combat (interact is the default action).
    Entity CreatePlanPlayer(Registry& registry, float x, float y);
    // The editor finished a place: its scene gets its markers, and the row is built.
    void FinishInEditor(Row& row);
    // An open world's sections as one row, the world (its Brief made from the plan); what was inside one waits for it.
    void WorldRow(const StoryGraph& g, std::vector<Row>& rows);

    struct Job {
        std::string id, kind, subject, status = "queued", error;
        int attempts = 0;
        nlohmann::json problems = nlohmann::json::array();
    };

    Host host_;
    PlannerStore store_;
    std::string stage_, status_, message_, mode_;
    std::vector<Row> rows_;
    std::deque<Job> jobs_;
    std::map<int, std::string> pending_;            // request id -> "job:<id>" | "build:<place>"
    std::map<std::string, std::unique_ptr<PlaceBuild>> builds_; // by place
    std::map<std::string, std::vector<std::pair<int, nlohmann::json>>> buildAnswers_; // answers waiting for Pump
    std::vector<std::pair<std::string, nlohmann::json>> jobAnswers_;                    // job id, answer
    nlohmann::json bindings_ = nlohmann::json::object();
    nlohmann::json packages_ = nlohmann::json::object(); // ⑪: the characters' AI packages, written in ⑫
    std::string populateAt_; // ⑪: what it was doing (a failure says where)
    bool placesApproved_ = false; // every place built: the user said go on (Continue) to ⑩ connecting them and ⑪ on
    bool placeAgain_ = false;     // Continue with quest roles not found: ⑪ again (PlaceCharactersAgain) on the next Pump
    bool gatewaysChecked_ = false;
    bool legacyFeatures_ = false; // ⑫ as before 34.3: the systems' features alone, not the requirements implemented // this session: the ways through not shown yet queued (a game built before them)
    nlohmann::json files_ = nlohmann::json::object();   // path -> provenance
    nlohmann::json playtest_ = nlohmann::json::object();
    nlohmann::json features_ = nlohmann::json::array(); // the plan's gameplay features, as they're built
    nlohmann::json art_ = nlohmann::json::array();      // ⑪b: each character drawn and animated (32.6)
    std::vector<std::pair<int, nlohmann::json>> artAnswers_;
    int nextJob_ = 1;

    void Save();
    // What was waiting on the AI when its answer was lost (the editor closed, the AI service stopped), with nothing
    // asked any more: each made ready to go again from where it was (a drawing queued, a panel or prop paused, a
    // reading asked again). RestartPaused (Continue) asks again for what paused.
    void ResetInFlight();
    void RestartPaused();
    // The AI service gone mid-build: everything asked dropped, the build stopped, its work kept where it was.
    void ServiceLost();
    void Load();
    Row* FindRow(const std::string& place);
    std::string StartScene() const;
    PlaceBuild::Host BuildHost(const std::string& place);

    // ⑩
    void MakeBuildPlan();
    bool DriveBuilds();
    bool DriveRow(Row& row);
    std::string BuildScene(Row& row, PlaceBuild& build);
    std::string PlaceholderScene(Row& row);
    void ConnectScenes();
    // ⑪
    void Populate();
    // One of each character per scene, and each only where ⑪ put it (its home; the story moves it elsewhere): copies the AI
    // panel or a run again added go. `scene`: that scene only, else every built one. How many went.
    int KeepOneOfEach(const std::string& scene = {});
    // What ⑪ animates a character doing, from the plan alone (its class's fighting, whether it fights): what the
    // requirements ask of it and what its art queues.
    nlohmann::json PlanActions(const std::string& who) const;
    // Each drawn character's sheet onto its prefab (or the player): after ⑪ makes them (the art comes first now).
    void ApplyDrawnArt();
    // The requirements whose checks already hold, verified (never failed here: what isn't built yet is ⑫'s): after each
    // step of ⑪ that meets some (the characters drawn, the quests bound, the placements).
    void MarkMetRequirements();
    // The Build tab an implementation item belongs to (each tab builds its own: characters, objects, quests, placements,
    // behaviour, scenes, systems, visuals), from its requirement's kind and how it's built.
    std::string TabOf(const nlohmann::json& item, const nlohmann::json& req) const;
    // A tab's own implementation items built (once per requirements made): true while they're being built; "" every item
    // left (the last pass).
    bool RunTab(const std::string& tab);
    // `part`: "characters" (and the enemies' kinds), "objects" (the items' icons, the ways through) or "interface" (the
    // UI panel's picture): each queued in its own Build tab.
    void QueueArt(bool others = false, const std::string& part = "characters"); // ⑪b: who is drawn, and what each is animated doing; `others`: added to it, the characters not in it yet
    bool DriveArt();  // their requests, one at a time each, two at once; then onto their prefabs
    // ⑪c the ways through (development_plan.md 32.12): a painted place's gateways onto the doors and gates the vision model
    // finds in its painting; a gateway nothing shows, art drawn for it and stood on it. Added to the art; how many.
    int QueueGateways();
    void QueueGatewayArt(const Row& from, const Row& to);
    void ApplyEntrances(nlohmann::json& art, const nlohmann::json& answer); // the doors found: the gateways onto them
    // An interior's or a dungeon's way back out (the vision model's look at its painting, /places/find-way-out): the
    // gateway onto the exit one can walk to (of several doors, an accessible one), its arrival marker beside it.
    void ApplyExit(nlohmann::json& art, const nlohmann::json& answer);
    std::string WayOutOf(const std::string& scene, const std::vector<std::string>& gateways) const; // the scene its way out leads to
    std::string RowPlaceOfScene(const std::string& scene) const;
    // A built place's buildings and things in its scene's cells: its plan's; a freely painted place's buildings where the
    // painting has them (structures.json, found with its doors), its land where the plan has it.
    std::vector<PlannedThing> ThingsOf(const Row& row);
    // A freely painted place's cells hidden behind its painted buildings (behind their ground, under their picture), in
    // its scene's cells: nobody is placed to stand there.
    std::set<std::pair<int, int>> HiddenCells(const Row& row);
    std::vector<PlannedArea> AreasOf(const Row& row);
    void SettleAreaAnchors(); // a painted place's story anchors of a square moved to where the painting has it
    nlohmann::json VisualsState() const; // planner/visuals-build.json: {panels: {id: {status, images, file, notes, error}}}
    void SaveVisualsState(const nlohmann::json& state) const;
    void NextVisualStep(const std::string& panel);
    void ApplyVisualAnswer(const std::string& key, const nlohmann::json& body);
    nlohmann::json TabDocuments(const std::string& tab) const;
    void ApplyTabRevision(const std::string& key, const nlohmann::json& body);
    // Placements ▸ Paths: each built outdoor place's roads as paths (road-<id>) and a "to-the-<side>" marker where one leaves
    // the scene: {roads, exits}, kept in the placements too.
    nlohmann::json LayRoads();
    // The gateways between two outdoor places: {scene, to, toPlace, toName, toLine, x, y}.
    nlohmann::json OutdoorWays() const;
    // The AI's choice of a way out for each such gateway ({scene, to, exit, why}) put into gateways.json; returns the
    // placements' gateways (the old ones not decided again kept).
    nlohmann::json SettleGateways(const nlohmann::json& decided, const nlohmann::json& exits, const nlohmann::json& old);
    void SettleGateAnchors(); // the story anchors by a moved gateway moved with it
    // Where an AI package of a placed character runs in its scene (34.11): {marker, label}. Sleep at their bed (their
    // home's door, or inside it), a spot the timeline names that is a marker there, work at their workplace, else where
    // they stand. hasMarker: whether that scene has a marker of that name.
    nlohmann::json PackageSpot(const nlohmann::json& placed, const std::string& tmpl, const std::string& spot,
                               const std::function<bool(const std::string&)>& hasMarker) const;
    void AnnotateCastPackages(); // Build ▸ Cast: each package of each character-state requirement with where it runs
    // ⑫
    void QueueWriting();
    bool DriveJobs();
    std::vector<std::string> HandleJob(Job& job, const nlohmann::json& answer);
    void WriteFiles();
    void WriteQuests(); // the quests' files from their designs and bindings_
    // These quests' designs (planner docs) into their files as they are now (⑫ may have changed them): each role's fill,
    // and the stage effects they haven't got. A fix of the plan (⑬ Validate) goes into the game this way.
    struct QuestMerge {
        std::set<std::string> aliases; // the roles a fix binds: their fills, and their objectives where a stage lacks them
        std::set<int> stages;          // the stages (their place in the design) whose effects a fix changed
    };
    void MergeQuests(const std::map<std::string, QuestMerge>& docs);
    // A quest design's stages' story staging: {"<index>": [move_actor ...]}, its scene's cast brought where it happens.
    nlohmann::json StoryStaging(const nlohmann::json& design) const;
    // Where a scene's participant is met when the scene happens at `location`: the story's place, unless the places say
    // they work in a building inside it (a merchant in their shop: its place lists them, or it's their home and they
    // sell): then that building. The story, the characters and the places read together.
    std::string MetAt(const std::string& who, const std::string& location) const;
    // The cast timeline (planner ⑥b, CastTimeline.cpp): each character's state before the first stage and what each
    // quest stage changes; its requirements (the Cast tab), the stage effects it stages, its checks.
    nlohmann::json Timeline() const;
    nlohmann::json TimelineCutscenes() const; // the cutscenes it plans: stage, place, trigger, cast, beats
    // Two quests holding one character (development_plan.md 34.7, "when quests overlap"): the change's priority (story
    // before normal before ambient), then the main quest before the side quests, then the side quests in their order. Lower
    // ranks first. Packages, dialogue and cutscenes are all kept (packages stacked in this order, each while its quest is
    // at its stages); where they are and how they behave is the highest-ranked quest's.
    int TimelineRank(const std::string& quest, const std::string& priority) const;
    std::string TimelineTopQuest(const std::string& who, const char* part) const; // the quest whose change of it wins
    // Its packages, quest by quest and stage span by stage span, in rank order: [{id, plan: {template, place, schedule},
    // quest, stages, last}] (Populate makes a record of each, conditioned on its quest being at those stages).
    nlohmann::json TimelinePackagePlan(const std::string& who) const;
    nlohmann::json TimelineStateAt(const std::string& who, const std::string& quest, int stage) const;
    nlohmann::json TimelineRequirements() const;
    nlohmann::json TimelineStaging(const nlohmann::json& design) const;
    nlohmann::json TimelineStatic(const nlohmann::json& check);
    void TimelinePlayed(const nlohmann::json& check, const nlohmann::json& record, nlohmann::json& result);
    // Placements (34.11, Placements.cpp)
    nlohmann::json placements_ = nlohmann::json::object(); // planner/placements.json as ⑪ uses it
    std::string placeStep_;   // "" | "decide" (ask the AI next Pump) | "waiting" (its answer) | "apply"
    std::string placeWhy_;    // what started it
    std::string placeStatus_; // the status to go back to once placed ("done": the game was built)
    std::vector<nlohmann::json> placeAnswers_; // the AI's decision
    std::vector<std::pair<std::string, nlohmann::json>> reviseAnswers_; // a Build tab's revision: "<tab>:<id>" -> the AI's edits
    std::vector<std::pair<std::string, nlohmann::json>> visualAnswers_; // Build ▸ Visuals: "img:<panel>:<image>" or "rml:<panel>" -> the answer
    std::vector<std::pair<std::string, nlohmann::json>> objectAnswers_; // Build ▸ Props & objects: "<object>:<state>" -> its picture
    nlohmann::json WorldState() const; // planner/world-build.json: {objects: {id: {...}}, scenes: {id: {...}}}
    void SaveWorldState(const nlohmann::json& state) const;
    void ApplyObjectState(const std::string& key, const nlohmann::json& body);
    // A clip a character (or the player) needs, added to its art (drawn as the art step draws the rest): false when
    // nothing of it is drawn to animate.
    bool QueueCharacterClip(const std::string& subject, const std::string& action);
    // The clips the art drew under their own names (the animator's: "repair" may come back "repair", "hammering" as
    // "hammering"): written into the Reactions and small scenes that play them, each animation's status kept.
    void ReconcileClips();
    void ApplyObjectAnimation(const std::string& oid, const std::string& clip, const nlohmann::json& body);
    nlohmann::json placeNews_; // for TakePlacementNews
    bool placeAskedForPopulate_ = false; // ⑪: the AI asked once before the characters are first placed
    std::string RequestPlacements(const std::string& why); // the decision asked again (queued)
    bool DrivePlacements();                                 // its steps, from Pump
    bool PaintedWithoutStructures();                        // a freely painted place whose buildings aren't found yet
    nlohmann::json PlacementBuildings();                    // every building of the built places, its interior and door
    nlohmann::json PlacementInput(const nlohmann::json& buildings); // what the AI decides from
    nlohmann::json RulePlacements(const nlohmann::json& buildings); // the decision by rule
    nlohmann::json PlacementImplementations(const nlohmann::json& req) const; // a whereabouts requirement's items: {how, does, by}
    // The AI's decision checked against the real ids (what's wrong or missing: the rule's), the locked entries kept.
    nlohmann::json MergePlacements(const nlohmann::json& decided, const nlohmann::json& rule, const nlohmann::json& old, const nlohmann::json& buildings,
                                   std::vector<std::string>& notes) const;
    void SavePlacements() const;
    // ⑪: the decision's spots in the scenes as they are (a building's door, a cell inside its interior, a marker, near one),
    // each whereabouts given a marker of its own where it has none. In the scenes `sceneOf` loads (Populate's).
    void ResolvePlacements(const std::function<Registry*(const std::string& scene)>& sceneOf);
    const nlohmann::json* PlacementOf(const std::string& kind, const std::string& id) const;
    // A character's whereabouts while a quest is at a stage (the latest that has begun by then): its scene and marker; null.
    nlohmann::json WhereaboutsAt(const std::string& who, const std::string& quest, int stage) const;
    nlohmann::json PlacementRequirements() const; // one "whereabouts" requirement per character (and prop) per span
    nlohmann::json PlacementStatic(const nlohmann::json& check);
    void PlacementPlayed(const nlohmann::json& check, const nlohmann::json& record, nlohmann::json& result);
    void ImplementWhereabouts(nlohmann::json& item, nlohmann::json& req); // its stage effect written (move_actor), else the AI panel
    void CheckPlacementRequirements();   // the whereabouts that hold already, verified
    void RefreshPlacementRequirements(); // the requirements' whereabouts items made again (the built ones whose text didn't change kept)
    // The game's quests' objectives, {quest: {"<index>": [objectives]}}; after a ⑫ step, what it added made optional and
    // what it took out or changed put back (the stages ask what the story does).
    nlohmann::json QuestAsks() const;
    void KeepQuestAsks(const nlohmann::json& before, const std::string& by);
    // A scene's markers one can't walk to from where one arrives there.
    std::set<std::string> MarkersOutOfReach(const std::string& scene) const;
    // Story anchors' markers placed again in their scene: in their room (RoomOfAnchor), on a cell one can walk to from
    // where one arrives. Why not, or "".
    std::string PlaceAnchorsAgain(const std::string& scene, const std::set<std::string>& ids);
    void WriteDialogue(const std::string& character, const nlohmann::json& dialogue, const std::string& madeBy);
    nlohmann::json FallbackDialogue(const std::string& character) const;
    bool DriveFeatures(); // each gameplay feature in turn
    void Playtest();
    // ⑬
    void WriteResult();
    // ⑬ Validate (BuildCheck.cpp)
    nlohmann::json check_ = nlohmann::json::object();       // planner/buildcheck.json: {status, trace, findings, scenes}
    std::vector<std::pair<int, nlohmann::json>> checkAnswers_; // a scene's row, the AI's findings
    nlohmann::json TraceScenes() const;                      // each story scene: what was built for it
    nlohmann::json RuleFindings(const nlohmann::json& trace) const;
    bool DriveCheck();
    nlohmann::json CheckView() const;
    void SaveCheck() const;
    void LoadCheck();
    std::vector<std::pair<std::string, nlohmann::json>> testAnswers_; // a story scene, the AI's verdict on its test
    bool TestsPending() const;
    // Requirements (34.1)
    nlohmann::json reqs_ = nlohmann::json::object();       // planner/requirements.json: {status, items, scenes}
    std::vector<std::pair<std::string, nlohmann::json>> reqAnswers_; // "s<row>" a scene's requirements, "assets", "i<row>" a scene's implementations
    nlohmann::json RuleRequirements() const;
    nlohmann::json RuleImplementations() const;     // the items the build makes by rule, for every requirement but the AI's from the scenes
    void AskAssets();                               // the visual requirements merged and their looks written (one AI call)
    void MergeAssets(const nlohmann::json& body);
    void StartImplementations();
    nlohmann::json SceneFacts(const std::string& sceneFile) const; // a built scene's markers and gateways
    bool DriveRequirements();
    void SaveRequirements() const;
    void LoadRequirements();
    // ⑫ Implement (Implement.cpp)
    std::vector<std::pair<std::string, nlohmann::json>> implAnswers_; // an item's id, the AI's cutscene
    std::map<std::string, std::unique_ptr<Registry>> checkScenes_;    // scenes read for the checks (while checking)
    void StartImplement(bool fresh);
    bool DriveImplement();
    void ImplementItem(nlohmann::json& item, nlohmann::json& req);
    void ImplementArt(nlohmann::json& item, nlohmann::json& req);
    void ImplementAnimation(nlohmann::json& item, nlohmann::json& req);
    // What an animation item animates and how ({who, action, name}; {add: a thing's name} when nothing drawn is it yet).
    nlohmann::json AnimationOf(const nlohmann::json& item, const nlohmann::json& req) const;
    bool AddAnimatedAssets(); // the assets animation items asked for: their requirements and items (drawn, placed first)
    void PlaceAsset(nlohmann::json& item, nlohmann::json& asset, const nlohmann::json& req);
    void AskCutscene(nlohmann::json& item, const nlohmann::json& req);
    void WriteCutsceneItem(nlohmann::json& item, const nlohmann::json& req, nlohmann::json cutscene);
    // A planned cutscene's start in words ("when the player comes out of Reed Apothecary into Bellwether, while q-main
    // stage 20 is on"), as Life ▸ Cutscenes plans it.
    std::string CutsceneStartText(const nlohmann::json& planned) const;
    // Its start made in the game exactly as planned: the quest stage's effect, a trigger at the spot or the building's
    // way out, or a rule on the conversation's end, the defeat, the use. Empty: made (what in `made`, said in `said`);
    // else why not.
    std::string PlaceCutsceneStart(const std::string& id, const nlohmann::json& planned, const std::string& sceneFile,
                                   nlohmann::json& made, std::string& said);
    void ObjectAnimated(nlohmann::json& item, const nlohmann::json& req, const nlohmann::json& body); // a thing's animation drawn
    void AnimateObjects(const nlohmann::json& asset);                                                // its sheet on its placed copies
    void NameThePlace(nlohmann::json& item, const nlohmann::json& req);
    void ToPanel(nlohmann::json& item, const nlohmann::json& req, const std::string& problem);
    nlohmann::json* AssetFor(const nlohmann::json& req);
    bool NeedsDrawing(const nlohmann::json& item, const nlohmann::json& req) const;
    nlohmann::json StaticChecks(const nlohmann::json& req);
    void PlayedChecks(const nlohmann::json& req, nlohmann::json& checks);
    void CheckRequirements();
    bool DriveTests();

    void Next(const std::string& stage);
    void Record(const std::string& kind, const std::string& subject, const std::string& chosen, const std::string& reason = {});
};

// The planner's quest design as the engine's quest file (quests v2): stages with objectives
// whose targets are the quest's aliases, the aliases filled from the bindings.
// `sceneOf`: a place id's scene (for stage effects that move characters there). `staging`: {"<stage index>": [effects]}
// the story adds as each stage begins (its scene's cast brought there, sent home after).
nlohmann::json EngineQuestFromDesign(const nlohmann::json& design, const nlohmann::json& bindings,
                                     const std::function<std::string(const std::string&)>& sceneOf = {},
                                     const nlohmann::json& staging = nlohmann::json());

} // namespace aige
