"""write_dialogue / write_quest checks (app/story.py), the near-miss cleanup,
and World Bible retrieval reaching the prompt (development_plan.md 3.2d)."""

import json
import shutil
from pathlib import Path

from fastapi.testclient import TestClient

from app.main import create_app
from app.models import Proposal
from app.providers import ScriptedProvider
from app.service import _drop_stray_scalars
from app.validation import validate_proposal



def proposal(*operations) -> Proposal:
    return Proposal.model_validate({"summary": "test", "operations": list(operations)})


def quest(**overrides):
    q = {
        "name": "Draughts",
        "giver": "character/villager",
        "objectives": [
            {"id": "get", "text": "Collect draughts", "type": "collect", "target": "prefabs/potion.prefab", "count": 2},
            {"id": "back", "text": "Return", "type": "talk", "target": "character/villager"},
        ],
    }
    q.update(overrides)
    return q


def dialogue(**overrides):
    d = {
        "speaker": "character/villager",
        "start": "hi",
        "nodes": [
            {"id": "hi", "text": "Hello.", "choices": [
                {"text": "Work?", "next": "", "do": [{"op": "start_quest", "quest": "draughts"}]}]},
        ],
    }
    d.update(overrides)
    return d


def test_a_quest_and_the_dialogue_that_starts_it_validate_together(schema, demo_context):
    p = proposal(
        {"op": "write_quest", "id": "draughts", "quest": quest()},
        {"op": "write_dialogue", "id": "tamsin2", "dialogue": dialogue()},
        {"op": "add_component", "entity": 288, "component": "Talk",
         "fields": {"dialogue": "dialogue/tamsin2.json", "character": "character/villager"}},
    )
    assert validate_proposal(p, schema, demo_context) == []


def test_story_problems_are_reported_precisely(schema, demo_context):
    broken_dialogue = dialogue(nodes=[
        {"id": "hi", "text": "Hello.", "choices": [
            {"text": "Go", "next": "nowhere"},
            {"text": "Start", "next": "", "do": [{"op": "start_quest", "quest": "ghost-quest"}]}]},
        {"id": "orphan", "text": "Unreachable.", "next": ""},
    ], speaker="character/nobody")
    errors = validate_proposal(proposal(
        {"op": "write_dialogue", "id": "Bad Id", "dialogue": broken_dialogue},
        {"op": "write_quest", "id": "q", "quest": quest(giver="character/ghost", objectives=[
            {"id": "a", "text": "x", "type": "dance", "target": "prefabs/potion.prefab"},
            {"id": "b", "text": "x", "type": "collect", "target": "prefabs/dragon.prefab"},
            {"id": "c", "text": "x", "type": "talk", "target": "character/ghost"}])},
        {"op": "add_component", "entity": 288, "component": "Talk",
         "fields": {"dialogue": "dialogue/missing.json", "character": "character/villager"}},
    ), schema, demo_context)
    text = "\n".join(errors)
    for expected in [
        "id must be lowercase",
        "speaker 'character/nobody' isn't in the World Bible",
        "goes to 'nowhere', which isn't a node",
        "no quest 'ghost-quest'",
        "node 'orphan' can't be reached",
        "giver 'character/ghost' isn't in the World Bible",
        "type must be one of",
        "'prefabs/dragon.prefab' isn't a prefab",
        "'character/ghost' isn't in the World Bible",
        "dialogue 'dialogue/missing.json' doesn't exist",
    ]:
        assert expected in text, (expected, text)


def test_a_character_added_in_the_same_proposal_can_speak(schema, demo_context):
    p = proposal(
        {"op": "command", "name": "world.write", "args": {
            "kind": "character", "id": "", "name": "Mira Quill", "summary": "Quartermaster.", "tags": "",
            "relations": "serves faction/wardens", "body": "Keeps the ledgers."}},
        {"op": "write_dialogue", "id": "mira", "dialogue": dialogue(speaker="character/mira-quill", nodes=[
            {"id": "hi", "text": "Mind the keep, Warden.", "next": ""}])},
    )
    assert validate_proposal(p, schema, demo_context) == []


