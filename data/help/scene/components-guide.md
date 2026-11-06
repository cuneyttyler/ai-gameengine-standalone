# A guide to the most used components

What the most common components do and when to use them: position and picture (WorldTransform2D, Sprite, Facing),
blocking and collision (Obstacle, Collider2D, Tile), animation (Animator, AnimatedPart, Attachment), characters
(Behaviour, Talk, Health, Faction, FactionMember), interaction (Action, Interactable, Door, Container, Merchant,
ItemPickup), places (Interior, Marker, Path, Gateway, Ramp) and scene-wide ones (Music, Weather, Light2D). The full
list of every component's fields is generated from the engine; this page explains how they're used together.

## Position and picture: WorldTransform2D, Sprite, Facing

- **WorldTransform2D**: where the thing stands, in grid units (1.0 = one tile; cell centres are n + 0.5). x grows
  down-right on screen, y down-left. Dragging or nudging a thing changes it.
- **ScreenTransform2D**: its position on screen in pixels and its draw order (depth). For anything with a world
  position it's derived (computed every frame) and read-only.
- **Sprite**: the image drawn: **texture** (empty: the built-in placeholder), **width/height** on screen in pixels,
  the texture region **u0, v0, u1, v1** (0–1; u0 greater than u1 mirrors it), a tint **r, g, b**, opacity **a**,
  **ground** (lies on the ground as drawn: a field, a rug, a path piece), **flat** (top-down art mapped onto the
  cell's diamond), **rotation** (quarter turns of a flat image, 0–3), **order** (a flat thing's place among ground
  pieces, higher on top; Ctrl+] / Ctrl+[), **lift** (raised off the ground, in pixels), **emissive** (lit by itself,
  not dimmed by darkness), **angle**.
- **Facing**: which way it faces; its animation shows that side. The Inspector adds **turn to** (south, south-east,
  east…); R turns it an eighth.
- **Name**: the name shown in the Scene Tree and usable from scripts.

See also: ground-and-tiles.md (ground and flat art), painted-places.md (draw order).

Questions: how do I change an object's picture? how do I tint or fade a sprite? how do I mirror a sprite? what is lift?
how do I make a character face east?

## Blocking and collision: Obstacle, Collider2D, Tile

- **Obstacle**: blocks movement through the cell it stands in (trees, rocks, crates) and, for something bigger, the
  cells beside and behind it: **left**, **right** and **back** count cells blocked in each direction. A building's
  Obstacle is its **footprint**: the cells it covers. A building drawn on several cells is drawn in front of or behind
  a character by the footprint point nearest them. When a scene opens, buildings' footprints are fitted to what their
  art covers. "Solid" when making a prop prefab adds an Obstacle.
- **Collider2D**: the box (or circle) a moving thing collides with walls by: **halfWidth/halfHeight** or **radius** in
  cells, **shape**, **layer** and **mask** (which collision layers it hits). Velocity2D + Collider2D marks the player.
- **Tile**: makes an entity a ground cell (**gx, gy**, **solid**). A cell is walkable only if it holds a non-solid
  tile, so empty space is a wall. Structural: you get it by painting a tile prefab, not by adding it.
- **Trigger**: a sensor area; whoever enters or leaves it raises trigger_enter / trigger_exit with its name.
- **RigidBody2D** and **Joint2D**: physical bodies (pushed, falling, bouncing) and joints between them.
- **View ▸ Physics shapes (colliders, triggers, bodies)** draws them over the scene.

Questions: how do I make a tree block the player? how do I make a building's footprint bigger? why can my player walk
through a house? what is the difference between Obstacle and Collider2D?

## Characters: Behaviour, Talk, Health, Faction and friends

- **Behaviour**: what a character does on its own: its **packages** (records/package, in order; the first whose
  schedule and conditions hold runs), **home**, **work**, **bed** (markers for @home, @work, @bed), **aggression**
  (Peaceful, Defensive, Aggressive, Frenzied), **confidence**, **assistance**, **combatStyle**, **speed**.
- **Talk**: someone the player can talk to: pressing E within **range** starts the **dialogue**; **character** is who
  they are in the World Bible (their name on screen, and what "talk to" objectives point at).
- **Health**: hit points (**current**, **max**). At 0 the entity dies (enemies are removed; the player stays, dead).
- **Faction** (shown as **Team**): Neutral, Player or Enemy. Attacks never hurt members of the same non-neutral team.
- **FactionMember**: belongs to a faction record; below 0 its **disposition** makes it hostile to the player.
- **Stats**, **Attributes**, **MeleeAttack**, **RangedAttack**, **Hurtbox**: combat numbers.
- **Inventory**, **Equipment**: what it carries and wears; **Rewards**: what killing it gives.
- **Perception**: sight cone and hearing. **Barks** (shown as **Speech**): lines said by itself (alert, hit, idle,
  flee; alternatives separated by |). **Routine**: follows a daily schedule. **Actor**: the same character in every
  scene. **Follow**: walks toward a target. **Responses** (shown as **Reaction: Character Rules**): what it does when
  an event happens.

Questions: how do I make an NPC talk? how do I give a character a routine? how do I make an enemy? how do I make an NPC
hostile? how does a character know where home is?

