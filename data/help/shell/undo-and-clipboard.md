# Undo, clipboard and dialogs

How undo and redo work in the AIGE editor (what's undoable, how many steps, when the history is cleared), copying and
pasting entities, duplicating, and how the editor's own dialogs (confirm, prompt, info) behave.

## Undo and redo

- **Undo**: Ctrl+Z, Edit ▸ Undo, or the toolbar's **Undo**.
- **Redo**: Ctrl+Y or Ctrl+Shift+Z, Edit ▸ Redo, or the toolbar's **Redo**.

Every change to the scene is one undo step: moving a thing (a whole drag is one step), painting or erasing (one
stroke is one step), placing, deleting, duplicating, pasting, nudging, turning, resizing (a quick run of resizes on
one thing counts as one), changing the drawing order, Inspector edits, replacing prefabs, paths and markers, ground
cells, and changes the AI applies. Big operations, like building a place or filling a layer, record only what they
changed, so undoing them is fast.

The history keeps the last **100 steps**. Undo and Redo are greyed out when there's nothing to take back or bring
back. A new edit after undoing replaces the redo steps.

### Example: taking back an AI change you don't like

1. In the AI panel, type:

> "Scatter a dozen pine trees along the north edge of the meadow."

2. You accept the proposal and the pines appear, but they hide the shrine.
3. Press Ctrl+Z (or click Undo on the toolbar) until the pines are gone: changes the AI applies are undo steps
   like your own.
4. Changed your mind again? Press Ctrl+Y: the pines are back. If you place a rock now instead, Redo greys out,
   because the new edit replaces the redo steps.

### Example: one paint stroke, one undo

1. With the grass tile brush, drag a long stroke across the field, then a second, shorter one.
2. Press Ctrl+Z once: only the second stroke goes. Press it again: the first goes too.

Questions: how do I undo? how many undo steps are there? can I undo an AI change? why is redo greyed out?

## What isn't undoable, and when history is cleared

- **Project Settings** aren't in the scene's undo history. Change them back by hand. They're saved with File ▸ Save.
- **Opening another scene clears the history**. AI proposals applied to the scene you left can no longer be undone
  from the AI panel.
- **Restoring recovered changes** after a crash starts with an empty history.
- **Undo and redo don't work during Play**. Play itself is undone when you stop, since the scene comes back as it was.
- File operations such as deleting a scene file or a prefab, or input.json changes, aren't scene edits and aren't
  undone by Ctrl+Z.

If you undo every edit until the scene matches its saved file, the editor won't ask to save when you close.

### Example: why Ctrl+Z doesn't bring back the old zoom

1. In File ▸ Project Settings… ▸ Camera you raised playZoom from 2 to 3, then edited a few fences.
2. Pressing Ctrl+Z undoes the fences, not the zoom: Project Settings aren't in the scene's undo history.
3. Set playZoom back to 2 by hand in Project Settings, then save.

### Example: undo before you leave a scene

1. In `harbour_market` you deleted the old pier, then opened `smugglers_cellar` from File ▸ Scenes….
2. Back in `harbour_market`, Edit ▸ Undo is greyed out: opening another scene cleared the history (and the pier is gone
   for good if you saved when asked).
3. Next time, undo (or don't save) before switching scenes.

Questions: why can't I undo after switching scenes? can I undo a settings change? does undo work in play mode?

## Copy, paste and duplicate

- **Copy** (Ctrl+C, Edit ▸ Copy, or the thing's right-click menu ▸ Copy): copies the selected thing to the editor's
  own clipboard. "Copied — Ctrl+V pastes at the cursor" confirms it. Tiles aren't copied from their menu.
- **Paste** (Ctrl+V, Edit ▸ Paste at cursor): pastes at the grid cell under the mouse. With the mouse outside the
  viewport, it pastes one cell beside the selection. The ground's right-click menu ▸ **Paste here** pastes at the cell
  you right-clicked. The pasted thing is selected.
- **Duplicate** (Ctrl+D, Edit ▸ Duplicate, right-click ▸ Duplicate): a copy of the selection, selected at once.
  Tiles and the player can't be duplicated ("Tiles and the player can't be duplicated").

The clipboard is the editor's own. It isn't the Windows clipboard, and it's empty when the editor starts. Pasting
doesn't work during Play.

### Example: copying a campfire into the other camp

1. Click the campfire in the bandit camp and press Ctrl+C. The status bar says "Copied — Ctrl+V pastes at the
   cursor".
2. Pan to the hunters' camp, right-click the ground where the fire should go and click Paste here.
3. The new campfire sits at that cell, selected. Press Ctrl+D twice for two more beside it, for a bigger camp.
4. Trying Ctrl+D on the player instead says "Tiles and the player can't be duplicated".

Questions: how do I copy an entity? where does paste put things? how do I paste at a specific cell? why can't I
duplicate the player?

## The editor's dialogs

Questions the editor asks (confirmations, names, results) open as dialogs inside the editor window, drawn above
everything, the viewport included. If several are waiting, they show one after another.

- **Confirm**: a message with a cancel button and a confirm button. Destructive actions have a red confirm button
  named after the action (for example **Delete**). Some have a check box option.
- **Prompt**: asks for text, such as a scene or marker name. The field is focused and selected. Some prompts also
  have a choice, like New Scene's Assembled / Paint.
- **Info**: a message with **Close** (for example "Export failed").

**Enter** confirms and **Esc** cancels. Clicking outside the dialog also cancels. While a dialog is open, the
editor's shortcuts wait. The questions about unsaved changes when switching scenes or projects, closing or exiting are
Windows message boxes with Yes, No and Cancel.

### Example: naming a marker, then changing your mind

1. Right-click the ground by the well and click Add a marker here…. A prompt opens with its text field focused.
2. Type `well_bucket` and press Enter: the marker is added.
3. Right-click another spot and click Add a marker here… again, then press Esc (or click outside the dialog): it's
   cancelled and nothing is added.
4. Pressing V or Ctrl+Z does nothing while a dialog is open: the editor's shortcuts wait until it closes.

### Example: deleting a scene with the red button

1. In the Project panel's Scenes tab, right-click `test_room` and click Delete….
2. A confirm dialog opens with a red Delete button, named after the action.
3. Click Delete (or press Enter) to remove the scene file; this can't be undone with Ctrl+Z.

Questions: how do I cancel a dialog? why don't my shortcuts work? (a dialog is open) what does the red button mean?
