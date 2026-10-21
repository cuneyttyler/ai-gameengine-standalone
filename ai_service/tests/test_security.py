"""Only the editor that started the service is answered (development_plan.md 11)."""
from fastapi.testclient import TestClient

from app.main import create_app


def test_without_a_token_the_service_is_open(monkeypatch):
    monkeypatch.delenv("AIGE_SERVICE_TOKEN", raising=False)
    client = TestClient(create_app(provider=None))
    assert client.get("/health").status_code == 200
    assert client.post("/places/questions", json={"query": "", "brief": {}, "context": {}}).status_code != 401


def test_with_a_token_only_the_editor_is_answered(monkeypatch):
    monkeypatch.setenv("AIGE_SERVICE_TOKEN", "s3cret")
    client = TestClient(create_app(provider=None), base_url="http://127.0.0.1:8765")
    # /health tells nothing, and answers anyone (the editor checks it's up).
    assert client.get("/health").status_code == 200
    body = {"query": "a village", "brief": {}, "context": {}}
    assert client.post("/places/questions", json=body).status_code == 401
    assert client.post("/places/questions", json=body, headers={"X-AIGE-Token": "wrong"}).status_code == 401
    assert client.post("/places/questions", json=body, headers={"X-AIGE-Token": "s3cret"}).status_code != 401
    # A page that reaches it by another name (DNS rebinding) is refused, token or not.
    other = TestClient(create_app(provider=None), base_url="http://evil.example:8765")
    assert other.post("/places/questions", json=body, headers={"X-AIGE-Token": "s3cret"}).status_code == 403
