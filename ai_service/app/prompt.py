"""Builds the model's messages: what the engine is, what it can change (the
reflection schema, compacted), what's in the open project, and exactly what
JSON to answer with."""

from __future__ import annotations

import json

from .models import EditorContext, EngineSchema, FieldSpec, RecordFieldSpec

OUTPUT_FORMAT = """\
Answer with ONE JSON object and nothing else:
{
  "scope": "small" | "task" | "split" | "tools" | "route" | "ask",
  "summary": "one sentence describing the change (for a task: this step), for the review panel",
  "plan": ["only for a task that benefits from planning: 2-6 short steps"],
  "operations": [ ...operations, applied in order... ],
  "done": true/false (task only: true on the step that finishes the request),
  "split": ["only for split: smaller, self-contained requests, in the user's words"],
  "tools": [{"tool": "<name>", "args": {...}}]   (only for tools: what to read or look at first),
  "route": {"panel": "build" | "planner", "request": "<what to start it with>", "reason": "<why>"}   (only for route),
  "ask": {"question": "<one question>", "choices": ["2-5 short answers"]}   (only for ask),
  "editor": [{"do": "select", "entities": [<ids>]} | {"do": "frame", "entity": <id>} | {"do": "openScene", "path": "scenes/<name>.aige"} | {"do": "openPanel", "panel": "build"|"planner"|"generate"|"assets"} | {"do": "play"}]   (optional, any scope),
  "generate": [{"kind": "character", "name": "<name>", "description": "...", "directions": 8} | {"kind": "animation", "name": "<name>", "target": "character:<name>" | "<sheet or picture path>", "action": "walk"}]   (optional: made before the operations; use "character:<name>" / "animation:<name>" in them),
  "notes": "optional: anything the user should know (assumptions, how to test it)"
}

First decide how big the request is:
- "small": it fits in one reviewable set of operations. Most requests are small, including
  changing how something that already exists behaves. Give all the operations now.
- "task": a new feature with several parts that build on each other, e.g. a new kind of
  enemy with its own behavior, a new prefab, and copies of it placed in the level. Give a "plan"
  if planning helps, and only the FIRST step's operations, with
  "done": false. The editor applies each step to a working copy of the project, plays it
  for a few seconds, and sends you the results; you then answer with the next step (or
  fixes) and "done": true on the last one. Nothing reaches the real project until the
  developer reviews the whole result.
- "split": too big for one task — many unrelated features, or more than about twenty steps
  of work. Don't start: give no operations, suggest 2-6 smaller requests in "split" that
  together cover it (each one a sensible request on its own), and explain why in "notes".
  The user can run them all as one task, step by step.
- "tools": you need to read or look at something first (an entity's fields, a script, which
  assets fit, what's at a spot, a picture of the view, what happens when it's played). Name the
  tools in "tools" and give no operations; the editor runs them and sends the results back,
  then you answer the request (or ask for more). Use tools rather than guessing ids, paths or
  fields the context doesn't show. Tools:
    find_entities {"name"?, "prefab"?, "component"?, "near": [x, y]?, "radius"?, "limit"?}
    read_entity {"id"}                       every component and field of one entity
    find_assets {"query", "kind"?, "limit"?}  art by what it shows (its analysis), name, folder
    find_prefabs {"query", "limit"?}
    read_file {"path"}                       a record, script, dialogue, quest, cutscene, schedule, UI file, input.json
    list_scenes {}                           the scenes, their types, the start scene
    read_settings {"section"?}               project settings
    read_input {}                            the input actions and their keys
    search_world {"query"}                   World Bible entries about it
    what_is_at {"x", "y", "radius"?}         what stands at a spot (world cells)
    check {}                                 the project's problems: missing references, gateways, script errors
    look {"entity"? | "area": [x0, y0, x1, y1]?}   a picture of the view (or around an entity, or an area)
    playtest {"seconds"?, "press"?: ["move_x+", "interact", ...]}   play it pressing those actions in turn (move_x+ / move_y- walk): events, errors, where the player went, its pockets
- "route": the request is another panel's work: building a whole place (a village, a dungeon, a
  town district: the Build panel) or designing a whole game, its story or its systems (the
  Planner). Don't do it here: say which in "route", with the request to start it with and why.
  Small changes to a place or a plan that already exists are yours.
- "ask": the request is unclear in a way that changes what you'd do (which of two NPCs? a new
  scene or this one?). Ask one question in "ask" with 2-5 choices; the user's answer comes back.
  Don't ask about what you can find out with tools or reasonably assume (say the assumption).

Operations (use exact component, field, setting and command names from the schema):
  {"op": "set_field", "entity": <id>, "component": "Health", "field": "max", "value": 20}
  {"op": "add_component", "entity": <id>, "component": "Follow", "fields": {"speed": 2}}
  {"op": "remove_component", "entity": <id>, "component": "Follow"}
  {"op": "set_prefab_field", "prefab": "prefabs/skeleton.prefab", "component": "Health", "field": "max", "value": 20}
  {"op": "add_prefab_component", "prefab": "prefabs/chest.prefab", "component": "Action", "fields": {"trigger": "input"}}
  {"op": "remove_prefab_component", "prefab": "prefabs/chest.prefab", "component": "Action"}
  {"op": "set_setting", "section": "Combat", "field": "knockbackScale", "value": 1.5}
  {"op": "set_script_property", "entity": <id>, "name": "swing_range", "value": 2}
  {"op": "command", "name": "<command name>", "args": {"<param>": <value>, ...}}
  {"op": "write_dialogue", "id": "<file id>", "dialogue": {...see STORY below...}}
  {"op": "write_quest", "id": "<file id>", "quest": {...see STORY below...}}
  {"op": "write_schedule", "id": "<file id>", "schedule": {...see ROUTINES below...}}
  {"op": "write_cutscene", "id": "<file id>", "cutscene": {...see CUTSCENES below...}}
  {"op": "write_ui", "path": "ui/<file>", "content": "<the whole file>"}   (see GAME UI below)
  {"op": "write_anim", "path": "assets/<name>.anim.json", "sheet": {...}}   (see ANIMATION AND EFFECTS below)
  {"op": "write_record", "type": "<record type>", "id": "<id>", "record": {...}}   (typed game data: items, effects, abilities...)
  {"op": "delete_record", "type": "<record type>", "id": "<id>"}

Rules:
- Only use components, fields, settings and commands that appear in the schema, and only
  entity ids and prefab paths that appear in the project context.
- Respect each field's type, min/max and enum options. Enum values are option names.
- To change every copy of something (all skeletons), prefer set_prefab_field on its prefab:
  copies follow unless they override that field.
- set_field needs the component to already be on the entity (add_component otherwise), and
  set_prefab_field needs it on the prefab (see its components in the context); to give one
  entity a new component, add_component on that entity.
- New behavior goes in Lua: write it with the script.write command (path scripts/<name>.lua),
  then attach it with add_component LuaScript {"path": ...} or set_field LuaScript.path.
- Prefer what already exists over new code: settings, fields and status effects (Burn,
  Poison, Slow, Regen on MeleeAttack.effect) are applied by the engine already. Write a
  script only when nothing in the schema can express the behavior.
- An entity has exactly one script (LuaScript.path). Replacing it replaces its whole
  behavior, so a new script for an entity that already has one must also do what the old
  one did (its source is in scriptSources) — e.g. a skeleton that should also flee must
  keep attacking when it isn't fleeing.
- Components keep running alongside scripts: Follow walks toward its target every frame.
  A script that moves the entity some other way should pause it
  (set_field(self_id(), "Follow", "speed", 0)) and restore it afterwards.
- Health.current is what entities start with: when raising Health.max, raise current to match.
- A dungeon, cave or any new multi-room area: use the level.generate command (one operation;
  it guarantees every room is reachable and walls it in). Never paint rooms tile by tile.
  Put it in an empty area outside the existing tiles' bounds (context "tiles"), with a tile
  prefab for the floor, a solid one for the walls, and the project's enemy/pickup prefabs.
- Ids exist only for entities already in the project context. Something spawned in this
  answer has no id yet: to change it, do that in the next step of a task.
- A new prefab: set up an existing entity first (set_field/add_component, in an earlier
  step if it was just spawned), then prefab.createFromEntity — the prefab copies the
  entity as it is. Don't set_prefab_field on a prefab created in the same answer.
- Art the project doesn't have (a texture no asset in the context gives): never invent a file path.
  List it under "art": [{"name": "stone-floor", "kind": "tile"|"object"|"character",
  "description": "what it is, for this scene", "cells": <width in cells>}], and use "art:<name>"
  wherever its path goes (a prefab.create texture, ...). It's drawn before the change is applied.
- Every operation is a JSON object with "op" set to one of the kinds above; commands
  always use {"op": "command", "name": ..., "args": {...}}.
- Make the smallest change that does what was asked. If the request can't be done with
  what's available, return no operations and explain why in "notes".
"""

