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

Questions: what objective types are there? how do I make a kill quest? how do I make a fetch quest? how do I make an
escort quest? how do I limit an objective to one scene?

## Roles (aliases)

A role is written `@thief` in an objective's target (or in a cutscene's actor) and filled in **Roles (aliases)**: an id,
a kind (character, item, location, entity) and what fills it (a World Bible character, or an item id, marker or name).
The same quest then works with whoever plays the part; change the fill and every objective follows. The AI Planner fills
roles when it builds a game.

Example: role `thief` (character, `character/mira`) and role `loot` (item, `silver-ring`); objectives "Find Mira" talk to
`@thief`, "Find the ring" have `@loot`.

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

Questions: how do I give quest rewards? how do I fail a quest? what happens when a quest completes? does killing all
enemies end the game?

## The journal, the tracker and quest markers

- **J** opens the journal: every quest started, active ones first, then finished and failed ones. The highlighted quest
  shows its description, its stages' journal lines so far and what's left to do.
- The quest's first open required objective is shown top-left with progress ("(1/3)", "(12s)").
- A small marker on a ring around the middle of the screen points to the current objective's target with its distance,
  when it's more than a few steps away and in this scene: a marker, a character, an item lying about.
- Project Settings ▸ **Story** turns the markers and the journal off.

Questions: how do I open the quest journal? how do I turn off quest markers? where is the current objective shown?

## Quests from scripts and checks

Lua: `start_quest(id)`, `quest_state(id)` ("inactive", "active", "completed", "failed"), `quest_stage(id)` (0 when not
active), `set_stage(id, stage)`, `fail_quest(id)`, `complete_objective(quest, objective)` (if it's open in the current
stage), `used(name)` (for use and examine objectives), and story variables `get_var` / `set_var`.

The Story tab checks quests: unknown quests, objectives, effects or objective types; World Bible keys that don't exist;
defeat and collect targets that aren't prefabs; objective scenes that don't exist; reach markers missing from their
scene (or from every scene). The AI panel writes quests with `write_quest`, checked the same way.

Saves (F6 / F9 and the save slots) keep flags, variables, the journal and every quest's stage and progress.

Questions: how do I advance a quest from a script? can the AI write a quest? are quests saved?
