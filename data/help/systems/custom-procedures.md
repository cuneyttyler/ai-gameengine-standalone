# Custom procedures

Writing a package's own steps (template `custom`) or a `procedure` record: the procedure editor, how a behaviour tree
runs, every composite, decorator and leaf step with its parameters and defaults, places like "@location", and worked
examples.

## The procedure editor

A package with template **custom** shows a **procedure** field in Project ▸ Records: a code box for the package's
behaviour tree as JSON.

- Line numbers on the left; **Tab** indents by two spaces; **Format** lays the JSON out tidily.
- It's checked as you type. The bar under it says "✓ Its steps are sound", or what's wrong: "Not JSON yet: …", "It
  must be one step: {"type": ...}", "<path>: "<x>" isn't a step (steps: …)", "(sequence) needs children", "(condition)
  takes exactly one child", "(wait) has no children", "(random): one weight per child". Paths name the step:
  `root.0.1` is the root's first child's second child.
- Under it, the steps are shown as an indented outline in words ("again and again", "in order", "walk to the place",
  "wait min 2 (data.waitMin, the template's)…"), with this package's data filled in.

Quickest start: pick a template close to what you want, then **Copy into a custom package…**, and edit the copied steps.

A **procedure** record (Records ▸ procedure) holds a tree the same way (`tree`), for reuse: a package whose template
names the procedure record runs it.

### Example: a town crier who walks to the square and shouts the news

1. In Project ▸ Records, press **+** on the **package** group; **Id** `crier-news`; **template** `custom`.
2. Type in the **procedure** box (press **Tab** to indent, or paste it on one line and press **Format**):

```json
{"type": "loop", "children": [{"type": "sequence", "children": [
  {"type": "move-to", "to": "town-square"},
  {"type": "face", "to": "@player"},
  {"type": "say", "text": "Hear ye! The bridge is closed!|Bandits on the north road!"},
  {"type": "wait", "seconds": "@data:pause=15"}
]}]}
```

3. The bar reads "✓ Its steps are sound", and the outline says: again and again, in order: walk to town-square, turn
   to the player, say…, wait 15. Press **Create**.
4. Put `crier-news` in the crier's Behaviour **packages**.

If you forget the `children` of the sequence, the bar says "(sequence) needs children" and points to `root.0`.

Questions: where do I write custom package steps? what does "needs children" mean? how do I format the procedure JSON?

## How a procedure runs

A procedure is a behaviour tree: one root step, each composite or decorator with child steps. Every step, the character
runs the tree from the root; each step is **running** (still doing it), **succeeded** or **failed**.

- A step that's running is carried on next time; the tree remembers where it was.
- When the whole tree succeeds or fails, it starts again from the top at the next step. So a package whose root is a
  `sequence` repeats; wrap it in `loop` to make the repetition explicit, or end with `idle` to stand still at the end.
- Each character has its own place in the tree and its own **blackboard** (named values set with `set`, read with
  `@bb:<key>`).
- When the package stops being the active one, its tree is reset.

The shape of a step: `{"type": "<step>", ...parameters, "children": [ ... ]}`. Any parameter can be `"@data:<name>"` or
`"@data:<name>=<default>"` to read the package's data.

### Example: a lamplighter who remembers where he started

The tree below stores the lamplighter's first post on the blackboard, walks to the lamp-post markers, then goes back.

```json
{"type": "sequence", "children": [
  {"type": "set", "key": "start", "value": [12.5, 8.5]},
  {"type": "move-to", "to": "lamp-1"},
  {"type": "play", "clip": "light", "seconds": 2},
  {"type": "move-to", "to": "lamp-2"},
  {"type": "play", "clip": "light", "seconds": 2},
  {"type": "move-to", "to": "@bb:start"},
  {"type": "idle"}
]}
```

