# Game UI

The UI of the games you make (what the player sees): screens, windows, the HUD, themes and notifications in Project ▸
UI, the UI designer (Design…), speech bubbles, labels and floating numbers, and UI from scripts.

## Game UI overview

Every screen the player sees is a document the project can use as is, restyle or replace: menus (title, pause, save and
load, game over, victory, credits), windows (inventory, shop, journal), the dialogue box and the HUD. The engine draws
them with RmlUi (HTML/CSS-like documents) from `data/ui`; a project's own copies live in its `ui/` folder and win.

- Documents show the game's data through models: `menu` (title, subtitle, body, footer, items, highlighted,
  `choose(i)`), `dialogue` (speaker, text, portrait, choices, `choose(i)`), `hud` (health, mana, stamina and maxima,
  level, xp, gold, quest, objective, clock, prompt, subtitle, marker, and the bubbles, labels and numbers lists),
  `notes` (toast, feed, banner, alert) and `ui` (`action('…')`, the `ui_action` event for scripts).
- The game's logic still decides what a menu shows; a click calls the same choice as keys, so every screen works by
  keyboard, gamepad or mouse.
- Project Settings ▸ UI ▸ documents off keeps the plain built-in overlays.

Questions: how do I change the game's menus? where are the game's screens? can I customise the HUD?

## The UI tab: screens, theme, notifications

**Project ▸ UI** ("The game's screens, theme and notifications"):

- **Screens:** each screen with its source (the engine's or the project's). **Customise** copies it into the project's
  `ui/` folder to change it; **Design…** opens it in the UI designer; a preview shows it over the viewport with sample
  data. New screens can be made here (a new screen id starts in the designer).
- **Theme:** a **Base theme** (stone, parchment, iron, arcane, scifi-glass, minimal, pixel) and token overrides (colours
  used as `var(--token)`); **Save theme** writes `ui/theme.json`.
- **Notifications:** rules mapping events to notifications (`ui/notifications.json`); **Save rules**.
- **Check all** shows every screen once and lists what doesn't load ("Every screen loads cleanly").

Questions: how do I change the game's colours? how do I customise a built-in screen? how do I check my screens?

## The UI designer

**Project ▸ UI ▸ Design…** opens a screen in the designer:

- **Canvas** in 16:9, 16:10, 21:9, 4:3 or Steam Deck shape with a safe area; drag to move, drag the corner to resize,
  arrow keys to nudge; **Undo** / **Redo** (Ctrl+Y).
- **Add**: panel, image, button, text, bar, list, slot grid, checkbox or slider, notifications, Custom RML.
- **Layers**: order, into or out of a panel, duplicate (Ctrl+D), delete (Del), hide, lock.
- **Properties**: anchor (a corner, an edge, the middle, or stretched across or down: it keeps its place at any size),
  position and size, theme colours, pictures imported from your computer (**Import…**, copied to `assets/ui/`) whole or
  in nine pieces, opacity, padding, text with the model's data inserted, a button's hover, pressed and disabled looks,
  and a font (**Import font…** copies a .ttf or .otf to `assets/fonts/`).
- **On click**: open or close a screen, send an event, press an input action, set or clear a flag, run effects, quick
  save, quick load, quit. **Only show if**: conditions, asked each frame.
- **Data**: HUD data, Menu / window data, or Dialogue data. Bars, lists (a click picks the line) and the slot grid (what
  the player carries; a click uses, wears or takes it in hand).
- **Code** shows the RML written from the design; **Preview** saves and shows it in the game's renderer; **Checks** list
  problems (a button that does nothing, an id used twice, a missing picture, text that may not fit).

Saved as `ui/designs/<id>.json`, with `ui/screens/<id>.rml` written from it. A hand-written screen opens as one Custom
RML element, kept whole. Not yet: code-view edits taken back into the design; a checkbox's or slider's value passed on
(it sends its event).

Questions: how do I design a HUD? how do I add a button that opens the inventory? how do I import a picture for the UI?
how do I use my own font?

## Speech bubbles, labels and floating numbers

Text over people and things, drawn by the HUD document and styled in the designer:

- **Speech bubbles:** what characters say (the Speech component's lines, the `say` effect, a Character Rule's `says`,
  Lua `say`). Project Settings ▸ UI ▸ **speech**: bubble, subtitle or both; **speechSeconds**. One bubble per speaker.
- **Labels:** the **Label** component (`text`, `within`: shown only while the player is that close, 0 always,
  `height`); Settings ▸ UI ▸ labels turns them off.
- **Floating numbers:** damage and heals over who took them (**damageNumbers**), items gained over the player ("+3
  Wheat", **itemNumbers**). They rise and fade.

In the designer, the element types bubbles, labels and numbers are templates repeated for each entry (text `{{it.text}}`,
a bubble's speaker `{{it.name}}`, numbers' colours by kind).

Questions: how do I show speech bubbles? how do I put a name over a shop? how do I turn off damage numbers?

## Notifications and UI from scripts

Notification channels: **toast** (one at a time), **feed** (stacked, coalesced: "+3 Gold" merges), **banner** (centre,
large) and **alert**; a history of 50. Rules in `ui/notifications.json` (else the engine's) map events such as
`quest_stage`, `item_added`, `level_up` to a channel and text.

Opening screens without a script: a Reaction's or part's `do` with `{"op": "open_screen", "screen": "inventory"}`
(crafting, spellbook, journal, or a screen id), an input action's **screen** (Project Settings ▸ Input), or a designer
button's On click.

Lua: `notify(channel, text[, seconds])`, `open_screen(id)`, `close_screen(id)`, `is_open(id)`; a document's
`action('…')` arrives as the `ui_action` event. The AI panel writes screens, styles, the theme and rules with
`write_ui`, checked as the UI tab checks them.

Questions: how do I show a notification? how do I open a screen from a script? how do I react to a UI button in Lua?