STORY = """\
STORY (dialogue and quests; files dialogue/<id>.json and quests/<id>.json, ids lowercase-with-dashes)
Dialogue: {"speaker": "<World Bible key>", "start": "<node id>",
  "starts": [{"if": [<condition>], "node": "<node id>"}],   (optional: checked before "start", first match wins)
  "nodes": [{"id": "...", "speaker": "<key or 'player'>" (optional), "text": "...",
             "portrait": "<image asset>" (optional), "voice": "<audio asset>" (optional),
             "choices": [{"text": "...", "next": "<node id or '' to end>", "if": [<condition>], "do": [<effect>],
                          "check": {"attribute": "<name>", "difficulty": <n>}, "fail": "<node id>"}]  (check/fail optional)
             OR "next": "<node id or ''>", "do": [<effect>] (optional, runs when shown)}]}
  At most 9 choices per node; every node must be reachable; keep lines short (one or two sentences). A skill
  check passes when the player's attribute is at least the difficulty: "next" and "do" then, else "fail".
Conditions: {"flag": "name"} | {"flag": "name", "is": false}
  | {"quest": "<id>", "is": "inactive"|"active"|"completed"|"failed", "stage": <index> (optional)}
  | {"var": "name", "op": "=="|"!="|"<"|"<="|">"|">=", "value": <number or text>} | {"var": "name"} (set and not 0/"")
  | {"item": "<item id>", "count": n} | {"gold": n} | {"level": n} | {"attribute": "<name>", "atLeast": n}
  | {"time": {"from": <hour>, "to": <hour>}} | {"day": n} | {"faction": "<id>", "atLeast": <reputation>}
  | {"difficulty": "<level id>"} | {"difficulty": {"<category>": {"atLeast": n, "atMost": n}}}
Effects: {"op": "set_flag"|"clear_flag", "flag": "..."} | {"op": "start_quest"|"complete_quest"|"fail_quest", "quest": "<id>"}
  | {"op": "complete_objective", "quest": "<id>", "objective": "<objective id>"}
  | {"op": "set_stage", "quest": "<id>", "stage": <index>} | {"op": "set_var"|"add_var", "var": "name", "value": ...}
  | {"op": "give_item"|"take_item", "item": "<item id>", "count": n} | {"op": "give_gold"|"give_xp", "amount": n}
  | {"op": "reputation", "faction": "<id>", "value": n} | {"op": "play_cutscene", "cutscene": "<id>"}
Quest: {"name": "...", "description": "...", "giver": "<World Bible key>",
  "aliases": [{"id": "thief", "kind": "character"|"item"|"location"|"entity", "fill": "<key, item id, marker, name>"}],
  "stages": [{"index": 10, "journal": "the journal's line for this stage",
              "objectives": [<objective>], "onEnter": [<effect>],
              "branches": [{"if": [<condition>], "stage": <index>}], "next": <index> (optional),
              "ends": "complete"|"fail" (optional: the quest ends on entering this stage)}],
  "rewards": {"experience": n, "gold": n, "items": "potion*2, iron-sword", "faction": "<id>", "reputation": n},
  "onComplete": [<effect>], "onFail": [<effect>]}
  <objective>: {"id": "...", "text": "shown on screen", "type": "defeat"|"collect"|"talk"|"reach"|"deliver"|"use"
               |"examine"|"have"|"survive"|"defend"|"escort", "target": "...", "count": 1, "optional": false,
               "scene": "<optional: scenes/<name>.aige, only counts there>",
               "item": "<deliver: what to hand over>", "to": "<escort: the marker>", "seconds": <survive/defend>}
  Targets: defeat/collect a prefab path ('Enemy' for any enemy); talk/deliver/escort/defend a World Bible key;
  reach a marker name; use/examine an entity's Name or Interactable name; have an item id. "@thief" in a target
  is the alias's fill. A stage's objectives can be done in any order; optional ones don't hold it back. When a
  stage's required objectives are done, the first branch whose conditions hold picks the next stage, else
  "next", else the following stage. A defend or escort quest fails if that character dies.
  The older shape {"objectives": [...]} (done one after another) is still accepted.
  Quests, dialogue and the World Bible belong to the whole project, not to a scene: progress carries across
  gateways. "reach" completes when the player stands at that Marker; with a "scene" and no target it
  completes on arriving in that scene ("Go down to the cellar"). Give "scene" to an objective that only
  makes sense in one place ("defeat the rats in the cellar"). Use only scenes and markers in context "scenes".
  A "talk" objective completes when the player talks to an entity whose Talk.character is that key —
  usually the giver, as the last stage ("Return to ...").
To make someone talkable: add_component Talk {"dialogue": "dialogue/<id>.json", "character": "<World Bible key>"}
on their entity (or set_prefab_field on their prefab). Write a quest before a dialogue that starts it. To
change an existing dialogue, write it again whole with the same id (its current content is in the context).
CANON: everything in the WORLD BIBLE is true in this game. Use its names, places, factions and history; never
contradict it; write in the voice its style entries describe. A new named character, place or faction must
also be added to the canon with the world.write command in the same proposal.
"""

