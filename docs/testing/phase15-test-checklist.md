# Phase 15 — Manual Test Checklist

The user-test checkpoints from Phase 15 (`development_plan.md`, "Narrative runtime v2"),
for one end-of-phase pass. Automated tests cover the logic: v1 quests loading as stages,
a branching quest with aliases and rewards, failing by stage, by effect and by an escorted
character's death, the new objective types, variables and world conditions, saves, skill
checks, validation, cutscene timing, waits and skipping, and the session playing a
triggered cutscene and paying a reward (`narrative_test.cpp`, `test_story_validation.py`).
This list covers how it looks and feels in play. Formats are in `story.md`.

**Before you start**
- **Rebuild:** close the editor, then `cmake --build build --config Release --target editor sandbox`.
- A project with a few World Bible characters (World panel), Talk components on their
  entities, and some markers (Marker component) in the scene.
- The journal window is in the game and the sandbox (`sandbox.exe <project>`); the editor's
  Play shows the objective and its marker but not the journal.

## 15.1 Quests v2

- [ ] Story panel ▸ **+ Quest**: it opens with one stage. Add a second and a third stage,
      give each a journal line, put two objectives in stage 20 and mark one **optional**.
      Save. The file (`quests/<id>.json`) has `"stages"`.
- [ ] Open an old quest (objectives only): it shows as one stage per objective. Change an
      objective's text and save: the file keeps its `"objectives"` form.
- [ ] Add a **Role** "thief" filled with a character; set an objective's target to
      "@thief". In play, talking to that character completes it.
- [ ] A **branch** in stage 20 (if variable `mercy` ≥ 1, go to stage 40). Set the
      variable from a dialogue choice (do ▸ set variable) and check the quest goes to 40;
      without it, to 30.
- [ ] A stage with **ends: fails the quest** reached by a dialogue choice (do ▸ set quest
      stage): "Quest failed: …" shows and the quest's **When failed** effects run.
- [ ] **Rewards** (experience 50, gold 20, items "potion*2"): completing the quest shows
      "Received …" and the inventory (I) has them.
- [ ] New objective types: **deliver** (talk to them carrying the item: it leaves your
      inventory), **use** (a lever with an Interactable named "lever"), **have** (pick up the
      item), **survive** (the objective counts down "(12s)"), **escort** (the character
      reaching the marker; killing them fails the quest).

## 15.2 Variables and conditions

- [ ] A dialogue choice visible only with an item (if ▸ player has item), only at night
      (if ▸ time of day 20–6), only at a level, only at a quest's stage: each appears and
      disappears as expected.
- [ ] Save (F6), change things, load (F9): flags, variables, the journal and stages come back.

## 15.3 Dialogue v2

- [ ] A node with six choices: keys 1–6 pick them.
- [ ] A choice with a **skill check** (speech ≥ 5) and an "else" node: it shows
      "[Speech 5] …"; with low speech the conversation goes to the else node and "Failure"
      shows; raise the attribute and it goes to the next node with "Success".
- [ ] A node with a **portrait** (an image in assets/) shows it above the box; a **voice**
      sound plays with the line.
- [ ] The dialogue's **Graph** view: nodes in columns from the start, choices numbered on
      the arrows, failed checks dashed red. Click a node: its editor shows below; links
      ("→") jump to the other node.

## 15.4 Journal and markers

- [ ] In the game (or sandbox), **J** opens the journal: active quests first, the
      highlighted one's journal lines and open objectives; finished ones marked "(done)",
      "(failed)". J or Esc closes it.
- [ ] With an objective far away (a marker, a character), a yellow pip on a ring around the
      middle points to it with its distance; it goes when you're close. Project Settings ▸
      Story ▸ objectiveMarkers off hides it.

## 15.5 Cutscenes

- [ ] Write `cutscenes/arrest.json` (the example in `story.md`, with your markers and
      characters). Give an entity a **CutsceneTrigger** (cutscene "arrest"). Walking into it:
      black bars slide in, the title card shows, the character walks to the marker, the
      camera follows, the line is spoken, and play resumes after.
- [ ] Walk back into the trigger: it doesn't play again (once). Save and load: still not.
- [ ] Press **Esc** during it: it ends at once, the character stands at their mark and the
      logic (e.g. set_stage) has happened. Esc doesn't open the pause menu then.
- [ ] A `wait_input` key: "Space Continue" shows and the cutscene waits for it.
- [ ] `play_cutscene("arrest")` from a Lua script and from a dialogue choice (do ▸ play
      cutscene) both start it.

## The AI

- [ ] Ask the AI for "a quest with two stages where the player can spare or arrest a thief,
      with a reward": the proposal has `"stages"`, an alias or branch, and passes the checks.

## The AI panel (cutscenes)

- [ ] Ask the AI "a short cutscene where the guard walks to the gate and says halt, played
      when the player first comes near": the proposal has a write_cutscene (and a
      scene.addMarker if the gate marker is new) and a CutsceneTrigger naming it; it passes
      the checks and plays in Play.
- [ ] A proposal whose dialogue plays a cutscene that doesn't exist is flagged before review.
