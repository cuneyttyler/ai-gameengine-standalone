"""Checks for abilities and magic (development_plan.md 19, docs/magic.md): ability and
magic-effect records the AI writes (mirroring engine/gameplay/Magic ValidateAbility), and
the abilities an Abilities component or an item names."""

from __future__ import annotations

import re
from typing import Any

ACTIVATIONS = {"press", "hold", "toggle", "passive"}
DELIVERIES = {"self", "touch", "projectile", "area", "cone", "aura"}
ARCHETYPES = {"damage", "heal", "modify", "over-time", "absorb", "state", "summon", "displace", "dispel", "light"}
HINT_TERMS = {"health", "mana", "stamina", "distance", "enemyHealth"}
COST_POOLS = {"mana", "stamina", "health"}


def _ids(seed: list[str], records: list[dict], kind: str, written: list) -> set[str]:
    ids = set(seed)
    ids |= {r.get("id") for r in records if r.get("type") == kind}
    ids |= {op.id for op in written if getattr(op, "type", None) == kind}
    return ids


def check_magic(proposal, context) -> list[str]:
    from .models import AddComponent, SetField, SetPrefabField, WriteRecord

    magic = context.magic or {}
    if not magic:
        return []
    writes = [op for op in proposal.operations if isinstance(op, WriteRecord)]
    abilities = _ids(magic.get("abilities", []), context.records, "ability", writes)
    effects = _ids(magic.get("effects", []), context.records, "magic-effect", writes)
    errors: list[str] = []
    for index, op in enumerate(proposal.operations, start=1):
        where = f"operation {index} ({op.op})"
        if isinstance(op, WriteRecord) and op.type == "ability":
            errors += _check_ability(op.record, effects, f"{where} ability {op.id}")
        elif isinstance(op, WriteRecord) and op.type == "magic-effect":
            if op.record.get("archetype", "damage") not in ARCHETYPES:
                errors.append(f"{where} magic-effect {op.id}: archetype is one of {', '.join(sorted(ARCHETYPES))}")
            magnitude = op.record.get("magnitude", "10")
            if isinstance(magnitude, str) and not re.fullmatch(r"[\w\s.+\-*/()]*", magnitude):
                errors.append(f"{where} magic-effect {op.id}: magnitude is a number or a formula (+ - * / and caster.level)")
        elif isinstance(op, WriteRecord) and op.type == "item":
            use = op.record.get("use") or {}
            for key in ("learn", "ability"):
                if isinstance(use, dict) and use.get(key) and use[key] not in abilities:
                    errors.append(f"{where} item {op.id}: no ability {use[key]!r} for use.{key}")
            names = list((use.get("effects") or []) if isinstance(use, dict) else [])
            ench = op.record.get("enchantment") or {}
            if isinstance(ench, dict):
                names += ench.get("effects") or []
                if ench.get("on", "worn") not in ("worn", "hit"):
                    errors.append(f"{where} item {op.id}: enchantment.on is worn or hit")
            for name in names:
                if name not in effects:
                    errors.append(f"{where} item {op.id}: no magic effect {name!r}")
        value = None
        if isinstance(op, AddComponent) and op.component == "Abilities":
            value = " , ".join(str(op.fields.get(k, "")) for k in ("known", "slots"))
        elif isinstance(op, (SetField, SetPrefabField)) and op.component == "Abilities":
            value = str(op.value)
        if value is not None:
            for name in [n.strip() for n in value.split(",") if n.strip()]:
                if name not in abilities:
                    errors.append(f"{where} Abilities: no ability {name!r} (write it, or pick one from context magic)")
    return errors


def _check_ability(record: dict[str, Any], effects: set[str], where: str) -> list[str]:
    errors = []
    if record.get("activation", "press") not in ACTIVATIONS:
        errors.append(f"{where}: activation is press, hold, toggle or passive")
    if record.get("delivery", "self") not in DELIVERIES:
        errors.append(f"{where}: delivery is self, touch, projectile, area, cone or aura")
    uses = record.get("effects") or []
    if not uses:
        errors.append(f"{where}: has no effects")
    for use in uses:
        name = use if isinstance(use, str) else (use or {}).get("effect")
        if name not in effects:
            errors.append(f"{where}: no magic effect {name!r}")
    cost = record.get("cost")
    if isinstance(cost, dict) and cost.get("attribute", "mana") not in COST_POOLS:
        errors.append(f"{where}: cost is paid in mana, stamina or health")
    ai = record.get("ai")
    if isinstance(ai, dict) and ai.get("when"):
        for name in re.findall(r"[A-Za-z_]+", ai["when"]):
            if name not in HINT_TERMS:
                errors.append(f"{where}: the AI hint asks about {name!r} (health, mana, stamina, distance, enemyHealth)")
    return errors