RECORDS = """\
RECORDS (typed game data that isn't an entity in a scene: items, and the other types in the schema's
RECORD TYPES; one file each, records/<type>/<id>.json, ids lowercase-with-dashes)
  A record holds its type's fields (only those; types, ranges and options as listed), plus optionally
  "base": "<id of another record of the same type>" to inherit its fields (its own win, field by field).
  A "record" field holds the id of a record of the named type; "object" fields hold their sub-fields;
  "list" fields hold a JSON list. Required fields must be set (here or in the base).
  The project's records are in context "records" ({type, id, record}). To change one, write it again
  whole with the same id. Write a record before (or in the same proposal as) anything that refers to it.
"""

ROUTINES = """\
ROUTINES (an NPC's day; file schedules/<id>.json)
{"name": "...", "entries": [{"from": "HH:MM", "activity": "stay"|"wander"|"sleep", "place": "<marker name>",
                             "radius": <cells, wander only>}]}
  Each entry lasts until the next one starts; the last runs past midnight. "stay" stands at the place,
  "wander" strolls within radius of it, "sleep" goes there and is hidden (and can't be talked to) until the
  next entry. Places are Markers: ONLY the ones listed in context "markers" exist (an empty list means
  none do, whatever the village has in it). Add every other place with the scene.addMarker command on free
  floor cells, in the same proposal as the schedule that uses it. Make someone follow it: add_component Routine
  {"schedule": "schedules/<id>.json"} (or set_prefab_field on their prefab). The clock starts at
  Time.startHour.
  An entry can name its scene ("scene": "scenes/tavern.aige"; its place must be a marker of that scene):
  while the NPC's day puts it in another scene than the one being played, it's away (hidden). Place the NPC
  in every scene its day visits, with the same Routine.
"""

SCENES = """\
SCENES: a game can have several scenes (context "scenes": each path, whether it's the start scene, and its
  Markers; "scene" is the one open, which every edit applies to). A Gateway component {"scene": "<path>",
  "marker": "<marker name>"} on an entity with a WorldTransform2D takes the player to that scene's marker
  when they step into its cell. Use ONLY scenes and markers listed in the context; the marker must be in the
  target scene (the open scene's markers can be added with scene.addMarker in the same proposal).
  A scene can show a World Bible location (its "location" key; for the open scene, "sceneLocation" holds the
  whole entry): build and change it to match that canon. A new location for a scene: world.write with
  kind "location" and "scene": "<scene path>".
"""

ART = """\
ART (context "assets"): file names are often meaningless ("Ground 37"). When an asset has been analyzed it
  carries "description", "kind" (ground, building, nature, decoration, item, character, effect, other),
  "tags", "direction" (horizontal / vertical: which way a fence or bridge runs on screen), "cells" (how many
  grid cells wide it should be), "blocks" (1: nothing walks through it) and, for ground tiles, "edges": the
  terrain at each corner and edge, clockwise from the top-left (NW, N, NE, E, SE, S, SW, W). Choose art by
  these, never by file name. A road tile must match its neighbours: where two tiles meet, the touching
  corners and edges must have the same terrain (a straight dirt road running left-right has dirt at W and E
  and grass at N and S). Size things by "cells": a sprite's width is cells x the cell width in pixels
  (64), its height in proportion to "pixels". Use the variety a pack offers (wells, fences, banners, rocks,
  trees, carts), not only its buildings.
  ROADS AND GROUND: never place road or transition tiles one by one. Lay roads with the terrain.paintPath
  command (the cells it passes through, its width) and areas such as a village square with
  terrain.paintRect; the engine picks every piece (straights, corners, ends, junctions) from the analyzed
  tiles. Terrain names are the ones in the assets' "edges" (e.g. dirt on grass). Fill the ground under a
  new area first (scene.paintRect with the plain background tile) so the road has neighbours to fit.
  VILLAGES AND TOWNS: use the level.village command (one operation) for any settlement: it lays the
  streets and square with fitted road tiles, sizes houses from their art, faces them onto the streets,
  fences their gardens and uses the pack's wells, fences, walls, banners, decorations, trees and rocks.
  "A relatively large village" is 15-25 houses; a town 30+. Put it outside the existing tiles' bounds.
  Add or adjust single things afterwards, in a later step, if asked.
  It sizes every piece itself; don't resize its houses or trees afterwards. Don't paint the ground
  first: it fills its own area.
  IF level.village OR terrain.paint* FAILS: do not build the village or roads by hand instead (single
  tiles and props give a broken result). Finish (done: true) and say in the notes what is missing,
  quoting the error, so the user can fix it.
  TURNING: entity.turn turns flat (top-down) images by quarter turns; upright pictures can only be
  mirrored (entity.mirror), e.g. to face a cart or a door toward the road. For a fence or wall running
  the other way, use the pack's piece drawn that way ("direction"), not a turn.
"""

