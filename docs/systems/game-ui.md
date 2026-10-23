# Game UI — menus, windows, HUD, notifications and themes

Status: **built, to be tried (2026-10-22)**; see "As built" at the end. Phase 17 in `development_plan.md`. This is the
UI *of the games* AIGE makes (what a player sees), not the editor's UI. The AI Planner
designs it in its Visuals stage (`ai-planner.md` §5.8).

---

## 1. Goal and the main principle

A finished game needs a title screen, an ESC menu, settings, save and load, windows for
inventory, journal, map and the rest, a HUD, and notifications. Today the engine draws six
hard-coded overlays (`ui/GameUi.h`: banner, dialogue box, objective, clock, toast,
prompt).

**The main principle: everything is customisable.** No screen, widget or notification is
hard-coded. Built-in screens are documents the project can copy and change; themes are
tokens and art the project can override; custom screens can be made from scratch. A user
can go from "pick a theme" to "design every pixel" without leaving the engine's tools.

Also:
- **Data-driven:** the UI shows game state through **bindings**, not code that pushes
  values.
- **One input path:** keyboard, mouse and gamepad navigate every screen.
- **Localised and accessible** from the start.
- **AI-writable:** documents in a format language models already write well.

## 2. What engines use

| System | Model | Notes |
|---|---|---|
| **Unity UI Toolkit** | UXML (markup) + USS (CSS-like) + data binding; UI Builder editor | web-like authoring inside an engine |
| Unity uGUI | GameObjects with RectTransforms in the scene | flexible, verbose |
| **Unreal UMG** | widget blueprints in a visual designer; bindings; MVVM view models; **CommonUI** for input routing, activatable widget stacks and platform glyphs | stacks of screens, input routing per layer |
| **Godot** | Control nodes in containers; **Theme** resources with type variations | themes as reusable resources |
| **RmlUi** (open source, MIT) | HTML/CSS-like documents (RML, RCSS) in C++, flexbox, transitions and animations, **data models and bindings**, custom render and input interfaces | a game UI library, not a browser |
| Coherent Gameface | HTML/CSS/JS in a game-specific renderer | commercial |
| NoesisGUI | XAML | commercial |
| Bethesda (Scaleform) | Flash movies; SkyUI shows how far players customise | the demand for customisability |
| Dear ImGui | immediate mode | debug tools only |

## 3. Decision (proposed): RmlUi at run time, our layer on top

- **Runtime:** RmlUi, with a render interface on our Vulkan RHI and an input interface on
  our input actions. It gives layout (flexbox), text, styles, transitions, animations and
  data binding: years of work we'd otherwise rewrite.
- **Our layer:** screens and layers (§4), data models from the reflection registry (§7),
  themes (§8), notifications (§9), input navigation (§10), the widget library and the
  designer (§12).
- **Why HTML/CSS-like:** designers know it; the editor itself is Vue and CSS; and language
  models write it reliably, which matters for the Planner's Visuals step.
- **Alternatives considered:**
  - our own UI system: text layout, flex, animation and binding from scratch, for no
    product gain;
  - an embedded browser (CEF): heavy, a second process, poor fit for a game loop;
  - commercial libraries: licensing.
- **Risk:** RCSS is a subset of CSS. The widget library and designer work within it; the
  validator flags unsupported properties.
- Adding the dependency (MIT) is the first decision of Phase 17 to confirm.

## 4. Concepts and layers

| Concept | What | Examples |
|---|---|---|
| **Screen** | a full menu that takes input; usually pauses | title, pause (ESC), settings, save/load, game over, credits |
| **Window** | a panel over gameplay; pauses or not, per window | inventory, journal, map, character, spell book, shop, container, crafting, dialogue |
| **HUD** | the persistent layer during play | bars, quick slots, quest tracker, minimap |
| **Widget** | a reusable component with properties and bindings | bar, slot grid, item tooltip, quest entry, key prompt |
| **Pop-up** | a modal question | "Overwrite save?", "Quit to title?" |
| **Notification** | a timed message | "Quest updated", "+25 gold", "Level up" |
| **World-space UI** | attached to entities | name plates, enemy health bars, damage numbers, interaction prompts |

