# Reviewing the AI panel's proposals

How a proposal card works in the AI panel: ticking and editing changes, Apply and Reject, Undo and Apply again, the review policy that applies small tweaks directly, the script check and the World Bible check.

## The proposal card

A small request's answer appears as a proposal card: a one-sentence summary, optional notes from the AI (assumptions, how to test it), and one row per change. Nothing touches the project until you press **Apply**.

Each row shows:
- a checkbox (ticked by default) to include or leave out that change;
- an icon for its kind: ✎ an entity field or script property, ◈ a prefab field, ⚙ a project setting, + a component added, − a component removed, ▸ a command (spawn, write a script, create a prefab, paint terrain...), “ a dialogue, ! a quest;
- the target (which entity, prefab or file);
- a **file** badge when it changes a file (a prefab, script, settings, a record...);
- the value before → after;
- ▸ / ▾ to show or hide its details (commands, dialogues and quests start expanded).

A row that can't be applied (the editor validated it again against the live project and found a problem) is greyed with its error and can't be ticked.

Questions: how do I review AI changes? what do the icons on a proposal mean? what does the file badge mean?

## Editing a change before applying it

You can change what the AI proposed before applying:

- **Values**: for an entity field, a prefab field or a setting, the "after" value is an editor like the Inspector's (a number, a checkbox, a list of options with its limits). Change it and Apply uses your value.
- **Scripts**: a "Write scripts/....lua" command's details show the whole script in an editable box. Edit the Lua, and Apply writes your version.
- **Leaving changes out**: untick the rows you don't want. The Apply button counts what is ticked ("Apply 3").

Questions: can I change the value the AI proposed? can I edit the script before it's written? how do I apply only some of the changes?

## Apply, Reject, Undo and Apply again

- **Apply N** applies the ticked changes as one undo step (scene side) and refreshes what they touched: the Inspector, the Scene tree, the Story and World lists, Project ▸ Records and the pickers, the Assets panel and the scene list. It is disabled while the game is playing.
- **Reject** discards the proposal; nothing changes. The card shows "Rejected".
- After applying, the card shows **Applied ✓** (or **Partly applied** with the errors listed when some operations failed) and an **Undo** button. Undo puts back the files the proposal wrote (prefabs, scripts, records, dialogue, quests, schedules, cutscenes, UI files, settings, input.json, World Bible entries; new scenes and prefabs it made are deleted) and undoes its scene changes. Undo is only available while it is the latest change: otherwise "Undo the later changes first (Ctrl+Z)".
- Once undone, the card shows **Undone** with **Apply**, which writes the files again and redoes the scene changes (or applies it afresh if newer edits replaced the redo).
- Ctrl+Z / Ctrl+Y follow it too, but on the scene's side only; the panel's Undo also restores files.
- A `file.move`'s rewritten references aren't put back by Undo.

A task's result can only come back by redo, not be applied afresh, because its operations refer to the working copy's entities.

Questions: how do I undo what the AI did? does Ctrl+Z undo AI changes? why is Undo disabled? how do I reapply an undone proposal?

## Review policy: apply small tweaks directly

In the AI panel's **⚙** settings, under **Review** (also File ▸ Preferences… ▸ AI ▸ **Its changes**):

- **Review every change before it's applied** (the default).
- **Apply small tweaks directly**: small tweaks apply right away and can be undone from the chat; everything else waits for your review.

A small tweak is a new value for an existing entity field, project setting or script property. Components, spawning, scripts, prefab edits and deletions are always reviewed, and so is changing which script runs (LuaScript) or which prefab an entity belongs to (PrefabInstance).

Auto-applied rows show ✓ instead of a checkbox, and the card says "N small tweaks applied automatically" with **Undo** (puts the previous values back). If you changed one of those values again since, Undo keeps your newer value and says so ("kept your newer value for ..."). Once undone, **Apply** puts them back (Ctrl+Y). If the rest of the proposal is rejected, the button reads **Reject the rest**.

Questions: can the AI apply changes without asking? what counts as a small tweak? how do I undo an auto-applied change?

## The script check after applying

Schema-valid isn't the same as working. When an applied proposal wrote a Lua script, the editor plays the scene headless for 2 seconds and reports errors from the scripts it wrote:

- "✓ Script check: ran scripts/x.lua for 2 seconds without errors", or
- "⚠ Script check found errors when the game runs:" with each error, and **Ask AI to fix**, which sends the errors back as a follow-up asking it to rewrite the whole script.

Questions: how do I know if the AI's script works? what does Ask AI to fix do?

## The World Bible check (canon consistency)

A proposal that writes dialogue, quests or World Bible entries (`world.write`) is checked against the World Bible by a second, cheaper model. The card shows "Checking against the World Bible…", then either "✓ Consistent with the World Bible" or "⚠ N possible contradictions with the World Bible", each with its severity (**contradiction**: both can't be true; **possible**: depends on reading), the quoted words, the canon entry it conflicts with and what that entry says, and a one-sentence explanation. Rows involved get a ⚠ mark.

The check flags; it doesn't block: you can still apply. **Ask AI to fix** sends the contradictions back asking it to propose again, consistent with the canon. If the check can't run you see "Couldn't check against the World Bible" with the reason.

The checker only counts real contradictions with evidence copied from the canon: details the canon doesn't mention, or characters' opinions of each other, are not contradictions.

Which model checks is chosen in File ▸ Preferences… ▸ AI ▸ Checks ▸ **Canon check**.

See also: world-bible.md.

Questions: what is the World Bible check? why does it say possible contradiction? can I apply despite a contradiction?

## Validation and repair behind the scenes

Every answer is checked twice before you see it. The AI service checks it against the engine schema and the project (components, fields, types and ranges, entity ids, prefabs, textures, audio, records, scripts, dialogues, schedules, cutscenes and World Bible keys that must exist or be written in the same answer). If it fails, the model gets one repair round with the exact errors. If it still fails you see "The AI couldn't produce a valid change." with the reasons.

For a task step, operations that still fail are left out and reported back as "skipped", so the next step redoes them, instead of losing the whole step. The editor then validates again against the live scene and shows any row that can't apply with its error.

Questions: what does "The AI couldn't produce a valid change" mean? why is one change greyed out?

See also: ai-panel.md, ai-panel-tasks.md, ai-requests-tips.md.
