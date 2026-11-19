"""Style-reference sheets: paint a few example designs of each part kind once, then give those sheets to the
tile painter as references so same-kind buildings vary in design but stay consistent across the whole place.

Rules (agreed with the user):
  - object variety Low/Medium/High = how many DISTINCT DESIGNS per part kind: 6 / 12 / 18.
  - each reference sheet holds at most 10 styled parts; at most 3 sheets (so at most 30 designs total).
  - designs are packed sheet by sheet in part order; stop when they all fit (no need for all 3 sheets).
  - if the total desired designs exceed 30, distribute proportionally across part kinds (largest remainder).
  - open-world: compute parts over a whole section and reuse its refs for all that section's tiles.
"""
from __future__ import annotations

import base64
import io
import json
from pathlib import Path

from PIL import Image

# Variety -> designs per part kind. One sheet holds all designs; columns define the grid.
VARIETY_DESIGNS = {"low": 6, "medium": 12, "high": 18}
SHEET_COLS = 5
MAX_DESIGNS = 30
SHEET_WIDTH = 1536
CELL_SIZE = 307


def _plural(n, kind):
    return f"1 {kind}" if n == 1 else f"{n} {kind + ('s' if not kind.endswith('s') else 'es')}"


def gather_parts(place: dict) -> list[tuple[str, int]]:
    """Collect (kind, count) over all zones, summing duplicate kinds. Only parts with count > 0."""
    totals = {}
    for z in (place.get("zones") or []):
        if not isinstance(z, dict):
            continue
        for b in (z.get("buildings") or []):
            if not isinstance(b, dict):
                continue
            kind = str(b.get("kind") or "").strip()
            try:
                count = int(b.get("count") or 0)
            except (TypeError, ValueError):
                count = 0
            if kind and count > 0:
                totals[kind] = totals.get(kind, 0) + count
    return list(totals.items())


