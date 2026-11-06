# Reviewing the AI panel's proposals

How a proposal card works in the AI panel: ticking and editing changes, Apply and Reject, Undo and Apply again, the review policy that applies small tweaks directly, the script check and the World Bible check.

## The proposal card

A small request's answer appears as a proposal card: a one-sentence summary, optional notes from the AI (assumptions, how to test it), and one row per change. Nothing touches the project until you press **Apply**.

Each row shows:
- a checkbox (ticked by default) to include or leave out that change;
- an icon for its kind: ✎ an entity field or script property, ◈ a prefab field, ⚙ a project setting, + a component added, − a component removed, ▸ a command (spawn, write a script, create a prefab, paint terrain...), “ a dialogue, ! a quest, • anything else (a record, a schedule, a cutscene...);
- the target (which entity, prefab or file);
- a **file** badge when it changes a file (a prefab, script, settings, a record...);
- the value before → after;
- ▸ / ▾ to show or hide its details (commands, dialogues and quests start expanded).

A row that can't be applied (the editor validated it again against the live project and found a problem) is greyed with its error and can't be ticked.

### Example: reading the card for a burning sword

1. In the AI panel type:

   > Give the hero a sword that burns enemies for 3 seconds

2. Press **Enter**. The card's summary reads something like "Adds an iron sword with a 3-second burn and equips the hero". Its rows might be:
   - `• Record · item/burning-sword` with a **file** badge (the new item, its JSON shown under ▸);
   - `✎ Hero · Inventory.items  iron-sword → burning-sword`;
   - `+ Hero · + MeleeAttack` if the hero had no attack yet.
3. Click ▸ on the record row to read the item's JSON before deciding, and read the notes (for example "Hit a skeleton to test: it should flash and lose health for 3 seconds").
4. Click **Apply 3**.

The item appears in Project ▸ Records ▸ Items and the hero carries it. The **file** badge warned that the record is a file, which the card's own **Undo** puts back, not the scene's Ctrl+Z.

Questions: how do I review AI changes? what do the icons on a proposal mean? what does the file badge mean?

## Editing a change before applying it

You can change what the AI proposed before applying:

- **Values**: for an entity field, a prefab field or a setting, the "after" value is an editor like the Inspector's (a number, a checkbox, a list of options with its limits). Change it and Apply uses your value.
- **Scripts**: a "Write scripts/....lua" command's details show the whole script in an editable box. Edit the Lua, and Apply writes your version.
- **Leaving changes out**: untick the rows you don't want. The Apply button counts what is ticked ("Apply 3").

### Example: soften a proposed value and keep one change out

1. Ask "Make the wolves in the forest more dangerous". The card proposes `◈ Wolf (prefab) · Stats.attack  2 → 6`, `◈ Wolf (prefab) · Health.max  12 → 20` and `+ Wolf (prefab) · + RangedAttack`.
2. Six attack is too much: click the "after" value 6 on the first row and type `4`.
3. Wolves shouldn't shoot: untick the RangedAttack row. The button now reads **Apply 2**.
4. Click **Apply 2**.

The Wolf prefab gets attack 4 and 20 health, and every wolf copy follows; no RangedAttack is added.

### Example: fix a line in a script before it is written

1. Ask "Write a script that makes the lighthouse lamp blink every 2 seconds".
2. Click ▸ on the row `▸ Write scripts/lighthouse_lamp.lua` to open the script in its editable box.
3. Change the interval in the Lua from `2` to `1.5`.
4. Click **Apply 1**.

The file `scripts/lighthouse_lamp.lua` is written with your 1.5, not the AI's 2, and the script check runs it.

Questions: can I change the value the AI proposed? can I edit the script before it's written? how do I apply only some of the changes?

## Apply, Reject, Undo and Apply again

