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

Questions: what parameters does move-to take? how do I make a character wander inside a building? how do I walk a path
in a custom procedure?

## Action leaf steps

| step | parameters (defaults) | does |
|---|---|---|
| `wait` | `seconds`, or `min` (2) and `max` (5) | waits; succeeds |
| `idle` | `hidden` | stands (hidden: invisible) for ever |
| `face` | `to` (@player) | turns to it; succeeds |
| `say` (old name `bark`) | `text` (`a|b` picks one; `@idle` uses its Speech idle lines) | a line over its head, only within 7 cells of the player |
| `use-object` | `type`, `affordance` (`sit|eat`), `around`, `radius` (10), `forever` | walks to the nearest free smart object that fits and uses it for its duration |
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

See also: Package templates, Paths, Scripting (for logic with choices and recipes).

Questions: can you show an example custom package? how do I combine walking a path with talking?
