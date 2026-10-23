"""The AI Planner's steps (development_plan.md 22): think, answer, check, repair once."""

import json

from fastapi.testclient import TestClient

from app.main import create_app
from app.planner import STEPS, plan_concept, plan_outline, plan_scene, plan_visuals
from app.providers import ScriptedProvider

THINKING = "Thinking it through."


def scripted(*answers):
    """A provider whose free-text calls think and whose answer calls return `answers` in turn."""
    queue = [json.dumps(a) for a in answers]

    def answer(messages):
        system = messages[0]["content"]
        if "Think it through in plain text first" in system and "Answer with the JSON object only" not in system:
            return THINKING
        return queue.pop(0)

    return ScriptedProvider(answer)


CONCEPT_INPUTS = {"request": "a short dark-fantasy ARPG about a warden recovering a stolen amulet", "reading": "typical",
                  "schema": {"genres": [{"id": "arpg"}, {"id": "life-sim"}], "fields": []}}


def test_concept_answers_a_brief_with_its_confidence_and_questions():
    provider = scripted({"brief": {"title": "The Moon Amulet", "logline": "A warden recovers a stolen amulet.", "genre": "arpg",
                                   "playTime": 3, "tone": ["dark"]},
                         "confidence": {"genre": 0.95}, "assumptions": ["three acts"],
                         "decisions": [{"question": "Fixed hero or created?", "assumed": "fixed", "path": "playerCharacter.kind"}],
                         "rationale": "An ARPG by its words."})
    result = plan_concept(provider, CONCEPT_INPUTS)
    assert result["ok"]
    assert result["brief"]["genre"] == "arpg"
    assert result["confidence"] == {"genre": 0.95}
    assert result["decisions"][0]["path"] == "playerCharacter.kind"
    assert result["rationale"] == "An ARPG by its words."
    # The reading is in the task, and the planner's own thinking instructions, not the place builder's.
    assert "as an experienced game designer would" in provider.requests[0][0]["content"]
    assert "build type it is" not in provider.requests[0][0]["content"]


def test_a_bad_answer_is_repaired_once():
    provider = scripted({"brief": {"title": "", "logline": "x", "genre": "shmup"}},
                        {"brief": {"title": "Moon", "logline": "x", "genre": "arpg"}})
    result = plan_concept(provider, CONCEPT_INPUTS)
    assert result["ok"] and result["brief"]["title"] == "Moon"
    assert "problems" not in result
    repair = provider.requests[-1][-1]["content"]
    assert "genre must be one of arpg, life-sim" in repair and "title is empty" in repair


def test_the_outline_is_checked_for_the_player_roles_and_places():
    inputs = {"template": {"roles": ["protagonist", "antagonist"]}, "buildTypes": ["settlement", "dungeon"]}
    bad = {"outline": {"story": {"acts": [{"id": "act-1"}]},
                       "cast": [{"id": "character/a", "narrativeRoles": ["protagonist"]}],
                       "places": [{"id": "location/x", "kind": "castle-in-the-sky"}], "connections": []}}
    provider = scripted(bad, bad)
    result = plan_outline(provider, inputs)
    assert result["ok"]  # the engine decides; the problems travel with the answer
    problems = " | ".join(result["problems"])
    assert "exactly one cast member is the player" in problems
    assert "nobody fills the antagonist role" in problems
    assert "isn't one of dungeon, settlement" in problems


def test_scene_facts_must_use_known_properties():
    good = {"scene": {"summary": "Tamsin names the thief.", "beats": [{"function": "reveal", "text": "..."}],
                      "facts": [{"p": "knows", "s": "character/warden", "about": "aldric-is-the-thief"}]}}
    result = plan_scene(scripted(good), {"properties": [{"id": "knows"}]})
    assert result["ok"] and "problems" not in result
    bad = {"scene": {"summary": "x", "facts": [{"p": "telepathy", "s": "character/a"}]}}
    result = plan_scene(scripted(bad, bad), {"properties": [{"id": "knows"}]})
    assert "fact property 'telepathy' doesn't exist" in result["problems"]


def test_visuals_pick_from_the_themes_windows_and_widgets():
    inputs = {"themes": ["stone", "parchment"], "windows": ["title", "inventory"], "widgets": ["health"]}
    result = plan_visuals(scripted({"ui": {"theme": "stone", "screens": ["title", "inventory"], "hud": ["health"]}}), inputs)
    assert result["ok"] and result["ui"]["theme"] == "stone"


def test_every_step_has_an_endpoint():
    assert set(STEPS) == {"concept", "outline", "act", "scene", "critic", "character", "quests", "place", "systems",
                          "visuals", "review", "revise", "dialogue"}
    provider = scripted({"findings": [], "rationale": "fine"})
    client = TestClient(create_app(provider=provider))
    response = client.post("/planner/review", json={"brief": {}, "issues": []})
    assert response.status_code == 200
    assert response.json()["ok"] and response.json()["findings"] == []
    assert client.post("/planner/nonsense", json={}).json()["ok"] is False


def test_concept_values_must_be_among_their_options():
    inputs = {**CONCEPT_INPUTS, "schema": {"genres": [{"id": "arpg"}], "fields": [
        {"path": "pattern", "type": "enum", "options": ["linear", "branch-and-bottleneck"]},
        {"path": "tone", "type": "enumSet", "options": ["dark", "heroic"]}]}}
    bad = {"brief": {"title": "Moon", "logline": "x", "genre": "arpg", "pattern": "hub", "tone": ["dark", "spooky"]}}
    good = {"brief": {"title": "Moon", "logline": "x", "genre": "arpg", "pattern": "branch-and-bottleneck", "tone": ["dark"]}}
    provider = scripted(bad, good)
    result = plan_concept(provider, inputs)
    assert result["brief"]["pattern"] == "branch-and-bottleneck"
    repair = provider.requests[-1][-1]["content"]
    assert "pattern: hub isn't one of linear, branch-and-bottleneck" in repair
    assert "tone: spooky isn't one of dark, heroic" in repair