- **Apply N** applies the ticked changes as one undo step (scene side) and refreshes what they touched: the Inspector, the Scene tree, the Story and World lists, Project ▸ Records and the pickers, the Assets panel and the scene list. It is disabled while the game is playing.
- **Reject** discards the proposal; nothing changes. The card shows "Rejected".
- After applying, the card shows **Applied ✓** (or **Partly applied** with the errors listed when some operations failed) and an **Undo** button. Undo puts back the files the proposal wrote (prefabs, scripts, records, dialogue, quests, schedules, cutscenes, UI files, settings, input.json, World Bible entries; new scenes and prefabs it made are deleted) and undoes its scene changes. Undo is only available while it is the latest change: otherwise "Undo the later changes first (Ctrl+Z)".
- Once undone, the card shows **Undone** with **Apply**, which writes the files again and redoes the scene changes (or applies it afresh if newer edits replaced the redo).
- Ctrl+Z / Ctrl+Y follow it too, but on the scene's side only; the panel's Undo also restores files.
- A `file.move`'s rewritten references aren't put back by Undo.

A task's result can only come back by redo, not be applied afresh, because its operations refer to the working copy's entities.

### Example: try a change, take it back, then keep it after all

1. Ask "Make the background a dark blue night sky" and click **Apply 1**. The card shows **Applied ✓** and **Undo**.
2. Play the scene and decide it's too dark. Stop Play mode (Undo is disabled while playing), then click **Undo** on the card. The setting goes back and the card shows **Undone** with **Apply**.
3. Ask for something else, such as "Add two more skeletons near the courtyard entrance", and apply it.
4. Change your mind about the sky. Its card's **Apply** writes the change again on top of the skeletons.

If instead you try **Undo** on the sky card while the skeletons card is the latest change, the button is disabled with "Undo the later changes first (Ctrl+Z)": undo the skeletons first.

Questions: how do I undo what the AI did? does Ctrl+Z undo AI changes? why is Undo disabled? how do I reapply an undone proposal?

## Review policy: apply small tweaks directly

In the AI panel's **⚙** settings, under **Review** (also File ▸ Preferences… ▸ AI ▸ **Its changes**):

- **Review every change before it's applied** (the default).
- **Apply small tweaks directly**: small tweaks apply right away and can be undone from the chat; everything else waits for your review.

A small tweak is a new value for an existing entity field, project setting or script property. Components, spawning, scripts, prefab edits and deletions are always reviewed, and so is changing which script runs (LuaScript) or which prefab an entity belongs to (PrefabInstance).

Auto-applied rows show ✓ instead of a checkbox, and the card says "N small tweaks applied automatically" with **Undo** (puts the previous values back). If you changed one of those values again since, Undo keeps your newer value and says so ("kept your newer value for ..."). Once undone, **Apply** puts them back (Ctrl+Y). If the rest of the proposal is rejected, the button reads **Reject the rest**.

### Example: let tweaks apply straight away while balancing combat

1. In the AI panel, click **⚙** and, under **Review**, choose **Apply small tweaks directly**.
2. Ask "Give the player 30 max health and make the bandit chief hit harder".
3. The card comes back with `✓ Player · Health.max  10 → 30` and `✓ Bandit Chief · Stats.attack  3 → 5` already applied, and the line "2 small tweaks applied automatically" with **Undo**.
4. Ask "and give the bandit chief a bow". Adding a component is never a small tweak, so this card waits with a checkbox and **Apply 1** as usual.

To go back to checking everything, choose **Review every change before it's applied** in the same ⚙ settings.

Questions: can the AI apply changes without asking? what counts as a small tweak? how do I undo an auto-applied change?

## The script check after applying

Schema-valid isn't the same as working. When an applied proposal wrote a Lua script, the editor plays the scene headless for 2 seconds and reports errors from the scripts it wrote:

- "✓ Script check: ran scripts/x.lua for 2 seconds without errors", or
- "⚠ Script check found errors when the game runs:" with each error, and **Ask AI to fix**, which sends the errors back as a follow-up asking it to rewrite the whole script.

