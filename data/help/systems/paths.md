# Paths

Named paths characters walk: drawing them with the Path tool, points and what's done at them, forks chosen by weight,
priority, conditions or a default, the Inspector's Path section, and using paths from packages, procedures and Lua.

## What a path is

A path is a route characters walk, drawn in the viewport and given a name (`market-loop`). Any number of packages and
characters can use the same path, each keeping its own place on it. It's a small network rather than a single line:
**points**, and **links** between them that can fork and join again. At a fork a character chooses its way at random by
weight, by priority, or by conditions.

A path is an entity with a **Path** component (and a position: moving the entity moves all its points). It's saved
with the scene and invisible in play.

| field | meaning |
|---|---|
| `name` | packages refer to it by this |
| `points` | a JSON list, in cells from its place: `{x, y, wait, clip, use, do, label}` |
| `links` | a JSON list (empty: point to point in order): `{from, to, weight, if, priority, default, do, label}` |
| `end` | at an end: `stop`, `loop` (the last point back to the first) or `back` (the way it came) |
| `shown` | drawn in the viewport |

### Example: the market loop as it's saved

A square path round the market, four points, closing on itself, as the Path component holds it:

| field | value |
|---|---|
| `name` | `market-loop` |
| `points` | `[{"x": 0, "y": 0, "label": "gate"}, {"x": 8, "y": 0}, {"x": 8, "y": 6, "label": "well", "wait": 4}, {"x": 0, "y": 6}]` |
| `links` | `[]` (point to point in order) |
| `end` | `loop` |

The town watchman's patrol package (data.route `market-loop`) and the baker's follow-path package both use it; each has
its own place on it, and both stop four seconds at the well.

Questions: what is a path? how do characters walk a route? can several characters share a path?

## Drawing a path with the Path tool

Choose **Path** on the toolbar.

- Click the ground to add points (at cell middles); each new point is linked from the picked point.
- Click an existing point to pick it, so the next click branches from it (a fork).
- Drag a point to move it.
- **Shift+click** a point to link the picked point to it (a fork joining up again).
- **Enter**, **Esc** or **Finish path** ends the path; the next click begins a new one (`path-1`, `path-2`, …).

**Right-click a point** for: Branch from here, Draw on from here; Wait here…, Play a clip here…, Use a part here…,
Label…; Delete point (a point in the middle of a line joins its neighbours); Finish drawing.

With the Select tool, clicking a point selects its path.

### Example: drawing a guard's round with a fork to the tavern

1. Choose **Path** on the toolbar.
2. Click the gate, then four more points along the wall to the square, then back towards the gate. Press **Enter**. The
   path is named `path-1`.
3. Click the square's point to pick it, then click by the tavern door twice: a branch from the square to the tavern.
4. With the tavern's last point picked, **Shift+click** the wall point after the square: the branch joins the round again.
5. Right-click the tavern door point ▸ **Wait here…**, type `10`. Right-click it again ▸ **Label…**, type `tavern`.
6. In Inspector ▸ Path, set **name** to `guard-round` and **end** to `loop`.

The square is now a fork: guards walking `guard-round` go on along the wall or detour to the tavern door for ten
seconds, then rejoin.

Questions: how do I draw a path? how do I make a fork in a path? how do I delete a point? how do I join two branches?

## Paths in the viewport and the Inspector

**In the viewport:** links are drawn with an arrow at their middle; points are diamonds (crossed where something is
done). Link colours: cyan for an ordinary link, orange for one with conditions, red for a priority link, grey for the
default. The path being drawn, the selected path and the selected character's paths are bright, others faint.
**View ▸ Paths** hides all but those. In Play, each character on a path has a green line to the point it's walking to.

**Inspector ▸ Path** (with a path selected):
- its **name**: renaming it renames it in every package that uses it;
- its **end** (stop, loop, back);
- who uses it: the packages, the characters, and any problems;
- each point's label, wait, clip and part;
- each link and, at a fork, its weight, **first** (priority), **default**, label and conditions. The conditions box turns
  red when it isn't a JSON list.

Deleting a path that packages use says which packages use it (undo brings it back).

