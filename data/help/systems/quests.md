# Quests

Making quests in Project ▸ Story: stages, objectives and their types, roles (aliases), branches, rewards, completing and
failing, the journal and quest markers, and driving quests from dialogue, Reactions and scripts.

## Quests: overview and the quest form

A quest is a file `quests/<id>.json`: a series of numbered **stages**, each with objectives, a journal line, and where it
goes when its objectives are done. Open **Project ▸ Story**, press **+** next to **Quests**, and fill in:

- **File** (`quests/<id>.json`), **Name**, **Given by** (a World Bible character), **Description**.
- **Roles (aliases)**: named parts the quest refers to as `@name`.
- **Stages**: each with objectives, branches, "then", and effects on reaching it.
- **Rewards**: experience, gold, items, reputation.
- **When completed** and **When failed**: effects.

**Save** (Ctrl+S), **Revert**, **Delete…**. Problems are listed at the top of the form and marked ⚠ in the list.

A quest is started by an effect (`start_quest` from a dialogue choice, a Reaction's `do`, another quest's stage), by Lua
`start_quest(id)`, or by `set_stage`, which starts it if needed.

### Example: Rats in the Cellar, for Vara Bell

1. Open **Project ▸ Story** and press **+** next to **Quests**.
2. **File** `quests/rats-in-the-cellar.json`; **Name** `Rats in the Cellar`; **Given by** Vara Bell
   (`character/vara-bell`); **Description** `Vara wants the rats out of the inn's cellar.`
3. Stage 10: journal `Vara asked me to clear the rats from her cellar.`; objective `rats`, text `Kill the rats`, type
   **defeat**, target `prefabs/rat.prefab`, count `6`, only in scene `scenes/goose-cellar.aige`.
4. Stage 20: journal `The cellar is clear. I should tell Vara.`; objective `tell`, type **talk to**, target
   `character/vara-bell`; ending **completes the quest**.
5. **Rewards**: experience `50`, gold `20`, items `healing-potion*2`. **Save**.

```json
{
  "name": "Rats in the Cellar",
  "giver": "character/vara-bell",
  "description": "Vara wants the rats out of the inn's cellar.",
  "stages": [
    { "index": 10, "journal": "Vara asked me to clear the rats from her cellar.",
      "objectives": [{ "id": "rats", "text": "Kill the rats", "type": "defeat", "target": "prefabs/rat.prefab",
                       "count": 6, "scene": "scenes/goose-cellar.aige" }] },
    { "index": 20, "journal": "The cellar is clear. I should tell Vara.", "ends": "complete",
      "objectives": [{ "id": "tell", "text": "Tell Vara", "type": "talk", "target": "character/vara-bell" }] }
  ],
  "rewards": { "experience": 50, "gold": 20, "items": "healing-potion*2" }
}
```

6. In Vara's dialogue, give a choice the effect start quest `rats-in-the-cellar`.

In play the tracker shows "Kill the rats (0/6)"; after the sixth rat, "Tell Vara"; talking to her completes the quest
and hands out the rewards.

Questions: how do I make a quest? how do I start a quest? where are quests saved?

## Stages

Stages are numbered (10, 20, 30…) and entered in turn. Each stage has:

- its number, and a **journal** line ("The journal's line for this stage"), added to the journal when it's entered;
- an ending: **goes on**, **completes the quest** or **fails the quest** (on reaching this stage);
- **objectives**, which can be done in any order; **optional** ones don't hold the stage back;
- **branches** (+ Branch): "go to stage N" when conditions hold;
- **then**: the stage to go to next (empty: the next stage below in the list);
- **On reaching it**: effects run when the stage is entered.

When a stage's required objectives are done, the first branch whose conditions hold picks the next stage, else "then",
else the next one in the list.

Example: stage 20 "Mira is the thief", objectives find Mira (talk) and find the ring (have, optional); a branch to stage
40 when variable `mercy` ≥ 1; then stage 30.

An older quest form with only an `objectives` list (no stages) still works: each objective is a stage of its own (10, 20…)
done one after another. The Story tab saves a quest in that form when it says everything.

### Example: The Stolen Ring, with a mercy branch

1. In Project ▸ Story, open `quests/stolen-ring.json`.
2. Stage 10: journal `Lord Hale's ring was stolen.`; objective talk to `character/mira`.
3. Stage 20: journal `Mira is the thief.`; objectives find Mira (talk to `character/mira`) and `ring` (have item
   `silver-ring`, tick **optional**); **+ Branch**: go to stage `40` when variable `mercy` ≥ `1`; **then** `30`.
4. Stage 30: journal `I turned Mira in.`; ending **completes the quest**.
5. Stage 40: journal `I let Mira go.`; **On reaching it**: set flag `mira_spared`; ending **completes the quest**.