## Animation: Animator, AnimatedPart, Attachment

- **Animator**: animates the sprite from a **sheet** (assets/…/<name>.anim.json) with an **animator** (humanoid,
  quadruped, flyer, prop, or an animator record), **speed**, **corpseSeconds**, **clips** (which clips a state plays,
  by hand: `attack=attack_punch|attack_kick`), **pick** (among several clips: weapon, random, order, first),
  **attackClass**. Needs a Sprite. Assets ▸ Animations ▸ right-click a sheet ▸ **Add to selected** gives the selected
  thing an Animator with that sheet.
- **AnimatedPart** (shown as **Reaction: Moving Part**): a part of its picture that moves on its own: a door, a window,
  a sign. It's drawn over the box it came from, plays its clip when activated and its close clip after. Made by Build ▸
  Animate or right-click ▸ **Animate a part…**. The Inspector shows **activation** (as the project's doors, on use, by
  itself as someone comes near, an input action, a game event, always playing, always open), **range**, **floor**,
  **locked**/**key**, **door**, **Only if**, **When activated**, **When it stops**, and **Picture and clips**. A thing
  with several parts has tabs (Part 1, Door 2…); **Edit parts…** / **Edit doors…** opens the building editor.
- **Attachment**: sits on another entity's point (a sword in a hand, a hat on a head): **to** (the other's Name),
  **point**, offsets, angle, pivot, size, **layer** (auto, front, behind, own), **turn**.
- **ParticleEmitter**, **EffectCues**, **Light2D**: particles, effects on hit/death/attack, lights.

See also: buildings-and-interiors.md.

Questions: how do I animate a sprite? how do I make a door open? how do I attach a sword to a hand? what does activation
mean?

## Interaction: Action, Interactable, Door, Container, Merchant, ItemPickup

- **Action** (shown as **Reaction**): does something when its **trigger** happens: **input** (its action pressed within
  range), **event**, **timer**, **touch** (walked onto), **near** (someone comes within range). It can require items
  (**requires**), take them (**takes**), give them (**gives**), need one in hand (**accepts**), play clips on the user
  and on itself, play effects, turn into another prefab (**becomes**), emit an event, set a flag, show text, run
  effects (**do**, **end**, **interrupted**), last a **duration**, have a **cooldown**, work **once**, or be a crafting
  **station**. A field farmed, a crop harvested, a well drawn from.
- **Interactable** (shown as **Reaction: Door, Chest, Sign**): used with the interact action: a lever, a sign, a bed, a
  locked door or chest. **verb** (Open, Read, Pull), **locked**/**key**, flags, **text**, **if**/**do**, **once**,
  **becomes** (an open chest), **animation**.
- **Door**: the interact action opens and closes it; open, it doesn't block (needs Interactable).
- **Container**: a chest, crate or corpse opened with interact, filled from its **loot** table the first time.
- **Merchant**: the interact action opens their shop; stock from a loot table, refilled each day; buy and sell rates.
- **ItemPickup**: an item lying in the world, picked up by walking over it (or with interact when **onUse**). The item
  "gold" is gold. **Pickup**: a healing pickup.
- **Label**: text always over it ("Shop open").

Questions: how do I make a chest? how do I make a shop? how do I make an item the player can pick up? how do I make
something happen when the player presses a key near it?

## Places: Interior, Marker, Path, Gateway, Ramp, Spawner

- **Interior** (shown as **Building Plan**): a building's inside: **kind** cutaway (drawn cut away in place) or scene
  (its own scene through its door), floors' pictures, stairs, ways in, solid things inside. See
  buildings-and-interiors.md.
- **Marker**: a named place for gateways, packages and schedules. Invisible in play.
- **Path**: a route characters follow, with points and forking links; drawn with the Path tool. See
  markers-and-paths.md.
- **Gateway**: stepping into its cell takes the player to **scene**, at **marker**. See scenes.md.
- **Ramp**: characters walk between heights here (stairs, a ramp, a ladder); elsewhere a cliff can't be walked up or
  down. Right-click the ground ▸ **Ramp here**.
- **Spawner**: makes enemies from an encounter record around it, up to maxAlive, again after respawnSeconds.
- **SmartObject**: something characters use (a bed, a chair, a forge), found by type and affordance.
- **CutsceneTrigger**: plays a cutscene when the player comes near.

Questions: how do I add a spawner? what is a smart object? how do I make stairs between heights?

## Scene-wide components: Music, Weather, Zone, Light

- **Music**: the scene's **track**, **ambience** and **combat** music. While the player is in the scene, the first
  entity with Music sets what plays; changing scene cross-fades.
- **Weather**: None, Rain, Snow, Fog or Sandstorm, with an intensity.
- **Zone**: the scene's level band; enemies here are made at a level within it.
- **Light2D**: a light (colour, intensity, radius, flicker, **nightOnly**) when Lighting is on.
- **Sounds**: an entity's sound effects (attack, hurt, death, collect).
- **LuaScript**: a script; its properties are per-entity settings.

Scene-wide components can go on any entity of the scene (+ Add component in the Inspector).

Questions: how do I add music to a scene? how do I make it rain? how do I set enemy levels for a scene? how do I add a
light?
