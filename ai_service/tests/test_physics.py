"""Physics v2 (development_plan.md 20): layers, triggers, bodies and joints the AI adds."""

from app.models import Proposal
from app.validation import validate_proposal


def proposal(*operations) -> Proposal:
    return Proposal.model_validate({"summary": "test", "operations": list(operations)})


def physics_context(demo_context):
    return demo_context.model_copy(update={"physics": {"layers": ["water"], "gravity": [0, 0], "bodies": 0,
                                                        "triggers": []}})


def physics_errors(p, schema, context):
    words = ("layer", "trigger", "rigidbody2d", "joint2d", "collider2d")
    return [e for e in validate_proposal(p, schema, context) if any(w in e.lower() for w in words)]


def test_a_trigger_crates_and_a_hinged_gate_validate(schema, demo_context):
    context = physics_context(demo_context)
    a, b = context.entities[0].id, context.entities[1].id
    p = proposal(
        {"op": "add_component", "entity": a, "component": "Trigger",
         "fields": {"name": "cave-mouth", "mask": "player, water", "once": True}},
        {"op": "add_component", "entity": a, "component": "Collider2D", "fields": {"layer": "crates", "mask": "world"}},
        {"op": "add_component", "entity": a, "component": "RigidBody2D", "fields": {"type": "Dynamic"}},
        {"op": "add_component", "entity": b, "component": "Collider2D", "fields": {"mask": "crates"}},
        {"op": "add_component", "entity": b, "component": "RigidBody2D", "fields": {"type": "Static"}},
        {"op": "add_component", "entity": a, "component": "Joint2D", "fields": {"type": "Revolute", "other": b}},
    )
    assert physics_errors(p, schema, context) == []


def test_unknown_layers_nameless_triggers_and_bodies_without_shapes_are_caught(schema, demo_context):
    context = physics_context(demo_context)
    a, b = context.entities[0].id, context.entities[1].id
    context.entities[0].components.pop("Collider2D", None)
    p = proposal(
        {"op": "add_component", "entity": a, "component": "Trigger", "fields": {"mask": "playr"}},
        {"op": "add_component", "entity": a, "component": "RigidBody2D", "fields": {}},
        {"op": "add_component", "entity": a, "component": "Joint2D", "fields": {"other": b}},
    )
    errors = " | ".join(physics_errors(p, schema, context))
    assert "no layer 'playr'" in errors
    assert "give it a name" in errors
    assert "needs a Collider2D" in errors
    assert "'other' must be an entity with a RigidBody2D" in errors
