# Phase 23 — Manual Test Checklist

The user-test checkpoints from Phase 23 (`development_plan.md`, "AI Planner II: building the
game"), for one end-of-phase pass. Automated tests build the designed ARPG with the place
builder's own fallbacks, check its scenes, gateways, bindings, files and playtest, a failed place's
placeholder, and a life-sim made the same way (`gamebuild_test.cpp`). This list is about doing it
with the real AI and looking at the result. The design is `ai-planner.md` (§17 "As built").

**Before you start**
- **Rebuild:** close the editor, then `cmake --build build --config Release --target editor sandbox`.
- A project with a validated plan (Phase 22's checklist, to "The design is validated").

## ⑩ Building every place

- [ ] Planner panel ▸ **⑩–⑬ Build** ▸ **Build automatically** (or "build the game" in the AI panel).
      The rows list each place with its scene, build type and status; they turn **built** one by one
      (the editor stays responsive).
- [ ] **Open build** on a built row: the Build panel shows its build (Brief, plan, layout); its log
      shows the planner's continues.
- [ ] `scenes/` has a scene per place; opening each shows the place laid out.
- [ ] In the start scene, gateways lead to the connected places; walking into one in Play goes there
      and arriving puts you at the matching marker; walking back returns you.
- [ ] Start a new plan's build with **Build each place myself**: the first row says **needs-you**;
      Open build, take it through Confirm, Plan and Approve, then **Built**: the next row starts.
- [ ] Break a place's Brief (its document ▸ brief ▸ buildType "sky-city"), Build again: that row
      becomes a **placeholder** room and the rest goes on; **Retry** after fixing it rebuilds it.

## ⑪ Binding

- [ ] The Bindings table: each quest alias with how it was bound (fixed, story-anchor, made) and where.
- [ ] Characters stand in their homes (open their scenes); talking to a quest giver offers the quest.
- [ ] The main quest's spot has a marker (`q-main/<alias>`) where the story anchor is.

## ⑫ The game's files and the playtest

- [ ] `quests/`, `dialogue/`, `cutscenes/` (for cutscene scenes), `records/` and `game.json` exist;
      Project ▸ Story shows the quests and dialogues without problems.
- [ ] The dialogues read in each character's voice and only mention what they know.
- [ ] The playtest lists each quest as **completed** (or stuck, with the stage and why) and its
      checks; a failing check names what's missing.

## ⑬ Result

- [ ] The headline ("Planned and built …: n scenes, n characters, n quests, n places; n of n quests
      can be finished"), asked vs made.
- [ ] **Play from the start**: the start scene opens and plays; the main quest can be started by
      talking to its giver and its first objective leads somewhere real.

## Robustness

- [ ] Close the editor while places are building; reopen and open the Planner panel: the build goes
      on from the row it was on.

## AI panel

- [ ] "build the game" with a validated plan starts ⑩; without one it says the plan must be validated.
- [ ] Asking "how far is the build?" — the AI answers from context `plan.build`.