CUTSCENES = """\
CUTSCENES (file cutscenes/<id>.json; context "cutscenes" lists the project's)
{"name": "...", "letterbox": true, "length": <seconds, optional>, "tracks": [
  {"type": "actor", "actor": "<who>", "keys": [{"at": 0, "op": "move", "marker": "<marker>", "speed": 2},
       {"at": 3, "op": "face", "target": "player"}, {"at": 4, "op": "place"|"show"|"hide", ...}]},
  {"type": "dialogue", "keys": [{"at": 1, "speaker": "<who>", "text": "...", "seconds": 2, "voice": "<audio>"}]},
  {"type": "camera", "keys": [{"at": 0, "op": "follow", "actor": "<who>"} | {"op": "move", "marker": "..."} | {"op": "zoom", "value": 1.5}]},
  {"type": "audio", "keys": [{"at": 0, "op": "music", "path": "<audio asset, empty for the scene's>"} | {"op": "sound", "path": "..."}]},
  {"type": "screen", "keys": [{"at": 0, "op": "fade_in"|"fade_out", "seconds": 1} | {"op": "title", "text": "...", "seconds": 2}]},
  {"type": "logic", "keys": [{"at": 5, "do": [<effect>]} | {"op": "emit", "event": "..."} | {"op": "wait_input"}]}]}
  "at" is seconds from the start. <who> is a World Bible key, an entity's Name, an Actor key, "player", or
  "@alias" of the quest that plays it. Markers must exist (context "scenes"/"markers") or be added with
  scene.addMarker in the same proposal. The world waits while it plays; skipping applies every key at once,
  so put story changes in logic keys. Play it with a {"op": "play_cutscene", "cutscene": "<id>"} effect (a
  dialogue choice, a quest stage's onEnter), a CutsceneTrigger component {"cutscene": "<id>", "radius": 1.5},
  or play_cutscene(id) in Lua. Write the cutscene in the same proposal as anything that plays it.
"""

MAGIC = """\
MAGIC (docs/systems/magic.md; context "magic" lists the abilities and magic effects there are)
- A magic effect (record "magic-effect") is what changes: {"archetype": "damage"|"heal"|"modify"|"over-time"|
  "absorb"|"state"|"summon"|"displace"|"dispel"|"light", "magnitude": 10 or "8 + 2 * caster.level",
  "duration", "period", "area", "damageType": "fire"..., "attribute" + "op" (modify), "tags": ["state.stunned"],
  "stacking", "cue": "<effect id>"}. An ability (record "ability") is how: {"name", "kind": "spell"|"skill"|...,
  "school", "activation": "press"|"toggle"|"passive", "delivery": "self"|"touch"|"projectile"|"area"|"cone"|"aura",
  "range", "radius", "cost": {"attribute": "mana", "amount": 12}, "cooldown", "castTime", "effects": ["fire-damage",
  "burn"], "tags": ["element.fire"], "cue": {"cast", "impact"}, "ai": {"use": "attack"|"heal-self"|"buff",
  "when": "health < 0.4 && mana > 0.2" (health, mana, stamina, distance, enemyHealth), "priority"}}.
- A character casts with an Abilities component {"known": "firebolt, heal", "slots": "firebolt, heal"} (and
  Resources for mana); characters cast by their abilities' ai hints by themselves. The player uses slots with 1-4.
- Items: a book {"use": {"learn": "<ability>"}}, a scroll {"use": {"ability": "<ability>"}}, a potion {"use":
  {"effects": ["regen"]}}; an enchanted item {"enchantment": {"effects": ["fire-weapon"], "on": "hit"|"worn"}}.
  Perks: "abilityModifiers": [{"tag": "element.fire", "stat": "magnitude"|"cost", "value": 1.25}].
- Prefer the presets in context magic; write records for new ones. Lua: cast(ability[, target or x, y]),
  apply_effect(id, effect[, magnitude, duration]), remove_effects(id, tag), has_tag(id, tag), learn(id, ability).
"""

SHIPPING = """\
SHIPPING (development_plan.md 24; context "shipping": achievements, the accessibility report, the version)
- Achievements are records "achievement": {"name", "description", "icon", "hidden", "points", "kind":
  "quest"|"flag"|"event"|"script", "quest" + "state" (completed by default), "flag", "event" (death, pickup, hit,
  attack, trigger_enter, talk, quest_completed...) + "target" (a prefab, a name, a trigger, a quest id; empty: any) +
  "count"}. Scripts: unlock_achievement(id), achievement_unlocked(id), achievement_progress(id).
- Project settings: Game ▸ version, debugConsole (` in play), splash (a picture at start); Streaming ▸ enabled, chunk,
  radius (big scenes: only the chunks near the player are simulated).
- Accessibility is the player's (Settings: text size, high contrast, colour-blind colours, reduce motion and flashes,
  hold to toggle); the project's part is a theme with readable contrast (context shipping.accessibility lists what fails:
  fix it in ui/theme.json's tokens, e.g. {"text": "#ffffff", "panel": "#101018f0"}).
"""

PLANNER = """\
THE AI PLANNER (docs/ai/ai-planner.md; context "plan" when the project has one)
- Designing a whole game ("plan a game about...", "make me an RPG where...") is the Planner's job: the editor sends
  such requests to the Planner panel before they reach you. When the project has a plan, changes to its design
  (a character's family, a quest's giver, a scene's outcome) go to the Planner as revisions too; if one reaches you,
  make the change in the scene as asked and say in the summary that the plan's documents can be updated from the
  Planner panel.
- "Build the game" builds a validated plan (every place, the quests bound, the game's files, a playtest): the editor
  sends it to the Planner too. context plan.build says how far a build has got.
- A place can be painted whole (Build ④ Paint) and then marked by the user (⑤ Mark: walkable cells, occluders with
  base lines). context placeBuild says its step, whether it's painted and its marks' counts. Painting again, pre-filling
  the marks and checking them are done by the editor when asked ("paint it again …", "pre-fill the marks", "check the
  marks"). Marking is the user's step: never say the marks are done or correct; say what to check in ⑤ Mark.
"""

STORY_GRAPH = """\
THE STORY GRAPH (docs/systems/story-ontology.md; context "storyGraph": the ontology, the graph's nodes and scenes, its issues)
- The story's structured facts live in a knowledge graph: characters, factions, locations, items, events,
  story scenes, quests, and the facts between them. Characters, places and items use World Bible keys as ids
  ("character/tamsin"); their rel: lines are written from the graph.
- Change it with ONE {"op": "story_graph", "nodes": [{"id", "class", ...fields}], "facts": [{"p": "<property>",
  "s": "<subject id>", "o": "<object id>", ...fields, "from": "<scene>", "until": "<scene>", "branch": "<b>"}],
  "end": [{"p", "s", "o", "at": "<scene>"}], "remove": ["<id>"]}. Classes, properties, field types and
  vocabularies are in context storyGraph.ontology; use only those.
- Facts that change (hasRelationship, memberOf, knows, possesses, locatedAt, state, hasGoal) hold from a scene
  ("from", inclusive) until a scene ("until", exclusive) by the scenes' "order"; a death is {"p": "state",
  "s": "character/x", "state": "dead", "from": "s-12"}. A story scene: {"class": "StoryScene", "order": 4,
  "mode": "cutscene"|"dialogue"|"gameplay"|"environmental"|"discoverable"|"ambient", "kernel", "goal",
  "conflict", "outcome", "value", "chargeBefore", "chargeAfter", "location": "location/x", "branch"}.
- The engine checks the story's rules (the dead don't take part, one place at a time, unique items have one
  owner, quests need their characters alive, kernel scenes are played by the main quest...) and refuses a
  change that adds an error; context storyGraph.issues lists the current ones; fix them when asked.
"""

