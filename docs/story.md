# Dialogue and quests

A project's conversations and quests are JSON files next to its scenes and scripts. The
engine reads them in `engine/gameplay/Story` (`development_plan.md` 3.2b). Characters
are referred to by their World Bible key (`character/villager`, see `world/`), so the
game shows the canon name, "Old Tamsin", and quests can say "talk to her".

Dialogue, quests, schedules and the World Bible belong to the **project**, not to a
scene. Quest progress, flags and the clock carry on when a Gateway takes the player to
another scene. Where something only makes sense in one place, it names the scene (below).

## Talking

An entity with a **Talk** component can be talked to:

| Field | Meaning |
|---|---|
| `dialogue` | The conversation, `dialogue/<id>.json`. |
| `character` | Who they are in the World Bible. Used for their name on screen and by "talk to" objectives. |
| `range` | How close the player must be, in cells. |

In play: **E** starts the conversation when someone is in range ("E  Talk to Old
Tamsin"). **1-9** pick a choice. **Up/Down** and **Enter** do the same. **Space**,
**Enter** or **E** continue past a line without choices. The world pauses while you
talk. **J** opens the journal (below).

## `dialogue/<id>.json`

```json
{
  "speaker": "character/villager",
  "start": "greet",
  "starts": [
    { "if": [{ "quest": "quiet-the-courtyard", "is": "completed" }], "node": "thanks" },
    { "if": [{ "quest": "quiet-the-courtyard", "is": "active" }], "node": "progress" }
  ],
  "nodes": [
    {
      "id": "greet",
      "text": "Another Warden? The last one came back in a sack.",
      "portrait": "assets/portraits/tamsin.png",
      "voice": "assets/voice/tamsin-greet.wav",
      "choices": [
        { "text": "I'm here about the keep.", "next": "offer" },
        { "text": "Surely the keep can pay more.", "check": { "attribute": "speech", "difficulty": 5 },
          "next": "paid", "fail": "refused", "do": [{ "op": "set_var", "var": "reward", "value": 50 }] },
        { "text": "Secret option", "next": "secret", "if": [{ "flag": "knows_secret" }] },
        { "text": "Goodbye.", "next": "" }
      ]
    },
    { "id": "offer", "text": "Put three of them back in the ground.",
      "choices": [{ "text": "I'll do it.", "next": "", "do": [{ "op": "start_quest", "quest": "quiet-the-courtyard" }] }] },
    { "id": "thanks", "speaker": "character/villager", "text": "You did it, then.", "next": "" }
  ]
}
```

- **`speaker`**: who talks by default. A node can override it; `"player"` means the
  player's own line. A World Bible key shows that entry's name, and plain text ("A
  guard") is shown as is.
- **`start`**: the first node. **`starts`** are checked in order before it, and the first
  one whose `if` holds wins. That's how a character greets you differently once a quest
  is under way.
- **Nodes** have `text`, then either up to 9 `choices` or a `next` node. An empty `next`
  ends the conversation. A node's `do` runs when it's shown. `portrait` is an image shown
  above the box; `voice` a sound played with the line.
- **Choices** can have `if` (all must hold, or the choice is hidden) and `do` (runs when
  picked).
- **Skill checks**: a choice with `check` tests the player: `{"attribute": "speech",
  "difficulty": 5}` passes when that attribute is at least 5 (any world condition below
  works as a check too). The choice shows "[Speech 5]". Passing goes to `next` and runs
  `do`; failing goes to `fail` and skips `do`.

### Conditions

All of a list must hold.

| Condition | Holds when |
|---|---|
| `{"flag": "name"}`, `{"flag": "name", "is": false}` | the flag is set (or not) |
| `{"quest": "<id>", "is": "inactive" \| "active" \| "completed" \| "failed"}` | the quest is in that state |
| `{"quest": "<id>", "is": "active", "stage": 20}` | ...and at that stage |
| `{"var": "paid", "op": ">=", "value": 10}` | a story variable compares (`==` `!=` `<` `<=` `>` `>=`) |
| `{"var": "met_mira"}` | a story variable is set and not 0, false or "" |
| `{"item": "potion", "count": 2}` | the player carries at least that many |
| `{"gold": 50}`, `{"level": 3}`, `{"day": 2}` | the player has that much gold, that level; it's that day or later |
| `{"attribute": "strength", "atLeast": 5}` (or `"below"`) | the player's attribute |
| `{"time": {"from": 20, "to": 6}}` | the time of day is in that range (hours; it may wrap past midnight) |
| `{"faction": "watch", "atLeast": 10}` (or `"below"`) | the player's reputation with a faction |
| `{"difficulty": "story"}`, `{"difficulty": {"enemies": {"atLeast": 4}}}` | the difficulty level, or a category's step |

### Effects

| Effect | Does |
|---|---|
| `{"op": "set_flag" \| "clear_flag", "flag": "..."}` | sets or clears a flag |
| `{"op": "start_quest" \| "complete_quest" \| "fail_quest", "quest": "..."}` | |
| `{"op": "complete_objective", "quest": "...", "objective": "..."}` | an objective of the quest's current stage |
| `{"op": "set_stage", "quest": "...", "stage": 30}` | moves the quest to that stage (starting it if needed) |
| `{"op": "set_var", "var": "...", "value": ...}`, `{"op": "add_var", "var": "...", "value": 1}` | story variables: numbers or text |
| `{"op": "give_item" \| "take_item", "item": "<item id>", "count": 1}` | the player's inventory |
| `{"op": "give_gold" \| "give_xp", "amount": 10}` | |
| `{"op": "reputation", "faction": "...", "value": 10}` | the player's reputation with a faction (and its members' disposition) |
| `{"op": "play_cutscene", "cutscene": "<id>"}` | plays `cutscenes/<id>.json` |

## `quests/<id>.json`

A quest is a series of numbered **stages**. Each has objectives, a line for the journal,
and where it goes when its objectives are done.

```json
{
  "name": "The thief",
  "description": "Someone has been stealing from the market.",
  "giver": "character/guard",
  "aliases": [
    { "id": "thief", "kind": "character", "fill": "character/mira" },
    { "id": "loot", "kind": "item", "fill": "silver-ring" }
  ],
  "stages": [
    { "index": 10, "journal": "The guard told me about a thief in the market.",
      "objectives": [{ "id": "guard", "text": "Talk to the guard", "type": "talk", "target": "character/guard" }] },
    { "index": 20, "journal": "Mira is the thief.",
      "objectives": [
        { "id": "find", "text": "Find Mira", "type": "talk", "target": "@thief" },
        { "id": "ring", "text": "Find the ring", "type": "have", "target": "@loot", "optional": true }
      ],
      "branches": [{ "if": [{ "var": "mercy", "op": ">=", "value": 1 }], "stage": 40 }] },
    { "index": 30, "journal": "Mira is in the cells.", "ends": "complete" },
    { "index": 40, "journal": "I let Mira go.", "onEnter": [{ "op": "set_flag", "flag": "spared" }], "ends": "complete" },
    { "index": 90, "journal": "Mira got away.", "ends": "fail" }
  ],
  "rewards": { "experience": 50, "gold": 20, "items": "potion*2", "faction": "watch", "reputation": 10 },
  "onComplete": [{ "op": "set_flag", "flag": "thief_caught" }],
  "onFail": [{ "op": "set_flag", "flag": "thief_escaped" }]
}
```

- **Stages** are entered in turn. Entering one adds its `journal` line, runs its
  `onEnter`, and, with `ends`, completes or fails the quest there. A stage's objectives
  can be done **in any order**; `optional` ones don't hold it back. When the rest are done,
  the first **branch** whose `if` holds picks the next stage, else `next`, else the stage
  after it in the list.
- **Aliases** are roles: `"@thief"` in a target is the alias's `fill`. The same quest
  works with whoever plays the part (the AI Planner fills them, `ai-planner.md` §9).
- **Rewards** are handed out when the quest completes: experience, gold, items
  (`"potion*2, iron-sword"`) and reputation with a faction.
- **Failing**: a stage that `ends: "fail"`, a `fail_quest` effect, or the one you
  escort or defend dying. `onFail` runs; a failed quest doesn't start again.
- The older form, `"objectives": [...]` with no stages, still works: each objective is a
  stage of its own (10, 20, ...), done one after another. The Story panel saves a quest in
  that form when it says everything.

The quest's first open required objective is shown top-left, with progress ("(1/3)",
"(12s)"), and a marker points the way to it (below).

