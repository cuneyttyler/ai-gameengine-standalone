"""The progress strips' feed (app/progress.py): each POST is one entry, the provider's streamed
text counts on it while the request runs, and GET /progress shows it."""

import json

from fastapi.testclient import TestClient

from app import progress
from app.main import create_app
from app.providers import ScriptedProvider

OUTLINE = {"outline": {"story": {"acts": [{"id": "act-1"}]}, "cast": [{"id": "character/a", "player": True}],
                       "places": [], "connections": []}, "rationale": ""}


def test_a_request_in_flight_shows_its_label_and_tokens():
    seen: list[dict] = []

    def answer(messages):
        seen.append(progress.snapshot())  # while the request runs, from the provider's thread
        return json.dumps(OUTLINE)

    client = TestClient(create_app(provider=ScriptedProvider(answer)))
    response = client.post("/planner/outline", json={"label": "② Outline"})
    assert response.json()["ok"]
    assert seen, "the provider was called"
    during = [c for c in seen[0]["calls"] if c["label"] == "② Outline"]
    assert during and during[0]["path"] == "/planner/outline"
    assert during[0]["calls"] == 1
    # Just after: shown done for a moment, with the provider's real counts.
    after = [c for c in client.get("/progress").json()["calls"] if c["label"] == "② Outline"]
    assert after and after[0]["phase"] == "done"
    assert after[0]["outputTokens"] >= 50 and after[0]["inputTokens"] >= 100


def test_a_request_with_no_model_call_isnt_shown():
    client = TestClient(create_app(provider=ScriptedProvider([])))
    client.post("/planner/no-such-step", json={})
    assert not [c for c in client.get("/progress").json()["calls"] if c["path"] == "/planner/no-such-step"]
