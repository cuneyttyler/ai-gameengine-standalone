# Phase 3 — Manual Test Checklist

The user-test checkpoints from every Phase 3 milestone (`development_plan.md`), for one
end-of-phase pass. Automated coverage already checks the logic: 175 GoogleTest and 70
pytest cases. They include a scripted quest playthrough, a simulated NPC day, 60
generated dungeons checked for connectivity, golden recordings of real model output, and
contradiction fixtures. This list is for what tests can't judge: whether it reads well,
plays well and looks right.

**Before you start**
- **Rebuild:** close the editor, then run `cmake --build build --config Debug`.
- **AI:** has to be on (2.7) for the AI items; everything else works without it.
- **Undo:** `git checkout projects/demo` resets the demo if the AI changes it.
- **New tabs** in the bottom panel: **World**, **Story**, **Level** and **Art**.

## 3.2 — Talk, take the quest, finish it (most important)

- [ ] Play. Walk up to Old Tamsin (the villager): "E  Talk to Old Tamsin" appears. Press
      **E**: the dialogue box opens and the world stops moving.
- [ ] Choices with **1-4**, and with **Up/Down + Enter**. **Space** continues past lines
      without choices. "Who are you?" then "About the keep…" leads to the offer.
- [ ] Accept: "New quest: Quiet the courtyard" shows briefly, and the objective appears
      top-left with progress (0/3).
- [ ] Defeat three skeletons: the count goes up, then "Return to Old Tamsin". Clearing every
      skeleton no longer ends the game while the quest is active.
- [ ] Talk to her again: the quest completes and she thanks you. Talk once more: she
      still uses the "thanks" line.
- [ ] The same in the standalone player (`sandbox.exe`). The game's own text (dialogue,
      banners, objective) is crisp and readable.
- [ ] Story tab: open `tamsin`, add a node and a choice that leads to it, then save. Play:
      it's there. Break a link on purpose: a ⚠ and a message appear, and the file
      still saves.

## 3.1 — World Bible

- [ ] World tab: seven entries grouped by kind. Open Old Tamsin, change her summary, save.
      Rename an entry's id: entries that pointed at it follow.
- [ ] Delete an entry other entries refer to: the confirmation names them, and the
      dangling links show as problems.
- [ ] Edit a `world/...md` file in a text editor, then come back to the editor: the change
      shows.
- [ ] Search "who heals people": "Related by meaning" finds Old Tamsin.

## 3.2d / 3.3 — Writing with the AI, checked against canon

- [ ] "Old Tamsin should have something to say about the Wardens: add a choice to her
      conversation." The diff shows the whole dialogue file, with her existing lines kept.
      Apply and play it. **Does it read well and fit the tone?**
- [ ] "Give Old Tamsin a second quest for after the courtyard is quiet: bring her two of
      her red healing draughts." This runs as a task (quest, then dialogue). Review the
      result, apply, play it.
- [ ] "Write a short conversation where Tamsin says she's been the village blacksmith for
      forty years." The card shows "Checking against the World Bible…", then flags it
      against Old Tamsin with the canon line it contradicts. **Ask AI to fix** produces a
      consistent version.
- [ ] Something consistent but new ("add a traveling tinker who passes through Brookvale")
      comes back "✓ Consistent". A new character is added to the World Bible in the same
      proposal.
- [ ] World tab: edit an entry to contradict another (Aren "a veteran of thirty years"),
      save, and click **Check against canon**: it's flagged.

## 3.4 — Dungeons

- [ ] Level tab: the map preview updates as you change rooms, size, loops and seed (🎲).
      **Generate** builds it east of the village. Ctrl+Z removes it.
- [ ] Play through two or three generated dungeons. **Are they navigable, and fun to
      clear?** Do they get harder further in? Do rooms feel too similar, too big, too
      small?
- [ ] "Build a small dungeon east of the village with skeletons and a couple of potions."
      Review shows a map; apply, play.

## 3.5 — Routines

- [ ] "Give Old Tamsin a daily routine: herb garden in the morning, the village well in
      the afternoon, home at night. Add the places she needs." Runs as a task: markers,
      schedule, Routine. Markers show as labelled diamonds while editing.
- [ ] Project Settings → Time → minutesPerSecond 60 (a day in 24 s). Play: the clock shows
      top-right. She walks between places, around walls, and wanders in the garden. At
      night she disappears home and "E" doesn't offer to talk; in the morning she's back.
- [ ] Story tab → Routines: change a time or place, save, play.
- [ ] **Does a village with a routine or two feel alive?** (Ask the AI for a second
      villager.)

## 3.6 — Art

- [ ] Art tab: "a wooden barrel with iron bands", variants "mossy, broken", Generate. Three
      images in about 30 s. Each is cut out cleanly in the gallery (checkerboard
      background).
- [ ] **Make prefab** on one, place it in the scene and play. **Does it look acceptable next
      to the existing art?** (Height 48 matches the characters.)
- [ ] A character ("an old fisherman with a net"), smooth style. Regenerate one, discard
      one.
- [ ] Assets tab: a generated texture shows "Generated: <description> (model)".
- [ ] Known: the model sometimes draws a little ground under the subject. Regenerate if so,
      and note how often it happens.

## Known issues going in

- **Assumed places:** for routines the model assumes a "village well" exists even when
  the scene has no markers. Validation catches it in the next step or the repair round.
  If a proposal still fails on it, that's worth reporting.
- **Hand-placed markers:** the demo scene has no markers or routines of its own. Adding
  them would have meant editing your locally changed `main.aige`.
- **Art scope:** floor tiles can't be generated yet, only objects and characters.
- **Your notes:** the usability items in `docs/notes.md` (menus, context menus,
  shortcuts, panels) are still open, as you asked.

## Notes to bring back

- Dialogue or quest writing that's off-tone or wrong about the world (and whether the
  checker caught it).
- Dungeon parameters that make good or bad levels.
- Generated sprites you'd keep versus discard, and why.
- Anything slow: generation and check times, and the editor's responsiveness while the AI
  works.
