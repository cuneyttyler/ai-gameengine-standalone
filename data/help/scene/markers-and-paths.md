# Markers and paths

How to add, find and use markers (named places), and how to draw paths that characters follow: the Path tool, forks
and joins, what happens at each point, choosing a way at a fork by weight, conditions, priority or default, the
Inspector's Path editor, and how packages and scripts use paths.

## Markers: named places

A **Marker** is a named place in the scene (`village_well`, `market`, `cave_entrance`), invisible in play. Gateways
land the player at a marker; packages, schedules and Behaviour's **home**, **work** and **bed** send characters to
markers.

- **Marker tool** (toolbar ▸ **Marker**): click a cell and give a name ("A name for this place, for packages, gateways
  and schedules to send people to"); click an existing marker to select it, then drag it with the Select tool.
- Right-click the ground ▸ **Add a marker here…**.
- **◇ Markers** (toolbar toggle) shows or hides markers in the viewport; a selected marker always shows, as a yellow
  cell outline.
- **Go to marker…** (toolbar, a searchable list of the scene's markers) selects one and centres the view on it.
- Rename a marker in Inspector ▸ Marker ▸ **name**. Fields that name a marker (Gateway, Behaviour, Building Plan) pick
  from the scene's markers.

### Example: a home, a workplace and a bed for Nell the weaver

1. Pick the toolbar's **Marker** tool. Click the cell inside Nell's cottage door and name it `nell_home`; click the
   cell by her loom in the market and name it `nell_loom`.
2. Right-click the cell beside her bed ▸ **Add a marker here…** and type `nell_bed`.
3. Select Nell and set **Behaviour ▸ home** `nell_home`, **work** `nell_loom`, **bed** `nell_bed` (each a list of the
   scene's markers).
4. The loom marker is a cell off: click it with the Marker tool and drag it with the Select tool.
5. Later, use **Go to marker…** and type `nell` to find all three.

Questions: how do I add a marker? how do I move a marker? how do I find a marker? how do I set a character's home?

## What a path is

A **path** is a route characters walk, drawn in the viewport, with a name; any number of packages can use it. It's a
small network rather than one line: **points**, and **links** between them that **fork** and **join** again. At a fork
each character chooses its way at random by weight, or by conditions. Each character keeps its own place on the path.

A path is an entity with a **Path** component and a WorldTransform2D (its own place: moving it moves all its points).
It's saved with the scene and invisible in play. Its fields: **name**, **points**, **links** (empty: point to point in
order), **end** (stop, loop, back) and **shown**.

### Example: what a market loop with a fork looks like inside

The path `market-loop` goes from the gate past the stalls, forks to either the fountain or the bakery, and joins at
the square. Selected, its **Path** fields hold:

```json
"name": "market-loop",
"points": [{"x": 0, "y": 0}, {"x": 4, "y": 0, "wait": 3}, {"x": 6, "y": -2, "label": "fountain"},
           {"x": 6, "y": 2, "label": "bakery"}, {"x": 9, "y": 0}],
"links": [{"from": 0, "to": 1}, {"from": 1, "to": 2, "weight": 3}, {"from": 1, "to": 3, "weight": 1},
          {"from": 2, "to": 4}, {"from": 3, "to": 4}],
"end": "loop"
```

Three of every four walkers go by the fountain; each keeps its own place on the path, and the loop takes them back to
the gate.

Questions: what is a path? how do characters walk a route? can a path branch?

## Drawing a path with the Path tool

Pick the toolbar's **Path** tool:
- click the ground to add points (cell middles), each linked from the picked point. The first click starts a new path
  (`path-1`, `path-2`…);
- click a point to pick it, so the next click branches from it (a fork);
- drag a point to move it;
- **Shift+click** a point to link the picked point to it (a fork rejoining);
- **Enter**, **Escape** or **Finish path** (on the toolbar while the tool is active) ends the path; the next click
  begins another.

Right-click a point for: **Branch from here (a fork)**, **Draw on from here**, **Wait here…** (seconds), **Play a clip
here…**, **Use a part here…** (a part by its name: its input action, station, clip or prompt, e.g. `cook`),
**Label…**, **Delete point** (a point in a line joins its neighbours), **Finish drawing**.

With the Select tool, clicking a path's point selects the path. Deleting a path that packages use says which do (undo
brings it back).

### Example: the baker's morning round

1. Pick the toolbar's **Path** tool and click the bakery door: a new path `path-1` starts.
2. Click the market stall, then the well, then the inn's back door: each point is linked from the one before.
3. Click the market stall point to pick it, then click the chapel: a fork from the stall.
4. **Shift+click** the inn's back door point: the chapel branch rejoins there.
5. Right-click the well point ▸ **Wait here…** and type `5`; right-click the inn point ▸ **Use a part here…** and type
   `oven`.
6. Press **Enter** (or **Finish path**). Select the path and rename it in the Inspector to `baker-round`.

Questions: how do I draw a path? how do I make a fork in a path? how do I make a character wait at a point? how do I
delete a point?

## Seeing paths in the viewport

Points are drawn as diamonds (crossed where something is done) and links with an arrow in their middle. Link colours:
**cyan** an ordinary link, **orange** one with conditions, **red** a priority link, **grey** the default.

The path being drawn, the selected path and the selected character's paths are bright; others are faint. The toolbar's
**⤳ Paths** toggle, or **View ▸ Paths (and, in Play, where each character is walking)**, hides all but those. In Play,
each character on a path has a green line to the point it's walking to.

### Example: checking the guards' routes during a playtest

1. The town has eight paths and the viewport is a tangle of faint lines. Turn off the toolbar's **⤳ Paths** toggle:
   only the selected path and the selected character's paths remain.
2. Select `Guard Osric`: his patrol path shows bright. Its fork to the gate tower is drawn **orange** (it has
   conditions), the way past the barracks **grey** (the default).
3. Press Play: a green line runs from Osric to the point he's walking to. If he heads for the barracks while you stand
   at the gate, his condition isn't holding; select the path and check that link's conditions in the Inspector.

Questions: why can't I see my paths? what do the path colours mean? how do I see where characters are walking?

## The Path editor in the Inspector

Select a path to edit it in **Inspector ▸ Path**:
- **Name**: packages use it by this; renaming renames it in every package that uses it.
- **At an end**: **stop**, **loop (back to the first point)**, **back (the way it came)**; **Draw on…** goes back to
  the Path tool from its last point.
- **Used by**: the packages and characters that use it, and any problems in red.
- **Points**: each one's number (⑂ marks a fork), **label**, wait seconds, **clip**, **use part**.
- **Links** ("more than one from a point: a fork"): each `from → to`; at a fork its **weight** (how often it's chosen
  against the others), **first** (priority: taken whenever its conditions hold), **default** (taken when no other's
  conditions hold), **label** (a package can prefer it by this), and its conditions. The conditions box turns red when
  it isn't a JSON list.

The raw **points** and **links** fields are the same data as JSON.

### Example: making the riverside walk loop and favour the bridge

1. Select the path `riverside` with the Select tool (click one of its points).
2. In **Inspector ▸ Path**, set **At an end** to **loop (back to the first point)**.
3. Under **Links**, find the fork at point 3 (marked ⑂): set the link `3 → 4` (over the bridge) to **weight** `4` and
   `3 → 7` (along the bank) to **weight** `1`.
4. Rename it in **Name** to `river-walk`: the packages listed under **Used by** are renamed along with it.
5. Type `{"playerNear": 4}` in a conditions box: it turns red, because conditions must be a JSON list:
   `[{"playerNear": 4}]`.

Questions: how do I rename a path? how do I make one branch more likely? how do I make a path loop? which packages use
this path?

## How a character chooses at a fork

At a point with several links, a character takes:
1. of the links whose conditions (**if**) hold, a **priority** one;
2. else one at random by **weight** (a package's `weights` by label override them);
3. else, when none holds, the **default** one;
4. else none: it's an end.

Conditions are checked when the character reaches the fork. They are dialogue's conditions (flags, quests, items, time
of day, factions, difficulty) plus three of the path's own: `{"playerNear": 5}` (the player within 5 cells),
`{"partOpen": "cook"}` (a part named so is open), `{"free": true}` (nobody else at the link's far point). `"not": true`
inverts one. Random choices are seeded, so a playtest replays the same way.

### Example: the gate guard comes to look when the player is near

The guard's path forks at the gate: one link to the watch post, one along the wall.
1. Select the path and, in Inspector ▸ Path ▸ Links, give the link to the watch post the conditions below and tick
   **first** (priority):

```json
[{"playerNear": 5}]
```

2. Tick **default** on the link along the wall.
3. In play, when the guard reaches the gate with the player within five cells, he goes to the watch post; otherwise he
   carries on along the wall. Add `"not": true` to the condition to make him avoid the player instead.

Questions: how do I make a guard take a different way when the player is near? how do weights work? what happens at the
end of a path?

## Using paths in packages and scripts

- Package template **follow-path**: `data.route` is the path's name; `data.mode` is `once` (to an end, then it stays),
  `loop`, or `wander` (any way at every point, links walked both ways, never stopping: town folk); `data.from` is
  `nearest` (default), `start` or `end`.
- Template **patrol**: `data.route` walks a named path for ever.
- A procedure step `{"type": "follow-path", "path", "mode", "from", "stops": true, "weights": {"<label>": weight},
  "hold"}`.
- Lua: `follow_path(name[, "start"])` walks a path and does its stops; `path_route(name[, from])` gives one walk's
  points.

Characters open doors on their way and wait a moment rather than walk into someone just ahead going to the same point.
The AI panel can add paths (`scene.addPath`) and make a loop round a building (`path.around`).

### Example: villagers who wander the town paths

1. Draw a network of paths through the streets and name it `town-streets`.
2. In **Project ▸ Records ▸ Packages**, make a package `stroll-town` with the template **follow-path** and the data:

```json
{ "schema": "aige.package/1", "id": "stroll-town", "template": "follow-path",
  "schedule": { "days": "all", "from": "09:00", "to": "18:00" },
  "data": { "route": "town-streets", "mode": "wander", "from": "nearest" },
  "description": "Strolls the streets by day." }
```

3. Add `stroll-town` to the **Behaviour ▸ packages** of each villager.
4. Or from a script: `follow_path("town-streets", "start")`.

In the AI panel: "Make the villagers wander the town streets during the day."

Questions: how do I make an NPC walk a path? how do I make villagers wander around town? how do I make a patrol route?
