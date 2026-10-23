"""The AI Planner's steps (development_plan.md 22): think, answer, check, repair once."""

import json

from fastapi.testclient import TestClient

from app.main import create_app
from app.planner import STEPS, check_outline, plan_character, plan_concept, plan_outline, plan_scene, plan_visuals
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


def test_outline_roles_come_from_their_own_vocabulary():
    inputs = {"vocab": {"NarrativeRole": ["protagonist", "mentor"], "GameplayRole": ["companion", "vendor"]}}
    outline = {"story": {"acts": [{"id": "act-1"}]}, "places": [], "connections": [],
               "cast": [{"id": "character/a", "player": True, "narrativeRoles": ["protagonist", "companion"],
                         "gameplayRoles": ["vendor"]}]}
    problems = " | ".join(check_outline(outline, inputs))
    assert "character/a narrativeRoles: 'companion' isn't one of protagonist, mentor" in problems
    assert "vendor" not in problems  # in its own list: fine


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


def test_a_retry_with_the_answer_is_a_quick_fix():
    """The engine sends the answer back with its problems: one call, no thinking pass, and the
    model sees its own answer to change only where the problems say."""
    fixed = {"scene": {"summary": "Fixed.", "beats": [{"function": "turn", "text": "x"}], "facts": [], "events": [],
                       "newEntities": []}, "rationale": ""}
    provider = ScriptedProvider([json.dumps(fixed)])
    inputs = {"id": "s1-01", "properties": [], "answer": {"scene": {"summary": "Broken.", "facts": [{"p": "nope"}]}},
              "problems": ["facts[0].p: no property 'nope'"]}
    result = plan_scene(provider, inputs)
    assert result["ok"] and result["scene"]["summary"] == "Fixed."
    assert len(provider.requests) == 1 and provider.text_requests == []  # no thinking pass
    sent = json.dumps(provider.requests[0])
    assert "Your answer" in sent and "no property 'nope'" in sent and "Broken." in sent


def test_an_answer_of_the_wrong_kinds_is_put_into_shape():
    """A small model's character: lists for texts, text for lists, a relationship as a name,
    numbers as text, null objects. Coerced to the step's shape, the check runs and nothing breaks."""
    messy = {"character": {"name": ["Elara", "Voidshade"], "importance": None, "line": 42,
                           "biography": {"backstory": {"text": "Born in ash."}, "personality": ["brave", ["calm", "0.4"]],
                                         "motivations": "protection", "arc": "redemption"},
                           "appearance": "Tall, grey cloak.", "voice": None,
                           "relationships": ["character/kael", {"with": "character/mara", "type": "friend", "valence": "0.5"}],
                           "gameplay": {"roles": "vendor", "level": "4", "skills": ["swords", "lore"]},
                           "behaviour": {"packages": "wander", "idle": "yes"}},
             "rationale": ""}
    provider = ScriptedProvider([THINKING, json.dumps(messy)])
    result = plan_character(provider, {"entry": {"id": "character/elara"}})
    assert result["ok"], result
    c = result["character"]
    assert c["name"] == "Elara, Voidshade" and c["importance"] == "" and c["line"] == "42"
    assert c["biography"]["backstory"] == "Born in ash."
    assert c["biography"]["personality"] == [["brave", 0.0], ["calm", 0.4]]
    assert c["biography"]["motivations"] == ["protection"]
    assert c["biography"]["arc"]["from"] == "redemption"
    assert c["appearance"] == {"description": "Tall, grey cloak."} and c["voice"] == {}
    assert c["relationships"][0] == {"with": "character/kael"} and c["relationships"][1]["valence"] == 0.5
    assert c["gameplay"]["roles"] == ["vendor"] and c["gameplay"]["level"] == 4 and c["gameplay"]["skills"] == "swords, lore"
    assert c["behaviour"]["packages"] == [{"template": "wander"}] and c["behaviour"]["idle"] is True


def test_a_scene_without_a_quest_stage_keeps_it_null():
    from app.shapes import coerce, parse_shape
    from app.planner import SCENE_SHAPE
    assert coerce({"scene": {"questStage": None, "summary": "x"}}, parse_shape(SCENE_SHAPE))["scene"]["questStage"] is None
