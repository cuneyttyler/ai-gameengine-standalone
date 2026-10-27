"""The AIGE logo (docs/brand): an isometric cube with a spark (the mark) and the wordmark, AI and GE styled apart.
Every letter and face is a polygon here, so the SVGs need no font and the PNG previews are drawn from the same shapes.

    ai_service/.venv/Scripts/python.exe -I docs/brand/make_logo.py

Writes aige-mark.svg, aige-logo.svg (mark + wordmark, for light backgrounds), aige-logo-dark.svg (for dark ones),
aige-wordmark.svg, aige-logo-outline.svg (GE outlined) and preview PNGs of each, and the app icon (aige-icon.png, .ico)."""
from pathlib import Path

from PIL import Image, ImageDraw

OUT = Path(__file__).resolve().parent

# Colours: the blues of the mark and the AI, a deep navy for GE on light, a pale blue for GE on dark.
SKY, BLUE, ROYAL, NAVY, CYAN = "#7cc4ff", "#2f7cf6", "#1d4ed8", "#0f2a6b", "#22d3ee"
INK_LIGHT, INK_DARK = "#0f2a6b", "#cfe3ff"

# The letters, 100 units tall, square-cut (the editor's look): polygons; A has a counter (a hole).
LETTERS = {
    "A": ([(0, 100), (38, 0), (62, 0), (100, 100), (76, 100), (68, 78), (32, 78), (24, 100)], [(40, 57), (50, 30), (60, 57)], 100),
    "I": ([(0, 0), (24, 0), (24, 100), (0, 100)], None, 24),
    "G": ([(0, 0), (90, 0), (90, 22), (24, 22), (24, 78), (66, 78), (66, 66), (46, 66), (46, 46), (90, 46), (90, 100), (0, 100)], None, 90),
    "E": ([(0, 0), (82, 0), (82, 22), (24, 22), (24, 39), (72, 39), (72, 61), (24, 61), (24, 78), (82, 78), (82, 100), (0, 100)], None, 82),
}
GAP, PAIR_GAP = 14, 30  # between letters; between AI and GE


def cube():
    """The mark, in a 120 x 128 box: the cube's three faces and the spark on its top."""
    top = [(60, 8), (108, 36), (60, 64), (12, 36)]
    left = [(12, 36), (60, 64), (60, 120), (12, 92)]
    right = [(60, 64), (108, 36), (108, 92), (60, 120)]
    cx, cy, r, w = 60, 36, 15, 4.2  # a four-pointed spark, flattened to the top face's angle
    spark = [(cx, cy - r * 0.58), (cx + w, cy - w * 0.58), (cx + r, cy), (cx + w, cy + w * 0.58), (cx, cy + r * 0.58),
             (cx - w, cy + w * 0.58), (cx - r, cy), (cx - w, cy - w * 0.58)]
    traces, nodes = [], []  # (circuit lines on the faces read as a scribble at small sizes: left out)
    return top, left, right, spark, traces, nodes


def word_layout(scale: float, x0: float, y0: float):
    """Each letter's polygons placed: [(letter, outer, hole)]."""
    out, x = [], x0
    for i, ch in enumerate("AIGE"):
        outer, hole, width = LETTERS[ch]
        place = lambda pts: [(x + px * scale, y0 + py * scale) for px, py in pts]  # noqa: E731
        out.append((ch, place(outer), place(hole) if hole else None))
        x += (width + (PAIR_GAP if ch == "I" else GAP)) * scale
    return out, x - GAP * scale


def pts(p):
    return " ".join(f"{x:.1f},{y:.1f}" for x, y in p)


def svg(kind: str, dark: bool = False, ge_style: str = "solid") -> str:
    """kind: mark, logo (mark + word), word. ge_style: solid (navy or pale) or outline."""
    ink = INK_DARK if dark else INK_LIGHT
    defs = f"""<defs>
  <linearGradient id="ai" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="{BLUE}"/><stop offset="1" stop-color="{CYAN}"/></linearGradient>
  <linearGradient id="top" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="{SKY}"/><stop offset="1" stop-color="#a5dcff"/></linearGradient>
</defs>"""
    body, width, height = [], 0, 0
    if kind in ("mark", "logo"):
        top, left, right, spark, traces, nodes = cube()
        body += [f'<polygon points="{pts(top)}" fill="url(#top)"/>', f'<polygon points="{pts(left)}" fill="{BLUE}"/>',
                 f'<polygon points="{pts(right)}" fill="{ROYAL}"/>', f'<polygon points="{pts(spark)}" fill="#ffffff"/>']
        for t in traces:
            body.append(f'<polyline points="{pts(t)}" fill="none" stroke="#bfe6ff" stroke-width="3" stroke-linejoin="miter" opacity="0.85"/>')
        for nx, ny in nodes:
            body.append(f'<rect x="{nx - 4}" y="{ny - 4}" width="8" height="8" fill="#ffffff"/>')
        width, height = 120, 128
    if kind in ("logo", "word"):
        scale = 0.78
        x0 = 138 if kind == "logo" else 8
        y0 = 64 - 50 * scale if kind == "logo" else 8
        letters, end = word_layout(scale, x0, y0)
        for ch, outer, hole in letters:
            d = f"M{pts(outer)} Z" + (f" M{pts(hole)} Z" if hole else "")
            if ch in "AI":
                body.append(f'<path d="{d}" fill="url(#ai)" fill-rule="evenodd"/>')
            elif ge_style == "outline":
                body.append(f'<path d="{d}" fill="none" stroke="{ink}" stroke-width="3.5" stroke-linejoin="miter" fill-rule="evenodd"/>')
            else:
                body.append(f'<path d="{d}" fill="{ink}" fill-rule="evenodd"/>')
        width = end + 8
        height = max(height, y0 + 100 * scale + 8)
    background = f'<rect width="100%" height="100%" fill="#0b1020"/>' if dark else ""
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width:.0f} {height:.0f}" width="{width:.0f}" height="{height:.0f}">\n'
            f'<title>AIGE: AI Game Engine</title>\n{defs}\n{background}\n' + "\n".join(body) + "\n</svg>\n")


