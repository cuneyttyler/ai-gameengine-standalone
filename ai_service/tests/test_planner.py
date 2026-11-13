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
    assert set(STEPS) == {"concept", "outline", "act", "scene", "critic", "character", "quests", "place", "gameplay", "system", "behaviour", "systems",
                          "visuals", "review", "revise", "dialogue", "buildcheck", "judgetest", "cutscene", "requirements",
                          "assets", "implement", "timeline", "placements"}
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


def test_a_replayed_answer_calls_no_model():
    """The engine re-applies an answer from its log: put into shape and checked, no model call."""
    provider = ScriptedProvider([])
    replay = {"character": {"name": ["Mira"], "relationships": ["character/kael"]}, "rationale": "x"}
    result = plan_character(provider, {"entry": {"id": "character/mira"}, "replay": replay})
    assert result["ok"] and result["character"]["name"] == "Mira"
    assert result["character"]["relationships"] == [{"with": "character/kael"}]
    assert provider.requests == []


def test_a_broken_or_cut_off_json_answer_is_repaired():
    """Trailing commas, a fence, and an answer cut off by the token limit: closed at the last
    complete item instead of "the answer wasn't JSON"."""
    from app.places import _parse
    value, problem = _parse('Here it is:\n```json\n{"systems": {"records": [{"type": "item", "id": "sword",},]}}\n```')
    assert problem == "" and value["systems"]["records"][0]["id"] == "sword"
    cut = '{"systems": {"summary": {"economy": "gold"}, "records": [{"type": "item", "id": "sword", "record": {"name": "Sword"}}, {"type": "item", "id": "shi'
    value, problem = _parse(cut)
    assert problem == "" and [r["id"] for r in value["systems"]["records"]] == ["sword"]
    assert value["systems"]["summary"]["economy"] == "gold"
    assert _parse("no json here")[0] is None


def test_the_systems_design_keeps_its_structure():
    """⑦'s summary parts are free: an object stays an object (shown as a table), not "a: b; c: d"."""
    from app.planner import SYSTEMS_SHAPE
    from app.shapes import coerce, parse_shape
    answer = {"systems": {"summary": {"economy": {"currency": "gold", "prices": {"weapons": {"min": 50, "max": 300}}},
                                      "loot": "chests and drops"},
                          "records": [{"type": "item", "id": ["sword"], "record": {"name": "Sword"}}]}}
    out = coerce(answer, parse_shape(SYSTEMS_SHAPE))["systems"]
    assert out["summary"]["economy"]["prices"]["weapons"]["max"] == 300
    assert out["summary"]["loot"] == "chests and drops"
    assert out["records"][0]["id"] == "sword"  # the rest is still put into shape


def test_a_place_is_planned_with_the_story_set_there():
    """The Planner's place builds carry the story (context.story): the planning prompt shows it."""
    from app.places import story_part
    context = {"story": {"logline": "A warden hunts a stolen amulet.", "scenesHere": [{"id": "s1-02", "needs": ["altar"]}]}}
    part = story_part(context)
    assert "THE STORY SET HERE" in part and "stolen amulet" in part and "altar" in part
    assert story_part({}) == ""



def test_gameplay_features_stay_within_the_briefs_extent():
    from app.planner import plan_gameplay
    feature = {"id": "cooking", "name": "Cooking", "description": "Cook at a stove.", "howItPlays": "Press E at a stove.",
               "buildRequest": "Add a stew recipe at a stove."}
    second = {**feature, "id": "farming", "name": "Farming"}
    # Too many for "small" (one to one here), then right.
    provider = scripted({"gameplay": {"features": [feature, second]}}, {"gameplay": {"features": [feature]}})
    result = plan_gameplay(provider, {"count": [1, 1]})
    assert result["ok"] and [f["id"] for f in result["gameplay"]["features"]] == ["cooking"]
    assert "2 features: the Brief asks for 1 to 1" in provider.requests[-1][-1]["content"]
    # What the AI panel needs is required of each.
    provider = scripted({"gameplay": {"features": [{**feature, "buildRequest": ""}]}}, {"gameplay": {"features": [feature]}})
    assert plan_gameplay(provider, {"count": [1, 3]})["ok"]
    assert "buildRequest is missing" in provider.requests[-1][-1]["content"]


