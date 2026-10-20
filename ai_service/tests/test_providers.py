"""Choosing the model (Mistral or Claude) and Claude's adapter, without the network."""
from fastapi.testclient import TestClient

import app.main as main
from app.main import create_app
from app.providers import ClaudeProvider, ScriptedProvider


def test_claude_takes_the_services_messages():
    system, messages = ClaudeProvider._convert([
        {"role": "system", "content": "You plan places."},
        {"role": "user", "content": [{"type": "text", "text": "The map:"},
                                     {"type": "image_url", "image_url": "data:image/png;base64,AAAA"}]},
        {"role": "assistant", "content": "ok"},
        {"role": "user", "content": "again"},
    ])
    assert system == "You plan places."
    assert messages[0]["content"][1] == {"type": "image", "source": {"type": "base64", "media_type": "image/png", "data": "AAAA"}}
    assert [m["role"] for m in messages] == ["user", "assistant", "user"]
    assert ClaudeProvider.reasoning  # it thinks apart from its answer: native mode


def test_the_provider_is_chosen_by_the_editor(monkeypatch):
    claude = ScriptedProvider(["{}"])
    claude.name, claude.model = "claude", "claude-opus-5-5"
    monkeypatch.setattr(main, "available_providers", lambda: {"mistral": True, "claude": True})
    monkeypatch.setattr(main, "make_provider", lambda name, model=None: claude if name == "claude" else None)
    client = TestClient(create_app(provider=ScriptedProvider(["{}"])))
    reply = client.put("/provider", json={"provider": "claude"}).json()
    assert reply["ok"] and reply["provider"] == "claude" and reply["model"] == "claude-opus-5-5"
    assert client.get("/provider").json()["provider"] == "claude"
    assert client.put("/provider", json={"provider": "gpt"}).json()["ok"] is False
    # No key: said so, the current one stays.
    reply = client.put("/provider", json={"provider": "mistral"}).json()
    assert reply["ok"] is False and "MISTRAL_API_KEY" in reply["errors"][0]
    assert client.get("/provider").json()["provider"] == "claude"
