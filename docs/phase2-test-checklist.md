# Phase 2 — Manual Test Checklist

The user-test checkpoints from every Phase 2 milestone (`development_plan.md`), collected
for one end-of-phase pass. Automated coverage already checks the logic: 131 GoogleTest
cases and 28 pytest cases, including golden replays of real model output, a simulated
fight with the AI-made burning sword, and a byte-for-byte check that both modes save the
same files. This list is for what tests can't judge: whether the AI's changes actually
play right, and whether the workflow feels good.

Build: `cmake --build build --config Debug`, then run `build/apps/editor/Debug/editor.exe`
(opens `projects/demo`). The AI needs `ai_service/.env` with `MISTRAL_API_KEY` and `uv`
on the PATH. The editor starts the service itself, and the AI panel header shows its
status.

Tip: commit or copy `projects/demo` first, so you can throw away what the AI changes
(`git checkout projects/demo`).

## 2.5 — Describe → review → apply → play (most important)

The Phase 2 exit criterion. Each row: type the instruction in the AI panel, review the
diff card that appears in the chat, click **Apply**, then **Play** and judge the result.

- [ ] *"Give the hero a sword that burns enemies for 3 seconds"*: hit a skeleton and
      it keeps taking damage for a few seconds (burn ticks, red flashes).
- [ ] *"Make skeletons tougher but slower"*: they take more hits and move slower, and
      they still start the fight at **full** health.
- [ ] *"Add two more skeletons near the courtyard entrance"*: they appear where you'd
      expect, and chase and fight like the others.
- [ ] *"Make the villager follow the hero"*: the villager trails you in play.
- [ ] *"Make the background a dark blue night sky"*: the viewport clear colour changes,
      and it's still that colour after a restart (it's saved to `settings.aige`).
- [ ] *"Make skeletons flee when their health drops below 3"*. **Known issue:** the
      model's script switches Follow off, then fights it, so fleeing looks jittery or
      doesn't happen. Note what you see; this is the gap between "valid" and "plays
      right" that this playtest exists to find.
- [ ] Something of your own the examples don't cover. Was the diff what you meant?
      Would you have accepted it without reading the notes?
- [ ] One **Ctrl+Z** undoes a whole applied proposal. Ctrl+Y redoes it.

## 2.4 — AI chat and Diff Review

- [ ] Diff cards show a readable target (*Skeleton #12 · Health.max*) with before → after.
      Script writes show the source.
- [ ] Untick an item, or edit a value inline, before accepting. Only what's ticked is
      applied, with your edited value.
- [ ] **Reject** leaves everything untouched. Follow-up messages keep the conversation
      ("make it 5 seconds instead").
- [ ] An impossible request ("make the hero fly to the moon") gets an explanation or a
      sensible partial proposal, not a broken one.
- [ ] While a request is running, the panel shows it's busy and you can keep editing.
- [ ] Stop the service (or remove the key) and the panel says AI is unavailable. The
      rest of the editor works normally.

## 2.5 — Script check and Ask AI

- [ ] After applying a proposal that writes a script, a script check reports "no errors"
      or lists runtime errors. Use **Ask AI to fix** on an error and the follow-up repairs it.
- [ ] The ✦ next to an Inspector field or setting opens the AI panel focused on that field
      (an "About Skeleton.Health.max" chip above the input). The next request is about it;
      **×** on the chip clears it.

## 2.6 — Modes

- [ ] The toolbar toggle **Assisted / AI-Driven** switches instantly. AI-Driven shows a
      wide AI panel; Assisted shows a thin ✦ AI rail on the right.
- [ ] In Assisted, clicking the rail or any ✦ opens the panel, and **›** collapses it.
      The conversation and any pending proposal are still there.
- [ ] Switch modes mid-session, with unsaved edits and a pending proposal: nothing is lost,
      and no prompt or conversion happens. Save, then restart: the mode is remembered, and
      the scene is the same in either mode.
- [ ] Do the same edit by hand in the Inspector (e.g. Skeleton prefab copy → Health.max)
      that the AI did. The result is indistinguishable (same values, same prefab override
      marker).

## 2.1–2.2 — Generated Inspector and Project Settings

- [ ] Every component section is generated: numbers have sensible ranges and refuse
      out-of-range input with an inline error. Enums are dropdowns, and asset fields pick
      assets.
- [ ] Hover a field to see its description.
- [ ] **Add Component** adds a component, along with anything it needs. Removing one
      works and undoes.
- [ ] On a prefab copy, an edited field shows as an override and can be reverted. Editing
      the prefab itself updates every non-overridden copy.
- [ ] With nothing selected, the Inspector shows **Project Settings**. Change camera
      follow stiffness, minimum damage, knockback scale, the clear colour, or player
      collision, and Play reflects each change. All of them survive a restart.

## Notes to bring back

- Which instructions produced a change that was valid but wrong in play.
- Anything in Diff Review you couldn't understand without reading the raw values.
- Typical response time, and whether that's acceptable for the loop.