def test_a_cutscene_uses_only_its_cast_and_markers():
    from app.planner import plan_cutscene
    good = {"cutscene": {"name": "Return", "world": "waits", "tracks": [
        {"type": "screen", "keys": [{"at": 0, "op": "fade_in", "seconds": 1}]},
        {"type": "actor", "actor": "character/lysa", "keys": [{"at": 1, "op": "move", "marker": "story/s3/lysa", "seconds": 3, "path": True}]},
        {"type": "dialogue", "keys": [{"at": 4, "op": "line", "speaker": "character/lysa", "text": "Here it rests.", "seconds": 3},
                                      {"at": 8, "op": "talk", "with": "character/lysa"}]},
        {"type": "logic", "keys": [{"at": 8.5, "op": "wait_until", "until": {"talk": True}, "max": 60}]}]}, "rationale": "r"}
    bad = {"cutscene": {"name": "x", "tracks": [{"type": "actor", "actor": "character/nobody", "keys": [
        {"at": 0, "op": "move", "marker": "nowhere"}]}]}, "rationale": "r"}
    inputs = {"cast": [{"key": "character/lysa"}], "markers": ["story/s3/lysa", "arrive"]}
    out = plan_cutscene(scripted(bad, good), inputs)
    assert out["cutscene"]["name"] == "Return"


def test_assets_cover_every_given_once():
    from app.planner import plan_assets
    bad = {"assets": [{"id": "gate", "name": "graveyard gate", "type": "gateway", "separate": True, "look": "",
                       "covers": ["r1"]}], "rationale": "r"}
    good = {"assets": [{"id": "gate", "name": "graveyard gate", "type": "gateway", "separate": True,
                        "look": "An iron gate in a stone wall, chained shut.", "size": "large", "states": ["chained", "open"],
                        "places": ["location/bellwether"], "covers": ["r1", "a-s1-02-3"]}], "rationale": "r"}
    out = plan_assets(scripted(bad, good), {"given": [{"id": "r1"}, {"id": "a-s1-02-3"}]})
    assert out["ok"] and out["assets"][0]["covers"] == ["r1", "a-s1-02-3"]


def test_every_requirement_gets_an_implementation():
    from app.planner import plan_implement
    bad = {"items": [{"req": "r1", "how": "magic", "does": "x", "by": "rule"}], "rationale": "r"}
    good = {"items": [{"req": "r1", "how": "quest-effect", "does": "Grey Lament moved to graveyard-entrance", "by": "rule"},
                      {"req": "a-1", "how": "cutscene", "does": "the attack watched", "by": "ai-step", "after": ["r1"]}],
            "rationale": "r"}
    out = plan_implement(scripted(bad, good), {"requirements": [{"id": "r1"}, {"id": "a-1"}]})
    assert out["ok"] and len(out["items"]) == 2


def test_a_timeline_names_only_the_plans_things():
    from app.planner import plan_timeline
    inputs = {"cast": [{"id": "character/nima", "roles": ["vendor"], "home": "location/shop"}, {"id": "character/ash", "player": True}],
              "places": [{"id": "location/village"}, {"id": "location/shop"}],
              "quests": [{"id": "q-main", "stages": [{"index": 10}, {"index": 20}]}], "templates": ["work", "wander"]}
    bad = {"timeline": {"characters": [{"who": "character/nima", "start": {"place": "location/nowhere"},
                         "changes": [{"quest": "q-main", "stage": 30, "place": "location/shop"}]}], "cutscenes": []}, "rationale": "r"}
    good = {"timeline": {"characters": [{"who": "character/nima", "start": {"place": "location/village", "behaviour": "friendly",
                                                                           "why": "she runs errands in the village"},
                          "changes": [{"quest": "q-main", "stage": 20, "on": "enter", "place": "location/shop",
                                       "packages": [{"template": "work"}], "why": "s1-02: the accounts are gathered"}]}],
                         "cutscenes": [{"id": "attack", "name": "The attack", "scene": "s1-02", "quest": "q-main", "stage": 20,
                                        "place": "location/village", "trigger": {"kind": "leave", "from": "location/shop"},
                                        "cast": [{"who": "character/nima", "role": "witness", "does": "screams"}]}]}, "rationale": "r"}
    inputs["scenes"] = [{"id": "s1-02", "mode": "cutscene"}]
    out = plan_timeline(scripted(bad, good), inputs)
    assert out["ok"] and out["timeline"]["characters"][0]["changes"][0]["place"] == "location/shop"
    assert out["timeline"]["cutscenes"][0]["trigger"]["kind"] == "leave"


def test_the_controls_bind_each_key_once_and_cover_what_the_game_uses():
    """⑦ Systems ▸ Input: one action a key in a context, real key names, every used name an action (or in also)."""
    from app.planner import plan_input
    inputs = {"keyNames": ["E", "J", "Q", "PadA", "PadY"], "used": ["open-journal", "show_journal"], "planned": []}
    clash = {"input": {"actions": [{"name": "interact", "context": "gameplay", "keys": ["E"]},
                                   {"name": "open-journal", "context": "gameplay", "keys": ["E", "Hyper"]}]}, "rationale": "r"}
    good = {"input": {"actions": [{"name": "interact", "context": "gameplay", "keys": ["E"], "pad": ["PadA"]},
                                  {"name": "open-journal", "also": ["show_journal"], "context": "gameplay", "keys": ["J"],
                                   "pad": ["PadY"], "screen": "journal"}]}, "rationale": "r"}
    out = plan_input(scripted(clash, good), inputs)
    assert out["ok"] and not out.get("problems")
    first = plan_input(scripted(clash, clash), inputs)
    found = " ".join(first.get("problems", []))
    assert "one key, one action" in found and "isn't a key name" in found and "show_journal" in found


