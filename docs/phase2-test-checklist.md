# Phase 2 — Manual Test Checklist

The user-test checkpoints from every Phase 2 milestone (`development_plan.md`), collected
for one end-of-phase pass. Automated coverage already checks the logic: 144 GoogleTest
cases and 39 pytest cases, including golden replays of real model output, a simulated
fight with the AI-made burning sword, and a byte-for-byte check that hand and AI edits save the
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

## 2.6 — AI panel, AI off, hand vs. AI edits

- [ ] **›** in the AI panel header collapses it to a thin ✦ AI rail. Clicking the rail or
      any ✦ Ask AI opens it again, and the conversation and any pending proposal are still
      there. Restart: the panel comes back open or collapsed, as you left it.
- [ ] **Turn off** in the AI panel header: the rail says "AI off", the ✦ Ask AI buttons
      disappear, the panel offers **Turn on AI**, and the `uv`/python process is gone
      (Task Manager). Restart: AI stays off and the service is never started. Everything
      else in the editor works. **Turn on AI** starts it again.
- [ ] Do the same edit by hand in the Inspector (e.g. Skeleton prefab copy → Health.max)
      that the AI did. The result is indistinguishable (same values, same prefab override
      marker).

## 2.7 — AI is opt-in

- [ ] To see the first run: delete `%LOCALAPPDATA%\aige\editor.json` and start the editor.
      The AI panel asks "Use AI in this editor?" and lists what a request sends. No
      `uv`/python process is running (Task Manager) until you click **Turn on AI**.
- [ ] **Not now**: restart, and the service still never starts. ⚙ → **Turn AI off** works the
      same way later.

## 2.8 — Review policy

- [ ] ⚙ → **Apply small tweaks directly**. Ask "make skeletons a bit faster and the
      knockback stronger". Value changes apply immediately (✓ in the card, "N small tweaks
      applied automatically"); anything else in the proposal still waits for **Apply**.
- [ ] **Undo** on that card restores the values. Change one of them by hand first, then
      Undo: your value is kept and named in the card.
- [ ] Scripts, components, spawning and prefab edits are never applied without review.
      Switch back to **Review every change**, and nothing applies until you click Apply.

## 2.9 — Bigger tasks

- [ ] "Add a necromancer enemy that keeps its distance, summons a skeleton every 5 seconds
      and is weak in melee; make it a prefab and put two in the courtyard." A task card
      shows the plan and each step as it happens (✓ or "N to fix"). The project doesn't
      change while it works.
- [ ] When it finishes, the result card ("Result of N steps") lists every change. Apply it,
      play, and judge: does the necromancer behave as asked? One Ctrl+Z undoes the scene
      part.
- [ ] Start another task and press **Stop** mid-way: "Stopped. Nothing was changed."
- [ ] "Turn this into a complete open-world RPG with inventory, crafting, quests, a world
      map and save games." Instead of changes, the AI suggests smaller requests. Clicking
      one puts it in the message box.
- [ ] Ordinary requests ("make skeletons tougher") still come back as one proposal, with
      no task card.

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
