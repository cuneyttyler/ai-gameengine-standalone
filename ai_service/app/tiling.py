"""Congruent tiling for painted places (docs/places/tiling.md §6).

A place's flat, colour-coded block-out is split into an n x n grid. Each tile is enlarged to a fixed painting
resolution and painted **directly from its flat block-out crop** (not from a composed whole-scene image), so its
content is generated at its own scale — essential for big scenes, where one whole-scene compose makes every
building the same size.

Tiles are painted one after another, right-to-left within a row, rows top-to-bottom. Every tile's block-out has a
flat context band on its LEFT and BOTTOM (the neighbouring tiles' geography, taken from the flat block-out). The
tile painted just before supplies its painted LEFT / BOTTOM / bottom-right-corner bands, which are pasted over this
tile's RIGHT / TOP / top-left-corner regions. After painting, those painted bands are restored exactly, so each
seam lies within a single paint pass and the tiles join pixel-for-pixel. Finally the flat LEFT/BOTTOM bands are cut
off, the tiles placed on an exact n x n grid and the result scaled back to the block-out's size.

Used for every paint: by plan (zoned block-out), free (land block-out, zones optional) and open-world sections
(they go through the same endpoint). Works for any number of tiles and any place type; zone descriptions come
from the editor's ontology, never from hardcoded place words.
"""
from __future__ import annotations

import io
import math
from pathlib import Path

from PIL import Image

Image.MAX_IMAGE_PIXELS = None

OVERLAP = 0.08          # 8% context band (docs/places/tiling.md §6)
TILE_WIDTH = 2048       # painting width of each tile; height follows the block-out's aspect


# ------------------------------------------------------------------------------------------------------------
# The prompt
# ------------------------------------------------------------------------------------------------------------
def _plural(n: int, kind: str) -> str:
    """'1 house' / '13 houses' — the kinds are already plain English (e.g. 'house', 'inn')."""
    try:
        count = int(n)
    except (TypeError, ValueError):
        return f"{n} {kind}"
    if count == 1:
        return f"1 {kind}"
    # A simple, readable plural (the kinds here are ordinary building names).
    plural = kind + ("s" if not kind.endswith("s") else "es")
    return f"{count} {plural}"


def _field_value(value) -> str:
    """Format an ontology field value for the prompt: a number, a [lo, hi] range, an object, a bool or an enum."""
    if isinstance(value, bool):
        return "yes" if value else "no"
    if isinstance(value, (int, float)):
        return str(value)
    if isinstance(value, (list, tuple)) and len(value) == 2:
        return f"{value[0]}–{value[1]}"
    if isinstance(value, dict):
        for key in ("count", "area", "area", "value"):
            if key in value:
                return _field_value(value[key])
        return ", ".join(f"{k}={_field_value(v)}" for k, v in value.items())
    return str(value).replace("-", " ")



def zone_key(place: dict) -> str:
    """A generic, short colour -> district key from the editor's zones. Each zone's ontology description gives the
    characteristic content; a walled zone gets an open gate. Part counts stay in the plan, not in this brief line."""
    zones = [z for z in (place.get("zones") or []) if isinstance(z, dict) and z.get("colour")]
    lines = []
    for z in zones:
        colour = str(z["colour"]).strip().lower()
        desc = str(z.get("description") or "").strip().split(". ")[0].strip()
        if ":" in desc:
            lead, body = desc.split(":", 1)
            text = (lead.strip()[0].lower() + lead.strip()[1:]) + ": " + body.strip()
        else:
            text = (desc[0].lower() + desc[1:]) if desc else colour
        walled = z.get("walled") is True or (isinstance(z.get("walled"), str)
                                              and z["walled"].lower() not in ("none", "false", ""))
        if walled:
            text += ", with a clear OPEN GATE in the fence so it can be entered."
        name = str(z.get("name") or z.get("id") or "").strip()
        label = f"{name} ({colour} area)" if name else f"{colour} area"
        lines.append(f"- {label} = {text}")
    return "\n".join(lines)


def zone_summary_line(place: dict) -> str:
    """One short 'Colour = what it holds' sentence used in the edit block, e.g. 'Orange = a fenced graveyard ...'."""
    parts = []
    for z in (place.get("zones") or []):
        if not isinstance(z, dict) or not z.get("colour"):
            continue
        colour = str(z["colour"]).strip().capitalize()
        summary = str(z.get("art_summary") or "").strip()
        if not summary:
            desc = str(z.get("description") or "").strip().split(". ")[0].strip()
            # The clause after the colon is what the place looks like (graves, chapel...); the clause before it is a
            # definition ("a graveyard as a part of the place of its own") that gives the painter no useful cue.
            summary = desc.split(":", 1)[1].strip() if ":" in desc else desc
            summary = summary.split(",")[0].strip()  # first visual phrase only
            summary = (summary[0].lower() + summary[1:]) if summary else colour.lower()
        parts.append(f"{colour} = {summary}.")
    return " ".join(parts) + (" " if parts else "")



