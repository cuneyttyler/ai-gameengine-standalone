"""Golden tests (development_plan.md 2.5): fixed prompts -> expected diff shape.

Each tests/golden/<name>.json holds what the real model answered for a fixed
prompt against the demo project (see tests/golden/record.py). Replaying those
answers through the service checks, offline and deterministically, that they
parse and validate against the current schema, and that each diff has the
shape the request calls for. Re-record when the prompt or schema changes and
review the new answers before committing.
"""

import json
from pathlib import Path

import pytest

from app.models import EditorContext, Proposal
from app.providers import ScriptedProvider
from app.service import propose

GOLDEN = Path(__file__).parent / "golden"
FIXTURES = Path(__file__).parent / "fixtures"


@pytest.fixture(scope="module")
def demo_context() -> EditorContext:
    return EditorContext.model_validate_json((FIXTURES / "demo_context.json").read_text(encoding="utf-8"))


def replay(name: str, schema, demo_context) -> Proposal:
    record = json.loads((GOLDEN / f"{name}.json").read_text(encoding="utf-8"))
    context = demo_context.model_copy(update={"focus": record.get("focus")})
    result = propose(ScriptedProvider(list(record["responses"])), schema, context, record["instruction"])
    assert result.errors == [], result.errors
    assert result.proposal is not None
    return result.proposal


def ops(proposal: Proposal, kind: str) -> list:
    return [op for op in proposal.operations if op.op == kind]


def entity_named(context: EditorContext, name: str) -> int:
    return next(e.id for e in context.entities if e.name == name)


# Recordings whose first answer needed the repair round, and why. Anything
# else needing one is a regression worth looking at before committing.
REPAIRED = {
    "necromancer": "a stray 0 among the operations (malformed JSON item), fixed on repair",
}


def test_every_golden_answer_is_valid_on_the_first_try(schema, demo_context):
    for path in sorted(GOLDEN.glob("*.json")):
        record = json.loads(path.read_text(encoding="utf-8"))
        assert record["valid"], path.name
        if record["name"] not in REPAIRED:
            assert record["attempts"] == 1, f"{path.name} needed a repair round"
        replay(record["name"], schema, demo_context)


def test_burning_uses_the_built_in_burn_effect(schema, demo_context):
    proposal = replay("burning", schema, demo_context)
    effect_changes = [
        op for op in proposal.operations
        if getattr(op, "component", None) == "MeleeAttack" and getattr(op, "field", None) == "effect"
    ]
    assert effect_changes and all(op.value == "Burn" for op in effect_changes)
    # On the hero (its copy or its prefab), not on the enemies.
    hero = entity_named(demo_context, "Hero")
    assert all(getattr(op, "entity", None) == hero or getattr(op, "prefab", "") == "prefabs/hero.prefab"
               for op in effect_changes)
    assert not [op for op in ops(proposal, "command") if op.name == "script.write"], "Burn needs no script"


def test_tougher_skeletons_edit_the_prefab_and_keep_them_at_full_health(schema, demo_context):
    proposal = replay("tougher_skeletons", schema, demo_context)
    prefab_ops = ops(proposal, "set_prefab_field")
    assert prefab_ops and all(op.prefab == "prefabs/skeleton.prefab" for op in prefab_ops)
    by_field = {(op.component, op.field): op.value for op in prefab_ops}
    assert by_field.get(("Health", "max"), 8) > 8 or by_field.get(("Stats", "defense"), 0) > 0
    if ("Health", "max") in by_field:
        assert by_field.get(("Health", "current")) == by_field[("Health", "max")]


def test_spawning_places_two_skeletons_on_walkable_ground_near_the_hero(schema, demo_context):
    proposal = replay("spawn_skeletons", schema, demo_context)
    spawns = [op for op in ops(proposal, "command") if op.name == "scene.spawn"]
    assert len(spawns) == 2
    hero = next(e for e in demo_context.entities if e.name == "Hero")
    hx, hy = hero.components["WorldTransform2D"]["x"], hero.components["WorldTransform2D"]["y"]
    for op in spawns:
        assert op.args["prefab"] == "prefabs/skeleton.prefab"
        assert abs(op.args["x"] - hx) <= 4 and abs(op.args["y"] - hy) <= 4


def test_night_sky_changes_only_the_renderer_background(schema, demo_context):
    proposal = replay("night_sky", schema, demo_context)
    settings = ops(proposal, "set_setting")
    assert settings and len(settings) == len(proposal.operations)
    values = {op.field: op.value for op in settings if op.section == "Renderer"}
    assert values.get("clearB", 0) > values.get("clearR", 1)  # bluish
    assert max(values.values()) < 0.5                          # dark


def test_villager_follow_adds_a_follow_component(schema, demo_context):
    proposal = replay("villager_follows", schema, demo_context)
    adds = ops(proposal, "add_component")
    assert len(adds) == 1
    assert adds[0].entity == entity_named(demo_context, "Villager")
    assert adds[0].component == "Follow"
    # No target means "the player" — exactly what was asked.
    assert adds[0].fields.get("target") is None


def test_flee_writes_a_script_that_keeps_the_attack_behavior(schema, demo_context):
    proposal = replay("flee_script", schema, demo_context)
    writes = [op for op in ops(proposal, "command") if op.name == "script.write"]
    assert len(writes) == 1
    source = writes[0].args["source"]
    path = writes[0].args["path"]
    # It's attached to the skeletons (via their prefab or each copy) — in this
    # answer, or, when the model treats it as a task, in a later planned step.
    attached = [op for op in proposal.operations
                if (getattr(op, "component", None) == "LuaScript" and getattr(op, "value", None) == path)
                or (op.op == "add_component" and op.fields.get("path") == path)]
    if proposal.scope == "task":
        assert not proposal.done and any("script" in step.lower() for step in proposal.plan[1:])
    else:
        assert attached
    # ... and, since it replaces enemy_melee.lua, still attacks.
    assert "attack_toward" in source
    # Uses the API as documented (these were real mistakes before the prompt spelled them out).
    assert "get_health()[" not in source
    assert "get_field(nil" not in source


# --- Request sizing (development_plan.md 2.9) --------------------------------------


@pytest.mark.parametrize("name", ["burning", "tougher_skeletons", "spawn_skeletons", "night_sky", "villager_follows"])
def test_ordinary_requests_stay_small(name, schema, demo_context):
    proposal = replay(name, schema, demo_context)
    assert proposal.scope == "small"
    assert proposal.operations


def test_a_new_kind_of_enemy_is_built_as_a_task(schema, demo_context):
    proposal = replay("necromancer", schema, demo_context)
    assert proposal.scope == "task"
    assert proposal.done is False
    assert len(proposal.plan) >= 3
    assert proposal.operations  # the first step, not the whole thing
    # It doesn't edit a prefab it hasn't created yet (needs the step results first).
    assert not [op for op in ops(proposal, "set_prefab_field") if "necromancer" in op.prefab]


def test_a_whole_game_is_split_into_requests(schema, demo_context):
    proposal = replay("whole_rpg", schema, demo_context)
    assert proposal.scope == "split"
    assert proposal.operations == []
    assert 2 <= len(proposal.split) <= 6
    assert proposal.notes
