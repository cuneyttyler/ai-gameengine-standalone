"""Checks a proposal against the engine schema and the editor context before
it leaves the service — the service-side half of the defense in depth in
docs/technical.md §7.5 (the editor validates again against its live registry).

Errors are plain sentences so they can be fed back to the model for a repair
attempt and shown to the user.
"""

from __future__ import annotations

import json
import math
import re
from typing import Any

from .models import (
    AddComponent,
    EditorContext,
    EngineSchema,
    FieldSpec,
    Proposal,
    RemoveComponent,
    RunCommand,
    SetField,
    SetPrefabField,
    SetScriptProperty,
    SetSetting,
    WriteDialogue,
    WriteQuest,
    WriteSchedule,
    WriteCutscene,
    WriteUi,
    WriteAnim,
    WriteRecord,
    DeleteRecord,
)
from .records import check_record
from .fx import check_anim, check_fx_refs
from .magic import check_magic
from .gameui import check_ui
from .story import StoryScope, check_cutscene, check_dialogue, check_quest, check_schedule

_SAFE_SCRIPT = re.compile(r"^scripts/[A-Za-z0-9_\-/]+\.lua$")


def check_value(spec: FieldSpec, value: Any, context: EditorContext, where: str) -> list[str]:
    """Type and constraint check for one value, mirroring the engine's Validate()."""
    errors: list[str] = []
    t = spec.type
    if t in ("float", "int"):
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
            return [f"{where}: {spec.name} must be a number"]
        if t == "int" and float(value) != math.floor(value):
            return [f"{where}: {spec.name} must be a whole number"]
        if spec.min is not None and value < spec.min:
            errors.append(f"{where}: {spec.name} must be at least {spec.min:g}")
        if spec.max is not None and value > spec.max:
            errors.append(f"{where}: {spec.name} must be at most {spec.max:g}")
    elif t == "bool":
        if not isinstance(value, bool):
            errors.append(f"{where}: {spec.name} must be true or false")
    elif t == "enum":
        if value not in (spec.options or []):
            errors.append(f"{where}: {spec.name} must be one of {', '.join(spec.options or [])}")
    elif t == "entity":
        ids = {e.id for e in context.entities}
        if value is not None and value not in ids:
            errors.append(f"{where}: {spec.name} refers to entity {value}, which doesn't exist")
    else:
        if not isinstance(value, str):
            return [f"{where}: {spec.name} must be text"]
        if t == "prefab" and value and value not in {p.path for p in context.prefabs}:
            errors.append(f"{where}: no prefab {value!r}")
        elif t == "texture" and value and value not in {a["path"] for a in context.assets if a.get("type") != "audio"}:
            errors.append(f"{where}: no texture {value!r}")
        elif t == "audio" and value and value not in {a["path"] for a in context.assets if a.get("type") == "audio"}:
            errors.append(f"{where}: no audio asset {value!r}")
        elif t == "component" and not value:
            errors.append(f"{where}: {spec.name} is required")
    return errors


