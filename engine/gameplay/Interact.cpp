#include "gameplay/Interact.h"

#include "gameplay/Components.h"
#include "gameplay/Items.h"
#include "gameplay/Story.h"
#include "physics2d/Components.h"
#include "records/Records.h"
#include "renderer2d/Components.h"

#include <cmath>
#include <vector>

namespace aige {

std::string ItemName(const std::string& item) {
    if (item == "gold") return "gold";
    if (const Record* record = ActiveRecords().Find("item", item)) {
        const std::string name = record->resolved.value("name", std::string());
        if (!name.empty()) return name;
    }
    return item;
}

namespace {

float RangeOf(Registry& registry, Entity entity) {
    if (const auto* i = registry.try_get<Interactable>(entity)) return i->range;
    return 1.3f;
}

bool Usable(Registry& registry, Entity entity) {
    if (const auto* i = registry.try_get<Interactable>(entity); i && i->once && i->used) return false;
    if (const auto* c = registry.try_get<Container>(entity)) {
        // An emptied, looted container has nothing left to offer.
        if (c->looted && c->items.empty() && c->gold == 0 && !registry.all_of<Interactable>(entity)) return false;
    }
    if (const auto* p = registry.try_get<ItemPickup>(entity)) return p->onUse || registry.all_of<Interactable>(entity);
    return registry.all_of<Interactable>(entity) || registry.all_of<Container>(entity) || registry.all_of<Merchant>(entity);
}

std::string NameOf(Registry& registry, Entity entity) {
    if (const auto* i = registry.try_get<Interactable>(entity); i && !i->name.empty()) return i->name;
    if (const auto* n = registry.try_get<Name>(entity); n && !n->value.empty()) return n->value;
    if (const auto* p = registry.try_get<ItemPickup>(entity)) return ItemName(p->item);
    if (registry.all_of<Merchant>(entity)) return "merchant";
    if (registry.all_of<Container>(entity)) return "chest";
    return "it";
}

std::string Join(const std::vector<std::string>& parts) {
    std::string out;
    for (const auto& p : parts) out += (out.empty() ? "" : ", ") + p;
    return out;
}

// Moves a pile of items and gold into the player's inventory; what was taken.
std::vector<std::string> Take(Registry& registry, Entity player, std::vector<ItemStack> items, int gold,
                              std::vector<ItemStack>* leftOver) {
    auto& inventory = registry.get_or_emplace<Inventory>(player);
    std::vector<std::string> took;
    if (gold > 0) {
        inventory.gold += gold;
        took.push_back(std::to_string(gold) + " gold");
    }
    for (const auto& stack : items) {
        if (stack.item == "gold") {
            inventory.gold += stack.count;
            took.push_back(std::to_string(stack.count) + " gold");
            continue;
        }
        const int fitted = AddItem(inventory, stack.item, stack.count);
        if (fitted > 0) took.push_back(ItemName(stack.item) + (fitted > 1 ? " x" + std::to_string(fitted) : ""));
        if (fitted < stack.count && leftOver) leftOver->push_back({stack.item, stack.count - fitted});
    }
    return took;
}

} // namespace

std::optional<Entity> FindInteractable(Registry& registry, Entity player) {
    const auto* at = registry.try_get<WorldTransform2D>(player);
    if (!at) return std::nullopt;
    std::optional<Entity> best;
    float bestDistance = 1e9f;
    for (auto [entity, world] : registry.view<WorldTransform2D>().each()) {
        if (entity == player || !Usable(registry, entity)) continue;
        const float dx = world.x - at->x, dy = world.y - at->y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (distance <= RangeOf(registry, entity) && distance < bestDistance) {
            best = entity;
            bestDistance = distance;
        }
    }
    return best;
}

std::string InteractPrompt(Registry& registry, Entity target) {
    std::string verb = "Use";
    if (const auto* i = registry.try_get<Interactable>(target)) {
        verb = i->verb;
        if (const auto* door = registry.try_get<Door>(target)) verb = door->open ? "Close" : "Open";
    } else if (registry.all_of<Merchant>(target)) {
        verb = "Trade with";
    } else if (registry.all_of<Container>(target)) {
        verb = "Open";
    } else if (registry.all_of<ItemPickup>(target)) {
        verb = "Take";
    }
    return verb + " " + NameOf(registry, target);
}

InteractResult Interact(Registry& registry, Entity player, Entity target, StoryState& story, int level,
                        std::mt19937& rng) {
    InteractResult result;
    if (!registry.valid(target)) return result;
    auto* interactable = registry.try_get<Interactable>(target);
    if (interactable) {
        if (interactable->once && interactable->used) return result;
        if (!interactable->requiresFlag.empty() && !story.Flag(interactable->requiresFlag)) {
            result.message = "It won't move.";
            return result;
        }
        if (interactable->locked) {
            auto* inventory = registry.try_get<Inventory>(player);
            if (interactable->key.empty() || !inventory || CountItem(*inventory, interactable->key) <= 0) {
                result.message = interactable->key.empty() ? "Locked." : "Locked: needs " + ItemName(interactable->key) + ".";
                return result;
            }
            if (interactable->consumeKey) RemoveItem(*inventory, interactable->key, 1);
            interactable->locked = false;
            result.message = "Unlocked with " + ItemName(interactable->key) + ".";
        }
    }
    result.used = true;
    if (auto* door = registry.try_get<Door>(target)) {
        door->open = !door->open;
        if (auto* sprite = registry.try_get<Sprite>(target)) {
            const std::string& texture = door->open ? door->openTexture : door->closedTexture;
            if (!texture.empty()) sprite->texture = texture;
        }
    }
    if (auto* container = registry.try_get<Container>(target)) {
        if (!container->looted && !container->loot.empty()) {
            const LootResult loot = RollLoot(container->loot, level, rng);
            auto items = ParseItems(container->items);
            items.insert(items.end(), loot.items.begin(), loot.items.end());
            container->items = FormatItems(items);
            container->gold += loot.gold;
        }
        container->looted = true;
        std::vector<ItemStack> left;
        const auto took = Take(registry, player, ParseItems(container->items), container->gold, &left);
        container->items = FormatItems(left);
        container->gold = 0;
        const std::string line = took.empty() ? "It's empty." : "Took " + Join(took) + ".";
        result.message = result.message.empty() ? line : result.message + " " + line;
    }
    if (auto* pickup = registry.try_get<ItemPickup>(target)) {
        const auto took = Take(registry, player, {{pickup->item, pickup->count}}, 0, nullptr);
        if (!took.empty()) result.message = "Took " + Join(took) + ".";
        registry.destroy(target);
        return result;
    }
    if (registry.all_of<Merchant>(target)) result.openShop = true;
    if (interactable) {
        if (!interactable->setFlag.empty()) story.SetFlag(interactable->setFlag, true);
        result.text = interactable->text;
        result.event = interactable->event;
        if (interactable->once) interactable->used = true;
    }
    return result;
}

std::vector<std::string> CollectItemPickups(Registry& registry, Entity player) {
    std::vector<std::string> notices;
    const auto* at = registry.try_get<WorldTransform2D>(player);
    if (!at) return notices;
    std::vector<Entity> taken;
    for (auto [entity, pickup, world] : registry.view<ItemPickup, WorldTransform2D>().each()) {
        if (pickup.onUse || registry.all_of<Interactable>(entity)) continue;
        if (std::abs(world.x - at->x) > 0.6f || std::abs(world.y - at->y) > 0.6f) continue;
        const auto took = Take(registry, player, {{pickup.item, pickup.count}}, 0, nullptr);
        if (took.empty()) continue; // no room
        notices.push_back(Join(took));
        taken.push_back(entity);
    }
    for (auto entity : taken) registry.destroy(entity);
    return notices;
}

} // namespace aige
