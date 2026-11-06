# Records

What records are, the Records page (Project ▸ Records), and how to create, edit, inherit, rename, duplicate and delete
them, with the checks that keep them valid. Each record type's purpose is in "Record types"; the per-field list is
given to the AI separately.

## What a record is

A record is a piece of typed game data that isn't an entity placed in a scene: an item, a loot table, a faction, an AI
package, a recipe, a difficulty level, a magic effect, an ability and so on. Each record is one JSON file:

`records/<type>/<id>.json` (for example `records/item/iron-sword.json`, `records/package/innkeeper-work.json`)

- The **type** decides which fields the record has, their kinds (number, whole number, on/off, text, a choice from a
  list, a picture or sound or script or prefab path, a reference to another record, a group of sub-fields, or a list of
  any of these), their ranges and defaults. The engine's types are built in; a project module can add its own types.
- The **id** is the file name: lower-case letters, digits, `-` and `_` only.
- A field left out of the file takes the type's default.
- Things in scenes refer to records by id: an Inventory's `items` ("potion*3, iron-sword"), a FactionMember's faction,
  a Spawner's encounter, a Behaviour's packages, a Container's loot table.
- Records belong to the project, not to one scene, and ship with the exported game.

### Example: an iron sword the blacksmith sells

Brannoc's smithy sells an iron sword, and the town guard carries one too.

1. The sword is one file, `records/item/iron-sword.json`; its id is `iron-sword`:

   ```json
   {
     "name": "Iron sword",
     "description": "Plain, heavy and sharp enough.",
     "kind": "weapon",
     "value": 60,
     "weight": 3,
     "slot": "weapon",
     "damage": 6,
     "class": "sword"
   }
   ```

2. Fields left out (`stack`, `unique`, `armor`...) take the item type's defaults.
3. In the scene, Brannoc's Inventory **items** reads `iron-sword, potion*3`, and the guard's reads `iron-sword`.

Both refer to the same record, so changing its `value` to 75 changes the price everywhere.

Questions: what is a record? where are records stored? what is a record id?

## The Records page: finding records

Open it from the **Project** panel's **Records** tab ("Typed game data: items and the project's other record types").

- The left column lists every record type as a group (item, loot, recipe, perk, progression, faction, package,
  procedure, combat-style, difficulty, difficulty-table, animator, particles, effect, magic-effect, ability,
  magic-balance, achievement, encounter, plus any types the project's modules add). Hover a group's name to read what
  the type is for. Click ▾/▸ to fold a group; the number beside it is how many records it holds.
- **Filter records…** at the top searches ids, names and the records' whole contents (typing "iron" finds every record
  mentioning iron).
- Each row shows the record's `name` (or its id when it has none), `: <base>` when it inherits from another record, and
  a ⚠ when it has problems (hover the ⚠ to read them).
- With nothing selected, the right side explains records and lists every problem in the project; click a problem's key
  (`item/iron-sword`) to open that record.
- Inspector ▸ Behaviour's ✎ button next to a package opens that package here directly.

### Example: finding everything that mentions iron and fixing a broken record

1. In the **Project** panel, click the **Records** tab.
2. Type `iron` in **Filter records…**. The list narrows to `iron-sword`, the `iron-ingot` material, the `smith-sword`
   recipe that uses ingots, and the `smithy-stock` loot table that lists the sword.
3. The `smithy-stock` row shows a ⚠. Hover it: the problem says its entry refers to an item `iron-helm` that doesn't
   exist.
4. Clear the filter and click nothing: the right side lists every problem; click `loot/smithy-stock` to open it, or
   create the missing `iron-helm` item with **+** on the item group.

The ⚠ goes away once the helm exists.

Questions: where is the Records panel? how do I search records? what does the warning sign next to a record mean?

## Creating and editing a record

1. In Project ▸ Records, press **+** on the type's group header (its tooltip says "New <type>").
2. The form opens with the type's defaults. Type an **Id** (the file name, e.g. `healing-potion`).
3. Fill in the fields. The form is generated from the type: text boxes, number boxes, tick boxes, drop-downs for
   choices, searchable pickers for references to other records (and for scripts, paths, markers, named things and
   dialogue files where the field asks for one), sections for grouped fields, and lists with add and remove buttons.
   Hover a field's name to read its description. Required fields are marked with an orange `*`.
