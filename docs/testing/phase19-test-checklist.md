# Phase 19 — Manual Test Checklist

The user-test checkpoints from Phase 19 (`development_plan.md`, "Abilities and magic"), for
one end-of-phase pass. Automated tests cover the logic: presets, formulas, cost and balance,
validation, learning from a book, a fire projectile, a heal with a cast time, an aura, costs and
cooldowns, stacking, stun, buffs and dispel, scrolls, potions, enchantments worn and on hit, perks
by tag, and an enemy mage that attacks and heals itself (`magic_test.cpp`, `test_magic.py`).
This list covers how it plays. The design is `magic.md` (see "As built").

**Before you start**
- **Rebuild:** close the editor, then `cmake --build build --config Release --target editor sandbox`.
- The player with **Resources** (mana, stamina) and **Attributes**, a few enemies.

## 19.1–19.2 Spells

- [ ] Records ▸ item: a book `{"name": "Tome of Fire", "kind": "book", "use": {"learn": "firebolt"}}`
      in a chest. Read it (I ▸ use): "firebolt" is known and in slot 1 (HUD: "1 Firebolt").
- [ ] Press **1**: the player casts (animation), a fiery projectile flies at the nearest enemy in
      front, sparks and an orange light where it lands, the enemy burns; mana goes down; pressing
      again at once says it isn't ready (and the HUD shows the seconds left).
- [ ] Learn *heal* and *flame-aura* (Lua `learn(self_id(), "heal")` or more books). **K** opens the
      abilities: pick *heal* to put it in slot 1. Heal takes a moment, then restores health; the
      aura burns enemies who come near for ten seconds.
- [ ] With too little mana: "not enough mana".

## 19.3 Items and perks

- [ ] A scroll `{"use": {"ability": "frost-nova"}}`: using it freezes and slows the enemies
      around without knowing the spell; it's used up.
- [ ] A potion `{"use": {"effects": ["regen"]}}`: health comes back over a few seconds.
- [ ] A sword with `"enchantment": {"effects": ["fire-weapon"], "on": "hit"}`: equipped, every hit
      sets the target burning. A ring with `{"effects": ["armour-up"], "on": "worn"}` adds defense
      while worn (inventory shows it), not after taking it off.
- [ ] A perk with `"abilityModifiers": [{"tag": "element.fire", "stat": "cost", "value": 0.5}]`,
      taken: firebolt costs half.

## 19.4 The Magic tab

- [ ] Project ▸ **Magic**: every ability with its cost, the formula's cost, output per cost and per
      second; Customise *firebolt*, set its cost to 1 in Records: its formula cell turns orange.
- [ ] **Try** on *frost-nova*: Play starts and the player casts it.

## 19.5 Enemies that cast

- [ ] An enemy with **Abilities** (known: "firebolt, heal") and Resources (mana): it shoots at you
      from a distance; hurt below 40% it stops to heal itself.

## The AI panel

- [ ] Ask the AI "a frost mage enemy that slows the player and heals when hurt": an ability or two
      (records), Abilities on the enemy prefab with AI hints, passing the checks.
- [ ] Ask for "a book that teaches the heal spell, sold by the priest": an item record with
      use.learn and the merchant's stock.