```json
{ "index": 20, "journal": "Mira is the thief.",
  "objectives": [
    { "id": "find", "text": "Confront Mira", "type": "talk", "target": "character/mira" },
    { "id": "ring", "text": "Get the ring back", "type": "have", "target": "silver-ring", "optional": true }
  ],
  "branches": [{ "if": [{ "var": "mercy", "op": ">=", "value": 1 }], "stage": 40 }],
  "next": 30 }
```

If a dialogue choice set `mercy` to 1 while talking to Mira, the quest goes to stage 40; otherwise to 30. The ring is a
bonus: the stage moves on without it.

Questions: how do quest stages work? how do I branch a quest? how do I end a quest at a stage? what does optional mean?

## Objective types

Each objective has an id, its text ("Shown on screen"), a type and a target:

| type | target | done when |
|---|---|---|
| defeat | a prefab, or a team ("Any enemy") | that many of them die (count) |
| collect | a prefab | that many are picked up |
| talk to | a World Bible character | the player talks to an entity whose Talk character is that key |
| reach | a Marker's name, or nothing (with a scene) | the player stands at the marker; with only a scene, arriving there |
| deliver to | a World Bible character, plus an item (and count) | the player talks to them carrying the item, which is handed over |
| use / examine | an entity's Name, Interactable name or Actor key | the player uses it (E), or a script calls `used(name)` |
| have item | an item id, count | the player carries that many |
| survive | seconds | that long passes |
| defend | someone, seconds | that long passes with them alive (their death fails the quest) |
| escort | someone, to (a marker) | they're within 1.5 cells of the marker (their death fails the quest) |

Any objective can be limited to a scene ("Only counts in this scene"): "Clear the rats" in `scenes/cellar.aige` ignores
rats elsewhere. A reach objective with a scene and no target ("Go down to the cellar") is done as soon as the player is
there, even if already there when it comes up.

### Example: one stage of each common kind of objective

The quest "Supplies for the Keep" has a stage with four objectives, done in any order:

1. In Project ▸ Story, open `quests/keep-supplies.json`, stage 10, and add:
   - `wolves`: **defeat**, target `prefabs/wolf.prefab`, count `3`, text `Thin out the wolves`;
   - `herbs`: **collect**, target `prefabs/moonleaf.prefab`, count `5`;
   - `grain`: **deliver to**, target `character/captain-rook`, item `grain-sack`, count `2`;
   - `gate`: **reach**, target the Marker `keep-gate`.
2. Stage 20: `escort`: **escort**, target `character/elda`, to `ruins-entrance`.

```json
"objectives": [
  { "id": "wolves", "text": "Thin out the wolves", "type": "defeat", "target": "prefabs/wolf.prefab", "count": 3 },
  { "id": "herbs", "text": "Pick moonleaf", "type": "collect", "target": "prefabs/moonleaf.prefab", "count": 5 },
  { "id": "grain", "text": "Bring Rook two sacks of grain", "type": "deliver", "target": "character/captain-rook",
    "item": "grain-sack", "count": 2 },
  { "id": "gate", "text": "Go to the keep gate", "type": "reach", "target": "keep-gate" }
]
```

The tracker shows the first open one with progress ("Thin out the wolves (1/3)"). Talking to Rook with two sacks hands
them over and ticks off `grain`. In stage 20, Elda dying fails the quest.

Questions: what objective types are there? how do I make a kill quest? how do I make a fetch quest? how do I make an
escort quest? how do I limit an objective to one scene?

## Roles (aliases)

A role is written `@thief` in an objective's target (or in a cutscene's actor) and filled in **Roles (aliases)**: an id,
a kind (character, item, location, entity) and what fills it (a World Bible character, or an item id, marker or name).
The same quest then works with whoever plays the part; change the fill and every objective follows. The AI Planner fills
roles when it builds a game.

Example: role `thief` (character, `character/mira`) and role `loot` (item, `silver-ring`); objectives "Find Mira" talk to
`@thief`, "Find the ring" have `@loot`.

### Example: the same thief quest with a different thief

1. In `quests/stolen-ring.json`, under **Roles (aliases)**, add role `thief` (kind character, fill `character/mira`)
   and role `loot` (kind item, fill `silver-ring`).
2. Write the objectives against the roles: "Confront the thief" talk to `@thief`; "Get it back" have `@loot`.

```json
"aliases": [
  { "id": "thief", "kind": "character", "fill": "character/mira" },
  { "id": "loot", "kind": "item", "fill": "silver-ring" }
],
```

3. Later, change the `thief` fill to `character/old-tobin` and `loot` to `gold-locket`, and **Save**.

Every objective now points at Old Tobin and the locket, and the quest marker leads to Tobin, with nothing else changed.

Questions: what are quest aliases? how do I reuse a quest with a different character?

## Rewards, completion and failure

- **Rewards** are handed out when the quest completes: experience, gold, items (`potion*2, iron-sword`) and reputation
  with a faction (a faction id and an amount).