FRAMING = (
    "ISOMETRIC PERSPECTIVE: a true isometric (2.5D angled) game-map view, buildings showing their roofs and two "
    "walls, camera tilted about 30 degrees, one light from the upper left with soft consistent shadows. NOT "
    "top-down, NOT flat 2D. Finished, richly detailed hand-crafted art, as in the best classic isometric RPGs. "
    "\n\n"
    "IMAGE 1 (<ref_image_0>) is the COLOUR-CODED AREA MAP — the image you are repainting, a temporary REFERENCE "
    "OVERLAY, not the finished art and not a plan to trace. If other images are sent, they are NOT this one: only "
    "IMAGE 1 fixes the land, the black area and where everything goes. The flat colours are only markers telling you "
    "WHAT KIND of place goes in each area (key below). The "
    "ground underneath every coloured patch is ORDINARY GROUND — natural grass, earth, moss or forest floor, exactly "
    "like the untinted land; the tint is NOT the colour of the ground. CRITICAL: absolutely no trace of the marker "
    "colours (orange, yellow, purple, red, white, pink) may appear in your painting — no orange, yellow, purple, red "
    "or white TINTED ground, no coloured patches matching the markers. Paint every tinted area as normal terrain "
    "with its buildings on it; the coloured overlay must completely disappear, leaving only real ground, buildings "
    "and scenery. "
    "\n\n"
    "Repaint IMAGE 1 only. IMAGE 1 is a colour-coded tile of a larger isometric game map. Keep its exact land shape, "
    "its black outer area, and the exact position of every road, river, coloured patch and cut edge. Change NOTHING "
    "about the layout: do not move, add or remove land, black "
    "area, roads, rivers or zones. Paint only on the land IMAGE 1 already shows; every black pixel stays black. If "
    "IMAGE 1 is a mostly-black corner with only a small land area, paint only that small area. "
    "\n\n"
    "Replace the flat colour markers with finished, richly detailed isometric game art, keeping the same camera and "
    "scale. §ZONE_LINE§No marker colour may remain "
    "in the ground — paint ordinary grass and earth under everything. Plain green land, dark-green woods and blue "
    "water carry no buildings, only trees, grass and water. "
    "\n\n"
    "Keep the same camera, framing, scale and orientation; keep the river and roads exactly where they are, with "
    "the same course and width. BUILDINGS GO ONLY INSIDE THE COLOURED ZONES. Every area with no colour tint — the "
    "plain green grass, the dark-green woods, the blue water and the brown roads — is LAND, not a district: it holds "
    "no buildings at all, only trees, grass, water and roads. Buildings in the plain green land are WRONG. "
    "\n\n"
    "IMAGE 1 IS ONE RECTANGULAR TILE / CROP OF A MUCH LARGER MAP — NOT THE WHOLE MAP. Do NOT recentre, resize, "
    "complete, or recompose it into a whole island, diamond or vignette; do not add a new outer boundary. Continue "
    "the land right up to the cut edges that join other tiles. Where the image is BLACK, that is the OUTER edge of "
    "the map: keep it black and never extend land, buildings or scenery past it into the black. "
    "\n\n"
    "PURE BLACK IS EMPTY: every black area of IMAGE 1 is outside the place. Leave every black pixel pure black; paint nothing "
    "in it and extend nothing into it. CRITICAL: the reverse is equally true — where IMAGE 1 shows land "
    "(any colour other than black, including green forest and grass), you MUST paint land there. NEVER paint black "
    "onto land, and NEVER extend the map's black outer edge into the land as a wedge, notch or triangular gap. The "
    "black boundary is only at the map's true outer edge, exactly as the block-out draws it: do not carry it inward "
    "or create any new black gap inside the land, especially near a cut edge. "
    "NO people, animals, monsters or any other figures. No text, no labels, no UI."
)

