# What the engine lacks for a finished game

Status: **draft for review (2026-10-21)**. This is the gap analysis behind Phases 12–24 in
`development_plan.md`. It lists what exists after Phase 11 and what is still missing
before AIGE can ship a complete game. The AI Planner (`ai-planner.md`) can only design
what the engine can run, so these gaps are filled **before** the Planner is built.

Related documents:

| Document | Subject |
|---|---|
| `game-types.md` | which kinds of game AIGE aims to support, with market examples |
| `ai-planner.md` | the AI Planner: designing a whole game from a request |
| `planner-schema.md` | the Planner's input schema (the Game Brief) and its documents |
| `story-ontology.md` | the story ontology (GSO) and the story knowledge graph |
| `ai-packages.md` | character AI: packages, behaviour, perception, scripted AI |
| `difficulty.md` | difficulty levels, scaling and assists |
| `game-ui.md` | menus, windows, HUD, notifications and themes |
| `effects-animation.md` | sprite animation, particles, screen effects, lighting, cutscenes |
| `magic.md` | abilities, spells and effects |
| `physics.md` | game physics (2D now, 3D later) |

---

## 1. What exists after Phase 11

| Area | What we have | Where |
|---|---|---|
| Rendering | 2D sprites and tile layers on Vulkan, y-sorting, isometric or top-down grid, big scenes (chunked layers, culling at 100k sprites), text | `renderer2d`, `rhi` |
| Scenes | text scene format, prefabs with overrides, multiple scenes joined by Gateways and Markers | `assets`, `gameplay` |
| Physics | custom AABB and tile collision, Obstacle footprints, A* pathfinding | `physics2d`, `gameplay/Pathfinding` |
| Combat | Health, Faction (3 teams), Stats (attack, defense, speed), Hurtbox, MeleeAttack, four status effects, knockback, a heal Pickup | `gameplay/Combat` |
| Character AI | `Follow` (walk to a target within range), `Routine` (daily schedules: stay, wander, sleep), Lua scripts | `gameplay/Schedule` |
| Story | linear quests (defeat, collect, talk, reach), dialogue trees (4 choices, flags, quest effects), the World Bible (character, faction, location, item, event, lore, style), story checks | `gameplay/Story`, `world` |
| Game UI | hard-coded overlays: banner, dialogue box, objective, clock, toast, prompt | `ui/GameUi` |
| Input | a fixed key list (WASD, arrows, E, Enter, 1–4, F-keys); Escape quits | `platform/Input.h` |
| Audio | fire-and-forget sound effects | `audio` |
| Save | one quicksave (F6/F9): scene, player position and health, clock, flags, quests; the scene reloads from disk, so enemies come back | `gameplay/GameSession` |
| Scripting | Lua per entity with properties and hot reload, about 25 functions; Lua modules adding components and commands | `scripting`, `modules` |
| Editor | docking panels, Inspector from reflection, AI panel with Diff Review, Story, World, Assets (catalog, library), Build panel, Ontology dialog | `apps/editor` |
| AI building | the whole place pipeline: Brief → Spec → PLL → solve → scene → verify, eight build types, asset sourcing with licences | `places`, `editing` |
| Export | desktop export with credits | `assets/Export` |

This is enough for a small top-down ARPG slice: walk, fight, talk, follow a linear quest.
It is not enough for a finished game of any genre in `game-types.md`.

## 2. The gaps

Each gap names what a finished game needs, why, and the phase that fills it. "Planner"
says what the AI Planner designs with it. Priorities: **P0** blocks any complete game;
**P1** blocks the target genres (ARPG, RPG, life-sim); **P2** widens the genres.

### 2.1 Foundations (Phase 12)

