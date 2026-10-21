# AIGE roadmap — v1 to v2

Status: **sketch for review (2026-10-21)**. Versions of the product, from the first release
to a finished, general-purpose engine. The engineering detail of each version is written in
`development_plan.md` when its work starts; the kinds of game are in `game-types.md`.

## 1. The idea

- **AI is in every version.** AIGE is AI-native (`product.md` §3): no version ships a
  feature, genre or asset type the AI can't work with. Each version extends the AI to what it
  adds (§2.1), and its "done when" includes games made by the AI Planner.
- **v1.0** is AIGE's first release: **2D isometric and top-down games**, with the AI
  Planner, the place builder and AI editing everywhere.
- **v1.1 to v1.9** widen the engine **genre by genre, in the order games themselves
  appeared**: from the first single-screen arcade games to the open-world 3D games of the
  early 2000s. Each version is a step in the history of games, so each builds on the
  previous one the way the industry did: scrolling came after single screens, pseudo-3D
  came before true 3D, open worlds came last.
- **3D in v1 means early 3D**: the techniques and scale of games up to about 2005 (baked
  lighting, simple materials, skinned characters, streamed worlds of that period's size).
- **v2.0** is the finished, Unity/Unreal-like engine, starting with **advanced graphics**
  (physically based rendering, real-time lighting and the rest of modern rendering) and
  the maturity a public engine needs.

Every version ships the same way: the engine features, the editor tools, and **the AI for
all of it** (below), with sample games made both by hand and by the Planner.

**The AI definition of done**, required of every version:
1. **AI editing:** every new component, record type, setting and tool is in the reflection
   registry, so the AI panel can read and change it, and it is in the AI's context.
2. **Planner:** a genre profile for each new genre; the Planner makes a game of that genre
   from one request.
3. **Generators:** build types for the genre's levels (a platformer level, a shmup stage, an
   RTS map, a 3D interior), with their checks.
4. **Assets:** the AI sources or generates the version's new kinds of asset (§2.1).
5. **Playtest:** a bot that plays the genre and reports whether its games can be finished.
6. **Off switch:** everything still works with AI off (`product.md` §4.1).

## 2. Overview

| Version | Theme | Era of the games | Reference games | New in the engine |
|---|---|---|---|---|
| **v1.0** | 2D isometric and top-down | 1986–2000 (and modern 2D) | Zelda: A Link to the Past, Ultima, Diablo, Fallout, Baldur's Gate, Stardew Valley | everything in Phases 0–24 |
| **v1.1** | Single-screen arcade and puzzle | 1972–1985 | Pong, Breakout, Space Invaders, Asteroids, Pac-Man, Frogger, Donkey Kong, Tetris, Sokoban, Boulder Dash | free-coordinate screen games, arcade framework, rules-first Planner |
| **v1.2** | Scrolling 2D: shooters, platformers, beat 'em ups | 1981–1991 | Defender, Xevious, **Star Force**, 1942, Gradius, R-Type, Pitfall!, **Super Mario Bros.**, Mega Man, Commander Keen, Double Dragon | side view, scrolling, platformer physics, wave and pattern scripting |
| **v1.3** | Exploration and physics in 2D | 1986–1997 (and modern) | Metroid, Castlevania: Symphony of the Night, Prince of Persia, Lemmings, Worms; Hollow Knight, Spelunky, Terraria | room worlds with ability gates, rigid bodies, destructible terrain |
| **v1.4** | Turns, parties and stories on screens | 1986–1998 | Dragon Quest, Final Fantasy VI, Chrono Trigger, Fire Emblem, X-COM: UFO Defense, Maniac Mansion, Monkey Island, visual novels | turn-based combat, tactics grids, point-and-click adventures, mobile export |
| **v1.5** | Strategy and simulation | 1989–2000 | SimCity, Civilization, Dune II, Warcraft II, Command & Conquer, Age of Empires II, Caesar III, Theme Hospital | units at scale, fog of war, economies, strategic AI, lockstep multiplayer |
| **v1.6** | Pseudo-3D | 1982–1995 | Pole Position, OutRun, F-Zero (Mode 7), **Wolfenstein 3D**, **Doom**, Duke Nukem 3D; Street Fighter II | first-person camera, raycast and sector rendering, sprite scaling, fighting games |
| **v1.7** | True 3D | 1996–2000 | **Quake**, **Tomb Raider**, Super Mario 64, Half-Life, Thief, Unreal, Ocarina of Time | the 3D renderer, 3D editor, Jolt physics, skeletal animation, navmesh, 3D AI building, online multiplayer |
| **v1.8** | 3D isometric and 3D RPGs | 2000–2005 | Neverwinter Nights, Dungeon Siege, Warcraft III, Age of Mythology, Titan Quest | 3D terrain, modular building kits, high cameras, the place builder and Planner in 3D |
| **v1.9** | Open worlds of the early 2000s | 2001–2005 | **Gothic**, Gothic II, **Morrowind**, GTA III, Max Payne, Half-Life 2, Far Cry, Fable | world streaming, LOD, vehicles and ragdolls, first/third-person RPG systems at world scale |
| **v2.0** | The finished engine | modern | The Witcher 3, Skyrim, Diablo IV, Baldur's Gate 3, modern shooters | advanced graphics, mature tools, platforms, ecosystem |