def test_a_revision_keeps_the_documents_structure():
    """Revise: a list of stages isn't written over with text; an item added to a list is of its kind."""
    from app.planner import _structure_problems
    doc = {"stages": [{"index": 10, "journal": "a", "objectives": [{"action": "talk", "target": "x"}]}]}
    assert _structure_problems("e", doc, "/stages", "advancesScene: s1-01; index: 10")
    assert not _structure_problems("e", doc, "/stages/0/journal", "Hear Elder Sella first.")
    assert not _structure_problems("e", doc, "/stages/0/objectives/-", {"action": "talk", "target": "elder-sella"})
    assert _structure_problems("e", doc, "/stages/0/objectives/-", "talk to the elder")


def test_a_behaviour_plan_reads_actions_and_events_given_as_objects():
    """⑦ Systems: a system design's actions and sends are objects ({name, key}, {event, when}); a quest's stages too."""
    from app.planner import plan_behaviour
    inputs = {"quests": [{"id": "q-main", "stages": [{"index": 10, "journal": "x"}]}],
              "systems": [{"actions": [{"name": "open-journal", "key": "J"}], "sends": [{"event": "rumor-accounted", "when": "x"}]}],
              "engineEvents": ["death"], "packageTemplates": ["work"]}
    answer = {"plan": {"character": "x", "packages": [{"id": "p1", "template": "work"}],
                       "behaviour": [{"when": {"quest": "q-main", "stage": 10}, "does": {"package": "p1"}},
                                     {"when": {"event": "rumor-accounted"}, "does": {"says": "hi"}},
                                     {"when": {"playerAction": "open-journal"}, "does": {"says": "ok"}}]}, "rationale": "r"}
    out = plan_behaviour(None, {**inputs, "replay": answer})
    assert out["ok"] and not out.get("problems")


def test_a_routine_alone_keeps_to_the_places():
    """④ A routine written alone: its haunts are places of the plan, each with a time of day."""
    from app.planner import plan_routine
    inputs = {"character": {"id": "character/jory", "name": "Jory Fen"},
              "places": [{"id": "location/village"}, {"id": "location/mill"}]}
    wrong = {"routine": {"summary": "He mills grain.", "haunts": [{"place": "location/town", "spot": "", "when": "day"}]}, "rationale": "r"}
    right = {"routine": {"summary": "He mills grain and sleeps at home.",
                         "haunts": [{"place": "location/village", "spot": "his house", "when": "night", "does": "sleeps"},
                                    {"place": "location/mill", "spot": "the millstones", "when": "day", "does": "mills"}],
                         "returnsTo": "work"}, "rationale": "r"}
    out = plan_routine(scripted(wrong, right), inputs)
    assert out["ok"] and out["routine"]["haunts"][1]["place"] == "location/mill"
    many = {"routine": {"summary": "s", "haunts": [{"place": "location/mill", "when": "day"}] * 6}, "rationale": "r"}
    first = plan_routine(scripted(many, many), inputs)
    assert any("two to five" in p for p in first.get("problems", []) + first.get("errors", []))


def test_a_gathering_ends_with_its_scene():
    """⑥b: someone put at a scene's anchor goes back to their routine once its stage is over."""
    from app.planner import plan_timeline
    inputs = {"cast": [{"id": "character/jory", "name": "Jory Fen"}],
              "places": [{"id": "location/village", "anchors": [{"id": "village-square", "for": "s1-01"}]}],
              "quests": [{"id": "q-main", "stages": [{"index": 10, "playsScene": "s1-01"}, {"index": 20, "playsScene": "s1-02"}]}],
              "templates": ["stay", "work"]}
    stays = {"timeline": {"characters": [{"who": "character/jory", "start": {"place": "location/village", "spot": "village-square",
                                                                             "why": "s1-01 gathers the villagers"}}],
                          "cutscenes": []}, "rationale": "r"}
    goes = {"timeline": {"characters": [{"who": "character/jory", "start": {"place": "location/village", "spot": "village-square",
                                                                            "why": "s1-01 gathers the villagers"},
                                         "changes": [{"quest": "q-main", "stage": 10, "on": "complete", "spot": "his house",
                                                      "packages": [{"template": "stay", "place": "location/village"}],
                                                      "why": "s1-01 is over: he goes home"}]}],
                         "cutscenes": []}, "rationale": "r"}
    out = plan_timeline(scripted(stays, goes), inputs)
    assert out["ok"] and out["timeline"]["characters"][0]["changes"][0]["spot"] == "his house"
    first = plan_timeline(scripted(stays, stays), inputs)
    assert any("nothing sends them on" in p for p in first.get("problems", []) + first.get("errors", []))