4. Press **Create** (or **Ctrl+S**). The engine checks the record against its type before writing it; if it breaks the
   type (a wrong kind of value, a missing required field, a value out of range) the reason appears in a box and nothing
   is written.

After creating, the button reads **Save**. **Revert** goes back to the saved file (**Cancel** for a record not yet
created). "Unsaved changes" shows while the form differs from the file. The record's path is shown at the bottom right.

Records the AI panel writes or deletes appear in the Records page and in every picker at once, without a restart.

### Example: a healing potion

1. In Project ▸ Records, press **+** on the **item** group header ("New item").
2. **Id**: `healing-potion`.
3. **name**: `Healing potion`; **description**: `A small red draught. Restores 25 health.`; **kind**: `consumable`;
   **value**: `25`; **weight**: `0.2`; **stack**: `10`.
4. Open the **use** section and set **heal** to `25` (leave **consumed** ticked so drinking uses it up).
5. Press **Create** (or **Ctrl+S**).

The file `records/item/healing-potion.json` is written:

```json
{
  "name": "Healing potion",
  "description": "A small red draught. Restores 25 health.",
  "kind": "consumable",
  "value": 25,
  "weight": 0.2,
  "stack": 10,
  "use": { "heal": 25 }
}
```

If you typed `-5` in **value**, Create would refuse and show the reason (below the minimum of 0) without writing.

Questions: how do I make a new item? how do I save a record? why won't my record save?

## Inheriting from a base record

A record can inherit the fields of another record of the same type: pick it in **Base** at the top of the form. The
file then holds `"base": "<id>"` and only the fields this record sets itself; every other field comes from the base.

- With a base chosen, each field gets a tick box. Ticked: "Set here (untick to inherit from the base)". Unticked:
  "Inherited from the base (tick to set here)", and the field shows the base's value, greyed.
- Unticking a field drops this record's own value and shows the base's again.
- The record's own fields win, field by field. Example: `{ "base": "iron-sword", "name": "Fine iron sword", "value": 75 }`
  is an iron sword in every way except its name and price.
- A base must exist and must not lead back to the record itself (a cycle is reported as a problem).

Use bases for families: a `potion` base with the icon, kind and weight, and small/medium/large potions that set only
`name`, `value` and `use.heal`.

### Example: a fine iron sword that inherits from the iron sword

1. In Project ▸ Records, select `iron-sword` and press **Duplicate**; change the **Id** to `fine-iron-sword`.
2. In **Base** at the top of the form, pick `iron-sword`. Every field now has a tick box.
3. Untick everything except **name** and **value**; set **name** to `Fine iron sword` and **value** to `75`.
4. Tick **damage** and set it to `8`.
5. Press **Create**.

The file holds only what differs:

```json
{ "base": "iron-sword", "name": "Fine iron sword", "value": 75, "damage": 8 }
```

Its kind, slot, weight and class come from `iron-sword`; raise the iron sword's weight later and the fine one follows.

Questions: can a record inherit from another? what does the tick box next to a field do? how do I make variants of an
item?

## Renaming, duplicating and deleting records