### 2.1 AI in every version

| Version | Planner and generators | AI assets | Playtest bot | AI in shipped games (optional, `product.md` §5.4) |
|---|---|---|---|---|
| **v1.0** | the AI Planner (story, characters, quests, places, systems, difficulty, UI); the place builder's 8 build types | sprites, tiles, UI themes, portraits | walks quests, checks reachability | — |
| **v1.1** | rules-first Planner: game rules written as Lua with tests; solvable puzzle boards; wave formations | arcade sprites, vector-style art, sound effects | plays by heuristics (survive, clear, score) | adaptive arcade difficulty |
| **v1.2** | platformer levels (gaps checked by jump arc), shmup stages (wave timelines), beat 'em up stages, bullet and boss patterns | animated sprite frames, parallax backgrounds, music loops | finishes levels, survives stages | — |
| **v1.3** | metroidvania worlds (mission and space graphs with ability locks), roguelite runs, physics puzzles | destructible terrain sets | explores with abilities, solves physics puzzles | procedural runs at play time |
| **v1.4** | battle balance, tactics maps, adventure puzzles from dependency charts, branching visual-novel stories | character art for battles, backgrounds, voice lines | wins battles, solves adventure puzzles | runtime dialogue with fallback to authored lines |
| **v1.5** | strategy maps with balance checks, faction and unit balance, opponent strategies as data | unit and building sets with facings | AI-versus-AI matches | the strategic opponent AI |
| **v1.6** | dungeons and castles as first-person sector levels, racing tracks, fighting rosters with frame-data balance | wall and floor textures, sprites from 8 angles | finishes FPS levels and races | — |
| **v1.7** | the place builder in 3D (heights, floors, brush interiors), FPS and third-person levels | 3D models, textures, rigging and animation clips (library and text-to-3D, with a critic) | navigates 3D levels, fights | — |
| **v1.8** | settlements, castles and regions emitted in 3D (terrain, building kits); 2D projects lifted to 3D | terrain materials, modular kits, vegetation | as v1.0, in 3D | — |
| **v1.9** | region-scale worlds with streaming cells; open-world quest density, factions, radiant quests | world-scale asset sets, vehicles | travels the world, completes quests | radiant quests from aliases |
| **v2.0** | the Planner and builders at modern scale and fidelity | PBR materials, high-detail models, motion and facial animation | as above, at scale | richer runtime characters, still with fallbacks |

## 3. The versions

### v1.0 — 2D isometric and top-down

**Scope:** `development_plan.md` Phases 0–24, the engine's gaps (`engine-gaps.md`) and the
AI Planner (`ai-planner.md`), for top-down and isometric games.
- **Genres:** top-down action-adventure, top-down RPG (real-time combat), isometric action
  RPG and RPG, life-sim, dungeon crawlers (`game-types.md` types 7, 8, 9, 10, part of 13).
- **Engine:** records, events, game flow, saves, audio, items and inventory, character AI
  packages, quests v2, difficulty, the customisable game UI, sprite animation and effects,
  abilities and magic, physics layers with top-down and grid-step controllers and Box2D.
- **AI:** AI editing of everything, the place builder (eight build types), asset sourcing,
  the story ontology, the AI Planner from request to playtested game.
- **Release:** desktop (Windows first, then macOS and Linux), documentation, template projects.

**Done when:** a short isometric action RPG and a top-down life-sim are each made by hand
and by the Planner, exported, and played through.

