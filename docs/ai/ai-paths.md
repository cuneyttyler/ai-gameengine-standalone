# Paths characters follow

Status: **built (2026-11-04)**, not yet run live.

A path is a route characters walk. You draw it in the viewport, it has a name, and any number of packages can use it.
It's a small network rather than a single line: points, and links between them that fork and join again. At a fork a
character chooses its way at random, by weight, or by conditions. Each character keeps its own place on the path.

## 1. A path in the scene

A path is an entity with a **Path** component (`engine/gameplay/Paths.h`) and a `WorldTransform2D`, which is its own
place: moving it moves all its points. It's saved with the scene and is invisible in play.

| Field | Meaning |
|---|---|
| `name` | packages refer to it by this (`market-loop`) |
| `points` | JSON list, in cells from its place: `{x, y, wait, clip, use, do, label}` |
| `links` | JSON list (empty: point to point in order): `{from, to, weight, if, priority, default, do, label}` |
| `end` | `stop` (done), `loop` (the last point back to the first), `back` (the way it came) |
| `shown` | drawn in the viewport |

**A point** can say what's done there, by everyone who walks it:
- `wait` seconds;
- a `clip` played while waiting;
- a part `use`d by its name (`cook`: [ai-packages.md §6b](ai-packages.md));
- effects (`do`).

**A link** carries how it's chosen at a fork (§3), effects run when it's taken (`do`), and a `label` that packages can
prefer it by.

In code: `ReadPath` / `WritePath` (the component as a graph in the world, and back), `FindNamedPath`, `LinksFrom`,
`ChooseLink`, `LinkConditionsHold`, `PathRoute` and `CheckPath`.

## 2. In the editor

- **The Path tool** (toolbar ▸ Path):
  - click the ground to add points (cell middles), each linked from the picked point;
  - click a point to pick it, so the next click branches from it (a fork);
  - drag a point to move it;
  - Shift+click a point to link the picked point to it (a fork rejoining);
  - Enter, Esc or **Finish path** ends the path, and the next click begins a new one (`path-1`, `path-2`, …).
- **Right-click a point:**
  - Branch from here, or Draw on from here;
  - Wait here…, Play a clip here…, Use a part here…, Label…;
  - Delete point (a point in a line joins its neighbours);
  - Finish drawing.
- **Selecting:** in the Select tool, clicking a point selects its path.
- **Inspector ▸ Path** (`PathEditor.vue`):
  - its name: renaming it renames it in every package that uses it (`path:rename`);
  - its end;
  - who uses it: the packages, the characters, and any problems;
  - each point's label, wait, clip and part;
  - each link, and at a fork its weight, **first** (priority), **default**, label and conditions. The conditions box
    turns red when it isn't a JSON list.
- **In the viewport:** links are drawn with an arrow in their middle, and points as diamonds (crossed where something
  is done).
  - Link colours: cyan for an ordinary link, orange for one with conditions, red for priority, grey for the default.
  - The path being drawn, the selected path and the selected character's paths are drawn bright; others are faint.
  - **View ▸ Paths** hides all but those.
  - In Play, each character on a path has a green line to the point it's walking to.
- **Deleting a path** packages use says which packages use it (undo keeps it).

All of this is in `apps/editor/PathTool.cpp`.

## 3. Forks

At a point with several links, the character takes:
1. of the links whose **conditions** (`if`) hold, a **priority** one;
2. else one at random by **weight** (a package's `weights` by label override it);
3. else, when none holds, the **default** one;
4. else none: it's an end.

Conditions are checked when the character reaches the fork, so a change in the world on the way counts. The
conditions are dialogue's (flags, quests, items, time of day, factions, difficulty), plus three of the path's own:

| Condition | Holds when |
|---|---|
| `{"playerNear": 5}` | the player is within 5 cells of the character |
| `{"partOpen": "cook"}` | a part named so is open (the stove cooking, the shop door open) |
| `{"free": true}` | nobody else is at the link's far point (the well taken: the market instead) |

`"not": true` inverts one of them. Each character draws its own random choices (seeded, so a playtest replays).

## 4. Packages and scripts

- **Template `follow-path`:**
  - `data.route` is the path's name;
  - `data.mode` is `once` (to an end, or once round, then it stays there), `loop` (on and on), or `wander` (any way at
    every point, links walked both ways, never stopping: town folk);
  - `data.from` is `nearest` (the default), `start` or `end`.
- **Template `patrol`:** `data.route` walks a named path for ever; `data.path` is still a list of markers.
- **The step** `{"type": "follow-path", "path", "mode", "from", "stops": true, "weights": {"<label>": weight}, "hold"}` goes
  in a package's own procedure. With mode once it succeeds at the end, so the next step runs; `hold` keeps it there instead.
- **Shared:** each character keeps its own place in its own node state. It waits a moment rather than walk into
  someone just ahead of it going to the same point. Its blackboard has `path`, `pathPoint` and `pathBranch` (the
  editor's Play overlay draws from them).
- **Doors:** characters open the doors on their way.
- **Lua:** `follow_path(name[, "start"])` walks a path and does its stops. `path_route(name[, from])` gives the points of
  one walk, with each fork chosen.

## 5. AI panel

- **Context:** the scene's paths are entities with a Path component.
- **Commands:**
  - `scene.addPath {name, points, links?, end?}` adds a path, or changes the one of that name;
  - `path.around {name, entity, margin?}` makes a loop a step outside a building's footprint.
- **The prompt's PATHS section:** reuse a path that fits rather than adding one, how forks are chosen, and a worked
  example with a fork by weight and one by a flag.
- **Checks** (`ai_service/app/packages.py`):
  - a package's `data.route`, and a procedure's `follow-path` or `patrol` path, names a path in the scene or one added
    in the same answer;
  - `data.mode` is once, loop or wander;
  - `scene.addPath` has at least two points, links between points it has, weights above zero, conditions that are JSON
    lists, and an end of stop, loop or back.

## 6. Tests

- `tests/character_ai_test.cpp`:
  - PathsForkByConditionsPriorityAndWeight: reading and writing, a 3:1 weighting, a label preference, the default, a
    priority link, conditions handed on, and the checks;
  - CharactersFollowANamedPathAndChooseAtForks: a walker takes the other way with no player near and the first way
    with one, and two walkers share the path.
- `ai_service/tests/test_packages.py`: paths named by packages, and `scene.addPath`'s arguments.