### Example: renaming path-3 to river-walk

1. With the Select tool, click any diamond of `path-3` in the viewport.
2. Inspector ▸ Path lists who uses it: the packages `fisher-walk` and `washer-walk`, and the characters Old Ivo and
   Bryn.
3. Change **name** to `river-walk`.
4. Open Project ▸ Records ▸ package ▸ `fisher-walk`: its **data ▸ route** now reads `river-walk`.
5. To see only this path, untick **View ▸ Paths**; the selected path stays bright, others are hidden.

Questions: how do I rename a path? what do the link colours mean? how do I hide paths in the viewport? how do I see who
uses a path?

## What happens at a point

A point can say what's done there by everyone who walks the path:

- `wait`: seconds to stop;
- `clip`: an animation played while waiting (stopped when it walks on);
- `use`: an animated part used by name within 3 cells (a stove's `cook`, a `door`);
- `do`: effects, as dialogue's (set a flag, give an item, send an event);
- `label`: a name for the point.

Stops can be switched off for one walker: the `follow-path` step's `stops: false`.

Example: a market loop with a point at the well (wait 4, clip `drink`) and one at the bakery (use `door`, wait 6).

### Example: a milkmaid's round that stops, drinks and delivers

1. Draw a path `milk-round` from the dairy to the well, the bakery and back.
2. Right-click the well point ▸ **Wait here…** `4`, then ▸ **Play a clip here…** `drink`.
3. Right-click the bakery point ▸ **Use a part here…** `door`, then ▸ **Wait here…** `6`.
4. Select the path; in Inspector ▸ Path, the bakery point's row has its wait and part; in the points JSON add a `do`:

```json
{ "x": 14, "y": 3, "use": "door", "wait": 6, "label": "bakery",
  "do": [{ "op": "send_event", "event": "milk_delivered" }] }
```

Walking the round, the milkmaid drinks at the well for four seconds, opens the bakery door, waits six seconds, and the
event `milk_delivered` is sent each time (a script can count deliveries with `on("milk_delivered", ...)`).

Questions: how do I make a character stop at a point? can a character play an animation on a path? can a path point run
effects?

## Forks: how a character chooses its way

At a point with several links, the character takes:

1. of the links whose **conditions** (`if`) hold, a **priority** one ("first");
2. else one at random by **weight** among those that hold (a package's `weights` by link label override the weights);
3. else, when none holds, the **default** link;
4. else none: it's an end (stop, loop or back, as the path's `end` says).

Conditions are checked when the character reaches the fork, so changes on the way count. They are dialogue's conditions
(flags, quests, items, time of day, factions, difficulty) plus three of the path's own:

| condition | holds when |
|---|---|
| `{"playerNear": 5}` | the player is within 5 cells of the character |
| `{"partOpen": "cook"}` | a part named so is open (the stove cooking, the shop door open) |
| `{"free": true}` | nobody else is at the link's far point (the well taken: the market instead) |

`"not": true` inverts one of them. A link's `do` effects run when it's taken. Each character draws its own random
choices, seeded, so a playtest replays the same.

Example: a fork at the square: to the tavern with weight 1 and `if: [{"time": {"from": 18, "to": 23}}]`, to the market
with weight 3, and the market link as default.

### Example: a watchman's patrol on a named path with a fork

The path `town-watch` runs gate → square, and at the square forks: to the tavern (label `tavern`), or on to the market
(label `market`). Both join again at the north gate.

1. Select the path; in Inspector ▸ Path, at the square's fork:
   - the tavern link: **weight** `1`, conditions `[{"time": {"from": 18, "to": 23}}]`;
   - the market link: **weight** `3`, **default** ticked;
   - a third link to the guardhouse: **first** ticked, conditions `[{"flag": "alarm_raised"}]`.
2. The three links leaving the square (point 1) in the links' JSON (the path's other links are in the same list):

```json
[
  { "from": 1, "to": 2, "label": "tavern", "weight": 1, "if": [{ "time": { "from": 18, "to": 23 } }] },
  { "from": 1, "to": 4, "label": "market", "weight": 3, "default": true },
  { "from": 1, "to": 6, "label": "guardhouse", "priority": true, "if": [{ "flag": "alarm_raised" }] }
]
```