### v1.1 — Single-screen arcade and puzzle (1972–1985)

The first video games: one screen, simple rules, score and lives.
- **Genres and references:** bat and ball (Pong, Breakout/Arkanoid), fixed shooters (Space
  Invaders, Galaxian), free-movement arcade (Asteroids), maze (Pac-Man), lane crossing
  (Frogger), single-screen platformers (Donkey Kong), falling-block and grid puzzles
  (Tetris, Columns, Sokoban, Boulder Dash), match puzzles (Bejeweled, later in time but
  the same tools).
- **Engine:** screen games without the tile grid (free pixel coordinates, fixed camera);
  the **arcade framework**: score, lives, levels and speed-ups, high-score tables, attract
  mode, continues; grid boards with rule scripting; simple bounce physics; wave
  formations; vector-style rendering (lines, glow) and retro filters (CRT, scanlines).
- **Editor:** a board and screen editor, a rules view (the game's Lua rules with their
  states), a wave editor.
- **AI:** the Planner's **rules-first** path (`ai-planner.md` §12): for games defined by
  their mechanics rather than a story, the Outline is a game structure (rules, modes,
  levels, scoring) and the AI writes the rules as Lua with tests; **playtest bots** that
  play by heuristics (survive, clear a level, reach a score) and report difficulty; level
  generators for puzzle boards with solvability checks (a Sokoban level must be solvable).

**Done when:** a Breakout, a Space Invaders, a Pac-Man-like and a Tetris-like game are each
made by the Planner from one request and play correctly.

### v1.2 — Scrolling 2D (1981–1991)

The screen starts to move: scrolling shooters, side-scrolling platformers and fighting
in the street.
- **Genres and references:** scrolling shooters (Defender, Xevious, **Star Force**, 1942,
  Gradius, R-Type), side-scrolling platformers (Pitfall!, **Super Mario Bros.**, Mega Man,
  Commander Keen), beat 'em ups (Kung-Fu Master, Double Dragon, Final Fight).
- **Engine:** side view with gravity; the **platformer controller** (`physics.md` §5:
  coyote time, jump buffering, slopes, one-way and moving platforms); free-flight
  controller; auto-scrolling and look-ahead cameras; parallax layers; bullet patterns
  (emitters, spirals, aimed shots) and enemy **wave timelines**; boss phases; checkpoints;
  local co-op (two players on one screen).
- **Editor:** a side-view level editor (tiles, entities, camera paths), a wave and pattern
  timeline, the jump preview.
- **AI:** new place-builder build types: **platformer level** (sections with rhythm, gaps
  checked against the jump arc), **shmup stage** (a timeline of waves over a scrolling
  background), **beat 'em up stage**; enemy pattern generation; genre profiles and
  playtest bots (a level can be finished, a stage survived at each difficulty).

**Done when:** a Star Force-like shooter and a Super Mario-like platformer of several
levels are made by the Planner and finished by their playtest bots.

### v1.3 — Exploration and physics in 2D (1986–1997, and their modern successors)

Worlds that open up with abilities, and physics as gameplay.
- **Genres and references:** metroidvanias (Metroid, Castlevania: Symphony of the Night,
  later Hollow Knight), cinematic platformers (Prince of Persia, Another World), physics
  and destruction (Lemmings, Worms), 2D sandboxes (Terraria), roguelite platformers
  (Spelunky).
- **Engine:** room-based worlds with a map; ability gates; rigid bodies, joints and ropes in
  play (Box2D); destructible and diggable terrain (cell-based); swimming, climbing, ledge
  grabs; rotoscope-style animation support (many frames, root motion).