def validate_proposal(proposal: Proposal, schema: EngineSchema, context: EditorContext) -> list[str]:
    errors: list[str] = []
    if proposal.scope == "split":
        if proposal.operations:
            errors.append('a "split" answer has no operations: suggest the smaller requests in "split" instead')
        if len(proposal.split) < 2:
            errors.append('a "split" answer needs at least two smaller requests in "split"')
        return errors
    entities = {e.id: e for e in context.entities}
    written_scripts: set[str] = set()

    for index, op in enumerate(proposal.operations, start=1):
        where = f"operation {index} ({op.op})"

        if isinstance(op, (SetField, AddComponent, RemoveComponent)):
            if op.entity not in entities:
                errors.append(f"{where}: entity {op.entity} doesn't exist")
                continue
            component = schema.component(op.component)
            if component is None:
                errors.append(f"{where}: unknown component {op.component!r}")
                continue
            has = op.component in entities[op.entity].components
            if isinstance(op, SetField):
                spec = component.field(op.field)
                if spec is None:
                    errors.append(f"{where}: {op.component} has no field {op.field!r}")
                elif not has:
                    errors.append(f"{where}: entity {op.entity} has no {op.component} (add it first)")
                else:
                    errors += check_value(spec, op.value, context, where)
            elif isinstance(op, AddComponent):
                if not component.userEditable:
                    errors.append(f"{where}: {op.component} can't be added by hand")
                for name, value in op.fields.items():
                    spec = component.field(name)
                    if spec is None:
                        errors.append(f"{where}: {op.component} has no field {name!r}")
                    else:
                        errors += check_value(spec, value, context, where)
            elif isinstance(op, RemoveComponent):
                if not has:
                    errors.append(f"{where}: entity {op.entity} has no {op.component}")
                elif not component.userEditable:
                    errors.append(f"{where}: {op.component} can't be removed by hand")

        elif isinstance(op, SetPrefabField):
            prefab = next((p for p in context.prefabs if p.path == op.prefab), None)
            component = schema.component(op.component)
            if prefab is None:
                errors.append(f"{where}: no prefab {op.prefab!r}")
            elif component is None or component.field(op.field) is None:
                errors.append(f"{where}: unknown field {op.component}.{op.field}")
            elif op.component not in prefab.components:
                errors.append(f"{where}: {op.prefab} has no {op.component}")
            else:
                errors += check_value(component.field(op.field), op.value, context, where)

        elif isinstance(op, SetSetting):
            section = schema.settings_section(op.section)
            spec = section.field(op.field) if section else None
            if spec is None:
                errors.append(f"{where}: no setting {op.section}.{op.field}")
            else:
                errors += check_value(spec, op.value, context, where)

        elif isinstance(op, SetScriptProperty):
            if op.entity not in entities:
                errors.append(f"{where}: entity {op.entity} doesn't exist")
            elif "LuaScript" not in entities[op.entity].components:
                errors.append(f"{where}: entity {op.entity} has no script")

        elif isinstance(op, RunCommand):
            command = schema.command(op.name)
            if command is None:
                errors.append(f"{where}: unknown command {op.name!r}")
                continue
            for param in command.params:
                if param.name not in op.args:
                    if not param.optional:
                        errors.append(f"{where}: {op.name} needs {param.name!r}")
                    continue
                errors += check_value(param, op.args[param.name], context, where)
            extra = set(op.args) - {p.name for p in command.params}
            if extra:
                errors.append(f"{where}: {op.name} doesn't take {', '.join(sorted(extra))}")
            if op.name == "script.write":
                path = op.args.get("path", "")
                if not isinstance(path, str) or not _SAFE_SCRIPT.match(path) or ".." in path:
                    errors.append(f"{where}: scripts must be written to scripts/<name>.lua")
                else:
                    written_scripts.add(path)

    # Dialogue and quests, checked against the project plus what this
    # proposal adds (quests it writes, World Bible entries it writes).
    quests = {q.get("id", ""): q for q in context.quests}
    world_keys = {e.get("key", "") for e in context.world}
    for op in proposal.operations:
        if isinstance(op, WriteQuest):
            quests[op.id] = op.quest
        elif isinstance(op, RunCommand) and op.name == "world.write":
            entry_id = op.args.get("id") or re.sub(r"[^a-z0-9_]+", "-", str(op.args.get("name", "")).lower()).strip("-")
            world_keys.add(f"{op.args.get('kind', '')}/{entry_id}")
    # Every scene's markers; the open one's with those this proposal adds.
    added_markers = {str(op.args.get("name", "")) for op in proposal.operations
                     if isinstance(op, RunCommand) and op.name == "scene.addMarker"}
    open_markers = {m.get("name", "") for m in context.markers} | added_markers
    scene_markers = {str(s.get("path", "")): set(s.get("markers", [])) for s in context.scenes}
    if context.scene:
        scene_markers[context.scene] = scene_markers.get(context.scene, set()) | open_markers
    scope = StoryScope(quests, world_keys, {p.path for p in context.prefabs}, scene_markers if context.scenes else None)
    all_markers = set().union(*scene_markers.values()) | open_markers if context.scenes else None
    for index, op in enumerate(proposal.operations, start=1):
        if isinstance(op, WriteDialogue):
            errors += check_dialogue(op.id, op.dialogue, scope, f"operation {index} (write_dialogue {op.id})")
        elif isinstance(op, WriteQuest):
            errors += check_quest(op.id, op.quest, scope, f"operation {index} (write_quest {op.id})")
        elif isinstance(op, WriteCutscene):
            errors += check_cutscene(op.id, op.cutscene, scope, all_markers, f"operation {index} (write_cutscene {op.id})")

    # Animation and effects (18): sheets, and the particles, effects and sheets components name.
    for index, op in enumerate(proposal.operations, start=1):
        if isinstance(op, WriteAnim):
            errors += check_anim(op.path, op.sheet, {a.get("path") for a in context.assets},
                                 f"operation {index} (write_anim {op.path})")
    errors += check_fx_refs(proposal, context)
    errors += check_magic(proposal, context)  # abilities and magic (19)

    # Game UI files (17).
    for index, op in enumerate(proposal.operations, start=1):
        if isinstance(op, WriteUi):
            errors += check_ui(op.path, op.content, list(context.ui.get("themes", [])), f"operation {index} (write_ui {op.path})")

    # DifficultyOverrides (16.2): JSON text, field -> level -> number, on scalable
    # fields and levels that exist (or are written here).
    level_ids = {str(level.get("id", "")) for level in context.difficulty.get("levels", [])}
    written_levels = {op.id for op in proposal.operations if isinstance(op, WriteRecord) and op.type == "difficulty"}
    if written_levels:
        level_ids = written_levels | {lid for lid in level_ids if not context.difficulty.get("builtIn", False)}
    scalable = {f"{c.name}.{f.name}" for c in schema.components for f in c.fields if f.scalable}
    for index, op in enumerate(proposal.operations, start=1):
        text = None
        if isinstance(op, AddComponent) and op.component == "DifficultyOverrides":
            text = op.fields.get("values")
        elif isinstance(op, (SetField, SetPrefabField)) and op.component == "DifficultyOverrides" and op.field == "values":
            text = op.value
        if text is None:
            continue
        where = f"operation {index} ({op.op} DifficultyOverrides)"
        try:
            values = json.loads(text) if isinstance(text, str) and text else {}
        except ValueError:
            errors.append(f"{where}: values must be JSON text like {{\"Health.max\": {{\"hard\": 40}}}}")
            continue
        if not isinstance(values, dict):
            errors.append(f"{where}: values must be a JSON object")
            continue
        for field, per_level in values.items():
            if field not in scalable:
                errors.append(f"{where}: {field} isn't a scalable field")
            if not isinstance(per_level, dict):
                errors.append(f"{where}: {field} needs {{level id: number}}")
                continue
            for level, number in per_level.items():
                if level_ids and level not in level_ids:
                    errors.append(f"{where}: no difficulty level {level!r}")
                if not isinstance(number, (int, float)) or isinstance(number, bool):
                    errors.append(f"{where}: {field} at {level} must be a number")

    # play_cutscene effects and CutsceneTrigger components name a cutscene that
    # exists or is written here.
    cutscenes = {str(c.get("id", "")) for c in context.cutscenes}
    cutscenes |= {op.id for op in proposal.operations if isinstance(op, WriteCutscene)}
    def _effects_in(value: Any):
        if isinstance(value, dict):
            if value.get("op") == "play_cutscene":
                yield value
            for v in value.values():
                yield from _effects_in(v)
        elif isinstance(value, list):
            for v in value:
                yield from _effects_in(v)
    for index, op in enumerate(proposal.operations, start=1):
        body = getattr(op, "dialogue", None) or getattr(op, "quest", None) or getattr(op, "cutscene", None)
        for effect in _effects_in(body):
            if effect.get("cutscene") not in cutscenes:
                errors.append(f"operation {index} ({op.op}): cutscene {effect.get('cutscene')!r} doesn't exist "
                              "and isn't written here (write_cutscene)")
        trigger = None
        if isinstance(op, AddComponent) and op.component == "CutsceneTrigger":
            trigger = op.fields.get("cutscene")
        elif isinstance(op, (SetField, SetPrefabField)) and op.component == "CutsceneTrigger" and op.field == "cutscene":
            trigger = op.value
        if isinstance(trigger, str) and trigger and trigger not in cutscenes:
            errors.append(f"operation {index} ({op.op}): cutscene {trigger!r} doesn't exist and isn't written here")

    # Routines: places must be markers in the scene or added here; a Routine
    # must point at a schedule that exists or is written here.
    markers = {m.get("name", "") for m in context.markers}
    markers |= {str(op.args.get("name", "")) for op in proposal.operations
                if isinstance(op, RunCommand) and op.name == "scene.addMarker"}
    schedules = {f"schedules/{s.get('id', '')}.json" for s in context.schedules}
    for index, op in enumerate(proposal.operations, start=1):
        if isinstance(op, WriteSchedule):
            errors += check_schedule(op.id, op.schedule, markers, f"operation {index} (write_schedule {op.id})",
                                     scene_markers if context.scenes else None)
            schedules.add(f"schedules/{op.id}.json")
    for index, op in enumerate(proposal.operations, start=1):
        value = None
        if isinstance(op, SetField) and op.component == "Routine" and op.field == "schedule":
            value = op.value
        elif isinstance(op, AddComponent) and op.component == "Routine":
            value = op.fields.get("schedule")
        elif isinstance(op, SetPrefabField) and op.component == "Routine" and op.field == "schedule":
            value = op.value
        if isinstance(value, str) and value and value not in schedules:
            errors.append(f"operation {index} ({op.op}): schedule {value!r} doesn't exist and isn't written here")

    # A Talk component must point at a dialogue that exists or is written here.
    dialogues = {f"dialogue/{d.get('id', '')}.json" for d in context.dialogues}
    dialogues |= {f"dialogue/{op.id}.json" for op in proposal.operations if isinstance(op, WriteDialogue)}
    for index, op in enumerate(proposal.operations, start=1):
        value = None
        if isinstance(op, SetField) and op.component == "Talk" and op.field == "dialogue":
            value = op.value
        elif isinstance(op, AddComponent) and op.component == "Talk":
            value = op.fields.get("dialogue")
            character = op.fields.get("character")
            if character and character not in world_keys:
                errors.append(f"operation {index} ({op.op}): Talk character {character!r} isn't in the World Bible")
        if isinstance(value, str) and value and value not in dialogues:
            errors.append(f"operation {index} ({op.op}): dialogue {value!r} doesn't exist and isn't written here")

    # A script attached in this proposal must exist or be written by it.
    known_scripts = set(context.scripts) | written_scripts
    for index, op in enumerate(proposal.operations, start=1):
        value = None
        if isinstance(op, SetField) and op.component == "LuaScript" and op.field == "path":
            value = op.value
        elif isinstance(op, AddComponent) and op.component == "LuaScript":
            value = op.fields.get("path")
        if isinstance(value, str) and value and value not in known_scripts:
            errors.append(f"operation {index} ({op.op}): script {value!r} doesn't exist and isn't written here")

    # Records (12.1): checked against their types; references may name records
    # the project has or this proposal writes.
    existing_records = {(str(r.get("type", "")), str(r.get("id", ""))): r.get("record") for r in context.records}
    written_records = {(op.type, op.id): op.record for op in proposal.operations if isinstance(op, WriteRecord)}

    def record_exists(record_type: str, record_id: str) -> bool:
        return (record_type, record_id) in existing_records or (record_type, record_id) in written_records

    for index, op in enumerate(proposal.operations, start=1):
        if isinstance(op, WriteRecord):
            where = f"operation {index} (write_record {op.type}/{op.id})"
            spec = schema.record_type(op.type)
            if spec is None:
                known = ", ".join(r.name for r in schema.records) or "none"
                errors.append(f"{where}: unknown record type {op.type!r} (types: {known})")
                continue
            base_id = op.record.get("base") if isinstance(op.record, dict) else None
            base = None
            if isinstance(base_id, str):
                base = written_records.get((op.type, base_id)) or existing_records.get((op.type, base_id))
            errors += check_record(spec, op.id, op.record, record_exists, base if isinstance(base, dict) else None,
                                   where)
        elif isinstance(op, DeleteRecord):
            where = f"operation {index} (delete_record {op.type}/{op.id})"
            if (op.type, op.id) not in existing_records:
                errors.append(f"{where}: there is no {op.type} {op.id!r} to delete")

    return errors
