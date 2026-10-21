# Game types AIGE aims to build

Status: **first outline (2026-10-21)**, to be detailed later. Today AIGE makes 2D games
on an isometric or top-down grid. This document lists the kinds of game we want to
support, with examples from the market, what each needs from the engine, and the version
that supports it (`roadmap.md`). Each type will become a **genre profile** for the AI
Planner (`planner-schema.md` §2.10).

## 1. Overview

| # | Type | Market examples | View | AIGE version (`roadmap.md`) |
|---|---|---|---|---|
| 1 | 2D platformer | Super Mario Bros., Super Mario World, Sonic the Hedgehog, Celeste, Shovel Knight, Super Meat Boy | 2D side | v1.2 |
| 2 | Metroidvania | Super Metroid, Castlevania: Symphony of the Night, Hollow Knight, Ori and the Blind Forest | 2D side | v1.3 |
| 3 | Falling-block puzzle | Tetris, Puyo Puyo, Dr. Mario, Columns, Lumines, Tetris Effect | 2D screen | v1.1 |
| 4 | Grid and match puzzle | Bejeweled, Candy Crush Saga, Sokoban, Baba Is You | 2D screen | v1.1 |
| 5 | Shoot 'em up (scrolling shooter) | **Star Force** (Tecmo, 1984), Xevious, 1942, Gradius, R-Type, Raiden, Ikaruga, DoDonPachi | 2D top or side, auto-scrolling | v1.2 |
| 6 | Twin-stick / arena / bullet heaven | Geometry Wars, Enter the Gungeon, The Binding of Isaac, Vampire Survivors | 2D top-down | v1.2 |
| 7 | Top-down action-adventure | The Legend of Zelda: A Link to the Past, Hyper Light Drifter, Minishoot' Adventures | 2D top-down | v1.0 |
| 8 | Top-down RPG / JRPG | Chrono Trigger, Final Fantasy VI, Pokémon, EarthBound, Undertale, Sea of Stars | 2D top-down | v1.0 (turn-based battles: v1.4) |
| 9 | Life-sim / farming | Harvest Moon, Stardew Valley, Sun Haven | 2D top-down | v1.0 |
| 10 | **2D isometric** RPG and action RPG | **Diablo**, **Diablo II**, Baldur's Gate, Baldur's Gate II, Fallout, Fallout 2, Planescape: Torment, Sacred, Divine Divinity | 2D isometric | **v1.0** (a slice today) |
| 11 | 2D isometric strategy and builders | Age of Empires II, SimCity 2000, Ultima Online, Banished-like | 2D isometric | v1.5 |
| 12 | Tactics (turn-based) | Fire Emblem, Advance Wars, Final Fantasy Tactics, Into the Breach, XCOM | 2D grid or iso | v1.4 |
| 13 | Roguelike / roguelite | Rogue, NetHack, Spelunky, Dead Cells, Hades, Slay the Spire | various | v1.0 (dungeon crawlers), v1.3 (platformer roguelites) |
| 14 | Visual novel / narrative | Phoenix Wright, Doki Doki Literature Club, Florence | 2D screens | v1.4 |
| 15 | **3D isometric** (3D world, high fixed camera) | **Diablo III**, **Diablo IV**, Path of Exile, Torchlight II, Divinity: Original Sin 2, Baldur's Gate 3 | 3D, isometric camera | v1.8 (early 3D: Neverwinter Nights, Dungeon Siege), v2.0 (modern fidelity) |
| 16 | **First-person shooter** | Doom (1993), Quake, Half-Life 2, Halo, Call of Duty 4, DOOM Eternal, Dusk | 3D first person | v1.6 (Doom), v1.7 (Quake, Half-Life), v1.9 (Half-Life 2), v2.0 (modern) |
| 17 | First-person open-world RPG (**Bethesda series**) | The Elder Scrolls III: Morrowind, IV: Oblivion, V: Skyrim; Fallout 3, Fallout 4, Starfield | 3D first/third person | v1.9 (Morrowind), v2.0 (Oblivion onwards) |
| 18 | **Third-person** action / adventure | Tomb Raider, Uncharted, God of War (2018) | 3D third person | v1.7 (Tomb Raider), v2.0 (modern) |
| 19 | Third-person open-world RPG (**Witcher series**) | The Witcher, The Witcher 2, The Witcher 3: Wild Hunt, Gothic, Dragon Age: Inquisition, Elden Ring | 3D third person | v1.9 (Gothic), v2.0 (The Witcher 3) |
| 20 | Others | racing (Micro Machines 2D; 3D later), fighting (Street Fighter II), survival-crafting (Terraria, Don't Starve 2D; Minecraft 3D), tower defence (Kingdom Rush), point-and-click (Monkey Island, Broken Sword), card games (Slay the Spire) | various | racing and fighting v1.6; tower defence v1.5; point-and-click and cards v1.4; Terraria-like v1.3 |

"Diablo series" spans 10 (Diablo I–II) and 15 (Diablo III–IV); "Bethesda series" is 17.
Versions follow the order games appeared (`roadmap.md`): 3D in v1 covers games up to about
2005; modern fidelity is v2.0.

## 2. What each type needs

| Type | Rendering and camera | Physics / controller | Main systems |
|---|---|---|---|
| 1 platformer | side view, tiles, parallax, scrolling camera with look-ahead | platformer controller, one-way/moving platforms, slopes | lives, checkpoints, collectibles, enemies with patterns, levels and worlds |
| 2 metroidvania | as 1, rooms joined into a map | as 1, with abilities that open paths | ability gating (lock–key over abilities), map, bosses, saves |
| 3 falling-block | one screen, a grid, no camera | none (grid logic) | piece generator, rules, scoring, levels and speed, menus, high scores |
| 4 grid/match | one screen | none | board rules (Lua), levels, scoring, undo (Sokoban) |
| 5 shmup | auto-scrolling, layered backgrounds | free-flight, many bullet overlaps | waves and patterns, power-ups, scoring, lives, bosses, stage scripting |
| 6 twin-stick | top-down, arena camera | free-flight or top-down, bullets | waves, upgrades, rooms (Isaac), roguelite meta-progression |
| 7 action-adventure | top-down tiles, rooms or scrolling | top-down | dungeons with keys, items that open paths, puzzles, story |
| 8 RPG / JRPG | top-down | grid-step or top-down | party, turn-based or real-time combat, inventory, story, quests |
| 9 life-sim | top-down, day/night | top-down | schedules, relationships, farming, crafting, seasons, economy |
| 10 2D isometric RPG | isometric tiles, big scenes | top-down on the iso grid | combat, loot, abilities, quests, dialogue, character AI |
| 11 iso strategy | iso, pannable camera | units' steering, pathfinding at scale | units, resources, building, fog of war |
| 12 tactics | grid or iso | grid-step | turn system, initiative, line of sight on a grid |
| 13 roguelike | various | various | procedural levels (the place builder), permadeath, runs, meta-progression |
| 14 visual novel | screens, characters, backgrounds | none | dialogue and choices, variables, save/load, gallery |
| 15 3D isometric | 3D renderer, high camera | 3D character controller (Jolt) | as 10 |
| 16 FPS | 3D first person | 3D controller, hitscan and projectiles | weapons, ammo, enemies' AI, levels |
| 17 Bethesda-like | 3D open world, streaming | 3D controller, physics objects | everything in 10 plus open-world streaming, radiant quests, crime |
| 18 third-person action | 3D, orbit camera | 3D controller, animation-driven movement | combat, traversal, cinematics |
| 19 Witcher-like | 3D open world | as 17 | as 17 with a strong main story, choices and consequences |

## 3. What the engine supports, when

| Capability | Now | Version |
|---|---|---|
| 2D isometric and top-down grids | ✓ | v1.0 |
| Screen-only games (no world grid, fixed camera) | partly | v1.1 |
| 2D side view (gravity, parallax, scrolling) | — | v1.2 |
| Rigid bodies and destructible terrain as gameplay | — | Box2D in v1.0; as gameplay in v1.3 |
| Turn-based combat, tactics grids | — | v1.4 |
| Units at scale, fog of war, strategic AI | — | v1.5 |
| Pseudo-3D (raycasting, sectors, sprite scaling) | — | v1.6 |
| 3D rendering, first and third person, Jolt physics | — | v1.7 |
| 3D isometric, terrain, modular building kits | — | v1.8 |
| Open-world streaming | — | v1.9 |
| Advanced graphics | — | v2.0 |

Each type gets a genre profile, a template project and playtests of its own in the version
that supports it.

## 4. To detail later

For each type: the genre profile's defaults and asked fields, the template project, the
place builder's build types it needs (platformer levels, shmup stages, puzzle boards), the
playtest it needs (a level can be finished; a stage can be survived), and reference games
studied in depth.
