#include "gameplay/EngineReflection.h"

#include "gameplay/Attributes.h"
#include "gameplay/Behaviour.h"
#include "gameplay/Components.h"
#include "gameplay/Cutscene.h"
#include "gameplay/Animation.h"
#include "gameplay/Difficulty.h"
#include "gameplay/Effects.h"
#include "gameplay/Magic.h"
#include "gameplay/Physics.h"
#include "gameplay/Items.h"
#include "gameplay/Settings.h"
#include "records/Records.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "scripting/Components.h"

#include <sstream>

namespace aige {

namespace {

// Script property values in scene text: `n:<number>`, `b:0|1`, `s:<text>`,
// so a scene loads without running the script to learn the types.
std::string EncodeScriptValue(const ScriptValue& value) {
    if (const auto* number = std::get_if<double>(&value)) {
        std::ostringstream out;
        out.precision(15);
        out << "n:" << *number;
        return out.str();
    }
    if (const auto* flag = std::get_if<bool>(&value)) {
        return *flag ? "b:1" : "b:0";
    }
    return "s:" + std::get<std::string>(value);
}

std::optional<ScriptValue> DecodeScriptValue(const std::string& text) {
    if (text.size() < 2 || text[1] != ':') {
        return std::nullopt;
    }
    std::string payload = text.substr(2);
    try {
        switch (text[0]) {
            case 'n': return ScriptValue(std::stod(payload));
            case 'b': return ScriptValue(payload == "1");
            case 's': return ScriptValue(payload);
            default: return std::nullopt;
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

const std::vector<std::string> kEffects = {"None", "Burn", "Poison", "Slow", "Regen"};
const std::vector<std::string> kDamageTypes = {"Physical", "Fire", "Frost", "Shock", "Poison"};

} // namespace

ProjectSettings& ActiveSettings() {
    static ProjectSettings settings;
    return settings;
}

void RegisterGameplayTypes(ReflectionRegistry& r) {
    // --- Identity & placement -------------------------------------------------

    r.Component<Name>("Name", "A designer-facing name, shown in the Scene Tree and usable from scripts.")
        .Field("value", &Name::value, "The entity's name.");

    r.Component<Tile>("Tile",
                      "A ground cell. Collision comes from tiles: a cell is walkable only if it holds a "
                      "non-solid tile, so empty space is a wall.")
        .Field("gx", &Tile::gx, "Grid column.")
        .Field("gy", &Tile::gy, "Grid row.")
        .Field("solid", &Tile::solid, "Blocks movement (walls, water, hedges).")
        .DependsOn({"ScreenTransform2D"})
        .Structural();

    r.Component<WorldTransform2D>("WorldTransform2D",
                                  "Position in world grid units (1.0 = one tile; cell centers are n + 0.5).")
        .Field("x", &WorldTransform2D::x, "Grid x; increases down-right on screen.")
        .Field("y", &WorldTransform2D::y, "Grid y; increases down-left on screen.")
        .DependsOn({"ScreenTransform2D"});

    r.Component<Velocity2D>("Velocity2D",
                            "Movement this frame, in cells per second. Velocity2D + Collider2D marks the player.")
        .Field("x", &Velocity2D::x, "Cells per second along grid x.")
        .Field("y", &Velocity2D::y, "Cells per second along grid y.")
        .DependsOn({"WorldTransform2D"});

    r.Component<Collider2D>("Collider2D", "Box used for wall collision, centered on the position, in cells.")
        .Field("halfWidth", &Collider2D::halfWidth, "Half the box width, in cells.").Range(0.05, 2.0)
        .Field("halfHeight", &Collider2D::halfHeight, "Half the box height, in cells.").Range(0.05, 2.0)
        .Field("shape", &Collider2D::shape, "A box or a circle.").Options({"Box", "Circle"})
        .Field("radius", &Collider2D::radius, "The circle's radius, in cells.").Range(0.05, 4.0)
        .Field("layer", &Collider2D::layer, "Its collision layer (empty: by team: player, enemy, ally; else default).")
        .Field("mask", &Collider2D::mask, "The layers it collides with: world, enemy... (empty: all)")
        .DependsOn({"WorldTransform2D"});

    // --- Combat ------------------------------------------------------------------

    r.Component<Faction>("Faction", "Which side an entity is on. Attacks never hurt members of the same "
                                    "non-neutral team.")
        .Field("team", &Faction::team, "Neutral, Player or Enemy.").Options({"Neutral", "Player", "Enemy"});

    r.Component<Health>("Health", "Hit points. At 0 the entity dies (enemies are removed; the player stays, dead).")
        .Field("current", &Health::current, "Current hit points; entities start the game with this, so keep it equal to max unless they should start hurt.").Range(0, 100000)
        .Field("max", &Health::max, "Maximum hit points; healing stops here.").Range(1, 100000).Scalable("health");

    r.Component<Stats>("Stats", "Combat numbers. Damage dealt = max(minimum, attack's damage + attack - "
                                "target's defense).")
        .Field("attack", &Stats::attack, "Added to every attack's damage.").Range(-1000, 1000).Scalable("damage")
        .Field("defense", &Stats::defense, "Subtracted from damage taken.").Range(-1000, 1000).Scalable("defense")
        .Field("moveSpeed", &Stats::moveSpeed, "Walking speed in cells per second (the player's).").Range(0, 50).Scalable("speed");

    r.Component<Hurtbox>("Hurtbox", "The area attacks can hit, centered on the position; brief invulnerability "
                                    "after each hit.")
        .Field("halfWidth", &Hurtbox::halfWidth, "Half the area's width, in cells.").Range(0.05, 3.0)
        .Field("halfHeight", &Hurtbox::halfHeight, "Half the area's height, in cells.").Range(0.05, 3.0)
        .Field("invulnerableTime", &Hurtbox::invulnerableTime, "Seconds of invulnerability after being hit.")
        .Range(0, 10)
        .DependsOn({"WorldTransform2D", "Health"});

    r.Component<MeleeAttack>("MeleeAttack",
                             "A close-range swing: a short-lived hit area `reach` cells in front of the attacker, "
                             "optionally applying a status effect on hit.")
        .Field("damage", &MeleeAttack::damage, "Damage per hit (before stats).").Range(0, 100000).Scalable("damage")
        .Field("reach", &MeleeAttack::reach, "How far in front the hit area appears, in cells.").Range(0, 10)
        .Field("radius", &MeleeAttack::radius, "Half-size of the hit area, in cells.").Range(0.05, 5)
        .Field("cooldown", &MeleeAttack::cooldown, "Seconds between swings.").Range(0, 60)
        .Field("knockback", &MeleeAttack::knockback, "How far a hit pushes the target, in cells.").Range(0, 10)
        .Field("effect", &MeleeAttack::effect, "Status effect applied on hit.").Options(kEffects)
        .Field("effectDuration", &MeleeAttack::effectDuration, "How long the effect lasts, in seconds.")
        .Range(0, 600)
        .Field("effectMagnitude", &MeleeAttack::effectMagnitude,
               "Burn/Poison: damage per tick. Regen: healing per tick. Slow: fraction of speed removed (0-1).")
        .Range(0, 100000)
        .Field("damageType", &MeleeAttack::damageType, "The kind of damage (resistances apply).").Options(kDamageTypes)
        .DependsOn({"WorldTransform2D"});

    // --- Character and gameplay systems (development_plan.md 13) --------------------
    r.Component<Attributes>("Attributes",
                            "A character's numbers (13.1): base values that equipment, perks and effects modify. "
                            "With it, Stats, Health.max and Resources are worked out from these each step.")
        .Field("maxHealth", &Attributes::maxHealth, "Maximum health.").Range(1, 1000000)
        .Field("maxMana", &Attributes::maxMana, "Maximum mana (0: none).").Range(0, 1000000)
        .Field("maxStamina", &Attributes::maxStamina, "Maximum stamina (0: none; blocking and dodging cost it).")
        .Range(0, 1000000)
        .Field("attack", &Attributes::attack, "Added to every hit's damage.").Range(-1000, 100000)
        .Field("defense", &Attributes::defense, "Taken from every hit's damage.").Range(-1000, 100000)
        .Field("moveSpeed", &Attributes::moveSpeed, "Walking speed, cells per second.").Range(0, 50)
        .Field("healthRegen", &Attributes::healthRegen, "Health per second.").Range(0, 1000)
        .Field("manaRegen", &Attributes::manaRegen, "Mana per second.").Range(0, 1000)
        .Field("staminaRegen", &Attributes::staminaRegen, "Stamina per second.").Range(0, 1000)
        .Field("resistFire", &Attributes::resistFire, "Share of fire damage taken away (0-0.9).").Range(-1, 0.9)
        .Field("resistFrost", &Attributes::resistFrost, "Share of frost damage taken away.").Range(-1, 0.9)
        .Field("resistShock", &Attributes::resistShock, "Share of shock damage taken away.").Range(-1, 0.9)
        .Field("resistPoison", &Attributes::resistPoison, "Share of poison damage taken away.").Range(-1, 0.9);
    r.Component<Resources>("Resources", "Mana and stamina now (13.1); their maxima come from Attributes.")
        .Field("mana", &Resources::mana, "Mana now.").Min(0)
        .Field("stamina", &Resources::stamina, "Stamina now.").Min(0)
        .Field("maxMana", &Resources::maxMana, "Maximum mana (worked out from Attributes).").Min(0)
        .Field("maxStamina", &Resources::maxStamina, "Maximum stamina (worked out from Attributes).").Min(0);
    r.Component<Progression>("Progression", "Experience, level and perks (13.2). The curve and each level's gains "
                                            "come from the progression record \"default\".")
        .Field("level", &Progression::level, "The level now.").Range(1, 1000)
        .Field("experience", &Progression::experience, "Experience towards the next level.").Min(0)
        .Field("perkPoints", &Progression::perkPoints, "Perks that can still be taken.").Min(0)
        .Field("perks", &Progression::perks, "Perks taken: perk record ids, comma-separated.");
    r.Component<Rewards>("Rewards", "What killing this gives (13.2, 13.4): experience to the killer, gold and loot "
                                    "dropped where it fell.")
        .Field("experience", &Rewards::experience, "Experience for the killer.").Min(0)
        .Field("goldMin", &Rewards::goldMin, "Gold dropped, at least.").Min(0)
        .Field("goldMax", &Rewards::goldMax, "Gold dropped, at most.").Min(0)
        .Field("loot", &Rewards::loot, "A loot record rolled for what's dropped.");
    r.Component<Inventory>("Inventory", "What a character carries (13.3): items as \"potion*3, iron-sword\" (item "
                                        "record ids), and gold.")
        .Field("items", &Inventory::items, "Item ids, with *count for more than one.")
        .Field("gold", &Inventory::gold, "Gold carried.").Min(0).Scalable("gold")
        .Field("slots", &Inventory::slots, "How many stacks fit (0: any number).").Min(0)
        .Field("maxWeight", &Inventory::maxWeight, "How much weight it carries (0: no limit).").Min(0);
    r.Component<Equipment>("Equipment", "What a character wears and wields (13.3): an item id per slot. The items' "
                                        "damage, armour and modifiers count towards its Attributes.")
        .Field("weapon", &Equipment::weapon, "A weapon item.")
        .Field("offhand", &Equipment::offhand, "A shield, a torch, a second weapon.")
        .Field("head", &Equipment::head, "A helmet.")
        .Field("body", &Equipment::body, "Armour.")
        .Field("hands", &Equipment::hands, "Gloves.")
        .Field("feet", &Equipment::feet, "Boots.")
        .Field("ring", &Equipment::ring, "A ring.")
        .Field("amulet", &Equipment::amulet, "An amulet.");
    r.Component<ItemPickup>("ItemPickup", "An item lying in the world (13.3): picked up by walking over it, or with "
                                          "the interact action when onUse. The item \"gold\" is gold.")
        .Field("item", &ItemPickup::item, "An item record id, or gold.")
        .Field("count", &ItemPickup::count, "How many.").Min(1).Scalable("loot")
        .Field("onUse", &ItemPickup::onUse, "Picked up with the interact action rather than by walking over it.")
        .DependsOn({"WorldTransform2D"});
    r.Component<Container>("Container", "A chest, a crate, a corpse (13.3): opened with the interact action; filled "
                                        "from its loot table the first time.")
        .Field("items", &Container::items, "What's inside, as item ids with *count.")
        .Field("gold", &Container::gold, "Gold inside.").Min(0).Scalable("gold")
        .Field("loot", &Container::loot, "A loot record rolled when it's first opened.")
        .Field("looted", &Container::looted, "Already opened and filled.")
        .DependsOn({"WorldTransform2D"});
    r.Component<Merchant>("Merchant", "Someone who trades (13.4): the interact action opens their shop. Stock comes "
                                      "from a loot table, filled again each day.")
        .Field("stock", &Merchant::stock, "A loot record their wares come from.")
        .Field("items", &Merchant::items, "What's for sale now.")
        .Field("gold", &Merchant::gold, "Gold they can pay for what they buy.").Min(0)
        .Field("buyRate", &Merchant::buyRate, "The player pays an item's value times this.").Range(0, 100).Scalable("price")
        .Field("sellRate", &Merchant::sellRate, "The player gets an item's value times this.").Range(0, 100).Scalable("loot")
        .DependsOn({"WorldTransform2D"});
    r.Component<Interactable>("Interactable", "Used with the interact action (13.5): a lever, a sign, a bed, a locked "
                                              "door or chest. Door, Container, Merchant and ItemPickup add what happens.")
        .Field("verb", &Interactable::verb, "The prompt's verb: Open, Read, Pull, Sleep.")
        .Field("name", &Interactable::name, "What it's called on the prompt (empty: its Name).")
        .Field("range", &Interactable::range, "How close the player must be, in cells.").Range(0.3, 10)
        .Field("locked", &Interactable::locked, "Locked until opened with its key.")
        .Field("key", &Interactable::key, "The item that unlocks it.")
        .Field("consumeKey", &Interactable::consumeKey, "The key is used up.")
        .Field("requiresFlag", &Interactable::requiresFlag, "A story flag that must be set first.")
        .Field("setFlag", &Interactable::setFlag, "A story flag set when it's used.")
        .Field("text", &Interactable::text, "Shown when it's used (a sign, a book).")
        .Field("event", &Interactable::event, "An event emitted when it's used, for scripts.")
        .Field("once", &Interactable::once, "Works only once.")
        .Field("used", &Interactable::used, "Already used (with once).")
        .DependsOn({"WorldTransform2D"});
    r.Component<Door>("Door", "A door (13.5): the interact action opens and closes it; open, it doesn't block.")
        .Field("open", &Door::open, "Open now.")
        .Field("openTexture", &Door::openTexture, "The picture when open (empty: unchanged).").As(FieldType::Texture)
        .Field("closedTexture", &Door::closedTexture, "The picture when closed.").As(FieldType::Texture)
        .DependsOn({"Interactable"});
    r.Component<RangedAttack>("RangedAttack", "Shoots projectiles (13.6): arrows, bolts, thrown knives.")
        .Field("damageType", &RangedAttack::damageType, "The kind of damage.").Options(kDamageTypes)
        .Field("damage", &RangedAttack::damage, "Damage per hit (before stats).").Range(0, 100000).Scalable("damage")
        .Field("range", &RangedAttack::range, "How far it flies, in cells.").Range(0.5, 100)
        .Field("speed", &RangedAttack::speed, "Cells per second.").Range(0.5, 100)
        .Field("cooldown", &RangedAttack::cooldown, "Seconds between shots.").Range(0, 60)
        .Field("projectile", &RangedAttack::projectile, "The projectile's picture (empty: a plain bolt).")
        .As(FieldType::Texture)
        .Field("pierce", &RangedAttack::pierce, "Flies on through what it hits.")
        .DependsOn({"WorldTransform2D"});
    r.Component<FactionMember>("FactionMember", "Belongs to a faction record (13.7). Below 0, its disposition makes "
                                                "it hostile to the player; attacking it lowers it.")
        .Field("faction", &FactionMember::faction, "A faction record id.")
        .Field("disposition", &FactionMember::disposition, "How it feels about the player.").Range(-100, 100);
    r.Component<Spawner>("Spawner", "Makes enemies from an encounter record around it (13.7), up to maxAlive, "
                                    "again after respawnSeconds.")
        .Field("encounter", &Spawner::encounter, "An encounter record id.")
        .Field("radius", &Spawner::radius, "How far from it they appear, in cells.").Range(0, 50)
        .Field("maxAlive", &Spawner::maxAlive, "How many at once.").Range(0, 100).Scalable("count")
        .Field("respawnSeconds", &Spawner::respawnSeconds, "Seconds until the next after one dies (0: never).")
        .Range(0, 100000)
        .Field("level", &Spawner::level, "The level the encounter is rolled at.").Range(1, 1000)
        .DependsOn({"WorldTransform2D"});

    // --- Character AI (development_plan.md 14, ai-packages.md) ------------------------
    r.Component<Behaviour>("Behaviour", "What a character does on its own (14): its packages (records/package), "
                                        "first that holds runs; how it fights and helps.")
        .Field("packages", &Behaviour::packages, "Package record ids, comma-separated; the first whose schedule and "
                                                 "conditions hold runs.")
        .Field("home", &Behaviour::home, "A marker: where @home is.")
        .Field("work", &Behaviour::work, "A marker: where @work is.")
        .Field("bed", &Behaviour::bed, "A marker: where @bed is (else the nearest free bed).")
        .Field("aggression", &Behaviour::aggression, "Peaceful: never fights. Defensive: fights when attacked. "
                                                     "Aggressive: attacks enemies it notices. Frenzied: attacks anyone.")
        .Options({"Peaceful", "Defensive", "Aggressive", "Frenzied"})
        .Field("confidence", &Behaviour::confidence, "How soon it runs when hurt.")
        .Options({"Cowardly", "Cautious", "Average", "Brave", "Foolhardy"})
        .Field("assistance", &Behaviour::assistance, "Who it helps in a fight.")
        .Options({"Nobody", "Allies", "Friends"})
        .Field("combatStyle", &Behaviour::combatStyle, "A combat-style record (empty: melee, runs at 20% health).")
        .Field("speed", &Behaviour::speed, "Walking speed, cells per second.").Range(0.1, 20)
        .DependsOn({"WorldTransform2D"});
    r.Component<Perception>("Perception", "What a character notices (14.5): sight in a cone, hearing of noises.")
        .Field("sight", &Perception::sight, "How far it sees, in cells.").Range(0, 50).Scalable("perception")
        .Field("angle", &Perception::angle, "The cone it sees in, in degrees.").Range(10, 360)
        .Field("hearing", &Perception::hearing, "How far it hears an ordinary noise, in cells.").Range(0, 50).Scalable("perception");
    r.Component<SmartObject>("SmartObject", "Something characters use (14.3): a bed, a chair, a counter, a forge. "
                                            "Packages find them by type and affordance.")
        .Field("type", &SmartObject::type, "bed, chair, table, counter, anvil, fireplace, well, bookshelf, altar...")
        .Field("affordances", &SmartObject::affordances, "What can be done with it: sleep, sit, eat, drink, work, "
                                                         "read, pray, warm (comma-separated).")
        .Field("slots", &SmartObject::slots, "How many can use it at once.").Range(1, 32)
        .Field("owner", &SmartObject::owner, "Only this character uses it (a name or World Bible key; empty: anyone).")
        .Field("minDuration", &SmartObject::minDuration, "A use lasts at least this many seconds.").Range(0, 100000)
        .Field("maxDuration", &SmartObject::maxDuration, "A use lasts at most this many seconds.").Range(0, 100000)
        .DependsOn({"WorldTransform2D"});
    r.Component<Barks>("Barks", "One-line remarks (14.5); alternatives separated by |.")
        .Field("alert", &Barks::alert, "When it notices the player as an enemy.")
        .Field("hit", &Barks::hit, "When it's hurt.")
        .Field("idle", &Barks::idle, "Now and then, near the player.")
        .Field("flee", &Barks::flee, "When it runs.");
    r.Component<Actor>("Actor", "The same character in every scene (14.4): its key, and the prefab it's made from "
                                "when it walks into a scene it wasn't placed in.")
        .Field("key", &Actor::key, "A World Bible key or a unique name.")
        .Field("prefab", &Actor::prefab, "Its prefab.").As(FieldType::Prefab).Optional();

    r.Component<Pickup>("Pickup", "Collected when the player walks over it (left alone at full health).")
        .Field("kind", &Pickup::kind, "What it does.").Options({"Heal"})
        .Field("amount", &Pickup::amount, "Hit points restored.").Range(0, 100000).Scalable("loot")
        .Field("radius", &Pickup::radius, "Pickup distance, in cells.").Range(0.05, 5)
        .DependsOn({"WorldTransform2D"});

    // --- World & AI ----------------------------------------------------------------

    r.Component<Obstacle>("Obstacle", "Blocks movement through the cell it stands in (trees, rocks, crates) and, "
                                      "for something bigger, the cells beside and behind it.")
        .Field("left", &Obstacle::left, "Cells blocked to the left of where it stands.").Range(0, 64)
        .Field("right", &Obstacle::right, "Cells blocked to the right.").Range(0, 64)
        .Field("back", &Obstacle::back, "Cells blocked behind it.").Range(0, 64)
        .DependsOn({"WorldTransform2D"});

    r.Component<Sounds>("Sounds", "Sound effects this entity plays.")
        .Field("attack", &Sounds::attack, "When it swings.").As(FieldType::Audio)
        .Field("hurt", &Sounds::hurt, "When it takes damage.").As(FieldType::Audio)
        .Field("death", &Sounds::death, "When it dies.").As(FieldType::Audio)
        .Field("collect", &Sounds::collect, "When it's picked up (pickups).").As(FieldType::Audio);

    r.Component<Follow>("Follow", "Walks toward a target (the player if none is set) while within range, "
                                  "stopping at stopDistance; collides with walls.")
        .Field("target", &Follow::target, "Who to follow; none = the player.")
        .Field("speed", &Follow::speed, "Cells per second.").Range(0, 50)
        .Field("stopDistance", &Follow::stopDistance, "Stops this close to the target, in cells.").Range(0, 50)
        .Field("range", &Follow::range, "Only follows within this distance; 0 = always.").Range(0, 1000)
        .DependsOn({"WorldTransform2D"});

    r.Component<Marker>("Marker", "A named place in the scene, for routines to send people to. Invisible in play.")
        .Field("name", &Marker::name, "The place's name, e.g. herb_garden.")
        .DependsOn({"WorldTransform2D"});
    r.Component<Gateway>("Gateway",
                         "A way into another scene: stepping into its cell during play takes the player there.")
        .Field("scene", &Gateway::scene, "The scene it leads to, e.g. scenes/cave.aige.")
        .Field("marker", &Gateway::marker, "The Marker in that scene the player arrives at.")
        .DependsOn({"WorldTransform2D"});
    r.Component<Routine>("Routine", "Follows a daily schedule: where to be, and what to do there, through the day.")
        .Field("schedule", &Routine::schedule, "The schedule, schedules/<id>.json.")
        .Field("speed", &Routine::speed, "Walking speed, cells per second.").Range(0.1, 20)
        .DependsOn({"WorldTransform2D"});

    r.Component<Talk>("Talk", "Someone the player can talk to: pressing E within range starts the dialogue.")
        .Field("dialogue", &Talk::dialogue, "The conversation, dialogue/<id>.json.")
        .Field("character", &Talk::character,
               "Who they are in the World Bible (e.g. character/villager): their name on screen, and "
               "what 'talk to' quest objectives point at.")
        .Field("range", &Talk::range, "How close the player must be, in cells.").Range(0.3, 10)
        .DependsOn({"WorldTransform2D"});
    // --- Animation and effects (18) ---------------------------------------------------
    r.Component<Animator>("Animator", "Animates the sprite (18.2): a sheet (assets/*.anim.json) and an animator "
                                      "(humanoid, quadruped, flyer, prop, or an animator record).")
        .Field("sheet", &Animator::sheet, "The sheet, assets/<name>.anim.json.")
        .Field("animator", &Animator::animator, "humanoid, quadruped, flyer, prop, or an animator record's id.")
        .Field("speed", &Animator::speed, "Playback rate.").Range(0, 10)
        .Field("corpseSeconds", &Animator::corpseSeconds, "A dead one stays this long, for its death clip.").Range(0, 30)
        .DependsOn({"Sprite"});
    r.Component<ParticleEmitter>("ParticleEmitter", "Emits particles (18.4): a particles record or preset "
                                                    "(fire, smoke, embers, fireflies...).")
        .Field("particles", &ParticleEmitter::particles, "The particles' id.")
        .Field("playing", &ParticleEmitter::playing, "Emitting now.")
        .Field("scale", &ParticleEmitter::scale, "Rate and size.").Range(0.05, 20)
        .DependsOn({"WorldTransform2D"});
    r.Component<EffectCues>("EffectCues", "Effects played when this is hit, dies or attacks (18.5).")
        .Field("onHit", &EffectCues::onHit, "An effect's id (hit, hit-fire...).")
        .Field("onDeath", &EffectCues::onDeath, "An effect's id (death...).")
        .Field("onAttack", &EffectCues::onAttack, "An effect's id.");
    r.Component<Light2D>("Light2D", "A light (18.6): lights what's around it when Lighting is on.")
        .Field("r", &Light2D::r, "Red (0-1).").Range(0, 1)
        .Field("g", &Light2D::g, "Green (0-1).").Range(0, 1)
        .Field("b", &Light2D::b, "Blue (0-1).").Range(0, 1)
        .Field("intensity", &Light2D::intensity, "How bright.").Range(0, 4)
        .Field("radius", &Light2D::radius, "How far, in cells.").Range(0.1, 50)
        .Field("flicker", &Light2D::flicker, "Flicker (0-1): a torch.").Range(0, 1)
        .Field("nightOnly", &Light2D::nightOnly, "On only in the dark (street lamps, windows).")
        .DependsOn({"WorldTransform2D"});
    r.Component<Weather>("Weather", "The scene's weather (18.6).")
        .Field("kind", &Weather::kind, "None, rain, snow, fog or sandstorm.").Options({"None", "Rain", "Snow", "Fog", "Sandstorm"})
        .Field("intensity", &Weather::intensity, "How strong (0-1).").Range(0, 1);
    r.Component<Abilities>("Abilities", "What it can cast or do (19): known abilities and its quick slots, "
                                        "each a list of ability ids (presets or records).")
        .Field("known", &Abilities::known, "Known abilities: firebolt, heal...")
        .Field("slots", &Abilities::slots, "The quick slots (1-4), in order.");
    // --- Physics v2 (20) -----------------------------------------------------------------
    r.Component<Trigger>("Trigger", "A sensor area (20.1): whoever enters or leaves it (on its mask's layers) raises "
                                    "trigger_enter / trigger_exit with its name.")
        .Field("name", &Trigger::name, "What scripts hear as the event's text.")
        .Field("mask", &Trigger::mask, "Which layers it notices: player, enemy, projectile... (empty: all)")
        .Field("once", &Trigger::once, "Only the first time.")
        .Field("radius", &Trigger::radius, "A circle of this radius (0: the entity's collider).").Range(0, 50)
        .DependsOn({"WorldTransform2D"});
    r.Component<CharacterController2D>("CharacterController2D", "How it moves (20.2): top-down walking, or grid-step "
                                                                "(cell by cell).")
        .Field("mode", &CharacterController2D::mode, "Top-down or grid-step.").Options({"TopDown", "GridStep"})
        .Field("stepTime", &CharacterController2D::stepTime, "Grid-step: seconds per cell.").Range(0.02, 2)
        .Field("diagonal", &CharacterController2D::diagonal, "Grid-step: diagonal steps allowed.");
    r.Component<RigidBody2D>("RigidBody2D", "A physical body (20.3): pushed, falling, toppling, bouncing (Box2D).")
        .Field("type", &RigidBody2D::type, "Static, kinematic (moved by code) or dynamic.").Options({"Static", "Kinematic", "Dynamic"})
        .Field("density", &RigidBody2D::density, "Mass per area.").Range(0.01, 100)
        .Field("friction", &RigidBody2D::friction, "0-1.").Range(0, 1)
        .Field("restitution", &RigidBody2D::restitution, "Bounce, 0-1.").Range(0, 1)
        .Field("linearDamping", &RigidBody2D::linearDamping, "Slowing down (a top-down floor's drag).").Range(0, 20)
        .Field("angularDamping", &RigidBody2D::angularDamping, "Slowing its spin.").Range(0, 20)
        .Field("gravityScale", &RigidBody2D::gravityScale, "Times Project Settings ▸ Physics gravity.").Range(-10, 10)
        .Field("fixedRotation", &RigidBody2D::fixedRotation, "Never turns.")
        .Field("bullet", &RigidBody2D::bullet, "Continuous collision, for fast bodies.")
        .DependsOn({"WorldTransform2D", "Collider2D"});
    r.Component<Joint2D>("Joint2D", "Joins this body to another (20.3): a hinge, a fixed distance (a rope), or welded.")
        .Field("type", &Joint2D::type, "Revolute (a hinge), distance or weld.").Options({"Revolute", "Distance", "Weld"})
        .Field("other", &Joint2D::other, "The other body.")
        .Field("anchorX", &Joint2D::anchorX, "Where on this body, cells right of its centre.").Range(-10, 10)
        .Field("anchorY", &Joint2D::anchorY, "Where on this body, cells below its centre.").Range(-10, 10)
        .Field("length", &Joint2D::length, "Distance: the length (0: as placed).").Range(0, 100)
        .Field("collide", &Joint2D::collide, "The two still collide with each other.")
        .DependsOn({"RigidBody2D"});
    r.Component<Zone>("Zone", "The scene's level band (16.3): enemies here are made at a level within it.")
        .Field("minLevel", &Zone::minLevel, "The lowest level.").Range(1, 100)
        .Field("maxLevel", &Zone::maxLevel, "The highest level.").Range(1, 100);
    r.Component<DifficultyOverrides>("DifficultyOverrides",
                                     "Values set by hand for some difficulty levels (16.2), as JSON: "
                                     "{\"Health.max\": {\"hard\": 20}}. The Inspector's 'by difficulty' edits it.")
        .Field("values", &DifficultyOverrides::values, "Per field, per level.");
    r.Component<CutsceneTrigger>("CutsceneTrigger", "Plays a cutscene when the player comes near (15.5).")
        .Field("cutscene", &CutsceneTrigger::cutscene, "The cutscene, cutscenes/<id>.json (its id).")
        .Field("radius", &CutsceneTrigger::radius, "How close the player must come, in cells.").Range(0.2, 50)
        .Field("once", &CutsceneTrigger::once, "Only the first time (remembered in saves).")
        .DependsOn({"WorldTransform2D"});
    r.Component<Music>("Music", "The scene's music and ambience (development_plan.md 12.6): while the player is in "
                                "this scene, the first entity with Music sets what plays. Changing scene "
                                "cross-fades to the next scene's.")
        .Field("track", &Music::track, "A looping music track (WAV, MP3 or FLAC). Empty: silence.").As(FieldType::Audio)
        .Field("ambience", &Music::ambience, "A looping ambience under the music: wind, a crowd, the sea.")
        .As(FieldType::Audio)
        .Field("combat", &Music::combat, "Music while an enemy is chasing the player. Empty: the game's (game.json).")
        .As(FieldType::Audio);

    // --- Rendering -------------------------------------------------------------------

    r.Component<ScreenTransform2D>("ScreenTransform2D",
                                   "Screen position in scene pixels and draw order. Computed from the world "
                                   "position for characters and tiles.")
        .Field("x", &ScreenTransform2D::x, "Pixels from the left.")
        .Field("y", &ScreenTransform2D::y, "Pixels from the top.")
        .Field("depth", &ScreenTransform2D::depth, "Draw order: higher draws in front.")
        .DerivedWhen([](Registry& registry, Entity entity) {
            return registry.any_of<WorldTransform2D, Tile>(entity);
        });

    r.Component<Sprite>("Sprite", "The image drawn for this entity: a texture (or a region of one, like a "
                                  "tileset tile), its size on screen and a color tint.")
        .Field("texture", &Sprite::texture, "Texture asset; empty = built-in placeholder.").As(FieldType::Texture)
        .Field("width", &Sprite::width, "On-screen width in pixels.").Range(1, 4096)
        .Field("height", &Sprite::height, "On-screen height in pixels.").Range(1, 4096)
        .Field("u0", &Sprite::u0, "Texture region left (0-1); greater than u1 mirrors it.").Range(0, 1)
        .Field("v0", &Sprite::v0, "Texture region top (0-1).").Range(0, 1)
        .Field("u1", &Sprite::u1, "Texture region right (0-1).").Range(0, 1)
        .Field("v1", &Sprite::v1, "Texture region bottom (0-1).").Range(0, 1)
        .Field("r", &Sprite::r, "Red tint (0-1).").Range(0, 1)
        .Field("g", &Sprite::g, "Green tint (0-1).").Range(0, 1)
        .Field("b", &Sprite::b, "Blue tint (0-1).").Range(0, 1)
        .Field("a", &Sprite::a, "Opacity (0-1).").Range(0, 1)
        .Field("flat", &Sprite::flat,
               "Lies flat on the ground: for art drawn top-down. The image is mapped onto the cell's diamond "
               "(width x height) instead of standing upright.")
        .Field("rotation", &Sprite::rotation, "Quarter turns clockwise of a flat image (0-3).").Range(0, 3)
        .Field("lift", &Sprite::lift, "Raised off the ground by this many pixels (standing on high ground).").Range(-512, 512)
        .Field("emissive", &Sprite::emissive, "Lit by itself (a lit window, glowing runes): darkness doesn't dim it.")
        .Field("angle", &Sprite::angle, "Turned around its position, in degrees (a rigid body sets it).").Range(-3600, 3600)
        .DependsOn({"ScreenTransform2D"});

    // --- Scripting -------------------------------------------------------------------

    r.Component<LuaScript>("LuaScript", "Lua behavior (see docs/systems/scripting.md). The script's `properties` "
                                        "table becomes per-entity settings.")
        .Field("path", &LuaScript::path, "The .lua file, relative to the project.").As(FieldType::Script)
        .OnSet([](Registry& registry, Entity entity, const FieldValue& value) {
            auto& script = registry.get<LuaScript>(entity);
            script.path = ToString(value);
            script.Unload(); // the new file loads on the next update
        });
    ComponentInfo& script = r.MutableComponent("LuaScript");
    script.writeExtra = [](Registry& registry, Entity entity) {
        std::vector<std::pair<std::string, std::string>> extra;
        for (const auto& [name, value] : registry.get<LuaScript>(entity).properties) {
            extra.emplace_back("prop." + name, QuoteValue(EncodeScriptValue(value)));
        }
        return extra;
    };
    script.readExtra = [](Registry& registry, Entity entity, const std::string& key, const std::string& text) {
        if (key.rfind("prop.", 0) != 0) {
            return false;
        }
        if (auto value = DecodeScriptValue(text)) {
            registry.get<LuaScript>(entity).properties[key.substr(5)] = *value;
        }
        return true;
    };

    // Which prefab an entity was placed from and which fields this copy
    // overrides (comma-separated `Component.field`, `-Component` for removed
    // components). Shown by the Inspector's prefab header, not as a section.
    r.Component<PrefabInstance>("PrefabInstance", "Placed from a prefab.")
        .Field("path", &PrefabInstance::path, "The prefab file.")
        .Field("overrides", &PrefabInstance::overrides, "Fields changed on this copy.")
        .Hidden();

    r.Component<PlaceNode>("PlaceNode", "Made by a place build: the build, its plan node and its layout statement.")
        .Field("place", &PlaceNode::place, "The build that made it.")
        .Field("node", &PlaceNode::node, "The plan (Spec) node it belongs to.")
        .Field("statement", &PlaceNode::statement, "The layout (PLL) statement that placed it.");

    // --- Project settings -------------------------------------------------------------

    ProjectSettings& settings = ActiveSettings();
    r.Settings("Renderer", "How the game is drawn.", &settings.renderer)
        .Field("clearR", &RendererSettings::clearR, "Background red (0-1).").Range(0, 1)
        .Field("clearG", &RendererSettings::clearG, "Background green (0-1).").Range(0, 1)
        .Field("clearB", &RendererSettings::clearB, "Background blue (0-1).").Range(0, 1);
    r.Settings("Camera", "The play camera.", &settings.camera)
        .Field("followStiffness", &CameraSettings::followStiffness,
               "How quickly the camera catches up with the player; higher is snappier.")
        .Range(0.5, 50)
        .Field("playZoom", &CameraSettings::playZoom, "Zoom while playing.").Range(0.25, 4)
        .Field("deadZone", &CameraSettings::deadZone,
               "How far from the centre (a share of the screen) the player walks before the camera follows.")
        .Range(0, 0.45)
        .Field("lookAhead", &CameraSettings::lookAhead, "Cells the camera leads the player by, the way they move.")
        .Range(0, 6)
        .Field("clampToLevel", &CameraSettings::clampToLevel, "Never show beyond the edges of the level.");
    r.Settings("Combat", "Rules shared by every attack.", &settings.combat)
        .Field("minimumDamage", &CombatSettings::minimumDamage, "A hit never deals less than this.").Range(0, 1000)
        .Field("hitFlashSeconds", &CombatSettings::hitFlashSeconds, "How long a hit sprite flashes red.")
        .Range(0, 2)
        .Field("knockbackScale", &CombatSettings::knockbackScale, "Multiplies every attack's knockback.")
        .Range(0, 10)
        .Field("blockReduction", &CombatSettings::blockReduction, "The share of a blocked hit that's stopped.")
        .Range(0, 1)
        .Field("dodgeDistance", &CombatSettings::dodgeDistance, "Cells a dodge covers.").Range(0, 10)
        .Field("dodgeStamina", &CombatSettings::dodgeStamina, "Stamina a dodge costs (for those with stamina).")
        .Range(0, 1000);
    r.Settings("Streaming", "Big scenes: only the chunks around the player are simulated (24).", &settings.streaming)
        .Field("enabled", &StreamingSettings::enabled, "On: characters, scripts, routines and spawners far away sleep.")
        .Field("chunk", &StreamingSettings::chunk, "A chunk's side, in cells.").Range(8, 256)
        .Field("radius", &StreamingSettings::radius, "Chunks around the player's that stay awake.").Range(1, 8);
    r.Settings("Physics", "Movement and collision.", &settings.physics)
        .Field("playerCollides", &PhysicsSettings::playerCollides, "Off lets the player walk through walls.")
        .Field("gravityX", &PhysicsSettings::gravityX, "Rigid bodies' gravity, cells per second squared.").Range(-100, 100)
        .Field("gravityY", &PhysicsSettings::gravityY, "(top-down games: 0; a side-on puzzle: about 10).").Range(-100, 100);
    r.Settings("Grid", "How the world is laid out: diamond cells seen at an angle, or square cells seen from above.",
               &settings.grid)
        .Field("style", &GridSettings::style,
               "Isometric (diamond cells) or TopDown (square cells, for art drawn from above). Changing it re-lays "
               "the whole scene.")
        .Options({"Isometric", "TopDown"});
    r.Settings("AI", "This project's AI: each left empty uses the app's choice (Preferences ▸ AI).", &settings.ai)
        .Field("provider", &AiSettings::provider, "The language model's provider (empty: the app's).")
        .Options({"", "mistral", "claude"})
        .Field("model", &AiSettings::model, "Its model (empty: the app's, or the provider's default).")
        .Field("art", &AiSettings::art, "Who draws the prefabs' art (empty: the app's).")
        .Options({"", "mistral", "pixellab", "bfl:flux-2-klein-4b", "bfl:flux-3-image", "bfl:flux-2-pro"})
        .Field("background", &AiSettings::background, "Who paints the places' backgrounds (empty: the app's).")
        .Options({"", "bfl:flux-3-image"});
    r.Settings("Game", "The game as a whole.", &settings.game)
        .Field("startScene", &GameSettings::startScene, "The scene the game starts in (scenes/*.aige).")
        .Field("version", &GameSettings::version, "The game's version: the window's title, the crash log, exports.")
        .Field("debugConsole", &GameSettings::debugConsole, "` opens the debug console in play (exports leave it off unless asked).")
        .Field("splash", &GameSettings::splash, "A picture shown for a moment when the game starts (assets/...).");
    r.Settings("Time", "The game clock (routines follow it).", &settings.time)
        .Field("startHour", &TimeSettings::startHour, "Time of day when a game starts, in hours.").Range(0, 23.99)
        .Field("minutesPerSecond", &TimeSettings::minutesPerSecond,
               "Game minutes per real second; 2 makes a day last 12 minutes.").Range(0.1, 600)
        .Field("showClock", &TimeSettings::showClock, "Always show the clock in play.");
    r.Settings("Difficulty", "Difficulty levels and how enemies' levels are chosen (difficulty.md).",
               &settings.difficulty)
        .Field("defaultLevel", &DifficultySettings::defaultLevel, "The level a new game starts at (a difficulty record).")
        .Field("changeAnytime", &DifficultySettings::changeAnytime, "The player can change the level during play.")
        .Field("adaptive", &DifficultySettings::adaptive,
               "Offer to adjust the enemies by one step to how the player does.")
        .Field("scaling", &DifficultySettings::scaling, "How enemies' levels are chosen.")
        .Options({"None", "FixedZones", "Scaled", "Hybrid"})
        .Field("levelOffset", &DifficultySettings::levelOffset, "Scaled and Hybrid: the player's level plus this.")
        .Range(-20, 20);
    r.Settings("Lighting", "2D lighting, day and night (effects-animation.md §7).", &settings.lighting)
        .Field("enabled", &LightingSettings::enabled, "Light the scene: ambient light plus Light2D lights.")
        .Field("dayNight", &LightingSettings::dayNight, "The ambient light follows the game clock.")
        .Field("dayAmbient", &LightingSettings::dayAmbient, "The light by day (#rrggbb).")
        .Field("nightAmbient", &LightingSettings::nightAmbient, "The light at night (#rrggbb).")
        .Field("dawnHour", &LightingSettings::dawnHour, "When day comes.").Range(0, 23.99)
        .Field("duskHour", &LightingSettings::duskHour, "When night falls.").Range(0, 23.99)
        .Field("vignette", &LightingSettings::vignette, "Darken the screen's edges.");
    r.Settings("UI", "The game's own UI (game-ui.md).", &settings.ui)
        .Field("documents", &UiSettings::documents,
               "Menus, windows and the HUD as themed documents (ui/); off: the built-in overlays.");
    r.Settings("Story", "Quest help on screen.", &settings.story)
        .Field("objectiveMarkers", &StorySettings::objectiveMarkers, "Point the way to the current objective.")
        .Field("journal", &StorySettings::journal, "The quest journal window (J).");

    RegisterEngineRecordTypes(r);
}

ReflectionRegistry& EngineReflection() {
    static ReflectionRegistry registry = [] {
        ReflectionRegistry built;
        RegisterGameplayTypes(built);
        return built;
    }();
    return registry;
}

} // namespace aige