def design_counts(parts: list[tuple[str, int]], variety: str) -> list[tuple[str, int]]:
    """How many distinct designs of each part kind to generate."""
    per_kind = VARIETY_DESIGNS.get(str(variety).lower(), VARIETY_DESIGNS["medium"])
    desired = [per_kind] * len(parts)
    total = sum(desired)
    if total <= MAX_DESIGNS:
        return list(zip([k for k, _ in parts], desired))
    # Cap at MAX_DESIGNS, distributed proportionally across kinds (here all kinds want the same, so equal split).
    base = [MAX_DESIGNS // len(desired)] * len(desired)
    remainder = MAX_DESIGNS - sum(base)
    for i in range(remainder):
        base[i] += 1
    return list(zip([k for k, _ in parts], base))


def pack_sheets(designs: list[tuple[str, int]]) -> list[list[str]]:
    """ONE sheet: expand designs into a flat list of kinds in part order (capped at MAX_DESIGNS)."""
    flat = []
    for kind, n in designs:
        flat.extend([kind] * n)
    return [flat[:MAX_DESIGNS]]


def sheet_prompt(sheet: list[str], all_kinds: list[str], art_direction: str = "") -> str:
    """Prompt for ONE reference sheet: a grid of isolated building designs (up to 30), each cell one design.
    `art_direction` is the place's Paint Prompt from the Build panel's Confirm window — used as the art-style guide."""
    n = len(sheet)
    rows = (n + SHEET_COLS - 1) // SHEET_COLS
    lines = []
    for i, kind in enumerate(sheet):
        r, c = divmod(i, SHEET_COLS)
        lines.append(f"- cell (row {r + 1}, column {c + 1}): a {kind}")
    positions = "\n".join(lines)
    return (
        "A clean REFERENCE SHEET for an isometric game's art, on a plain light neutral background. A tidy grid of "
        f"{SHEET_COLS} columns by {rows} rows. Each cell holds ONE isolated, fully-drawn building example, none "
        "touching another, all seen at the same isometric angle and consistent scale. Paint each building in "
        "detailed, hand-crafted game art with visible roof, walls and windows. The buildings in each cell are:\n"
        + positions + "\n"
        "Give buildings of the same kind clearly different designs from each other (roof shape, height, material, "
        "colour and small details) so this sheet shows a RANGE of designs for each kind, not copies. "
        + ("Match this art style from the place's Paint Prompt (Build panel, Confirm window): "
           + art_direction.strip() + " "
           if art_direction.strip() else "")
        + "No text, no labels, no borders around cells, no people."
    )


def paint_prompt_addendum(sheets: list[list[str]]) -> str:
    """Text appended to the main tile prompt so the painter knows how to use the reference sheets. References are
    passed to the model in order (sheet 1, sheet 2, ...)."""
    if not sheets:
        return ""
    lines = []
    for i, sheet in enumerate(sheets):
        counts = {}
        for k in sheet:
            counts[k] = counts.get(k, 0) + 1
        desc = ", ".join(_plural(n, k) for k, n in counts.items())
        # BFL numbers images by their slot in `images`: slot 0 is the block-out, slots 1.. are the sheets. Use the
        # documented <ref_image_N> tokens so the model knows the block-out is the scene and the sheets are swatches.
        lines.append(f"<ref_image_{i + 1}> shows building designs: {desc}.")
    return (
        "\n\nSTYLE REFERENCES — IMAGE ROLES (FLUX-3 image slots): "
        "<ref_image_0> is the block-out — the ONLY source of land shape, black area, coloured zones and layout. Keep "
        "it exactly: paint land only where it shows land, keep every black pixel black, and do not treat it as a design "
        "sample. " + " ".join(lines) + " "
        "The reference image(s) are DESIGN SWATCHES only: use them just for building shapes, roofs, materials and "
        "palette. They are not scenes — never copy their grid, background or arrangement into the painting. Place each "
        "design only in the coloured zone whose COLOUR KEY entry lists that building kind — for example, if inns are "
        "listed for the yellow area, paint inns only there, never on plain land. Vary between the designs of a kind."
    )


def per_sheet_addenda(place: dict, variety: str) -> list[str]:
    """One short addendum per generated sheet, so a tile that receives ONE sheet is told about exactly that sheet
    (FLUX-3 must only be given one reference image per call — several make it ignore the block-out)."""
    parts = gather_parts(place)
    sheets = pack_sheets(design_counts(parts, variety))
    return [paint_prompt_addendum([sheet]) for sheet in sheets]


def generate_style_refs(place: dict, variety: str, painter, art_direction: str = "",
                        out_dir: str | Path | None = None) -> tuple[list[bytes], str]:
    """Generate ONE style-reference sheet with all designs (up to 30). `painter` is a BflPainter (uses create()).
    `art_direction` is the place's Paint Prompt from the Build panel's Confirm window, used as the style guide.
    Returns (refs, addendum); refs has exactly one PNG when there are designs."""
    parts = gather_parts(place)
    designs = design_counts(parts, variety)
    sheets = pack_sheets(designs)
    if not sheets or not sheets[0]:
        return [], ""
    sheet = sheets[0]
    rows = (len(sheet) + SHEET_COLS - 1) // SHEET_COLS
    width = SHEET_WIDTH
    height = max(16, round(rows * CELL_SIZE / 16) * 16)
    prompt = sheet_prompt(sheet, [k for k, _ in parts], art_direction)
    png = painter.create(prompt, width=width, height=height, seed=None)
    out = Path(out_dir) if out_dir else None
    if out:
        out.mkdir(parents=True, exist_ok=True)
        (out / "style-ref.png").write_bytes(png)
        (out / "style-ref-prompt.txt").write_text(prompt, encoding="utf-8")
    return [png], ""


def generate_design_catalogue(place: dict, variety: str, writer, art_direction: str = "") -> tuple[str, object | None]:
    """Generate a TEXT catalogue of distinct building designs per part kind. Returns (prompt_text, usage).

    FLUX-3 draws from every image it is given, so reference sheets make it compose a whole scene instead of following
    the block-out. The variety budget (6/12/18 designs per kind) is therefore expressed as text descriptions: the
    painter chooses among them, while the block-out remains the only image sent.
    """
    parts = gather_parts(place)
    designs = design_counts(parts, variety)
    if not designs or writer is None:
        return "", None
    # Which coloured zone lists each part kind — so the catalogue can say "Chapel (Orange area): ..." and the painter
    # never has to guess what "the block-out" means for a given design.
    kind_colours: dict[str, list[str]] = {}
    for z in (place.get("zones") or []):
        if not isinstance(z, dict):
            continue
        colour = str(z.get("colour") or "").strip()
        if not colour:
            continue
        for b in (z.get("buildings") or []):
            if isinstance(b, dict):
                kind = str(b.get("kind") or "").strip()
                if kind and colour not in kind_colours.setdefault(kind, []):
                    kind_colours[kind].append(colour)
    spec = ", ".join(f"{kind}: {n} distinct designs" for kind, n in designs)
    task = (
        "You write a DESIGN CATALOGUE for an isometric game artist. For each building kind listed, give exactly the "
        "number of DISTINCT short visual design descriptions requested. Each description is 5-12 words, concrete and "
        "visual: roof shape and material, wall material, height, windows, and one distinguishing detail. Designs of "
        "the same kind must differ clearly from one another. Describe only the individual buildings — never placement, "
        "layout, streets, scenes or ground. Use exactly the kind names given. Return ONLY JSON, e.g. "
        '{"House plot": ["a thatched ...", "a slate-roofed ..."]}.'
    )
    user = "Kinds and counts: " + spec + (". Art style: " + art_direction[:300] if art_direction else "")
    try:
        reply = writer.complete([{"role": "system", "content": task},
                                 {"role": "user", "content": user}])
        text = (reply.text or "").strip()
        a, b = text.find("{"), text.rfind("}")
        obj = json.loads(text[a:b + 1]) if a >= 0 and b > a else None
        if not isinstance(obj, dict):
            return "", getattr(reply, "usage", None)
        lines = []
        for kind, n in designs:
            items = [str(x).strip() for x in (obj.get(kind) or []) if str(x).strip()][:n]
            if not items:
                continue
            areas = kind_colours.get(kind) or []
            label = f"{kind} ({', '.join(c.capitalize() + ' area' for c in areas)})" if areas else kind
            lines.append(f"- {label}: " + "; ".join(items))
        if not lines:
            return "", getattr(reply, "usage", None)
        return ("\n\nDESIGN VARIETIES — use each kind's designs ONLY in the colour area shown after its name. "
                "IMAGE 1 (the block-out) fixes exactly where those colour areas are, so paint each kind only inside "
                "its own area on IMAGE 1 and nowhere else:\n" + "\n".join(lines), getattr(reply, "usage", None))
    except Exception:
        return "", None
