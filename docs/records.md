# Records

Records are a game's typed data that isn't an entity in a scene: items now, and with the
next phases loot tables, factions, abilities, AI packages, difficulty levels and more
(`engine-gaps.md` §2.1, `development_plan.md` 12.1). Each record is one JSON file:

```
records/<type>/<id>.json
```

The type's fields come from the reflection registry, so the editor's **Records** tab, Lua,
the AI and the checks all read the same schema. Engine types are registered in C++
(`engine/records/Records.cpp`); a project adds its own in a module (`modules.md`, "Record
types").

## A record

```json
{
  "name": "Iron sword",
  "kind": "weapon",
  "icon": "assets/items/iron_sword.png",
  "value": 30,
  "weight": 3,
  "tags": ["blade", "iron"]
}
```

- Only the type's fields, with their types, ranges and options. Fields left out take the
  type's default.
- **Ids** are lower-case letters, digits, `-` and `_`: the file name.
- **`"base": "<id>"`** inherits another record's fields (of the same type); the record's
  own fields win, field by field:

  ```json
  { "base": "iron-sword", "name": "Fine iron sword", "value": 75 }
  ```

- **`"provenance": {...}`**: where it came from (the AI Planner, a build); kept, not checked.

Field kinds:

| Kind | In the file |
|---|---|
| `float`, `int`, `bool`, `string` | a number, a whole number, `true`/`false`, text |
| `enum` | one of its options, by name |
| `texture`, `audio`, `script`, `prefab` | a project-relative path |
| `record` | the id of a record of the field's `recordType` |
| `object` | a JSON object of its sub-fields |
| any, with `list` | a JSON list of such values |

## The engine's types

**`item`** — something that can be picked up, carried, sold or used.

| Field | Type | |
|---|---|---|
| `name` | string, required | shown to the player |
| `description` | string | a line or two for the inventory |
| `kind` | misc, weapon, armor, consumable, key, quest, material, currency, book | |
| `icon` | texture | |
| `value` | int ≥ 0 | price |
| `weight` | float ≥ 0 | |
| `stack` | int ≥ 1 (default 1) | how many fit in one slot |
| `unique` | bool | only one exists (story items) |
| `tags` | list of string | for searching, loot tables, conditions |

Phase 13 extends `item` and adds the other gameplay types.

## Where records are used

- **Editor:** Project panel ▸ **Records**. Records listed by type (a filter searches names,
  ids and contents); + makes one from the type's defaults; the form is generated from the
  type; **Base** picks a record to inherit from, and each field's tick says whether it's
  set here or inherited; **Duplicate**, **Delete…** (warns what still uses it); changing the
  id renames the record and every reference to it. Problems are marked ⚠.
- **Lua:** `record(type, id)` and `records(type)` (`scripting.md`).
- **AI:** the schema lists the record types; the context lists the project's records; the
  AI writes them with `write_record` and removes them with `delete_record`, shown in Diff
  Review like any file it writes.
- **Export:** `records/` ships with the game.

## Checks

The Records tab, the AI and the tests report: files that don't parse or have a bad id;
folders that aren't a known type; unknown fields; wrong types, ranges and options; missing
required fields; a base that doesn't exist or leads back to the record; references to
records that don't exist; asset paths that don't exist. Saving refuses records that break
their type; a reference to a record not written yet is allowed (and reported), so records
can be written in any order.