FILL_RULES = (
    "\n\nBUILD ONLY INSIDE THE COLOURED ZONES — FILL THEM, LEAVE THE LAND EMPTY:\n"
    "- Fill the ENTIRE zone area densely with its characteristic buildings and content. A ZONE left as an empty field "
    "— just grass and roads with no buildings — is WRONG. This applies everywhere inside the zone, INCLUDING where "
    "the zone reaches a tile corner or edge and including areas beside the painted edge strips. Give each district "
    "plenty of its characteristic structures, its lanes, yards and gardens. "
    "- Only the ZONE tints listed in the COLOUR KEY (orange, yellow, purple, red, white, pink) are zones. The "
    "TERRAIN colours are not zones even though they are coloured: light green is meadow, dark green is forest, blue "
    "is water and brown is road. These carry NO buildings at all — paint dark-green patches as dense forest and "
    "light-green ground as open meadow with grass and wildflowers. A building on green land, in forest or in water "
    "is WRONG — leave such terrain open and natural. "
    "\n"
    "HOW THE DISTRICTS SHOULD LOOK — ORGANIC, NOT GEOMETRIC:\n"
    "- Do NOT render the districts as perfect circles, discs, rounded rectangles or any hard-edged geometric blobs. "
    "Real settlements grow irregularly: give each district a natural, ragged, uneven outline. Keep each district's "
    "general position and rough extent, but let its outline be organic. A district's buildings stay INSIDE its "
    "coloured area (only its edge may be ragged) — they must NOT spread out onto the plain green land or fill any "
    "part of the meadow or forest that has no colour tint. Where TWO coloured districts meet, let their buildings, "
    "lanes and gardens intermingle across a soft, gradual transition rather than stopping at a hard line or ring; "
    "this blending is only ever between two coloured zones, never between a zone and bare land.\n"
    "- The colours say WHAT to build, not where a wall goes: do NOT draw fences, walls or hedges along the colour "
    "boundaries; districts blend into one another without any line marking the border.\n"
    "- Each zone holds ONLY the kinds of part listed for it in the colour key, in the numbers given: do not put a "
    "kind of building in a zone that does not list it.\n"
    "- Give buildings of the same kind slight natural differences in shape and detail so they are not identical\n"
    "- Any area that is fenced, walled or otherwise enclosed must have a clear OPEN ENTRANCE (a gate or opening in "
    "the fence/wall) so it can be entered — never seal it off completely. Graveyards, yards and walled gardens all "
    "need an open gate in their boundary.\n"
)

GROUND_WATER_RULES = (
    "\nFORESTS AND GROUND — RICH, NOT FLAT (IMAGE 1):\n"
    "- The VERY DARK GREEN (#3A5430) patches are DENSE OLD-GROWTH FOREST. They must be almost completely covered "
    "by a dense canopy of MANY full-grown trees — no exposed flat dark-green ground. Fill every dark-green patch "
    "edge to edge with closely packed mature trees of many species and heights: tall oaks, pines, firs, spruces, "
    "birches, maples and beeches, each with a distinct layered crown, a visible trunk and richly varied greens, "
    "golds and autumn reds. Overlap the crowns into a continuous canopy so little forest floor shows; let a few "
    "trees spill just beyond the patch so it reads as a real wood. It is WRONG to leave a dark-green area as flat "
    "green grass with only a few trees, as small bushes, or as round blobs.\n"
    "- The LIGHTER GREEN (#608246) is open meadow: lush grass with natural light and shade, wildflowers, tall-grass "
    "tufts, clover and scattered stones, with only occasional solitary trees. Brown strips = dirt roads and paths; "
    "grey = a paved square. All ground must be richly textured and varied, never a flat uniform colour and never a "
    "visible grid or cell pattern.\n"
    "\nWATER — RIVERS, LAKES AND SEAS (IMAGE 1):\n"
    "- Paint water ONLY where the block-out actually has BLUE (#346096). If this tile's block-out has NO blue at all, "
    "then there is NO water here: do NOT invent, add or imagine any river, stream, lake, pond or sea. A painted river "
    "that has no blue area in the block-out is WRONG — leave that ground as land. Never paint blue water onto green "
    "grass or forest. What matters is the block-out in front of you, not what a place of this kind might usually have.\n"
    "- The BLUE (#346096) areas are real water, never a flat blue fill. Keep the exact course, width and position "
    "of every river and water body. Give every water body a CLEAN, well-defined bank: a crisp shoreline where land "
    "meets water, with a narrow strip of exposed earth or sand, grassy or reedy edges, small stones and overhanging "
    "vegetation — the bank is a clear, detailed line, not a soft blur. Rivers meander naturally with slightly "
    "varying width, shallow reed-lined margins, gentle ripples, reflections of the sky and nearby trees, and a hint "
    "of current. Lakes and ponds are calm with a natural shoreline that shallows at the edges and deepens towards "
    "the middle, with reflections and a few ripples. Seas and large water bodies have gentle layered waves, white "
    "foam at the shore and a wet sandy or rocky beach. Add depth and clarity so the water reads as translucent and "
    "alive. Water stays inside its blue area; do not flood the land or put water on the grass. Where a brown strip "
    "crosses water, paint it as a proper bridge with banks and supports.\n"
    "\n- The coloured areas are only guides: erase them completely. Under each one, paint ordinary natural ground "
    "(grass, earth, moss, forest floor), never ground tinted orange, yellow, purple, red or white. If any flat marker "
    "colour is still visible in the ground of your painting, that is WRONG. ONE consistent art style, palette, "
    "lighting direction and colour temperature throughout, so this tile matches the neighbouring tiles exactly."
)

CONTINUITY = (
    "\n\nThe painted strips on the named edges are finished art from the tiles already painted. Continue their "
    "buildings, roads, forest and ground ACROSS the cut edges so the two sides line up perfectly: roads meet roads, "
    "rivers meet rivers, buildings and trees carry on at the same scale, in the same style, light and colours. "
    "Critically, do NOT stop at those strips — fill every coloured district right up to the edges with its "
    "buildings, never leaving an empty field beside a cut."
)


