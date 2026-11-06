# Crafting and items in hand

Recipes (Project ▸ Records ▸ recipe), combining two items, the Crafting list for three or more items, stations, items in
hand ("Use on..."), characters crafting, and the related scripts and events.

## Recipes

A **recipe** record makes items from items. Make one in Project ▸ Records with **+** on the recipe group:

| field | meaning |
|---|---|
| `name` | shown in the Crafting list (required) |
| `inputs` | items used up, each with a count (1 when empty) |
| `keeps` | items needed but not used up (a knife, a pot) |
| `gives` | what it makes, each with a count |
| `at` | a station (`stove`): made only at a Reaction or moving part whose `station` names it; empty: anywhere |
| `seconds` | how long it takes; what it makes comes after |
| `clip`, `effect`, `text` | on the player as it's made |
| `if` | conditions: it can be made only while they hold |
| `do`, `start`, `interrupted` | effects when made, when begun (with seconds), if cut short (inputs are given back) |

The form edits `if`, `do`, `start` and `interrupted` with the rule editor. Inputs are unordered.

Example, a stove's stew: inputs meat, water, turnip ×2; keeps pot; gives stew; at `stove`.

Questions: how do I add a recipe? how do I make a recipe need a tool that isn't used up? how do I make cooking take time?

## Combining two items, and the Crafting list for more

- **Two items** combine by using one on the other: pick an item in the inventory, choose another item, and the recipe of
  those two is made, or "That doesn't work".
- **Three or more items** go through the **Crafting** list: right-click an item in the inventory ▸ **Crafting** (the
  recipes using it), press the `crafting` action (C: everything that can be made here), or use a station (a Reaction or
  moving part with `station`: its recipes too).
- In the list, lines that can't be made are greyed and say what's missing.

A recipe with `seconds` under way is kept in a save; cut short (the player hit, a cutscene), its inputs come back.

Questions: how do I combine items? how does crafting with three ingredients work? where is the crafting window? why is a
recipe greyed out?

## Items in hand and "Use on..."

Pick an item in the inventory: one with no use of its own by a click, any by a right click (`ui_context` on keys and
gamepads) ▸ **Use on...**. The item is now in hand. Then:

- choose another inventory item: their recipe is made, or "That doesn't work";
- close the window and click a thing, or press interact near it: its Reaction runs if it `accepts` the item (seeds on a
  field, a key on a lock), or a station's recipe of that item alone is made, or the station's list opens;
- right click or Esc (`put_back`, pad B): put it away.

A Reaction with `accepts` works for the player only with the item in hand; other users just need to carry one. It takes
the item only if `takes` says so.

Questions: how do I use an item on something in the world? how do I make a lock that needs the key in hand? how do I put
an item away?

## Characters crafting, scripts and events

- A character makes a recipe from its own inventory, into it: Lua `craft(recipe, station, who)` returns "" or why not.
  Use it in a script package (find the stove with `find_part`, walk there, use it, craft).
- Lua for the player: `hold_item(id)`, `held_item()`, `put_back_item()`, `craft(id[, station])`.
- Events: `item_used` (text: the item), `item_combined` and `crafted` (text: the recipe).
- The AI panel can write recipe records, checked like the Records page checks them.

See also: Actions and Reactions, Record types.

Questions: can an NPC cook a recipe? how do I know when the player crafted something? how do I give an item to the
player's hand from a script?
