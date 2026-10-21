# Character AI — packages, behaviour, perception and scripted AI

Status: **draft for review (2026-10-21)**. Phase 14 in `development_plan.md`. The AI
Planner writes every character's package (`ai-planner.md` §5.4).

---

## 1. Goal

A character in a finished game does things on its own: the innkeeper serves behind the
counter by day and sleeps upstairs at night, guards patrol and challenge intruders, a
bandit ambushes from the bushes and flees when hurt, a companion follows and fights. Today
the engine has `Follow` (walk to a target in range), `Routine` (stay, wander, sleep by
the clock) and Lua scripts.

We need:
1. a **declarative** way to say what a character does, when and where, that the editor,
   designers and the AI can write without code (**packages**);
2. a **runtime** that carries them out (a small behaviour-tree engine);
3. **perception**, **combat behaviour** and **reactions**;
4. a **scripted** path in Lua for behaviour no template covers;
5. editor tools to see and debug all of it.

## 2. What exists elsewhere

| System | Idea | What we take |
|---|---|---|
| **Creation Kit AI packages** (Oblivion, Skyrim, Fallout) | each actor has an ordered **package stack**; the first package whose **conditions** and **schedule** hold runs; packages come from **templates** (Travel, Sandbox, Sleep, Eat, Patrol, Follow, Escort, UseItemAt, Ambush, Guard, Flee…) whose **procedure trees** (Sequence, Stack, Simultaneous, Random, Try-to) take **data** (location, target, duration); quests add packages through aliases; **AI data** (aggression, confidence, morality, assistance) and **combat styles** | the whole package model; procedures; quest-added packages; AI data and combat styles |
| **Behaviour trees** (Halo 2, Isla 2005; Unreal Engine) | composites, decorators, leaves; a blackboard; services that update it | the procedure runtime |
| **GOAP** (F.E.A.R., Orkin 2006) | actions with preconditions and effects; a planner finds a sequence for a goal | not as the core (harder to author and debug); possible later for combat |
| **Utility AI** (Dave Mark; The Sims' needs) | score options by curves over needs and context; pick the best | optional **utility selection** inside Sandbox (which smart object to use) |
| **HTN** (Killzone 2, Horizon Zero Dawn) | decompose tasks by methods | the procedure trees are a simple HTN: a template decomposes into steps |
| **Smart objects** (The Sims; Unreal Smart Objects) | objects advertise what can be done with them and how | smart objects for sandboxing and work |
| **Radiant AI** (Oblivion) | goals and schedules over smart objects | packages + smart objects cover it |
| **Stardew Valley schedules** | per-day, per-season routes for each villager | schedules with day and season conditions |
| **Generative Agents** (Park et al. 2023) | memory, reflection, planning with an LLM | at design time only: the Planner writes biographies and schedules; the runtime is deterministic and needs no AI |

## 3. Overview

```
  conditions, schedule, events
          │
          ▼
  ┌─ Package stack ─┐   picks the active package (first that holds)
  │ 1 Quest: escort │
  │ 2 Work: counter │──► Procedure tree (behaviour tree) ──► Actions
  │ 3 Sleep: room   │      Sequence / Selector / Random /      move-to, use object,
  │ 4 Sandbox: inn  │      Parallel / decorators / leaves      wait, face, bark,
  └─────────────────┘                                         animate, attack, cast
          ▲                                                    │
  overrides: combat (combat style), reactions, scripted ◄──── perception, events
```

- The **package stack** chooses *what* the character is doing.
- The active package's **procedure** does it, step by step.
- **Combat** and **reactions** interrupt the package when perception or events call for it;
  the package resumes after.
- **Scripted packages** replace the procedure with Lua.

## 4. Packages

### 4.1 The package record (`aige.package/1`)

Packages are **records** (`engine-gaps.md` §2.1) under `records/packages/`. A character's
`Behaviour` component lists its stack.

```json
{ "schema": "aige.package/1", "id": "innkeeper-work", "template": "work",
  "location": { "anchor": "counter-staff", "in": "@home-building" },
  "schedule": { "days": "all", "from": "08:00", "to": "22:00" },
  "conditions": [{ "quest": "q-moon-amulet", "stageBelow": 50 }],
  "flags": { "interruptible": true, "walk": "walk", "weapon": "sheathed" },
  "data": { "objectType": "counter", "greetPlayer": true },
  "description": "Serves behind the counter during opening hours." }
```

| Field | Meaning |
|---|---|
| `template` | the template whose procedure runs (§5), or `script` (§10) |
| `location` | where: a marker, an anchor in a part, an area, near an entity, an alias (`@name` resolves against the character's aliases: home, work, a quest's) |
| `target` | whom or what: an entity, an alias, a character, an object type |
| `schedule` | `days` (all, weekdays, list, seasons), `from`, `to` or `duration`; empty = always |
| `conditions` | the engine's condition language (story variables, quest stages, attributes, time, weather, faction standing, distance to player) |
| `flags` | interruptible, must-complete, walk/run, weapon drawn/sheathed, ignore combat, lock doors behind, unlock doors |
| `data` | the template's own parameters (validated against its schema) |

### 4.2 The stack

The `Behaviour` component:

| Field | Meaning |
|---|---|
| `packages` | the ordered stack; the first whose conditions and schedule hold is active |
| `aliases` | named locations and entities for the packages (`home`, `work`, `bed`) |
| `aiData` | aggression (peaceful, defensive, aggressive, frenzied), confidence (cowardly … foolhardy), assistance (nobody, allies, friends), morality (any crime … none) |
| `combatStyle` | a combat style record (§8) |
| `fallback` | what to do when nothing holds: stand at the current spot (default) |

Quests can **push packages** onto an alias's stack for a stage (a captive follows the
player, a guard moves to the gate), as Creation Kit quest aliases do; they're removed when
the stage ends. Pushed packages sit on top.

