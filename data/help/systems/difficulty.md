# Difficulty

Difficulty levels and how they scale the game: categories and steps, the category table, modifiers, per-entity
overrides, the Difficulty tab (Levels, Category table, Preview, Curve), level scaling, adaptive difficulty and assists.

## How difficulty works

Designers set **base values** once (an enemy's Health.max, a weapon's damage); a difficulty level is applied on top and
never written back into files.

- A **level** chooses a **step** (1-5; 3 is as designed, higher is harder) for each **category**: player, enemies,
  allies, stealth, resources.
- Scalable fields have a **kind**: health, damage, defense, speed, perception, count, gold, loot, price, xp. A field's
  category comes from its kind and its owner's side: gold, loot, price and xp are always resources; an enemy's
  perception is stealth; the rest are player, enemies or allies by whose it is.
- Scalable fields include Health.max, Stats (attack, defense, moveSpeed), MeleeAttack.damage, RangedAttack.damage,
  Perception (sight, hearing), Spawner.maxAlive, Inventory.gold, Container.gold, ItemPickup.count, Pickup.amount,
  Merchant buyRate and sellRate.
- The **category table** gives the multiplier for each category, kind and step.
- **Modifiers** on a level add to that for one field.

Effective values are applied when play starts, when a scene loads, after spawns and when the level changes; health keeps
its share.

### Example: what Hard does to a skeleton

The skeleton prefab has Health.max `30` and MeleeAttack.damage `6`; the player has Health.max `100`.