| `type` | `target` | Done when |
|---|---|---|
| `defeat` | a prefab path, or a team (`Enemy`) | `count` of them die |
| `collect` | a prefab path | `count` of them are picked up |
| `talk` | a World Bible key | the player talks to an entity whose Talk `character` is that key |
| `reach` | a Marker's name, or nothing (with a `scene`) | the player stands at that marker; with only a `scene`, arriving there |
| `deliver` | a World Bible key; `item` (and `count`) | the player talks to them carrying the item, which is handed over |
| `use`, `examine` | an entity's Name, Interactable name or Actor key | the player uses it (E), or a script calls `used(name)` |
| `have` | an item id; `count` | the player carries that many |
| `survive` | — ; `seconds` | that long passes |
| `defend` | someone; `seconds` | that long passes with them alive (their death fails the quest) |
| `escort` | someone; `to` (a marker) | they're within 1.5 cells of the marker (their death fails the quest) |

Any objective can add `"scene": "scenes/<name>.aige"`: it then only counts in that
scene ("Clear the rats" with `"scene": "scenes/cellar.aige"` ignores rats elsewhere).
A `reach` objective with a `scene` and no target ("Go down to the cellar") is done as
soon as the player is there, including when they already are when it comes up.

While any quest is active, defeating every enemy no longer ends the game: the quest is
the goal.

