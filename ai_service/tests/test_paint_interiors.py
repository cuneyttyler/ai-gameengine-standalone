"""A painted place's buildings' interiors (matched.paint_interiors, docs/places/interiors.md §7): each building checked by
the vision model, then its floors, its exterior on its own and its layout written beside them."""
import json

from PIL import Image, ImageDraw

from app import matched
from app.providers import Completion


def painted_place(tmp_path):
    """A painting with a house (a brown box with a red roof) at 100-200 x 80-200, and its occluder's mask."""
    painting = Image.new("RGB", (400, 300), (90, 140, 70))
    draw = ImageDraw.Draw(painting)
    draw.rectangle([100, 120, 200, 200], fill=(140, 100, 60))
    draw.polygon([(95, 120), (150, 80), (205, 120)], fill=(170, 50, 40))
    painting.save(tmp_path / "painting.png")
    (tmp_path / "marks").mkdir()
    Image.new("RGBA", (110, 120), (255, 255, 255, 255)).save(tmp_path / "marks" / "o1.png")
    return [{"id": "o1", "box": [95, 80, 110, 120], "mask": "marks/o1.png", "role": "house", "storeys": 1}]


class Vision:
    """Says whether there's a building, and where (thousandths of the crop it's shown)."""

    def __init__(self, building=True, box=(250, 250, 750, 750)):
        self.building, self.box, self.asked = building, box, 0

    def complete(self, messages):
        self.asked += 1
        if not self.building:
            return Completion(json.dumps({"building": False}))
        return Completion(json.dumps({"building": True, "box_2d": list(self.box)}))


def no_painting(monkeypatch):
    """The floors 'painted' as the exterior itself (no BFL in tests)."""
    monkeypatch.setattr(matched, "make_floors", lambda painter, exterior, *a, **k: [exterior.copy()])
    monkeypatch.setattr(matched, "count_storeys", lambda vision, exterior: 1)


def test_a_building_found_gets_its_floor_exterior_and_layout(tmp_path, monkeypatch):
    no_painting(monkeypatch)
    items = painted_place(tmp_path)
    made = matched.paint_interiors(None, tmp_path, items, {}, 7, vision=Vision())
    assert made == ["o1"]
    assert (tmp_path / "marks" / "o1-interior.png").is_file()
    assert (tmp_path / "marks" / "o1-exterior.png").is_file()
    with Image.open(tmp_path / "marks" / "o1-exterior.png") as ext:
        assert ext.size == (110, 120)  # the occluder's box: it lies exactly over the painted building
    info = json.loads((tmp_path / "marks" / "o1-interior.json").read_text())
    assert info["storeys"] == 1 and info["exterior"] == "marks/o1-exterior.png"
    assert len(info["layout"].split(";")) == 6  # bx,by;rx,ry;lx,ly;storey;w,h;along,deep
    assert info["check"]["building"] is True and 0.0 <= info["check"]["outside"] <= 1.0


def test_no_building_seen_gets_no_interior(tmp_path, monkeypatch):
    no_painting(monkeypatch)
    items = painted_place(tmp_path)
    made = matched.paint_interiors(None, tmp_path, items, {}, 7, vision=Vision(building=False))
    assert made == []
    assert not (tmp_path / "marks" / "o1-interior.png").exists()
    assert json.loads((tmp_path / "marks" / "o1-interior.json").read_text())["check"] == {"building": False}


def test_a_building_past_its_outline_is_measured(tmp_path, monkeypatch):
    no_painting(monkeypatch)
    items = painted_place(tmp_path)
    # The whole crop said to be the building: it reaches well past the outline's box.
    matched.paint_interiors(None, tmp_path, items, {}, 7, vision=Vision(box=(0, 0, 1000, 1000)))
    info = json.loads((tmp_path / "marks" / "o1-interior.json").read_text())
    assert info["check"]["outside"] > 0.25


def test_without_a_vision_model_it_goes_on_unchecked(tmp_path, monkeypatch):
    no_painting(monkeypatch)
    items = painted_place(tmp_path)
    assert matched.paint_interiors(None, tmp_path, items, {}, 7, vision=None) == ["o1"]
    assert json.loads((tmp_path / "marks" / "o1-interior.json").read_text())["check"] == {}
