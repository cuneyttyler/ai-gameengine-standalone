"""AI packages written by the AI panel (docs/ai/ai-packages.md): templates' data, own procedures, scripts, places."""
import json

from app.packages import check_package, check_procedure, check_stacks

SCRIPTS = {"scripts/vara.lua"}
NAMES = {"house-4", "Vara Bell"}


def errors(record):
    return check_package("p", record, "op 1", SCRIPTS, NAMES)


def test_a_template_with_what_it_needs_passes():
    assert errors({"template": "live", "location": {"building": "house-4"}, "data": {"action": "cook", "radius": 4}}) == []
    assert errors({"template": "wander", "location": {"marker": "square"}, "data": {"radius": 3}}) == []


def test_a_template_missing_its_data_is_said():
    assert any("data.action" in e for e in errors({"template": "use-part", "location": {"building": "house-4"}}))
    assert any("target" in e for e in errors({"template": "follow"}))
    assert any("data.script" in e for e in errors({"template": "script"}))


def test_a_script_must_exist_or_be_written():
    assert errors({"template": "script", "data": {"script": "scripts/vara.lua"}}) == []
    assert any("doesn't exist" in e for e in errors({"template": "script", "data": {"script": "scripts/other.lua"}}))


def test_places_are_things_in_the_scene():
    assert any("house-9" in e for e in errors({"template": "live", "location": {"building": "house-9"}, "data": {"action": "cook"}}))


def test_an_own_procedure_is_checked_step_by_step():
    good = {"type": "loop", "children": [{"type": "random", "weights": [2, 1], "children": [
        {"type": "wander", "around": "@location", "radius": 4},
        {"type": "sequence", "children": [{"type": "use-part", "part": "cook", "seconds": 6}, {"type": "wait", "min": 2, "max": 4}]}]}]}
    assert errors({"template": "custom", "procedure": json.dumps(good)}) == []
    assert errors({"template": "custom", "procedure": good}) == []  # as an object too
    bad = {"type": "loop", "children": [{"type": "random", "weights": [1], "children": [
        {"type": "teleport"}, {"type": "use-part"}, {"type": "wait", "seconds": "soon"}]}]}
    found = check_procedure(bad, "op 1")
    assert any("teleport" in e for e in found)
    assert any("use-part" in e and "'part'" in e for e in found)
    assert any("weights" in e for e in found)
    assert any("wait" in e and "number" in e for e in found)
    assert any("procedure" in e for e in errors({"template": "custom"}))
    assert any("isn't JSON" in e for e in errors({"template": "custom", "procedure": "{loop"}))


def test_a_stack_names_packages_that_exist():
    known = {"vara-live"}
    assert check_stacks([("op 2", "vara-live")], known.__contains__) == []
    assert any("vara-cook" in e for e in check_stacks([("op 2", "vara-live, vara-cook")], known.__contains__))


def test_paths_named_by_packages_exist_or_are_added():
    from app.packages import check_package
    paths = {"lane"}
    ok = {"template": "follow-path", "data": {"route": "lane", "mode": "wander"}}
    assert check_package("p", ok, "op 1", SCRIPTS, NAMES, paths) == []
    missing = {"template": "patrol", "data": {"route": "market-loop"}}
    assert any("market-loop" in e for e in check_package("p", missing, "op 1", SCRIPTS, NAMES, paths))
    tree = {"type": "loop", "children": [{"type": "follow-path", "path": "nowhere"}]}
    assert any("nowhere" in e for e in check_package("p", {"template": "custom", "procedure": tree}, "op 1", SCRIPTS, NAMES, paths))
    bad_mode = {"template": "follow-path", "data": {"route": "lane", "mode": "sometimes"}}
    assert any("mode" in e for e in check_package("p", bad_mode, "op 1", SCRIPTS, NAMES, paths))


def test_add_path_is_checked_as_the_engine_reads_it():
    from app.packages import check_add_path
    good = {"name": "lane", "points": "[[1.5, 1.5], [4.5, 1.5], [4.5, 6.5]]",
            "links": json.dumps([{"from": 0, "to": 1}, {"from": 1, "to": 2, "weight": 2, "if": [{"flag": "x"}]}]), "end": "back"}
    assert check_add_path(good, "op 1") == []
    assert any("two points" in e for e in check_add_path({"points": "[[1, 1]]"}, "op 1"))
    assert any("end" in e for e in check_add_path({**good, "end": "forever"}, "op 1"))
    broken = {**good, "links": json.dumps([{"from": 0, "to": 7}, {"from": 0, "to": 1, "weight": 0}, {"from": 1, "to": 2, "if": "{"}])}
    found = check_add_path(broken, "op 1")
    assert any("joins points" in e for e in found)
    assert any("weight" in e for e in found)
    assert any("conditions" in e for e in found)
