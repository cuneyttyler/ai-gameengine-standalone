# Dialogue and quests

A project's conversations and quests are JSON files next to its scenes and scripts. The
engine reads them in `engine/gameplay/Story` (`development_plan.md` 3.2b). Characters
are referred to by their World Bible key (`character/villager`, see `world/`), so the
game shows the canon name, "Old Tamsin", and quests can say "talk to her".

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

When the last objective is done, the quest completes and `onComplete` runs. While any
quest is active, defeating every enemy no longer ends the game: the quest is the goal.

Scripts can drive the same state; see `start_quest`, `quest_state`, `set_flag` and the
other story functions in [scripting.md](scripting.md).

## Checks

The editor reports, and the tests enforce for the demo:
- **Broken files:** files that don't parse.
- **Broken links:** links to nodes that don't exist, nodes that can't be reached, more
  than 4 choices, and unknown quests, objectives, effects or objective types.
- **References:** World Bible keys that don't exist, and defeat/collect targets that
  aren't prefabs.
