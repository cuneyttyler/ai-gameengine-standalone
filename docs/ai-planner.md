# The AI Planner — designing and building a whole game

Status: **draft for review (2026-10-21)**. Phases 21–23 in `development_plan.md`. It comes
after the engine's gaps are filled (Phases 12–20, `engine-gaps.md`), because the Planner
can only design what the engine can run.

| Document | Authority for |
|---|---|
| **`ai-planner.md`** (this) | why, the research, the procedure, the Planner panel, multi-scene building, quest building, robustness and audit |
| `planner-schema.md` | the Game Brief (the input schema and its confirmation window), the Planner's documents and their files, the decision record |
| `story-ontology.md` | GSO and the story knowledge graph: what a story, a character, a quest and a place are; the consistency rules |
| `ai-packages.md`, `difficulty.md`, `game-ui.md`, `magic.md`, `effects-animation.md`, `physics.md` | the systems the Planner configures, each with a section on how the Planner uses it |
| `place-interaction.md`, `place-schema.md` | the Build panel, which the Planner drives for each place; the AI-call protocol it reuses |
| `game-types.md` | the kinds of game, which decide which stages apply |

---

## 0. Approach (article form)

Language models can write a premise, but a game is more than a story: it is characters
with behaviour, places with layouts, quests bound to things in those places, systems with
numbers, and an interface. Work on long-form generation agrees that a **hierarchy** helps:
Dramatron writes a logline, then characters, then a plot of scenes with locations, then
location descriptions, then dialogue [1]; Re3 and DOC plan an outline before writing and
revise against it [2, 3]; *From World-Gen to Quest-Line* separates campaign planning from
quest expansion and conditions each stage on the strict output of the earlier ones [4].
A second strand keeps a **memory** of what has been written: DOME and SCORE store the
story as a temporal knowledge graph and retrieve from it, and report fewer contradictions
than outline-only pipelines [5, 6]. A third binds stories to **world state**: StoryVerse
instantiates abstract acts against the current simulation [7]; game quest generators since
Doran and Parberry compose quests from NPC motivations, strategies and atomic actions [8];
and the Creation Kit's quests refer to *aliases*, roles filled by world objects when the
quest starts, which is what lets Bethesda's Radiant quests run in any town [9].

The AI Planner combines these strands with the place pipeline of Phase 7. A request
becomes a confirmed **Game Brief**; the Brief becomes an **outline of the three pillars**
(story, characters, places) developed together, not in sequence; the story is written
**scene by scene** against a **story ontology** (GSO) and a **knowledge graph** that holds
who is alive, where, knowing what, at every scene; characters, quests and places are then
detailed from it; systems, difficulty and the interface are designed to fit; everything is
**validated** across documents; and then the game is **built**: every place through the
Build panel, every quest bound to the objects those builds produced, every file written
with a link to the decision that made it.

Three properties carry over from Phase 7 and matter more at this scale:
- **The AI decides meaning, solvers decide geometry.** The Planner never places a
  character at a coordinate. It chooses among candidates the place build already knows
  ("the counter of the inn's tavern hall"), and the engine places it.
- **Everything is a typed, checked document.** Each stage writes JSON with a schema, and
  validators run before anything moves on.
- **Every decision is recorded and explainable.** One question — *why is this here?* —
  can be answered for any scene, character, quest, place or built entity.

**References**
1. P. Mirowski, K. Mathewson et al. *Co-Writing Screenplays and Theatre Scripts with Language Models: An Evaluation by Industry Professionals* (Dramatron). CHI 2023.
2. K. Yang, Y. Tian, N. Peng, D. Klein. *Re3: Generating Longer Stories With Recursive Reprompting and Revision.* EMNLP 2022.
3. K. Yang, D. Klein, N. Peng, Y. Tian. *DOC: Improving Long Story Coherence With Detailed Outline Control.* ACL 2023.
4. D. Borawski et al. *From World-Gen to Quest-Line: A Dependency-Driven Prompt Pipeline for Coherent RPG Generation.* arXiv:2604.25482, 2026.
5. *DOME: Dynamic Hierarchical Outlining with Memory-Enhancement for Long-Form Story Generation.* NAACL 2025.
6. *SCORE: Story Coherence and Retrieval Enhancement for AI Narratives.* arXiv:2503.23512, 2025.
7. Y. Wang, Q. Zhou, D. Ledo. *StoryVerse: Towards Co-authoring Dynamic Plot with LLM-based Character Simulation via Narrative Planning.* FDG 2024.
8. J. Doran, I. Parberry. *A Prototype Quest Generator Based on a Structural Analysis of Quests from Four MMORPGs.* PCG Workshop 2011.
9. Bethesda Softworks. *Creation Kit* documentation: Quests, Quest Aliases, Story Manager, AI Packages.
10. M. Riedl, R. M. Young. *Narrative Planning: Balancing Plot and Character.* JAIR 39, 2010.
11. H. Huot et al. *Agents' Room: Narrative Generation through Multi-step Collaboration.* ICLR 2025.
12. J. S. Park et al. *Generative Agents: Interactive Simulacra of Human Behavior.* UIST 2023.
13. P. Ammanabrolu et al. *Bringing Stories Alive: Generating Interactive Fiction Worlds.* AIIDE 2020.

Also used: the narratology and ontologies in `story-ontology.md` §2; the Phase 7 sources in
`ai-building.md`; articy:draft's entity templates and flow; Ludo.ai's game-concept tools;
build systems (Make, Bazel) for the dependency graph (§10.3); durable workflow engines
(Temporal, the saga pattern) for resumable jobs (§10.5).

## 1. Goal

One request — *"a short dark-fantasy action RPG about a disgraced warden recovering a
stolen amulet, three towns and a dungeon, about four hours"* — becomes a playable game:
a story with its scenes, characters with biographies and behaviour, quests that can be
completed, places built and connected, items and abilities, difficulty levels, menus and a
HUD. Fully automatically if wanted; step by step under the user's control if not. The
Planner must:

1. understand **what game was asked for**, and confirm it with the user (Game Brief);
2. develop **story, characters and places together**, consistent at every point;
3. design the **systems** the game needs: quests, items, abilities, AI, difficulty, UI;
4. **validate** the whole design before building;
5. **build** every place, connect them, and **bind the quests** to what was built;
6. **verify** the game is playable (quests completable, places reachable);
7. **record every decision**, survive errors, and let the user change any part later.

## 2. Proposed decisions

These mirror Phase 7's agreed decisions; each is to be confirmed in review.

| Question | Proposal |
|---|---|
| Confirmation | The Game Brief window opens for every plan (as the Build panel's does), with a **Difficulty** tab and a **Visuals** tab. |
| Autonomy | Per stage: **ask** (the user approves each document), **review** (the Planner continues; the user reviews at the stage's end), **auto** (continues; findings reported). Defaults: ask for Concept and Outline, review for the rest. Building has its own choice: manual or automatic per scene (§8). |
| Scope | From a single-screen game (a puzzle game) to a region-scale RPG of 30+ places and 100+ quests. |
| Genres | Any in `game-types.md` that the engine supports at the time. Genre profiles switch stages on and off (§12). |
| Editing | Every Planner document stays editable after the plan; a change marks what depends on it as stale and regenerates only that (§10.3). |
| Model budget | No limit by default (as Phase 7); a per-plan ceiling can be set. |
| Story ontology | GSO (`story-ontology.md`), built before the Planner; every story step reads and writes the knowledge graph. |
| Quest generation | Three moments: the **main quest's skeleton alongside the story**, **side quests after the story and characters**, **binding after the places are built** (§5.5, §9). |

## 3. What we take from existing work

| Source | What we take |
|---|---|
| Dramatron [1] | the hierarchy: logline → characters → scenes with locations → location descriptions → dialogue; each level conditioned on the ones above |
| Re3, DOC [2, 3] | a detailed outline before writing; revise against the outline; a controller that checks each passage follows its outline item |
| World-Gen to Quest-Line [4] | strict JSON between stages, explicit data flow; campaign plan apart from quest expansion |
| DOME, SCORE [5, 6] | a temporal knowledge graph of what has been written, retrieved for each new part |
| StoryVerse [7] | abstract acts instantiated against the world state: our scenes are written against the graph's state at that point |
| Doran & Parberry [8] | quests from motivations → strategies → atomic actions |
| Creation Kit [9] | stage-indexed quests; aliases; Story Manager events; AI packages; scenes for scripted conversations |
| Riedl & Young [10] | characters act from their own intentions; every kernel action is motivated (rule R-C2) |
| Agents' Room [11] | specialised roles (planner, writer, critic) with a shared scratchpad; we use roles per stage, the graph as the scratchpad |
| Generative Agents [12] | characters with memory, plans and reflection, used here at design time (biographies, schedules), not as runtime simulation |
| Bringing Stories Alive [13] | a knowledge graph of the world extracted and completed before generating the game from it |
| Phase 7 (ours) | Brief → confirm with three readings; think, then answer in a strict schema, validate, repair once; verified examples; "Why?" |

## 4. The procedure

```
Request
  │
  ▼
① Concept ──► ② Outline ──► ③ Story ⇄ characters & places grow ──► ④ Characters ──► ⑤ Quests
 Game Brief     3 pillars       scene by scene, against the graph       detail          main + side
  │             together                                                                    │
  │                                                                                         ▼
  │           ⑧ Visuals ◄── ⑦ Systems ◄── ⑥ Places (descriptions → Briefs) ◄────────────────┘
  │               │
  │               ▼
  │           ⑨ Validate (whole design) ──► design baseline
  │               │
  │               ▼
  └─ difficulty ► ⑩ Build places (Build panel per place; manual or automatic)
                  │
                  ▼
                ⑪ Populate & bind quests ──► ⑫ Write & playtest ──► ⑬ Result
                                                                  (later: Animations)
```

**Not a waterfall.** The arrows are the order of *focus*, not of exclusive ownership:
- While the story is written, it **creates** characters and places as stubs in the graph
  (a name, a role, a line). The Characters and Places stages detail them later.
- While characters are detailed, a biography may **contradict** the story or suggest a
  scene. These come back as **story issues** for the Story stage (an edit to a scene, or a
  new satellite scene), not as silent changes.
- While places are described, a layout may be impossible as written ("a harbour in a
  desert"). The issue goes back to the scene or the place.
- Every document records **what it depends on** (§10.3). A change anywhere marks its
  dependents stale and the Planner offers to regenerate exactly those.

The stages are the same for every game; which ones run, and how much they do, depends on
the genre profile (§12).

## 5. The stages

Each stage reads documents written before it, writes its own, and ends with validation.
Every AI call follows the protocol of `place-interaction.md` Part B: think in free text,
answer in the strict schema, validate, repair once with the exact violations, then report
a problem rather than guess.

### 5.1 ① Concept — the Game Brief

What the game is. Same shape as the Build panel's Interpret and Confirm:
- The request is read **three times** in parallel (literal, typical, generous) into three
  Game Brief drafts, each with confidences, assumptions and decisions.
- They're validated against the Brief schema and **merged** field by field; disagreements
  are marked *differ*, low confidence *guessed* (`place-interaction.md` B7).
- The **confirmation window** is generated from the schema (`planner-schema.md` §2.12). Tabs:

  | Tab | Holds |
  |---|---|
  | **Game** | title, logline, genre, perspective and grid, camera, scope (play time, places, quests), audience and rating, art style (from the project's art or a choice) |
  | **Story** | setting, era, tone, themes, structure template, branching pattern, endings, player character (fixed or created) |
  | **Characters** | cast size by importance, companions, factions, how much behaviour (simple, scheduled, simulated) |
  | **Places** | world shape (one place, a hub, a linear chain, an open region), the kinds of place and how many, interiors, dungeons |
  | **Systems** | combat, abilities and magic, inventory, crafting, economy, progression, dialogue, stealth, day and night, schedules; each on or off, with its main options |
  | **Difficulty** | `difficulty.md` §8: levels and names, what scales, assists, adaptive difficulty, level scaling, permadeath |
  | **Visuals** | UI theme (from the library or new), HUD density, which windows, notification style (`game-ui.md` §17) |
  | **Plan** | autonomy per stage, building manual or automatic, model, budget ceiling, asset sources |

- **"To decide"** lists open points with the assumed answer (as in ① Confirm of the Build
  panel). Answers become project defaults (`planner/defaults.json`).
- Output: `planner/brief.json`.

### 5.2 ② Outline — the three pillars together

One step drafts the three pillars **in one context**, so they fit from the start:
- **Story outline:** logline, synopsis (one page), the structure template's acts with one
  paragraph each, storylines (main, and side lines named), endings.
- **Cast list:** each character with name, importance, narrative role(s), gameplay
  role(s), one line. Required roles from the template are filled (R-K1).
- **Place list:** each location with its kind (GBO build type), parent, one line, and the
  **world graph**: how places connect (road, door, stairs, portal) and what gates each
  connection (a quest stage, a key, a level).

The step thinks, answers, is validated (structure and the R-K1, R-P1, R-P2 rules), and
**seeds the knowledge graph**: every character, place and storyline becomes a node; World
Bible entries are created for characters, places and factions with their one-line
summaries. The user reviews the outline (default autonomy: ask) in the Planner panel's
Outline view: three columns, editable, with "Regenerate" per item.

Output: `planner/outline.json`, the graph at revision 1, World Bible stubs.

### 5.3 ③ Story — scene by scene

The story is written from the top down and then **scene by scene in discourse order**:

1. **Acts → chapters.** Each act is split into chapters (a dramatic question each, often
   a region or a play session), with the template's beats assigned to them.
2. **Chapters → scene list.** Each chapter gets its scenes as **scene cards**: goal,
   conflict, outcome, value and charges, mode (cutscene, dialogue, gameplay,
   environmental, discoverable, ambient), kernel or satellite, location, participants.
3. **Scene by scene.** For each scene, in order, one call writes the scene in full:
   - **inputs:** the scene card; the act and chapter summaries; the **world state** at this
     scene from the graph (who is alive, where, relationships, what each participant
     knows, possessions); open threads (unresolved conflicts, unpaid setups, active goals);
     the previous scene in full and earlier ones as summaries; retrieved scenes by meaning
     (`story-ontology.md` §6.5); the ontology slice for the mode and beat;
   - **answer:** the scene's beats; the events it presents; the **facts it asserts and
     retracts** (with intervals); new characters, places and items it needs, as stubs; for
     the main line, the **quest-stage skeleton** it implies (`advancesScene`); a summary;
     a rationale;
   - **checks:** shapes and the continuity rules (R-T1…R-T6) on the facts; a violation
     goes back once.
4. **Act review.** At each act's end, a **critic** call reads the act's summaries and the
   graph and checks structure (R-S*), causality (R-C*) and pacing. Findings become edits
   to scene cards, which regenerate only the affected scenes.

Branching: at a `ChoicePoint`, each option's scenes are written as a branch with its own
facts until a convergence or an ending (`story-ontology.md` §6.3).

**Characters and places grow here.** A new stub (a ferryman the scene needs) is added to
the graph and the World Bible at once, so the next scene can use it. Stubs carry the
scene that introduced them.

Output: `planner/story/` (acts, chapters, scenes), the graph at the stage's revision, the
main quest skeleton.

### 5.4 ④ Characters — detail

Every character in the graph is detailed, major ones first, in parallel within each
importance band:
- **Biography:** backstory (as backstory events in the graph), personality (traits with
  intensities), need, desire, weakness, motivations, goals over time, arc with its
  turning scenes, secrets, how they speak (voice, verbal tics, a sample line).
- **Appearance:** for the Art tool and the asset plan (a description and a palette that
  fits the art style).
- **Relationships:** typed and temporal, consistent both ways (R-K3).
- **Gameplay:** gameplay roles; a **class record** (attributes and level band, from the
  game's progression and `difficulty.md`); equipment and inventory; abilities
  (`magic.md`); faction membership and disposition.
- **Behaviour:** the **AI package** (`ai-packages.md` §9, §13): a stack of packages from the
  library with locations and schedule (field-defined), or a **scripted package** in Lua
  where the behaviour is special (a ghost that appears only when the player holds the
  amulet). Combat style for fighters. Barks.
- **Dialogue topics:** what they can talk about, what they know (from the graph's
  `knows`), and the quests they give.

A character's detail can raise **story issues** (§4). The World Bible entry gets the
biography as its body; the graph gets the structured facts.

Output: `planner/characters/<id>.json`, class and package records (drafts), World Bible
bodies.

### 5.5 ⑤ Quests — main and side

Where quest generation goes, and why:

| Moment | What | Why there |
|---|---|---|
| **During ③ Story** | the **main quest's skeleton**: one stage per kernel gameplay scene, its journal line, which scene it advances | the main quest *is* the playable main storyline; designing it apart from the story would duplicate and drift |
| **⑤ Quests, after ④ Characters** | the main quest in **full** (objectives, aliases, rewards, conditions); **side quests** and faction and companion quests | side quests grow from characters' motivations and relationships (Doran & Parberry), which exist only after ④; and the full story tells which characters are available, alive and where, at each point |
| **⑪ after building** | **binding**: aliases filled by built entities, objects placed, markers set | only then do real places and objects exist (§9) |

The quest design:
1. **Main quest:** each skeleton stage gets objectives (`ObjectiveAction` from the scene's
   mode and beats), aliases (who and what it involves, as fixed entities or queries),
   conditions, journal text, rewards, and a level from the chapter's band.
2. **Side quests:** for each storyline the outline named, and to reach the Brief's count:
   a giver is chosen (a character whose motivation and situation suggest a need), a
   **strategy** for that motivation, and the strategy's **action sequence** turned into
   stages (Doran & Parberry's rewriting: "recover stolen item" → goto, kill or steal,
   take, goto, give). Each side quest attaches to a satellite scene or to a location and
   chapter, so it's reachable when the story makes it so.
3. **Relations:** `requires`, `excludes`, `unlocks` between quests; effects on story
   variables and factions.
4. **Checks:** R-Q1…R-Q7 on the design (completability at the design level: every
   required character alive and reachable at that point; keys before locks; levels in
   band; rewards in the economy's band). They are run again after binding (§9.5).

Each quest also lists the **story anchors** it needs in places ("a hidden spot in the
crypt's last chamber for the amulet", "a bed in the inn for the ambush"). These go into
the place descriptions (§5.6), so the builds make them (§9.2).

Output: `planner/quests/<id>.json` (design; the engine's `quests/<id>.json` is written in ⑫).

### 5.6 ⑥ Places — descriptions, then Briefs

Each location in the graph gets a **place description**: a detailed account of what it
contains and how it's laid out, written from everything the story, characters and quests
need from it:
- **What it is:** kind (GBO build type), size class, purpose, era and culture, mood, who
  lives or works there.
- **What it contains:** zones and their character; named buildings and features; interiors
  and dungeons below or inside it (each a location of its own, nested).
- **Inner cells:** for interiors and dungeons, the rooms and their use (a tavern hall with
  a counter and hearth; guest rooms above; a cellar with a hidden door).
- **Story anchors:** the spots the quests and scenes need, by name and relation ("the
  altar, at the far end of the crypt, hidden"), and the characters who live or work
  there (for actor slots, §9.2).
- **Connections:** the exits and entrances matching the world graph (a road east to
  Millbrook, stairs down to the crypt), so the builds place Gateways (§8.4).
- **Assets and style:** palette and materials, special assets needed (the Moon Amulet's
  shrine), sourcing preferences.

From the description, the Planner writes a **draft Brief** for the Build panel's build type
(`place-schema.md` §3.1): the same document the Build panel's Interpret step would produce
from a chat request, validated against the build type's Brief schema and the GBO, with the
story anchors carried as `storyAnchors` (required modules or concepts with relations) and
the connections as `exits`. A description that can't become a valid Brief ("a sky city"
when no build type covers it) is reported as an issue with the closest build type
proposed, or a new module proposed for the ontology (Phase 9's mechanism).

Output: `planner/places/<id>.json` (description + draft Brief).

### 5.7 ⑦ Systems — the game's data

From the Brief's systems tab and what the story and characters use:

| System | What the Planner writes | Doc |
|---|---|---|
| Attributes and progression | the attribute set, level curve, experience sources, skills or perks | `engine-gaps.md` §2.2 |
| Items | item records the story needs (unique items) and the game's generic sets (weapons, armour, consumables) in the art style and level bands | |
| Loot and economy | loot tables per enemy type and container kind; prices; merchant stock | |
| Abilities and magic | schools, spells and effects for the player and characters | `magic.md` §9 |
| Factions | faction records, relations, the player's starting standing | |
| Enemies | enemy classes and encounter tables per region and level band | `ai-packages.md`, `difficulty.md` |
| Difficulty | the presets from the Brief's Difficulty tab; which fields scale; assists; the difficulty curve over chapters | `difficulty.md` §8 |
| Input | the action map for the genre | `engine-gaps.md` §2.1 |
| Audio | music per place and state (from the mood), ambience; sourcing like art | |

Records are validated against their schemas from the reflection registry and checked for
balance (values within bands; every loot table reachable; every item used or sold).

Output: `planner/systems.json` and drafts of each record (written to `records/` in ⑫).

### 5.8 ⑧ Visuals — menus, windows, HUD, notifications

From the Brief's Visuals tab and the systems that are on (`game-ui.md` §17):
- **Theme:** chosen from the library to fit the art style and tone, or generated (palette
  from the project's art, fonts from the library, frame and button art from the Art tool).
- **Screens:** title, pause, settings, save/load, game over, credits; the windows the
  systems need (inventory if items, journal if quests, map if more than one place, spell
  book if magic, character sheet if progression, shop if merchants, dialogue).
- **HUD:** widgets and positions by HUD density (health, resource bars, quick slots,
  quest tracker, minimap or compass, clock, boss bar).
- **Notifications:** which events produce toasts and pop-ups, their priority and style.

The step produces UI documents with the theme and renders **preview images** of each screen
for review.

Output: `planner/ui.json` and the UI documents (written to `ui/` in ⑫).

**Animations** will be a further step here (character animation sets, effects per ability)
once Phase 18's systems exist; it is left out of this design on purpose.

### 5.9 ⑨ Validate — the whole design

Before anything is built, one stage checks everything together:
1. **Rules:** all of `story-ontology.md` §7 over the graph; each document's schema;
   records against their schemas.
2. **Cross-document checks:**
   - every character in a scene has a detail document, a place to be, and a package that
     puts them there when the scene happens;
   - every quest alias has a fill or a query that the place descriptions can satisfy
     (a story anchor exists for it);
   - every place a quest needs is in the build plan, reachable in the world graph given
     the quest gates;
   - every item a quest or a character uses has a record;
   - every system a quest or ability uses is on in the Brief;
   - level bands rise along the critical path (`difficulty.md` §6);
   - every screen a system needs exists.
3. **Critic review:** an AI pass over the story summary, cast and places for tone, theme,
   world rules and genre fit (the "critic" rules), with findings as decisions.
4. **Report:** findings by severity, each linked to the document and node, with a
   proposed fix. Errors block building. **Fix** applies the proposed edits (stale
   dependents regenerate); **Accept** keeps a warning with a reason (recorded).

When validation passes, the design is saved as the **baseline** (a named revision of every
document). Building works from the baseline; later edits are compared against it.

Output: `planner/report.json`, the baseline.

### 5.10 ⑩ Build places

Every place is built through the Build panel, in dependency order, manually or
automatically (§8).

### 5.11 ⑪ Populate and bind quests

Characters are placed, aliases filled, quest objects and triggers set, using what the
builds know about each place (§9).

### 5.12 ⑫ Write and playtest

The design becomes the engine's files (§11.1), and an automated playtest checks the game
can be played (§11.2).

**Dialogue is written here**, last, as in Dramatron: by now every scene, character voice,
quest stage, alias and place is fixed, so lines can name real things. For each scene in
`dialogue` mode, each quest's conversations (offer, progress, completion, per branch) and
each character's topics and barks, one call writes the dialogue tree from the scene's
beats, the speaker's voice and **what the speaker knows at that point** (the graph's
`knows`). Each tree is checked by the engine's dialogue validator and by R-T3 (no line
refers to something the speaker doesn't know), and repaired once. Cutscene files are
written from `cutscene` scenes the same way, with actor marks from the place anchors.

### 5.13 ⑬ Result — the game's page

Like the Build panel's Result (`place-interaction.md` A4), for the whole game:
1. **Headline:** *"Planned and built The Moon Amulet: 3 acts, 41 scenes, 26 characters,
   18 quests, 9 places in 4 scenes, in 1 h 12 min."*
2. **Asked vs made:** the Brief's scope against what was made, with ✓ / ⚠.
3. **Places:** each with its build (open its Build panel page), checks and assets.
4. **Quests:** each with its binding and playtest result.
5. **Checks:** validation and playtest, passed / total, each failure linked.
6. **Assets:** by source with licences; placeholders flagged.
7. **What next:** "Play from the start", "Play chapter 2", "Add a side quest in
   Millbrook", "Generate animations" (later).

The page stays as the game's design home (the Planner panel's Overview, §6).

## 6. The Planner panel

A dockable panel like the Build panel (`place-interaction.md` A2), for the same reasons:
the documents are wide, editable and long-lived; chat scrolls them away.

```
┌ Planner — The Moon Amulet ────────────────────────────────────────────────────────────────┐
│ ①Concept ②Outline ③Story ④Characters ⑤Quests ⑥Places ⑦Systems ⑧Visuals ⑨Validate ⑩Build ⑪Populate ⑫Write ⑬Result │
│───────────────────────────────────────────────────────────────────────────────────────────│
│ ┌ Acts ────────┐ ┌ Act I · Chapter 2: Riverford ──────────────────────────────────────────┐ │
│ │ ▸ Act I      │ │ s-04 Tamsin names the thief   dialogue  kernel  Temple   truth −→+  ✓  │ │
│ │   Ch1 Prolog │ │ s-05 Aldric's house           gameplay  kernel  Aldric's  ⚠ R-T3       │ │
│ │ ▸ Ch2 River… │ │ s-06 The ferryman's price     dialogue  satell. Docks              …  │ │
│ │ ▸ Act II     │ └────────────────────────────────────────────────────────────────────────┘ │
│ │ ▸ Act III    │ Scene s-05 · goal / conflict / outcome · participants · facts (+3 −1)      │
│ └──────────────┘ [Why?] [Regenerate] [Edit card] [Lock]    Stale: 2  [Regenerate stale]   │
└───────────────────────────────────────────────────────────────────────────────────────────┘
```

- **A stepper** with one view per stage. Stages already done stay open for editing; a
  stage whose inputs changed shows a stale count.
- **Views:** the Concept window (tabs); Outline (three columns); Story (act and chapter tree
  and a scene board with modes, kernels and check marks); Characters (a cast board with
  portraits, roles and relationships as a graph); Quests (a quest graph: stages, relations,
  the scenes they advance); Places (the world graph with each place's description, Brief
  and build status); Systems (record tables); Visuals (screen previews); Validate (the
  report); Build (the build plan list, §8); Result.
- **The knowledge graph viewer:** any node with its facts over time (a slider over the
  scenes), what depends on it, and the decisions that made it.
- **Every item:** **Why?** (its rationale and the facts and decisions behind it),
  **Regenerate**, **Edit**, **Lock** (locked items are never regenerated; dependents that
  conflict with them are reported instead).
- **The AI panel** shows one **plan card** (stage, progress, the headline at the end) with
  "Open planner panel", as the build card does. Chat follow-ups that name a part of the
  plan ("make Aldric the mentor's son") go to it as an edit.
- **Opens** from the menu (File ▸ New game with the Planner…, a shortcut), from the start
  window for new projects, and from a chat request recognised as a game request.

## 7. From place descriptions to builds

The bridge between the Planner and Phase 7:

| Place description (`planner/places/<id>.json`) | Brief (`places/<build>/brief.json`) |
|---|---|
| kind | `buildType` (settlement, interior, dungeon…) |
| size class, counts | `scale` (class and numbers) |
| zones, named buildings, features | components on/off and amounts; module fields |
| culture, era, mood, palette | `style` |
| inner cells (rooms) | interior programme / dungeon levels and rooms |
| story anchors | `storyAnchors`: required modules or concepts with relations and a `quest` / `scene` reference |
| connections | `exits`: an edge, direction or module where a Gateway goes, with its target place |
| assets and sourcing | the asset plan's per-component sources |

The Build panel accepts a Brief directly (`engine-gaps.md` §2.11): its ① Confirm opens
pre-filled (manual mode) or is accepted as is (automatic mode). Story anchors become
required Spec nodes, so the planner places them; the solver gives them their relations;
the scene build tags them (§9.2).

## 8. Building many scenes

### 8.1 The build plan

The Places stage's output becomes a **build plan**: one row per place to build.

| Field | Meaning |
|---|---|
| place | the location (and its World Bible key) |
| scene | the engine scene it goes into: its own scene, or inside a parent's scene (a village's houses are in the village scene; an inn's interior is a scene of its own) |
| build type, Brief | from §7 |
| prompt | a one-paragraph request, as a user would type it, for the log and for the Build panel's header |
| depends on | places that must be built first: a region before its sites; an exterior before the interiors whose doors it places; a parent before its children |
| status | waiting, building, needs you, built, verified, failed |
| build | the Build panel's build id (`places/<id>/`) once started |

Shown as a list in the Planner panel's ⑩ Build view, each row with **Open build panel**.

### 8.2 Manual or automatic

Asked when building starts (default from the Brief's Plan tab), and changeable per row:

- **Manual approval.** The first place's build starts; its Build panel opens on ① Confirm,
  pre-filled. The user goes through the steps as for any build (Confirm, Plan, Assets,
  Build, Result), adjusting anything. When its Result is kept, the Planner marks it built
  and starts the next place's build, waiting at its ① Confirm.
- **Automatic.** Builds start in order (independent ones can run concurrently, within the
  job limits of 7.10's background jobs). Each build's **automatic driver** advances its
  steps when each is ready: Confirm accepted as is, Plan approved when its checks pass,
  Assets by the policy, Build, Verify, Result kept. The user can open any build's panel and
  watch it; opening one doesn't stop it. A build whose checks fail after its repair
  rounds **stops and waits** ("needs you") while the others continue.
- **Mixed.** A row can be switched to manual (for the capital city, say) while the rest run
  automatically.

The automatic driver is a Build panel feature (Phase 22): the same steps, the same
documents, the same log, with each step's "continue" taken by the driver and recorded in
the build's history as `actor: planner` with the reason (checks passed, policy). A
manual build differs only in who pressed continue.

### 8.3 Traceability

Every build stays a normal build (`places/<id>/` with brief, spec, layouts, report, log
and history) and is listed in the build plan. From the Planner: the build's page; from the
build: "Planned by The Moon Amulet, place location/riverford". After the Planner finishes,
the build plan remains, so every scene's build, step by step, can be opened and replayed.

### 8.4 Connecting scenes

The world graph's connections become **Gateways** with **Markers** on both sides:
- An exit in a Brief (`exits`) is solved like any relation (a road leaving the east edge, a
  door of the temple, stairs in the crypt) and built as a Gateway entity with a Marker
  for arrivals.
- When both ends are built, the Planner links them (scene and marker names) and checks
  the round trip (walk out, arrive, walk back).
- A gated connection gets its gate: a locked door (an `Interactable` with its key or
  condition), a guard with a package and dialogue, or a level requirement.

### 8.5 Failures

A failed build doesn't stop the plan: dependents wait, independent places continue. The
row shows why (the build's own report) with **Retry**, **Open build panel**, **Edit
description**, or **Use a placeholder place** (a simple room of the right kind, so quest
binding and playtest can proceed and the place is rebuilt later).

## 9. Binding quests to the built world

The hard part: the quest design names roles and spots ("the thief", "a hidden spot in the
crypt"); the game needs entities at cells. The Planner never picks coordinates. It uses
three things the builds already know, in order.

### 9.1 What a build knows about its place

| Source | Gives |
|---|---|
| the Spec (`spec.json`) | every part by module, with its fields and rationale: "inn, tavern hall, guest room 3" |
| the solved layout (`layout.solved.json`) | every region, object and line with cells |
| `places/Where.h` | spatial facts in words: areas, directions, banks, distances, a text map |
| `PlaceNode` on entities | which entity is which part |
| **anchors and actor slots** (new, §9.2) | named spots each module offers: a counter's staff side, a bed, a hearth, a guard post, a dungeon's goal |

### 9.2 Requirements flow forward: anchors and actor slots

Most binding should be a lookup, not a search. So the requirements are sent *into* the
builds:
- **Modules declare anchors and actor slots** (an addition to `aige.module/1`, Phase 14):
  `tavern-hall` has `counter-staff`, `seat` ×n, `hearth`; `bedroom` has `bed`; `gate` has
  `guard-post` ×2; `crypt-chamber` has `altar`, `alcove` ×n; a dungeon level's goal room has
  `goal`. Each anchor has a facing, an affordance (sit, sleep, work, stand, hide, contain)
  and whether it's a smart object for packages (`ai-packages.md` §6).
- **The scene build writes them** as Markers (and smart objects), tagged with `PlaceNode`
  and named by the part path (`riverford/inn/tavern-hall/counter-staff`).
- **Story anchors** from the place description are required Spec nodes, so they exist by
  construction, and their markers carry the quest or scene reference
  (`quest: q-main, anchor: amulet-hiding-spot`).

### 9.3 Filling aliases

Each alias is filled in this order:
1. **Fixed:** a character, item or location named directly: the entity the build made for
   it (a story anchor's marker, a character's home).
2. **Query over the place:** an alias with a fill query, e.g.

   ```json
   { "alias": "ambush-spot", "kind": "spot", "in": "location/old-road",
     "where": { "affordance": "hide", "near": "road", "far-from": "location/riverford" } }
   ```

   The engine enumerates **candidates** from the anchors, Spec nodes and Where facts of
   that place, filters them by the hard parts of the query, and scores the soft ones.
   - One clear winner: taken, recorded with the scores.
   - Several close: **the AI chooses among the top candidates** (each described in words:
     "a thicket 6 cells north of the road, 40 cells from Riverford's gate"), with a
     rationale. It can't invent a candidate.
3. **Make it:** no candidate. A local edit adds the missing part with the module brush's
   partial solve (7.8): the Planner asks the build to place one module (a thicket, an
   alcove) in the part the query names; only that part is laid out again and its scene
   entities replaced. Recorded in the build's history as a planner edit.
4. **Fail:** reported with what was tried; the quest is marked unbound and the playtest
   skips it.

### 9.4 Placing characters, objects and triggers

- **Characters:** each character's AI package names its locations (home, work, sandbox
  area) as aliases; filled as above. The character's entity is placed at its package's
  start location for the time of day the game starts, with its `Talk`, package, class and
  inventory. A character who appears in several scenes over its schedule is placed in
  each (as `story.md` "Routines" requires today, or by the persistent-actor mechanism of
  `ai-packages.md` §7).
- **Quest objects:** items in containers or on anchors; enemies from encounter tables at
  spawners in the alias's area; readable documents on tables.
- **Triggers:** a `reach` objective gets a trigger area (the alias's region, or a radius
  around its marker); cutscenes get their start triggers and actor marks.
- **Placeholders:** an object without art gets the placeholder of its role (7.7), flagged
  in the Result.

### 9.5 Verification

After binding, each quest is checked against the built world:
- every alias filled; every target entity exists in the scene it's expected in;
- every objective location is **reachable** from the quest's start over the real collision
  and gateways, given the gates opened by earlier stages (the dungeon solver's
  completability check, extended across scenes);
- keys before locks across scenes; required characters' packages put them where the stage
  expects them, at an hour the player can reach them;
- R-Q1…R-Q7 again with real data.

Failures loop back: a re-query, a local edit, or an issue to the Quests stage.

## 10. Robustness and audit

### 10.1 The decision record

Every choice the Planner makes, AI or rule, and every user action on a plan, is a
**decision** appended to `planner/decisions.jsonl` (`planner-schema.md` §5):

```json
{ "id": "d-0377", "time": "2026-11-02T14:03:11Z", "stage": "story", "actor": "ai",
  "kind": "write-scene", "subject": "s-14",
  "inputs": { "docs": { "story/act-2.json": "r12" }, "kg": "r31", "facts": ["f-0921", "f-1004"] },
  "call": "log:5512", "chosen": "...", "alternatives": ["..."],
  "rationale": "Aldric's betrayal pays off the setup in s-05 and gives Act II its midpoint.",
  "checks": { "passed": 14, "failed": 0 }, "outputs": ["story/scenes/s-14.json@r1", "kg:f-1210", "kg:f-1211"] }
```

- **AI decisions** link to their call in `planner/log.jsonl` (inputs with fact ids,
  thinking, answer, validation, repair), as `places/<id>/log.jsonl` does for builds.
- **User decisions** record the edit, the accept or the override, with its reason when
  the user gives one ("keep warning R-K4: the twins are meant to be confused").
- **Rule decisions** (expansion, merging, candidate scoring) record their inputs and result.
- Every generated node, document and engine file carries `madeBy` (the decision id). That
  is how **Why?** answers for anything: a scene, a character's trait, a quest stage, a
  built entity (via `PlaceNode` → build → planner row → decision).

### 10.2 Documents and versions

Every Planner document is a JSON file under `planner/` with a revision number. Each stage
**commits** its documents together (a revision of the plan, like a commit); the history
shows each commit with its decisions; any revision can be compared and restored. The
**baseline** (§5.9) is a named revision. The project's git sees plain files.

### 10.3 Dependencies and staleness

Each document (and each node in it) records the revisions of what it was made from
(`inputs` in its decision). The Planner keeps the resulting **dependency graph**:
- **Stale:** an input changed after the document was made. Shown in the panel; nothing is
  regenerated without the user's say (or automatically, at autonomy auto).
- **Regenerate stale:** recomputes stale nodes in dependency order, as a build system
  rebuilds only what's out of date. Locked nodes are kept; if a locked node conflicts with
  a new input, it's reported, not changed.
- **Impact preview:** before an edit is applied, "this changes 3 scenes, 1 quest and 2
  dialogues" (competency question 11 of `story-ontology.md` §9).

### 10.4 Errors

| Failure | Handling |
|---|---|
| AI call times out or errors | retried up to 3 times with backoff (as 7.7); another configured model as a fallback; then the step reports and waits |
| Answer doesn't parse | read from prose where possible (7.4); else counted as a failed attempt |
| Answer violates the schema or rules | one repair round with the exact violations; then the step reports the violations and offers Edit / Regenerate / Accept |
| A batch item fails (one character of 26) | the others continue; the item is marked failed with Retry |
| A build fails | §8.5 |
| Binding fails | §9.3 step 4 |
| The editor closes mid-plan | resumes from the last committed step (§10.5) |
| A user edit breaks a rule | shown at once (the validators run on edit); saving is allowed, building is blocked until it's fixed or accepted |

No step guesses silently: a fallback is always a decision with its reason.

### 10.5 Jobs: persisted and resumable

The Planner runs as a **state machine** saved in `planner/state.json`: the current stage,
the queue of pending jobs (one per AI call, build or binding), and each job's status.
- **Idempotent jobs:** a job writes its outputs only when it finishes, under a revision; a
  job run twice gives the same files (or a new revision), never a half-written one.
- **Resume:** on reopening, finished jobs are kept, running ones restarted, and the plan
  continues (the 7.7 "builds reopen across sessions" behaviour, for the whole plan).
- **Concurrency:** independent jobs (characters in a band, places without dependencies)
  run in parallel within a limit; the editor stays responsive (7.10's background jobs).
- **Pause and stop:** at any time; stopping keeps everything committed.

### 10.6 Budget

Each AI call has its token and time ceilings (`place-interaction.md` B5). The plan shows
calls made and estimated remaining per stage; a per-plan budget ceiling (optional) pauses
the plan when reached.

## 11. Writing the game and playing it

### 11.1 Files written

| Planner document | Engine files |
|---|---|
| graph, characters, places | `world/<kind>/<id>.md` (World Bible: header from the graph, body from the detail) |
| characters | class records, package records and Lua scripts (`records/`, `scripts/ai/`), prefabs per character |
| quests | `quests/<id>.json` (quests v2), dialogue files, cutscene files |
| systems | `records/` (items, loot, abilities, factions, difficulty presets), the input map, music assignments |
| visuals | `ui/` (documents, theme), HUD and notification settings |
| build plan | scenes (through the builds), gateways, markers |
| brief | `game.json` (title, genre, systems on, start scene, title screen), Project Settings |

Each file carries `provenance: {planner: "<doc>#<node>", decision: "d-…"}` (in its JSON,
or the `.meta` for assets). Files are written through the same command path as any edit
(`product.md` §4.4), as one undoable step per stage, and shown as a summary (file counts by
kind with "Open") rather than a per-change list.

### 11.2 The automated playtest

An agent plays the game's logic headlessly (no rendering), using the engine's own systems:
- **Load:** every scene loads; every prefab, record, script and UI document resolves.
- **Walk the quests:** for the main quest and each side quest, in an order the quest
  relations allow, the agent moves the player along paths (A* over the real collision and
  gateways), triggers objectives (talks, kills by simulated combat at the difficulty's
  normal level or by a test cheat, collects, uses), and checks each stage advances, the
  journal updates and rewards arrive.
- **Endings:** each ending is reachable by some sequence of choices.
- **Dialogue:** every node reachable, every condition and effect valid, no reference to
  something the speaker doesn't know (R-T3 on the final text).
- **UI:** every screen opens, every binding resolves.
- **Report:** per quest: completed, stuck (where and why), skipped (unbound). Failures link
  to the quest, the place's build and the decision.

This is not a test of fun. It tells the user the game can be finished, which is what a
generated game most often gets wrong (GLDL's agent found 9.5% of generated levels
unplayable, `ai-building.md`).

## 12. Games without a story

The pillars apply in proportion to the genre (`game-types.md`); the profile in the Brief
switches stages:

| Genre (examples) | Story | Characters | Places | Quests | Systems and visuals |
|---|---|---|---|---|---|
| RPG, action RPG (Diablo, Skyrim-like in 2D) | full | full | full | full | full |
| Action-adventure (Zelda-like) | full | medium | full | main + few side | full |
| Life-sim (Stardew-like) | light (kishōtenketsu, seasons) | full (schedules, relationships) | one hub + interiors | many small | full |
| Platformer (Mario-like) | light (premise, intro, ending) | few (player, bosses) | levels as places (worlds → levels) | none; level goals | physics profile, menus |
| Shoot 'em up (Star Force-like) | premise | enemies and bosses | stages | none | waves, power-ups, scoring |
| Puzzle (Tetris-like) | none | none | one screen | none | rules, scoring, levels, menus |

v1.0 ships the profiles for its own genres (the first three rows, in 2D isometric and
top-down); the others arrive with their versions (`roadmap.md`: arcade and
puzzle in v1.1, platformers and shooters in v1.2).

For a game with no story, ② Outline holds the **game structure** (modes, levels, progression)
instead of the three pillars, and ③–⑤ are skipped. The rest (systems, difficulty, visuals,
building, playtest) apply, with genre-specific playtests (a level can be finished; a
puzzle mode starts and ends).

## 13. AI calls

The protocol of `place-interaction.md` Part B, with these steps thinking:

| Step | Thinks | Notes |
|---|---|---|
| Concept: three readings | yes | as Build's Interpret |
| Outline | yes, long | the three pillars at once; the most important call |
| Story: chapters, scene lists | yes | |
| Story: each scene | yes | inputs from the graph (§5.3) |
| Story: act critic | yes | |
| Characters: each | yes, short | parallel per band |
| Quests: main detail, each side quest | yes | |
| Places: each description and Brief | yes | |
| Systems: records | short | numbers checked by rules, not by the model |
| Visuals | short | |
| Validate: critic | yes | |
| Binding: choose among candidates | short | |
| ⑫ Dialogue trees and cutscenes | short | checked against what each speaker knows (R-T3) |
| Journal text, item descriptions, string translations | no | formatting steps |

Roles (Agents' Room [11]): each step's task description gives the model one role — *story
architect* (outline, chapters), *writer* (scenes, dialogue), *character designer*, *quest
designer*, *level designer* (places), *systems designer*, *UI designer*, *critic*. The
critic never reviews its own call's output in the same context.

Context: never the whole design. Each call gets the documents above it in the hierarchy as
summaries, its own inputs in full, and the graph slice (`story-ontology.md` §6.4). A scene
call stays under about 12k tokens of input on a 40-scene story; retrieval keeps it flat as
stories grow.

## 14. Design review (2026-10-21)

Points revised while writing this document set:

1. **Gaps before the Planner.** The request put the Planner at Phase 12. It is designed now
   and numbered 21–23, after the gaps (12–20): it writes quests v2, AI packages,
   difficulty presets, records and UI documents, and each needs its schema in the engine
   first (Phase 7's rule: schemas before generators). The design documents exist now, so
   gap phases are built knowing what the Planner will need.
2. **Quests in three moments**, not one stage (§5.5): the main quest is the story's playable
   form and is sketched with it; side quests need characters; binding needs places.
3. **Requirements flow forward** (§9.2): quests' needs go into place descriptions and Briefs
   as story anchors, so binding is mostly a lookup. Searching and local edits are
   fallbacks. Without this, binding would be the least reliable step.
4. **A Systems stage** (⑦) was added between places and visuals. The request named story,
   characters, places and visuals; items, loot, abilities, factions, progression and
   difficulty presets also have to be designed for a working game.
5. **Difficulty is asked in ① Concept** (a tab of the Brief window) and applied in ⑦; the
   class records of ④ carry base values, and difficulty scales them (`difficulty.md`), so
   characters aren't written once per difficulty.
6. **One fact, one place** between the knowledge graph and the World Bible (§5.2,
   `story-ontology.md` §6.2): the graph holds structure, the files hold prose, `rel:` lines
   are derived.
7. **Validation before building** (⑨) and **playtest after** (⑫): design errors are cheap
   before building and expensive after.
8. **A genre profile** (§12) so the same Planner can make a puzzle game; the three pillars
   remain the backbone for story games.
9. **The Build panel gets an automatic driver and pre-filled Briefs** (§8.2) rather than the
   Planner building places itself, so every place build is the same, inspectable, editable
   build as a manual one.
10. **Dialogue is written last** (§5.12), after binding, when every name, place and quest
    stage is final; written earlier, it would go stale with every edit upstream.
11. **Persistent actors** (`ai-packages.md` §7) replace "one copy of an NPC per scene", which
    a generated game with 26 characters across 9 scenes couldn't keep consistent.

## 15. Open points

- **Runtime story generation** (`product.md` §5.4) is out of scope here: the Planner designs
  at development time. Its graph could later ground runtime dialogue and radiant quests.
- **How much automatic is safe.** Automatic building of a 30-place game is long; whether to
  checkpoint with the user per chapter is decided after trying it.
- **Voice and music generation** are sourced like art, through the asset plan; their tools
  are not designed here.
- **Animations step** (after Phase 18).
- **Learning from verified plans:** as builds feed `places/examples.json`, verified plans
  could give examples to the Outline and Quests steps.