Re-evaluation: when the game hour changes, when a quest stage or story variable it reads
changes, when the active package finishes, and at most every 2 seconds otherwise. A
running package with `must-complete` finishes its procedure first.

## 5. Templates

Each template is a **procedure tree** with parameters (`data/ai/templates/<id>.json`). The
library:

| Template | Does | Data |
|---|---|---|
| `travel` | goes to the location, then ends | arrival radius, run |
| `stay` | stays at the location | facing |
| `wander` | strolls within a radius, pausing | radius, pause range |
| `sandbox` | lives in an area: uses its smart objects (sit, eat, drink, read, warm hands), wanders, chats with others | radius, allowed affordances, utility weights |
| `sleep` | goes to its bed (owned, or any free), sleeps (hidden or lying), wakes when woken or at the end | bed alias, wake on noise |
| `eat` | goes to a table or a food object, eats | |
| `work` | uses a work object (counter, forge, field, desk) for the period | object type, greet player |
| `patrol` | walks a path of markers, waiting at each | path, wait at points, loop or ping-pong |
| `guard` | holds a post, watches an area, challenges intruders | post, watched area, challenge dialogue |
| `follow` | follows a target at a distance | distance range |
| `escort` | leads a target to a location, waiting when it falls behind | target, location |
| `accompany` | goes along with a target, fighting beside it (companions) | |
| `flee` | runs from a threat to a safe location | threat, safe place |
| `hide` | goes to a hiding spot and stays hidden until a condition | |
| `ambush` | hides at a spot until the target is near, then attacks | trigger radius |
| `wait` | stands until a condition or a time | |
| `use-object` | uses one object a number of times | object, count |
| `approach-player` | walks up to the player and starts a dialogue or a bark | dialogue, once |
| `vendor` | `work` at a counter, opens the shop on Talk | shop record |
| `script` | runs a Lua package (§10) | script, properties |

`Routine` and its schedules (`story.md`) become a shorthand: a schedule file's entries are
read as `stay`, `wander` and `sleep` packages, so existing projects keep working.

### 5.1 Procedures

The procedure runtime is a behaviour tree with a **blackboard** per character:

