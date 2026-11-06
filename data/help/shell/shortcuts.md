# Keyboard shortcuts

Every keyboard shortcut in the AIGE editor, grouped as in Help ▸ Keyboard Shortcuts, which keys are fixed, and how to
change (rebind) them.

## The Keyboard shortcuts window

Open **Help ▸ Keyboard Shortcuts…** or press **F1** (F1 works even while playing or typing). Preferences ▸ Keyboard ▸
**Keyboard shortcuts…** opens it too. Press F1 again, Esc or × to close it.

The window lists every action in groups (File, Edit, Tools, View, Panels, Play, Help). Each row shows the action's
keys and what it does. Changed actions have a **Reset** button, and rows with a plain key label (no buttons) are fixed
keys that can't be changed. Your shortcuts are yours, kept on this computer (in your user preferences, not in the
project). They work the same in the viewport and in the panels.

See also: menus.md.

### Example: looking up the key for the Generate panel

1. Press F1. The Keyboard shortcuts window opens over the editor.
2. Find the Panels group: the row for the Generate panel shows K.
3. Press F1 again (or Esc) to close the window, then press K: the Generate panel shows or hides.
4. Your shortcuts are kept in your own preferences, so they are the same in every project you open on this computer.

Questions: where is the list of shortcuts? what does F1 do? are shortcuts saved in the project?

## Changing a shortcut (rebinding)

In Help ▸ Keyboard Shortcuts:

1. **Click a key** next to an action to replace it, or click **+** to add another key to that action.
2. "Press the new keys…" appears. Press the key combination you want, for example Ctrl+Shift+K. Pressing a modifier
   (Ctrl, Shift, Alt) on its own does nothing; the editor waits for the key. **Esc cancels**.
3. If another action had that key, it moves over to this action, and a line says which action lost it.

Click **×** beside a key to remove it. **Reset** on a row puts that action back to its default keys. **Reset all**
puts every shortcut back.

Symbol keys are recorded by the character they type, so `[` works on keyboard layouts where it's AltGr+8. Letters are
recorded in upper case ("Ctrl+S"). Menu items and tooltips show your new keys at once. Most shortcuts don't fire while
you're typing in a text box. Ctrl+S, Ctrl+J, F11 and F1 still do. During Play, only F5, F6, F9, F3 and F1 act; the
other keys belong to the game.

### Example: duplicating with Ctrl+Shift+D as well as Ctrl+D

You come from another editor where Ctrl+Shift+D duplicates.

1. Press F1 and find the Edit group's Duplicate row (Ctrl+D).
2. Click + on that row. "Press the new keys…" appears.
3. Hold Ctrl and Shift and press D. The row now shows Ctrl+D and Ctrl+Shift+D, and Edit ▸ Duplicate in the menu
   shows the new keys at once.
4. If Ctrl+Shift+D had belonged to another action, a line names the action that lost it. Changed your mind? Click
   Reset on the row to go back to Ctrl+D alone.

### Example: moving Erase off E

1. In Help ▸ Keyboard Shortcuts, click the E next to the Erase tool.
2. Press X. Erase is now X.
3. To undo every change you've made to the shortcuts, click Reset all.

Questions: how do I rebind a key? how do I reset shortcuts? why doesn't my shortcut work while typing? can I add two
keys to one action?

## File and Edit shortcuts

The defaults, all of which you can rebind:

**File**
- Ctrl+S: save the scene (also while typing in a text box)
- Ctrl+Shift+S: save project as…
- Ctrl+O: open project…
- Ctrl+N: new project…

**Edit**
- Ctrl+Z: undo
- Ctrl+Y or Ctrl+Shift+Z: redo
- Ctrl+C: copy the selection
- Ctrl+V: paste at the cursor
- Ctrl+D: duplicate the selection
- Delete or Backspace: delete the selection
- Escape: deselect, back to the Select tool
- ] or +: bigger (the brush while one is armed, else the selection)
- [ or -: smaller
- }: a little bigger; {: a little smaller
- Ctrl+]: bring forward (a flat thing over the ground pieces under it)
- Ctrl+[: send backward
- Ctrl+Shift+]: bring to front (ground pieces); Ctrl+Shift+[: send to back
- R: turn (the brush while one is armed, else the selection; flat: a quarter, upright: mirror)
- Shift+R: turn the other way

Fixed (not rebindable): **Arrows** nudge the selection a cell (Shift: a tenth).

### Example: a row of fence posts in a few keystrokes

1. Click the fence post by the pig pen and press Ctrl+D. A copy appears beside it, selected.
2. Press the Right arrow to line it up a cell along, then Ctrl+D again for the next post. Repeat until the row
   reaches the gate.
3. The last post is a bit too tall: press { twice to make it 4% smaller.
4. Too far? Press Ctrl+Z to step back, and Ctrl+Y (or Ctrl+Shift+Z) to step forward again.

Questions: what is the shortcut for redo? how do I duplicate? what key rotates? how do I make something a little
bigger?

## Tools, View, Panels, Play and Help shortcuts

**Tools**: V Select tool, E Erase tool. Fixed: 1–9 pick prefab 1–9 as the brush; Alt+wheel resize; Shift+wheel turn;
right-click opens the menu for what's under the cursor; right-drag pans and the wheel zooms.

**View**: F frame the scene; G tile borders on empty ground; H the selected building's interior, each floor, then its
exterior; Shift+H every building's interior or exterior; PageUp / PageDown a floor up or down; F3 frame stats (also in
Play).

**Panels**: B Scene panel; Ctrl+J show/hide the bottom panels (they keep their tabs and size); J Assets; P Project
(scenes, World Bible, story); K Generate (level, art); I AI panel; M the viewport; U Build panel; Y Planner panel; F11
maximise the panel in use to the whole window, or bring the layout back.

**Play**: F5 play / stop; F6 save the game (while playing); F9 load the saved game (or continue from it). Fixed: Esc
stops playing. The game's own keys (moving, attacking, talking) come from Project Settings ▸ Input.

**Help**: F1 show / hide the shortcuts.

The single-letter panel keys only work when you're not typing in a text box.

See also: panels-and-layout.md, play-mode.md, input-settings.md.

### Example: more room for the viewport while painting, then the AI

1. Press Ctrl+J: the bottom panels hide, keeping their tabs and size, and the viewport gets their space.
2. Press 2 to arm the second prefab (cobblestones) and paint the square.
3. Press I to show the AI panel and type:

> "Put six market stalls around the square, facing the middle."

4. Press I again to hide it, and Ctrl+J to bring the bottom panels back. If a letter key types into a text box
   instead, click the viewport first: single-letter panel keys don't work while you're typing.

Questions: how do I toggle the AI panel? what does Ctrl+J do? how do I maximise a panel? what key opens the Build
panel?
