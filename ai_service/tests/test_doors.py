"""app/doors.py: a door opened from its own pixels finds its doorway and keeps what's around it."""

from PIL import Image, ImageDraw

from app.doors import door_frames


def _wall_with_door() -> Image.Image:
    # A cream wall with a beam across, and a door of vertical brown planks under it at x 40..80, y 60..140; then
    # sheared as an isometric wall is (half a pixel down per pixel to the right): at x 40 the beam ends at y 72, the
    # door starts at y 80.
    picture = Image.new("RGBA", (160, 200), (230, 215, 180, 255))
    draw = ImageDraw.Draw(picture)
    draw.rectangle([20, 40, 140, 52], fill=(90, 60, 40, 255))            # the beam
    for x in range(40, 80, 6):
        draw.rectangle([x, 60, x + 4, 140], fill=(110, 75, 50, 255))     # planks
        draw.rectangle([x + 5, 60, x + 5, 140], fill=(60, 40, 28, 255))  # the gaps between them
    return picture.transform((160, 280), Image.AFFINE, (1, 0, 0, -0.5, 1, 0), Image.NEAREST, fillcolor=(230, 215, 180, 255))


def test_the_doorway_is_found_under_the_beam_and_the_frames_stay_in_it():
    picture = _wall_with_door()
    made = door_frames(picture, [36, 65, 48, 100], frames=6)
    assert made is not None
    frames, (x, y, w, h) = made
    assert len(frames) == 6
    assert 34 <= x <= 46 and 74 <= x + w <= 86   # its sides at the door's
    assert y >= 73                               # below the beam
    assert all(f.size == (w, h) for f in frames)
    # Closed, it looks like the door; open, the doorway is dark where the door was.
    closed, opened = frames[0], frames[-1]
    middle = (w // 2, h // 2)
    assert closed.getpixel(middle)[3] > 0 and sum(opened.getpixel(middle)[:3]) < sum(closed.getpixel(middle)[:3])


def test_no_doorway_off_the_picture():
    assert door_frames(_wall_with_door(), [150, 190, 40, 40]) is None
