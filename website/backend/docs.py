"""The website's documentation (/docs), read from the editor's knowledge base (data/help, also what the AI panel answers
from): index.json gives the areas, their groups and pages in reading order; each page is a Markdown file whose `## `
headings are its sections and `### ` headings their subsections (the worked examples among them).

Pages are read again when their files change, so editing data/help shows on the site without a restart.
"""
from __future__ import annotations

import json
import re
import threading
from pathlib import Path

import markdown

HELP = Path(__file__).resolve().parents[2] / "data" / "help"

_lock = threading.Lock()
_cache: dict[str, tuple[int, dict]] = {}  # page key -> (file mtime, parsed page)


def _slug(text: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")


def _html(text: str) -> str:
    return markdown.markdown(text, extensions=["fenced_code", "tables", "sane_lists"], output_format="html")


def _plain(text: str) -> str:
    """Markdown as plain words, for search."""
    text = re.sub(r"```.*?```", " ", text, flags=re.S)
    return re.sub(r"[`*_#>|\[\]()]", " ", text)


def _split_questions(body: str) -> tuple[str, list[str]]:
    """A section's text without its `Questions:` line, and the questions (shown as "this answers")."""
    questions: list[str] = []
    kept = []
    lines = body.split("\n")
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("Questions:"):
            text = line[len("Questions:"):]
            while i + 1 < len(lines) and lines[i + 1].strip() and not lines[i + 1].startswith(("#", "Questions:")):
                i += 1
                text += " " + lines[i].strip()
            questions += [q.strip() + "?" for q in text.split("?") if q.strip()]
        else:
            kept.append(line)
        i += 1
    return "\n".join(kept).strip(), questions


def _parse(area: str, page: str, nav: str) -> dict | None:
    file = HELP / area / f"{page}.md"
    if not file.is_file():
        return None
    text = file.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
    title = next((line[2:].strip() for line in text.splitlines() if line.startswith("# ")), nav)
    parts = re.split(r"(?m)^## ", text)
    intro = re.sub(r"(?m)^# .*$", "", parts[0]).strip()
    sections = []
    used: set[str] = set()

    def unique(base: str) -> str:
        slug, n = base or "section", 2
        while slug in used:
            slug, n = f"{base}-{n}", n + 1
        used.add(slug)
        return slug

    for part in parts[1:]:
        heading, _, body = part.partition("\n")
        heading = heading.strip()
        body, questions = _split_questions(body.strip())
        # Its subsections: the text before the first `### ` is the section's own.
        pieces = re.split(r"(?m)^### ", body)
        own = pieces[0].strip()
        section_slug = unique(_slug(heading))
        subsections = []
        for piece in pieces[1:]:
            sub_heading, _, sub_body = piece.partition("\n")
            sub_heading = sub_heading.strip()
            example = sub_heading.lower().startswith("example")
            shown = re.sub(r"^example:\s*", "", sub_heading, flags=re.I) if example else sub_heading
            subsections.append({
                "title": shown[:1].upper() + shown[1:],
                "slug": unique(f"{section_slug}-{_slug(sub_heading)}"),
                "example": example,
                "html": _html(sub_body.strip()),
            })
        sections.append({
            "title": heading,
            "slug": section_slug,
            "html": _html(own),
            "subsections": subsections,
            "questions": questions,
            "text": _plain(heading + " " + body),
        })
    return {"area": area, "id": page, "nav": nav, "title": title, "intro": _html(intro), "sections": sections}


def page(area: str, page_id: str, nav: str = "") -> dict | None:
    file = HELP / area / f"{page_id}.md"
    if not file.is_file():
        return None
    key = f"{area}/{page_id}"
    stamp = file.stat().st_mtime_ns
    with _lock:
        cached = _cache.get(key)
        if cached and cached[0] == stamp:
            return cached[1]
    parsed = _parse(area, page_id, nav)
    if parsed is not None:
        with _lock:
            _cache[key] = (stamp, parsed)
    return parsed


def _index() -> dict:
    return json.loads((HELP / "index.json").read_text(encoding="utf-8"))


def tree() -> dict:
    """The sidebar: areas, their groups, pages and each page's sections and subsections (titles and anchors only)."""
    index = _index()
    areas = []
    listed: set[str] = set()
    for area in index["areas"]:
        groups = []
        for group in area["groups"]:
            pages = []
            for entry in group["pages"]:
                p = page(area["id"], entry["id"], entry.get("nav", ""))
                if p is None:
                    continue
                listed.add(f"{area['id']}/{entry['id']}")
                pages.append({
                    "id": p["id"], "nav": entry.get("nav") or p["title"], "title": p["title"],
                    "sections": [{"title": s["title"], "slug": s["slug"],
                                  "subsections": [{"title": u["title"], "slug": u["slug"], "example": u["example"]}
                                                  for u in s["subsections"]]}
                                 for s in p["sections"]],
                })
            if pages:
                groups.append({"title": group["title"], "pages": pages})
        # A file not listed in index.json still shows, at the end of its area.
        extra = []
        for file in sorted((HELP / area["id"]).glob("*.md")):
            if f"{area['id']}/{file.stem}" not in listed:
                p = page(area["id"], file.stem)
                if p:
                    extra.append({"id": p["id"], "nav": p["title"], "title": p["title"],
                                  "sections": [{"title": s["title"], "slug": s["slug"], "subsections": []} for s in p["sections"]]})
        if extra:
            groups.append({"title": "More", "pages": extra})
        areas.append({"id": area["id"], "title": area["title"], "intro": area.get("intro", ""), "groups": groups})
    return {"title": index.get("title", "Documentation"), "intro": index.get("intro", ""), "areas": areas}


def search(query: str, limit: int = 30) -> list[dict]:
    """Sections whose words match the query, best first: every word must appear; titles count more."""
    words = [w for w in re.findall(r"[a-z0-9]+", query.lower()) if len(w) > 1]
    if not words:
        return []
    hits = []
    for area in _index()["areas"]:
        for group in area["groups"]:
            for entry in group["pages"]:
                p = page(area["id"], entry["id"], entry.get("nav", ""))
                if p is None:
                    continue
                for s in p["sections"]:
                    title = s["title"].lower()
                    body = s["text"].lower()
                    if not all(w in body or w in title for w in words):
                        continue
                    score = sum(3 if w in title else 0 for w in words) + sum(body.count(w) for w in words) * 0.1
                    first = min((body.find(w) for w in words if w in body), default=0)
                    snippet = re.sub(r"\s+", " ", s["text"][max(0, first - 60):first + 160]).strip()
                    hits.append({"area": area["id"], "areaTitle": area["title"], "page": p["id"], "pageTitle": entry.get("nav") or p["title"],
                                 "section": s["slug"], "title": s["title"], "snippet": snippet, "score": score})
    hits.sort(key=lambda h: -h["score"])
    return hits[:limit]
