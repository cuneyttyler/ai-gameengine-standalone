"""Animate the interior (docs/places/interior-objects.md): an interior's moving things found by a scripted vision model
among the ontology's candidates, and how each moves written by a scripted LLM."""
import json

from PIL import Image

from app.interior_parts import interior_parts, write_motions
from app.providers import ScriptedProvider

CANDIDATES = [
    {"id": "fireplace", "label": "fireplace", "draw": "",
     "animate": [{"clip": "burn", "motion": "the flames flicker", "opens": "always"}]},
    {"id": "stove", "label": "stove", "draw": "",
     "animate": [{"clip": "burn", "motion": "the fire glows", "opens": "always"},
                 {"clip": "cook", "motion": "steam rises from the pot", "opens": "use", "prompt": "Cook", "duration": 4}]},
]


def floors(root, n=2):
    folder = root / "assets" / "pieces"
    folder.mkdir(parents=True, exist_ok=True)
    paths = []
    for k in range(1, n + 1):
        Image.new("RGBA", (400, 300), (90, 70, 50, 255)).save(folder / f"house-interior-{k}.png")
        paths.append(f"assets/pieces/house-interior-{k}.png")
    return paths


def seen(messages):
    """The vision model: floor 1 has a fireplace (twice, overlapping) and something not asked for; floor 2 a stove."""
    text = messages[0]["content"][0]["text"]
    assert "- fireplace: fireplace" in text and "- stove: stove" in text  # the ontology's kinds, by id
    if "floor 1" in text:
        return json.dumps({"parts": [
            {"kind": "fireplace", "label": "a stone fireplace with a lit fire", "box": [100, 50, 80, 60], "confidence": 0.9},
            {"kind": "fireplace", "label": "the same fireplace", "box": [104, 52, 78, 58], "confidence": 0.6},
            {"kind": "bed", "label": "a bed", "box": [10, 10, 50, 40], "confidence": 0.9},
            {"kind": "stove", "label": "too small", "box": [0, 0, 1, 1]},
        ]})
    return json.dumps({"parts": [{"kind": "stove", "label": "an iron stove with a pot", "box": [200, 100, 40, 50], "confidence": 0.8}]})


def test_each_floor_is_looked_at_and_each_thing_gets_its_motion(tmp_path):
    paths = floors(tmp_path)
    vision = ScriptedProvider(seen)
    text = ScriptedProvider(lambda messages: json.dumps({"parts": [
        {"n": 1, "motion": "the fire in the stone fireplace flickers; the stones stay still", "clip": "Burn!", "opens": "always"},
        {"n": 2, "motion": "steam curls up from the pot on the iron stove", "clip": "cook", "opens": "use", "prompt": "Cook",
         "duration": 99},
    ]}))
    result = interior_parts(vision, text, tmp_path, paths, CANDIDATES, "house", "a family home")
    assert result["ok"]
    parts = result["parts"]
    assert [(p["floor"], p["kind"]) for p in parts] == [(1, "fireplace"), (2, "stove")]  # one fireplace, no bed
    fire, stove = parts
    assert fire["box"] == [50, 25, 40, 30]  # shown at twice its size (small art enlarged): its boxes halved back
    assert fire["clip"] == "burn" and fire["opens"] == "always" and fire["source"] == "llm"
    assert stove["opens"] == "use" and stove["prompt"] == "Cook" and stove["duration"] == 30.0  # kept to 30 s
    assert len(vision.requests) == 2 and len(text.requests) == 1
    asked = text.requests[0][0]["content"]
    assert "a stone fireplace with a lit fire" in asked and "steam rises from the pot" in asked  # its label, its suggestions


def test_the_ontology_s_motion_stands_when_the_llm_can_t_answer(tmp_path):
    parts = [{"floor": 1, "kind": "stove", "label": "a stove", "box": [0, 0, 10, 10], "confidence": 0.9}]
    out = write_motions(ScriptedProvider(lambda messages: "not json"), "house", "", parts, {c["id"]: c for c in CANDIDATES})
    assert out[0]["motion"] == "the fire glows" and out[0]["opens"] == "always" and out[0]["source"] == "ontology"


def test_a_rect_s_boxes_come_back_in_the_picture_s_pixels(tmp_path):
    paths = floors(tmp_path, 1)
    vision = ScriptedProvider(lambda messages: json.dumps({"parts": [{"kind": "fireplace", "label": "f", "box": [10, 10, 20, 20]}]}))
    result = interior_parts(vision, None, tmp_path, paths, CANDIDATES, rects=[[100, 50, 200, 150]])
    box = result["parts"][0]["box"]
    assert box[0] >= 100 and box[1] >= 50  # the rect's offset added back


def test_no_candidates_is_said(tmp_path):
    result = interior_parts(ScriptedProvider(lambda m: "{}"), None, tmp_path, floors(tmp_path, 1), [])
    assert not result["ok"] and "ontology" in result["errors"][0]