- **Renaming:** change the **Id** box and press **Save**. The file is renamed, and every reference to it in other
  records (record-reference fields such as a loot entry's item, a recipe's inputs, a perk's requires, a faction's
  relations, and other records' `base`) is rewritten to the new id. References held as plain text outside records,
  such as a character's Behaviour packages list or an Inventory's items in a scene, are not rewritten: update those by
  hand. A new id must be valid and not already used by a record of that type.
- **Duplicate** (enabled when there are no unsaved changes) opens a copy under a new id (`<id>-copy`, `<id>-copy-2`…);
  press **Create** to write it.
- **Delete…** asks for confirmation and deletes the file. The confirmation lists what still uses the record ("Still
  used by: …"), and the form shows a **Used by** line with links to each record that references this one.

### Example: renaming a potion used in a loot table and a recipe

1. In Project ▸ Records, open the item `potion`. Its **Used by** line shows `loot/chest-common` and `recipe/brew-potion`.
2. Change **Id** to `minor-healing-potion` and press **Save**.
3. The file becomes `records/item/minor-healing-potion.json`, and the loot table's entry and the recipe's input now say
   `minor-healing-potion`.
4. Vara Bell's Inventory in the inn scene still reads `potion*3`: select her, and in Inspector ▸ Inventory change
   **items** to `minor-healing-potion*3` by hand.
5. To make a stronger one, press **Duplicate** (it opens `minor-healing-potion-copy`), set **Id** to
   `healing-potion`, raise **use ▸ heal**, and press **Create**.

Questions: how do I rename a record? does renaming update references? how do I copy a record? what happens when I
delete a record that's in use?

## Record checks and problems

The Records page, the AI panel and the tests all check records and report problems (⚠ in the list, and a box under the
form):

- files that don't parse as JSON, or have an invalid id;
- folders under `records/` that aren't a known type;
- unknown fields, wrong kinds of value, values out of range, values not among a field's options;
- missing required fields;
- a base that doesn't exist or leads back to the record;
- references to records that don't exist;
- asset paths (pictures, sounds, scripts, prefabs) that don't exist.

Saving refuses a record that breaks its type. A reference to a record that doesn't exist yet is allowed (and reported),
so related records can be written in any order: write the loot table first, then its items.

### Example: a loot table written before its items

1. Create a loot record `crypt-chest` whose entries name `bone-charm` and `old-coin`, neither of which exists yet.
2. **Create** succeeds; the row shows ⚠ and the box under the form lists two references to missing items.
3. Create the items `bone-charm` and `old-coin` (**+** on the item group).
4. Back on `crypt-chest`, the ⚠ is gone.

If instead you type `"weight": "heavy"` in an entry when editing the file by hand, the record shows ⚠ (wrong kind of
value), and the Records page refuses to save it until the weight is a number.

Questions: why does my record show a warning? what do record problems mean? can a record refer to one that doesn't exist
yet?

## Template-dependent fields (packages)

Some record fields only apply in some cases. The clearest example is the `package` record: its `procedure` applies
only to template `custom`, `target` only to follow, escort, flee, custom and script, and each value inside `data` only
to the templates that read it (`route` to follow-path, patrol and custom; `mode` and `from` to follow-path; `outsideWeight`
to live and sandbox, and so on).

- The Records form shows only the fields that apply to the record's current values; change the template and the form
  changes with it.
- When you save, values of fields that no longer apply (data left over from another template) are dropped.
- For a package, the form also shows **What <template> does**: the template's steps in words with this package's data
  filled in, the JSON of the template under "As JSON", and **Copy into a custom package…**.

### Example: switching a package from wander to patrol

1. In Project ▸ Records ▸ package, open `guard-wander`. Its template is `wander`, and the form shows **location** and
   **data** with **radius** (`6`), **waitMin** and **waitMax**.
2. Change **template** to `patrol`. **radius**, **waitMin** and **waitMax** disappear; **route**, **path** and
   **duration** appear.
3. Pick `town-wall` in **route** and press **Save**.

The saved file has `"data": { "route": "town-wall" }`: the old `radius` was dropped because patrol doesn't read it.
**What patrol does** now describes walking the `town-wall` path.

See also: AI packages, Package templates, Custom procedures.

Questions: why did a field disappear when I changed the template? why was some data removed when I saved a package?

## Records from Lua and the AI panel

- **Lua:** `record(type, id)` returns a table of the record's fields with its base and defaults applied
  (`record("item", "iron-sword").value`), or nil; `records(type)` returns the ids of every record of a type.
- **The AI panel:** its context lists the project's records and the schema of every record type. It writes a record
  with `write_record` and removes one with `delete_record`; both appear in Diff Review like any file it writes, and are
  checked the same way the Records page checks them. Ask it, for example, "make three healing potions of rising
  strength" or "add a bandit faction hostile to the town guard".
- **Export:** the `records/` folder ships with the game.

### Example: a sign that shows an item's price, and potions made by the AI

The smithy's price board has an Interactable (verb `Read`, **event** `read-prices`) and this script:

```lua
on("read-prices", function(e)
  local sword = record("item", "iron-sword")
  if sword then
    notify("toast", "Iron sword: " .. sword.value .. " gold")
  end
end)
```

To make records with the AI panel, type:

> "make three healing potions of rising strength: minor 15, normal 30, greater 60 health"

It writes `records/item/minor-healing-potion.json` and the others with `write_record`; check them in Diff Review and
press Apply. They appear in Project ▸ Records at once.

See also: Record types, Lua API.

Questions: how do I read a record in a script? can the AI create records for me?