| Node | Kind | Meaning |
|---|---|---|
| `sequence`, `selector` | composite | in order until one fails / succeeds |
| `parallel` | composite | children together; ends by policy (any, all) |
| `random` | composite | one child by weight |
| `stack` | composite | like the Creation Kit's: tries the first, falls back to the next |
| `condition`, `cooldown`, `timeout`, `repeat`, `invert` | decorator | |
| `move-to`, `face`, `wait`, `use-object`, `animate`, `bark`, `say` (a dialogue line), `pick-up`, `drop`, `open`, `close`, `attack`, `cast`, `set-var`, `emit`, `lua` | leaf (action) | |
| `find-object`, `find-actor`, `find-spot` | leaf (query) | writes a blackboard key |

A designer (or the AI) can write **new templates** as data from these nodes; they're
validated like any record. A visual tree editor is later; the Records panel edits the JSON
with a tree view.

## 6. Smart objects

An entity with a `SmartObject` component advertises what can be done with it:

| Field | Meaning |
|---|---|
| `type` | bed, chair, bench, table, counter, forge, anvil, well, hearth, bookshelf, altar, field, crate, lever |
| `slots` | where a user stands or sits, facing, how many at once |
| `affordances` | sleep, sit, eat, drink, read, work, pray, warm, store, craft |
| `animation` | the animation per affordance (`effects-animation.md`) |
| `duration` | how long a use lasts (range) |
| `owner` | a character, a faction, or public |
| `effects` | optional effects on the user (rest restores stamina) |

**Place builds create them** from module anchors (`ai-planner.md` §9.2): a `bed` anchor
in a bedroom becomes a bed smart object owned by the character the place description
assigned to that room. Hand-placed prefabs can have the component too.

## 7. Persistent actors

A character is **one actor** in the game, however many scenes its packages visit:
- Its state (where it is, health, inventory, package, relationship to the player) is kept
  in the save under its character key, not in a scene.
- When its active package's location is in another scene, it's **away**: removed from the
  current scene and advanced coarsely (its position along its schedule by time), as
  `story.md`'s routines do now, but without placing a copy in every scene.
- When the player enters a scene, the actors whose packages put them there are spawned
  from their prefab at the right spot, with their kept state.
- Gateways connect scenes for actors too: an actor travelling to another scene walks to the
  gateway and leaves.

This replaces "place the NPC in every scene their day visits" and is what makes killed or
moved characters stay so.

## 8. Perception and combat

### 8.1 Perception

| Sense | Model |
|---|---|
| sight | a cone (angle, range) with line of sight over the tile grid's solid cells; reduced by darkness (with 2D lighting, `effects-animation.md` §5) and by the target's stealth |
| hearing | **noise events** with a radius and loudness: footsteps (by speed), combat, doors, spells, thrown objects |
| memory | the last known position of each detected actor, decaying over time |

Awareness per detected actor: `unaware` → `suspicious` (investigates the noise or last
position) → `alert` (searches) → `combat`. Thresholds and decay are per character
(`aiData`). Faction relations decide whether an alert actor fights, flees, or calls guards.

### 8.2 Combat

When a hostile actor is detected and `aiData` says fight, the **combat behaviour**
overrides the package. A **combat style** record decides how:

| Field | Meaning |
|---|---|
| `range` | melee, ranged, kiting (keeps distance), mixed |
| `aggression` | how readily it attacks vs waits for openings |
| `fleeAt` | health fraction to flee (confidence modifies it) |
| `block`, `dodge` | chances, if the combat system has them |
| `abilities` | which abilities to use, when (`magic.md`): heal below 40%, area attack when 3+ targets |
| `group` | surround, focus the weakest, protect a leader, call for help radius |

Targets are scored by distance, threat and faction. Positioning uses the pathfinding grid
(and later a flow field for crowds). `difficulty.md` scales reaction time, accuracy and
aggression per level.

### 8.3 Reactions

Short responses to events without changing the package: hit (turn, bark, fight or flee),
greeted, a crime seen (report, attack, ignore by morality), a corpse found (alarm), a
door left open. Each is data (`reactions` on the character or its class), with barks.

## 9. Field-defined or scripted

Character generation (`ai-planner.md` §5.4) chooses per character:

| | Field-defined (templates) | Scripted (Lua) |
|---|---|---|
| When | the behaviour fits templates: villagers, guards, merchants, enemies, companions | special logic: a ghost that appears only to amulet-bearers, a boss with phases, a puzzle NPC |
| Written as | a package stack of records with data | a `script` package with a Lua file, plus field packages for the rest of the day |
| Checked by | the record validators (locations resolvable, schedule covers the day, conditions valid) | Lua compile, the AI API whitelist, a headless run of the package for 60 simulated seconds without errors |
| Edited in | the Inspector's Behaviour section and the Records panel | the script editor |

Most characters are field-defined; scripted packages are for the few that need it. Both
use the same stack, so a scripted package can be one entry among templates.

## 10. Scripted packages

A Lua package script defines lifecycle functions; `run` is a coroutine, so behaviour reads
top to bottom:

```lua
-- scripts/ai/ghost_of_aldric.lua
properties = { reveal_radius = 4.0 }

function applies(ctx)            -- extra condition, checked with the package's own
    return has_item(player_id(), "records/items/moon-amulet")
end

function run(ctx)
    set_visible(self_id(), false)
    while true do
        local d = distance_to(player_id())
        if d and d < properties.reveal_radius then
            set_visible(self_id(), true)
            face(player_id())
            bark("You carry what I stole...")
            wait(3)
            start_dialogue("dialogue/ghost-aldric")
            return               -- the package ends; the stack re-evaluates
        end
        wait(0.5)
    end
end

function stop(ctx)               -- interrupted or ended
    set_visible(self_id(), true)
end
```

The AI API for scripts (added to `scripting.md` in Phase 14): `move_to(x, y | marker |
id)`, `wait(s)`, `face(id)`, `use_object(id)`, `find_object(type, radius)`,
`find_actor(filter)`, `bark(text)`, `say(line)`, `start_dialogue(path)`, `animate(name)`,
`attack(id)`, `cast(ability, target)`, `set_visible(id, bool)`, `awareness_of(id)`,
`push_package(id, package)`, `blackboard_get/set`. `move_to` and `use_object` yield until
done, so scripts don't poll.

## 11. Runtime

- **Levels of detail:** full near the player (perception, procedures every step); reduced
  further away (procedures every 0.5 s, no perception); coarse in other scenes (§7).
- **Budget:** a fixed number of stack re-evaluations and path requests per frame; the rest
  wait a frame.
- **Determinism:** seeded random per actor, so a playtest replays.
- **Save:** each actor's active package, procedure position and blackboard essentials.

## 12. Editor

- **Inspector ▸ Behaviour:** the package stack as a list with a **day timeline** (24 h bar
  showing which package runs when), conditions as chips, add / reorder / remove, and the
  aliases with "pick in viewport".
- **Records panel:** packages, templates, combat styles.
- **Play debug overlay** (a toggle): over each actor, its active package and procedure
  node; sight cones and hearing radii; awareness; paths.
- **Simulate a day:** fast-forward the clock in the editor to watch schedules play out.
- **Checks:** a package location that resolves to nothing; a day with gaps; a bed owned by
  two; a patrol path through walls; an unreachable work object.

## 13. How the Planner uses it

From each character's behaviour description and roles (`ai-planner.md` §5.4):
1. **Roles to templates:** innkeeper → `work` at counter (opening hours), `sandbox` in the
   inn (evening), `sleep` in their room; guard → `guard` or `patrol` by shift; enemy →
   `sandbox` or `patrol` in its lair with a combat style; companion → `accompany`.
2. **Locations as aliases** (home, work, bed, post), resolved at binding (§9 of
   `ai-planner.md`) against the anchors the builds produced; the place description lists
   the anchors each character needs, so they exist.
3. **Quests push packages** for their stages (escort, wait for the player, ambush).
4. **Scripted** where the description has logic templates can't express; the script is
   written with its test (the headless run) and its check results recorded.
5. **Validation:** schedule coverage, resolvable locations, combat style for every fighter
   (R-K6 of `story-ontology.md` §7.4).
