"""The project's look, kept so its art stays coherent (place-refine.md §15): a few pieces of each
built place (a building, its ground, a prop) are its style references, each with its palette.
New art is briefed to match them, and art found on the web is ranked by how close its palette is."""

from __future__ import annotations

import io
import json
from pathlib import Path

from PIL import Image

KEEP = 12  # references kept, the latest


def palette(image: Image.Image, colours: int = 8) -> list[str]:
    """Its main colours (opaque pixels), as hex, the commonest first."""
    rgba = image.convert("RGBA")
    rgba.thumbnail((96, 96))
    pixels = [p[:3] for p in rgba.getdata() if p[3] > 128]
    if not pixels:
        return []
    flat = Image.new("RGB", (len(pixels), 1))
    flat.putdata(pixels)
    q = flat.quantize(colors=colours, method=Image.Quantize.MEDIANCUT)
    pal = q.getpalette()[: colours * 3]
    counts = sorted(q.getcolors() or [], reverse=True)
    out = []
    for _, index in counts:
        r, g, b = pal[index * 3: index * 3 + 3]
        out.append(f"#{r:02x}{g:02x}{b:02x}")
    return out


def _rgb(h: str) -> tuple[int, int, int]:
    return int(h[1:3], 16), int(h[3:5], 16), int(h[5:7], 16)


def similarity(a: list[str], b: list[str]) -> float:
    """0 (unlike) to 1 (the same colours): each of a's colours against b's nearest, both ways."""
    if not a or not b:
        return 0.0

    def nearest(x: list[str], y: list[str]) -> float:
        total = 0.0
        for h in x:
            r, g, bl = _rgb(h)
            d = min(((r - r2) ** 2 + (g - g2) ** 2 + (bl - b2) ** 2) ** 0.5 for r2, g2, b2 in map(_rgb, y))
            total += max(0.0, 1.0 - d / 160.0)
        return total / len(x)

    return (nearest(a, b) + nearest(b, a)) / 2


def _file(root: Path) -> Path:
    return root / "places" / "style-references.json"


def load(root: Path) -> list[dict]:
    try:
        data = json.loads(_file(root).read_text(encoding="utf-8"))
        return data.get("references", []) if isinstance(data, dict) else []
    except (OSError, ValueError):
        return []


def add(root: Path, pieces: list[dict]) -> list[dict]:
    """Keeps these pieces ({path, role?, name?}) as references (their palettes read), the latest KEEP."""
    refs = [r for r in load(root) if r.get("path") not in {p.get("path") for p in pieces}]
    for p in pieces:
        path = root / p.get("path", "")
        try:
            with Image.open(path) as image:
                refs.append({"path": p["path"], "role": p.get("role", ""), "name": p.get("name", ""), "palette": palette(image)})
        except (OSError, KeyError):
            continue
    refs = refs[-KEEP:]
    _file(root).parent.mkdir(parents=True, exist_ok=True)
    _file(root).write_text(json.dumps({"schema": "aige.style-references/1", "references": refs}, indent=1), encoding="utf-8")
    return refs


def project_palette(root: Path) -> list[str]:
    """The references' colours together (the commonest of each), for ranking and briefs."""
    out: list[str] = []
    for r in load(root):
        for h in r.get("palette", [])[:4]:
            if h not in out:
                out.append(h)
    return out[:16]


def brief_context(root: Path) -> dict:
    """What a new piece's art brief is told about the project's look."""
    refs = load(root)
    if not refs:
        return {}
    return {"palette": project_palette(root), "pieces": [r.get("role") or r.get("name") or Path(r["path"]).stem for r in refs][-8:],
            "rule": "match the project's other art: the same palette, outline weight and pixel size as these pieces"}


def image_palette(data: bytes) -> list[str]:
    try:
        with Image.open(io.BytesIO(data)) as image:
            return palette(image)
    except Exception:
        return []
