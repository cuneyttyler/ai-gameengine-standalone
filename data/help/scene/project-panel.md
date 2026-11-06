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

### Example: from the troll's health to the difficulty table and back

1. Press **P** to show the Project panel; its tabs run Scenes, World, Story, Records, Scripts and so on.
2. Click **Story** to look over the quest "The Lost Ring" you're writing.
3. Now select the `Cave troll` in the scene and click the **⚖** by **Health ▸ max** in the Inspector: the Project
   panel switches to **Difficulty** on its own, showing the troll's health at every level.
4. Click **Records** to check the troll's loot table, then **Scenes** to go back to the scene list.

Questions: what is the project panel? where are quests and dialogue? where are items defined? how do I open the project
panel?

## Project ▸ Scenes

The Scene Manager: every scene of the project with its **start** and **open** badges and its marker count, the **New
scene…** button, and on the right the **Gateways** column listing gateways that lead nowhere. Double-click a scene to
open it; right-click for Open, Set as start scene, Rename…, Duplicate…, Type, New scene…, Delete…. **File ▸ Scenes…**
brings this page up.

See scenes.md for the details of each action, scene types and gateway problems.

### Example: jumping from the harbour to the lighthouse scene

1. Choose **File ▸ Scenes…** (or click **Scenes** in the Project panel).
2. The list shows `harbour` (**open**), `lighthouse` (2 markers) and `village` (**start**).
3. Double-click `lighthouse`. If the harbour has unsaved changes, you're asked about saving first; then the lighthouse
   opens and gets the **open** badge.
4. Right-click `lighthouse` ▸ **Duplicate…** to keep a copy before a big change.

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

### Example: a ringing bell script, and who uses it

1. Click **Project ▸ Scripts** and press **New script…**; type `church_bell`. `scripts/church_bell.lua` opens in the
   script editor.
2. Write it and save:

```lua
properties = {
    sound = "bell.wav",   -- the bell's sound
}

function update(dt)
    -- ring on the hour
end
```

3. Select the church tower in the scene, add **LuaScript** (+ Add component) and pick `church_bell` in **path**.
4. Back on Project ▸ Scripts, the `church_bell` row now names the church tower among its users; type `tower` in
   **Search scripts and what uses them** to find it again among many scripts.

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

### Example: a silver key for the crypt

1. Click **Project ▸ Records**, choose the item type and make a new record with the id `crypt_key`.
2. Fill in its form: name `Silver Key`, kind `key`, tick unique, and a description. The record is saved as
   `records/item/crypt_key.json`:

```json
{
  "description": "A tarnished silver key with a skull on its bow. It opens the crypt under the chapel.",
  "kind": "key",
  "name": "Silver Key",
  "unique": true
}
```

3. Select the crypt's door, and in its **Reaction: Door, Chest, Sign** pick `crypt_key` as the **key**: the list now
   offers it.

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

### Example: giving the blacksmith his dialogue and his day

1. In **Project ▸ World**, check that `Brom Ashford`, the blacksmith, is among the characters.
2. In **Project ▸ Story**, write his dialogue `brom_forge` and a daily routine `smith_day` (forge by day, home at
   night).
3. Select Brom in the scene. In **Talk**, pick **character** `Brom Ashford` and **dialogue** `brom_forge`; add
   **Routine** and pick `smith_day` as its **schedule**.
4. To make his hammering sparks, open **Project ▸ Effects** and make a particle effect there.

Questions: where is the World Bible? where are daily routines? where do I edit an animation sheet? where are cutscenes?
