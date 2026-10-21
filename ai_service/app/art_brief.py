"""The art brief (place-refine.md §14): before a piece of a place is drawn, the text model writes
what exactly to draw for it, in that place: the subject that fits there (a dungeon's light is a
wall torch, not a street lamp), its materials and colours, the exact view and the way it faces,
its proportions, and what to leave out. The image model then draws from that brief."""

from __future__ import annotations

import json

from .providers import Provider

TASK = """You write the prompt an image model draws ONE sprite from, for a 2D game seen in the view given.
The sprite is one piece of a place (given): it must fit that place, its style and its other pieces.

Write it as an art director briefing a pixel artist, in one paragraph of 60 to 120 words:
1. The subject, chosen to fit the place: the role says what it is for, the place says which one
   (a light in a dungeon is a torch in an iron wall bracket or a brazier, not a street lamp; a door in a
   crypt is heavy iron-bound oak in a stone arch; a well in a desert town is mud-brick). Name it exactly.
2. Its materials, colours and condition, from the place's kind and style (era, condition, mood, light):
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
7. If the last attempt went wrong (given), say plainly how this one differs.
8. In a place lit indoors ("light": "indoor"), it is seen by torchlight: dark colours, warm highlights.

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
