# Game flow, saves and the world's memory

How a game made with AIGE runs as a whole (`development_plan.md` 12.4–12.5): the
title screen, new game and continue, pause, saving and loading, game over and
victory, credits, scene changes, and a world that remembers what the player did.
The code is `engine/game/` (`GameFlow`, `SaveGames`, `GameRunner`); the player
(`sandbox.exe` and exported games) runs every game through `GameRunner`.

## game.json

The game's manifest, in the project folder:

```json
{
  "schema": "aige.game/1",
  "title": "The Moon Amulet",
  "subtitle": "A short dark-fantasy adventure",
  "titleScreen": true,
  "titleBackground": "",
  "titleMusic": "assets/music/title.ogg",
  "autosave": true,
  "credits": "Made by ...",
  "genre": "arpg",
  "systems": { "combat": true, "inventory": true }
}
```

| Field | Meaning |
|---|---|
| `title`, `subtitle` | the window title and the title screen. Without one, the folder's name |
| `titleScreen` | start at the title screen; `false` goes straight into play |
| `titleMusic` | music on the title screen |
| `autosave` | save to the autosave slot every time the player changes scene |
| `credits` | the Credits screen's text |
| `genre`, `systems` | what kind of game it is and which systems are on (`game-types.md`, `engine-gaps.md` §4); the AI Planner writes them |

The start scene stays in Project Settings ▸ Game.

## The screens

```
Title ──New game──► Playing ◄──Resume── Paused (Esc)
  │  └─Continue / Load──┘  │              ├─ Save game ─► slots
  │                        │              ├─ Load game ─► slots
  └─Credits                ├─ death ──► Game over: Load last save · Start again · Quit to title
                           └─ victory ► Victory: Play again · Quit to title
```

| Screen | Items |
|---|---|
| Title | Continue (the newest save), New game, Load game, Credits, Quit |
| Paused | Resume, Save game, Load game, Quit to title, Quit game |
| Load game | every used slot: its place, when, how long played |
| Save game | the player's slots (1–6); quick and auto are written by the game |
| Game over | Load last save, Start again, Quit to title |
| Victory | Play again, Quit to title (all enemies defeated, with no quest under way) |
| Credits | the manifest's credits, Back |

Menus are driven by the input actions of the `menu` context (`ui_up`, `ui_down`,
`ui_select`, `ui_back`; `menu` — Esc or Start — opens and closes the pause menu), so
keyboard and gamepad both work. A conversation can't be paused away. The look is the
engine's built-in one until the game UI system (Phase 17) makes every screen a
customisable document.

Scene changes through gateways fade from black.

## Saves

Slots: **quick** (the `quicksave` action, F6; `quickload`, F9), **auto** (each scene
change, when `autosave` is on), and **slot1–slot6** (Save game). Each is one file:

- the player: `%LOCALAPPDATA%\AIGE\saves\<game>\<slot>.json`;
- the editor's Play: `<project>\.cache\saves\quick.json`.

```json
{
  "schema": "aige.save/2",
  "meta": { "slot": "slot1", "time": "2026-10-21 18:40:12", "place": "Riverford", "playSeconds": 3720, "title": "..." },
  "session": { "scene": "scenes/riverford.aige", "clock": 840, "story": { "...": "flags and quests" } },
  "player": "<the player entity, every component, as scene text>",
  "worlds": { "scenes/riverford.aige": { "...": "what changed there" }, "scenes/crypt.aige": { } }
}
```

A save is written to a temporary file and renamed, so a crash while saving keeps the old
one. A version-1 `quicksave.json` (Phase 6) still loads, as the quick slot.

## The world's memory

Every scene remembers what changed in it since it was loaded from its file, whether the
player comes back through a gateway or loads a game:

| Change | Kept as |
|---|---|
| an entity removed (an enemy killed, a pickup taken) | its place in the scene file |
| a field changed (health, position, a door opened later in Phase 13) | the component and field |
| a component added or removed | the component |
| something made in play (spawned) | the whole entity |

Entities are identified by their order in the scene file (`SceneOrigin`, set when a scene
is read). Editing a scene file between a save and a load can shift that order; a save then
applies to whatever stands at those places.

## In the editor

Play starts straight in the scene being edited (no title screen); Esc or Stop returns
to editing. F6 and F9 save and load the quick slot with the world's memory; travelling
between scenes remembers each one as you left it.
