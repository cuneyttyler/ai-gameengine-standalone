# Lua API

The functions a Lua script can call, grouped by subject, with what they return. Ids are entity ids (integers); positions
are in cells. See "Scripting with Lua" for where scripts run and how events and coroutines work.

## Lua: this entity, position and movement

| function | returns | notes |
|---|---|---|
| `self_id()` | id | this entity |
| `get_position()` | x, y | this entity's world position |
| `set_position(x, y)` | | teleports; no collision |
| `move_toward(x, y, speed)` | | walks toward a point for one step, with wall collision and Slow applied |
| `position_of(id)` | x, y or nil | |
| `distance_to(id)` | number or nil | |
| `find_nearest(team)` | id, distance or nil | team "Player", "Enemy" or "Neutral"; living entities with a Faction |
| `entity_named(name)` | id or nil | a thing by its Name, or a character by its name or World Bible key ("house-4") |
| `random_point_near(x, y, radius[, building])` | x, y or nil | a walkable cell's middle within radius; with a building's id, in its rooms |
| `move_to(x, y)` | boolean | in a coroutine: walks there around walls, opening doors; true on arrival, false if there's no way |
| `follow_path(name[, "start"])` | boolean | in a coroutine: walks a named path doing its stops; true at its end |
| `path_route(name[, from])` | list or nil | one walk of a path, forks chosen: `{x, y, wait, clip, use}` each |
| `face(id)` | | turns this entity toward another |
| `set_visible(id, visible)` | | shows or hides an entity |
| `spawn(prefab, x, y)` | id or nil | places a copy of a prefab (`"prefabs/skeleton.prefab"`); tile prefabs paint the cell |
| `become(id, prefab)` | new id or nil | replaces it in place by a prefab, turned as it was, contents kept |
| `set_texture(id, path)` | boolean | changes its picture |

Questions: how do I move an entity from a script? how do I find an entity by name? how do I spawn a prefab? how do I pick
a random walkable spot?

## Lua: fields, health and combat