# Every side of a tile is one of three things:
#   painted  - a band copied from a neighbour painted EARLIER (right/top). Finished art: leave it, continue its scene.
#   flat     - a flat block-out band from a neighbour painted LATER (left/bottom). It is LAND: paint it, do not leave
#              the flat colour; it is cut off again at assembly but must read as continuous ground in the paint.
#   outer    - no neighbour at all: the real map boundary, keep black.
SIDE_LABEL = {"right": "RIGHT", "top": "TOP", "left": "LEFT", "bottom": "BOTTOM"}
SIDE_ROLE = {
    "painted": "A strip copied from the tile painted immediately before this one, on this side. It shows the SAME "
               "buildings, graves, trees, roads and ground as this tile — the scene does not end at this strip. "
               "Continue exactly what is in it across the cut into this tile at the same positions, scale, style and "
               "light. Paint the area RIGHT NEXT TO this strip to carry on what the strip shows: if the strip has "
               "buildings and a market, keep painting buildings and market right up to it — do NOT switch to open "
               "land or meadow beside it.",
    "flat": "a FLAT BLOCK-OUT band copied from the neighbouring tile on this side, which is painted later. Paint "
            "exactly what IMAGE 1 shows there: ground where it is coloured (grass, forest, zone tints) — turn the flat "
            "colour into finished art — and pure black where it is black (the map ends here too). Do NOT invent land "
            "in black areas or leave the band as a flat colour patch.",
    "outer": "the OUTER EDGE of the map. The black here is the real boundary: keep it black and never extend land, "
             "buildings or scenery into it.",
}


def tile_edge_roles(r: int, c: int, cols: int, rows: int) -> dict[str, str]:
    """Role of each of the tile's four sides (right/top = earlier neighbours, left/bottom = later neighbours)."""
    return {
        "right": "painted" if c < cols - 1 else "outer",
        "top": "painted" if r > 0 else "outer",
        "left": "flat" if c > 0 else "outer",
        "bottom": "flat" if r < rows - 1 else "outer",
    }


def tile_position(r: int, c: int, cols: int, rows: int) -> str:
    """A concrete, per-tile position description (not a generic one)."""
    if r == 0 and c == cols - 1:
        return "the top-right corner of the map, painted first"
    if r == 0 and c == 0:
        return "the top-left corner of the map"
    if r == rows - 1 and c == 0:
        return "the bottom-left corner of the map"
    if r == rows - 1 and c == cols - 1:
        return "the bottom-right corner of the map"
    if r == 0:
        return f"the top row, column {c + 1} of {cols}"
    if r == rows - 1:
        return f"the bottom row, column {c + 1} of {cols}"
    if c == 0:
        return f"the left column, row {r + 1} of {rows}"
    if c == cols - 1:
        return f"the right column, row {r + 1} of {rows}"
    return f"row {r + 1}, column {c + 1} of {cols} x {rows}"


def _edge_sentence(roles: dict[str, str], position: str) -> str:
    """State the role of all four sides, so black is kept only where there is no neighbour at all."""
    sides = "\n".join(f"- {SIDE_LABEL[side]} edge: {SIDE_ROLE[roles.get(side, 'outer')]}"
                       for side in ("right", "top", "left", "bottom"))
    return ("This tile is " + position + ". " +
            "EDGES OF IMAGE 1 — each side has its own role, read carefully:\n" + sides + "\n"
            "Only sides marked OUTER EDGE are the map boundary and stay black. On the flat bands, paint ground only "
            "where IMAGE 1 actually shows colour, and keep every black pixel black — the black runs through the flat "
            "bands too. Continue the scene from every neighbour band into this tile.")


def _tile_safe_art_direction(text: str) -> str:
    """For a per-TILE paint, drop sentences that tell the painter to compose the WHOLE place ("paint X as a complete
    map", "show a village", "the square at the centre"). Those belong to a whole-place paint, not to one crop. Keep the
    style sentences (materials, palette, mood)."""
    import re
    if not text:
        return ""
    drop = re.compile(
        r"\b(?:complete|whole|entire|full)\b[^.!?]{0,90}\bmap\b"
        r"|\bmap\b[^.!?]{0,90}\b(?:complete|whole|entire|full)\b"
        r"|\bnear the cent(?:er|re)\b"
        r"|\b(?:show|paint|draw|render|depict|compose)\b[^.!?]{0,70}\b(?:village|town|city|settlement|map|scene)\b"
        r"|\bas a whole\b",
        re.IGNORECASE,
    )
    return " ".join(p.strip() for p in re.split(r"(?<=[.!?])\s+", text)
                    if p.strip() and not drop.search(p))


