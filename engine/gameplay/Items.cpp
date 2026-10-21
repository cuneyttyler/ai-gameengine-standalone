#include "gameplay/Items.h"

#include "gameplay/Magic.h"
#include "gameplay/Attributes.h"
#include "gameplay/Combat.h"
#include "gameplay/Components.h"
#include "records/Records.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <sstream>

namespace aige {

namespace {

std::string Trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

const nlohmann::json* ItemRecord(const std::string& item) {
    const Record* record = ActiveRecords().Find("item", item);
    return record ? &record->resolved : nullptr;
}

int StackSize(const std::string& item) {
    const nlohmann::json* r = ItemRecord(item);
    return r ? std::max(1, r->value("stack", 1)) : 1;
}

} // namespace

std::vector<ItemStack> ParseItems(const std::string& text) {
    std::vector<ItemStack> out;
    std::stringstream in(text);
    std::string part;
    while (std::getline(in, part, ',')) {
        part = Trim(part);
        if (part.empty()) continue;
        ItemStack stack;
        const auto star = part.find('*');
        if (star == std::string::npos) {
            stack.item = part;
        } else {
            stack.item = Trim(part.substr(0, star));
            stack.count = std::max(1, std::atoi(part.substr(star + 1).c_str()));
        }
        if (stack.item.empty()) continue;
        // One entry per item.
        auto it = std::find_if(out.begin(), out.end(), [&](const ItemStack& s) { return s.item == stack.item; });
        if (it != out.end()) it->count += stack.count;
        else out.push_back(stack);
    }
    return out;
}

std::string FormatItems(const std::vector<ItemStack>& items) {
    std::string out;
    for (const auto& stack : items) {
        if (stack.count <= 0 || stack.item.empty()) continue;
        if (!out.empty()) out += ", ";
        out += stack.item;
        if (stack.count > 1) out += "*" + std::to_string(stack.count);
    }
    return out;
}

const std::vector<std::string>& EquipmentSlots() {
    static const std::vector<std::string> slots = {"weapon", "offhand", "head", "body", "hands", "feet", "ring", "amulet"};
    return slots;
}

std::string* EquipmentSlot(Equipment& e, const std::string& slot) {
    if (slot == "weapon") return &e.weapon;
    if (slot == "offhand") return &e.offhand;
    if (slot == "head") return &e.head;
    if (slot == "body") return &e.body;
    if (slot == "hands") return &e.hands;
    if (slot == "feet") return &e.feet;
    if (slot == "ring") return &e.ring;
    if (slot == "amulet") return &e.amulet;
    return nullptr;
}

int CountItem(const Inventory& inventory, const std::string& item) {
    for (const auto& stack : ParseItems(inventory.items)) {
        if (stack.item == item) return stack.count;
    }
    return 0;
}

float InventoryWeight(const Inventory& inventory) {
    float weight = 0.0f;
    for (const auto& stack : ParseItems(inventory.items)) {
        if (const nlohmann::json* r = ItemRecord(stack.item)) weight += r->value("weight", 0.0f) * stack.count;
    }
    return weight;
}

int AddItem(Inventory& inventory, const std::string& item, int count) {
    if (item.empty() || count <= 0) return 0;
    auto items = ParseItems(inventory.items);
    const nlohmann::json* record = ItemRecord(item);
    const float unitWeight = record ? record->value("weight", 0.0f) : 0.0f;
    int fits = count;
    if (inventory.maxWeight > 0.0f && unitWeight > 0.0f) {
        const float room = inventory.maxWeight - InventoryWeight(inventory);
        fits = std::min(fits, std::max(0, static_cast<int>(std::floor(room / unitWeight + 1e-4f))));
    }
    if (inventory.slots > 0) {
        // Slots in use: each stack takes ceil(count / stack size).
        int used = 0;
        for (const auto& s : items) used += (s.count + StackSize(s.item) - 1) / StackSize(s.item);
        const int stack = StackSize(item);
        const int have = CountItem(inventory, item);
        const int freeInLast = have % stack == 0 ? 0 : stack - have % stack;
        const int room = freeInLast + std::max(0, inventory.slots - used) * stack;
        fits = std::min(fits, room);
    }
    if (fits <= 0) return 0;
    auto it = std::find_if(items.begin(), items.end(), [&](const ItemStack& s) { return s.item == item; });
    if (it != items.end()) it->count += fits;
    else items.push_back({item, fits});
    inventory.items = FormatItems(items);
    return fits;
}

int RemoveItem(Inventory& inventory, const std::string& item, int count) {
    auto items = ParseItems(inventory.items);
    auto it = std::find_if(items.begin(), items.end(), [&](const ItemStack& s) { return s.item == item; });
    if (it == items.end()) return 0;
    const int removed = std::min(count, it->count);
    it->count -= removed;
    inventory.items = FormatItems(items);
    return removed;
}

std::string Equip(Registry& registry, Entity entity, const std::string& item) {
    auto* inventory = registry.try_get<Inventory>(entity);
    if (!inventory || CountItem(*inventory, item) <= 0) return "you don't have it";
    const nlohmann::json* record = ItemRecord(item);
    const std::string slot = record ? record->value("slot", std::string("none")) : "none";
    auto& equipment = registry.get_or_emplace<Equipment>(entity);
    std::string* target = EquipmentSlot(equipment, slot);
    if (!target) return "it can't be worn or wielded";
    RemoveItem(*inventory, item, 1);
    if (!target->empty()) AddItem(*inventory, *target, 1); // what was there goes back
    *target = item;
    return "";
}

std::string Unequip(Registry& registry, Entity entity, const std::string& slot) {
    auto* equipment = registry.try_get<Equipment>(entity);
    std::string* target = equipment ? EquipmentSlot(*equipment, slot) : nullptr;
    if (!target || target->empty()) return "nothing is there";
    auto& inventory = registry.get_or_emplace<Inventory>(entity);
    if (AddItem(inventory, *target, 1) == 0) return "no room to carry it";
    target->clear();
    return "";
}

std::string UseItem(Registry& registry, Entity entity, const std::string& item) {
    auto* inventory = registry.try_get<Inventory>(entity);
    if (!inventory || CountItem(*inventory, item) <= 0) return "you don't have it";
    const nlohmann::json* record = ItemRecord(item);
    if (!record || !record->contains("use") || !(*record)["use"].is_object()) return "it can't be used";
    const nlohmann::json& use = (*record)["use"];
    // Magic (19.3): a book teaches an ability, a scroll casts one, a potion applies effects.
    if (use.contains("learn") || use.contains("ability") || use.contains("effects")) {
        MagicSystem* magic = MagicOf(registry);
        if (!magic) return "magic isn't available here";
        std::string why;
        if (!magic->UseItem(entity, use, &why)) return why.empty() ? "it can't be used" : why;
        if (use.value("consumed", true)) RemoveItem(*inventory, item, 1);
        return "";
    }
    const bool doesSomething = use.value("heal", 0.0f) > 0.0f || use.value("mana", 0.0f) > 0.0f ||
                               use.value("stamina", 0.0f) > 0.0f || use.value("effect", std::string("none")) != "none" ||
                               !use.value("attribute", std::string()).empty();
    if (!doesSomething) return "it can't be used";
    if (const float heal = use.value("heal", 0.0f); heal > 0.0f) Heal(registry, entity, heal);
    if (auto* resources = registry.try_get<Resources>(entity)) {
        resources->mana = std::min(resources->maxMana, resources->mana + use.value("mana", 0.0f));
        resources->stamina = std::min(resources->maxStamina, resources->stamina + use.value("stamina", 0.0f));
    }
    const std::string effect = use.value("effect", std::string("none"));
    static const std::vector<std::pair<std::string, EffectKind>> effects = {
        {"burn", EffectKind::Burn}, {"poison", EffectKind::Poison}, {"slow", EffectKind::Slow}, {"regen", EffectKind::Regen}};
    for (const auto& [name, kind] : effects) {
        if (name == effect) ApplyStatus(registry, entity, kind, use.value("duration", 5.0f), use.value("magnitude", 1.0f));
    }
    // A boost for a while: {"attribute": "attack", "value": 2, "duration": 30}
    if (!use.value("attribute", std::string()).empty()) {
        AddModifier(registry, entity, {use.value("attribute", std::string()), ModifierOp::Add, use.value("value", 0.0f),
                                       "item:" + item, use.value("duration", 30.0f)});
    }
    if (use.value("consumed", true)) RemoveItem(*inventory, item, 1);
    return "";
}

LootResult RollLoot(const std::string& table, int level, std::mt19937& rng) {
    LootResult result;
    std::vector<std::string> visiting;
    std::function<void(const std::string&, int)> roll = [&](const std::string& id, int depth) {
        const Record* record = ActiveRecords().Find("loot", id);
        if (!record || depth > 8 || std::find(visiting.begin(), visiting.end(), id) != visiting.end()) return;
        visiting.push_back(id);
        const nlohmann::json& loot = record->resolved;
        const int goldMin = loot.value("goldMin", 0), goldMax = std::max(goldMin, loot.value("goldMax", 0));
        if (goldMax > 0) result.gold += std::uniform_int_distribution<int>(goldMin, goldMax)(rng);
        std::vector<const nlohmann::json*> entries;
        float total = 0.0f;
        const nlohmann::json list = loot.value("entries", nlohmann::json::array()); // the pointers below point into it
        for (const auto& e : list) {
            const int lo = e.value("levelMin", 0), hi = e.value("levelMax", 0);
            if ((lo > 0 && level < lo) || (hi > 0 && level > hi)) continue;
            entries.push_back(&e);
            total += std::max(0.0f, e.value("weight", 1.0f));
        }
        const int rolls = std::max(0, loot.value("rolls", 1));
        for (int r = 0; r < rolls && total > 0.0f; ++r) {
            float pick = std::uniform_real_distribution<float>(0.0f, total)(rng);
            const nlohmann::json* chosen = entries.back();
            for (const auto* e : entries) {
                pick -= std::max(0.0f, e->value("weight", 1.0f));
                if (pick <= 0.0f) {
                    chosen = e;
                    break;
                }
            }
            if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) > chosen->value("chance", 1.0f)) continue;
            const int lo = std::max(1, chosen->value("min", 1)), hi = std::max(lo, chosen->value("max", lo));
            const int count = std::uniform_int_distribution<int>(lo, hi)(rng);
            if (!chosen->value("item", std::string()).empty()) {
                const std::string item = chosen->value("item", std::string());
                auto it = std::find_if(result.items.begin(), result.items.end(), [&](const ItemStack& s) { return s.item == item; });
                if (it != result.items.end()) it->count += count;
                else result.items.push_back({item, count});
            } else if (!chosen->value("table", std::string()).empty()) {
                for (int n = 0; n < count; ++n) roll(chosen->value("table", std::string()), depth + 1);
            }
        }
        visiting.pop_back();
    };
    roll(table, 0);
    return result;
}

int ItemPrice(const std::string& item, float rate) {
    const nlohmann::json* record = ItemRecord(item);
    const int value = record ? record->value("value", 0) : 0;
    return std::max(1, static_cast<int>(std::lround(value * rate)));
}

} // namespace aige