1. Make a custom package `lamplighter-evening` with this procedure and a **schedule** from `19:00` to `20:00`.
2. In Play, at 19:00 he walks to `lamp-1`, plays `light`, then `lamp-2`, then back to cell (12, 8), and stands
   (`idle` keeps the tree running, so it doesn't start over).

Without the final `idle`, the sequence would succeed and he'd light the lamps again and again until 20:00.

Questions: how does a behaviour tree run? does my procedure repeat? what is the blackboard?

## Composite steps

Composites have one or more children.

| step | does |
|---|---|
| `sequence` | its children in order; fails as soon as one fails; succeeds when all have |
| `selector` | its children in order until one succeeds (the first that works); fails if all fail |
| `stack` | the same as selector: tries the first, falls back to the next |
| `random` | one child at random by `weights` (one per child), then that child only |
| `parallel` | all children every step; ends as soon as any child finishes, with its result |
| `loop` | its single child again and again; with `count`, that many times, then succeeds |

**Weights** in `random` are numbers, `"@data:<name>=<default>"` (the package's value; 0 means never), or
`"@actionsWeight"` (the package's `data.actions` weights added together: 1 for a single `data.action`, 0 for none). A
missing weight counts 1.

Example: `{"type": "random", "weights": [3, 1], "children": [ {wander…}, {say…} ]}` wanders three times as often as it
talks.

### Example: a fisherman who sometimes fishes, sometimes mends nets

```json
{"type": "loop", "children": [
  {"type": "random", "weights": [3, 1], "children": [
    {"type": "sequence", "children": [
      {"type": "move-to", "to": "jetty-end"},
      {"type": "play", "clip": "fish", "seconds": "@data:duration=20"}]},
    {"type": "selector", "children": [
      {"type": "use-part", "part": "mend-nets", "around": "@location", "seconds": 10},
      {"type": "wait", "seconds": 4}]}
  ]}
]}
```

1. Make a custom package `old-ivo-fish`, **location ▸ marker** `harbour`, with this procedure; **Create**.
2. In Play he goes fishing three times in four; otherwise he mends nets at the net rack, or, when the rack can't be found,
   waits four seconds (the selector's fallback).
3. Set **data ▸ duration** `40` in the form and each spell of fishing lasts 40 seconds instead of 20.

Questions: what's the difference between sequence and selector? how do I pick a random action? how do I repeat steps a
number of times?

## Decorator steps

Decorators have exactly one child.

| step | parameters | does |
|---|---|---|
| `condition` | `if` (a conditions list, as dialogue's), `time` (a schedule: `from`, `to`, `days`) | runs its child only while they hold; otherwise fails |
| `cooldown` | `seconds` (default 5) | after its child finishes, fails for that many seconds |
| `invert` | | succeeded becomes failed and failed succeeded |

Example: say a line at most once a minute, only by day:

```json
{"type": "condition", "time": {"from": "08:00", "to": "20:00"}, "children": [
  {"type": "cooldown", "seconds": 60, "children": [
    {"type": "say", "text": "Fresh bread!|Bread, still warm!"}]}]}
```

Put a cooldown inside a `selector` with an alternative, so the selector moves on while it's cooling.

### Example: a guard who warns the player only during the curfew

```json
{"type": "loop", "children": [{"type": "selector", "children": [
  {"type": "condition", "if": [{"flag": "curfew"}], "time": {"from": "22:00", "to": "05:00"}, "children": [
    {"type": "cooldown", "seconds": 45, "children": [
      {"type": "sequence", "children": [
        {"type": "face", "to": "@player"},
        {"type": "say", "text": "Curfew! Get indoors!"}]}]}]},
  {"type": "sequence", "children": [
    {"type": "wander", "around": "@location", "radius": 4},
    {"type": "wait", "min": 2, "max": 4}]}
]}]}
```

1. Make a custom package `guard-curfew` (**location ▸ marker** `market-square`) with this procedure.
2. Between 22:00 and 05:00, once the flag `curfew` is set, the guard turns and shouts at most every 45 seconds; while
   the cooldown runs, the selector falls through to wandering.
3. By day, or with the flag unset, the condition fails and he only wanders round the square.

Questions: how do I add a condition to one step? how do I stop a step repeating too often? what does invert do?

## Movement leaf steps

| step | parameters (defaults) | does |
|---|---|---|
| `move-to` | `to` (@location), `radius` (0.5) | walks there; succeeds on arrival; fails when there's no way |
| `wander` | `around` (@location), `radius` (3), `inside` | walks to a random free cell within radius; `inside: true` picks a cell in the package building's rooms, or give a building's name |
| `follow` | `target` (@player), `distance` (2) | keeps near the target; never finishes |
| `flee` | `from` (@player), `distance` (8) | runs away until that far; never finishes |
| `escort` | `target` (@player), `to` (@location), `wait` (4) | walks to `to`, waiting while the target is further than `wait`; succeeds on arrival |
| `patrol` | `route` (a path name) or `points` (marker names), `wait` (2) | walks the path or the markers in order for ever |
| `follow-path` | `path`, `mode` (once), `from` (nearest), `stops` (true), `weights`, `hold` | walks a named path; see Paths |

A wander walk gives up after 12 seconds, or within half a second when there's no way to the cell, and then succeeds so
the next step runs.

### Example: a maid who tidies every room of the manor, then follows her mistress

```json
{"type": "sequence", "children": [
  {"type": "loop", "count": 4, "children": [{"type": "sequence", "children": [
    {"type": "wander", "inside": "ashford-manor"},
    {"type": "play", "clip": "dust", "seconds": 5}]}]},
  {"type": "follow", "target": "Lady Ashford", "distance": 2.5}
]}
```

1. Make a custom package `maid-morning` with this procedure and a **schedule** from `08:00` to `11:00`.
2. In Play, she walks to four random spots inside the manor's rooms (the building named `ashford-manor`), dusting
   five seconds at each.
3. After the fourth, `loop` succeeds and she follows Lady Ashford two and a half cells behind until 11:00 (`follow`
   never finishes).

Questions: what parameters does move-to take? how do I make a character wander inside a building? how do I walk a path
in a custom procedure?

## Action leaf steps

| step | parameters (defaults) | does |
|---|---|---|
| `wait` | `seconds`, or `min` (2) and `max` (5) | waits; succeeds |
| `idle` | `hidden` | stands (hidden: invisible) for ever |
| `face` | `to` (@player) | turns to it; succeeds |
| `say` (old name `bark`) | `text` (`a|b` picks one; `@idle` uses its Speech idle lines) | a line over its head, only within 7 cells of the player |
| `use-object` | `type_` (the smart object's type), `affordance` (`sit|eat`), `around`, `radius` (10), `forever` | walks to the nearest free smart object that fits and uses it for its duration |
| `use-part` | `part`, or `parts` (a list of names or `{action, weight}`), `around`, `radius` (12), `seconds`, `clip`, `leaveOn` | walks to an animated part, uses it as the player would, stays `seconds` (its own duration, else 5), turns off what it turned on unless `leaveOn` |
| `act` | `action` (interact), `around`, `radius` (12) | the nearest free Reaction or Interactable with that action, else a part named so: walked to and used |
| `play` | `clip`, `seconds` (2) | plays a clip on itself, then stops it |
| `talk-to-player` | `dialogue` | walks to the player and starts the dialogue |
| `ambush` | `radius` (3) | lies low; attacks a hostile player who comes within radius |
| `set` | `key`, `value` | writes the blackboard |
| `emit` | `event` | sends a game event (scripts, triggers, Character Rules hear it) |
| `script` | `script` | runs a Lua file's `run()` until it returns |

`use-part` on a part that's already on (a door open, a stove cooking) doesn't use it again (that would shut it); a part
that always plays is used anyway. Unknown step types fail.

### Example: a tavern keeper who serves, rings the bell and announces closing

```json
{"type": "sequence", "children": [
  {"type": "use-object", "type_": "counter", "affordance": "work", "around": "@location", "radius": 6},
  {"type": "act", "action": "ring-bell", "around": "@location"},
  {"type": "emit", "event": "last_orders"},
  {"type": "say", "text": "Last orders!"},
  {"type": "use-part", "part": "door", "around": "@location", "radius": 8, "leaveOn": true},
  {"type": "idle"}
]}
```

1. Make a custom package `goose-closing` (**location ▸ building** `golden-goose`, **schedule** `23:00`-`23:59`).
2. He works the counter for its duration, rings the bell (a Reaction whose action is `ring-bell`), sends the event
   `last_orders` (a script can hear it with `on("last_orders", ...)`), shouts, and opens the door, leaving it open.

Questions: how do I make a character play an animation? how do I make a character say something? how do I make a
character use a door or a lever? what's the difference between act and use-part?

## Places a step can name

Parameters that take a place (`to`, `around`, `target`, `from`) accept:

- `@location`: the package's location;
- `@home`, `@work`, `@bed`: the character's Behaviour markers (bed falls back to home; home and work fall back to where
  it stands);
- `@target`: the package's target (the player when the target is `player`, else a named character or a marker);
- `@player`: the player;
- `@bb:<key>`: a point stored on the blackboard as `[x, y]`;
- any other text: a Marker's name.

### Example: a shepherd who checks the flock, goes home, and goes to bed

```json
{"type": "sequence", "children": [
  {"type": "move-to", "to": "@location"},
  {"type": "wander", "around": "@location", "radius": 5},
  {"type": "move-to", "to": "@home"},
  {"type": "wait", "seconds": 30},
  {"type": "move-to", "to": "@bed"},
  {"type": "idle", "hidden": true}
]}
```

1. On the shepherd Aled, set Inspector ▸ Behaviour **home** to the marker `aled-cottage` and **bed** to `aled-bed`.
2. Make a custom package `aled-evening` with **location ▸ marker** `sheep-pen`, this procedure and a **schedule** from
   `18:00` to `06:00`.
3. At 18:00 he walks to the pen, strolls once among the sheep, goes home, waits, walks to his bed and disappears into it.

Writing `"to": "well"` instead would send him to the Marker named `well`.

Questions: what does @location mean in a procedure? can a step walk to a marker? what is @bb?

## Example custom procedures

A baker who sells, then sweeps, then rests, with her own parameter `rest`:

```json
{"type": "loop", "children": [{"type": "sequence", "children": [
  {"type": "use-object", "type_": "counter", "affordance": "work", "around": "@location", "radius": 8},
  {"type": "say", "text": "Who's next?|Fresh loaves!"},
  {"type": "use-part", "part": "sweep", "around": "@location", "seconds": 6},
  {"type": "move-to", "to": "@home"},
  {"type": "wait", "seconds": "@data:rest=20"}]}]}
```

(`type_` in a step is the smart object's type, as the built-in templates write it.)

A lookout who walks a path once, holds at its end, and calls out when the player is near:

```json
{"type": "parallel", "children": [
  {"type": "follow-path", "path": "wall-walk", "mode": "once", "hold": true},
  {"type": "loop", "children": [{"type": "cooldown", "seconds": 30, "children": [
    {"type": "condition", "if": [{"flag": "intruder"}], "children": [{"type": "say", "text": "Who goes there?"}]}]}]}]}
```

### Example: every shopkeeper in the project sells, then rests

A procedure record named like a built-in template replaces that template for the whole project. Here it changes what
`vendor` does.

1. In Project ▸ Records, press **+** on the **procedure** group; **Id** `vendor`; **description**
   `Works a counter, then rests at home.`
2. **tree**:

```json
{"type": "loop", "children": [{"type": "sequence", "children": [
  {"type": "use-object", "type_": "counter", "affordance": "work", "around": "@location", "radius": "@data:radius=8"},
  {"type": "say", "text": "Come again!"},
  {"type": "move-to", "to": "@home"},
  {"type": "wait", "seconds": "@data:rest=20"}]}]}
```

3. Press **Create**. The packages `baker-shop` and `grocer-shop` keep template `vendor`, with **location ▸ building**
   `bakery` and `grocery`.

Both shopkeepers now run these steps at their own shop and home instead of standing at the counter all day; set the
grocer's package **data ▸ radius** to `12` and he looks for a counter further from his shop's centre. The rest stays 20
seconds, the default written in the tree. Delete the record to bring back the
built-in vendor.

See also: Package templates, Paths, Scripting (for logic with choices and recipes).

Questions: can you show an example custom package? how do I combine walking a path with talking?