3. Make a package `watch-town`: template `patrol`, **data ▸ route** `town-watch`, schedule `20:00`-`06:00`.

From 18:00 to 23:00 the watchman takes the tavern way one time in four, otherwise the market; after 23:00 only the market
holds. If `alarm_raised` is set when he reaches the square, he goes to the guardhouse.

Questions: how do forks work? how do I make a character choose a branch by a condition? what does priority do? how do I
weight a branch?

## Using a path from a package

- **Template follow-path:** `data.route` is the path's name; `data.mode` is `once` (to an end, or once round, then it
  stays there), `loop` (on and on), or `wander` (any way at every point, links walked both ways, never stopping: town
  folk); `data.from` is `nearest` (default), `start` or `end`.
- **Template patrol:** `data.route` walks a named path for ever; `data.path` is still accepted as a list of markers.
- **The step** in a custom procedure: `{"type": "follow-path", "path": "market-loop", "mode": "once", "from": "nearest",
  "stops": true, "weights": {"tavern": 2}, "hold": false}`. With mode once it succeeds at the end so the next step runs;
  `hold: true` keeps it standing there instead.

Several characters share a path: each keeps its own place, and waits a moment rather than walk into someone just ahead
going to the same point. Characters open doors on their way. A point that can't be reached within 30 seconds makes the
step fail. The blackboard keys `path`, `pathPoint` and `pathBranch` say where a character is (the Play overlay uses
them).

### Example: a pilgrim who walks to the shrine once and stays, preferring the river road

1. Draw `pilgrim-way` from the village to the shrine, forking halfway into a hill road (label `hill`) and a river road
   (label `river`).
2. Make a custom package `pilgrim-go` with this procedure:

```json
{"type": "sequence", "children": [
  {"type": "follow-path", "path": "pilgrim-way", "mode": "once", "from": "start", "weights": {"river": 3, "hill": 1}},
  {"type": "say", "text": "At last."},
  {"type": "idle"}
]}
```

3. Put it in the pilgrim's Behaviour packages.

She walks from the first point, takes the river road three times in four, says "At last." at the shrine and stands
there. With `"hold": true` instead of the next steps, she'd stand at the end without speaking.

Questions: how do I make a guard walk a path? what are the follow-path modes? how do I make a character prefer one
branch? how do I stop at the end of a path?

## Paths in Lua and the AI panel

**Lua:**
- `follow_path(name[, "start"])` walks a path from its nearest point (or its first), doing its stops (move, clip, part,
  wait); returns true at the end, false if a point can't be reached. Use it in a coroutine (a scripted package's
  `run()`).
- `path_route(name[, from])` gives the points of one walk, each fork chosen as a package's would be, as a list of
  `{x, y, wait, clip, use}`.

**AI panel:**
- Its context lists the scene's paths.
- `scene.addPath {name, points, links?, end?}` adds a path, or changes the one of that name; `path.around {name, entity,
  margin?}` makes a loop just outside a building's footprint.
- Its prompt tells it to reuse a path that fits rather than adding one.
- Checks: a package's `data.route`, and a procedure's follow-path or patrol path, names a path in the scene or one added
  in the same answer; `data.mode` is once, loop or wander; an added path has at least two points, links between points
  it has, weights above zero, conditions that are JSON lists, and an end of stop, loop or back.

### Example: a ghost that walks the manor path by script, and a path round the chapel from the AI

A scripted package (`scripts/ai/ghost.lua`, template `script`):

```lua
function run()
  while true do
    if follow_path("manor-hall", "start") then
      say("Whooo...")
      wait(10)
    else
      wait(2)   -- a point couldn't be reached
    end
  end
end
```

To have the AI draw a path, select the chapel and type in the AI panel:

> "make a path called chapel-round just outside the chapel and have the monk walk it"

It uses `path.around {name: "chapel-round", entity: "chapel"}` and writes a patrol package for the monk; Diff Review
shows both. After Apply the path's diamonds appear round the chapel.

Questions: can a script walk a path? can the AI draw a path? how do I make a path around a building?
