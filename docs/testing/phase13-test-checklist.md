# Phase 13 — Manual Test Checklist

The user-test checkpoints from Phase 13 (`development_plan.md`, "Gameplay systems"), for
one end-of-phase pass. Automated tests cover the logic: attributes and modifiers,
experience and perks, inventories (stacks, weight, slots), equipping and using, loot
rolls, doors, locks, chests and pickups, shops, projectiles, resistances, blocking and
dodging, factions and spawners, and kills giving experience and loot in play
(`gameplay_systems_test.cpp`). This list covers how it plays. The reference is
`gameplay-systems.md`.

**Before you start**
- **Rebuild:** close the editor, then `cmake --build build --config Release --target editor sandbox`.
- A test project with a player and a few enemies. Make these records in Project ▸
  Records: a **potion** (use: heal 5), a **sword** (slot weapon, damage 3), a **ring**
  (slot ring, a modifier maxHealth multiply 1.5), a **bone**, a **loot** table "skeleton"
  (bone and potion entries, gold 2-6), a progression record **default**.

## 13.1–13.2 Attributes and progression

- [ ] Give the player **Attributes** (maxHealth 20, maxStamina 50), **Inventory** and
      **Progression**. In Play, the Inspector shows Stats and Health.max following them.
- [ ] Give a skeleton **Rewards** (experience 150, gold 3-5, loot skeleton). Killing it
      shows "Level 2!" and heals you; the inventory (I) shows level 2.
- [ ] Equip the sword: attack goes up by 3. Equip the ring: max health x1.5.

## 13.3 Items and the inventory

- [ ] The skeleton's loot lies where it fell, with the items' icons (or coloured tokens);
      walking over it picks it up with a notice.
- [ ] **I** opens the inventory over a paused game: gold, level, health, attack, defense;
      the sword "- equip", the potion "- use". Using a potion heals; equipping moves the
      sword to [weapon]; picking [weapon] takes it off. Esc or I closes.
- [ ] A chest (**Container** with loot "skeleton"): E opens it and takes everything once;
      again: "It's empty."

## 13.4 Trade

- [ ] An NPC with **Merchant** (stock: a loot table): E opens the shop with both sides'
      gold; buying takes your gold, selling gives you theirs; you can't buy without gold.
- [ ] The stock refills the next game day.

## 13.5 Things to use

- [ ] A door entity with **Obstacle**, **Interactable** (locked, key: a key item) and
      **Door**: E says it needs the key; with the key it opens and you walk through; E again
      closes it.
- [ ] A sign (**Interactable**, verb Read, text): E shows the text long enough to read.
- [ ] A lever (**Interactable**, setFlag) sets a story flag a dialogue checks.
- [ ] Build a dungeon with locks (Build panel). In Play, its locked doors need their keys,
      and the keys lie in their rooms.

## 13.6 Combat

- [ ] An enemy with **RangedAttack** shoots at you from range when it can see you; walls
      stop its shots.
- [ ] Give the player a RangedAttack: **F** shoots the way you face.
- [ ] Hold **Left Ctrl** to block: hits do much less (stamina drains). **Left Shift**
      dodges: a quick dash, untouchable while it lasts.
- [ ] Set MeleeAttack.damageType Fire on an enemy and resistFire 0.5 on the player: its
      hits do half.

## 13.7 Factions and spawners

- [ ] Two faction records with a relation of -80; members of each (FactionMember) fight
      each other. A friendly member (disposition 30) leaves you alone until you hit it.
- [ ] A **Spawner** with an encounter (prefab: your skeleton) fills up to maxAlive when the
      scene starts, and makes another respawnSeconds after one dies.

## The AI panel

- [ ] Ask the AI "give the blacksmith a shop that sells swords and armour": it writes item
      records (and a loot table) and adds a Merchant whose stock names it.
- [ ] Ask for "a locked chest that needs the cellar key, with some gold inside": an
      Interactable (locked, key) and a Container, with the key item written as a record.
- [ ] Ask for "skeletons that spawn in the crypt": an encounter record and a Spawner.
