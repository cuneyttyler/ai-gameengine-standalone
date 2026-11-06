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

Questions: how do I toggle the AI panel? what does Ctrl+J do? how do I maximise a panel? what key opens the Build
panel?
