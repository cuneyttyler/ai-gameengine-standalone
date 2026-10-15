"""Builds the model's messages: what the engine is, what it can change (the
reflection schema, compacted), what's in the open project, and exactly what
JSON to answer with."""

from __future__ import annotations

import json

from .models import EditorContext, EngineSchema, FieldSpec

OUTPUT_FORMAT = """\
Answer with ONE JSON object and nothing else:
{
  "summary": "one sentence describing the change, for the review panel",
  "operations": [ ...operations, applied in order... ],
  "notes": "optional: anything the user should know (assumptions, how to test it)"
}

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
- set_field needs the component to already be on the entity (add_component otherwise).
- New behavior goes in Lua: write it with the script.write command (path scripts/<name>.lua),
  then attach it with add_component LuaScript {"path": ...} or set_field LuaScript.path.
- Make the smallest change that does what was asked. If the request can't be done with
  what's available, return no operations and explain why in "notes".
"""

LUA_API = """\
Lua scripts (one environment per entity; `update(dt)` runs 60 times per second while playing):
  self_id() -> id                          get_position() -> x, y        set_position(x, y)
  move_toward(x, y, speed)                 position_of(id) -> x, y       distance_to(id) -> number
  find_nearest("Player"|"Enemy"|"Neutral") -> id, distance             is_alive(id) -> bool
  get_health([id]) -> current, max         attack_toward(id) / attack_toward(x, y) -> bool
  apply_status(id, "burn"|"poison"|"slow"|"regen", seconds, magnitude)
  get_field(id, "Component", "field")      set_field(id, "Component", "field", value)
  spawn("prefabs/x.prefab", x, y) -> id    play_sound("assets/audio/x.wav")
  A top-level `properties = { name = default, ... }` table becomes per-entity settings,
  read inside functions as properties.name.
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
