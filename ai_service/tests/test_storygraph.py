"""The story knowledge graph (development_plan.md 21): the story_graph op and its checks."""

import json
from pathlib import Path

from app.models import Proposal
from app.validation import validate_proposal

DATA = Path(__file__).resolve().parents[2] / "data" / "story"


def ontology_summary() -> dict:
    """What the engine's StoryOntology::Summary gives the context, from data/story."""
    classes = json.loads((DATA / "classes.json").read_text(encoding="utf-8"))["classes"]
    properties = json.loads((DATA / "properties.json").read_text(encoding="utf-8"))["properties"]
    vocab = json.loads((DATA / "vocabularies.json").read_text(encoding="utf-8"))["vocabularies"]
    return {
        "classes": {k: {"layer": v.get("layer", ""), "fields": [f"{n}:{t}" for n, t in v.get("fields", {}).items()],
                        **({"parent": v["parent"]} if v.get("parent") else {})} for k, v in classes.items()},
        "properties": {k: {"domain": v.get("domain", []), "range": v.get("range", []),
                           **({"temporal": True} if v.get("temporal") else {})} for k, v in properties.items()},
        "vocabularies": vocab,
    }


def story_context(demo_context):
    return demo_context.model_copy(update={"storyGraph": {
        "ontology": ontology_summary(), "revision": 3,
        "nodes": {"character/tamsin": "Character", "character/aldric": "Character", "location/riverford": "Location",
                  "s-01": "StoryScene", "s-02": "StoryScene"},
        "scenes": [{"id": "s-01", "order": 1, "mode": "cutscene"}, {"id": "s-02", "order": 2, "mode": "dialogue"}],
    }})


def proposal(*operations) -> Proposal:
    return Proposal.model_validate({"summary": "test", "operations": list(operations)})


def story_errors(p, schema, context):
    return [e for e in validate_proposal(p, schema, context) if "story_graph" in e]


def test_a_betrayal_validates(schema, demo_context):
    context = story_context(demo_context)
    p = proposal({"op": "story_graph",
                  "nodes": [{"id": "s-03", "class": "StoryScene", "order": 3, "mode": "gameplay", "kernel": True,
                             "location": "location/riverford", "value": "trust-betrayal"},
                            {"id": "item/amulet", "class": "Item", "unique": True}],
                  "facts": [{"p": "hasRelationship", "s": "character/tamsin", "o": "character/aldric", "type": "betrayed-by",
                             "from": "s-03"},
                            {"p": "possesses", "s": "character/aldric", "o": "item/amulet", "until": "s-03"},
                            {"p": "knows", "s": "character/tamsin", "about": "aldric-stole-amulet", "from": "s-02"},
                            {"p": "participant", "s": "s-03", "o": "character/tamsin"}],
                  "end": [{"p": "hasRelationship", "s": "character/tamsin", "o": "character/aldric", "at": "s-03"}]})
    assert story_errors(p, schema, context) == []
    # It round-trips with the engine's key names.
    assert p.model_dump()["operations"][0]["facts"][0]["p"] == "hasRelationship"


def test_unknown_classes_properties_vocabulary_and_scenes_are_caught(schema, demo_context):
    context = story_context(demo_context)
    p = proposal({"op": "story_graph",
                  "nodes": [{"id": "x", "class": "Wizard"},
                            {"id": "s-09", "class": "StoryScene", "order": 9, "mode": "musical"},
                            {"id": "s-10", "class": "StoryScene"}],
                  "facts": [{"p": "flies", "s": "character/tamsin", "o": "s-01"},
                            {"p": "locatedAt", "s": "character/tamsin", "o": "character/aldric"},
                            {"p": "hasRelationship", "s": "character/tamsin", "o": "character/odo", "type": "frenemy"},
                            {"p": "state", "s": "character/aldric", "state": "dead", "from": "s-77"}]})
    errors = " | ".join(story_errors(p, schema, context))
    assert "no class 'Wizard'" in errors
    assert "mode 'musical' isn't a SceneMode" in errors
    assert "scene s-10 needs an order and a mode" in errors
    assert "no property 'flies'" in errors
    assert "character/aldric is a Character, not one of Location" in errors
    assert "no node 'character/odo'" in errors
    assert "'frenemy' isn't one of" in errors
    assert "from 's-77': no such scene" in errors


def test_one_story_graph_operation_per_change(schema, demo_context):
    context = story_context(demo_context)
    p = proposal({"op": "story_graph", "nodes": [{"id": "character/odo", "class": "Character"}]},
                 {"op": "story_graph", "nodes": [{"id": "character/una", "class": "Character"}]})
    assert any("one story_graph operation" in e for e in story_errors(p, schema, context))