def tile_prompt(place: dict, scale: str, design: str, request: str, guide: str, notes: str,
                edges: dict[str, str], with_refs: bool = False,
                terrain_lines: list[str] | None = None) -> str:
    """Build the full prompt for one tile. `edges` maps each side to 'painted', 'flat' or 'outer'.
    The block-out is always IMAGE 1; a style sheet, when sent, is IMAGE 2."""
    key = zone_key(place)
    prompt = FRAMING.replace("§ZONE_LINE§", zone_summary_line(place))
    if with_refs:
        marker = "only trees, grass and water. "
        prompt = prompt.replace(marker, marker + (
            "\n\nIMAGE 2 (<ref_image_1>) is a sheet of example building designs. Use it ONLY for the look of "
            "individual buildings — their shapes, roofs, materials and palette. It is not a scene or a layout: "
            "never copy its rows, light background or arrangement. Use each design only in its matching coloured "
            "zone of IMAGE 1.\n\nKeep everything else in IMAGE 1 exactly unchanged. "), 1)
    key_lines = [key] if key else []
    if terrain_lines:
        key_lines.extend(terrain_lines)
    if key_lines:
        prompt += "\n\nCOLOUR KEY (each colour shown in IMAGE 1 = what kind of place fills that area of IMAGE 1):\n" + "\n".join(key_lines)
    else:
        prompt += ("\n\nThis block-out shows only the land: grass, dark-green woods, blue water, brown roads and "
                   "grey squares. Paint the land as real ground and woods; put no buildings in it.")
    prompt += FILL_RULES + GROUND_WATER_RULES
    art = " ".join(x for x in (guide, design, request, notes) if x).strip()
    if art:
        # The planner's paint prompt describes the place AS A WHOLE. Keep it as STYLE ONLY: in a per-tile paint it must
        # not read as an instruction to compose a whole map inside one crop.
        art = _tile_safe_art_direction(art)
        if art:
            prompt += ("\n\nThe place's ART DIRECTION below describes the place AS A WHOLE. Use it for STYLE only — "
                       "palette, era, building materials, mood. It is NOT a description of this one tile.\n" + art[:1400])
    # Strong single-tile override, placed after the art direction so it beats any whole-place wording in it.
    prompt += (
        "\n\n*** IMAGE 1 (<ref_image_0>) IS EXACTLY ONE TILE OF A LARGER MAP — NOT THE WHOLE MAP. *** IMAGE 1, the "
        "block-out you are repainting, determines exactly what exists here. Paint land ONLY where IMAGE 1 shows land; "
        "keep EVERY black pixel black. If this tile's block-out is mostly black with just a small area of land (it is "
        "a corner or edge of the map), paint ONLY that small land area — do NOT fill the black, do NOT invent a whole "
        "village, and do NOT recentre, resize, complete or recompose the scene into a whole map, island, diamond or "
        "vignette. Any words in the art direction about painting a complete map, a whole village, or a square at the "
        "centre describe the WHOLE place and are NOT instructions for this tile."
    )
    # Per-tile position and the role of each side. The caller supplies the grid position via `edges`.
    position = edges.get("position", "one tile of a larger map") if isinstance(edges, dict) else ""
    role_dict = {k: v for k, v in edges.items() if k in SIDE_LABEL} if isinstance(edges, dict) else {}
    if role_dict:
        prompt += "\n\n" + _edge_sentence(role_dict, position)
    return prompt


def tile_note(edges: dict[str, str]) -> str:
    """Just the crop/context language, appended to a prompt built elsewhere (e.g. a building-colour key)."""
    head = ("\n\nIMAGE 1 (<ref_image_0>) IS ONE RECTANGULAR TILE / CROP OF A MUCH LARGER MAP — NOT THE WHOLE MAP. "
            "Paint up to the cut edges, no frame or border. ")
    role_dict = {k: v for k, v in edges.items() if k in SIDE_LABEL} if isinstance(edges, dict) else {}
    if role_dict:
        return head + _edge_sentence(role_dict, edges.get("position", "one tile of a larger map")) + "\nNo people, animals or text."
    return head + "Paint land exactly where IMAGE 1 shows it; keep black only at the map's true outer edge."


# ------------------------------------------------------------------------------------------------------------
# Cross-tile tone matching
# ------------------------------------------------------------------------------------------------------------
def _rgb2lab(rgb):
    import numpy as np
    rgb = np.clip(rgb, 0, 255) / 255.0
    lin = np.where(rgb > 0.04045, ((rgb + 0.055) / 1.055) ** 2.4, rgb / 12.92)
    M = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722],
                  [0.0193, 0.1192, 0.9505]], np.float32)
    xyz = (lin @ M.T) / np.array([0.95047, 1.0, 1.08883], np.float32)
    e, k = 216 / 24389, 24389 / 27
    f = np.where(xyz > e, np.cbrt(np.clip(xyz, 0, None)), (k * xyz + 16) / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]),
                     200 * (f[..., 1] - f[..., 2])], -1)


