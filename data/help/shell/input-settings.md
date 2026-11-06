# Input settings (the game's controls)

Project Settings ▸ Input: the player's controls for your game, kept in the project's input.json. Covers the built-in
actions and their default keys, binding keys, mouse and gamepad buttons, axes, adding your own actions, what an action
can do when pressed, groups, the search box, and resetting.

## Opening the Input page

Open **File ▸ Project Settings…** and click **Input** at the bottom of the list. The page shows a table of actions with
the columns **Action**, **When**, **Bindings** and **Plays / event**, with **Reset to defaults** at the bottom. Every
change is saved to the project's `input.json` straight away. "Saved" confirms it, and a problem shows in red. If
`input.json` is missing or broken, the defaults are used, and built-in actions missing from the file are added back.

These are the game's keys, used in Play and in exported games. The editor's own shortcuts are separate: see
shortcuts.md.

Questions: how do I change the player's controls? where are the game's keys saved?

## The built-in actions and their default keys

The engine's own actions can be rebound but never removed:

- **move_x** / **move_y** (axes): A/D or Left/Right, W/S or Up/Down, the pad's d-pad and left stick.
- **attack**: left mouse button, Q, the pad's X. **interact** (talk, open, pick up, use): E, Space, the pad's A.
- **fire**: F, RB. **block**: Left Ctrl, LB. **dodge**: Left Shift, the pad's B.
- **inventory**: I, Tab, Y. **crafting**: C. **put_back** (put away the item in your hand): right mouse button, Z, B.
- **ability_1–4**: keys 1–4. **spellbook**: K. **journal**: J, Back.
- **skip** (a cutscene): Esc, Enter, Space, A.
- In dialogue: **confirm** (Enter, Space, E, A), **nav_up** and **nav_down**, and **choice_1–9** (keys 1–9).
- In menus: **ui_up**, **ui_down**, **ui_select**, **ui_back** (Backspace, B), **ui_context** (Q, X).
- **menu** (pause): Esc, Start. **quicksave** F6, **quickload** F9.
- Debug: **debug_stats** F3, **debug_ai** F4, **debug_physics** F5.

**When** is the context an action works in: gameplay, dialogue, menu, cutscene, or any.

Questions: what is the default interact key? which key attacks? how do I change the pause key? what keys choose
dialogue answers?

## Binding keys, mouse and gamepad buttons

In the **Bindings** column, each binding shows as a chip. Click **×** on a chip to remove that binding.

- **+ Key**: click it, then press a key. "Press a key… (Esc to cancel)" shows while it waits. Letters, digits (Num0–9),
  F1–F12, arrows, Space, Enter, Escape, Tab, Backspace, and the left and right Shift, Ctrl and Alt can be bound.
- **+ Button…**: a list of mouse buttons and gamepad buttons to add.
- **Axes** (move_x, move_y, marked "(axis)"): use **+ Key (−)** and **+ Key (+)** to add a key that pushes the axis one
  way or the other. The chip shows − or + before the key. Stick bindings show as their own chips.

A key can be bound to several actions. If they share a **group**, only the one whose target is nearest acts (see
below).

Questions: how do I add a gamepad button? how do I bind a key to move left? how do I remove a key binding?

## Adding your own actions

Type a name in the box at the top ("a new action: farm, parry, kick…") and click **Add action** or press Enter. The
name is made lower case, with words joined by `_`. A name that's already taken is ignored. A new action starts in the
gameplay context with no keys.

For your own actions you can also:

- write a description ("what it does") under its name;
- change its **When** context (gameplay, dialogue, menu, any);
- remove it with **Remove**.

Things in the scene react to an action with a **Reaction** component (in the Inspector), or with a part whose input
action is set to it. A part's input action picker can also add a new action, with its key, right where you pick it.

Questions: how do I add a custom action like farm? how do I delete an action? how do objects respond to my action?

## What an action does when pressed

For button actions, the **Plays / event** column has:

- **Clip**: a clip the player plays when the action is pressed (for example parry or attack_kick), picked from the
  characters' clips. "(missing)" marks a clip that no longer exists.
- **Event**: an event sent when it's pressed, for scripts, triggers and quests to react to. **New event…** names a new
  one.
- **Effect**: an effect played on the player (from Project ▸ Effects).
- **Screen**: a panel or one of the project's screens (ui/screens) opened when it's pressed.
- **Only near something it activates**: the clip, event and effect only happen when something the action activates
  is in range (a part with it as its input action, or a thing's Reaction). For example, cook only at a stove.
- **Group**: actions that share a key in one group (for example interact and cook) — pressed together, only the one
  whose target is nearest acts.

Questions: how do I make a key play an animation? how do I send an event to a script from a key? how do I open a
screen with a key? how do two actions share the same key?

## Searching and resetting

The **search box** ("Search actions, keys, groups, clips, events…") filters the table. It shows the actions that
contain every word you type, in their name, description, keys, group, clip or event, and "N of M" tells how many
match. For example, type `space` to find every action bound to Space.

**Reset to defaults** puts back the engine's default input map.

Questions: how do I find which action uses a key? how do I reset controls to default?