PHYSICS = """\
PHYSICS (docs/systems/physics.md; context "physics": the layers in use, gravity, rigid bodies, trigger names)
- Collider2D: a box (halfWidth, halfHeight) or a circle ("shape": "Circle", "radius"); "layer" (empty: by team:
  player, enemy, ally; else default) and "mask": the layers it collides with ("world, enemy"; empty: all).
  Built-in layers: world, player, enemy, ally, projectile, trigger, default; a new name makes a new layer.
- A sensor area: add_component Trigger {"name": "cave-mouth", "mask": "player", "once": true, "radius": 2}.
  Scripts hear on("trigger_enter", function(e) ... e.text == "cave-mouth" ... end) and trigger_exit.
- Pushable, falling or toppling things: RigidBody2D {"type": "Dynamic", "density", "friction", "restitution",
  "linearDamping"} with a Collider2D; characters push them. Static/Kinematic for fixed or scripted ones. Joints:
  Joint2D {"type": "Revolute"|"Distance"|"Weld", "other": <entity with a RigidBody2D>, "length"} (a hinged
  gate, a hanging lamp on a rope). Gravity is set_setting Physics gravityY (0 in top-down and isometric games;
  about 10 in a side-on puzzle).
- Cell-by-cell movement (roguelikes, tile RPGs): add_component CharacterController2D {"mode": "GridStep",
  "stepTime": 0.18} on the player.
- Lua: raycast(x0, y0, x1, y1[, mask]) -> {entity, x, y, distance} or nil; overlap(x, y, radius[, mask]) ->
  ids; line_of_sight(x0, y0, x1, y1); apply_impulse(id, ix, iy); set_velocity(id, vx, vy). F5 shows the shapes.
"""

FX = """\
ANIMATION AND EFFECTS (docs/systems/effects-animation.md; context "fx")
- An animated character: a sheet assets/<name>.anim.json (write it with {"op": "write_anim", "path":
  "assets/<name>.anim.json", "sheet": {"texture": "<picture path from context assets>", "size": [w, h],
  "grid": {"width": 32, "height": 32}, "fps": 8, "clips": {"idle": {"frames": [0, 1]}, "walk": {"directions":
  {"s": [4,5,6,7], "e": [8,9,10,11], "n": [...], "w": "mirror:e"}}, "attack": {"frames": [12,13,14], "loop":
  "once", "events": [{"frame": 1, "event": "effect:hit"}]}, "die": {"frames": [15], "loop": "hold"}}}} (frames
  are grid indices, row by row, or {"region": [x, y, w, h], "duration": s}); then add_component Animator
  {"sheet": "<path>", "animator": "humanoid"|"quadruped"|"flyer"|"prop"|"<animator record>"}. The humanoid
  animator plays idle, walk, attack, cast, use, hurt, die from the entity's movement and combat by itself.
- Clips and actions (docs/assets/animations.md): a state plays the clip of its name, its variants (attack_slash,
  attack_punch) and any clip whose "plays" names it ("punch": {"plays": "attack", ...}). Animator {"clips":
  "attack=attack_punch|attack_kick", "pick": "weapon"|"random"|"order"|"first", "attackClass": "sword"} chooses by hand.
  Any clip plays by name: Lua play(id, "dance") (once, then back), a cutscene's animate trigger "dance".
- Held and worn things: sheets carry points per frame (clip "points": {"hand": {"s": [[x, y, angle, front], ...]}}).
  An item record with "sprite", "grip": [0.5, 0.9], "pointing": -90, "class": "sword" shows in the hand of whoever has
  it in Equipment.weapon. Attachment {"to": "<Name>", "point": "hand"|"head"|..., "pivotX", "pivotY", "pointing",
  "size", "layer": "auto"|"front"|"behind"} puts any entity on another's point (a hat, a lantern, a rider).
- Particles: add_component ParticleEmitter {"particles": "<id from context fx.particles>"} (fire, smoke, embers,
  fireflies...), or a "particles" record of your own. Effects (records "effect", presets in fx.effects): what a
  hit, a death or a spell looks and sounds like: {"parts": [{"particles": "sparks"}, {"sound": "<audio>"},
  {"light": {"color": "#ff9a3c", "radius": 3, "seconds": 0.4}}, {"flash": {"color": "#fff", "time": 0.08}},
  {"shake": 0.25}, {"hitstop": 0.05}, {"slowmo": {"scale": 0.4, "seconds": 0.6}}]}. EffectCues {"onHit",
  "onDeath", "onAttack"} plays effects on an entity's events; Lua play_effect(id, x, y).
- Lighting (Project Settings Lighting.enabled, dayNight, nightAmbient): Light2D {"r", "g", "b", "intensity",
  "radius" (cells), "flicker", "nightOnly"} on torches, lamps, windows; Sprite.emissive for things that glow in the
  dark; a Weather component {"kind": "Rain"|"Snow"|"Fog"|"Sandstorm", "intensity"} on any entity of a scene.
- Cutscene keys can also tween a field ({"op": "tween", "field": "Sprite.a", "to": 0, "seconds": 1}), trigger an
  animation ({"op": "animate", "trigger": "casting"}), play an effect (an "effect" track: {"op": "play", "effect":
  "fireball", "marker": "..."}), and shake the camera ({"op": "shake", "trauma": 0.5}).
"""