Scripts can drive the same state; see `start_quest`, `set_stage`, `fail_quest`,
`get_var`, `set_var`, `quest_state` and the other story functions in
[scripting.md](scripting.md).

## The journal and markers

**J** opens the journal: every quest started, active ones first, then finished and
failed ones. The highlighted quest shows its description, its stages' journal lines so
far and what's left to do. While playing, a small marker on a ring around the middle of
the screen points the way to the current objective's target, with its distance, when
it's more than a few steps away and in this scene: a marker, a character, an item lying
about. Project Settings ▸ **Story** turns the markers and the journal off.

## Cutscenes: `cutscenes/<id>.json`

A cutscene is a timeline of tracks; each key happens at its time (`at`, seconds) and some
last a while (`seconds`). The world waits while it plays: no input, no AI, no combat.
**Esc**, **Space** or **Enter** skips it: every key left is applied at once (actors jump
to their marks, logic runs), so the story ends up the same.

```json
{
  "name": "The arrest",
  "letterbox": true,
  "tracks": [
    { "type": "screen", "keys": [{ "at": 0, "op": "fade_in", "seconds": 1 },
                                 { "at": 0.5, "op": "title", "text": "Later that night", "seconds": 2 }] },
    { "type": "actor", "actor": "@thief", "keys": [{ "at": 1, "op": "move", "marker": "cells", "speed": 2 },
                                                   { "at": 4, "op": "face", "target": "player" }] },
    { "type": "camera", "keys": [{ "at": 1, "op": "follow", "actor": "@thief" }] },
    { "type": "dialogue", "keys": [{ "at": 4, "speaker": "@thief", "text": "Fine. You win.", "seconds": 2,
                                     "voice": "assets/voice/mira-win.wav" }] },
    { "type": "audio", "keys": [{ "at": 0, "op": "music", "path": "assets/music/tense.ogg" }] },
    { "type": "logic", "keys": [{ "at": 6, "op": "wait_input" },
                                { "at": 6, "do": [{ "op": "set_stage", "quest": "thief", "stage": 30 }] }] }
  ]
}
```

