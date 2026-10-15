"""Builds the model's messages: what the engine is, what it can change (the
reflection schema, compacted), what's in the open project, and exactly what
JSON to answer with."""

from __future__ import annotations

import json

from .models import EditorContext, EngineSchema, FieldSpec

OUTPUT_FORMAT = """\
Answer with ONE JSON object and nothing else:
{
  "scope": "small" | "task" | "split",
  "summary": "one sentence describing the change (for a task: this step), for the review panel",
  "plan": ["only for a task that benefits from planning: 2-6 short steps"],
  "operations": [ ...operations, applied in order... ],
  "done": true/false (task only: true on the step that finishes the request),
  "split": ["only for split: smaller, self-contained requests, in the user's words"],
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
- "split": too big for one task — a whole game, many unrelated features, or more than about
  six steps of work. Don't start: give no operations, suggest 2-6 smaller requests in
  "split" that together cover it (each one a sensible request on its own), and explain why
  in "notes".

Operations (use exact component, field, setting and command names from the schema):
  {"op": "set_field", "entity": <id>, "component": "Health", "field": "max", "value": 20}
  {"op": "add_component", "entity": <id>, "component": "Follow", "fields": {"speed": 2}}
  {"op": "remove_component", "entity": <id>, "component": "Follow"}
  {"op": "set_prefab_field", "prefab": "prefabs/skeleton.prefab", "component": "Health", "field": "max", "value": 20}
  {"op": "set_setting", "section": "Combat", "field": "knockbackScale", "value": 1.5}
  {"op": "set_script_property", "entity": <id>, "name": "swing_range", "value": 2}
  {"op": "command", "name": "<command name>", "args": {"<param>": <value>, ...}}

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
- Ids exist only for entities already in the project context. Something spawned in this
  answer has no id yet: to change it, do that in the next step of a task.
- A new prefab: set up an existing entity first (set_field/add_component, in an earlier
  step if it was just spawned), then prefab.createFromEntity — the prefab copies the
  entity as it is. Don't set_prefab_field on a prefab created in the same answer.
- Every operation is a JSON object with "op" set to one of the seven kinds above; commands
  always use {"op": "command", "name": ..., "args": {...}}.
- Make the smallest change that does what was asked. If the request can't be done with
  what's available, return no operations and explain why in "notes".
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
  start_quest(id)                      starts quests/<id>.json
  complete_objective(quest, objective) completes the quest's current objective if it's that one
  quest_state(id)                   -> "inactive" | "active" | "completed"
  set_flag(name[, value]) / get_flag(name) -> story flags, shared with dialogue conditions
  play_sound(asset_path)
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
    return "\n".join(lines)


def build_messages(
    schema: EngineSchema, context: EditorContext, instruction: str, history: list[dict] | None = None
) -> list[dict]:
    system = "\n\n".join(
        [
            "You are the editing assistant inside an isometric 2.5D action-RPG game engine. You turn the "
            "developer's requests into precise, reviewable edits of their project. You never edit anything "
            "directly: you propose operations, the developer reviews them in a diff view, and the engine "
            "applies the accepted ones through the same code its manual editor uses.",
            OUTPUT_FORMAT,
            LUA_API,
            "ENGINE SCHEMA (everything that can be changed)\n" + describe_schema(schema),
        ]
    )
    project = context.model_dump(exclude_none=True)
    focus = f"\nThe developer asked about {context.focus} specifically.\n" if context.focus else ""
    user = (
        "PROJECT CONTEXT (current state of the open project)\n"
        + json.dumps(project, separators=(",", ":"))
        + focus
        + "\n\nREQUEST\n"
        + instruction
    )
    messages = [{"role": "system", "content": system}]
    messages += history or []
    messages.append({"role": "user", "content": user})
    return messages


def repair_message(errors: list[str]) -> dict:
    return {
        "role": "user",
        "content": "That proposal can't be applied:\n- "
        + "\n- ".join(errors)
        + "\nAnswer again with a corrected JSON object only.",
    }
