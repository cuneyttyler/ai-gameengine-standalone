# The Project panel and its pages

What the Project panel is and what each of its pages lists: Scenes, World, Story, Records, Scripts, Difficulty, UI,
Effects, Magic and Story graph. The pages other than Scenes and Scripts are explained in detail in their own topics.

## The Project panel

The **Project panel** holds the game's project-wide content: what doesn't belong to one scene. Show or hide it with
**P**. Its pages are tabs along the top:

| Tab | What it lists |
|---|---|
| **Scenes** | the project's scenes, the start scene, and gateways that lead nowhere |
| **World** | the World Bible: the game's canon (characters, places, factions…) |
| **Story** | dialogue, quests and daily routines |
| **Records** | typed game data: items and the project's other record types |
| **Scripts** | every Lua script in the project, what uses it, and the script editor |
| **Difficulty** | difficulty levels, the category table, previews and the curve |
| **UI** | the game's screens, theme and notifications (documents in `ui/`) |
| **Effects** | animation sheets, particles and effects, cutscenes |
| **Magic** | abilities and magic effects, with their cost and output |
| **Story graph** | the story knowledge graph: facts over time, dependents and the story rules |

Hover a tab for its description. Other parts of the editor open pages here for you: the Inspector's ⚖ opens
Difficulty, a package's ✎ opens Records, Assets ▸ Animations ▸ Edit… opens Effects.

See also: scenes.md.

Questions: what is the project panel? where are quests and dialogue? where are items defined? how do I open the project
panel?

## Project ▸ Scenes

The Scene Manager: every scene of the project with its **start** and **open** badges and its marker count, the **New
scene…** button, and on the right the **Gateways** column listing gateways that lead nowhere. Double-click a scene to
open it; right-click for Open, Set as start scene, Rename…, Duplicate…, Type, New scene…, Delete…. **File ▸ Scenes…**
brings this page up.

See scenes.md for the details of each action, scene types and gateway problems.

Questions: where do I manage scenes? how do I see all my levels?

## Project ▸ Scripts

**Project ▸ Scripts** lists every Lua script in the project's `scripts/` folder:
- each row shows the script's name, what uses it (things in this scene and packages; "not used here" when nothing in
  the open scene does) and **✎ Edit**;
- **Search scripts and what uses them** filters by the script's name and its users' names (every word must match);
- **Refresh** looks again for scripts and their users;
- **New script…** asks a name (letters, digits, `-` and `_`), and opens `scripts/<name>.lua` in the script editor to
  write.

Double-click a row or press **✎ Edit** to open it in the script editor. With none: "No scripts yet. Most things need
none; New script… starts one."

To attach a script to a thing, select it and use the Inspector's **+ New script…** (makes and attaches one) or add a
**LuaScript** component and pick the script in its **path** field.

Questions: where are my scripts? how do I create a Lua script? which objects use this script? how do I open the script
editor?

## Project ▸ Records (overview)

**Project ▸ Records** holds the game's typed data: items, and every other record type the engine or the project's
modules declare (packages, loot tables, factions, encounters, combat styles, effects, animators and more). Each record
is a file, `records/<type>/<id>.json`; the page lists records by type and edits one at a time with a form made from its
type's fields. The host checks each record.

Many Inspector fields pick records from here: Container's and Merchant's loot, FactionMember's faction, Spawner's
encounter, Behaviour's packages and combatStyle, and the items in a Reaction's requires/takes/gives. A Behaviour
package's **✎** button opens that package on this page.

Records are covered in detail in their own topic.

Questions: what are records? where do I make items? where are packages edited?

## Project ▸ World, Story, Difficulty, UI, Effects, Magic, Story graph

- **World**: the World Bible, the canon of the game: characters, places, factions and more. Talk's **character** field
  and a scene's place come from here; a scene that shows a World Bible place has its name beside it in the Scene Tree.
- **Story**: dialogue, quests and daily routines (schedules). Talk's **dialogue** and Routine's **schedule** fields pick
  from here.
- **Difficulty**: the difficulty levels, how each category of value scales, a preview of the selected entity at every
  level (the Inspector's ⚖), and the curve.
- **UI**: the game's screens, theme and notifications.
- **Effects**: animation sheets (with the sheet editor), particles and effects, cutscenes.
- **Magic**: abilities and magic effects.
- **Story graph**: facts over time, what depends on them, and the story rules.

Each has its own topic.

Questions: where is the World Bible? where are daily routines? where do I edit an animation sheet? where are cutscenes?
