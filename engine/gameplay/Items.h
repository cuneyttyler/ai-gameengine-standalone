#pragma once

#include "ecs/Registry.h"

#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace aige {

// Items, inventories, equipment, loot and trade (development_plan.md 13.3–13.4).
// Items are records (records/item/<id>.json, records.md); entities hold them by
// id. Contents are written as text so the Inspector and scene files show them
// plainly: "potion*3, iron-sword" (an id, then *count when more than one).

struct ItemStack {
    std::string item;
    int count = 1;
};

std::vector<ItemStack> ParseItems(const std::string& text);
std::string FormatItems(const std::vector<ItemStack>& items);

// What a character carries.
struct Inventory {
    std::string items;      // "potion*3, iron-sword"
    int gold = 0;
    int slots = 0;          // 0: as many as you like
    float maxWeight = 0.0f; // 0: no limit
};

// What a character wears and wields: an item id per slot. Its items'
// modifiers, damage and armour count towards its attributes (Attributes.h).
struct Equipment {
    std::string weapon;
    std::string offhand;
    std::string head;
    std::string body;
    std::string hands;
    std::string feet;
    std::string ring;
    std::string amulet;
};

const std::vector<std::string>& EquipmentSlots();
std::string* EquipmentSlot(Equipment& equipment, const std::string& slot);

// An item lying in the world: picked up by walking over it (or E when `onUse`).
struct ItemPickup {
    std::string item;
    int count = 1;
    bool onUse = false; // needs the interact action rather than walking over it
};

// A chest, a crate, a corpse's pocket: its contents, filled from a loot table
// the first time it's opened.
struct Container {
    std::string items;
    int gold = 0;
    std::string loot; // a loot record id
    bool looted = false;
};

// Someone who trades: their stock (from a loot table, restocked each day) and
// how prices are set.
struct Merchant {
    std::string stock;      // a loot record id
    std::string items;      // what's for sale now
    int gold = 500;         // what they can pay
    float buyRate = 1.0f;   // the player pays value x this
    float sellRate = 0.4f;  // the player gets value x this
    int restockDay = 0;     // the day the stock was last filled (runtime)
};

// --- Inventory operations (the player's and anyone's) -----------------------------------

int CountItem(const Inventory& inventory, const std::string& item);
// Adds; returns how many fitted (slots and weight).
int AddItem(Inventory& inventory, const std::string& item, int count = 1);
// Removes up to `count`; returns how many were removed.
int RemoveItem(Inventory& inventory, const std::string& item, int count = 1);
float InventoryWeight(const Inventory& inventory);

// Equips an item from the entity's inventory into its slot (the record's
// `slot`), putting back what was there. The reason it can't, else "".
std::string Equip(Registry& registry, Entity entity, const std::string& item);
std::string Unequip(Registry& registry, Entity entity, const std::string& slot);
// Uses a consumable (the record's `use`: heal, mana, stamina, a status effect),
// taking one from the inventory. The reason it can't, else "".
std::string UseItem(Registry& registry, Entity entity, const std::string& item);

// --- Loot -----------------------------------------------------------------------------

struct LootResult {
    std::vector<ItemStack> items;
    int gold = 0;
};
// Rolls a loot record (records/loot/<id>): `rolls` times a weighted pick of
// its entries (an item with a count range, or another table), each with its
// chance; gold in its range. Entries outside `level`'s band are skipped.
LootResult RollLoot(const std::string& table, int level, std::mt19937& rng);

// Price of one of `item` at `rate` (its record's value x rate, at least 1).
int ItemPrice(const std::string& item, float rate);

} // namespace aige