def test_stray_scalars_in_object_lists_are_dropped():
    assert _drop_stray_scalars({"a": [0, {"x": 1}, {"y": 2}]}) == {"a": [{"x": 1}, {"y": 2}]}
    assert _drop_stray_scalars({"a": [1, 2, 3]}) == {"a": [1, 2, 3]}  # all scalars: a real list of numbers
    assert _drop_stray_scalars({"a": [True, {"x": 1}]}) == {"a": [True, {"x": 1}]}  # booleans aren't noise
    assert _drop_stray_scalars([{"n": [0, {"z": 1}]}]) == [{"n": [{"z": 1}]}]

def test_schedules_are_checked_against_the_scene_and_the_proposal(schema, demo_context):
    day = {"name": "Tamsin", "entries": [
        {"from": "06:00", "activity": "wander", "place": "herb_garden", "radius": 2},
        {"from": "13:00", "activity": "stay", "place": "village_well"},
        {"from": "21:00", "activity": "sleep", "place": "tamsin_home"}]}
    markers = [{"op": "command", "name": "scene.addMarker", "args": {"name": n, "x": 4.5, "y": 1.5}}
               for n in ("herb_garden", "village_well", "tamsin_home")]
    ok = proposal(*markers, {"op": "write_schedule", "id": "tamsin", "schedule": day},
                  {"op": "add_component", "entity": 288, "component": "Routine",
                   "fields": {"schedule": "schedules/tamsin.json"}})
    assert validate_proposal(ok, schema, demo_context) == []

    # The model's habit in recordings: assuming a well that isn't there. The
    # step that uses it is refused, with how to fix it.
    assumed = proposal(*markers[::2], {"op": "write_schedule", "id": "tamsin", "schedule": day})
    errors = validate_proposal(assumed, schema, demo_context)
    assert any("no marker called 'village_well' (add it with scene.addMarker)" in e for e in errors)

    broken = {"entries": [{"from": "25:00", "activity": "stay", "place": "herb_garden"},
                          {"from": "06:00", "activity": "dance", "place": "herb_garden"},
                          {"from": "06:00", "activity": "wander", "place": "herb_garden", "radius": 99}]}
    errors = validate_proposal(proposal(*markers, {"op": "write_schedule", "id": "Bad", "schedule": broken},
                                        {"op": "add_component", "entity": 288, "component": "Routine",
                                         "fields": {"schedule": "schedules/missing.json"}}), schema, demo_context)
    text = "\n".join(errors)
    for expected in ["id must be lowercase", "must be a time like 06:30", "activity must be stay, wander or sleep",
                     "radius must be between 0 and 20", "two entries start at the same time",
                     "schedule 'schedules/missing.json' doesn't exist"]:
        assert expected in text, (expected, text)


def _with_scenes(context):
    """The demo context with two scenes: main (open, no markers of its own) and a cellar with one."""
    c = context.model_copy(deep=True)
    c.scene = "scenes/main.aige"
    c.scenes = [{"path": "scenes/main.aige", "start": True, "markers": []},
                {"path": "scenes/cellar.aige", "start": False, "markers": ["cellar_door"]}]
    return c


