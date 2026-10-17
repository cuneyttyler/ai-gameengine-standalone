"""Image analysis (development_plan.md 6.2), with a scripted vision model."""
import json

from fastapi.testclient import TestClient
from PIL import Image

from app.main import create_app
from app.providers import ScriptedProvider
from app.vision import BATCH, describe_assets


def make_images(root, names):
    folder = root / "assets" / "pack"
    folder.mkdir(parents=True, exist_ok=True)
    for name in names:
        Image.new("RGBA", (300, 200), (120, 90, 60, 255)).save(folder / name)
    return [f"assets/pack/{name}" for name in names]


def answer_for(messages):
    """Describes every image it was shown, numbered as asked."""
    content = messages[0]["content"]
    shown = sum(1 for chunk in content if chunk.get("type") == "image_url")
    return json.dumps({"assets": [
        {"index": i, "name": f"Stone Well #{i}", "kind": "Building", "description": "A stone well.\nFor water.",
         "tags": ["Well", "water", "well", "  "], "direction": "none", "sizeCells": 1.2, "blocks": True}
        for i in range(1, shown + 1)
    ]})


def test_every_image_is_shown_and_described(tmp_path):
    paths = make_images(tmp_path, [f"Prop {i}.png" for i in range(BATCH + 2)])
    provider = ScriptedProvider(answer_for)
    result = describe_assets(provider, tmp_path, paths, workers=1)
    assert result["ok"] and not result["errors"]
    assert [a["path"] for a in result["assets"]] == paths  # in the order asked
    assert len(provider.requests) == 2  # BATCH per request
    first = provider.requests[0][0]["content"]
    images = [c for c in first if c["type"] == "image_url"]
    assert len(images) == BATCH and images[0]["image_url"].startswith("data:image/png;base64,")
    assert any("300x200 pixels" in c.get("text", "") for c in first)  # the real size, as a hint
    described = result["assets"][0]
    assert described["name"] == "stone-well-1"
    assert described["kind"] == "building"
    assert described["description"] == "A stone well. For water."
    assert described["tags"] == ["well", "water"]
    assert described["sizeCells"] == 1.2 and described["blocks"] is True


def test_bad_answers_are_reported_and_cleaned(tmp_path):
    paths = make_images(tmp_path, ["a.png", "b.png", "c.png"])

    def sloppy(messages):
        return json.dumps({"assets": [
            {"index": 1, "name": "x", "kind": "spaceship", "description": "d", "tags": [], "direction": "none",
             "sizeCells": 99, "blocks": False},
            {"index": 2, "name": "y", "kind": "item", "description": "d", "tags": [], "direction": "diagonal",
             "sizeCells": 1, "blocks": False},
            {"index": 7, "name": "z"},
        ]})

    result = describe_assets(ScriptedProvider(sloppy), tmp_path, paths)
    assert [a["path"] for a in result["assets"]] == ["assets/pack/a.png"]
    assert result["assets"][0]["kind"] == "other" and result["assets"][0]["sizeCells"] == 20.0
    assert any("b.png" in e for e in result["errors"])  # a direction that doesn't exist
    assert any("c.png: not described" in e for e in result["errors"])


def test_only_project_images(tmp_path):
    paths = make_images(tmp_path, ["a.png"])
    (tmp_path / "notes.txt").write_text("x")
    result = describe_assets(ScriptedProvider(answer_for), tmp_path, paths + ["../outside.png", "notes.txt"])
    assert len(result["assets"]) == 1
    assert len(result["errors"]) == 2


def test_the_endpoint(tmp_path):
    paths = make_images(tmp_path, ["a.png"])
    client = TestClient(create_app(vision=ScriptedProvider(answer_for)))
    assert client.post("/assets/describe", json={"paths": paths}).json()["ok"] is False  # no project yet
    client.put("/project", json={"root": str(tmp_path)})
    answer = client.post("/assets/describe", json={"paths": paths}).json()
    assert answer["ok"] and answer["assets"][0]["kind"] == "building"
    no_vision = TestClient(create_app())
    no_vision.put("/project", json={"root": str(tmp_path)})
    assert no_vision.post("/assets/describe", json={"paths": paths}).json()["ok"] is False
