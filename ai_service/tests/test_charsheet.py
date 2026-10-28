"""Character sheets (app/charsheet.py): a clip each direction packed into slots, a clip added later in new rows."""
from PIL import Image

from app import charsheet


def _frame(colour):
    return Image.new("RGBA", (32, 32), colour)


def test_a_clip_is_every_direction_and_a_later_clip_takes_new_rows(tmp_path):
    sheet = charsheet.new_sheet("", 32, {"name": "hero"})
    sheet, atlas = charsheet.add_clip(sheet, None, "idle", {d: [_frame((i * 20, 0, 0, 255))] for i, d in enumerate(charsheet.DIRECTIONS)})
    assert sheet["clips"]["idle"]["directions"]["s"] == [0]
    assert sheet["clips"]["idle"]["directions"]["sw"] == [7]
    assert atlas.size == (8 * 32, 32)
    sheet, atlas = charsheet.add_clip(sheet, atlas, "walk", {"south": [_frame((0, 255, 0, 255))] * 4, "east": [_frame((0, 0, 255, 255))] * 4})
    assert sheet["clips"]["walk"]["directions"]["s"] == [8, 9, 10, 11]  # the next row: the idle slots kept their numbers
    assert sheet["clips"]["walk"]["directions"]["e"] == [12, 13, 14, 15]
    assert sheet["size"] == [256, 64]
    path, texture = charsheet.write(tmp_path, "assets/characters/hero", "hero", sheet, atlas)
    back, picture = charsheet.read(tmp_path, path)
    assert back["kind"] == "character" and back["texture"] == texture
    assert charsheet.directions_of(back) == charsheet.DIRECTIONS
    assert charsheet.frame(back, picture, "walk", "east").getpixel((5, 5)) == (0, 0, 255, 255)


def test_an_action_is_the_clip_the_animators_play():
    assert charsheet.clip_for("walking") == "walk"
    assert charsheet.clip_for("attacking with a sword") == "attack"
    assert charsheet.clip_for("casting a spell") == "cast"
    assert charsheet.clip_for("dancing") == "dancing"