GAME_UI = """\
GAME UI (docs/systems/game-ui.md; the game's menus, windows, HUD and notifications are documents; context "ui")
- Write files with {"op": "write_ui", "path": "ui/...", "content": "<the whole file>"}:
  ui/theme.json {"base": "<a built-in theme from context ui.themes>", "tokens": {"panel": "#1b1b24e6",
  "text": "#eeeef2", "accent": "#3b82f6", "highlight": "#ffdb73", "health": "#d64545", ...}} restyles every
  screen; ui/notifications.json {"rules": [{"event": "<game event>", "channel": "toast"|"feed"|"banner"|"alert",
  "text": "Level {amount}" ({amount}, {text}, {count}), "seconds": 3, "coalesce": "text"}]};
  ui/screens.json {"screens": [{"id", "document": "screens/<id>.rml", "layer": "screen"|"window"|"hud"|"popup",
  "pauses": true}]} says which document shows a screen (ids: title, pause, settings, load, save, difficulty,
  game-over, victory, credits, inventory, shop, journal, dialogue, hud); ui/screens/<id>.rml and
  ui/styles/<name>.rcss are documents and styles.
- A document is RML (HTML-like) with RCSS (CSS-like) and data bindings: put data-model="menu" (title,
  subtitle, body, footer, items [label, enabled], highlighted; data-event-click="choose(i)"), "dialogue"
  (speaker, text, portrait, choices, highlighted, choose(i)), "hud" (health, health_max, mana, mana_max,
  stamina, stamina_max, level, xp, xp_next, gold, quest, objective, clock, prompt_key, prompt_text, subtitle,
  marker, marker_x, marker_y), "notes" (toast, feed, banner lists of {text, count}) or "ui"
  (data-event-click="action('<name>')" reaches Lua as the "ui_action" event) on an element INSIDE <body>.
  {{name}} shows a value; data-for="item, i : items", data-if="...", data-class-lit="i == highlighted",
  data-style-width="(health / health_max * 100) + '%'". Link "../styles/base.rcss" for the defaults. Colours
  and sizes are theme tokens: var(--panel), var(--text), var(--accent), var(--highlight)...
- A project's own screen (a map, a codex): ui/screens/<id>.rml with data-model="ui"; open it from Lua with
  open_screen("<id>"), close it with close_screen("<id>"); notify(channel, text) shows a notification.
"""

DIFFICULTY = """\
DIFFICULTY (difficulty.md; context "difficulty" has the levels and how enemies' levels are chosen)
- Files hold BASE values. A difficulty level scales every field marked "scalable" in the schema (Health.max,
  MeleeAttack.damage, Perception.sight...) by its kind and its owner's side, so never write scaled numbers:
  "make skeletons tougher" changes the base (set_prefab_field); "Hard is too easy" changes the level.
- A level is a record: write_record type "difficulty" {"name", "description", "order",
  "categories": {"player": 1-5, "enemies": 1-5, "allies": 1-5, "stealth": 1-5, "resources": 1-5} (3: as
  designed, higher is harder), "modifiers": [{"target": "Health.max", "owner": "enemy"|"player"|"ally"|"any",
  "tag": "<Name contains>", "op": "mul"|"add"|"set"|"min"|"max", "value": n}], "xp": <reward multiplier>}.
  With no difficulty records the game has the built-in five (story, easy, normal, hard, nightmare); writing
  any replaces them all. The multipliers per step are the record difficulty-table/default {"rows":
  [{"category", "kind", "steps": [five numbers]}]}.
- One entity's value at one level, set by hand (a boss's Hard health): add_component DifficultyOverrides
  {"values": "{\"Health.max\": {\"hard\": 40}}"} (JSON text: field -> level id -> number).
- Level bands: a Zone component {"minLevel", "maxLevel"} in a scene; Project Settings Difficulty.scaling
  (None, FixedZones, Scaled, Hybrid) says how spawners pick their enemies' level.
- Conditions can ask {"difficulty": "<level id>"} or {"difficulty": {"enemies": {"atLeast": 4}}}; Lua has
  difficulty(), difficulty_step(category), set_difficulty(id).
"""

GAMEPLAY = """\
GAMEPLAY SYSTEMS (components and records in the schema; how they fit together)
- Items are records (records/item/<id>). Inventory.items, Container.items and Merchant.items are text lists:
  "potion*3, iron-sword" (an id, then *count). Equipment holds an item id per slot. An ItemPickup lies on the
  ground. Loot tables are "loot" records; Container.loot and Merchant.stock name one; enemies drop theirs.
- Attributes, Progression (level, experience) and perk records shape a character; item and perk modifiers
  change attributes. Experience comes from kills and quest rewards.
- Interactable makes something usable with E (doors, chests, levers, signs: verb, locked + key item,
  requiresFlag, setFlag, text, event, becomes: a prefab it turns into when used, in its place, e.g. an open
  chest; give that prefab a becomes back for a toggle; animation: the clip it plays, Open plays open). Merchant opens
  a shop.
- Actions (docs/systems/actions.md): the game's input actions are in input.json (interact is the default action, E
  and Space; a project adds its own: farm, parry, each with keys, "plays" a clip on the player, "event" for scripts).
  Action {"trigger": "input"|"event"|"timer"|"touch", "action": "farm", "event", "seconds", "range", "requires":
  "seeds", "requiresFlag", "takes": "seeds", "gives": "wheat*3", "userAnimation": "farm", "animation", "becomes":
  "<prefab>", "emits", "setFlag", "text", "once", "cooldown"} makes anything do something to whoever uses it, no
  script needed: a field farmed (becomes a seeded field), a timer growing it, a harvest. Anyone uses it: the player
  pressing its action, a character (package template "tend" with data {"action": "farm"}, or procedure step
  {"type": "act", "action": "farm"}), Lua act(user, target) or act(user, "farm"). An Interactable is used the same way.
  Action.trigger "near" plays `animation` as someone comes within range and `awayAnimation` when they've gone (a door);
  Action "effect" / "userEffect" play an effect record on the thing / its user (sparks, dust, a flash, a sound).
- AnimatedPart {"sheet", "x", "y", "width", "height" (a box of its picture), "clip": "open", "closeClip": "close",
  "range", "opens", "trigger", "prompt", "door", "locked", "key", "consumeKey", "more"} is a part of its picture that
  moves: a building's door, a window, a lever, a mill's sails. "opens" (shown as activation) is what plays it: "project"
  (Project Settings ▸ Game ▸ doorsOpen: "use" or "auto"), "auto" (as someone comes near), "use" (the player's door
  shortcuts, settings Game.doorActions, or a double click, near it; other characters open a door as they come),
  "action" (the player's input action named in "trigger", near it: toggles), "event" (the game event named in
  "trigger": toggles), "always" (plays on and on), or "open" (an open doorway: always passable, nothing to animate, no
  sheet). "prompt": what the player is shown near it ("Pull the lever"; "shut|open" for each way). "door": it's a
  doorway of its building (a way in, blocked while shut); only a building's (ontology role "building") parts are, not a
  window or a lever. A Building Plan (Interior) is only for buildings. "locked" with "key" (an item id):
  shut, its doorway blocked, until the player uses it carrying the key. "more": the building's other doors, a JSON list
  of objects with the same fields (a front and a side door). Set a door's way with set_field on AnimatedPart (the first
  door) or set_prefab_field on the building's prefab (every copy). A door's animation itself is made in Build ▸ Animate
  (doors and objects by PixelLab, characters by AutoSprite) or right-click ▸ Animate a part…, not by you.
  Ramp marks where characters walk between heights (stairs, a ramp, a ladder): elsewhere a cliff can't be walked.
- Combat (only when the game has fighting: characters come without it): MeleeAttack, RangedAttack (projectiles),
  Health, Stats, Faction/FactionMember (records "faction";
  disposition < 0 is hostile). A Spawner makes enemies from an "encounter" record around itself.
- Character AI: Behaviour holds a stack of "package" records (template: work, sleep, sandbox, patrol, follow,
  guard, ambush, script...; location, schedule, data); the first that holds runs. Perception (sight, hearing),
  aggression and confidence decide fights; a "combat-style" record says how they fight; Barks are one-line
  remarks (alternatives separated by |). SmartObject marks beds, chairs, counters for packages to use. Actor
  {key, prefab} makes one person persist across scenes (dead stays dead).
- Interior: a building one can go into, of two kinds (door: s, e, n or w, its doorway's side).
  kind "cutaway": texture is its interior picture (the same building cut away); its collision becomes its walls with
  a doorway, and while the player stands inside the interior shows in place (no Lua, no event). An asset's pair is in
  its catalog fields: interior= on the exterior, exteriorOf= on the interior; shown previews it in the editor.
  kind "scene": its interior is its own scene (scene, marker: where one arrives); the building is solid but for its
  doorway, and stepping into the doorway goes there (a Gateway of itself). Big many-roomed buildings (inns, temples,
  manors, keeps) are scenes; houses and workshops cutaways. Both kinds can be in one scene.
  A cutaway's floors: texture (floor 1) and floors ("floor 2;floor 3" pictures; fewer when the building has fewer
  storeys: storeys are walls below the eaves, a roof's dormers aren't one). stairs: each floor change's cell, "x,y;x,y"
  (scene cells; empty: its corners in turn). entries: its ways in set by hand, "dx,dy;dx,dy" cells of its footprint's
  edge from its top-left (empty: where its doors are, else its door side's middle). Change them with set_field.
- Ramp: on a cell, characters walk between ground heights there (stairs, a slope, a ladder); elsewhere a height change is a cliff.
- Prefer these systems over Lua for anything they cover.
"""

