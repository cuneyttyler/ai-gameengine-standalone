"""The art brief (place-refine.md §14): before a piece of a place is drawn, the text model writes
what exactly to draw for it, in that place: the subject that fits there (a dungeon's light is a
wall torch, not a street lamp), its materials and colours, the exact view and the way it faces,
its proportions, and what to leave out. The image model then draws from that brief."""

from __future__ import annotations

import json

from .providers import Provider

TASK = """You write the prompt an image model draws ONE sprite from, for a 2D game seen in the view given.
The sprite is one piece of a place (given): it must fit that place, its style and its other pieces.

Write it as an art director briefing a pixel artist, in one paragraph of 90 to 160 words, concrete and visual
(name colours, materials, shapes and light; no vague words like "nice" or "detailed"):
1. The subject, chosen to fit the place: the role says what it is for, the place says which one
   (a light in a dungeon is a torch in an iron wall bracket or a brazier, not a street lamp; a door in a
   crypt is heavy iron-bound oak in a stone arch; a well in a desert town is mud-brick). Name it exactly.
   It stands in a room or part of the place ("room") and serves its story ("placeStory"): make it belong
   there — a lamp in a tavern's common room is a smoky oil lamp or a candle on a wooden table, not a
   street lamp; a chair in a crypt's shrine is stone, not a padded armchair.
2. Its materials, colours and condition, from the place's kind and style (era, condition, mood, light) and
   its story's mood ("placeStory": a cursed, ruined or oppressive place is drawn dark, weathered and muted;
   a prosperous one bright and well kept). When the project's palette is given ("matchProjectStyle"), keep its
   pixel size, outline and shading, and name its colours (hex) where they suit the mood; the mood wins
   where they clash (a grim place never gets a cheerful palette because earlier art had one):
   read what the place is like and draw that. A dungeon, crypt, cave or mine is old, damp and dark: rough-hewn
   stone, cracks, moss, rust, soot, worn edges, nothing clean or new. A village is lived-in and weathered; a
   palace rich and well kept; a camp makeshift. Never clean, tidy or modern unless the place is.
3. The view, exactly: for "isometric", a true isometric 2:1 view from above at about 30 degrees, the
   object's two visible sides facing down-left and down-right. If it "runs" along a line (a door in its
   wall, a fence or wall section), say exactly which way it runs, as given, and that it is seen at that angle.
4. Its proportions: how wide it is (in cells) and how tall for its width, as given.
5. The art direction, as given (keep its words).
6. Framing: the one object alone, whole, centred, on a plain flat background of one colour; no ground
   patch, no base, no cast shadow on the ground (the game draws shadows), no text, no frame. A thing that
   hangs on or stands in a wall (a torch, a door, a banner) is drawn alone: no wall, board, plaque, panel
   or card behind it, nothing but the object and its own fixings.
   For a ground tile instead: the ground alone, filling the square edge to edge, seen straight from above.
   A BUILDING ("whole": "building") is the WHOLE building seen from outside: its walls, its roof, its door
   and windows, standing on its footprint — never only its sign, door, gate, porch or another part of it.
   An inn is the inn's building (a sign may hang on it), not the sign.
7. If the last attempt went wrong ("lastAttemptWrong", with the "previousPrompt" it was drawn from):
   start from that prompt and rewrite it so each problem can't happen again — name what to draw instead and
   say plainly what NOT to draw (e.g. "the whole two-storey inn building, NOT a hanging sign"). Keep what
   was right.
8. In a place lit indoors ("light": "indoor"), it is seen by torchlight: dark colours, warm highlights.
10. When the place was sketched ("sketch"), the piece's crop of that painting is the look to keep: describe
   what the crop shows of it (its shape, materials, colours, light), drawn alone and whole.
9. It is designed against other pieces ("goesWith": a door against the wall it stands in, a torch against the
   wall it hangs on, a chair against its table): as a human artist would, look at them and draw a piece that
   works with them — the same materials, colours, pixel size and way of drawing, the right scale against them,
   and say how it meets them (the door set flush in that stone wall, its frame in the same stone).

Answer with JSON only: {"subject": "<the subject in a few words>", "prompt": "<the paragraph>"}"""


def write_art_prompt(provider: Provider, description: str, context: dict, kind: str) -> tuple[str, dict]:
    """The prompt to draw from, and the brief as written (subject, prompt). Falls back to the
    description given when the model's answer isn't usable."""
    asked = {"kind": kind, "what": description, **context}
    reply = provider.complete([
        {"role": "system", "content": TASK},
        {"role": "user", "content": json.dumps(asked, ensure_ascii=False)},
    ])
    try:
        data = json.loads(reply.text)
    except (json.JSONDecodeError, TypeError):
        return description, {"error": "not JSON"}
    prompt = str(data.get("prompt", "")).strip() if isinstance(data, dict) else ""
    if len(prompt) < 20:
        return description, {"error": "no prompt"}
    return prompt, {"subject": str(data.get("subject", "")), "prompt": prompt}


SEARCH_TASK = """You help find free 2D game art on OpenGameArt and Wikimedia Commons. A search came back with
results that don't fit (their titles are given). Write up to four better searches for what is wanted: the words
artists use for such art (its common names, "sprite", "tileset", "isometric", the object's kind), short (2 to 4
words each), different from each other and from the first search.
Answer with JSON only: {"queries": ["...", "..."]}"""


def better_queries(provider: Provider, query: str, titles: list[str]) -> list[str]:
    """Better searches for `query`, given the titles a first search found (empty when there are none)."""
    try:
        reply = provider.complete([{"role": "system", "content": SEARCH_TASK},
                                   {"role": "user", "content": json.dumps({"wanted": query, "found": titles}, ensure_ascii=False)}])
        data = json.loads(reply.text)
    except Exception:
        return []
    qs = data.get("queries", []) if isinstance(data, dict) else []
    return [str(q).strip() for q in qs if str(q).strip() and str(q).strip().lower() != query.lower()][:4]
