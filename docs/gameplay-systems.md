# Gameplay systems

The character and gameplay systems of `development_plan.md` Phase 13: attributes and
progression, items, inventories and equipment, loot and trade, things to use, ranged
combat with blocking and dodging, factions and spawners. Everything is components (set in
the Inspector, read by scripts and the AI) and records (`records.md`).

## Attributes (13.1)

**Attributes** holds a character's base numbers: `maxHealth`, `maxMana`, `maxStamina`,
`attack`, `defense`, `moveSpeed`, `healthRegen`, `manaRegen`, `staminaRegen` and the
resistances `resistFire`, `resistFrost`, `resistShock`, `resistPoison` (the share of that
damage taken away, up to 0.9).

Each step the effective value of each is worked out:

```
effective = (base + every add) x every multiply
```

from equipment (an item's `modifiers`, a weapon's `damage` to attack, armour's `armor` to
defense), perks taken, and timed modifiers (a potion's boost, a script's `add_modifier`).
The results are written to **Stats** (attack, defense, moveSpeed), **Health.max** and
**Resources** (mana and stamina maxima), so everything that reads those keeps working.
Without Attributes, Stats and Health are set by hand as before. Health, mana and stamina
regenerate at their rates.

## Progression (13.2)

**Progression**: `level`, `experience` towards the next, `perkPoints`, `perks` taken.
**Rewards** on an enemy: `experience` for its killer (whoever hit it last), and gold and a
`loot` table dropped where it fell.

The curve and each level's gains come from the progression record **default**
(`records/progression/default.json`): `baseExperience` (level 1 to 2), `growth` (each next
level needs that many times more), `maxLevel`, `perkPointsPerLevel`, and `perLevel`
(attributes added at each level). A level up heals, shows "Level n!" and emits
`level_up`.

**Perk** records: `name`, `level` (from when it can be taken), `requires` (other perks),
`modifiers`. `take_perk(id, perk)` in Lua (the perk screen comes with the game UI, Phase 17).

## Items, inventories, equipment (13.3)

Items are **item** records. Phase 13 adds `slot` (none, weapon, offhand, head, body, hands,
feet, ring, amulet), `damage`, `armor`, `modifiers` and `use` (heal, mana, stamina, a status
effect, an attribute boost for a while, consumed or not).

| Component | Holds |
|---|---|
| **Inventory** | `items` as text: `"potion*3, iron-sword"` (ids, `*count` for more than one), `gold`, `slots` (0: any number), `maxWeight` (0: no limit) |
| **Equipment** | an item id per slot |
| **ItemPickup** | an item lying in the world (`item`, `count`); walked over, or used with E when `onUse`. The item `gold` is gold |
| **Container** | a chest or crate: `items`, `gold`, a `loot` table rolled the first time it's opened |

In play, **I** (Tab, gamepad Y) opens the inventory: the character's gold, level, health,
attack and defense, what's worn (pick it to take it off) and what's carried (pick it to
equip or use it). The game waits while it's open.

## Loot and trade (13.4)

**Loot** records: `rolls` picks from weighted `entries` (an item with a count range, or
another loot `table`), each with a `chance` and a level band; and gold between `goldMin`
and `goldMax`. Enemies drop their loot where they fall, as pickups with the items' icons.

**Merchant**: their `stock` (a loot table, filled again each day), what's for sale now
(`items`), the `gold` they can pay, and `buyRate` / `sellRate` (times an item's value).
E opens their shop: buy and sell, with both sides' gold shown.

## Things to use (13.5)

**Interactable**: the prompt's `verb` and `name`, `range`, `locked` with its `key` item
(and `consumeKey`), `requiresFlag`, `setFlag`, `text` (shown when used: a sign, a book),
`event` (emitted for scripts), `once`. What else happens comes from its other components:

| With | Using it |
|---|---|
| **Door** | opens and closes it; open, its Obstacle doesn't block (`openTexture`, `closedTexture` swap the picture) |
| **Container** | takes everything inside |
| **ItemPickup** | picks it up |
| **Merchant** | opens the shop |

E uses the nearest one in range when nobody to talk to is closer; the prompt says what
it'll do ("Open Chest", "Read Sign").

**Dungeons built by the place builder** get working locks and keys: each locked door is a
locked Door that opens with its key, and each key lies in its room as a pickup, with an
item record of its own (`records/item/key-<...>.json`).

## Combat v2 (13.6)

- **Damage types**: Physical, Fire, Frost, Shock, Poison on MeleeAttack and RangedAttack;
  resistances take their share away.
- **RangedAttack**: `damage`, `range`, `speed`, `cooldown`, `projectile` picture, `pierce`.
  The player shoots with **F** (gamepad RB), or with attack when it has no melee weapon.
  Enemies with one shoot at the player when in range and in sight.
- **Block**: hold **Left Ctrl** (gamepad LB): `Combat.blockReduction` of a hit is stopped,
  for twice the blocked damage in stamina (when the character has stamina).
- **Dodge**: **Left Shift** (gamepad B): a dash of `Combat.dodgeDistance` cells the way
  you walk, untouchable while it lasts, for `Combat.dodgeStamina`.

## Factions and spawners (13.7)

**Faction** records: `name`, `relations` with other factions (`standing` -100..100: below 0
they fight), `playerStanding`. **FactionMember**: the faction, and a `disposition` to the
player (below 0: hostile). Attacking a member lowers it. Without factions, the three teams
(Neutral, Player, Enemy) decide as before.

**Encounter** records: weighted `entries` (a prefab, a count range, a level band).
**Spawner**: makes its `encounter`'s enemies within `radius`, up to `maxAlive`, again
`respawnSeconds` after one dies (0: once), rolled at its `level`.

## Lua

| Function | Does |
|---|---|
| `attribute(id, name)` | the effective value |
| `add_modifier(id, attribute, value[, seconds, source, "multiply"])` | a modifier (seconds < 0: until removed) |
| `remove_modifiers(id, source)` | removes a source's modifiers |
| `give_item(id, item[, n])` / `take_item(id, item[, n])` / `count_item(id, item)` | the inventory (`"gold"` for gold) |
| `equip(id, item)` / `use_item(id, item)` | ok, reason |
| `award_xp(id, n)` / `level(id)` / `take_perk(id, perk)` | progression |
| `drop_loot(x, y, table[, level])` | rolls a loot table onto the ground |
| `shoot(x, y)` / `dodge(dx, dy)` | this entity's ranged attack, a dodge |
| `is_hostile(a, b)` / `set_disposition(id, n)` | factions |

Events: `item_added`, `level_up` (amount: the new level), `interact`, and an
Interactable's own `event`.