| Track | Keys |
|---|---|
| `actor` (with `actor`) | `move` (to a `marker` or `x`/`y`, at `speed`), `place` (jump there), `face` (a `marker`, an actor or `"player"`), `show`, `hide` |
| `dialogue` | a line: `speaker`, `text`, `seconds`, `voice` |
| `camera` | `follow` (an `actor`), `move` (to a `marker` or `x`/`y`), `zoom` (`value`); `shake` comes with Phase 18 |
| `audio` | `music` (`path`; `""` for the scene's own), `sound` (`path`) |
| `screen` | `fade_out`, `fade_in` (`seconds`), `title` (`text`, `seconds`) |
| `logic` | `do` (effects, as in dialogue), `emit` (`event`, for scripts), `wait_input` (holds until a key) |

Actors are named as quests name people: an Actor key, a World Bible key, an entity's
Name, `"player"`, or `"@alias"` (from the quest that started it, else any active quest
with that alias). A cutscene plays from a `play_cutscene` effect (a dialogue choice, a
quest stage's `onEnter`), from Lua (`play_cutscene("arrest")`), or from a
**CutsceneTrigger** component: the player coming within its `radius` plays it, once
unless `once` is off (remembered in saves as the flag `cutscene:<id>`). `length` sets
the running time when it should outlast its last key. Scripts hear `cutscene_started`
and `cutscene_ended`. The timeline editor comes with Phase 18.

## Routines: `schedules/<id>.json`

A day for an NPC. Each entry lasts until the next one starts, and the last runs past
midnight:

```json
{
  "name": "Tamsin's day",
  "entries": [
    { "from": "06:00", "activity": "wander", "place": "herb_garden", "radius": 2 },
    { "from": "13:00", "activity": "stay", "place": "village_well" },
    { "from": "21:00", "activity": "sleep", "place": "tamsin_home" }
  ]
}
```

| `activity` | What they do at `place` |
|---|---|
| `stay` | Stand there. |
| `wander` | Stroll within `radius` cells of it. |
| `sleep` | Go there and disappear indoors. They can't be talked to until the next entry. |

Places are **Markers**: entities with a Marker component (`name`). They're invisible in
play and labelled in the editor. Give an NPC a **Routine** component (`schedule`,
`speed`) to follow a schedule. They find their way around walls (A* pathfinding).

An entry can say which scene it happens in: `"scene": "scenes/tavern.aige"` (its `place`
is then a marker of that scene). While someone's day puts them in another scene than the
one being played, they're **away**: hidden, with nobody to talk to. Place the NPC, with
the same Routine, in every scene their day visits.

## Places and scenes

A World Bible **location** can name the scene that shows it, in its header:

```
---
name: The inn's cellar
summary: Damp, and full of rats.
scene: scenes/cellar.aige
---
```

The Scene panel shows each scene's place, and the AI gets the open scene's location
entry in full, so what it builds or changes there follows the canon. One scene has at
most one location. Renaming a scene updates its location, and the quests and schedules
that name it.

## Saved games

**F6** saves the game while playing, and **F9** loads it. A save holds the scene, where
the player stands and their health, the clock, and the story: flags, variables, the
journal and every quest's stage and progress. The scene itself loads as it is on disk, so
enemies come back.
- In the editor, **Play ▸ Continue from save** starts Play from the save. Editor saves
  are kept in the project's `.cache/saves/`, apart from players' saves.
- In the game, saves go to `%LOCALAPPDATA%\AIGE\saves\<game>\quicksave.json`.

The game clock starts at **Time.startHour** and runs at **Time.minutesPerSecond** game
minutes per real second (Project Settings). It shows top-right in play when anyone has a
routine. Scripts read it with `time_of_day()` and `day()`.

## Writing with the AI

Ask in the AI panel ("give Old Tamsin a quest to…", "what would she say about…"). The
AI sees every World Bible entry by name. It also gets the full text of the ones related
to your request, plus the tone guide. Its answers are `write_dialogue` / `write_quest`
operations, shown in Diff Review as the files they'll write, and checked like the Story
tab checks them. Any new named character or place it invents is added to the World
Bible in the same proposal.

## Checks

The editor reports, and the tests enforce for the demo:
- **Broken files:** files that don't parse.
- **Broken links:** links to nodes that don't exist, nodes that can't be reached, more
  than 4 choices, and unknown quests, objectives, effects or objective types.
- **References:** World Bible keys that don't exist, and defeat/collect targets that
  aren't prefabs. Objective and schedule scenes that don't exist, reach markers missing
  from their scene (or from every scene), schedule places missing from their entry's
  scene, and location scenes that don't exist or are claimed twice.
