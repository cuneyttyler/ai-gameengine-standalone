"""The HTTP API end to end, with a scripted provider standing in for the model."""

import json

from fastapi.testclient import TestClient

from app.main import create_app
from app.providers import ProviderError, ScriptedProvider

GOOD = json.dumps(
    {
        "summary": "Skeletons get 12 max health",
        "operations": [
            {"op": "set_prefab_field", "prefab": "prefabs/skeleton.prefab", "component": "Health", "field": "max",
             "value": 12}
        ],
    }
)
BAD = json.dumps(
    {"summary": "oops", "operations": [{"op": "set_field", "entity": 7, "component": "Health", "field": "max",
                                        "value": -5}]}
)


def client_with(answers, schema=None):
    provider = ScriptedProvider(answers)
    return TestClient(create_app(provider=provider, schema=schema)), provider


def test_health_reports_provider_and_schema(schema):
    client, _ = client_with([], schema=None)
    assert client.get("/health").json() == {"ok": True, "provider": "scripted", "model": "scripted",
                                            "schemaLoaded": False}


def test_editor_sends_its_schema(schema_json):
    client, _ = client_with([])
    response = client.put("/schema", json=schema_json)
    assert response.status_code == 200
    assert response.json()["components"] == len([c for c in schema_json["components"]])
    assert client.get("/schema").json()["version"] == 1
    assert client.get("/health").json()["schemaLoaded"] is True


def test_propose_returns_a_validated_proposal(schema, context):
    client, provider = client_with([GOOD], schema=schema)
    response = client.post("/propose", json={"instruction": "make skeletons tougher",
                                             "context": context.model_dump()}).json()
    assert response["ok"] is True
    assert response["proposal"]["operations"][0]["op"] == "set_prefab_field"
    assert response["attempts"] == 1
    # The model saw the schema and the project.
    system, user = provider.requests[0][0]["content"], provider.requests[0][-1]["content"]
    assert "- MeleeAttack" in system and "script.write" in system
    assert "Skeleton" in user and "make skeletons tougher" in user


def test_invalid_answer_gets_one_repair_round(schema, context):
    client, provider = client_with([BAD, GOOD], schema=schema)
    response = client.post("/propose", json={"instruction": "x", "context": context.model_dump()}).json()
    assert response["ok"] is True
    assert response["attempts"] == 2
    repair = provider.requests[1][-1]["content"]
    assert "max must be at least 1" in repair  # the model is told exactly what was wrong


def test_still_invalid_after_repair_reports_errors(schema, context):
    client, _ = client_with([BAD, "this is not json"], schema=schema)
    response = client.post("/propose", json={"instruction": "x", "context": context.model_dump()}).json()
    assert response["ok"] is False
    assert any("wasn't valid JSON" in e for e in response["errors"])


def test_provider_failures_are_reported_not_raised(schema, context):
    def fail(_messages):
        raise ProviderError("Mistral request failed: 401 Unauthorized")

    client, _ = client_with(fail, schema=schema)
    response = client.post("/propose", json={"instruction": "x", "context": context.model_dump()}).json()
    assert response == {**response, "ok": False, "errors": ["Mistral request failed: 401 Unauthorized"]}


def test_usage_accumulates(schema, context):
    client, _ = client_with([GOOD, BAD, GOOD], schema=schema)
    client.post("/propose", json={"instruction": "a", "context": context.model_dump()})
    client.post("/propose", json={"instruction": "b", "context": context.model_dump()})
    usage = client.get("/usage").json()
    assert usage["requests"] == 3
    assert usage["inputTokens"] == 300


def test_missing_schema_or_instruction(schema, context):
    client, _ = client_with([GOOD])
    assert client.post("/propose", json={"instruction": "x"}).json()["ok"] is False
    client, _ = client_with([GOOD], schema=schema)
    assert client.post("/propose", json={"instruction": "  "}).json()["errors"] == ["Say what you'd like to change"]


def test_without_a_provider_the_service_says_how_to_configure_one(schema):
    client = TestClient(create_app(provider=None, schema=schema))
    response = client.post("/propose", json={"instruction": "x"}).json()
    assert "MISTRAL_API_KEY" in response["errors"][0]


def test_a_split_answer_comes_back_with_its_suggestions(schema, context):
    split = json.dumps(
        {"scope": "split", "summary": "That's a whole game", "split": ["add a dodge roll", "add a boss"],
         "notes": "Too much for one go."}
    )
    client, provider = client_with([split], schema=schema)
    response = client.post("/propose", json={"instruction": "make an RPG", "context": context.model_dump()}).json()
    assert response["ok"] is True
    assert response["proposal"]["scope"] == "split"
    assert response["proposal"]["split"] == ["add a dodge roll", "add a boss"]
    assert response["proposal"]["operations"] == []
    # The model was told how to size requests.
    assert '"scope": "small" | "task" | "split"' in provider.requests[0][0]["content"]
