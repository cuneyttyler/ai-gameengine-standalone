# The story ontology — GSO, and the story knowledge graph

Status: **draft for review (2026-10-21)**. Working name **GSO** (Game Story Ontology),
namespace `https://w3id.org/aige/gso#`, the sibling of the place ontology GBO
(`place-ontology.md`). It is built in Phase 21, before the AI Planner (`ai-planner.md`)
that generates stories with it.

| Document | Authority for |
|---|---|
| `ai-planner.md` | the procedure that generates a game, its stages and its user interface |
| `planner-schema.md` | the Game Brief and the Planner's documents |
| **`story-ontology.md`** (this) | the classes, properties, vocabularies and rules of stories, characters, quests and their places; the knowledge graph |
| `story.md` | the engine's runtime files for dialogue, quests and schedules |

---

## 1. Why a story ontology

The AI Planner writes a story scene by scene, along with its characters, places and
quests. Without a shared model, three things go wrong, and each is reported in the
literature on long LLM stories:
- **Drift.** Facts change between scenes: a character's sister becomes her cousin, a
  dead villain speaks. DOME (NAACL 2025) and SCORE (2025) keep a knowledge graph of what
  has been written for this reason; outline-only pipelines lose the thread as stories
  grow (*Long Story Generation via Knowledge Graph and Literary Theory*, 2025).
- **Untraceable decisions.** A free-text story can't say *why* a scene exists or what
  depends on it, so a change can't be propagated and nothing can be checked.
- **Unplayable output.** A story for a game must turn into quests, dialogue, places and
  behaviour. Free text says "she hides the amulet in the crypt"; the game needs an item,
  a container, a location, an alias and a quest stage.

GSO gives the Planner one vocabulary for all of this. Every step reads from it and writes
to the **story knowledge graph** (§6), the structured record of this game's story. As in
GBO, the ontology is the schema and the checks, and the graph is the data.

| Step (`ai-planner.md`) | Asks the ontology / graph |
|---|---|
| Concept | Which structure templates, tones, conflict types fit this genre and scope? |
| Outline | Which narrative roles must be filled (protagonist, antagonist, mentor)? Which acts does the template have? |
| Story | What does this scene need (goal, conflict, outcome, value turn)? Who is alive, where, knowing what, at this point? Which threads are open? |
| Characters | Which roles and motivations does this character have? Which relationships must be consistent? Which gameplay role (vendor, boss)? |
| Quests | Which motivations and strategies suit this giver? Which aliases must the quest fill? Which story scenes does it advance? |
| Places | Which places does the story use, what happens in each, what must each contain? |
| Validate | Which rules apply (§7)? |
| Explain | Why does this scene, character or quest exist? (Each fact cites the decision that made it.) |

## 2. What exists, and what we take

### 2.1 Narratology