- **AI:** metroidvania world generation (Dormans' mission and space graphs with ability
  locks, which the place builder's lock–key logic already starts); procedural runs for
  roguelites; physics puzzle generation with solvability checks.

**Done when:** a small metroidvania (map, three abilities that open new areas, bosses) and a
Worms-like physics game are made by the Planner and played through by their playtest bots.

### v1.4 — Turns, parties and stories on screens (1986–1998)

The classic console RPG, tactics and adventure games.
- **Genres and references:** JRPGs with turn-based battles (Dragon Quest, Final Fantasy VI,
  Chrono Trigger), tactics (Fire Emblem, Advance Wars, X-COM: UFO Defense, Final Fantasy
  Tactics), point-and-click adventures (Maniac Mansion, The Secret of Monkey Island),
  visual novels, card battlers.
- **Engine:** turn-based combat as a combat mode (battle scenes, initiative, party, targets,
  menus); tactics on grids and isometric maps (movement ranges, line of sight, cover);
  point-and-click (verbs, combining items, hotspots, scaling characters by depth); card
  and deck systems; **mobile export** (these genres suit touch).
- **AI:** battle balance from the difficulty model; tactics map generation; adventure
  puzzles from **puzzle dependency charts** (Ron Gilbert's method: every puzzle's
  prerequisites checked for solvability, as quest completability is); branching stories
  for visual novels on the story ontology.

**Done when:** a JRPG with turn-based battles, a tactics campaign and a point-and-click
adventure are made by the Planner and completed by playtest; one runs on a phone.

### v1.5 — Strategy and simulation (1989–2000)

Many units, economies and opponents that think.
- **Genres and references:** city and management sims (SimCity, Caesar III, Theme
  Hospital), 4X (Civilization), real-time strategy (Dune II, Warcraft II, Command &
  Conquer, StarCraft, Age of Empires II), tower defence.
- **Engine:** selection and commands for many units; pathfinding at scale (flow fields,
  formations); fog of war; resources and production chains; building placement; simulation
  ticks; a **strategic AI** for opponents (build orders, attack waves, utility-based
  decisions); **lockstep multiplayer** over LAN and internet; replays.
- **AI:** strategy maps from the place builder (resources, start positions, balance
  checked by symmetry and distance); faction and unit balance; opponent behaviour
  generated as data; playtest by AI-versus-AI matches.

**Done when:** an Age of Empires II-like RTS with two factions and an opponent AI, and a
SimCity-like builder, are made by the Planner and played (the RTS against its AI and in
multiplayer, and in AI-versus-AI playtests).

### v1.6 — Pseudo-3D (1982–1995)

3D-looking games drawn with 2D techniques: the bridge to 3D, using v1's 2D tools.
- **Genres and references:** sprite-scaling racing (Pole Position, OutRun), Mode 7
  (F-Zero, Super Mario Kart), raycasting (**Wolfenstein 3D**), sector-based 2.5D shooters
  (**Doom**, Heretic, Duke Nukem 3D); also 2D fighting games of the same era (Street
  Fighter II, Mortal Kombat).
- **Engine:** a first-person camera over a 2D map; a raycast renderer (walls on a grid)
  and a **sector renderer** (floors and ceilings at heights, doors, lifts); billboard sprites
  with angles; sprite-scaling roads; fighting-game systems (frame data, hitboxes per frame,
  input buffers and motions, rollback-ready determinism).
- **Editor:** maps edited in 2D with heights (as Doom's editors did), previewed in first
  person.
- **AI:** the place builder's dungeons and castles **become first-person levels** (rooms →
  sectors with heights, locks and keys → coloured key doors); FPS genre profile; racing
  tracks; fighting rosters with frame-data balance checks.

**Done when:** a Doom-like episode of several levels (generated from the place builder) and
an OutRun-like racer are made by the Planner and finished by playtest bots.

### v1.7 — True 3D (1996–2000)

The 3D engine arrives, at the level of the first true-3D games.
- **Genres and references:** first-person shooters (**Quake**, Half-Life, Unreal), stealth
  (Thief), third-person action-adventure (**Tomb Raider**, Ocarina of Time), 3D platformers
  (Super Mario 64).
- **Engine:** `renderer3d` on the RHI (meshes, materials, **baked lightmaps**, dynamic
  lights, skyboxes, fog, particles in 3D); glTF import; the **3D editor** (viewport, gizmos,
  brush/CSG level building for interiors, prefabs in 3D); **Jolt** physics; 3D character
  controllers (first person, third person, 3D platformer); **skeletal animation** (skinning,
  blending, state machines from v1.0's animators); 3D cameras; 3D audio; **navmeshes**
  (Recast/Detour) for character AI; **online client-server multiplayer** (the Quake model).
  Every system of v1.0 (records, quests, AI packages, UI, difficulty, magic) works in 3D
  unchanged, through reflection.
- **AI:** the place builder in 3D (PLL and solvers with heights and floors; interiors with
  brushes and modular pieces); 3D asset sourcing (libraries, text-to-3D generation with a
  critic); FPS and third-person level generation; the Planner for 3D action games.

**Done when:** a Quake-like shooter (single player and online deathmatch) and a Tomb
Raider-like adventure are made, by hand and by the Planner.

### v1.8 — 3D isometric and 3D RPGs (2000–2005)

v1.0's own genres, in 3D.
- **Genres and references:** 3D isometric RPGs and action RPGs (Neverwinter Nights,
  Dungeon Siege, Titan Quest), 3D RTS (Warcraft III, Age of Mythology).
- **Engine:** **terrain** (heightmaps, texture splatting, water planes); modular building
  kits (walls, roofs, floors snapping on a grid); high and rotatable cameras; mouse-driven
  3D movement; vegetation instancing; character customisation (equipment shown on skinned
  models).
- **AI:** the place builder's settlements, castles and regions **emitted in 3D** (terrain,
  kits, props) from the same plans; 2D projects can be lifted to 3D with matching asset
  sets; the Planner's RPG profile in 3D.

**Done when:** an isometric 3D action RPG with a town, a dungeon and a boss, and a Warcraft
III-like skirmish, are made by the Planner.

### v1.9 — Open worlds of the early 2000s (2001–2005)

The end of v1: large connected 3D worlds at the scale of their time.
- **Genres and references:** open-world RPGs in first and third person (**Gothic**, Gothic II,
  **Morrowind**, Fable), open-world action (GTA III), physics-driven shooters (Max Payne,
  Half-Life 2, Far Cry).
- **Engine:** **world streaming** (cells or regions loaded by distance), level of detail for
  meshes and terrain, occlusion culling; day and night and weather in 3D; vehicles;
  ragdolls and physics props as gameplay; first/third-person switch; dialogue cameras;
  saves of large persistent worlds; character AI packages over a whole world (persistent
  actors at scale).
- **AI:** region-scale 3D world building (the place builder's region type with streaming
  cells); the Planner's open-world RPG profile: quest density over a world, factions,
  radiant quests from aliases; playtest bots that travel the world.

**Done when:** a Gothic-sized open-world RPG (a valley with towns, camps, dungeons, factions
and a main story) is planned and built by the Planner and played through by its playtest
bot, in first and third person.

### v2.0 — The finished engine

AIGE published as a general-purpose engine alongside Unity and Unreal, starting with
**advanced graphics**:
- **Rendering:** physically based materials, real-time global illumination, cascaded and
  soft shadows, HDR and a full post-processing stack, volumetric fog and clouds,
  screen-space and ray-traced effects where hardware allows, GPU-driven rendering, virtual
  texturing, large-scale foliage.
- **Animation and physics:** motion matching, inverse kinematics, retargeting, facial
  animation; destruction, cloth.
- **Worlds:** world partitioning for very large worlds; procedural terrain and
  biomes at scale.
- **Tools and ecosystem:** a mature asset pipeline, plugins and an extension marketplace,
  collaboration and version control in the editor, profiling tools, public documentation
  and samples, licensing.
- **Platforms:** desktop, mobile and the web; consoles where licences allow.
- **AI:** the Planner and builders at modern scale and fidelity (games like The Witcher 3,
  Skyrim, Diablo IV, Baldur's Gate 3 as targets), with the art pipeline producing modern 3D
  assets.

v2 will be broken into its own versions when v1.9 is done.

## 4. Across every version

| Track | What each version adds |
|---|---|
| **AI** | in every version, by the AI definition of done (§1, §2.1): AI editing of every new system, a genre profile, generators, AI assets for the new asset kinds, a playtest bot |
| **Planner** | the new genres from one request, by the same procedure (`ai-planner.md`) |
| **Templates** | a template project per genre, made with the engine's own tools |
| **Compatibility** | projects from earlier versions open and play unchanged; every earlier genre's samples stay in the test suite |
| **Platforms** | Windows → macOS and Linux (v1.0) → mobile (v1.4) → web and consoles (v2) |
| **Multiplayer** | local co-op (v1.2) → lockstep (v1.5) → client-server online (v1.7) |

## 5. How this maps to the development plan

- **v1.0** = Phases 0–24 of `development_plan.md`, for top-down and isometric games. Two
  items there move to later versions: the side-view part of Phase 20 (20.4, the platformer
  and free-flight controllers) goes to **v1.2**, and the Planner profiles for games
  without a story (23.5) go to **v1.1**.
- Each later version gets its phases in `development_plan.md` when its work starts, as
  phases after 5 have been written so far.
