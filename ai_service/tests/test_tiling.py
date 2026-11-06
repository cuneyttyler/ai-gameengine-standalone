"""Tiled painting (docs/places/tiling.md §6): how many tiles a place's cells need, the tiles' boxes, their join."""
import numpy as np
from PIL import Image

from app.bfl import TILE_NOTE, TILE_OVERLAP, _tile_legend, join_tiles, tile_boxes, tile_count


def test_tile_count_from_cells():
    assert tile_count(24, 24) == 1  # a room: one image
    assert tile_count(30, 30) == 1
    assert tile_count(50, 50) == 2  # a dungeon level: 2 × 2
    assert tile_count(116, 116) == 4  # a village: 4 × 4
    assert tile_count(0, 0) == 1


def test_boxes_cover_the_image_with_their_overlap():
    w, h = 4000, 2140
    boxes = tile_boxes(w, h, 2)
    assert len(boxes) == 4
    covered = np.zeros((h, w), dtype=int)
    for x0, y0, x1, y1 in boxes:
        assert 0 <= x0 < x1 <= w and 0 <= y0 < y1 <= h
        covered[y0:y1, x0:x1] += 1
    assert covered.min() >= 1
    # Neighbours share a strip of TILE_OVERLAP of the image.
    assert boxes[0][2] - boxes[1][0] == round(w * TILE_OVERLAP / 2) * 2
    assert tile_boxes(w, h, 1) == [(0, 0, w, h)]


def test_three_by_three_boxes_reach_the_edges():
    boxes = tile_boxes(3001, 1601, 3)
    assert len(boxes) == 9
    assert min(b[0] for b in boxes) == 0 and max(b[2] for b in boxes) == 3001
    assert min(b[1] for b in boxes) == 0 and max(b[3] for b in boxes) == 1601


def test_join_of_flat_tiles_is_seamless():
    w, h = 1200, 640
    boxes = tile_boxes(w, h, 3)
    tiles = [Image.new("RGB", (x1 - x0, y1 - y0), (90, 120, 150)) for x0, y0, x1, y1 in boxes]
    joined = np.asarray(join_tiles(tiles, boxes, w, h))
    assert joined.shape == (h, w, 3)
    assert (joined == (90, 120, 150)).all()


def test_join_fades_across_the_overlap():
    w, h = 1000, 500
    boxes = tile_boxes(w, h, 2)
    colours = [(0, 0, 0), (200, 200, 200), (0, 0, 0), (200, 200, 200)]  # left dark, right light
    tiles = [Image.new("RGB", (b[2] - b[0], b[3] - b[1]), c) for b, c in zip(boxes, colours)]
    row = np.asarray(join_tiles(tiles, boxes, w, h))[h // 4, :, 0].astype(int)
    assert row[0] == 0 and row[-1] == 200
    # Within the shared strip it rises steadily, with no jump.
    strip = row[boxes[1][0]:boxes[0][2]]
    assert (np.diff(strip) >= 0).all() and np.abs(np.diff(strip)).max() <= 3


def test_a_tile_sees_its_share_of_the_scale():
    legend = {"scale": {"across": 50, "down": 50, "metresPerCell": 2, "grid": True}}
    tiled = _tile_legend(legend, 2)
    assert tiled["scale"]["across"] == round(50 * (0.5 + TILE_OVERLAP))
    assert legend["scale"]["across"] == 50  # the place's own legend is left as it was
    assert "ONE PART" in TILE_NOTE