def test_objectives_can_be_tied_to_scenes_and_reach_markers(schema, demo_context):
    # Quests belong to the project; an objective can still say where it counts (6.9).
    context = _with_scenes(demo_context)
    good = quest(objectives=[
        {"id": "down", "text": "Go down to the cellar", "type": "reach", "scene": "scenes/cellar.aige"},
        {"id": "door", "text": "Find the old door", "type": "reach", "target": "cellar_door", "scene": "scenes/cellar.aige"},
        {"id": "rats", "text": "Clear the rats", "type": "defeat", "target": "Enemy", "count": 3,
         "scene": "scenes/cellar.aige"},
    ])
    assert validate_proposal(proposal({"op": "write_quest", "id": "cellar", "quest": good}), schema, context) == []

    bad = quest(objectives=[
        {"id": "a", "text": "Go nowhere", "type": "reach"},
        {"id": "b", "text": "Find it", "type": "reach", "target": "cellar_door", "scene": "scenes/main.aige"},
        {"id": "c", "text": "Somewhere", "type": "defeat", "target": "Enemy", "scene": "scenes/attic.aige"},
    ])
    text = "\n".join(validate_proposal(proposal({"op": "write_quest", "id": "cellar", "quest": bad}), schema, context))
    for expected in ["needs a marker", "no marker called 'cellar_door' in scenes/main.aige", "no scene 'scenes/attic.aige'"]:
        assert expected in text, (expected, text)


def test_schedule_entries_can_happen_in_other_scenes(schema, demo_context):
    context = _with_scenes(demo_context)
    marker = {"op": "command", "name": "scene.addMarker", "args": {"name": "home", "x": 4.5, "y": 1.5}}
    day = {"name": "Tamsin", "entries": [
        {"from": "06:00", "activity": "stay", "place": "home"},                                   # here, added now
        {"from": "20:00", "activity": "stay", "place": "cellar_door", "scene": "scenes/cellar.aige"}]}
    assert validate_proposal(proposal(marker, {"op": "write_schedule", "id": "tamsin", "schedule": day}),
                             schema, context) == []
    day["entries"][1]["place"] = "home"  # a marker of the open scene, not of the cellar
    errors = validate_proposal(proposal(marker, {"op": "write_schedule", "id": "tamsin", "schedule": day}), schema, context)
    assert any("no marker called 'home' in scenes/cellar.aige" in e for e in errors), errors


# Narrative v2 (development_plan.md 15): stages, aliases, rewards, failure, the new
# objectives and effects, variables and world conditions, skill checks.
def test_a_staged_quest_with_aliases_validates(schema, demo_context):
    staged = {
        "name": "The thief", "giver": "character/villager",
        "aliases": [{"id": "thief", "kind": "character", "fill": "character/villager"}],
        "rewards": {"experience": 50, "gold": 20, "items": "potion*2", "faction": "watch", "reputation": 5},
        "stages": [
            {"index": 10, "journal": "A thief.", "objectives": [
                {"id": "find", "text": "Find the thief", "type": "talk", "target": "@thief"},
                {"id": "coin", "text": "Have 10 gold", "type": "have", "target": "gold-coin", "optional": True}],
             "branches": [{"if": [{"var": "mercy", "op": ">=", "value": 1}], "stage": 30}]},
            {"index": 20, "journal": "Caught.", "ends": "complete"},
            {"index": 30, "journal": "Let go.", "onEnter": [{"op": "give_gold", "amount": 5}], "ends": "complete"},
            {"index": 90, "journal": "Gone.", "ends": "fail"}],
        "onFail": [{"op": "set_flag", "flag": "escaped"}],
    }
    gate = dialogue(nodes=[
        {"id": "hi", "text": "Halt.", "portrait": "assets/guard.png", "choices": [
            {"text": "Let me pass.", "check": {"attribute": "speech", "difficulty": 5}, "next": "ok", "fail": "no"},
            {"text": "Fail it", "next": "", "do": [{"op": "set_stage", "quest": "thief", "stage": 90}]},
            {"text": "3", "next": ""}, {"text": "4", "next": ""}, {"text": "5", "next": "",
             "if": [{"quest": "thief", "is": "failed"}, {"item": "potion", "count": 1}]}]},
        {"id": "ok", "text": "Go.", "next": ""},
        {"id": "no", "text": "No.", "next": ""}])
    errors = validate_proposal(proposal(
        {"op": "write_quest", "id": "thief", "quest": staged},
        {"op": "write_dialogue", "id": "gate", "dialogue": gate},
    ), schema, demo_context)
    assert errors == [], errors


