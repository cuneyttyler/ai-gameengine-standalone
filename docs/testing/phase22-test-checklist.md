# Phase 22 — Manual Test Checklist

The user-test checkpoints from Phase 22 (`development_plan.md`, "AI Planner I: designing the
game"), for one end-of-phase pass. Automated tests cover the logic with scripted AI answers: the
Brief, profiles, scope and window; merging readings; a short ARPG designed and validated; resuming
after closing mid-plan; an edit regenerating only its dependents; revisions in words; the offline
Brief (`planner_test.cpp`); and each AI step's think, check and repair (`test_planner.py`). This
list is about using it with the real AI. The design is `ai-planner.md` (see "As built").

**Before you start**
- **Rebuild:** close the editor, then `cmake --build build --config Release --target editor sandbox`.
- A new, empty project (the plan writes `planner/`, the World Bible and the story graph).
- The AI service running with a model (the Planner makes many calls: a short ARPG is roughly
  40–80 calls).

## ① Concept

- [ ] AI panel: "plan a game: a short dark-fantasy action RPG about a disgraced warden recovering a
      stolen amulet, three towns and a dungeon, about four hours". The Planner panel opens (Y
      toggles it); the AI panel shows a plan card.
- [ ] The Game tab: title, logline, genre arpg, perspective, play time ~4. Fields the readings
      disagreed on show **differ** (hover: the other readings); profile defaults show **assumed**.
- [ ] Change the genre to rpg: the assumed fields change to the rpg profile's (perspective
      top-down); fields you set stay.
- [ ] Story, Characters, Places, Systems, Difficulty, Visuals and Plan tabs all show values;
      "To decide" lists open questions.
- [ ] **Continue**: the outline is written.

## ② Outline

- [ ] Three columns: story (logline, synopsis, acts), cast (one player, roles), places and
      connections. World tab: the characters and places are there as entries.
- [ ] Project ▸ Story graph: the story, acts, characters with their roles, places.
- [ ] **Continue** (the outline's autonomy is "ask").

## ③ Story and ④–⑧

- [ ] The Story view fills act by act: scene cards first, then ✓ as each scene is written. A scene's
      document shows its beats, events, facts and summary; Why? shows its decision and rationale.
- [ ] Story graph: facts appear over time (a death, an item changing hands); a branch's facts only
      on its branch.
- [ ] Characters, quests and places fill in; places show their purpose; the crypt (or wherever the
      main quest's item is) lists the story anchor the quest needs.
- [ ] Systems shows records (the reward items); Visuals a theme, screens and HUD widgets.

## ⑨ Validate

- [ ] The report: errors and warnings, each with its rule and subject. With no errors: "The design
      is validated (baseline n)". Accept… keeps a finding with your reason.

## Editing and robustness

- [ ] Open a character, change its need in the document, Save changes: the stepper shows stale
      counts on Quests and Systems; **Regenerate stale** redoes only those.
- [ ] Lock a place, Regenerate it: refused ("locked").
- [ ] Close the editor while the Story stage is writing scenes; reopen the project and the Planner
      panel: it carries on from where it was (no scene written twice).
- [ ] In the AI panel, "make Tamsin want forgiveness": the plan card says it went to the Planner;
      Tamsin's document changes and its dependents go stale.
- [ ] Places ▸ a place ▸ **Build…**: the Build panel opens on ① Confirm with that place's Brief.
- [ ] Turn the AI off and start a new plan: the Brief comes from the request's words; Continue waits
      for the AI with a message.

## AI panel

- [ ] "make me a cozy farming life-sim" starts a plan (or asks to replace the existing one).
- [ ] An ordinary change ("make the bandits faster") still goes through a normal proposal.