def test_a_repair_with_only_what_it_fixed_keeps_the_rest():
    """A repair that answers with only the entry it fixed keeps the others (⑥b lost 16 of 17 characters this way)."""
    from app.planner import plan_timeline
    inputs = {"cast": [{"id": "character/jory", "name": "Jory Fen"}, {"id": "character/ina", "name": "Ina Crook"}],
              "places": [{"id": "location/village", "anchors": [{"id": "village-square", "for": "s1-01"}]}],
              "quests": [{"id": "q-main", "stages": [{"index": 10, "playsScene": "s1-01"}, {"index": 20, "playsScene": "s1-02"}]}],
              "templates": ["stay", "work"]}
    ina = {"who": "character/ina", "start": {"place": "location/village", "spot": "her farm", "why": "she farms"}}
    jory_stays = {"who": "character/jory", "start": {"place": "location/village", "spot": "village-square", "why": "s1-01 gathers the villagers"}}
    jory_goes = {**jory_stays, "changes": [{"quest": "q-main", "stage": 10, "on": "complete", "spot": "his house",
                                            "packages": [{"template": "stay", "place": "location/village"}], "why": "s1-01 is over"}]}
    first = {"timeline": {"characters": [ina, jory_stays], "cutscenes": []}, "rationale": "r"}
    only_fixed = {"timeline": {"characters": [jory_goes], "cutscenes": []}, "rationale": "r"}
    out = plan_timeline(scripted(first, only_fixed), inputs)
    assert out["ok"] and not out.get("problems")
    assert [c["who"] for c in out["timeline"]["characters"]] == ["character/ina", "character/jory"]
    assert out["timeline"]["characters"][1]["changes"][0]["spot"] == "his house"


def test_placements_are_checked_against_the_buildings_markers_and_stages():
    """34.11: the AI's placements use only real buildings, markers and quest stages; inside only where an interior is drawn."""
    from app.planner import PLACEMENTS_SHAPE, plan_placements
    from app.shapes import parse_shape
    assert parse_shape(PLACEMENTS_SHAPE) is not None
    inputs = {"buildings": [{"id": "town/house-1", "scene": "scenes/town.aige", "kind": "house", "interior": None},
                            {"id": "town/shop-1", "scene": "scenes/town.aige", "kind": "shop", "interior": "scenes/shop.aige"}],
              "scenes": [{"scene": "scenes/town.aige", "markers": ["square", "gate"]}, {"scene": "scenes/shop.aige", "markers": ["counter"]}],
              "characters": [{"id": "character/nima"}, {"id": "character/hobb"}],
              "props": [{"id": "q-main/seal"}],
              "quests": [{"id": "q-main", "stages": [{"index": 10}, {"index": 20}]}]}
    bad = {"placements": {"characters": [
        {"id": "character/nima", "home": "town/house-9", "scene": "scenes/town.aige", "spot": {"building": "town/house-1", "inside": True}, "why": "x"},
        {"id": "character/hobb", "scene": "scenes/town.aige", "spot": {"marker": "well"}, "why": "x",
         "whereabouts": [{"from": {"quest": "q-main", "stage": 30}, "scene": "scenes/town.aige", "spot": {"marker": "gate"}}]}],
        "props": []}}
    out = plan_placements(ScriptedProvider([]), {**inputs, "replay": bad})
    problems = " ".join(out.get("problems", []))
    assert "town/house-9" in problems and "no drawn interior" in problems and "'well'" in problems
    assert "no stage 30" in problems and "q-main/seal" in problems
    good = {"placements": {"characters": [
        {"id": "character/nima", "home": "town/house-1", "work": "town/shop-1", "scene": "scenes/shop.aige",
         "spot": {"building": "town/shop-1", "inside": True}, "why": "she keeps the shop"},
        {"id": "character/hobb", "scene": "scenes/town.aige", "spot": {"near": "gate", "note": "at the gate"}, "why": "the watch captain",
         "whereabouts": [{"from": {"start": True}}, {"from": {"quest": "q-main", "stage": 20}, "scene": "scenes/town.aige", "spot": {"marker": "square"}, "why": "s2"}]}],
        "props": [{"id": "q-main/seal", "scene": "scenes/town.aige", "spot": {"near": "town/house-1"}, "why": "x",
                   "whereabouts": [{"from": {"quest": "q-main", "stage": 20, "on": "complete"}, "holder": "player"}]}]}}
    out = plan_placements(ScriptedProvider([]), {**inputs, "replay": good})
    assert out["ok"] and not out.get("problems")