def _lab2rgb(lab):
    import numpy as np
    L, a, b = lab[..., 0], lab[..., 1], lab[..., 2]
    fy = (L + 16) / 116
    fx = fy + a / 500
    fz = fy - b / 200
    def finv(f):
        return np.where(f ** 3 > 216 / 24389, f ** 3, (116 * f - 16) / (24389 / 27))
    xyz = np.stack([finv(fx), finv(fy), finv(fz)], -1) * np.array([0.95047, 1.0, 1.08883], np.float32)
    M = np.array([[3.2406, -1.5372, -0.4986], [-0.9689, 1.8758, 0.0415],
                  [0.0557, -0.2040, 1.0570]], np.float32)
    lin = xyz @ M.T
    srgb = np.where(lin > 0.0031308, 1.055 * np.clip(lin, 0, None) ** (1 / 2.4) - 0.055, 12.92 * lin)
    return np.clip(srgb * 255, 0, 255)


def _match_tile_tones(image, n, black):
    """Each tile was painted in its own BFL call, so their brightness/colour drifts. Bring each tile's mean Lab to
    the median of all tiles, with a smooth Gaussian weight across seams so no hard quadrant edge appears. Black
    (outside the map) is left untouched. This is the step that made the manual tiling_6 result join cleanly."""
    import numpy as np
    arr = np.asarray(image).astype(np.float32)
    H, W, _ = arr.shape
    cell_w, cell_h = W / n, H / n
    lab = _rgb2lab(arr)
    Y, X = np.mgrid[0:H, 0:W]
    centres = [((r + 0.5) * cell_h, (c + 0.5) * cell_w) for r in range(n) for c in range(n)]
    means = []
    for cy, cx in centres:
        mask = (np.abs(Y - cy) < cell_h / 2) & (np.abs(X - cx) < cell_w / 2) & (lab[..., 0] > 25)
        means.append(lab[mask].mean(0) if mask.any() else np.zeros(3))
    means = np.stack(means)
    target = np.median(means, axis=0)
    deltas = target - means
    sigma = cell_w * 0.75
    weights = np.stack([np.exp(-((Y - cy) ** 2 + (X - cx) ** 2) / (2 * sigma ** 2)) for cy, cx in centres], -1)
    weights /= weights.sum(-1, keepdims=True)
    corr = np.sum(weights[..., None] * deltas[None, None, :, :], axis=2)
    corr[lab[..., 0] <= 25] = 0
    out = _lab2rgb(lab + corr).astype(np.uint8)
    return Image.fromarray(out)


# ------------------------------------------------------------------------------------------------------------
# The tiling engine
# ------------------------------------------------------------------------------------------------------------
def tile_count(n_across: int, n_down: int, cell_span: int) -> int:
    """Tiles per side for a scene of n_across x n_down cells (max dimension based)."""
    return max(1, math.ceil(max(n_across, n_down) / cell_span))


