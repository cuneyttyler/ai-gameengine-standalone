# End-user tests — v1.0

The tests a user-facing v1.0 release must pass: the whole app, from installing it to exporting a game, done by hand and
through the AI panel. Each item is marked with what the build covers today:

- **✓** built
- **◐** partly (the note says what's missing)
- **✗** not yet

§22 lists what v1.0 still needs. Run the tests on a clean machine with a fresh project, and again with an existing
project (FirstGame).

## 1. Install, launch, first project

- ✓ The launcher opens: New Project…, Open Folder…, recent projects (open; × removes the entry, the folder stays; a
  missing project is flagged).
- ◐ Open the demo (the demo project was removed; the button must open something or go).
- ✓ New Project: name, location (Browse…), template (Blank, RPG top-down, Action RPG isometric, Action-adventure, Life
  sim), grid (top-down / isometric), build method, AI choices (LLM, art, paint, background models); Create opens it.
- ✓ Each template gives a playable start: a scene, a player that moves, F5 plays.
- ✓ File ▸ Open Project, Recent Projects, Save (Ctrl+S), Save Project As…, Close Project, Exit (asks about unsaved
  changes).
- ✓ Crash recovery: kill the editor with unsaved changes; on the next start, Restore or Discard.

## 2. The editor window

- ✓ Eight panels (Viewport, Scene, Assets, Project, Generate, AI, Build, Planner): drag tabs to dock anywhere,
  double-click to maximise, the tab menu (Hide, Maximise, Dock to an edge), the side rail for hidden panels, View ▸
  Reset layout. The layout is kept between sessions.
- ✓ Every menu item works (File, Edit, View, Play, Help) and shows its shortcut.
- ✓ Help ▸ Keyboard Shortcuts (F1): rebind a shortcut, add a key, reset; the new key works at once.
- ✓ The title shows the scene and a dot while there are unsaved changes.
- ✓ Preferences: interface scale, open the last project, viewport zoom and wheel, grid at start, AI (on/off,
  provider, model, keys), art, paint and animate models, new-project defaults.
- ✓ Project Settings: every section edits and saves (Renderer, Camera, Combat, Game, Time, Grid, Lighting, Physics,
  Places, Player, Story, Difficulty, AI, Sourcing, UI, Input); each field's ✦ Ask AI.

## 3. Viewport editing

- ✓ Click selects; dragging a rectangle on empty ground selects several; Esc deselects.
- ✓ Right-drag pans, the wheel zooms; View ▸ Frame scene; G toggles the grid; F3 shows frame stats.
- ✓ Tools: Select (V), the prefab brush (1–9, or a prefab card's Paint with), Rotate, Erase (E); brush auto-fit and
  size; Height (raise, lower, level, radius).
- ✓ Arrows nudge a cell (Shift: a tenth); R / Shift+R turn; [ ] + - resize, { } fine; Alt+wheel resizes, Shift+wheel
  turns; Ctrl+C / Ctrl+V (at the cursor) / Ctrl+D; Delete.
- ✓ An entity's right-click menu: Draw with it, Replace prefab…, Duplicate, Copy, Bigger, Smaller, Turn, Save as
  prefab…, Revert to prefab, Ask AI about this…, Delete; interiors (show, floors, add cutaway or scene, open, build).
- ✓ The ground's right-click menu: draw with its tile, make an asset from the cell, place or paint here, paste here,
  add a marker here.
- ✓ Interiors: H steps through the selected building's floors, Shift+H all buildings, PgUp / PgDn floors.
- ✓ Edit ▸ Undo / Redo for every edit above, across many steps.

## 4. Scene Tree, Inspector, prefabs

- ✓ The Scene Tree lists the scenes; the open one in groups (Characters, Props, Tiles…); clicking selects; the same
  right-click menus as the viewport.
- ✓ Inspector: edit any field (numbers, text, colours, options, prefab and texture pickers), reset a field to its
  default, Ask AI about a field.
- ✓ + Add component (alphabetical), remove a component; components that need another add it.
- ✓ A prefab copy: changed fields show as overrides; revert one, Apply to prefab, discard this copy's changes.
- ✓ Size %, facing (turn to a direction), script properties, + New script….
- ✓ Several selected: what they share, Delete them.
- ✓ Save as prefab; a prefab edited updates every copy; Replace prefab (this one or every copy, with a facing).

## 5. Scenes

- ✓ New Scene: name and type (Assembled / Paint); it opens, empty.
- ✓ A scene's menu: Open, Set as start scene, Rename… (gateways follow), Duplicate…, Type ▸ Assembled / Paint, Delete…
  (not the start or open scene).
- ✓ Gateways and markers connect scenes; walking through a gateway in Play loads the other scene at its marker.
- ◐ The scene's type is kept in its file (being built).

## 6. Assets

- ✓ Tabs: Prefabs, Assets, Characters, Animations, Web Search, Library, Catalog.
- ✓ Folders, kind filter, search; multi-select (Ctrl/Shift-click, Ctrl+A); drag cards onto a folder; rename and move
  update references.
- ✓ Import… images and audio; Analyze; Import as prefabs (review dialog).
- ✓ An asset's details: preview, import settings, tileset tile picker, New tile / prop prefab, Use on the selected
  sprite, interior pairing.
- ✓ A prefab card's menu: Paint with, Place it, interiors, Rename, Move, Show in Explorer, Replace every copy, Lay flat /
  Stand upright, Delete.
- ✓ Web Search (OpenGameArt): find art, look at its files and page, Add to assets (licence noted).
- ✓ Library: add to the project, folders, import and export a .aige-library, sharing choice.
- ✓ Catalog: review an entry's role, footprint, perspective, pieces; re-measure it.

## 7. Generate

- ✓ Level: a dungeon (parameters, seed) or a village (terrain, road, region), built into the scene.
- ✓ Art: describe a sprite (kind, style), Generate; Make prefab, Regenerate, Use again, Discard; the history stays.
- ✓ Character: 4 or 8 directions, size, view; one character sheet; Make player, Make NPC; Animate.
- ✓ Animate with AutoSprite (Preferences ▸ AI ▸ Animate): a character in all 8 directions in one go; an image (a chest)
  in its own view; its motions (walk, punch, cast…) or custom words; outline stripped, colours matched.
- ✓ Humanoid or not decided from what it is (a chest isn't rigged as a person).
- ✓ Every button says why it's off; progress shows in the strip on top; the history stays when the tab changes.

## 8. Animation (docs/assets/animations.md)

- ✓ Assets ▸ Characters / Animations: each card plays; a clip chip plays that clip.
- ✓ The sheet editor (Edit…): clips (loop, fps, Plays on), directions, mirrors, frames, Add pictures…, the point tool;
  Remove a clip; Save / Revert.
- ✓ A clip's Plays on (`punch` playing on attack); several attacks (`attack_slash`, `attack_punch`); the Animator's
  `clips`, `pick` (weapon, random, order, first) and `attackClass`.
- ✓ Animators run in the editor: a character stands in its idle, facing its Facing; R turns it.
- ✓ `play(id, "dance")` plays a clip once; a cutscene's animate key plays any clip.
- ✓ Equipment ▸ weapon with a `sprite` item: the sword shows in the hand, follows the swing, goes behind the body facing
  away; unequipped, gone; never saved into the scene; clicking it selects its wearer.
- ✓ An Attachment puts any entity on another's point (a hat, a lantern, a rider).
- ◐ Holds a weapon (the magenta rod): the hand's points from a generated attack (needs a run with AutoSprite).

## 9. Build panel (docs/places)

- ✓ Describe a place; ① Confirm its brief (place type, size, choices, asset variety); ② Layout; ③ Plan; ⑥ Assets; ⑦
  Result into the scene; Revise in words at any step; Undo / Redo; Builds list to reopen one.
- ✓ Assembled: each piece its own art (project, library, web, generated, placeholder), on the grid over a painted
  background.
- ✓ Paint: ④ Paint (the place painted whole), ⑤ Mark (walkable and occluders: brush, eraser, fill, rectangle, base
  line; Fit; Save), then Assets and Result.
- ✓ Per building type: interior Cutaway / Scene / None; Generate interior per floor.
- ◐ The method comes from the open scene's type; choosing the other changes the scene's type (UI done, being built).
- ◐ From a picture, as the painting: Layout, Plan and Paint skipped, straight to Mark (being built).
- ◐ From a picture, as a reference: ④ Paint paints after it (the service is done, the build's side is being built).
- ✓ Edit ▸ Background Image: regenerate (model, build, words) or a picture file.

## 10. Places in play

- ✓ Statues, wells, carts, rocks block (roles.json `solid`); flowers, rugs, benches don't.
- ✓ Along a building's front or sides the player is drawn in front of it, not walked into it.
- ✓ Behind a building one can walk up to its walls (its blocked area fitted to its art), hidden by it.
- ✓ Scenes built before are repaired on opening ("made solid or passable… (Save keeps it)").
- ✓ A cutaway building shows its interior when the player is inside; a scene interior loads through its door.

## 11. Planner

- ✓ Home: a new plan from words and choices (genre, perspective, tone, art style), or an earlier plan.
- ✓ Stages ① Concept to ⑬ Result: each item's Why?, Regenerate, Edit JSON, Lock / Keep, Remove / Replace; regenerate
  what's stale.
- ✓ Build automatically or each place by hand; Pause / Resume / Stop; Retry failed; Fix now; Play from the start.
- ✓ The art style chosen becomes Project Settings ▸ AI ▸ Art style; every art prompt draws in it.
- ✓ The scenes the planner makes take the plan's build method as their type.

## 12. Story

- ✓ World Bible: entries by kind, search (by meaning with AI on), edit, relations, Delete; Ask AI to check consistency.
- ✓ Dialogue: nodes, choices, speakers, conditions, effects, skill checks; talk to the NPC in Play.
- ✓ Quests: stages, objectives, aliases; tracked in Play (accept, progress, complete, fail).
- ✓ Daily routines (schedules); the story graph (facts over time, rule issues, Sync).

## 13. Characters and their AI

- ✓ Make player / Make NPC from a character sheet; the player moves, collides and walks through gateways.
- ◐ A character comes with no attack: interact is the default action; MeleeAttack added gives it one (being built).
- ✓ Behaviour packages (sandbox, work, sleep, patrol, follow, guard, tend…) with schedules and locations; Perception,
  Barks, SmartObjects (beds, chairs, counters).
- ✓ An Actor persists across scenes.

## 14. Actions and input (docs/systems/actions.md)

- ✓ Project Settings ▸ Input: rebind built-in actions (keys, mouse, gamepad); **Add action** (farm) and **Remove** it;
  its description, context, **plays** (a clip on the player) and **event** (for scripts); Reset to defaults.
- ◐ Interact is the default action (E, Space); attack only with MeleeAttack (left click) (being built).
- ✓ An Action on a prefab: near it, press its action: it happens.
- ✓ `requires` / `takes` / `gives`: without seeds, "Needs seeds" and nothing happens; with them, taken and wheat given.
- ✓ `userAnimation` (the user plays `farm`), `animation` (the thing plays its own); `becomes` after both.
- ✓ trigger timer (a crop grows), touch (walked onto), event (another thing's `emits`).
- ✓ `once` and `cooldown`.
- ✓ An NPC with the package template **tend** farms the nearest free field; Lua `act(npc, door)` / `act(npc, "farm")`.
- ✓ An NPC opens a locked door with its own key; an Interactable chest plays `open` and holds; its becomes waits for it.
- ✓ Farming without a script: field → seeded (timer) → grown → harvested → field.

## 15. Data: records, items, magic, difficulty

- ✓ Records: a new record of each type, the form editor, inherit-from, Duplicate, Save, Revert, Delete; problems shown.
- ✓ Items, inventories, equipment, loot tables, merchants (a shop in Play).
- ✓ Magic: abilities and effects, Try, formula outliers.
- ✓ Difficulty: levels, the category table, preview, curve; the start level for Play.

## 16. Look and feel: effects, cutscenes, lighting, UI

- ✓ Particles and effects: Try, Customise; EffectCues on hit, death, attack.
- ✓ Cutscenes: tracks, keys, Save, ▶ Play; they play in the game.
- ✓ Lighting: day and night, Light2D, emissive sprites, weather.
- ✓ UI: screens (customise, edit, preview), theme, notifications; they show in Play.

## 17. Play, save, load

- ✓ F5 plays the open scene (or the start scene); Esc / F5 stops and the editor is as it was.
- ✓ Movement, interact, dialogue choices (1–4, arrows + Enter), the inventory, the HUD.
- ✓ F6 saves, F9 loads; Continue from save; a saved game keeps opened chests, dead enemies, quests.
- ✓ Script errors show in the console with the script and line.

## 18. Export

- ✓ File ▸ Export Game: name, folder (or a .zip), the debug console kept or left out; the accessibility report; Show
  in Explorer.
- ✓ The exported game runs on its own (no editor), starts at the start scene, saves and loads.

## 19. Robustness

- ✓ Undo / Redo across panels; Save keeps everything; reopening the project shows it as it was.
- ✓ The AI service off or missing a key: every AI button says why it's off; the rest of the editor works.
- ✓ Large scenes (1,500 things) edit and play smoothly; the laptop doesn't overheat on idle (frame cap).

## 20. The AI panel as an agent

Almost anything the user can do by hand, the AI panel can do from words. A big request is split into steps it carries
out one after another, looking at the results as it goes. Work that belongs to another panel (a whole place, a whole
game) is handed to that panel.

### 20.1 How it works

1. **Understand**: it reads what it needs with its *tools* (find, read, look) rather than being sent everything.
2. **Plan**: a small request is one step. A big one becomes a plan: numbered *tasks*, each a few *actions*, shown
   before it starts.
3. **Act**: each task's actions are applied to a working copy of the project.
4. **Check**: after each task it looks at the result (problems, a short playtest, a picture of the view) and fixes
   what it broke.
5. **Review**: the user reviews and applies, step by step or all at the end (their choice); Stop at any time;
   everything is one undo.
6. **Route**: a request that's a whole place goes to the Build panel; a whole game or story design goes to the
   Planner. It says why, with a button that opens that panel with the request filled in.

### 20.2 Tools (read and look)

| Tool | |
|---|---|
| `find_entities` | by name, prefab, component, area, tag; their ids, positions, components |
| `read_entity` | one entity's components and fields |
| `find_assets` / `find_prefabs` | by description (analysed art), folder, kind; the project's, the library's, the web's |
| `read_record`, `read_script`, `read_dialogue`, `read_quest`, `read_cutscene` | one file |
| `search_world` | the World Bible by meaning |
| `list_scenes`, `read_settings`, `read_input` | the project's scenes, settings, input actions |
| `look` | a picture of the view, or of an area or entity (to see what it made) |
| `what_is_at` | what's at a cell or a point in the picture |
| `check` | the problems: references, story graph, marks, solidity, scripts |
| `playtest` | play N seconds, optionally pressing actions; what happened (events, errors, where things went) |

### 20.3 Actions (change)

Everything in the Inspector, menus and panels, as operations:

| Area | Actions |
|---|---|
| Entities | spawn, delete, move, turn, mirror, resize, set any field, add / remove component, duplicate, copy |
| Prefabs | create (from an entity, an image), edit, replace (one, every copy), rename, move, delete |
| Ground | paint, paint a path, replace a tile, raise / lower |
| Scenes | create (with a type), open, rename, duplicate, delete, set start, set type, add marker, gateway |
| Assets | import, rename, move, delete, analyse, find on the web and add, keep in the library |
| Generate | art, a character, an animation (waits for it), a level, a background, an interior |
| Animation | write a sheet, a clip's Plays on, points; Animator clips / pick |
| Story | World Bible entries, dialogues, quests, schedules, cutscenes, the story graph |
| Data | records (write, delete), items, loot, abilities, difficulty |
| Actions and input | add / remove an input action, bind keys, plays / event; Action components |
| Scripts | write, attach, set properties |
| Settings | project settings, preferences it may change |
| Editor | select, frame the camera, open a panel, play |

### 20.4 Tests

- ✓ Small requests: "make the barrel bigger", "move the well to the square", "give the guard 30 health": one step,
  reviewed, applied, undone.
- ✓ Uses the selection: "make this solid" applies to what's selected.
- ◐ Several selected: "make these three red" (only one id is sent today).
- ✓ Writes quests, dialogues, World Bible entries, schedules, cutscenes, records, scripts, UI.
- ✓ Places characters at the right spot ("an innkeeper behind the bar").
- ✓ "When the player opens the door, the one inside steps forward": a script or an Action with trigger event.
- ✓ Generates missing art for a request, makes it a prefab, places it.
- ◐ "Animate the chest opening" / "make a farmer that walks": generates the animation (it can't call Generate ▸
  Animate yet).
- ◐ "Make a farm": a plan of tasks (seeds item, three field prefabs with Actions, a farmer with tend), carried out step
  by step, each checked, then reviewed (up to six steps today, one review at the end; no per-step review).
- ◐ "Add a cellar scene under the inn with stairs down": creates a scene, a gateway both ways (it can't create or open
  scenes yet).
- ◐ "Bind right click to parry and make the player parry": an input action and a clip (no input operation yet).
- ◐ It looks at what it made and fixes it ("the fence has a gap"): a picture of the view (not yet), a playtest of more
  than 2 seconds (not yet).
- ◐ It reads what it needs instead of being sent everything (no tools yet: the context is sent up front, 200 entities
  at most).
- ◐ "Build a village by the river" → the Build panel, request filled in; "design a game about…" → the Planner (keyword
  routing today; the model doesn't decide it).
- ✓ Script errors after applying: shown, with "Ask AI to fix".
- ✓ Stop mid-task: nothing applied; everything applied is one undo.
- ✗ It asks when a request is unclear, with choices to click.

## 21. Accessibility and help

- ✓ Help ▸ Documentation opens the docs; every button has a tooltip; panels say what to do when empty.
- ✓ The export's accessibility report (contrast, subtitles, rebinding).

## 22. What v1.0 still needs

The items marked ◐ or ✗ above, gathered. **AI panel (§20)** is the largest group.

### AI panel

1. **Tools**: function calling for `find_entities`, `read_entity`, `find_assets`, `find_prefabs`, `read_record` /
   `read_script` / dialogue / quest / cutscene, `search_world`, `list_scenes`, `read_settings`, `read_input`,
   `what_is_at`, `check`. It reads what it needs instead of getting all the context (200 entities, capped records)
   up front.
2. **Looking**: a `look` tool (a picture of the view, an area or an entity) and a `playtest` tool (N seconds, actions
   pressed, events and errors back).
3. **The agent loop**: plans of more than six steps; per-step review as an option; split suggestions carried out in a
   chain instead of only filling the input box.
4. **Missing actions**:
   - scenes (create with a type, open, rename, delete, set start, set type, gateway);
   - selection and camera;
   - input actions (add, remove, bind, plays, event);
   - Generate (character, animation, level, background, interior) that waits for its result;
   - assets (import, rename, move, delete, web search and add, library);
   - `delete_record` named in the prompt;
   - the prompt's count of operation kinds fixed (it says fifteen; there are sixteen).
5. **Routing by the model**: it answers "this is for the Build panel / Planner" with a reason and the request to fill
   in; this replaces the keyword checks in the editor.
6. **Several selected**: every selected id sent.
7. **Undo for files**: prefabs, scripts and settings written by the AI undoable too (today only the scene is).
8. **Clarifying questions**: when a request is unclear, choices to click.

### Elsewhere

9. Scene types stored in the scene file and used by the Build panel and Planner (the UI is done).
10. Build from a picture as the painting (Layout, Plan, Paint skipped) and as a reference (the build's side).
11. Interact as the default action; characters without a built-in attack.
12. NPCs opening doors on their way (today only with `act` or a package step).
13. The magenta-rod hand points, run with AutoSprite and checked.
14. Open the demo: a demo project again, or the button removed.
