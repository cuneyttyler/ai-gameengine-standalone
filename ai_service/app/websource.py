"""Free art from the web (development_plan.md 7.9, place-assets.md rule W).

Searches an approved site (OpenGameArt: free game art with a licence per
asset), reads each result's licence and author from its page, and downloads
PNGs only, size-checked, into the need's own folder under assets/web/. A
result whose licence can't be read, or isn't allowed, is never downloaded.

How a search finds good art (2026-10-21):
- several queries, tried in turn ("cottage house", "cottage", "house"): the site's
  search wants every word, so one long phrase finds almost nothing;
- words like "isometric" or "top-down" don't filter (few pages say them), they
  rank: a page that says them comes first;
- each result page is ranked by how well its title names what's wanted, a single
  object before a pack or a sheet (unless a sheet is wanted);
- the file of a page whose name best matches is the one taken, not the first.
"""
from __future__ import annotations

import io
import json
import re
import time
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from datetime import date
from pathlib import Path
from typing import Callable

from PIL import Image

SITE = "https://opengameart.org"
AGENT = {"User-Agent": "AIGE-asset-sourcing/0.1 (game engine; finds free art for a user's own game)"}
MAX_BYTES = 4 * 1024 * 1024
ALLOWED = {"CC0", "CC-BY", "OGA-BY"}
VIEW_WORDS = {"isometric", "iso", "top-down", "topdown", "top", "down", "orthogonal", "side", "view", "2d", "pixel"}
PACK_WORDS = {"pack", "set", "collection", "tileset", "tiles", "sheet", "spritesheet", "atlas", "bundle", "kit"}

Fetch = Callable[[str], bytes]


def _fetch(url: str) -> bytes:
    with urllib.request.urlopen(urllib.request.Request(url, headers=AGENT), timeout=30) as response:
        return response.read(MAX_BYTES + 1)


def normalise_licence(name: str) -> str:
    """'CC-BY 3.0' -> CC-BY; 'CC0' -> CC0; 'OGA-BY 3.0' -> OGA-BY (as the engine's LicencePolicy)."""
    s = name.upper().replace("_", "-")
    if not s.strip():
        return ""
    if "CC0" in s or "PUBLIC DOMAIN" in s:
        return "CC0"
    if "OGA-BY" in s or "OGA BY" in s:
        return "OGA-BY"
    if "NC" in s:
        return "CC-BY-NC"
    if "ND" in s:
        return "CC-BY-ND"
    if "SA" in s and ("CC" in s or "BY" in s):
        return "CC-BY-SA"
    if "CC-BY" in s or "CC BY" in s:
        return "CC-BY"
    if "GPL" in s:
        return "GPL"
    return s.strip()


def read_page(html: str) -> dict:
    """An OpenGameArt asset page: its title, licences, author, tags and image files."""
    title = re.search(r"<title>\s*([^<|]+?)\s*(?:\||</title>)", html)
    licences = [normalise_licence(m) for m in re.findall(r"class='license-name'>([^<]+)<", html)]
    author = re.search(r"<span class='username'><a [^>]*>([^<]+)</a>", html)
    tags = [t.strip().lower() for t in re.findall(r'href="/art-search-advanced\?[^"]*field_art_tags_tid=([^"&]+)', html)]
    files = []
    for f in re.findall(r'href="(https://opengameart\.org/sites/default/files/[^"]+\.png)"', html):
        low = f.lower()
        if any(skip in low for skip in ("/css/", "/license_images/", "/styles/", "_prev", "preview")):
            continue
        if f not in files:
            files.append(f)
    previews = re.findall(r'src="(https://opengameart\.org/sites/default/files/styles/[^"]+\.(?:png|jpg|gif))"', html)
    return {"title": title.group(1).strip() if title else "", "licences": [l for l in licences if l],
            "author": author.group(1).strip() if author else "", "files": files,
            "tags": [urllib.parse.unquote_plus(t) for t in tags], "preview": previews[0] if previews else ""}


def _words(text: str) -> list[str]:
    return [w for w in re.split(r"[^a-z0-9]+", text.lower()) if w]