1. Open **Project ▸ Difficulty ▸ Preview** and select a skeleton.
2. Health.max (kind health, an enemy's: category enemies) reads 30 at Normal (step 3) and higher at Hard, which uses
   enemies step 4; MeleeAttack.damage rises the same way from 6. The player's Health.max is in the player category,
   which Hard may leave at 3.
3. The skeleton's file still says 30 and 6: the level is applied only when play starts or a scene loads.

Switching from Normal to Hard mid-fight keeps a half-dead skeleton at half its new health.

Questions: how does difficulty scale enemies? what are difficulty categories? does difficulty change my files?

## The Difficulty tab: Levels

Open **Project ▸ Difficulty**. A switch at the top shows **Levels**, **Category table**, **Preview** and **Curve**; **Play
at** picks the level Play starts at (default: the project's default level).

**Levels:** with no `records/difficulty` yet, the built-in five apply (Story, Easy, Normal, Hard, Nightmare); **Make them
this project's levels** copies them into records so you can edit them. Then **+ Level** adds one. For each level:

- **Id**, **Name**, **Order** (easiest first), **Description**;
- **Category steps**: buttons 1-5 per category;
- **Modifiers (on top of the steps)**: a field, whose (anyone's, the player's, enemies', allies'), "name contains"
  (e.g. boss: only entities whose Name contains it), an op (×, +, =, at least, at most) and a value; **+ Modifier**;
- **Experience** × rewards, and **one life** (permadeath);
- **Create** / **Save**, **Delete…**.

The default level, changing difficulty during play, adaptive difficulty and level scaling are in Project Settings ▸
Difficulty.

### Example: an "Ironman" level with one life and tougher bosses

1. Open **Project ▸ Difficulty ▸ Levels**. With no levels of your own yet, press **Make them this project's levels**.
2. Press **+ Level**. **Id** `ironman`, **Name** `Ironman`, **Order** `6`, **Description** `One life. Bosses hit hard.`
3. **Category steps**: player `4`, enemies `4`, allies `3`, stealth `4`, resources `5`.
4. **+ Modifier**: field `MeleeAttack.damage`, whose `enemies'`, name contains `boss`, op `×`, value `1.25`.
5. **Experience** `1.5`; tick **one life**. **Create**.

```json
{
  "name": "Ironman",
  "description": "One life. Bosses hit hard.",
  "order": 6,
  "categories": { "player": 4, "enemies": 4, "allies": 3, "stealth": 4, "resources": 5 },
  "modifiers": [{ "target": "MeleeAttack.damage", "owner": "enemy", "tag": "boss", "op": "mul", "value": 1.25 }],
  "xp": 1.5,
  "permadeath": true
}
```

6. Set **Play at** to `Ironman` to test it.

The game's difficulty choice lists Ironman after Nightmare. Enemies use step 4, gold and loot are scarcer, the
"Crypt boss" hits a quarter harder again, experience rewards are 1.5×, and dying ends the run.

Questions: how do I add a difficulty level? how do I make bosses tougher on Hard only? how do I choose which difficulty
Play uses?

## Category table, Preview, Curve

- **Category table:** "Multipliers per step (1 easiest … 5 hardest)". A row per category and kind with five numbers;
  **Save table** writes `records/difficulty-table/default.json` (rows left out use the built-in table).
- **Preview:** select an entity to see each scalable field at every level, with the base value; type a value to set it
  by hand for one level (saved in the entity's **DifficultyOverrides** component as `{"Health.max": {"hard": 20}}`), ↺ to
  go back. The **combat preview** shows the selection against the player per level: the player's hits to kill it, its
  hits to kill the player, and seconds each way. The Inspector's ⚖ opens the preview for the selection.
- **Curve:** checks scenes in gateway order from the start scene by their **Zone** band (minLevel, maxLevel): a band
  starting more than two levels above the previous one's end is "too hard", a band below the previous one is flagged,
  enemies outside their band are flagged. **Check again** reruns it.

### Example: a crypt boss with its own Story-mode health, and checking the curve

1. In **Category table**, find the row enemies / health and change step 5 from its value to `2.0`; **Save table**.
2. Switch to **Preview** and select the Crypt boss. Health.max shows its base 200 and a value per level. Type `120` in
   the Story column: it's saved in the boss's DifficultyOverrides as `{"Health.max": {"story": 120}}`. ↺ undoes it.
3. The combat preview shows, per level, the hits and seconds each way; say at Hard the player needs 18 hits and 25 seconds, the boss needs 6 hits. Lower the boss's
   MeleeAttack.damage if that's too harsh.
4. Switch to **Curve** and press **Check again**: the crypt scene's Zone band 8-10 follows the town's 1-3, so it's
   flagged "too hard". Add the forest scene (band 4-6) between them through gateways, or lower the crypt's band.

Questions: how do I set a value for one difficulty level only? how many hits does it take to kill an enemy on Hard? how
do I check the difficulty curve?

## Level scaling, adaptive difficulty and assists

- **Level scaling:** the **Zone** component gives a scene's level band; Project Settings ▸ Difficulty sets how enemies
  follow the player (scaling, levelOffset). Spawners use it and set a spawned character's level.
- **Adaptive difficulty:** ±1 step on enemies and stealth, after three deaths, or two fights won unhurt; off unless the
  game allows it and the player turns it on; kept in saves.
- **Assists** (the player's settings): invincible, damage reduction, more protection per death (2% each, up to 80%),
  game speed.
- **Scripts and conditions:** Lua `difficulty()`, `difficulty_step(category)`, `set_difficulty(id)`, the event
  `difficulty_changed`; dialogue conditions `{"difficulty": "story"}` or `{"difficulty": {"enemies": {"atLeast": 4}}}`.
- **The AI panel** knows the levels, table and scalable fields, so "make skeletons tougher" changes base values and
  "Hard is too easy in the crypt" changes a level's modifiers or the zone band.

### Example: an extra guard on hard levels, and asking the AI to rebalance

A script on the vault spawns a third guard on steps 4 and 5, and a dialogue line changes on Story:

```lua
-- scripts/vault-guards.lua
if difficulty_step("enemies") >= 4 then
  spawn("prefabs/vault-guard.prefab", 14.5, 9.5)
end
on("difficulty_changed", function(e)
  notify("toast", "Difficulty: " .. difficulty())
end)
```

```json
{ "text": "Need a hint, friend?", "next": "hint", "if": [{ "difficulty": "story" }] }
```

1. Select the crypt's Zone and set **minLevel** `5`, **maxLevel** `7`, so its enemies come at those levels.
2. In the AI panel, type:

   > "Hard is too easy in the crypt"

   It proposes, for example, a modifier on the Hard level for enemies' Health.max, or a higher Zone band; check it in
   Diff Review.

Questions: do enemies scale with the player's level? what is adaptive difficulty? what assists are there? how do I read
the difficulty in a script?
