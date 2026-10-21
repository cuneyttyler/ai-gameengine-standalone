"""Animation and effects (development_plan.md 18): write_anim sheets and the names
components give to particles, effects, sheets and animators."""

from app.models import Proposal
from app.validation import validate_proposal


def proposal(*operations) -> Proposal:
    return Proposal.model_validate({"summary": "test", "operations": list(operations)})


def fx_context(demo_context):
    assets = list(demo_context.assets) + [{"path": "assets/hero.png", "type": "texture"}]
    return demo_context.model_copy(update={"assets": assets, "fx": {
        "sheets": [], "particles": ["fire", "smoke"], "effects": ["hit", "death"],
        "animators": ["humanoid", "quadruped", "flyer", "prop"], "lighting": False}})


SHEET = {"texture": "assets/hero.png", "size": [128, 128], "grid": {"width": 32, "height": 32},
         "clips": {"idle": {"frames": [0, 1]}, "walk": {"directions": {"s": [4, 5], "e": [8, 9], "w": "mirror:e"}}}}


def test_a_sheet_and_an_animated_entity_validate(schema, demo_context):
    context = fx_context(demo_context)
    entity = context.entities[0].id
    p = proposal(
        {"op": "write_anim", "path": "assets/hero.anim.json", "sheet": SHEET},
        {"op": "add_component", "entity": entity, "component": "Animator",
         "fields": {"sheet": "assets/hero.anim.json", "animator": "humanoid"}},
        {"op": "add_component", "entity": entity, "component": "ParticleEmitter", "fields": {"particles": "fire"}},
        {"op": "add_component", "entity": entity, "component": "EffectCues", "fields": {"onHit": "hit"}},
    )
    errors = [e for e in validate_proposal(p, schema, context) if "Animator" in e or "write_anim" in e
              or "ParticleEmitter" in e or "EffectCues" in e]
    assert errors == [], errors


def test_fx_problems_are_reported(schema, demo_context):
    context = fx_context(demo_context)
    entity = context.entities[0].id
    bad_sheet = {"texture": "assets/missing.png", "clips": {"walk": {"directions": {"up": [0], "w": "mirror:e"},
                                                                       "loop": "forever"}}}
    p = proposal(
        {"op": "write_anim", "path": "sprites/hero.json", "sheet": bad_sheet},
        {"op": "add_component", "entity": entity, "component": "ParticleEmitter", "fields": {"particles": "lava"}},
        {"op": "add_component", "entity": entity, "component": "Animator", "fields": {"sheet": "assets/nope.anim.json"}},
    )
    text = "\n".join(validate_proposal(p, schema, context))
    for expected in ["sheets are assets/<name>.anim.json", "no picture 'assets/missing.png'", "no direction 'up'",
                     "w mirrors 'e', which it hasn't", "loop is loop, once, pingpong or hold", "grid indices need",
                     "no particles 'lava'", "no sheet 'assets/nope.anim.json'"]:
        assert expected in text, (expected, text)