| function | returns | notes |
|---|---|---|
| `get_field(id, component, field)` | value or nil | any field the Inspector shows: `get_field(self_id(), "Health", "max")`; enums as option names |
| `set_field(id, component, field, value)` | boolean | enums accept a name or index; false on a missing component or wrong type |
| `is_alive(id)` | boolean | |
| `get_health([id])` | current, max or nil | |
| `attack_toward(id)` / `attack_toward(x, y)` | boolean | swings this entity's MeleeAttack; false on cooldown |
| `shoot(x, y)` | boolean | this entity's RangedAttack toward a point |
| `dodge(dx, dy)` | boolean | a dodge dash |
| `apply_status(id, kind, seconds, magnitude)` | | "burn", "poison" (can't kill), "slow", "regen"; re-applying refreshes |
| `is_hostile(a, b)` | boolean | factions and teams |
| `set_disposition(id, n)` | | a FactionMember's disposition to the player (-100..100) |
| `raycast(x0, y0, x1, y1[, mask])` | table or nil | first solid tile or collider: `{entity, x, y, distance}`; entity nil for a wall |
| `overlap(x, y, radius[, mask])` | ids | entities whose colliders touch a circle |
| `line_of_sight(x0, y0, x1, y1)` | boolean | nothing solid between |
| `apply_impulse(id, ix, iy)` / `set_velocity(id, vx, vy)` | | rigid bodies |

Questions: how do I read a component field in Lua? how do I damage or slow an entity? how do I check line of sight? how
do I push a physics object?

## Lua: items, attributes and progression

| function | returns | notes |
|---|---|---|
| `give_item(id, item[, n])` | number given | `"gold"` for gold; sends `item_added` |
| `take_item(id, item[, n])` | number taken | |
| `count_item(id, item)` | number | |
| `equip(id, item)` / `use_item(id, item)` | ok, reason | |
| `attribute(id, name)` | number | the effective value |
| `add_modifier(id, attribute, value[, seconds, source, "multiply"])` | | seconds < 0 lasts until removed |
| `remove_modifiers(id, source)` | | |
| `award_xp(id, n)` | levels gained | scaled by the difficulty's xp |
| `level(id)` | number | |
| `take_perk(id, perk)` | ok, reason | |
| `drop_loot(x, y, table[, level])` | number dropped | rolls a loot table onto the ground |
| `hold_item(item)` / `held_item()` / `put_back_item()` | | the player's item in hand |
| `craft(recipe[, station[, who]])` | "" or why not | made by the player, or by `who` from its own inventory |
| `record(type, id)` / `records(type)` | table / ids | a record's fields with base and defaults; all ids of a type |

Questions: how do I give the player an item in Lua? how do I check how many items the player has? how do I make an NPC
craft? how do I add a temporary buff?

## Lua: story, quests and dialogue

| function | returns | notes |
|---|---|---|
| `set_flag(name[, value])` / `get_flag(name)` | / boolean | value defaults to true |
| `get_var(name)` / `set_var(name, value)` | value / | numbers, text, booleans; nil removes |
| `start_quest(id)` | | if not started |
| `quest_state(id)` | text | "inactive", "active", "completed", "failed" |
| `quest_stage(id)` / `set_stage(id, stage)` / `fail_quest(id)` | number / / | stage 0 when not active |
| `complete_objective(quest, objective)` | | if open in the current stage |
| `used(name)` | | counts as using something (use, examine objectives) |
| `start_dialogue(path)` | boolean | a conversation with this entity as the speaker |
| `say(text)` (old `bark`) | | a line over this entity's head; `a|b` picks one |
| `play_cutscene(id)` / `skip_cutscene()` / `in_cutscene()` | boolean / / boolean | |
| `time_of_day()` / `day()` | hours / day number | the game clock |

Questions: how do I set a story flag in Lua? how do I start a quest from a script? how do I check a quest's stage? how do I
make a character speak from a script?

## Lua: character AI and the world's things

| function | returns | notes |
|---|---|---|
| `push_package(id, package)` / `pop_package(id, package)` | | an AI package on top of a character's stack until popped |
| `awareness_of([id])` | 0-1 | how aware a character is of the player |
| `find_object(type[, radius])` | id or nil | the nearest smart object of a type (radius default 10) |
| `find_part(name[, radius])` | `{id, index, x, y}` or nil | the nearest animated part this one can use named so (radius default 20) |
| `use_part(name)` / `use_part(id[, index])` | boolean | uses it as the player would (opens or shuts, sends its event, runs its do, plays its player clip); by name within 3 cells |
| `part_open(id[, index])` | boolean | a door open, a stove cooking, a part always playing |
| `act(user, target)` / `act(user, "farm")` | boolean | `user` uses a Reaction or Interactable, or the nearest with that action within 1.5 cells, else a part named so |
| `use(id)` | boolean | used as the player would (key from their pockets, opened, taken, flag, event, becomes) |

A part is named by its input action or event, its station, its clip, the event it sends, its prompt's words, or `door`.

Questions: how do I make a character open a door in a script? how do I check if the stove is on? how do I push a package
from Lua?

## Lua: animation, effects, sound and UI

| function | notes |
|---|---|
| `play(id, name)` | a state, a variant ("attack_kick") or a clip by name, once; "stop" goes back to the default |
| `anim_trigger(id, trigger)` | an animator state: attacking, casting, using, hurt |
| `anim_set(id, name, value)` | a parameter for an animator record's conditions |
| `tween(id, "Component.field", to, seconds[, ease])` / `tween_done(handle)` | eases a number field: linear, ease_in, ease_out (default), ease_in_out, bounce |
| `play_effect(id, x, y)` / `play_effect(id, entity)` | an effect record or preset |
| `emit_particles(id, x, y[, seconds])` | a burst, or that many seconds |
| `shake(trauma)` / `hitstop(seconds)` / `slow_motion(scale, seconds)` | screen shake (0-1), freeze, slow motion |
| `play_sound(path)` / `play_sound_at(path, x, y)` | a sound; at a point, quieter with distance and panned |
| `play_music(path)` | music instead of the scene's; `""` back to the scene's |
| `notify(channel, text[, seconds])` | toast, feed, banner or alert |
| `open_screen(id)` / `close_screen(id)` / `is_open(id)` | the project's screens (ui/screens/<id>.rml) and panels |

Questions: how do I play an animation from a script? how do I play a sound at a position? how do I show a notification?
how do I change the music for a boss fight?

## Lua: magic, input, difficulty, achievements, events

| function | notes |
|---|---|
| `cast(ability[, target])` / `cast(ability, x, y)` | this entity casts an ability, known or not |
| `apply_effect(id, effect[, magnitude, duration])` / `remove_effects(id, tag)` | magic effects |
| `has_tag(id, tag)` / `add_tag(id, tag)` / `cooldown_left(ability)` / `learn(id, ability)` | tags, cooldowns, teaching |
| `action_down(name)` / `action_pressed(name)` | an input action held, or pressed this step |
| `axis(name)` | an axis action in [-1, 1] (`axis("move_x")`) |
| `difficulty()` / `difficulty_step(category)` / `set_difficulty(id)` | the level's id; a category's step 1-5; change level |
| `unlock_achievement(id)` / `achievement_unlocked(id)` / `achievement_progress(id)` | achievements |
| `on(event, fn)` / `emit(event[, data])` | events (`"*"` for all); data may have entity, other, amount, text |
| `after(s, fn)` / `every(s, fn)` / `cancel(handle)` | timers |
| `start(fn)` / `wait(seconds)` | coroutines |

Questions: how do I cast a spell from a script? how do I read input in Lua? how do I check the difficulty in a script? how
do I unlock an achievement?