- **When completed:** effects run on completion (set a flag, start the next quest…).
- **Failing:** a stage that fails the quest, a `fail_quest` effect, or the person you escort or defend dying. **When
  failed** effects run; a failed quest doesn't start again.

While any quest is active, defeating every enemy no longer ends the game with Victory: the quest is the goal.

Events for scripts and Character Rules: `quest_started`, `quest_stage`, `objective_completed`, `quest_completed`,
`quest_failed` (text: the quest's id). A Character Rule can react to `quest:<id>:<stage>`.

### Example: rewards, a follow-up quest, and failing when the merchant dies

1. In `quests/merchant-escort.json`, **Rewards**: experience `100`, gold `40`, items `potion*2, iron-sword`,
   reputation with faction `merchants-guild` `10`.
2. **When completed**: start quest `merchant-second-run`; set flag `escorted_merchant`.
3. **When failed**: set flag `merchant_dead`.
4. Stage 10 has an **escort** objective for `character/aldo` to `market-gate`; if Aldo dies on the road the quest fails.

```json
"rewards": { "experience": 100, "gold": 40, "items": "potion*2, iron-sword", "faction": "merchants-guild", "reputation": 10 },
"onComplete": [
  { "op": "start_quest", "quest": "merchant-second-run" },
  { "op": "set_flag", "flag": "escorted_merchant" }
],
"onFail": [{ "op": "set_flag", "flag": "merchant_dead" }]
```

A script can react, too:

```lua
on("quest_failed", function(e)
  if e.text == "merchant-escort" then play_sound("assets/audio/sad-horn.wav") end
end)
```

Questions: how do I give quest rewards? how do I fail a quest? what happens when a quest completes? does killing all
enemies end the game?

## The journal, the tracker and quest markers

- **J** opens the journal: every quest started, active ones first, then finished and failed ones. The highlighted quest
  shows its description, its stages' journal lines so far and what's left to do.
- The quest's first open required objective is shown top-left with progress ("(1/3)", "(12s)").
- A small marker on a ring around the middle of the screen points to the current objective's target with its distance,
  when it's more than a few steps away and in this scene: a marker, a character, an item lying about.
- Project Settings ▸ **Story** turns the markers and the journal off.

### Example: following Rats in the Cellar in play

1. Press Play, talk to Vara Bell and accept the job.
2. A notice shows the quest's name; top-left reads "Kill the rats (0/6)". There's no marker yet: the rats are in the
   cellar scene.
3. Go down to the cellar: a marker on the ring round the screen's middle points to the nearest rat with its distance.
4. Press **J**: the journal lists Rats in the Cellar first, with its description, "Vara asked me to clear the rats from
   her cellar." and what's left.
5. For a game without these, open Project Settings ▸ **Story** and turn off the markers (and the journal).

Questions: how do I open the quest journal? how do I turn off quest markers? where is the current objective shown?

## Quests from scripts and checks

Lua: `start_quest(id)`, `quest_state(id)` ("inactive", "active", "completed", "failed"), `quest_stage(id)` (0 when not
active), `set_stage(id, stage)`, `fail_quest(id)`, `complete_objective(quest, objective)` (if it's open in the current
stage), `used(name)` (for use and examine objectives), and story variables `get_var` / `set_var`.

The Story tab checks quests: unknown quests, objectives, effects or objective types; World Bible keys that don't exist;
defeat and collect targets that aren't prefabs; objective scenes that don't exist; reach markers missing from their
scene (or from every scene). The AI panel writes quests with `write_quest`, checked the same way.

Saves (F6 / F9 and the save slots) keep flags, variables, the journal and every quest's stage and progress.

### Example: a shrine that advances a quest from a script

The quest `lost-pilgrim` has stage 10 "Find the shrine" with a **use** objective `pray` targeting `old-shrine`, and
stage 20 "Return to the abbot".

The shrine (Name `old-shrine`) has an Interactable (verb `Pray`, **event** `prayed`); pressing **E** at it already
counts for the use objective. Its script adds the rest:

```lua
on("prayed", function(e)
  if quest_state("lost-pilgrim") ~= "active" then
    notify("toast", "The shrine is silent.")
    return
  end
  set_var("prayers", (get_var("prayers") or 0) + 1)
  if get_var("prayers") >= 3 and quest_stage("lost-pilgrim") == 20 then
    set_stage("lost-pilgrim", 30)        -- a hidden blessing stage
  end
end)
```

Praying with the quest active ticks off `pray` and the quest moves to stage 20; praying three times in all jumps it to
stage 30. A script on something without an Interactable would call `used("old-shrine")` instead. To ask the AI instead, type "add a quest
where the abbot sends the player to pray at the old shrine"; it writes `quests/lost-pilgrim.json` with `write_quest`.

Questions: how do I advance a quest from a script? can the AI write a quest? are quests saved?
