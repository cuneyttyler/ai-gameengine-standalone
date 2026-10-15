"""Contradiction checking (development_plan.md 3.3, technical.md §14: seeded
World Bible + known-contradictory writing -> expect a flag)."""

import json
import shutil
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from app.consistency import parse_issues, writing_from_operations
from app.main import create_app
from app.providers import ScriptedProvider

CHECKS = Path(__file__).parent / "golden" / "checks"
DEMO = Path(__file__).resolve().parents[2] / "projects" / "demo"


def records():
    return [json.loads(p.read_text(encoding="utf-8")) for p in sorted(CHECKS.glob("*.json"))]


@pytest.mark.parametrize("record", records(), ids=lambda r: r["name"])
def test_the_checker_flags_contradictions_and_only_those(record):
    issues, errors = parse_issues(record["response"], record["canon"], len(record["operations"]), record["writing"])
    assert errors == []
    if record["expect"]:
        flagged = [i for i in issues if i.conflictsWith == record["expect"] and i.severity == "contradiction"]
        assert flagged, f"{record['name']} should be flagged against {record['expect']}: {issues}"
        assert flagged[0].op == 1
    else:
        assert issues == [], f"{record['name']} is consistent with the canon: {issues}"


def test_writing_is_extracted_from_the_operations_that_make_claims():
    text = writing_from_operations([
        {"op": "set_field", "entity": 1, "component": "Health", "field": "max", "value": 3},
        {"op": "command", "name": "world.write", "args": {
            "kind": "character", "id": "mira", "name": "Mira", "summary": "Quartermaster.", "relations": "",
            "tags": "", "body": "Keeps the ledgers."}},
        {"op": "write_dialogue", "id": "mira", "dialogue": {"speaker": "character/mira", "nodes": [
            {"id": "a", "text": "Mind the keep.", "choices": [{"text": "Why?", "next": ""}]}]}},
        {"op": "write_quest", "id": "q", "quest": {"name": "Ledgers", "giver": "character/mira",
                                                   "description": "Find the ledger.", "objectives": [{"text": "Find it"}]}},
    ])
    assert "[op 1]" not in text  # a field change says nothing about the world
    assert "[op 2] New World Bible entry character/mira: Mira" in text and "Keeps the ledgers." in text
    assert "[op 3]" in text and "character/mira: Mind the keep." in text and "player: Why?" in text
    assert "[op 4] Quest quests/q.json: Ledgers" in text and "- Find it" in text
    assert writing_from_operations([{"op": "set_setting", "section": "Combat", "field": "x", "value": 1}]) == ""


def test_unverifiable_issues_are_dropped():
    canon = {"character/villager": "[character/villager] Old Tamsin — Brookvale's herb-keeper.\nShe brews draughts."}
    writing = "[op 1] Dialogue\ncharacter/villager: I'm the blacksmith. I love iron."
    answer = json.dumps({"issues": [
        {"op": 1, "quote": "I'm the blacksmith.", "conflictsWith": "character/villager/Old Tamsin",
         "canonSays": "Brookvale's herb-keeper.", "severity": "contradiction", "explanation": "x"},  # kept (key suffix)
        {"op": 1, "quote": "I love iron.", "conflictsWith": "character/villager",
         "canonSays": "The canon doesn't mention iron.", "severity": "contradiction"},  # invented evidence
        {"op": 1, "quote": "I hate herbs.", "conflictsWith": "character/villager",
         "canonSays": "Brookvale's herb-keeper.", "severity": "contradiction"},  # quote not in the writing
        {"op": 1, "quote": "I'm the blacksmith.", "conflictsWith": "faction/unknown",
         "canonSays": "Brookvale's herb-keeper.", "severity": "contradiction"},  # an entry it wasn't shown
        {"op": 9, "quote": "I love iron.", "conflictsWith": "character/villager",
         "canonSays": "Old Tamsin. She brews draughts.", "severity": "odd"},  # kept: op and severity normalized
    ]})
    issues, errors = parse_issues(answer, canon, op_count=1, writing=writing)
    assert errors == []
    assert [(i.quote, i.conflictsWith, i.op, i.severity) for i in issues] == [
        ("I'm the blacksmith.", "character/villager", 1, "contradiction"),
        ("I love iron.", "character/villager", None, "possible"),
    ]
    assert parse_issues("not json", canon, 1)[1]
    assert parse_issues('{"nope": []}', canon, 1)[1]


def test_the_endpoint_checks_against_retrieved_canon(tmp_path):
    shutil.copytree(DEMO / "world", tmp_path / "world")
    contradiction = {"op": "write_dialogue", "id": "x", "dialogue": {"speaker": "character/villager", "start": "a",
                     "nodes": [{"id": "a", "text": "Forty years I've been the village blacksmith.", "next": ""}]}}
    answer = json.dumps({"issues": [{"op": 1, "quote": "Forty years I've been the village blacksmith.",
                                     "conflictsWith": "character/villager",
                                     "canonSays": "Brookvale's herb-keeper, who brews the red healing draughts.",
                                     "explanation": "She's the herb-keeper.", "severity": "contradiction"}]})
    checker = ScriptedProvider([answer])
    client = TestClient(create_app(embedder=None, checker=checker))

    assert client.post("/consistency", json={"operations": [contradiction]}).json()["ok"] is False  # no project yet
    client.put("/project", json={"root": str(tmp_path)})
    nothing = client.post("/consistency", json={"operations": [{"op": "set_field"}]}).json()
    assert nothing["checked"] is False and nothing["ok"] is True

    result = client.post("/consistency", json={"operations": [contradiction]}).json()
    assert result["checked"] and result["issues"][0]["conflictsWith"] == "character/villager"
    assert "character/villager" in result["canonKeys"]
    sent = checker.requests[0][-1]["content"]
    assert "Old Tamsin" in sent and "NEW WRITING" in sent

    # Re-checking an entry: it isn't compared with itself.
    rewrite = {"op": "command", "name": "world.write", "args": {"kind": "character", "id": "villager",
               "name": "Old Tamsin", "summary": "Herb-keeper.", "tags": "", "relations": "", "body": ""}}
    checker2 = ScriptedProvider([json.dumps({"issues": []})])
    client2 = TestClient(create_app(embedder=None, checker=checker2))
    client2.put("/project", json={"root": str(tmp_path)})
    assert "character/villager" not in client2.post("/consistency", json={"operations": [rewrite]}).json()["canonKeys"]
