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


# --- catalog v2 (7.3) ---------------------------------------------------------------

def test_small_pixel_art_is_enlarged_and_objects_are_cropped(tmp_path):
    import base64
    import io

    from app.vision import image_chunk
    folder = tmp_path / "assets"
    folder.mkdir()
    Image.new("RGBA", (16, 16), (200, 0, 0, 255)).save(folder / "tiny.png")
    chunk, size = image_chunk(folder / "tiny.png")
    shown = Image.open(io.BytesIO(base64.b64decode(chunk["image_url"].split(",", 1)[1])))
    assert size == (16, 16) and shown.size == (256, 256)  # x16, pixels kept sharp

    sheet = Image.new("RGBA", (200, 100), (0, 0, 0, 0))
    sheet.paste((0, 200, 0, 255), (120, 10, 160, 90))
    sheet.save(folder / "sheet.png")
    chunk, size = image_chunk(folder / "sheet.png", [120, 10, 40, 80])
    assert size == (40, 80)


def test_roles_perspective_and_anchors(tmp_path):
    paths = make_images(tmp_path, ["house.png", "fence.png"])
    roles = ["house", "fence-piece-set", "tree"]

    def answer(messages):
        text = messages[0]["content"][0]["text"]
        assert "fence-piece-set" in text  # the ontology's roles are offered
        assert sum(1 for c in messages[0]["content"] if "cut from a sheet" in c.get("text", "")) == 1
        return json.dumps({"assets": [
            {"index": 1, "name": "cottage", "kind": "building", "description": "A cottage.", "tags": ["house"],
             "direction": "none", "sizeCells": 2.5, "blocks": True, "perspective": "3/4", "role": "house",
             "anchors": {"Door": [0.5, 1.4], "bad": "x"}, "accessible": True},
            {"index": 2, "name": "fence", "kind": "decoration", "description": "A fence.", "tags": ["fence"],
             "direction": "horizontal", "sizeCells": 1, "blocks": True, "perspective": "sideways", "role": "spaceship"},
        ]})

    items = [{"id": "assets/pack/fence.png#0", "path": "assets/pack/fence.png", "rect": [0, 0, 100, 50]},
             {"id": "bad", "path": "assets/pack/fence.png", "rect": [0, 0]}]
    result = describe_assets(ScriptedProvider(answer), tmp_path, ["assets/pack/house.png"], workers=1,
                             items=items, roles=roles)
    house, fence = result["assets"]
    assert house["id"] == "assets/pack/house.png" and house["perspective"] == "three-quarter"
    assert house["role"] == "house" and house["accessible"] is True
    assert house["anchors"] == {"door": [0.5, 1.0]}  # clamped; the bad one dropped
    assert fence["id"] == "assets/pack/fence.png#0" and fence["path"] == "assets/pack/fence.png"
    assert fence["perspective"] == "unknown" and fence["role"] == ""  # not one of the roles offered
    assert any(e.startswith("bad:") for e in result["errors"])
