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
Tamsin"). **1-4** pick a choice. **Up/Down** and **Enter** do the same. **Space**,
**Enter** or **E** continue past a line without choices. The world pauses while you
talk.

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
      "choices": [
        { "text": "I'm here about the keep.", "next": "offer" },
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
- **Nodes** have `text`, then either up to 4 `choices` or a `next` node. An empty `next`
  ends the conversation. A node's `do` runs when it's shown.
- **Choices** can have `if` (all must hold, or the choice is hidden) and `do` (runs when
  picked).

**Conditions**: `{"flag": "name"}`, `{"flag": "name", "is": false}`,
`{"quest": "<id>", "is": "inactive" | "active" | "completed"}`.

**Effects**: `{"op": "set_flag", "flag": "..."}`, `{"op": "clear_flag", "flag": "..."}`,
`{"op": "start_quest", "quest": "..."}`, `{"op": "complete_quest", "quest": "..."}`,
`{"op": "complete_objective", "quest": "...", "objective": "..."}`.

## `quests/<id>.json`

```json
{
  "name": "Quiet the courtyard",
  "description": "Old Tamsin asked you to put three of the restless dead back in the ground.",
  "giver": "character/villager",
  "objectives": [
    { "id": "skeletons", "text": "Defeat skeletons in the courtyard", "type": "defeat",
      "target": "prefabs/skeleton.prefab", "count": 3 },
    { "id": "report", "text": "Return to Old Tamsin", "type": "talk", "target": "character/villager" }
  ],
  "onComplete": [{ "op": "set_flag", "flag": "courtyard_quieted" }]
}
```

Objectives are done **in order**, and only the current one counts. The current one is
shown top-left, with progress ("(1/3)").

| `type` | `target` | Done when |
|---|---|---|
| `defeat` | a prefab path, or a team (`Enemy`) | `count` of them die |
| `collect` | a prefab path | `count` of them are picked up |
| `talk` | a World Bible key | the player talks to an entity whose Talk `character` is that key |
| `reach` | a Marker's name, or nothing (with a `scene`) | the player stands at that marker; with only a `scene`, arriving there |

Any objective can add `"scene": "scenes/<name>.aige"`: it then only counts in that
scene ("Clear the rats" with `"scene": "scenes/cellar.aige"` ignores rats elsewhere).
A `reach` objective with a `scene` and no target ("Go down to the cellar") is done as
soon as the player is there, including when they already are when it comes up.

When the last objective is done, the quest completes and `onComplete` runs. While any
quest is active, defeating every enemy no longer ends the game: the quest is the goal.

Scripts can drive the same state; see `start_quest`, `quest_state`, `set_flag` and the
other story functions in [scripting.md](scripting.md).

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
the player stands and their health, the clock, and the story: flags and every quest's
progress. The scene itself loads as it is on disk, so enemies come back.
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