| Gap | Why it's needed | Priority |
|---|---|---|
| **Records** (typed game data) | Items, spells, classes, loot tables, factions, difficulty presets and AI packages are *data*, not entities in a scene. Today there is nowhere to keep them except prefabs. Every engine has this: Unity ScriptableObjects, Unreal Data Assets and Data Tables, Creation Kit's object records, Godot Resources. | P0 |
| **Records panel** (the object window) | Browse, filter, edit and duplicate records of every type, generated from their schema, like the Inspector. | P0 |
| **Events** | Quests, UI, AI and effects all need to react to things that happen (`on_death`, `on_hit`, `on_pickup`, `on_enter`, `on_interact`, `quest_stage`). Today each system polls. A typed event bus, visible to Lua, records and the UI. | P0 |
| **Lua: events, timers, coroutines** | `on(event, fn)`, `after(seconds, fn)`, `wait(seconds)` inside coroutines. Without them, scripted behaviour is a state machine in `update`. | P0 |
| **Per-frame module systems** | `modules.md` "Not yet". A module's Lua `system(dt)` runs every step over its components. | P1 |
| **Input actions** | Named actions (`move`, `attack`, `interact`, `menu`) bound to keys, mouse and gamepad; rebinding at run time; contexts (gameplay, menu, dialogue). Fixed keys can't ship. | P0 |
| **Gamepad** | Expected on desktop, required for any console-like genre. | P1 |
| **Game flow** | Title screen → New game / Continue / Load → play → pause → game over / victory → credits. Scene transitions with a fade. Today Play starts in a scene and Escape quits. | P0 |
| **Save system v2** | Slots, autosave, save metadata (time, place, a thumbnail), and **world-state persistence**: killed enemies stay dead, opened chests stay open, moved objects stay moved, per scene. Versioned saves. | P0 |
| **Audio v2** | Music (streamed, looping, cross-fades, by place and by state such as combat), ambience, buses (master, music, effects, voice, UI) with volumes, 2D positional sound, ducking under dialogue. | P0 |
| **Camera v2** | Bounds per scene, dead zones, look-ahead, zoom levels, shake (`effects-animation.md`), cinematic moves for cutscenes. | P1 |
| **Localization** | Every player-facing string through string tables (`loc/<lang>.json`), fonts with the needed glyphs, a language setting. Dialogue, quests, UI and records use string keys. Retrofitting this later costs far more. | P1 |
| **Runtime settings** | Resolution, window mode, vsync, volumes, language, controls, accessibility, difficulty, saved per player. | P0 |

### 2.2 Character and gameplay systems (Phase 13)

| Gap | Why | Priority |
|---|---|---|
| **Attributes** | One general attribute model (health, mana, stamina, strength…) with base values and modifiers, replacing the fixed `Stats`. The difficulty system, magic, items and progression all modify attributes. Model: Unreal's Gameplay Ability System attributes, Creation Kit actor values. | P0 |
| **Progression** | Experience and levels, skills, perks or talent trees, as records. Optional per game. | P1 |
| **Items** | Item records (kind, value, weight, stack size, icon, equip slot, effects), world pickups that refer to them. | P0 |
| **Inventory and equipment** | Per-character inventory, equipment slots changing attributes and visuals, containers (chests, corpses), currency. | P0 |
| **Loot and economy** | Loot tables (weighted, levelled, nested; like Creation Kit's levelled lists and Diablo's treasure classes), merchants, prices. | P1 |
| **Interactables** | One `Interactable` component (verb, prompt, conditions, effects): doors (locked by key or condition), levers, switches, chests, beds, signs, readable books. The dungeon builder already plans locks and keys; the runtime has nothing to run them. | P0 |
| **Combat v2** | Ranged attacks and projectiles, area attacks, damage types and resistances, blocking and dodging, combat styles as records. | P1 |
| **Factions and relations** | Named factions (records) with relations between them and a disposition per character towards the player, instead of three teams. Crime and reputation where the game wants them. | P1 |
| **Spawners and encounters** | Spawn points with encounter tables, respawn rules, level ranges (encounter zones). | P1 |

### 2.3 Character AI (Phase 14) — `ai-packages.md`

