from app.models import Proposal
from app.validation import validate_proposal


def proposal(*operations) -> Proposal:
    return Proposal.model_validate({"summary": "test", "operations": list(operations)})


def test_valid_operations_pass(schema, context):
    p = proposal(
        {"op": "set_field", "entity": 7, "component": "Health", "field": "max", "value": 12},
        {"op": "set_field", "entity": 3, "component": "MeleeAttack", "field": "effect", "value": "Burn"},
        {"op": "set_prefab_field", "prefab": "prefabs/skeleton.prefab", "component": "Health", "field": "max",
         "value": 10},
        {"op": "set_setting", "section": "Combat", "field": "knockbackScale", "value": 1.5},
        {"op": "set_script_property", "entity": 7, "name": "swing_range", "value": 2},
        {"op": "add_component", "entity": 7, "component": "Stats", "fields": {"defense": 1}},
        {"op": "remove_component", "entity": 7, "component": "Follow"},
        {"op": "command", "name": "scene.spawn", "args": {"prefab": "prefabs/skeleton.prefab", "x": 4.5, "y": 2.5}},
    )
    assert validate_proposal(p, schema, context) == []


def test_unknown_names_and_entities_are_rejected(schema, context):
    errors = validate_proposal(
        proposal(
            {"op": "set_field", "entity": 99, "component": "Health", "field": "max", "value": 1},
            {"op": "set_field", "entity": 7, "component": "Mana", "field": "max", "value": 1},
            {"op": "set_field", "entity": 7, "component": "Health", "field": "maximum", "value": 1},
            {"op": "set_setting", "section": "Weather", "field": "rain", "value": 1},
            {"op": "command", "name": "scene.explode", "args": {}},
            {"op": "set_prefab_field", "prefab": "prefabs/dragon.prefab", "component": "Health", "field": "max",
             "value": 1},
        ),
        schema,
        context,
    )
    assert len(errors) == 6
    assert "entity 99 doesn't exist" in errors[0]
    assert "unknown component 'Mana'" in errors[1]


def test_types_ranges_and_options_are_enforced(schema, context):
    errors = validate_proposal(
        proposal(
            {"op": "set_field", "entity": 7, "component": "Health", "field": "max", "value": "lots"},
            {"op": "set_field", "entity": 7, "component": "Health", "field": "max", "value": 0},
            {"op": "set_field", "entity": 3, "component": "MeleeAttack", "field": "effect", "value": "Freeze"},
            {"op": "set_setting", "section": "Physics", "field": "playerCollides", "value": 1},
        ),
        schema,
        context,
    )
    assert [e.split(": ", 1)[1] for e in errors] == [
        "max must be a number",
        "max must be at least 1",
        "effect must be one of None, Burn, Poison, Slow, Regen",
        "playerCollides must be true or false",
    ]


def test_set_field_requires_the_component(schema, context):
    errors = validate_proposal(
        proposal({"op": "set_field", "entity": 3, "component": "Follow", "field": "speed", "value": 2}),
        schema,
        context,
    )
    assert "has no Follow (add it first)" in errors[0]


def test_command_arguments_are_checked(schema, context):
    errors = validate_proposal(
        proposal(
            {"op": "command", "name": "scene.spawn", "args": {"prefab": "prefabs/skeleton.prefab", "x": 1}},
            {"op": "command", "name": "scene.paintRect",
             "args": {"prefab": "prefabs/grass.prefab", "x0": 0, "y0": 0, "x1": 2.5, "y1": 2, "color": "red"}},
        ),
        schema,
        context,
    )
    assert any("needs 'y'" in e for e in errors)
    assert any("x1 must be a whole number" in e for e in errors)
    assert any("doesn't take color" in e for e in errors)


def test_scripts_must_stay_in_scripts_and_exist_when_attached(schema, context):
    errors = validate_proposal(
        proposal(
            {"op": "command", "name": "script.write", "args": {"path": "../evil.lua", "source": "--"}},
            {"op": "add_component", "entity": 3, "component": "LuaScript", "fields": {"path": "scripts/missing.lua"}},
        ),
        schema,
        context,
    )
    assert any("scripts/<name>.lua" in e for e in errors)
    assert any("scripts/missing.lua" in e for e in errors)

    # Writing it in the same proposal makes attaching it fine.
    ok = validate_proposal(
        proposal(
            {"op": "command", "name": "script.write", "args": {"path": "scripts/burning.lua", "source": "--"}},
            {"op": "add_component", "entity": 3, "component": "LuaScript", "fields": {"path": "scripts/burning.lua"}},
        ),
        schema,
        context,
    )
    assert ok == []


def test_structural_components_cannot_be_added(schema, context):
    errors = validate_proposal(
        proposal({"op": "add_component", "entity": 7, "component": "Tile", "fields": {}}), schema, context
    )
    assert "can't be added by hand" in errors[0]


# --- Request size (development_plan.md 2.9) --------------------------------------


def test_small_is_the_default_scope():
    p = proposal({"op": "set_field", "entity": 7, "component": "Health", "field": "max", "value": 12})
    assert p.scope == "small" and p.plan == [] and p.split == [] and p.done is False


def test_a_task_step_is_validated_like_any_proposal(schema, context):
    p = Proposal.model_validate(
        {
            "scope": "task",
            "summary": "step 1: write the dodge script",
            "plan": ["write the script", "attach it to the hero", "tune the speed"],
            "operations": [{"op": "set_field", "entity": 7, "component": "Health", "field": "max", "value": -1}],
            "done": False,
        }
    )
    assert p.plan[0] == "write the script"
    assert validate_proposal(p, schema, context) != []  # same checks as a small proposal


def test_a_split_suggests_requests_instead_of_operations(schema, context):
    good = Proposal.model_validate(
        {"scope": "split", "summary": "too big", "split": ["add a dodge roll", "add a boss fight"], "notes": "why"}
    )
    assert validate_proposal(good, schema, context) == []

    with_ops = Proposal.model_validate(
        {
            "scope": "split",
            "summary": "too big",
            "split": ["a", "b"],
            "operations": [{"op": "set_setting", "section": "Combat", "field": "knockbackScale", "value": 1.5}],
        }
    )
    assert "has no operations" in validate_proposal(with_ops, schema, context)[0]

    one_part = Proposal.model_validate({"scope": "split", "summary": "too big", "split": ["everything"]})
    assert "at least two" in validate_proposal(one_part, schema, context)[0]