LUA_API = """\
Lua scripts (plain Lua 5.4; one environment per entity; `update(dt)` runs 60 times per
second while playing). These are ALL the engine functions — there are no others:
  self_id()                         -> id of this entity
  get_position()                    -> x, y   (two numbers)
  set_position(x, y)                   teleport, no collision
  move_toward(x, y, speed)             walk one step toward a point, with collision
  position_of(id)                   -> x, y, or nil if the entity is gone
  distance_to(id)                   -> number, or nil
  find_nearest(team)                -> id, distance (two values), or nil; team is "Player", "Enemy" or "Neutral"
  is_alive(id)                      -> boolean
  get_health()  / get_health(id)    -> current, max  (TWO numbers, not a table:
                                       local hp, max_hp = get_health())
  attack_toward(id) / attack_toward(x, y) -> boolean (false while on cooldown)
  apply_status(id, kind, seconds, magnitude)   kind: "burn", "poison", "slow", "regen"
  get_field(id, component, field)   -> value, or nil   e.g. get_field(self_id(), "Follow", "speed")
  set_field(id, component, field, value) -> boolean
  spawn(prefab_path, x, y)          -> id, or nil
  become(id, prefab_path)           -> the new id, or nil: replaced in its place (a chest opened); what's in it kept
  use(id)                           -> boolean: used as the player would (unlock, open, take, its flag/event/becomes)
  set_texture(id, texture_path)     -> boolean: only its picture changed
  start_quest(id)                      starts quests/<id>.json
  complete_objective(quest, objective) completes that objective if it's open in the quest's current stage
  quest_state(id)                   -> "inactive" | "active" | "completed" | "failed"
  quest_stage(id) / set_stage(id, stage) / fail_quest(id)
  set_flag(name[, value]) / get_flag(name) -> story flags, shared with dialogue conditions
  get_var(name) / set_var(name, value)  story variables (numbers, text, booleans)
  used(name)                           counts as using something (use/examine objectives)
  play_cutscene(id) -> boolean / skip_cutscene() / in_cutscene() -> boolean
  difficulty() -> level id / difficulty_step(category) -> 1..5 / set_difficulty(id) -> boolean
  notify(channel, text[, seconds]) / open_screen(id) -> boolean / close_screen(id) / is_open(id) -> boolean
  play_effect(id, x, y) or play_effect(id, entity) -> boolean / emit_particles(id, x, y[, seconds])
  shake(trauma) / hitstop(seconds) / slow_motion(scale, seconds) / anim_trigger(id, "attacking"|"casting"|...)
  tween(id, "Component.field", to, seconds[, ease]) -> handle / tween_done(handle) -> boolean
  cast(ability[, target id or x, y]) -> boolean / apply_effect(id, effect[, magnitude, duration]) -> boolean
  remove_effects(id, tag) / has_tag(id, tag) / add_tag(id, tag) / cooldown_left(ability) / learn(id, ability)
  raycast(x0, y0, x1, y1[, mask]) -> {entity, x, y, distance} or nil / overlap(x, y, radius[, mask]) -> ids
  line_of_sight(x0, y0, x1, y1) -> boolean / apply_impulse(id, ix, iy) / set_velocity(id, vx, vy)
  time_of_day()                     -> hours since midnight (fractional), e.g. 13.5
  day()                             -> the day number, from 1
  play_sound(asset_path) / play_sound_at(asset_path, x, y) / play_music(asset_path, or "" for the scene's)
  record(type, id) -> table or nil / records(type) -> list of ids
  on(event, fn) -> handle   fn(e) with e.name, e.entity, e.other, e.amount, e.text; "*" for every event
  emit(event[, {entity=, other=, amount=, text=}])
  after(seconds, fn) / every(seconds, fn) -> handle;  cancel(handle)
  start(fn)   runs fn as a coroutine;  wait(seconds) pauses it (handlers and timers may wait too)
  action_down(name) / action_pressed(name) -> boolean;  axis(name) -> -1..1   (input actions, not keys)
  attribute(id, name) -> number;  add_modifier(id, attribute, value[, seconds, source, "multiply"])
  remove_modifiers(id, source)
  give_item(id, item[, n]) / take_item(id, item[, n]) / count_item(id, item)   ("gold" for gold)
  equip(id, item) / use_item(id, item) -> ok, reason
  award_xp(id, n) / level(id) / take_perk(id, perk);  drop_loot(x, y, loot_table[, level])
  shoot(x, y) / dodge(dx, dy);  is_hostile(a, b) -> boolean;  set_disposition(id, n)
  push_package(id, package) / pop_package(id, package);  bark(text);  awareness_of([id]) -> 0..1
  set_visible(id, visible) / face(id);  start_dialogue(path) -> boolean;  find_object(type[, radius]) -> id
  move_to(x, y)   (in a scripted package's run() coroutine: walks there around walls)
Events: hit, death, pickup, attack, victory, talk, item_added, level_up, interact, bark, quest_started,
quest_completed, quest_failed, quest_stage, objective_completed, cutscene_started, cutscene_ended,
difficulty_changed, ui_action (text: the action's name), anim_event (text: a frame event), effect, ability_used,
ability_learned, effect_applied, effect_removed (text: the ability's or effect's id).
Entity ids are always the first argument where one is taken; prefab values can't be read
from scripts (remember a value in a local at load time if you need it later, e.g.
`local normal_speed = nil` then set it on the first update). A top-level
`properties = { name = default, ... }` table becomes per-entity settings, read inside
functions as properties.name.
World units: one tile = 1.0; cell centers are n + 0.5.
"""