| Gap | Why | Priority |
|---|---|---|
| **AI packages** | What a character does, when and where: travel, sandbox, sleep, eat, work, patrol, guard, follow, escort, flee, ambush, use an object. A stack of packages chosen by conditions and schedule, as in the Creation Kit. `Routine` becomes one kind of package. | P0 |
| **Behaviour runtime** | `technical.md` §6 lists a `behavior_graph` module that was never built. Packages run on a small behaviour-tree runtime; scripted packages run Lua. | P0 |
| **Perception** | Sight (cone and line of sight over the tile grid), hearing (noise events), memory of the last known position. | P1 |
| **Combat AI** | Engage, keep distance, flank, retreat at low health, use abilities, call for help. | P1 |
| **Smart objects** | Beds, chairs, counters, forges, wells announce what can be done with them; packages use them ("sandbox in the tavern" sits on chairs and drinks at the counter). Place builds create them. | P1 |
| **Companions** | Follow, wait, fight beside the player, inventory sharing. | P2 |

### 2.4 Narrative runtime v2 (Phase 15)

| Gap | Why | Priority |
|---|---|---|
| **Quest stages and branches** | Today a quest is a fixed list of objectives in order. Games need stages with several objectives, optional objectives, branches (help or betray), failure, and several endings. Creation Kit quests are stage-indexed with conditions; we follow that. | P0 |
| **Quest aliases** | A quest refers to *roles* ("the questgiver", "the bandit leader", "the hideout") filled by entities when the quest starts, not to fixed prefab paths. This is what makes generated quests placeable (`ai-planner.md` §9) and quests reusable (Creation Kit reference and location aliases, Radiant quests). | P0 |
| **Rewards** | Items, currency, experience, reputation, unlocking places or dialogue. | P0 |
| **Journal and markers** | A quest journal (`game-ui.md`), objective markers on the map, compass or screen edge. | P0 |
| **Story variables** | Typed variables (int, float, string, enum) as well as flags; conditions on attributes, items, factions, time, quest stages. | P0 |
| **Dialogue v2** | Conditions and effects over the new state (items, attributes, skill checks), more than four choices with scrolling, portraits, barks (one-line remarks without a conversation), voice lines, and a node-graph editor. | P1 |
| **Cutscenes** | Scripted sequences: move actors, play animations, camera moves, dialogue lines, waits, fades, music. Unity Timeline, Unreal Sequencer and Creation Kit Scenes do this. The story needs it for its key scenes. (`effects-animation.md` §8.) | P1 |

### 2.5 Difficulty (Phase 16) — `difficulty.md`

Nothing exists. Needed: difficulty levels, per-level multipliers on any reflected field,
per-character overrides, assists, optional adaptive difficulty, level scaling, and a panel
to tune it. P0 for any shipped game.

### 2.6 Game UI (Phase 17) — `game-ui.md`

The game UI is six hard-coded overlays. Needed: a data-driven UI with documents, styles,
themes and data binding; the menus (title, pause, settings, save/load), windows
(inventory, journal, map, character, dialogue, shop), the HUD, notifications and pop-ups;
a UI designer panel; gamepad and keyboard navigation. P0.

### 2.7 Animation and effects (Phase 18) — `effects-animation.md`

There is **no sprite animation at all**: every entity is one still image. Needed:
animation clips from sheets and Aseprite files, animation state machines with events,
directional sets, tweens, particles, screen effects (shake, flash, hit-stop), 2D lighting,
day and night tint, weather, and cutscenes. P0 (animation), P1 (the rest).

### 2.8 Abilities and magic (Phase 19) — `magic.md`

Only melee attacks with one optional status effect. Needed: abilities with costs,
cooldowns, targeting and delivery; effects as records that modify attributes or run
scripts; spells, enchantments, potions and scrolls all made of the same effects. P1.

### 2.9 Physics v2 (Phase 20) — `physics.md`

The AABB and tile system is enough for top-down ARPGs. Platformers, shooters, puzzle and
physics games need rigid bodies, triggers, collision layers, raycasts, one-way and moving
platforms, slopes and genre character controllers. P2 for the current genres; the
side-view parts move to v1.2 with the platformer genres (`roadmap.md`).

### 2.10 Shipping quality (Phase 24)