| Source | What it says | What GSO takes |
|---|---|---|
| Propp, *Morphology of the Folktale* (1928) | 31 functions in fixed order; 7 spheres of action (villain, donor, helper, sought-for person, dispatcher, hero, false hero) | narrative functions as a vocabulary of beat types; the spheres as narrative roles |
| Greimas, actantial model (1966) | six actants: subject, object, sender, receiver, helper, opponent | `actant` on character roles per storyline: one character can be helper in one line and opponent in another |
| Campbell / Vogler, *The Writer's Journey* (1992) | the hero's journey's 12 stages; 8 archetypes (hero, mentor, threshold guardian, herald, shapeshifter, shadow, ally, trickster) | a structure template; archetypes as character roles |
| Freytag (1863); the three-act structure (Field); Snyder, *Save the Cat* (2005) | exposition, rising action, climax, falling action, resolution; acts and beats | structure templates with their beats and their positions in the story (percent) |
| Kishōtenketsu | four parts without central conflict: introduction, development, twist, reconciliation | a template for stories without an antagonist (cozy games) |
| Barthes, Chatman, *Story and Discourse* (1978) | **kernels** (events the plot can't lose) and **satellites** (can be removed); story (fabula) vs discourse (the telling) | `kernel`/`satellite` on scenes: kernels are main-line, satellites optional content; story time vs presentation order |
| McKee, *Story* (1997) | a scene turns a **value** (life/death, trust/betrayal) from one charge to the other; beats inside scenes | every story scene has a value at stake, its charge before and after |
| Truby, *The Anatomy of Story* (2007) | weakness and need, desire, opponent, plan, battle, self-revelation | a character's `need`, `desire` and `weakness` for arcs |
| Trabasso; Riedl & Young, IPOCL (2010) | stories as causal networks; characters' actions must be explained by their intentions | causal links between events; every kernel action is motivated by a character goal (rule R-C2) |
| Elson, Story Intention Graph (2012) | timeline, plus goals, plans, beliefs and affect of agents, and how events actualize or prevent goals | goals and beliefs on characters; `actualizes`, `prevents` between events and goals |

### 2.2 Ontologies

| Ontology | Scope | What GSO takes / aligns to |
|---|---|---|
| **OntoMedia** (Jewell et al., 2005) | events, entities and timelines in media; an entity's state at a time ("being") | **temporal facts**: a relationship or state holds between two scenes (§6.3) |
| **Drammar** (Damiano & Lombardo, 2016) | drama: agents, plans, emotions, drama units | agents' plans and goals; scenes as units |
| **NOnt**, the Narrative Ontology (Meghini, Bartalesi, Metilli, *Semantic Web* 12(2), 2021) | narratives as events with causal, mereological and temporal relations, on CIDOC CRM, FRBRoo and OWL-Time | event relations: `causes`, `partOf`, `before`; the fabula/narrative split |
| **GOLEM** (Pianzola, Pannach et al., 2023–2025) | fiction and narrative on CIDOC CRM, LRMoo and DOLCE; a character module | character modelling (traits, roles, relations); alignment target for characters |
| **SEM**, Simple Event Model (van Hage et al., 2011) | event, actor, place, time, each with roles | the event–actor–place–time core with role-typed participation |
| **BBC Storyline ontology** (2013) | storylines that group events across time | `Storyline` (main, side, companion lines) |
| **Video Game Ontology** (Parkkila et al., 2017) | games: characters, items, achievements, sessions | alignment for `Character`, `Item`, `Achievement` |
| **Game Ontology Project** (Zagal et al., 2005) | game structure: goals, entities, rules | the vocabulary for gameplay roles |
| **GBO** (ours) | places, modules, build types | `Location` maps to a GBO build type and module (§4.4) |

None covers game stories as a whole: player choice, quests with roles filled at run time,
gameplay functions of characters (vendor, boss), places that must be built, or the link
to engine files. GSO keeps their terms where they overlap (§10 lists the alignments) and
adds those.

### 2.3 Game practice

| Source | What GSO takes |
|---|---|
| Doran & Parberry, *A prototype quest generator based on a structural analysis of quests from four MMORPGs* (PCG 2011) | 9 NPC **motivations** (knowledge, comfort, reputation, serenity, protection, conquest, wealth, ability, equipment), each with **strategies**, built from about 20 **atomic actions** (goto, kill, gather, deliver, escort, spy, use…) |
| Aarseth, *From Hunt the Wumpus to EverQuest* (2005); Howard, *Quests* (2008) | quest axes: place, time, objective |
| Creation Kit quests | stage-indexed quests, objectives shown per stage, **reference and location aliases** filled when the quest starts, Story Manager events, quest conditions |
| Ashwell, *Standard Patterns in Choice-Based Games* (2015) | branching patterns: time cave, gauntlet, branch-and-bottleneck, quest, open map, sorting hat, floating modules, loop-and-grow |
| articy:draft | entity templates with typed properties; flow fragments, dialogues, locations |
| Dramatron (Mirowski et al., CHI 2023) | logline → characters → plot (scenes with locations) → location descriptions → dialogue: the hierarchy the Planner follows |
| *From World-Gen to Quest-Line* (Borawski et al., arXiv 2604.25482, 2026) | each stage conditions on the strict JSON of the earlier ones; campaign planning kept apart from quest expansion |
| StoryVerse (Wang et al., FDG 2024) | **abstract acts** instantiated against the current world state |

## 3. Design

- **Four layers**, kept apart because they change for different reasons:

  | Layer | Holds | Example |
  |---|---|---|
  | **World** | what exists: characters, factions, places, items, lore, rules of the world | Old Tamsin, the Wardens, Riverford, the Moon Amulet |
  | **Fabula** | what happens, in story time, with causes | the amulet is stolen; Tamsin is cursed |
  | **Plot** | how the story is told and played: acts, scenes, storylines, choices, endings | Act I, scene 4: the player finds Tamsin cursed |
  | **Play** | what the game runs: quests, stages, objectives, aliases, dialogue, cutscenes | quest `the-moon-amulet`, stage 20, alias `thief` |

  The Planner writes down the layers in that order inside each scene (§5 of
  `ai-planner.md`), and checks between layers (every kernel event appears in a scene;
  every main-line scene is reached by a quest stage).
- **Time is explicit.** Facts that change (alive, location, relationship, possession,
  knowledge) hold over an interval of scenes (§6.3), so "who is alive in scene 12" has an
  answer.
- **Roles over individuals.** Narrative roles (antagonist), gameplay roles (vendor) and
  quest aliases (the thief) are separate from the characters filling them, so one
  character can fill several and the checks can ask "is every role filled?".
- **Closed vocabularies** for anything the engine or the checks depend on (scene modes,
  objective actions, relationship types); free text for everything else (descriptions,
  voice).
- **Provenance on every fact**: the decision that made it (`ai-planner.md` §10) or the user.

## 4. Classes (the TBox)

### 4.1 Plot structure

| Class | Meaning | Key properties |
|---|---|---|
| `Story` | the game's story | `logline`, `synopsis`, `structure` (a template), `pattern` (branching pattern), `tone`, `themes`, `endings` |
| `StructureTemplate` | a shape for stories, with its beats and their positions | three-act, hero's journey, five-act, save-the-cat, kishōtenketsu, chapters (episodic) |
| `Act` | a top-level part | `order`, `purpose` (setup, confrontation, resolution), `beats` |
| `Chapter` | a group of scenes with one dramatic question; usually a play session or a region | `question`, `hub` (a place) |
| `StoryScene` | **one unit of story** (not an engine scene): a goal pursued against conflict to an outcome, turning a value | `goal`, `conflict`, `outcome`, `value`, `chargeBefore`, `chargeAfter`, `mode`, `kernel`, `location`, `participants`, `storyTime`, `order` |
| `Beat` | a moment inside a scene (an action and a reaction) | `function` (a Propp-style function), `text` |
| `Storyline` | a thread of scenes: main, side, companion, faction | `kind`, `scenes`, `resolvedBy` |
| `ChoicePoint` | where the player's choice changes what follows | `options` (each with `conditions`, `consequences`, `leadsTo`) |
| `Convergence` | where branches join again (branch-and-bottleneck) | `from` (branches), `to` (scene), `carries` (variables that remember the branch) |
| `Ending` | one way the story ends | `conditions`, `epilogue` |
| `StoryVariable` | state the story remembers (choice made, reputation, a count) | `type`, `default`, `setBy`, `readBy` |

Scene **modes** (vocabulary `SceneMode`): how the player experiences it.

| Mode | The player… | Becomes, in the engine |
|---|---|---|
| `cutscene` | watches | a cutscene (`effects-animation.md` §6) |
| `dialogue` | talks | a conversation (dialogue file) |
| `gameplay` | acts (fight, explore, solve, sneak) | a quest stage with objectives |
| `environmental` | finds it in the world (a note, a ruin, a corpse) | placed objects, readable items |
| `discoverable` | may never see it (optional) | a satellite: side content |
| `ambient` | overhears it (barks, NPC conversations) | barks, background scenes |

### 4.2 Fabula: events and causes

| Class | Meaning |
|---|---|
| `Event` | something that happens in story time: an action by an agent, or a happening |
| `Action ⊑ Event` | done by an agent with an intention (`agent`, `intends` → a `Goal`) |
| `Happening ⊑ Event` | without an agent's intention (a storm, a plague) |
| `StateChange` | an event's effect on a fact: `asserts` / `retracts` a temporal fact (§6.3) |
| `BackstoryEvent ⊑ Event` | before the game starts; known to the world, revealed in play |

Causal properties (NOnt, Trabasso, SIG): `causes`, `enables`, `motivates` (event →
goal), `prevents`, `actualizes` (event → goal), `resolves` (event → conflict).
Temporal: `before`, `during`, `storyTime` (an ordinal or a date in the game's calendar).
A scene `presents` events; the order of scenes is the discourse order, which may differ
from story time (flashbacks, revealed backstory).

### 4.3 Agents

| Class | Meaning | Key properties |
|---|---|---|
| `Character` | a person or creature with a part in the story | `name`, `summary`, `species`, `age`, `appearance`, `voice`, `traits`, `need`, `desire`, `weakness`, `motivations`, `goals`, `beliefs`, `arc`, `home`, `factions`, `importance` (major, supporting, minor, extra) |
| `PlayerCharacter ⊑ Character` | the player's character | `fixed` or `created`; `class` options |
| `Group` / `Faction` | an organisation | `goals`, `members`, `territory`, `relations` (to other factions) |
| `NarrativeRole` | a function in a storyline: Vogler archetype, Propp sphere or Greimas actant | `storyline`, `filledBy` |
| `GameplayRole` | a function in play | quest giver, vendor, trainer, companion, enemy, boss, guard, innkeeper, healer, ferryman, info broker |
| `Trait` | a stable disposition | a vocabulary (Big Five poles, plus game-useful ones: greedy, loyal, cowardly) with an intensity |
| `Motivation` | why a character wants things | Doran & Parberry's nine |
| `Goal` | what a character wants, at a time | `kind`, `target`, `status` (active, achieved, failed, abandoned) |
| `Belief` / `Knowledge` | what a character knows or believes, true or false | `about` (a fact), `true`, `since` (scene); used by dialogue checks |
| `Arc` | how a character changes | `from`, `to` (traits or beliefs), `turningScenes` |
| `Relationship` | a typed tie between two agents, reified so it can change | `type`, `valence` (−1…1), `public` / `secret`, `since`, `until` |

The **AI package** of a character (`ai-packages.md`) and its **stats** (a class record)
are written in the Characters stage and linked from the character node (`hasPackage`,
`hasClass`), so the graph knows what each character does in the world.

### 4.4 World

| Class | Meaning | Key properties |
|---|---|---|
| `Location` | a place in the story world | `kind`, `parent` (region hierarchy), `description`, `mood`, `connections`, `buildType` (GBO), `requiredParts`, `scene` (the engine scene once built) |
| `Connection` | how two locations join | `kind` (road, door, stairs, portal, sea route), `gated` (by a key, quest stage, level) |
| `Item` | a thing with a part in the story | `kind`: key item, MacGuffin, reward, clue, document; `unique`; `record` (the engine item record) |
| `Lore` | background: history, religion, myths | links to events and places |
| `WorldRule` | a rule of this world | "magic costs memories", "the dead rise at night"; checked as text by the AI critic |
| `Theme` | what the story is about | "loyalty", "the cost of power" |
| `Conflict` | an opposition | `type` (person vs person, vs society, vs nature, vs self, vs fate, vs technology), `between`, `stakes` |

**The bridge to GBO.** A `Location` names its GBO build type (settlement, interior,
dungeon…) and the parts the story needs in it as GBO modules or concepts ("a temple with
a crypt", "a tavern with rooms upstairs"). The Places stage turns these into a Brief for
the Build panel (`ai-planner.md` §7). The story's `Location` hierarchy (region →
settlement → building → room) maps onto GBO's nested places (`place-schema.md` §3.5).

### 4.5 Play: quests

| Class | Meaning | Key properties |
|---|---|---|
| `Quest` | a unit of play with a goal, given and completed | `storyline`, `type` (main, side, faction, companion, repeatable, misc), `motivation`, `strategy`, `giver`, `stages`, `aliases`, `startConditions`, `rewards`, `level` (difficulty band) |
| `QuestStage` | a numbered state of a quest | `index`, `objectives`, `onEnter`, `journal` (text), `advancesScene` (a `StoryScene`), `terminal` (completes or fails) |
| `Objective` | what the player must do in a stage | `action` (§8), `target` (an alias), `count`, `optional`, `location` (an alias) |
| `Alias` | a **role filled when the quest starts**: a character, an item, a location, a container | `kind`, `fill` (a fixed entity, or a query: "a bandit leader in the hideout"), `required`, `createIfMissing` |
| `Reward` | what completing gives | items, currency, experience, reputation, unlocks |
| `QuestRelation` | between quests | `requires`, `excludes`, `unlocks`, `sameStoryline` |

A quest **implements** a storyline's scenes: each main-quest stage `advancesScene` a
kernel scene, so the main quest is the playable form of the main storyline. Side quests
implement satellite scenes or storylines of their own.

### 4.6 Presentation

| Class | Links to |
|---|---|
| `Dialogue` | an engine dialogue file; `speaker`, `scene`, `topics` |
| `Cutscene` | an engine cutscene file |
| `Bark` | a one-line remark with conditions |
| `Document` | a readable in the world: letter, book, note |

### 4.7 Provenance

| Class | Meaning |
|---|---|
| `Decision` | a recorded choice by the AI or the user (`ai-planner.md` §10): `stage`, `question`, `chosen`, `alternatives`, `rationale`, `inputs` |
| `Source` | a theory, a template, the user's request, a learned fact |

Every node and edge in the graph has `madeBy` (a `Decision`) and may have `source`.

## 5. Properties (summary)

| Property | Between | Notes |
|---|---|---|
| `partOf` | Beat → Scene → Chapter → Act → Story | mereology |
| `inStoryline` | Scene → Storyline | a scene may serve several |
| `next` / `branchesTo` | Scene → Scene | discourse order; branches via `ChoicePoint` |
| `presents` | Scene → Event | |
| `participant` | Scene / Event → Character | with a role: agent, patient, witness, mentioned |
| `at` | Scene / Event → Location | |
| `causes`, `enables`, `motivates`, `prevents`, `actualizes`, `resolves` | Event → Event / Goal / Conflict | causality |
| `fills` | Character → NarrativeRole / GameplayRole | |
| `hasRelationship` | Character → Relationship → Character | reified, temporal |
| `memberOf` | Character → Faction | temporal |
| `knows` | Character → Belief | temporal |
| `possesses` | Character / Location → Item | temporal |
| `locatedAt` | Character / Item → Location | temporal (where they are, by default) |
| `gives` | Character → Quest | |
| `advancesScene` | QuestStage → Scene | |
| `fillsAlias` | entity → Alias | written by quest building (`ai-planner.md` §9) |
| `realizedBy` | any → engine file or entity | the link to what the engine runs |
| `madeBy` | any → Decision | provenance |

## 6. The story knowledge graph

### 6.1 What it is

The instance data of GSO for one game: its characters, places, events, scenes, quests and
the facts between them. It is written by the Planner as it goes and by the user in the
editor, and it is read by every later step.

### 6.2 Storage and identity

- **File:** `planner/kg.json` in the project, the engine's typed property-graph JSON (the
  same format as GBO's, `place-ontology.md` §3): nodes with a class, an id and fields;
  edges with a property, endpoints, fields and an interval. Exported to Turtle on demand.
- **Identity with the World Bible.** Characters, factions, locations, items, events and lore
  in the graph **are** World Bible entries: the node id is the World Bible key
  (`character/tamsin`). The World Bible file holds the prose (the body); the graph holds
  the structured facts. The `rel:` lines of each entry are written from the graph's
  current (end-of-story) relationships, so the two never disagree. Editing a `rel:` line by
  hand updates the graph. **One fact, one place**: structure in the graph, prose in the file.
- **Plot and play nodes** (scenes, storylines, quests, stages, aliases) live only in the
  graph and in the Planner's documents; quests also become `quests/<id>.json`.
- **Versions:** each Planner stage commits the graph with a revision number; the Planner
  history (`ai-planner.md` §10.4) can show and restore any revision.

### 6.3 Temporal facts

Facts that change hold over an interval of scenes, by discourse order:

```json
{ "p": "hasRelationship", "s": "character/tamsin", "o": "character/aldric",
  "type": "trusts", "valence": 0.7, "from": "s-01", "until": "s-14", "madeBy": "d-0231" }
{ "p": "hasRelationship", "s": "character/tamsin", "o": "character/aldric",
  "type": "betrayed-by", "valence": -0.9, "from": "s-14", "madeBy": "d-0377" }
{ "p": "state", "s": "character/aldric", "state": "dead", "from": "s-22", "madeBy": "d-0410" }
```

`until` absent means "to the end". A scene's **world state** is every fact whose interval
holds at it: the Story stage gives the AI that state (who is alive, where, what each
participant knows) for the scene being written. With branches, intervals are per branch
(`branch: "spare-aldric"`), and facts outside any branch hold in all.

### 6.4 How generation uses it

Each AI call gets a **subgraph**, never the whole graph (as GBO does):
- the scene's participants, their current state, relationships, goals and knowledge;
- the open threads: unresolved conflicts, active goals, planted setups not yet paid off;
- the location's facts and the scenes that happened there;
- the relevant ontology slice (the scene's mode, the template's beat for this position).

The AI answers with the scene **and the facts it adds or ends** (assert / retract, with
intervals). These are validated against the shapes and rules (§7) before they're
committed; a violation goes back once with its message (the repair round of
`place-interaction.md` B1).

### 6.5 Retrieval for long stories

Big games have hundreds of scenes. Besides the subgraph by structure, the AI service
indexes scene summaries and World Bible entries by embedding (the existing
`.cache/world_index.sqlite`), so a scene can recall "the last time these two met" by
meaning. The graph remains the authority; retrieval only finds candidates.

## 7. Rules and checks

Shapes (SHACL Core) cover types, cardinalities and closed vocabularies. The rules below go
beyond SHACL; like GBO's aggregates they are engine expressions over the graph, evaluated
by `engine/story/` (Phase 21). Each has an id, a severity and a message template.

### 7.1 Structure

| Id | Rule | Severity |
|---|---|---|
| R-S1 | every act has at least one scene; acts follow the template's order | error |
| R-S2 | every main-line scene is reachable from the first scene along `next`/`branchesTo` | error |
| R-S3 | every branch reaches an ending or a convergence | error |
| R-S4 | every `ChoicePoint` option has consequences (a variable, a scene, a relationship) | warning |
| R-S5 | the climax (the template's beat) is in the last 25% of the main line | warning |
| R-S6 | every kernel scene turns its value (charge before ≠ charge after) | warning |

### 7.2 Causality and motivation

| Id | Rule | Severity |
|---|---|---|
| R-C1 | every kernel event is caused or enabled by an earlier event, or is the inciting incident | warning |
| R-C2 | every `Action` in a kernel scene has an agent goal it serves (IPOCL's intentionality) | warning |
| R-C3 | every setup (a planted clue, a Chekhov item) is paid off, or marked `openEnded` | warning |
| R-C4 | every conflict is resolved by an ending, or deliberately left open | warning |

### 7.3 Continuity (temporal facts)

| Id | Rule | Severity |
|---|---|---|
| R-T1 | a character participates only while alive (unless the scene is a flashback, a vision or the character is undead) | error |
| R-T2 | a character is in one location per scene | error |
| R-T3 | a character refers only to what it `knows` at that scene (checked on dialogue) | error |
| R-T4 | a unique item has one possessor at a time; it moves only by an event | error |
| R-T5 | relationship changes have a cause (a scene that presents the change) | warning |
| R-T6 | travel between locations in consecutive scenes is possible by `Connection` (and in the time elapsed, if the game has time) | warning |

### 7.4 Characters

| Id | Rule | Severity |
|---|---|---|
| R-K1 | every narrative role the template requires is filled (protagonist, antagonist where the template has conflict) | error |
| R-K2 | every major character has need, desire, motivation and an arc or a reason not to change | warning |
| R-K3 | relationships are consistent both ways (parent ↔ child, siblings, married) | error |
| R-K4 | names are unique and distinguishable (no two major characters starting with the same three letters) | warning |
| R-K5 | every character with a gameplay role has what it needs: a vendor has stock, a trainer skills, a boss a fight, a companion a package | error |
| R-K6 | every character placed in the world has an AI package or a reason to stand still | warning |

### 7.5 Quests

| Id | Rule | Severity |
|---|---|---|
| R-Q1 | every main-line kernel scene in `gameplay` mode is advanced by a main-quest stage | error |
| R-Q2 | every alias is fillable: a fixed entity that exists, or a query with at least one candidate (checked again after building, §9 of `ai-planner.md`) | error |
| R-Q3 | each quest is completable: from its start, every required stage has a path, keys come before locks, required characters are alive at that point | error |
| R-Q4 | a quest's giver has a motivation consistent with the quest's (Doran & Parberry) | warning |
| R-Q5 | quest levels follow the region's level band and the game's difficulty curve (`difficulty.md` §6) | warning |
| R-Q6 | rewards fit the economy: their value is within the band for the quest's level and type | warning |
| R-Q7 | a quest that kills or removes a character doesn't break a later quest that needs them (or that quest is excluded) | error |

### 7.6 Places

| Id | Rule | Severity |
|---|---|---|
| R-P1 | every location used by a scene exists, with a build type | error |
| R-P2 | every location is reachable from the start location by connections, given the quest gates | error |
| R-P3 | the parts a scene needs in a location are in its required parts (a scene in "the crypt below the temple" needs a crypt) | error |
| R-P4 | a location's mood and description agree with the scenes set there (critic) | warning |

Rules marked "critic" are judged by the AI reading the subgraph, not by an expression;
their verdicts are recorded as decisions like any other.

## 8. Vocabularies (SKOS)

| Scheme | Values (abridged) |
|---|---|
| `StructureTemplate` | three-act (setup 25%, confrontation 50%, resolution 25%), hero's journey (12 stages), five-act, save-the-cat (15 beats), kishōtenketsu, episodic chapters |
| `BranchingPattern` (Ashwell) | linear, gauntlet, branch-and-bottleneck, time cave, quest (hub of independent lines), open map, sorting hat, floating modules, loop-and-grow |
| `SceneMode` | cutscene, dialogue, gameplay, environmental, discoverable, ambient |
| `NarrativeRole` | protagonist, antagonist, deuteragonist, mentor, ally, herald, threshold guardian, shapeshifter, trickster, love interest, false hero, dispatcher, donor, foil |
| `Actant` | subject, object, sender, receiver, helper, opponent |
| `GameplayRole` | quest giver, vendor, trainer, companion, enemy, elite, boss, guard, innkeeper, healer, transport, information, crafter, quest target, civilian |
| `Motivation` | knowledge, comfort, reputation, serenity, protection, conquest, wealth, ability, equipment |
| `QuestStrategy` | per motivation (Doran & Parberry), e.g. protection: attack threatening entities, treat or repair, create a diversion, assemble a fortification, guard an entity |
| `ObjectiveAction` | goto, reach, talk, kill, defeat-count, gather, collect, deliver, escort, defend, use, interact, examine, spy, stealth, capture, free, craft, wait, choose, survive |
| `ConflictType` | person, society, nature, self, fate, technology, supernatural |
| `RelationshipType` | family (parent, child, sibling, spouse), romantic, friend, rival, enemy, mentor-of, serves, employs, owes, trusts, fears, betrayed-by, member-of |
| `Value` (McKee) | life–death, love–hate, trust–betrayal, freedom–slavery, truth–lie, hope–despair, loyalty–treachery, wealth–poverty, justice–injustice |
| `Trait` | Big Five poles (open, conscientious, extravert, agreeable, neurotic, and their opposites) plus brave, cowardly, greedy, generous, loyal, treacherous, pious, cynical, curious, proud, humble |
| `Tone` | heroic, dark, grim, comic, whimsical, cozy, tragic, mysterious, horror, romantic |

Vocabularies grow per project and per user, as GBO's do (Phase 9's mechanism: engine,
user, project).

## 9. Competency questions

The ontology and graph are complete when they can answer these (each becomes a test):
1. Who is alive, where, at scene *n*, on branch *b*?
2. What does character *c* know at scene *n*? Can *c* mention *x* in this dialogue?
3. Which scenes does the main quest's stage 30 advance, and what does it need in which place?
4. Which characters fill no role, and which roles have no character?
5. Which setups have no payoff? Which conflicts are unresolved at each ending?
6. Which quests become impossible if the player kills *c* at scene *n*?
7. Which places does the story use, and which parts must each contain?
8. Which characters does the player meet in chapter 2, in what order?
9. What is the giver's motivation for quest *q*, and which strategy does it follow?
10. Why does scene *s* exist? (Its decision, rationale and the facts it used.)
11. What depends on character *c*'s betrayal (scenes, quests, dialogue, endings)?

## 10. Representation and publishing

- **In the engine:** `data/story/` holds the ontology as JSON (classes, properties,
  vocabularies, templates, rules), loaded by `engine/story/` (Phase 21) like
  `data/places/` by `engine/places/`. A project adds `story/ontology/` extensions.
- **Published:** `ontology/gso.ttl` (core, OWL 2, by hand), `gso-vocab.ttl` (SKOS, generated)
  and `gso-shapes.ttl` (SHACL, generated), with a test that fails when they fall behind,
  as for GBO. CC BY 4.0.
- **Alignments** (`ontology/gso-align.ttl`, `owl:equivalentClass` / `rdfs:subClassOf` /
  `skos:closeMatch`): `gso:Event` ~ `sem:Event`, `nont:Event`; `gso:Character` ~
  `vgo:Character`, GOLEM's character class; `gso:Location` ~ `sem:Place`;
  `gso:Storyline` ~ `storyline:Storyline`; `gso:Relationship` ~ OntoMedia's relationship
  events. We align, not import: GSO stays small enough to load in memory and to quote in
  prompts.

## 11. Example

A fragment of a graph after the Story stage (abridged):

```json
{ "nodes": [
  { "id": "story", "class": "Story", "structure": "three-act", "pattern": "branch-and-bottleneck",
    "logline": "A disgraced warden must recover a stolen amulet before the dead wake." },
  { "id": "character/tamsin", "class": "Character", "importance": "supporting",
    "motivations": ["protection"], "traits": [["agreeable", 0.8], ["brave", 0.4]] },
  { "id": "role/mentor", "class": "NarrativeRole", "archetype": "mentor", "storyline": "main" },
  { "id": "s-04", "class": "StoryScene", "act": "act-1", "mode": "dialogue", "kernel": true,
    "goal": "the player learns who stole the amulet", "conflict": "Tamsin is cursed and fading",
    "outcome": "she names Aldric before she sleeps", "value": "truth-lie",
    "chargeBefore": -1, "chargeAfter": 1, "location": "location/riverford-temple" },
  { "id": "q-main", "class": "Quest", "type": "main", "motivation": "protection",
    "strategy": "recover-stolen-item" },
  { "id": "q-main/20", "class": "QuestStage", "index": 20, "journal": "Tamsin named the thief." },
  { "id": "q-main/alias/thief", "class": "Alias", "kind": "character", "fill": "character/aldric" }
],
"edges": [
  { "p": "fills", "s": "character/tamsin", "o": "role/mentor" },
  { "p": "participant", "s": "s-04", "o": "character/tamsin", "role": "agent" },
  { "p": "knows", "s": "character/tamsin", "about": "aldric-stole-amulet", "true": true, "from": "s-02" },
  { "p": "advancesScene", "s": "q-main/20", "o": "s-04" }
]}
```

## 12. Open points

- **Branch-local facts at scale.** Intervals per branch are simple for branch-and-bottleneck;
  for open-map stories with many independent lines, facts may need conditions on story
  variables instead of branches. Decide in Phase 21 with a test game of each pattern.
- **How much the critic judges.** World rules, mood and tone are judged by the AI; the
  thresholds for blocking vs warning are set when we see real results.
- **Name.** GSO is a working name, like GBO.