### Example: a script that fails when the game runs

1. Ask "Make the well play a splash sound when the player walks next to it" and click **Apply 2** (the script and its LuaScript component).
2. Under the card the editor reports "⚠ Script check found errors when the game runs:" followed by an error from `scripts/well_splash.lua`, such as an attempt to call a missing function.
3. Click **Ask AI to fix**.

A new card proposes the whole `scripts/well_splash.lua` again, written to avoid the error. Apply it; this time the check says "✓ Script check: ran scripts/well_splash.lua for 2 seconds without errors".

Questions: how do I know if the AI's script works? what does Ask AI to fix do?

## The World Bible check (canon consistency)

A proposal that writes dialogue, quests or World Bible entries (`world.write`) is checked against the World Bible by a second, cheaper model. The card shows "Checking against the World Bible…", then either "✓ Consistent with the World Bible" or "⚠ N possible contradictions with the World Bible", each with its severity (**contradiction**: both can't be true; **possible**: depends on reading), the quoted words, the canon entry it conflicts with and what that entry says, and a one-sentence explanation. Rows involved get a ⚠ mark.

The check flags; it doesn't block: you can still apply. **Ask AI to fix** sends the contradictions back asking it to propose again, consistent with the canon. If the check can't run you see "Couldn't check against the World Bible" with the reason.

The checker only counts real contradictions with evidence copied from the canon: details the canon doesn't mention, or characters' opinions of each other, are not contradictions.

Which model checks is chosen in File ▸ Preferences… ▸ AI ▸ Checks ▸ **Canon check**.

See also: world-bible.md.

### Example: a dialogue that contradicts the canon

The World Bible has a character entry for Aldric saying "Aldric has never left Riverford."

1. Ask "Write a dialogue where Aldric tells the player about his years as a sailor".
2. The card shows the row `“ Dialogue · dialogue/aldric_sailor.json`, then "Checking against the World Bible…", then "⚠ 1 possible contradiction with the World Bible":
   - **contradiction** "my years at sea on the Gull" — Aldric's entry says "Aldric has never left Riverford." Both can't be true.
   The dialogue row gets a ⚠ mark.
3. Either click **Apply 1** anyway (the check doesn't block), or click **Ask AI to fix**.

**Ask AI to fix** brings a new card whose dialogue keeps Aldric at home (he dreams of the sea instead), and this time it reads "✓ Consistent with the World Bible".

Questions: what is the World Bible check? why does it say possible contradiction? can I apply despite a contradiction?

## Validation and repair behind the scenes

Every answer is checked twice before you see it. The AI service checks it against the engine schema and the project (components, fields, types and ranges, entity ids, prefabs, textures, audio, records, scripts, dialogues, schedules, cutscenes and World Bible keys that must exist or be written in the same answer). If it fails, the model gets one repair round with the exact errors. If it still fails you see "The AI couldn't produce a valid change." with the reasons.

For a task step, operations that still fail are left out and reported back as "skipped", so the next step redoes them, instead of losing the whole step. The editor then validates again against the live scene and shows any row that can't apply with its error.

### Example: asking for a picture the project doesn't have

1. Ask "Use knight_gold.png as the hero's sprite" in a project with no file of that name.
2. The service checks the answer against the project: the texture must exist. The model gets one repair round with that error; if it can't make it valid (there is nothing to point at), you see "The AI couldn't produce a valid change." with the reason listed under it.
3. Import `knight_gold.png` into the project's assets (or generate the sprite), then ask again.

This time the card has a valid row for the hero's sprite and **Apply 1** is enabled. A row that was valid for the service but not for the scene as the editor has it (an entity gone since) would instead show greyed with its error when the card arrives.

Questions: what does "The AI couldn't produce a valid change" mean? why is one change greyed out?

See also: ai-panel.md, ai-panel-tasks.md, ai-requests-tips.md.