def paint_tiled(blockout_png: bytes, paint_one, cols: int, prompt_for_tile, rows: int | None = None,
                report=None, tiles_dir: str | Path | None = None, seed: int | None = None,
                black: tuple[int, int, int] | None = None, debug_dir: str | Path | None = None) -> bytes:
    """Paint a colour-coded block-out as congruent tiles and return stitched PNG bytes at the block-out's size.

    Works for ANY tile grid the editor chooses: `cols` tiles across, `rows` down (rows == cols when omitted). The
    block-out is split into a cols x rows grid; each cell is enlarged to the painting resolution. Tiles are painted
    right-to-left within each row, rows top-to-bottom, so a tile's right neighbour (same row, painted earlier) and top
    neighbour (row above, painted earlier) always exist and supply its right and top context bands:

      - top row: context only from the RIGHT tile;
      - right column: context only from the tile ABOVE;
      - all other tiles: context from BOTH the right tile and the tile above (and the four-tile meeting corner, which
        flows through the full-width bottom band and full-height left band).

    Each tile's block-out is a VARIABLE-SIZE appended block-out: the main crop plus only the flat LEFT band (when a
    left neighbour exists) and/or flat BOTTOM band (when a bottom neighbour exists). Tiles in the top row omit the
    bottom band, tiles in the left column omit the left band, the top-left tile omits both — so block-out dimensions
    differ per tile, exactly as the worked 2x2 example (TR 2212x1140, TL 2048x1140, BR 2212x1056, BL 2048x1056).
    After each paint the context bands are overwritten with the exact pixels of the neighbour they came from, so the
    seam lies inside a single paint pass. Finally the 8% bands are cut off and the tiles placed on an exact
    cols x rows grid, scaled back to the block-out's size.

    `paint_one(png, prompt, width, height, seed) -> painted PNG bytes` (the BFL call).
    `prompt_for_tile(r, c, context_edges) -> prompt string`.
    """
    src = Image.open(io.BytesIO(blockout_png)).convert("RGB")
    W, H = src.size
    if rows is None:
        rows = cols
    if black is None:
        black = src.getpixel((2, 2))

    saved = Path(tiles_dir) if tiles_dir else None
    if saved:
        saved.mkdir(parents=True, exist_ok=True)
    dbg = Path(debug_dir) if debug_dir else None
    if dbg:
        for sub in ('blocks', 'overlays', 'extracted', 'painted'):
            (dbg / sub).mkdir(parents=True, exist_ok=True)
        Image.open(io.BytesIO(blockout_png)).convert("RGB").save(dbg / "blockout_original.png")

    # A single tile: paint the whole block-out, no context.
    if cols <= 1 and rows <= 1:
        buf = io.BytesIO(); src.save(buf, format="PNG")
        out = Image.open(io.BytesIO(paint_one(buf.getvalue(), prompt_for_tile(0, 0, {"right":"outer","top":"outer","left":"outer","bottom":"outer","position":"the whole map (a single tile)"}), W, H, seed))).convert("RGB")
        if out.size != src.size:
            out = out.resize(src.size, Image.LANCZOS)
        b = io.BytesIO(); out.save(b, format="PNG")
        return b.getvalue()

    # Painting resolution per tile; height follows the block-out aspect, rounded to a multiple of 16.
    pw = min(TILE_WIDTH, W)
    ph = max(16, round(pw * H / W / 16) * 16)
    ctx_x, ctx_y = round(pw * OVERLAP), round(ph * OVERLAP)

    # Per-tile appended-block-out geometry. A tile only carries the bands that actually exist, so block-outs have
    # different dimensions: a flat LEFT band (right 8% of the tile to its LEFT) exists when c>0; a flat BOTTOM band
    # (top 8% of the tile BELOW it) exists when r<rows-1; the bottom-left corner exists when both exist. Painted bands
    # from already-painted right/top neighbours overwrite the main crop's edges and add no extra extent.
    def has_left(c):   return c > 0
    def has_bottom(r): return r < rows - 1
    def left_w(c):     return ctx_x if has_left(c) else 0
    def bottom_h(r):   return ctx_y if has_bottom(r) else 0
    def canvas_size(r, c):
        return (pw + left_w(c), ph + bottom_h(r))

    # Steps 1-2: split into cols x rows cells, each enlarged to the painting resolution.
    crops = {}
    for r in range(rows):
        for c in range(cols):
            bx0, bx1 = c * W // cols, (c + 1) * W // cols
            by0, by1 = r * H // rows, (r + 1) * H // rows
            crops[(r, c)] = src.crop((bx0, by0, bx1, by1)).resize((pw, ph), Image.LANCZOS)

    # Flat context bands cut from the (flat) blockout crops of the neighbouring cells. None when that band doesn't
    # exist (the tile is on the map's left / bottom outer edge) — no black padding is added.
    def right_of(r, c):   # right 8%, full height — the tile to this tile's LEFT
        if not has_left(c):
            return None
        return crops[(r, c - 1)].crop((pw - ctx_x, 0, pw, ph))
    def top_of(r, c):     # top 8%, full width — the tile BELOW this one
        if not has_bottom(r):
            return None
        return crops[(r + 1, c)].crop((0, 0, pw, ctx_y))
    def topright_of(r, c):  # top-right 8%x8% — the tile diagonally down-left (four-tile meeting corner)
        if not (has_left(c) and has_bottom(r)):
            return None
        return crops[(r + 1, c - 1)].crop((pw - ctx_x, 0, pw, ctx_y))

    # Painted bands from a neighbour, as painted (full height / full width so the corner flows through them). The
    # neighbour's canvas size differs from ours, so take its full extent rather than a global cw/ch.
    def painted_left(img):   return img.crop((0, 0, ctx_x, img.height))
    def painted_bottom(img): return img.crop((0, img.height - ctx_y, img.width, img.height))

    def context_edges(r, c):
        roles = tile_edge_roles(r, c, cols, rows)
        roles["position"] = tile_position(r, c, cols, rows)
        return roles

    total = cols * rows
    step = 0
    painted: dict[tuple[int, int], Image.Image] = {}

    # Paint order: right-to-left within each row, rows top-to-bottom.
    for r in range(rows):
        for c in range(cols - 1, -1, -1):
            right_nb = painted.get((r, c + 1)) if c < cols - 1 else None
            top_nb = painted.get((r - 1, c)) if r > 0 else None

            # Build this tile's variable-size appended block-out: only the flat bands that exist + main crop, then
            # painted neighbour bands over the main crop's right/top edges.
            cw, ch = canvas_size(r, c)
            lx = left_w(c)                              # x where the main crop starts
            rb, bb, cb = right_of(r, c), top_of(r, c), topright_of(r, c)
            block = Image.new("RGB", (cw, ch), black)
            if rb is not None:
                block.paste(rb, (0, 0))                 # flat left band
            if bb is not None:
                block.paste(bb, (lx, ph))               # flat bottom band
            if cb is not None:
                block.paste(cb, (0, ph))                # flat bottom-left corner (map centre here)
            block.paste(crops[(r, c)], (lx, 0))         # main crop
            # Painted context bands come from the ALREADY-PAINTED right/top neighbours (never from the flat blockout).
            # Order matters at the shared top-right corner: take %8 LEFT of the right neighbour (BR) onto this tile's
            # RIGHT edge FIRST, then %8 BOTTOM of the top neighbour (TL) onto the TOP. General for any grid position.
            pl = painted_left(right_nb) if right_nb is not None else None
            pb = painted_bottom(top_nb) if top_nb is not None else None
            if pl is not None:
                block.paste(pl, (cw - ctx_x, 0))          # BR -> right edge (first)
            if pb is not None:
                block.paste(pb, (0, 0))                   # TL -> top edge (second)

            if dbg:
                crops[(r, c)].save(dbg / "blocks" / f"crop_r{r}_c{c}.png")
                # Flat bands appended during BLOCKOUT preparation (from neighbouring block-outs, not paintings).
                if rb is not None: rb.save(dbg / "extracted" / f"flat_left_from_blockout_r{r}_c{c}.png")
                if bb is not None: bb.save(dbg / "extracted" / f"flat_bottom_from_blockout_r{r}_c{c}.png")
                if cb is not None: cb.save(dbg / "extracted" / f"flat_corner_from_blockout_r{r}_c{c}.png")
                # Painted bands EXTRACTED FROM ALREADY-PAINTED neighbours — the real seam context (steps 7/8/10/12).
                if pl is not None: pl.save(dbg / "extracted" / f"PAINTED_right_from_r{r}_c{c+1}_to_r{r}_c{c}.png")
                if pb is not None: pb.save(dbg / "extracted" / f"PAINTED_top_from_r{r-1}_c{c}_to_r{r}_c{c}.png")
                block.save(dbg / "overlays" / f"blockout_overlay_r{r}_c{c}.png")

            # No tile caching: always repaint, so a run can never reuse a stale tile from a previous/changed block-out.
            # Show WHICH tile is being built while it is in flight.
            if report:
                report(step, total, f"Painting tile {step + 1} of {total} (row {r+1}, column {c+1})")
            if saved:
                block.save(saved / f"blockout_r{r}_c{c}.png")
            buf = io.BytesIO(); block.save(buf, format="PNG")
            raw_painted = Image.open(io.BytesIO(paint_one(buf.getvalue(), prompt_for_tile(r, c, context_edges(r, c), step),
                                                          cw, ch, seed, step))).convert("RGB").resize((cw, ch), Image.LANCZOS)
            if dbg:
                raw_painted.save(dbg / "painted" / f"raw_r{r}_c{c}.png")
            result = raw_painted
            # EXACT restoration of the painted neighbour bands (congruence). Same as the manual 4-tile test:
            # TOP neighbour's bottom band first, then RIGHT neighbour's left band.
            if top_nb is not None:
                result.paste(painted_bottom(top_nb), (0, 0))
            if right_nb is not None:
                result.paste(painted_left(right_nb), (cw - ctx_x, 0))
            if saved:
                result.save(saved / f"painted_r{r}_c{c}.png")

            if dbg:
                result.save(dbg / "painted" / f"restored_r{r}_c{c}.png")
                (dbg / f"prompt_r{r}_c{c}.txt").write_text(prompt_for_tile(r, c, context_edges(r, c), step), encoding="utf-8")
            painted[(r, c)] = result
            step += 1
            if report:
                report(step, total, f"Painted tile {step} of {total} (row {r+1}, column {c+1})")

    # Cut the 8% context bands, combine on an exact grid.
    canvas = Image.new("RGB", (pw * cols, ph * rows))
    for r in range(rows):
        for c in range(cols):
            lx = left_w(c)
            main = painted[(r, c)].crop((lx, 0, lx + pw, ph))
            canvas.paste(main, (c * pw, r * ph))
    if dbg:
        canvas.save(dbg / "combined_raw_grid.png")

    # Restore the outer black at tile resolution (before scaling): where the block-out is black, force black. Doing this
    # before the scale keeps the mask aligned with the painted canvas instead of fighting the resample. This fixes
    # "content painted in black areas" without the "sides cut off" artifact of masking the already-scaled image.
    import numpy as _np
    src_scaled = src.resize((pw * cols, ph * rows), Image.LANCZOS)
    s_arr = _np.asarray(src_scaled).astype(int)
    c_arr = _np.asarray(canvas).astype(int)
    black_arr = _np.array(black).astype(int)
    is_black = (_np.abs(s_arr - black_arr).sum(2) <= 30)
    c_arr[is_black] = black_arr
    canvas = Image.fromarray(c_arr.astype("uint8"))

    # Scale back to the block-out's size.
    stitched = canvas.resize((W, H), Image.LANCZOS)
    if dbg:
        stitched.save(dbg / "combined_scaled.png")
        stitched.save(dbg / "final.png")
    out = io.BytesIO()
    stitched.save(out, format="PNG")
    return out.getvalue()