Layers, bottom to top: world-space → HUD → windows → screens → pop-ups → notifications →
debug. Screens and windows form a **stack**: ESC closes the top; opening the pause menu
over the inventory keeps the inventory underneath. Each layer entry declares its **input
context** (`gameplay`, `menu`, `dialogue`), so input goes to the top context only
(CommonUI's model).

## 5. The screens and windows

Each is a built-in document the project can use as is, restyle, or replace.

| Id | Shown when | Contains |
|---|---|---|
| `title` | the game starts | logo, New game, Continue, Load, Settings, Credits, Quit |
| `new-game` | New game | difficulty choice (`difficulty.md`), character creation if the game has it |
| `pause` | ESC in play | Resume, Save, Load, Settings, Help/controls, Quit to title, Quit |
| `save-load` | Save / Load | slots with thumbnail, place, play time, date; autosaves; delete |
| `settings` | Settings | tabs: Video (resolution, window mode, vsync, UI scale), Audio (buses), Controls (rebinding per device), Gameplay (difficulty if changeable, assists, subtitles), Accessibility, Language |
| `game-over` / `victory` | death / an ending | message, Load last save, Retry from checkpoint, Title |
| `credits` | from title or after an ending | `CREDITS.md` and the game's own credits |
| `loading` | scene transitions | a tip, art, progress |
| `inventory` | `inventory` action | items (list or grid per the game's model), filters, weight/slots, item details, use/equip/drop |
| `character` | `character` action | attributes, equipment slots, level and experience |
| `skills` | `skills` action | skills, perks or a tree |
| `spellbook` | `spells` action | abilities by school, assign to quick slots (`magic.md`) |
| `journal` | `journal` action | quests (active, completed, failed), stages and journal text, the tracked quest |
| `map` | `map` action | the local map (from the scene's tiles, revealed as explored), the world map (from the world graph), markers, fast travel |
| `dialogue` | a conversation | speaker, portrait, text, choices, history (today's dialogue box, as a document) |
| `shop` | a vendor's Talk | stock, player inventory, prices, buy/sell/barter |
| `container` | open a chest or corpse | contents, take all |
| `crafting` | a crafting station | recipes, ingredients, result |
| `codex` | `codex` action | lore entries unlocked (World Bible entries marked public) |
| `tutorial` | a tutorial event | a card with text and key glyphs |

## 6. The HUD

Widgets, each with bindings, a position (anchored to a screen edge or corner, with an
offset), a visibility rule (always, in combat, when changed, never) and a player-adjustable
scale and opacity:

| Widget | Shows |
|---|---|
| `bar` (health, mana, stamina, experience, boss) | a value and its max, with a change trail |
| `quickslots` | items and abilities on keys, with cooldowns and counts |
| `status-effects` | icons with time left |
| `quest-tracker` | the tracked quest's current objectives and progress (today's objective overlay) |
| `minimap` / `compass` | surroundings and markers |
| `clock` | day and time (today's clock overlay) |
| `prompt` | the interaction prompt with the device's glyph (today's prompt) |
| `score`, `lives`, `timer`, `combo`, `multiplier` | arcade genres |
| `ammo`, `crosshair` | shooters (later genres) |
| `subtitles` | spoken lines |
| `notification-area` | where toasts appear (§9) |
| `damage-numbers`, `enemy-bars`, `name-plates` | world-space |

The HUD is a document (`ui/hud.rml`) like any other, so its layout is edited in the
designer; the player's adjustments (scale, opacity, hide a widget) are saved in their
settings.

## 7. Data binding

Documents bind to **data models**, which the engine builds from what it already knows:

| Model | From |
|---|---|
| `player` | the player entity's reflected components and attributes |
| `inventory`, `equipment` | the inventory system |
| `quests`, `journal` | the quest system (quests v2) |
| `story` | story variables and flags |
| `world` | time, weather, the scene and its location's name |
| `settings` | runtime settings (two-way) |
| `saves` | save slots |
| `context` | the screen's own arguments (the container being looted, the vendor) |
| custom | any module or Lua can register a model |

RmlUi syntax, with our models:

```html
<div class="bar health" data-model="player">
  <div class="fill" data-style-width="(player.health / player.health_max * 100) + '%'"></div>
  <span>{{ player.health | int }} / {{ player.health_max | int }}</span>
</div>
<div data-model="inventory" class="grid">
  <div class="slot" data-for="item : inventory.items" data-event-click="ui.use(item.id)">
    <img data-attr-src="item.icon"/><span class="count" data-if="item.count > 1">{{ item.count }}</span>
  </div>
</div>
```

- **Events call UI actions**, which are registered commands (`ui.use`, `ui.equip`,
  `ui.save(slot)`, `ui.open('map')`), so Lua and the AI can call them too and the
  reflection registry documents them.
- **Formatting:** numbers, percent, time, and localised strings (`{{ 'quest.updated' | tr }}`).

## 8. Themes

A **theme** is everything visual that isn't layout:

| Part | What |
|---|---|
| tokens | colours (background, panel, text, accent, danger, rarity colours…), fonts and sizes, spacing, borders, shadows, motion durations |
| frames | 9-slice images for panels, buttons, slots, tooltips, bars (with their slice margins) |
| icons | the default icon set (close, back, gold, quest marker, rarity) |
| sounds | hover, click, open, close, error, notification |
| motion | open/close transitions per layer |

Widgets and screens refer to **tokens only** (`var(--panel)`, `frame: panel`), so changing
the theme restyles everything.

- **Built-in themes** (`data/ui/themes/`): Parchment, Stone, Iron, Arcane, Sci-fi glass,
  Minimal flat, Pixel frame. Each is complete and licensed for any use.
- **A game's theme** (`ui/theme.json`): a built-in theme plus overrides, or entirely its own.
- **Generated themes:** palette from the project's art (the catalog's style profile), fonts
  from the library, frames from the Art tool (checked to be valid 9-slices: corners and
  repeating edges), sounds from the library.
- **Checks:** text contrast (WCAG AA) per token pair, a missing frame, a font without the
  game's languages' glyphs.

## 9. Notifications and pop-ups

**Channels**, each a widget in the HUD with its own style and queue:

| Channel | For | Behaviour |
|---|---|---|
| `toast` | quest started/updated/completed, location discovered | top centre, one at a time, 3 s, queued |
| `feed` | items gained, experience, gold | side list, stacked, coalesced ("+3 Gold" merges into "+7 Gold") |
| `banner` | level up, chapter title, boss name | centre, large, 2–4 s |
| `subtitle` | spoken lines | bottom |
| `alert` | low health, overweight, cannot do | brief, by the relevant widget |

**Rules** (`ui/notifications.json`) map events to notifications:

```json
{ "rules": [
  { "event": "quest_stage", "if": { "journal": true }, "channel": "toast",
    "text": "quest.updated", "args": ["quest.name"], "icon": "quest", "sound": "notify", "priority": 2 },
  { "event": "item_added", "channel": "feed", "text": "item.gained", "coalesce": "item.id" },
  { "event": "level_up", "channel": "banner", "text": "player.levelup", "pauseQueue": "toast" } ] }
```

Priority, coalescing, duration, whether it shows during dialogue or cutscenes, and a
**history** (the last 50, viewable from the journal). Lua: `notify(channel, text, opts)`.

**Pop-ups** are documents with a title, text and buttons; `confirm(text, on_yes, on_no)` in
Lua; they take the top input context.

## 10. Input and navigation

- Every focusable element is reachable by **directional navigation** (automatic, by
  geometry; overridable per element with `nav-up/down/left/right`).
- **Input contexts** per layer (§4); the `menu` action (ESC / Start) opens `pause` from
  gameplay and closes the top layer otherwise.
- **Glyphs:** prompts show the current device's key or button (keyboard, Xbox,
  PlayStation, Switch layouts), switching when the device changes.
- Mouse hover and click everywhere; scrolling lists by wheel, stick and keys.

## 11. Levels of customisation

| Level | The user… | How |
|---|---|---|
| 1 | picks a theme | Visuals in Project Settings, or the Planner |
| 2 | changes tokens, frames, icons, sounds, fonts | the theme editor |
| 3 | changes a built-in screen's layout | "Customise" copies its document into `ui/screens/`; the designer edits it; the project's copy wins |
| 4 | makes new screens and widgets | new documents and RCSS; Lua for behaviour; registered in `ui/screens.json` with their action or event |
| player | adjusts the HUD (scale, opacity, hide widgets), text size, subtitles | the settings screen |

Built-in screens are updated with the engine; a customised copy is the project's and is
not overwritten (the designer shows when the built-in has changed since the copy).

## 12. The UI designer (editor)

A panel, **UI**, docked like the others:
- **Screens list** (built-in and the project's), the HUD, widgets, pop-ups.
- **Canvas** with resolution and aspect presets (16:9, 16:10, 21:9, 4:3, Steam Deck) and a
  safe-area overlay.
- **Palette** of widgets; **hierarchy**; **properties** generated from each widget's schema
  (like the Inspector); **styles** with token pickers.
- **Bindings:** a picker over the data models (from reflection), with mock data in the
  editor or live data while playing.
- **Code view:** the RML and RCSS, kept in step with the visual edits.
- **Theme editor**, **notification rules** editor, **screens manifest** (which action
  opens which screen, pauses, input context).
- **Checks:** unresolved bindings, unsupported RCSS, contrast, text overflowing at the
  longest translation, unreachable focusable elements, a screen no action opens.

## 13. Files

| File | Holds |
|---|---|
| `ui/screens.json` | the manifest: each screen/window id, document, layer, input context, pauses, opened by (action, event, Lua) |
| `ui/screens/<id>.rml`, `ui/widgets/<id>.rml` | the project's documents (only customised or new ones; the rest come from the engine) |
| `ui/styles/*.rcss` | styles |
| `ui/theme.json` | the theme: base theme and overrides |
| `ui/hud.rml` | the HUD (if customised) |
| `ui/notifications.json` | notification rules |
| `loc/<lang>.json` | strings (shared with the rest of the game) |

## 14. Lua

`open_screen(id, args)`, `close_screen(id)`, `is_open(id)`, `notify(channel, text,
opts)`, `confirm(text, on_yes, on_no)`, `hud_show(widget, bool)`, `ui_model(name)` (a
table whose writes update bindings), `on_ui(action, fn)`.

## 15. Localisation and accessibility

- Every string a document shows is a key (`tr`), with the source language written by the
  designer or the Planner and other languages by translation (AI-assisted, reviewed).
- Fonts per language (fallback chains); right-to-left later.
- Text size scale, colour-blind palettes (token sets swapped per mode), subtitles with
  speaker names and background, reduced motion (disables transitions and screen shake),
  hold-to-toggle; a screen reader later.

## 16. Existing overlays

The six overlays of `ui/GameUi.h` become default documents (`dialogue`, `quest-tracker`,
`clock`, `toast` channel, `prompt`, `banner`), so existing projects look the same until
they customise.

## 17. How the Planner designs the UI

From the Brief's Visuals tab and the systems that are on (`ai-planner.md` §5.8):
1. **Theme:** the library theme closest to the art style and tone (its tags against the
   Brief's), or a generated one; token overrides from the project's palette.
2. **Screens:** the core set always; windows by system (inventory ⇐ items, journal ⇐
   quests, map ⇐ more than one place, spellbook ⇐ magic, character and skills ⇐
   progression, shop ⇐ merchants, crafting ⇐ crafting); settings tabs by what exists
   (difficulty if `changeAnytime`, assists).
3. **HUD:** widgets by system and the Brief's HUD density (minimal: health and prompt;
   standard: + resource bars, quick slots, quest tracker, compass; full: + minimap, clock,
   status effects, experience), placed by genre convention (bars top-left or bottom-centre,
   minimap top-right).
4. **Notifications:** rules for the events the game has, at the Brief's prominence.
5. **Previews:** each screen rendered with mock data in the theme, shown in the Planner
   panel for approval; edits go to the documents and re-render.
6. **Checks:** §12's, plus every system's screen exists and is opened by an action in the
   input map.

## 18. As built (Phase 17)

- **RmlUi 6.0** (MIT, fetched by CMake, static, without FreeType): `ui/Documents` gives it our
  TrueType font engine (the built-in face for any family, or a `.ttf` by its file name), a render
  interface that turns its geometry into the renderer's UI quads (clipped to scissor regions;
  CSS transforms, gradients and rounded corners are approximated as rectangles), a file interface
  that looks in the project's `ui/` before the engine's `data/ui` and puts the theme's tokens into
  styles, and the game's data as models.
- **The game's logic stays where it was.** GameFlow and PlayWindows still decide what a menu
  shows (MenuView); a document only shows it, and a click calls the same `Choose()`. So every
  screen works the same by keyboard, gamepad or mouse.
- **Models:** `menu` (title, subtitle, body, footer, items, highlighted, `choose(i)`), `dialogue`
  (speaker, text, portrait, choices, `choose(i)`), `hud` (health, mana, stamina and their maxima,
  level, xp, xp_next, gold, quest, objective, clock, prompt_key, prompt_text, subtitle, marker),
  `notes` (toast, feed, banner, alert) and `ui` (`action('…')`, the `ui_action` event for Lua).
  A model must be on an element inside `<body>`.
- **Documents** (`data/ui/screens`): `menu.rml` (every menu), `title.rml`, `window.rml`
  (inventory, shop, journal), `dialogue.rml`, `hud.rml`; styles in `data/ui/styles`. The
  manifest `data/ui/screens.json` (and a project's `ui/screens.json`) says which document a
  screen uses. **Customise** in the UI tab copies a document into the project and points the
  manifest at it.
- **Themes:** seven built-in token sets (`data/ui/themes`: stone, parchment, iron, arcane,
  scifi-glass, minimal, pixel); `ui/theme.json` picks one and overrides tokens. 9-slice frames,
  icon sets and sounds per theme are later work.
- **Notifications:** toast (one at a time), feed (coalesced by key), banner and alert channels;
  the game's own notices go to them by kind, and `ui/notifications.json` (else the engine's)
  maps events to notifications. A history of 50.
- **Glyphs:** prompts show the key or gamepad button the action is bound to, for the device used
  last.
- **Editor:** Project ▸ **UI** tab: screens (source, Customise, preview over the viewport with
  sample data, new screens), the theme (base and token overrides), notification rules, and
  "Check all". Play uses the documents too.
- **Lua:** `notify(channel, text[, seconds])`, `open_screen(id)`, `close_screen(id)`, `is_open(id)`.
- **The AI:** `write_ui` writes screens, styles, the theme, the manifest and the rules; checked
  like the UI tab checks them.
- **Fallback:** Project Settings ▸ UI.documents off keeps the built-in overlays (and headless
  runs and tests without a renderer use them).
- **Not yet:** pop-ups and `confirm()`, the loading, character, skills, spellbook, map, codex,
  crafting and tutorial screens (with the systems they show), world-space UI (name plates,
  damage numbers), the player's HUD adjustments, mouse input in the standalone player, a visual
  (drag and drop) designer, contrast and overflow checks.

