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

### Example: a torch from a stick and rags, and a stew at the stove

1. In Project ▸ Records, press **+** on the **recipe** group; **Id** `torch`.
2. **name** `Torch`; **inputs**: `stick` count 1, `rags` count 1; **gives**: `torch` count 1; **text** `You wrap the
   rags round the stick.` **Create**.
3. Press **+** again; **Id** `stew`: **name** `Hearty stew`; **inputs** `meat` 1, `water` 1, `turnip` 2; **keeps**
   `pot`; **gives** `stew` 1; **at** `stove`; **seconds** `5`; **clip** `cook`; **text** `The stew is ready.`
4. In **if**, add a condition with the rule editor: flag `learned_cooking` is set. **Create**.

```json
{
  "name": "Hearty stew",
  "inputs": [{ "item": "meat", "count": 1 }, { "item": "water", "count": 1 }, { "item": "turnip", "count": 2 }],
  "keeps": ["pot"],
  "gives": [{ "item": "stew", "count": 1 }],
  "at": "stove",
  "seconds": 5,
  "clip": "cook",
  "text": "The stew is ready.",
  "if": "[{\"flag\": \"learned_cooking\"}]"
}
```

The torch can be made anywhere by using the stick on the rags. The stew shows up only at a stove, only once Vara has
taught the player (`learned_cooking`); it takes five seconds of the cook clip, uses the food, keeps the pot.

Questions: how do I add a recipe? how do I make a recipe need a tool that isn't used up? how do I make cooking take time?

## Combining two items, and the Crafting list for more

- **Two items** combine by using one on the other: pick an item in the inventory, choose another item, and the recipe of
  those two is made, or "That doesn't work".
- **Three or more items** go through the **Crafting** list: right-click an item in the inventory ▸ **Crafting** (the
  recipes using it), press the `crafting` action (C: everything that can be made here), or use a station (a Reaction or
  moving part with `station`: its recipes too).
- In the list, lines that can't be made are greyed and say what's missing.

A recipe with `seconds` under way is kept in a save; cut short (the player hit, a cutscene), its inputs come back.

### Example: making the torch and the stew in play

1. Press Play with `stick` and `rags` in the player's Inventory. Press **I**, click the stick (it has no use of its
   own), then click the rags: "You wrap the rags round the stick." and a torch appears. Clicking the stick on a turnip
   says "That doesn't work".
2. Walk to the inn's stove (a Reaction with **station** `stove`) and press **E**: the Crafting list opens with the
   stove's recipes.
3. "Hearty stew" is greyed and says what's missing ("Needs 1 more turnip"). Pick up another turnip, and it can be made.
4. Choose it: the player plays the cook clip for five seconds. Get hit halfway and the meat, water and turnips come back.

Pressing **C** anywhere instead lists everything that can be made here: the torch, but not the stew.

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

### Example: a lock that needs the key in hand, and seeds sown from the hand

1. Select the vault door's Reaction: **trigger** `input`, **action** `interact`, **accepts** `vault-key`, **becomes**
   `prefabs/vault-door-open.prefab`, **text** `The lock turns.`
2. Select the empty field's Reaction: **accepts** `seeds`, **takes** `seeds`, **becomes** `prefabs/field-seeded.prefab`.
3. In play, press **I**, right-click the vault key ▸ **Use on...**: it's in hand. Close the inventory and press **E**
   at the vault door: "The lock turns." and the door opens; the key isn't taken (no `takes`).
4. Put seeds in hand the same way and click a field: one seed is taken and the field is sown.
5. Right-click or press **Esc** to put the item away.

Pressing E at the vault door with the key only in the bag does nothing: for the player, `accepts` needs it in hand. A
guard told to `act` on the door just needs to carry it.

Questions: how do I use an item on something in the world? how do I make a lock that needs the key in hand? how do I put
an item away?

## Characters crafting, scripts and events

- A character makes a recipe from its own inventory, into it: Lua `craft(recipe, station, who)` returns "" or why not.
  Use it in a script package (find the stove with `find_part`, walk there, use it, craft).
- Lua for the player: `hold_item(id)`, `held_item()`, `put_back_item()`, `craft(id[, station])`.
- Events: `item_used` (text: the item), `item_combined` and `crafted` (text: the recipe).
- The AI panel can write recipe records, checked like the Records page checks them.

### Example: counting stews, and handing the player a fishing rod

```lua
-- scripts/cook-tally.lua, on any entity in the inn scene
on("crafted", function(e)
  if e.text == "stew" then
    set_var("stews_made", (get_var("stews_made") or 0) + 1)
    if get_var("stews_made") == 5 then unlock_achievement("master-cook") end
  end
end)

-- on the fisherman: "here, take my rod"
on("lend-rod", function(e)
  local player = find_nearest("Player")
  give_item(player, "fishing-rod", 1)
  hold_item("fishing-rod")          -- straight into the player's hand
end)
```

For Vara to cook the stew herself, a script package calls `craft("stew", "stove", self_id())` at the stove (see
"Scripting", scripted AI packages). To have the AI write recipes, type:

> "add recipes for bread (flour and water at the oven, 6 seconds) and a fishing rod (stick and string)"

See also: Actions and Reactions, Record types.

Questions: can an NPC cook a recipe? how do I know when the player crafted something? how do I give an item to the
player's hand from a script?
