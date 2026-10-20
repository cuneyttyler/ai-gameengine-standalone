"""Free art from the web (development_plan.md 7.9, place-assets.md rule W).

Searches an approved site (OpenGameArt: free game art with a licence per
asset), reads each result's licence and author from its page, and downloads
PNGs only, size-checked, into the need's own folder under assets/web/. A
result whose licence can't be read, or isn't allowed, is never downloaded.
"""
from __future__ import annotations

import io
import re
import time
import urllib.parse
import urllib.request
from datetime import date
from pathlib import Path
from typing import Callable

from PIL import Image

SITE = "https://opengameart.org"
AGENT = {"User-Agent": "AIGE-asset-sourcing/0.1 (game engine; finds free art for a user's own game)"}
MAX_BYTES = 4 * 1024 * 1024
ALLOWED = {"CC0", "CC-BY", "OGA-BY"}

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
    """An OpenGameArt asset page: its title, licences, author and image files."""
    title = re.search(r"<title>\s*([^<|]+?)\s*(?:\||</title>)", html)
    licences = [normalise_licence(m) for m in re.findall(r"class='license-name'>([^<]+)<", html)]
    author = re.search(r"<span class='username'><a [^>]*>([^<]+)</a>", html)
    files = []
    for f in re.findall(r'href="(https://opengameart\.org/sites/default/files/[^"]+\.png)"', html):
        low = f.lower()
        if any(skip in low for skip in ("/css/", "/license_images/", "/styles/", "_prev", "preview")):
            continue
        if f not in files:
            files.append(f)
    return {"title": title.group(1).strip() if title else "", "licences": [l for l in licences if l],
            "author": author.group(1).strip() if author else "", "files": files}


def search_web(root: Path, query: str, folder: str, max_assets: int = 4, max_pages: int = 3, share_alike: bool = False,
               fetch: Fetch = _fetch, pause: float = 0.5) -> dict:
    """Up to `max_assets` images for `query`, each with its licence, author and page."""
    allowed = ALLOWED | ({"CC-BY-SA"} if share_alike else set())
    target = (root / folder).resolve()
    if root.resolve() not in target.parents:
        return {"ok": False, "errors": ["the folder must be inside the project"]}
    target.mkdir(parents=True, exist_ok=True)
    found, rejected, seen = [], [], set()
    for page in range(max_pages):
        if len(found) >= max_assets:
            break
        url = (f"{SITE}/art-search-advanced?keys={urllib.parse.quote(query)}&field_art_type_tid%5B%5D=9"
               f"&sort_by=count&sort_order=DESC&page={page}")
        try:
            html = fetch(url).decode("utf-8", "replace")
        except Exception as error:  # the network: say so, keep what was found
            rejected.append({"url": url, "reason": f"search failed: {error}"})
            break
        links = [l for l in dict.fromkeys(re.findall(r'href="(/content/[^"#?]+)"', html))
                 if l not in ("/content/faq",) and "art-challenge" not in l]
        if not links:
            break
        for link in links:
            if len(found) >= max_assets or link in seen:
                continue
            seen.add(link)
            page_url = SITE + link
            time.sleep(pause)
            try:
                info = read_page(fetch(page_url).decode("utf-8", "replace"))
            except Exception as error:
                rejected.append({"url": page_url, "reason": f"couldn't read the page: {error}"})
                continue
            licence = next((l for l in info["licences"] if l in allowed), "")
            if not licence:
                rejected.append({"url": page_url, "reason": "licence " + (", ".join(info["licences"]) or "unreadable") + " isn't allowed"})
                continue
            if not info["files"]:
                rejected.append({"url": page_url, "reason": "no PNG to download"})
                continue
            file_url = info["files"][0]
            try:
                data = fetch(file_url)
            except Exception as error:
                rejected.append({"url": file_url, "reason": f"download failed: {error}"})
                continue
            if len(data) > MAX_BYTES or not data.startswith(b"\x89PNG"):
                rejected.append({"url": file_url, "reason": "not a PNG, or too big"})
                continue
            try:
                with Image.open(io.BytesIO(data)) as image:
                    width, height = image.size
            except Exception:
                rejected.append({"url": file_url, "reason": "not a readable image"})
                continue
            if width < 16 or height < 16:
                rejected.append({"url": file_url, "reason": "too small"})
                continue
            name = re.sub(r"[^a-z0-9]+", "-", Path(urllib.parse.urlparse(file_url).path).stem.lower()).strip("-")[:48] or "asset"
            path = target / f"{name}.png"
            path.write_bytes(data)
            found.append({"path": path.relative_to(root.resolve()).as_posix(), "licence": licence, "author": info["author"],
                          "url": page_url, "title": info["title"], "retrieved": date.today().isoformat(),
                          "pixels": [width, height]})
    return {"ok": True, "assets": found, "rejected": rejected}
