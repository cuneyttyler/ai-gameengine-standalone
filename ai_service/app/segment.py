"""Click-to-select (docs/places/painted-places.md §4): SAM 2.1 (tiny, on the CPU) finds the thing under the user's
clicks in a painting. The image's embedding is computed once (a second or two) and kept by path and modification time,
so each click after that is quick. Needs the service's `segment` extra (torch, transformers)."""
from __future__ import annotations

import io
import threading
from pathlib import Path

MODEL_ID = "facebook/sam2.1-hiera-tiny"
_lock = threading.Lock()
_model = None
_processor = None
_cache: dict[str, tuple[float, object, object]] = {}  # path -> (mtime, image embeddings, processor inputs)


def available() -> str:
    """"" when it can run, else why not."""
    try:
        import torch  # noqa: F401
        from transformers import Sam2Model  # noqa: F401
    except Exception as error:  # noqa: BLE001
        return f"click-to-select needs the service's segment extra (uv sync --extra segment): {error}"
    return ""


def _load():
    global _model, _processor
    if _model is None:
        import torch
        from transformers import Sam2Model, Sam2Processor
        torch.set_grad_enabled(False)
        _processor = Sam2Processor.from_pretrained(MODEL_ID)
        _model = Sam2Model.from_pretrained(MODEL_ID).eval()
    return _model, _processor


def _embedded(path: Path):
    model, processor = _load()
    key = str(path.resolve())
    mtime = path.stat().st_mtime
    hit = _cache.get(key)
    if hit and hit[0] == mtime:
        return hit[1], hit[2]
    from PIL import Image
    image = Image.open(path).convert("RGB")
    inputs = processor(images=image, return_tensors="pt")
    embeddings = model.get_image_embeddings(inputs["pixel_values"])
    if len(_cache) > 4:
        _cache.pop(next(iter(_cache)))
    _cache[key] = (mtime, embeddings, inputs)
    return embeddings, inputs


def segment(path: Path, points: list[list[float]], box: list[float] | None = None) -> tuple[bytes, list[int]]:
    """The mask (an L PNG the painting's size, 255 inside) of the thing at `points` ([x, y, 1] to add, [x, y, 0] to
    leave out) and, if given, within `box` [x0, y0, x1, y1]; and its bounding box [x, y, w, h]."""
    import numpy as np
    from PIL import Image
    with _lock:
        model, processor = _load()
        embeddings, inputs = _embedded(path)
        kwargs = {}
        if points:
            kwargs["input_points"] = [[[[float(p[0]), float(p[1])] for p in points]]]
            kwargs["input_labels"] = [[[int(p[2]) if len(p) > 2 else 1 for p in points]]]
        if box:
            kwargs["input_boxes"] = [[[float(v) for v in box]]]
        prompt = processor(images=None, original_sizes=inputs["original_sizes"], return_tensors="pt", **kwargs) if kwargs else {}
        prompt = {k: v for k, v in dict(prompt).items() if k in ("input_points", "input_labels", "input_boxes")}
        outputs = model(image_embeddings=embeddings, multimask_output=False, **prompt)
        masks = processor.post_process_masks(outputs.pred_masks.cpu(), inputs["original_sizes"])[0]
    mask = np.asarray(masks[0, 0] if masks.ndim == 4 else masks[0], dtype=bool)
    image = Image.fromarray((mask * 255).astype("uint8"), mode="L")
    out = io.BytesIO()
    image.save(out, format="PNG")
    ys, xs = np.nonzero(mask)
    bbox = [int(xs.min()), int(ys.min()), int(xs.max() - xs.min() + 1), int(ys.max() - ys.min() + 1)] if len(xs) else [0, 0, 0, 0]
    return out.getvalue(), bbox
