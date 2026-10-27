"""Other looks of a kind painted from their own words (matched.derive_pieces, builder_tests/variant_test)."""
import io
import json
from pathlib import Path

from PIL import Image, ImageDraw

from app import matched
from app.providers import Completion


class Writer:
    """The LLM: two descriptions, different in form."""

    def __init__(self):
        self.asked = []

    def complete(self, messages):
        self.asked.append(messages)
        return Completion(json.dumps({"variants": ["a two-storey timber house with a balcony", "a round stone hut with a thatched roof"]}))


class Painter:
    """A house-shaped blob on a dark maroon background (not the key: as models often paint it)."""

    def __init__(self):
        self.prompts = []

    def create(self, prompt, width=1024, height=1024, seed=None):
        self.prompts.append(prompt)
        image = Image.new("RGB", (width, height), (90, 30, 50))
        ImageDraw.Draw(image).rectangle([300, 300, 700, 760], fill=(160, 120, 70))
        out = io.BytesIO()
        image.save(out, format="PNG")
        return out.getvalue()

    def repaint(self, *args, **kwargs):  # the edit way: not used when the words are there
        raise AssertionError("a look with its own words isn't an edit")


def test_each_other_look_is_painted_from_its_own_words(tmp_path: Path):
    (tmp_path / "pieces").mkdir()
    Image.new("RGBA", (64, 64), (160, 120, 70, 255)).save(tmp_path / "pieces" / "house.png")
    first = {"key": "house", "slug": "house", "role": "house", "concepts": ["cottage"], "w": 3, "h": 3, "purpose": "a home", "layer": "building"}
    looks = [dict(first, key="house#v2", slug="house-v2", variant=2, variantKey="house"),
             dict(first, key="house#v3", slug="house-v3", variant=3, variantKey="house")]
    writer, painter = Writer(), Painter()
    made = matched.derive_pieces(painter, tmp_path, looks, [first] + looks, {"place": {"name": "Elm", "type": "village"}}, 7, writer=writer)
    assert sorted(made) == ["house-v2", "house-v3"]
    assert len(writer.asked) == 1  # one call for the kind
    assert any("balcony" in p for p in painter.prompts) and any("round stone hut" in p for p in painter.prompts)
    assert all("image 2" not in p for p in painter.prompts)  # no reference image
    # Cut from the maroon: only the house is left, its background clear.
    cut = Image.open(tmp_path / "pieces" / "house-v2.png")
    assert cut.getpixel((0, 0))[3] == 0 and cut.width < 500
    assert json.loads((tmp_path / "variant-prompts.json").read_text())["house#v2"].startswith("a two-storey")