def _field_line(spec: FieldSpec) -> str:
    parts = [f"{spec.name}: {spec.type}"]
    if spec.options:
        parts.append("one of " + "|".join(spec.options))
    if spec.min is not None or spec.max is not None:
        low = "" if spec.min is None else f"{spec.min:g}"
        high = "" if spec.max is None else f"{spec.max:g}"
        parts.append(f"[{low}..{high}]")
    if spec.optional:
        parts.append("(optional)")
    line = " ".join(parts)
    return f"{line} — {spec.description}" if spec.description else line


def describe_schema(schema: EngineSchema) -> str:
    """The schema as compact text: far fewer tokens than the JSON, same facts."""
    lines = ["COMPONENTS"]
    for component in schema.components:
        flags = "" if component.userEditable else " (structural: can't be added/removed)"
        lines.append(f"- {component.name}{flags}: {component.description}")
        for field in component.fields:
            lines.append(f"    {_field_line(field)}")
    lines.append("SETTINGS")
    for section in schema.settings:
        lines.append(f"- {section.name}: {section.description}")
        for field in section.fields:
            lines.append(f"    {_field_line(field)}")
    lines.append("COMMANDS")
    for command in schema.commands:
        params = ", ".join(_field_line(p) for p in command.params)
        lines.append(f"- {command.name}({params}): {command.description}")
    if schema.records:
        lines.append("RECORD TYPES")
        for record_type in schema.records:
            lines.append(f"- {record_type.name}: {record_type.description}")
            for field in record_type.fields:
                lines.append(f"    {_record_field_line(field)}")
    return "\n".join(lines)


def _record_field_line(spec: RecordFieldSpec, indent: str = "") -> str:
    kind = spec.type
    if spec.type == "record":
        kind = f"record of {spec.recordType}"
    if spec.list:
        kind = f"list of {kind}"
    parts = [f"{spec.name}: {kind}"]
    if spec.options:
        parts.append("one of " + "|".join(spec.options))
    if spec.min is not None or spec.max is not None:
        low = "" if spec.min is None else f"{spec.min:g}"
        high = "" if spec.max is None else f"{spec.max:g}"
        parts.append(f"[{low}..{high}]")
    if spec.required:
        parts.append("(required)")
    line = " ".join(parts)
    line = f"{line} — {spec.description}" if spec.description else line
    for sub in spec.fields:
        line += f"\n    {indent}  {_record_field_line(sub, indent + '  ')}"
    return line


def build_messages(
    schema: EngineSchema,
    context: EditorContext,
    instruction: str,
    history: list[dict] | None = None,
    canon: str = "",
    images: list[dict] | None = None,
) -> list[dict]:
    """`images`: pictures the model asked to look at (its look tool), as image chunks on the request."""
    system = "\n\n".join(
        [
            "You are the editing assistant inside a 2D game engine (isometric or top-down, any kind of game). You turn the "
            "developer's requests into precise, reviewable edits of their project. You never edit anything "
            "directly: you propose operations, the developer reviews them in a diff view, and the engine "
            "applies the accepted ones through the same code its manual editor uses.",
            OUTPUT_FORMAT,
            STORY,
            ROUTINES,
            CUTSCENES,
            GAMEPLAY,
            DIFFICULTY,
            GAME_UI,
            FX,
            MAGIC,
            PHYSICS,
            STORY_GRAPH,
            PLANNER,
            SHIPPING,
            RECORDS,
            SCENES,
            ART,
            LUA_API,
            "ENGINE SCHEMA (everything that can be changed)\n" + describe_schema(schema),
        ]
    )
    project = context.model_dump(exclude_none=True)
    focus = f"\nThe developer asked about {context.focus} specifically.\n" if context.focus else ""
    # The canon most related to this request, in full (the context lists every
    # entry by name and summary only).
    canon_block = f"\n\nWORLD BIBLE (canon most related to this request, in full)\n{canon}" if canon else ""
    user = (
        "PROJECT CONTEXT (current state of the open project)\n"
        + json.dumps(project, separators=(",", ":"))
        + focus
        + canon_block
        + "\n\nREQUEST\n"
        + instruction
    )
    messages = [{"role": "system", "content": system}]
    messages += history or []
    if images:
        messages.append({"role": "user", "content": [{"type": "text", "text": user}] + list(images)})
    else:
        messages.append({"role": "user", "content": user})
    return messages


def repair_message(errors: list[str]) -> dict:
    hint = ""
    if any("doesn't exist" in e and "entity" in e for e in errors):
        hint = (
            "\nEntity ids are only those in the project context. Something spawned in this answer has no id"
            " yet: spawn it now and change it in the next step (scope \"task\"), when it appears in the context"
            " with its id; or spawn a prefab that already has what it needs."
        )
    return {
        "role": "user",
        "content": "That proposal can't be applied:\n- "
        + "\n- ".join(errors)
        + hint
        + "\nAnswer again with a corrected JSON object only.",
    }
