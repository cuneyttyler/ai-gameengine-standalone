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

Questions: how do I add a marker? how do I move a marker? how do I find a marker? how do I set a character's home?

## What a path is

A **path** is a route characters walk, drawn in the viewport, with a name; any number of packages can use it. It's a
small network rather than one line: **points**, and **links** between them that **fork** and **join** again. At a fork
each character chooses its way at random by weight, or by conditions. Each character keeps its own place on the path.

A path is an entity with a **Path** component and a WorldTransform2D (its own place: moving it moves all its points).
It's saved with the scene and invisible in play. Its fields: **name**, **points**, **links** (empty: point to point in
order), **end** (stop, loop, back) and **shown**.

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

Questions: how do I draw a path? how do I make a fork in a path? how do I make a character wait at a point? how do I
delete a point?

## Seeing paths in the viewport

Points are drawn as diamonds (crossed where something is done) and links with an arrow in their middle. Link colours:
**cyan** an ordinary link, **orange** one with conditions, **red** a priority link, **grey** the default.

The path being drawn, the selected path and the selected character's paths are bright; others are faint. The toolbar's
**⤳ Paths** toggle, or **View ▸ Paths (and, in Play, where each character is walking)**, hides all but those. In Play,
each character on a path has a green line to the point it's walking to.

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

Questions: how do I make an NPC walk a path? how do I make villagers wander around town? how do I make a patrol route?