| Gap | Why | Priority |
|---|---|---|
| **Accessibility** | Subtitles, text size, colour-blind modes, remapping, hold-to-toggle, screen shake off, assists (with `difficulty.md`). | P1 |
| **In-game debug console** | Run Lua, set variables, teleport, spawn, toggle god mode; stripped from exports or behind a flag. | P1 |
| **Profiler overlay** | Frame time per system, entity counts, draw calls. F3 shows frame stats now. | P2 |
| **Achievements and statistics** | A local system with a platform layer for Steam later. | P2 |
| **Export polish** | Icon, splash, version, window title, installer or archive, crash log. | P1 |
| **Streaming big scenes** | Load regions by camera (7.2 "not yet"). Needed for open-world regions. | P2 |

### 2.11 Gaps in existing systems found while designing the Planner

| Gap | Fix | Phase |
|---|---|---|
| Place builds don't leave **markers or smart objects** for what they built (doors, counters, beds, gates, the centre of a square) | each module declares `actorSlots` and `anchors`; the scene build writes Markers and smart objects there, tagged with `PlaceNode` | 14 |
| A build can't **run on its own**: the Build panel waits for the user at each step | an automatic driver that advances the steps when each is ready (`ai-planner.md` §8) | 22 |
| A build's request comes from chat text only | a build can start from a **pre-filled Brief** (from a place description), skipping Interpret | 22 |
| The World Bible has no structured facts beyond `rel:` lines | the story knowledge graph (`story-ontology.md` §6) holds them; `rel:` lines are kept in step with it | 21 |
| `Talk.character` and quest targets refer to fixed keys or prefab paths | aliases (2.4) | 15 |
| Gateways are placed by hand | the Planner's world graph places them in the place builds (`ai-planner.md` §8.4) | 23 |
| No project-level **game manifest** saying what the game is (genre, systems on, start scene, title screen) | `game.json`, written by Project Settings and by the Planner | 12 |

## 3. The order, and why

```
12 Foundations ─► 13 Gameplay systems ─► 14 Character AI ─► 15 Narrative v2
                                              │                  │
16 Difficulty ◄───────────────────────────────┴──────────────────┘
17 Game UI ─► 18 Animation & effects ─► 19 Abilities & magic ─► 20 Physics v2
21 Story ontology ─► 22 AI Planner I (design) ─► 23 AI Planner II (build) ─► 24 Shipping
```

- **Records and events first.** Almost every later system is records plus events: items,
  spells, packages, loot, difficulty presets, UI bindings.
- **Systems before the Planner.** The Planner writes records, quests, packages, UI documents
  and difficulty presets. Each must exist, with its schema in the reflection registry,
  before the AI can generate it. This is the same rule as Phase 7: the schemas first.
- **Difficulty after the systems it scales** (attributes, AI, spawns, items).
- **UI before animation**: menus are needed by every game; most animation needs the UI's
  tweening anyway.
- **Physics v2 is late** because the target genres don't need much of it: v1.0 gets layers,
  sensors, queries, top-down and grid-step controllers and Box2D; side-view platformer
  physics comes with v1.2 (`roadmap.md`).
- **The story ontology is designed early** (this document set) but built in Phase 21, just
  before the Planner that uses it, once quests v2 and AI packages fix what it must describe.

## 4. Principles every new system follows

1. **Reflection first.** Every component, record type and setting is registered with
   types, ranges and descriptions, so the Inspector, the Records panel, Lua and the AI see
   it with no extra work (`technical.md` §7).
2. **Data, then script.** The common case is data (a record, a package, a UI document);
   Lua is the escape hatch for the rest (`product.md` §5.2).
3. **Plain project files.** Records, UI documents, packages and planner documents are
   JSON in the project, diffable, editable by hand, valid with AI off.
4. **Checked.** Each file type has a validator used by the editor, the tests and the AI
   service (as `story.md` "Checks" does today).
5. **Traceable.** Generated files carry where they came from (`provenance`: the planner
   document and node, the build, the AI call), like `PlaceNode` for built entities.
6. **Genre-neutral.** Systems are switched on per game in `game.json`; a Tetris clone has
   no inventory and pays nothing for it.
