"""Abilities and magic (development_plan.md 19): the records and components the AI writes."""

from app.models import Proposal
from app.validation import validate_proposal


def proposal(*operations) -> Proposal:
    return Proposal.model_validate({"summary": "test", "operations": list(operations)})


def magic_context(demo_context):
    return demo_context.model_copy(update={"magic": {"abilities": ["firebolt", "heal"],
                                                      "effects": ["fire-damage", "heal", "burn", "slow"]}})


def test_a_frost_mage_validates(schema, demo_context):
    context = magic_context(demo_context)
    entity = context.entities[0].id
    p = proposal(
        {"op": "write_record", "type": "magic-effect", "id": "frost-bite",
         "record": {"archetype": "damage", "magnitude": "6 + caster.level", "damageType": "frost"}},
        {"op": "write_record", "type": "ability", "id": "frost-bolt",
         "record": {"name": "Frost bolt", "delivery": "projectile", "cost": {"attribute": "mana", "amount": 10},
                    "effects": ["frost-bite", "slow"], "ai": {"use": "attack", "when": "distance < 6 && mana > 0.2"}}},
        {"op": "add_component", "entity": entity, "component": "Abilities", "fields": {"known": "frost-bolt, heal"}},
        {"op": "write_record", "type": "item", "id": "heal-book",
         "record": {"name": "Book of Healing", "use": {"learn": "heal"}}},
    )
    errors = [e for e in validate_proposal(p, schema, context) if "ability" in e.lower() or "magic" in e.lower()
              or "frost" in e]
    assert errors == [], errors


def test_magic_problems_are_reported(schema, demo_context):
    context = magic_context(demo_context)
    entity = context.entities[0].id
    p = proposal(
        {"op": "write_record", "type": "ability", "id": "bad",
         "record": {"name": "Bad", "delivery": "telepathy", "effects": ["nothing"], "cost": {"attribute": "gold", "amount": 1},
                    "ai": {"use": "attack", "when": "luck > 3"}}},
        {"op": "add_component", "entity": entity, "component": "Abilities", "fields": {"known": "meteor"}},
        {"op": "write_record", "type": "item", "id": "dud", "record": {"name": "Dud", "use": {"learn": "teleport"},
                                                                         "enchantment": {"effects": ["vorpal"], "on": "sometimes"}}},
    )
    text = "\n".join(validate_proposal(p, schema, context))
    for expected in ["delivery is self, touch", "no magic effect 'nothing'", "cost is paid in mana", "asks about 'luck'",
                     "no ability 'meteor'", "no ability 'teleport'", "no magic effect 'vorpal'", "enchantment.on is worn or hit"]:
        assert expected in text, (expected, text)