def queries_for(query: str, extra: list[str] | None = None) -> tuple[list[str], set[str]]:
    """The queries to try (the phrase without view words, then each content word), and the
    view words to rank by."""
    words = _words(query)
    prefer = {w for w in words if w in VIEW_WORDS}
    content = [w for w in words if w not in VIEW_WORDS and w not in {"a", "an", "the", "for", "game", "of", "with"}]
    tried: list[str] = []
    for q in [" ".join(content)] + list(extra or []) + list(reversed(content)):
        q = " ".join(w for w in _words(q) if w not in VIEW_WORDS)
        if q and q not in tried:
            tried.append(q)
    return tried, prefer


def score_page(info: dict, wanted: list[str], prefer: set[str], want_sheet: bool = False) -> float:
    """How well a page names what's wanted: its words in the title and tags, the preferred view,
    a single object before a pack (or the other way round, when a sheet is wanted)."""
    title = set(_words(info.get("title", "")))
    tags = set(_words(" ".join(info.get("tags", []))))
    score = 0.0
    for w in wanted:
        score += 2.0 if w in title else 1.0 if w in tags else 0.0
    for w in prefer:
        if w in title or w in tags or (w == "isometric" and "iso" in title):
            score += 1.5
    pack = bool((title | tags) & PACK_WORDS)
    score += (1.0 if pack else -0.5) if want_sheet else (-1.0 if pack else 0.5)
    return score


def pick_file(files: list[str], wanted: list[str]) -> str:
    """The file of a page whose name best names what's wanted (else its first)."""
    best, best_score = files[0], -1.0
    for f in files:
        name = set(_words(Path(urllib.parse.urlparse(f).path).stem))
        s = sum(1 for w in wanted if w in name) - (0.5 if name & {"sheet", "spritesheet", "atlas", "preview"} else 0)
        if s > best_score:
            best, best_score = f, s
    return best


def find_pages(query: str, max_pages: int, fetch: Fetch, rejected: list) -> list[str]:
    """The asset pages a query's search lists (2D art only), in the site's order."""
    links: list[str] = []
    for page in range(max_pages):
        url = (f"{SITE}/art-search-advanced?keys={urllib.parse.quote(query)}&field_art_type_tid%5B%5D=9"
               f"&sort_by=count&sort_order=DESC&page={page}")
        try:
            html = fetch(url).decode("utf-8", "replace")
        except Exception as error:  # the network: say so, keep what was found
            rejected.append({"url": url, "reason": f"search failed: {error}"})
            break
        found = [l for l in dict.fromkeys(re.findall(r'href="(/content/[^"#?]+)"', html))
                 if l not in ("/content/faq",) and "art-challenge" not in l]
        if not found:
            break
        links.extend(l for l in found if l not in links)
    return links