# --- PNG previews, drawn from the same shapes ---

def hex_rgb(h: str):
    return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))


def gradient_fill(img: Image.Image, poly, hole, c0: str, c1: str, box):
    """A polygon filled with a diagonal gradient across `box` (x0, y0, x1, y1)."""
    mask = Image.new("L", img.size, 0)
    d = ImageDraw.Draw(mask)
    d.polygon(poly, fill=255)
    if hole:
        d.polygon(hole, fill=0)
    x0, y0, x1, y1 = box
    a, b = hex_rgb(c0), hex_rgb(c1)
    grad = Image.new("RGB", img.size)
    gp = grad.load()
    span = max(1.0, (x1 - x0) + (y1 - y0))
    for y in range(img.size[1]):
        for x in range(img.size[0]):
            t = min(1.0, max(0.0, ((x - x0) + (y - y0)) / span))
            gp[x, y] = tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))
    img.paste(grad, (0, 0), mask)


def png(kind: str, path: Path, dark: bool = False, ge_style: str = "solid", s: float = 4.0):
    ink = INK_DARK if dark else INK_LIGHT
    w = 120 if kind == "mark" else (138 + word_layout(0.78, 0, 0)[1] + 8 if kind == "logo" else word_layout(0.78, 8, 8)[1] + 8)
    h = 128 if kind != "word" else 100 * 0.78 + 16
    img = Image.new("RGB", (int(w * s), int(h * s)), hex_rgb("#0b1020") if dark else (255, 255, 255))
    d = ImageDraw.Draw(img)
    S = lambda p: [(x * s, y * s) for x, y in p]  # noqa: E731
    if kind in ("mark", "logo"):
        top, left, right, spark, traces, nodes = cube()
        gradient_fill(img, S(top), None, SKY, "#a5dcff", (12 * s, 8 * s, 108 * s, 64 * s))
        d.polygon(S(left), fill=BLUE)
        d.polygon(S(right), fill=ROYAL)
        d.polygon(S(spark), fill="#ffffff")
        for t in traces:
            d.line(S(t), fill="#bfe6ff", width=int(3 * s), joint="curve")
        for nx, ny in nodes:
            d.rectangle([(nx - 4) * s, (ny - 4) * s, (nx + 4) * s, (ny + 4) * s], fill="#ffffff")
    if kind in ("logo", "word"):
        x0 = 138 if kind == "logo" else 8
        y0 = 64 - 50 * 0.78 if kind == "logo" else 8
        letters, _ = word_layout(0.78, x0, y0)
        ai_box = None
        for ch, outer, hole in letters:
            if ch in "AI":
                xs = [p[0] for p in outer]
                ai_box = (min(xs + ([ai_box[0]] if ai_box else [])), y0, max(xs + ([ai_box[2]] if ai_box else [])), y0 + 78)
        for ch, outer, hole in letters:
            if ch in "AI":
                gradient_fill(img, S(outer), S(hole) if hole else None, BLUE, CYAN, tuple(v * s for v in ai_box))
            elif ge_style == "outline":
                d.line(S(outer + [outer[0]]), fill=ink, width=int(3.5 * s), joint="curve")
            else:
                d.polygon(S(outer), fill=ink)
    img.save(path)


def icon():
    """The app icon: the mark on a deep blue square (sharp corners), 256 px, and a multi-size .ico."""
    S = 256
    img = Image.new("RGBA", (S, S), hex_rgb("#0b1020") + (255,))  # the dark background the mark is drawn on
    mark = Image.new("RGB", (480, 512), hex_rgb("#0b1736"))
    tmp = OUT / "_mark.png"
    png("mark", tmp, dark=True, s=4.0)
    m = Image.open(tmp).convert("RGB")
    tmp.unlink()
    m.thumbnail((200, 200), Image.LANCZOS)
    img.paste(m, ((S - m.width) // 2, (S - m.height) // 2))
    img.save(OUT / "aige-icon.png")
    img.save(OUT / "aige-icon.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    del mark


if __name__ == "__main__":
    files = {
        "aige-mark": ("mark", False, "solid"),
        "aige-logo": ("logo", False, "solid"),
        "aige-logo-dark": ("logo", True, "solid"),
        "aige-logo-outline": ("logo", False, "outline"),
        "aige-logo-outline-dark": ("logo", True, "outline"),
        "aige-wordmark": ("word", False, "solid"),
    }
    for name, (kind, dark, ge) in files.items():
        (OUT / f"{name}.svg").write_text(svg(kind, dark, ge), encoding="utf-8")
        png(kind, OUT / f"{name}.png", dark, ge, s=3.0)
    icon()
    print("written to", OUT)
