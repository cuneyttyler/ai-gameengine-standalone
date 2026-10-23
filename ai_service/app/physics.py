"""Checks for physics v2 (development_plan.md 20, docs/systems/physics.md): collision layers and
masks, triggers, rigid bodies and joints the AI adds (mirroring engine/gameplay/Physics)."""

from __future__ import annotations

from typing import Any

# Layers every game has (engine/gameplay/Physics.cpp LayerOf).
BUILT_IN_LAYERS = {"world", "player", "enemy", "ally", "projectile", "trigger", "default"}


def _layers(value: Any) -> list[str]:
    return [part.strip() for part in str(value or "").split(",") if part.strip()]


def check_physics(proposal, context) -> list[str]:
    from .models import AddComponent, SetField, SetPrefabField

    physics = context.physics or {}
    known = BUILT_IN_LAYERS | set(physics.get("layers", []))
    # Layers this proposal puts colliders on count as known.
    for op in proposal.operations:
        if isinstance(op, AddComponent) and op.component == "Collider2D":
            known |= set(_layers(op.fields.get("layer")))
        elif isinstance(op, (SetField, SetPrefabField)) and op.component == "Collider2D" and op.field == "layer":
            known |= set(_layers(op.value))

    components = {e.id: set(e.components) for e in context.entities}
    for op in proposal.operations:
        if isinstance(op, AddComponent):
            components.setdefault(op.entity, set()).add(op.component)

    errors: list[str] = []
    for index, op in enumerate(proposal.operations, start=1):
        where = f"operation {index} ({op.op})"
        masks: list[tuple[str, Any]] = []
        if isinstance(op, AddComponent):
            if op.component in ("Collider2D", "Trigger"):
                masks.append((f"{op.component}.mask", op.fields.get("mask")))
            if op.component == "Trigger" and not str(op.fields.get("name", "")).strip():
                errors.append(f"{where} Trigger: give it a name (scripts hear it in trigger_enter / trigger_exit)")
            if op.component in ("RigidBody2D",) and "Collider2D" not in components.get(op.entity, set()):
                errors.append(f"{where} RigidBody2D: entity {op.entity} needs a Collider2D (its shape) too")
            if op.component == "Joint2D":
                other = op.fields.get("other")
                if other is None or "RigidBody2D" not in components.get(other, set()):
                    errors.append(f"{where} Joint2D: 'other' must be an entity with a RigidBody2D")
                if "RigidBody2D" not in components.get(op.entity, set()):
                    errors.append(f"{where} Joint2D: entity {op.entity} needs a RigidBody2D")
        elif isinstance(op, (SetField, SetPrefabField)) and op.field == "mask" and op.component in ("Collider2D", "Trigger"):
            masks.append((f"{op.component}.mask", op.value))
        for label, value in masks:
            for layer in _layers(value):
                if layer not in known:
                    errors.append(f"{where} {label}: no layer {layer!r} ({', '.join(sorted(known))}, or put a "
                                  f"Collider2D on it first)")
    return errors