def candidates(query: str, max_results: int = 12, max_pages: int = 2, share_alike: bool = False, queries: list[str] | None = None,
               fetch: Fetch = _fetch, pause: float = 0.5, rejected: list | None = None) -> list[dict]:
    """Result pages for `query` whose licence is allowed, ranked best first, nothing downloaded:
    {page, title, licence, author, files, file (the best one), preview, score}."""
    allowed = ALLOWED | ({"CC-BY-SA"} if share_alike else set())
    rejected = rejected if rejected is not None else []
    tried, prefer = queries_for(query, queries)
    wanted = [w for w in _words(query) if w not in VIEW_WORDS]
    want_sheet = any(w in PACK_WORDS for w in wanted)
    out: list[dict] = []
    seen: set[str] = set()

    def read(link: str) -> tuple[str, dict | None, str]:
        page_url = SITE + link
        try:
            return page_url, read_page(fetch(page_url).decode("utf-8", "replace")), ""
        except Exception as error:
            return page_url, None, f"couldn't read the page: {error}"

    # Result pages read side by side (a few at a time: polite to the site), until there are enough.
    with ThreadPoolExecutor(max_workers=6) as pool:
        for q in tried:
            if len(out) >= max_results:
                break
            links = [l for l in find_pages(q, max_pages, fetch, rejected) if l not in seen]
            seen.update(links)
            for start in range(0, len(links), 6):
                if len(out) >= max_results:
                    break
                if pause:
                    time.sleep(pause)
                for page_url, info, why in pool.map(read, links[start:start + 6]):
                    if info is None:
                        rejected.append({"url": page_url, "reason": why})
                        continue
                    licence = next((l for l in info["licences"] if l in allowed), "")
                    if not licence:
                        rejected.append({"url": page_url, "reason": "licence " + (", ".join(info["licences"]) or "unreadable") + " isn't allowed"})
                        continue
                    if not info["files"]:
                        rejected.append({"url": page_url, "reason": "no PNG to download"})
                        continue
                    # Names nothing that was asked for (a basketball field for "fence"): not it.
                    named = set(_words(info["title"])) | set(_words(" ".join(info["tags"])))
                    meaningful = {w for w in wanted if len(w) >= 3}
                    if meaningful and not named & meaningful:
                        rejected.append({"url": page_url, "reason": "names none of: " + ", ".join(sorted(meaningful))})
                        continue
                    out.append({"page": page_url, "title": info["title"], "licence": licence, "author": info["author"],
                                "files": info["files"], "file": pick_file(info["files"], wanted), "preview": info["preview"] or info["files"][0],
                                "tags": info["tags"], "score": score_page(info, wanted, prefer, want_sheet), "query": q,
                                "source": "opengameart", "matched": sorted(named & meaningful)})
                    if len(out) >= max_results:
                        break
    out.sort(key=lambda c: -c["score"])
    return out


COMMONS = "https://commons.wikimedia.org/w/api.php"
COMMONS_FILES = "https://upload.wikimedia.org/"


def _plain(html: str) -> str:
    return re.sub(r"<[^>]+>", "", html or "").strip()


def commons_candidates(query: str, max_results: int, share_alike: bool, fetch: Fetch = _fetch, rejected: list | None = None) -> list[dict]:
    """Wikimedia Commons' PNGs for `query` (game-art words added), licence allowed, ranked:
    the same fields as `candidates`, with source "wikimedia"."""
    allowed = ALLOWED | ({"CC-BY-SA"} if share_alike else set())
    rejected = rejected if rejected is not None else []
    wanted = [w for w in _words(query) if w not in VIEW_WORDS]
    params = {"action": "query", "format": "json", "generator": "search", "gsrnamespace": "6", "gsrlimit": str(max(5, min(30, max_results * 2))),
              "gsrsearch": f"{query} sprite OR pixel OR tile filetype:bitmap", "prop": "imageinfo",
              "iiprop": "url|size|mime|extmetadata", "iiurlwidth": "256"}
    try:
        data = json.loads(fetch(COMMONS + "?" + urllib.parse.urlencode(params)).decode("utf-8", "replace"))
    except Exception:
        return []  # a second source down: the first one's results stand
    if not isinstance(data, dict):
        return []
    out = []
    for page in (data.get("query", {}).get("pages", {}) or {}).values():
        info = (page.get("imageinfo") or [{}])[0]
        if info.get("mime") != "image/png":
            continue
        meta = info.get("extmetadata", {})
        licence = normalise_licence(_plain(meta.get("LicenseShortName", {}).get("value", "")))
        title = page.get("title", "").removeprefix("File:").rsplit(".", 1)[0]
        if licence not in allowed:
            rejected.append({"url": info.get("descriptionurl", ""), "reason": f"licence {licence or 'unreadable'} isn't allowed"})
            continue
        named = set(_words(title)) | set(_words(_plain(meta.get("ImageDescription", {}).get("value", ""))))
        matched = sorted(named & {w for w in wanted if len(w) >= 3})
        if not matched:
            continue
        out.append({"page": info.get("descriptionurl", ""), "title": title, "licence": licence,
                    "author": _plain(meta.get("Artist", {}).get("value", ""))[:80], "files": [info.get("url", "")], "file": info.get("url", ""),
                    "preview": info.get("thumburl", info.get("url", "")), "tags": [], "size": [info.get("width", 0), info.get("height", 0)],
                    "score": 0.6 * len(matched) / max(1, len(wanted)), "query": query, "source": "wikimedia", "matched": matched})
    out.sort(key=lambda c: -c["score"])
    return out[:max_results]