def test_staged_quest_problems_are_reported(schema, demo_context):
    bad = {
        "name": "Bad", "aliases": [{"id": "who", "fill": ""}],
        "stages": [
            {"index": 10, "objectives": [{"id": "a", "text": "A", "type": "deliver", "target": "@nobody"}],
             "next": 70},
            {"index": 20, "objectives": [{"id": "b", "text": "B", "type": "survive"}], "ends": "maybe"}],
    }
    errors = validate_proposal(proposal(
        {"op": "write_quest", "id": "bad", "quest": bad},
        {"op": "write_dialogue", "id": "d", "dialogue": dialogue(nodes=[
            {"id": "hi", "text": "Hi.", "choices": [
                {"text": "x", "next": "", "fail": "hi"},
                {"text": "y", "next": "", "do": [{"op": "set_stage", "quest": "bad", "stage": 55},
                                                 {"op": "give_item"}]}]}])},
    ), schema, demo_context)
    text = "\n".join(errors)
    for expected in ["alias 'who' isn't filled", "no alias 'nobody'", "goes to stage 70", "survive needs \"seconds\"",
                     "\"ends\" is complete or fail", "has a \"fail\" node but no \"check\"", "has no stage 55",
                     "give_item needs an \"item\""]:
        assert expected in text, (expected, text)


def test_a_cutscene_and_what_plays_it_validate_together(schema, demo_context):
    marker = next(iter(demo_context.markers), {}).get("name") if demo_context.markers else None
    actor_key = {"at": 0, "op": "move", "marker": marker} if marker else {"at": 0, "op": "move", "x": 3.5, "y": 3.5}
    cutscene = {"name": "Arrival", "tracks": [
        {"type": "actor", "actor": "character/villager", "keys": [actor_key]},
        {"type": "dialogue", "keys": [{"at": 1, "speaker": "character/villager", "text": "You came.", "seconds": 2}]},
        {"type": "logic", "keys": [{"at": 3, "do": [{"op": "set_flag", "flag": "arrived"}]}]}]}
    p = proposal(
        {"op": "write_cutscene", "id": "arrival", "cutscene": cutscene},
        {"op": "write_dialogue", "id": "greet", "dialogue": dialogue(nodes=[
            {"id": "hi", "text": "Hello.", "next": "", "do": [{"op": "play_cutscene", "cutscene": "arrival"}]}])},
    )
    assert validate_proposal(p, schema, demo_context) == []


def test_cutscene_problems_are_reported(schema, demo_context):
    bad = {"tracks": [
        {"type": "actor", "keys": [{"at": 0, "op": "move"}]},
        {"type": "screen", "keys": [{"at": 0, "op": "explode"}]},
        {"type": "dialogue", "keys": [{"at": 1, "speaker": "character/nobody", "text": ""}]},
        {"type": "actor", "actor": "player", "keys": [{"at": 0, "op": "move", "marker": "no-such-marker"}]}]}
    errors = validate_proposal(proposal(
        {"op": "write_cutscene", "id": "Bad Id", "cutscene": bad},
        {"op": "write_dialogue", "id": "d", "dialogue": dialogue(nodes=[
            {"id": "hi", "text": "Hi.", "next": "", "do": [{"op": "play_cutscene", "cutscene": "missing"}]}])},
    ), schema, demo_context)
    text = "\n".join(errors)
    for expected in ["id must be lowercase", "needs an \"actor\"", "move needs a \"marker\"", "op must be one of",
                     "a line needs \"text\"", "'character/nobody' isn't in the World Bible",
                     "cutscene 'missing' doesn't exist"]:
        assert expected in text, (expected, text)
    if demo_context.scenes:
        assert "no marker called 'no-such-marker'" in text
