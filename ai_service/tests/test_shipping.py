"""Shipping (development_plan.md 24): achievement records and shipping settings the AI writes."""

from app.models import Proposal
from app.validation import validate_proposal


def proposal(*operations) -> Proposal:
    return Proposal.model_validate({"summary": "test", "operations": list(operations)})


def shipping_errors(p, schema, context):
    return [e for e in validate_proposal(p, schema, context) if "achievement" in e or "Streaming" in e]


def test_achievements_validate(schema, demo_context):
    p = proposal(
        {"op": "write_record", "type": "achievement", "id": "wolf-slayer",
         "record": {"name": "Wolf Slayer", "kind": "event", "event": "death", "target": "Wolf", "count": 10}},
        {"op": "write_record", "type": "achievement", "id": "secret-door",
         "record": {"name": "Hidden Door", "kind": "flag", "flag": "found-door", "hidden": True}},
    )
    assert shipping_errors(p, schema, demo_context) == []


def test_bad_achievements_are_caught(schema, demo_context):
    p = proposal(
        {"op": "write_record", "type": "achievement", "id": "a", "record": {"kind": "event"}},
        {"op": "write_record", "type": "achievement", "id": "b", "record": {"name": "B", "kind": "quest"}},
        {"op": "write_record", "type": "achievement", "id": "c", "record": {"name": "C", "kind": "luck"}},
    )
    errors = " | ".join(shipping_errors(p, schema, demo_context))
    assert "it needs a name" in errors
    assert "names its event" in errors
    assert "names its quest" in errors
    assert "kind is quest, flag, event or script" in errors