def fetch_file(root: Path, url: str, folder: str, fetch: Fetch = _fetch) -> tuple[str | None, str, list[int]]:
    """One PNG from the site into the project's `folder`: its path, or None and why."""
    if not (url.startswith(SITE + "/sites/default/files/") or url.startswith(COMMONS_FILES)) or not url.lower().endswith(".png"):
        return None, "only PNG files from OpenGameArt and Wikimedia Commons are fetched", []
    target = (root / folder).resolve()
    if root.resolve() not in target.parents:
        return None, "the folder must be inside the project", []
    try:
        data = fetch(url)
    except Exception as error:
        return None, f"download failed: {error}", []
    if len(data) > MAX_BYTES or not data.startswith(b"\x89PNG"):
        return None, "not a PNG, or too big", []
    try:
        with Image.open(io.BytesIO(data)) as image:
            width, height = image.size
    except Exception:
        return None, "not a readable image", []
    if width < 16 or height < 16:
        return None, "too small", []
    target.mkdir(parents=True, exist_ok=True)
    name = re.sub(r"[^a-z0-9]+", "-", Path(urllib.parse.urlparse(url).path).stem.lower()).strip("-")[:48] or "asset"
    path = target / f"{name}.png"
    path.write_bytes(data)
    return path.relative_to(root.resolve()).as_posix(), "", [width, height]


def all_candidates(query: str, max_results: int = 12, share_alike: bool = False, queries: list[str] | None = None,
                   fetch: Fetch = _fetch, pause: float = 0.5, rejected: list | None = None, palette: list[str] | None = None) -> list[dict]:
    """Every source's results for `query`, ranked together. With the project's palette (its style
    references), each top result's preview is read and its colours count: art that matches the
    project's look ranks higher ("coherence" in the result)."""
    rejected = rejected if rejected is not None else []
    found = candidates(query, max_results, 2, share_alike, queries, fetch, pause, rejected)
    found += commons_candidates(query, max(4, max_results // 2), share_alike, fetch, rejected)
    if palette:
        from .style import image_palette, similarity
        for c in found[: max_results * 2]:
            try:
                fit = similarity(image_palette(fetch(c["preview"])), palette)
            except Exception:
                continue
            c["coherence"] = round(fit, 2)
            c["score"] = c["score"] + 0.8 * fit
    found.sort(key=lambda c: -c["score"])
    for c in found:
        why = [f"names {', '.join(c.get('matched', []))}"] if c.get("matched") else []
        if "coherence" in c:
            why.append(f"its colours match the project's {round(100 * c['coherence'])}%")
        why.append(f"{c['licence']} on {c.get('source', 'the web')}")
        c["why"] = "; ".join(why)
    return found[:max_results]


def search_web(root: Path, query: str, folder: str, max_assets: int = 4, max_pages: int = 3, share_alike: bool = False,
               fetch: Fetch = _fetch, pause: float = 0.5, queries: list[str] | None = None) -> dict:
    """Up to `max_assets` images for `query`, each with its licence, author and page: the
    best-ranked pages of several queries, the best file of each."""
    target = (root / folder).resolve()
    if root.resolve() not in target.parents:
        return {"ok": False, "errors": ["the folder must be inside the project"]}
    rejected: list = []
    from .style import project_palette
    ranked = all_candidates(query, max(max_assets * 3, 6), share_alike, queries, fetch, pause, rejected, project_palette(root))
    found = []
    for c in ranked:
        if len(found) >= max_assets:
            break
        path, why, pixels = fetch_file(root, c["file"], folder, fetch)
        if path is None:
            rejected.append({"url": c["file"], "reason": why})
            continue
        found.append({"path": path, "licence": c["licence"], "author": c["author"], "url": c["page"], "title": c["title"],
                      "retrieved": date.today().isoformat(), "pixels": pixels, "source": c.get("source", ""), "why": c.get("why", "")})
    return {"ok": True, "assets": found, "rejected": rejected}
