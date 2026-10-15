"""The sprite pipeline and the Asset Generator Studio endpoint
(development_plan.md 3.6), with synthetic images and a fake generator."""

import io
import json

from fastapi.testclient import TestClient
from PIL import Image, ImageDraw

from app.images import GeneratedImage, build_prompt, generate_sprites
from app.main import create_app
from app.sprites import SpriteOptions, crop_to_content, make_sprite, remove_background


def drawn_object(size=(400, 300), interior_white=True, fmt="JPEG") -> bytes:
    """A red disc on white, anti-aliased (drawn 4x and scaled down), with a
    white patch inside it — like a generated picture of an object."""
    w, h = size
    big = Image.new("RGB", (w * 4, h * 4), (255, 255, 255))
    d = ImageDraw.Draw(big)
    d.ellipse((150 * 4, 80 * 4, 290 * 4, 220 * 4), fill=(200, 40, 30))
    if interior_white:
        d.rectangle((205 * 4, 135 * 4, 235 * 4, 165 * 4), fill=(255, 255, 255))
    image = big.resize((w, h), Image.Resampling.LANCZOS)
    out = io.BytesIO()
    image.save(out, format=fmt, quality=92)
    return out.getvalue()


def test_the_background_goes_but_white_inside_the_object_stays():
    image = Image.open(io.BytesIO(drawn_object()))
    sprite = remove_background(image, tolerance=40)
    px = sprite.load()
    assert px[0, 0][3] == 0 and px[399, 299][3] == 0 and px[100, 150][3] == 0  # backdrop
    assert px[220, 150][3] == 255  # the white patch inside the disc isn't background
    assert px[170, 150][3] == 255  # the disc itself
    # No light halo: every fully opaque pixel touching the cleared backdrop is
    # clearly not backdrop-coloured (the anti-aliased rim was cleared or faded).
    w, h = sprite.size
    for y in range(1, h - 1):
        for x in range(1, w - 1):
            r, g, b, a = px[x, y]
            if a == 255 and any(px[nx, ny][3] == 0 for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1))):
                assert min(255 - r, 255 - g, 255 - b) > 0 and max(255 - r, 255 - g, 255 - b) > 120, (x, y, px[x, y])


def test_crop_scale_and_palette():
    png, info = make_sprite(drawn_object(), SpriteOptions(height=48, style="pixel", colors=16))
    sprite = Image.open(io.BytesIO(png))
    assert sprite.mode == "RGBA"
    assert sprite.size[1] == 48 and abs(sprite.size[0] - 48) <= 2  # the disc is round
    assert info["croppedSize"][0] < 160 and info["croppedSize"][1] < 160  # cropped to the disc, not the frame
    alphas = {a for *_, a in sprite.getdata()}
    assert alphas <= {0, 255}  # pixel style: hard edges
    assert len({tuple(rgb) for *rgb, a in sprite.getdata() if a}) <= 16
    assert sprite.getpixel((0, 0))[3] == 0  # corners stay transparent

    smooth, info = make_sprite(drawn_object(), SpriteOptions(height=96, style="smooth"))
    sprite = Image.open(io.BytesIO(smooth))
    assert sprite.size[1] == 96 and info["colors"] is None
    # No visible white fringe after scaling (premultiplied resize): the soft edge
    # exists, and whatever in it is mostly opaque isn't backdrop-white.
    edge = [p for p in sprite.getdata() if 0 < p[3] < 255]
    assert edge
    assert all(not (r > 235 and g > 235 and b > 235) for r, g, b, a in edge if a >= 128)


def test_an_empty_picture_survives():
    blank = io.BytesIO()
    Image.new("RGB", (64, 64), (255, 255, 255)).save(blank, format="PNG")
    sprite = crop_to_content(remove_background(Image.open(io.BytesIO(blank.getvalue()))))
    assert sprite.size == (64, 64)  # nothing to crop to; doesn't fail


class FakeImages:
    name = "fake"

    def __init__(self):
        self.prompts = []

    def generate(self, prompt):
        self.prompts.append(prompt)
        return GeneratedImage(drawn_object(), "fake-flux", prompt + " (as sent)")


def test_variants_become_a_tagged_family(tmp_path):
    images = FakeImages()
    results = generate_sprites(images, tmp_path, "barrel", "wooden barrel with iron bands", "object", 40, "pixel",
                               ["mossy", "broken open"])
    assert [r["path"] for r in results] == [
        "assets/textures/generated/barrel-mossy.png",
        "assets/textures/generated/barrel-broken-open.png",
    ]
    folder = tmp_path / "assets" / "textures" / "generated"
    meta = json.loads((folder / "barrel-mossy.png.gen.json").read_text(encoding="utf-8"))
    assert meta["family"] == "barrel" and meta["variant"] == "mossy" and meta["kind"] == "object"
    assert meta["model"] == "fake-flux" and meta["provider"] == "fake"
    assert meta["seed"] is None  # recorded honestly: not exposed
    assert "wooden barrel with iron bands, mossy" in meta["prompt"]
    assert "pure white background" in meta["prompt"] and "isometric" in meta["prompt"]
    assert meta["providerPrompt"].endswith("(as sent)")
    assert Image.open(folder / "barrel-mossy.png").size[1] == 40
    assert len(images.prompts) == 2


def test_prompts_ask_for_something_the_pipeline_can_cut_out():
    prompt = build_prompt("old herbalist woman with a basket", "character", "pixel")
    assert prompt.startswith("a single old herbalist woman with a basket, full body")
    assert "plain pure white background" in prompt and "no shadow" in prompt and "pixel art" in prompt


def test_the_endpoint(tmp_path):
    images = FakeImages()
    client = TestClient(create_app(images=images))
    body = {"name": "barrel", "description": "a barrel"}
    assert client.post("/assets/generate", json=body).json()["ok"] is False  # no project yet
    client.put("/project", json={"root": str(tmp_path)})
    result = client.post("/assets/generate", json=body).json()
    assert result["ok"] and result["assets"][0]["path"] == "assets/textures/generated/barrel.png"
    for bad in [{"name": "../escape", "description": "x"}, {"name": "Barrel", "description": "x"},
                {"name": "b", "description": " "}, {"name": "b", "description": "x", "kind": "tile"},
                {"name": "b", "description": "x", "height": 4},
                {"name": "b", "description": "x", "variants": list("abcdefg")}]:
        answer = client.post("/assets/generate", json=bad).json()
        assert answer["ok"] is False and answer["errors"], bad
    assert not (tmp_path.parent / "escape.png").exists()
