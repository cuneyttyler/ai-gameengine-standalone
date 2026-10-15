"""The World Bible search index (development_plan.md 3.1).

The canon itself lives in the project as world/<kind>/<id>.md files (engine/world
reads and writes them; the format is documented in engine/world/WorldBible.h).
This module only *indexes* them for retrieval: an SQLite file in the project's
.cache/ holding one embedding per entry, refreshed incrementally (an entry is
re-embedded only when its text changes) and rebuildable at any time.

Without an embedder — no API key, offline, or the call fails — search falls back
to keyword ranking, so it always answers.
"""

from __future__ import annotations

import hashlib
import math
import re
import sqlite3
import struct
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Protocol

KINDS = ("character", "faction", "location", "item", "event", "lore", "style")


@dataclass
class WorldEntry:
    kind: str
    id: str
    name: str = ""
    summary: str = ""
    tags: list[str] = field(default_factory=list)
    relations: list[tuple[str, str]] = field(default_factory=list)  # (type, "<kind>/<id>")
    body: str = ""

    @property
    def key(self) -> str:
        return f"{self.kind}/{self.id}"

    def text(self) -> str:
        """What gets embedded: everything a question about this entry might match."""
        parts = [f"{self.name} ({self.kind})", self.summary]
        if self.tags:
            parts.append("Tags: " + ", ".join(self.tags))
        if self.relations:
            parts.append("Related: " + "; ".join(f"{t} {target}" for t, target in self.relations))
        parts.append(self.body)
        return "\n".join(p for p in parts if p)


def parse_entry(text: str, kind: str, id: str) -> WorldEntry | None:
    """Mirror of engine/world ParseWorldEntry. None if it isn't an entry."""
    lines = text.replace("\r\n", "\n").replace("\r", "\n").split("\n")
    i = 0
    while i < len(lines) and not lines[i].strip():
        i += 1
    if i >= len(lines) or lines[i].strip() != "---":
        return None
    entry = WorldEntry(kind=kind, id=id)
    i += 1
    while i < len(lines) and lines[i].strip() != "---":
        line = lines[i].strip()
        i += 1
        if not line:
            continue
        key, sep, value = line.partition(":")
        if not sep:
            return None
        key, value = key.strip(), value.strip()
        if key == "name":
            entry.name = value
        elif key == "summary":
            entry.summary = value
        elif key == "tags":
            entry.tags = [t.strip() for t in value.split(",") if t.strip()]
        elif key == "rel":
            for part in value.split(";"):
                part = part.strip()
                if not part:
                    continue
                type_, _, target = part.rpartition(" ")
                entry.relations.append(((type_.strip() or "related_to").replace(" ", "_"), target.strip()))
    if i >= len(lines):
        return None  # header never closed
    entry.body = "\n".join(lines[i + 1 :]).strip("\n")
    return entry


def load_entries(root: Path) -> list[WorldEntry]:
    entries = []
    for kind in KINDS:
        folder = root / "world" / kind
        if not folder.is_dir():
            continue
        for path in sorted(folder.glob("*.md")):
            entry = parse_entry(path.read_text(encoding="utf-8", errors="replace"), kind, path.stem)
            if entry is not None:
                entries.append(entry)
    return entries


class Embedder(Protocol):
    name: str

    def embed(self, texts: list[str]) -> list[list[float]]: ...


@dataclass
class SearchHit:
    key: str
    kind: str
    name: str
    summary: str
    score: float


_WORD = re.compile(r"[a-z0-9]+")


def _words(text: str) -> list[str]:
    return _WORD.findall(text.lower())


def _pack(vector: list[float]) -> bytes:
    return struct.pack(f"<{len(vector)}f", *vector)


def _unpack(blob: bytes) -> list[float]:
    return list(struct.unpack(f"<{len(blob) // 4}f", blob))


def _cosine(a: list[float], b: list[float]) -> float:
    dot = sum(x * y for x, y in zip(a, b))
    na = math.sqrt(sum(x * x for x in a))
    nb = math.sqrt(sum(y * y for y in b))
    return dot / (na * nb) if na and nb else 0.0


def format_entry(entry: WorldEntry) -> str:
    """An entry as the model reads it: the same facts as its file."""
    lines = [f"[{entry.key}] {entry.name}" + (f" — {entry.summary}" if entry.summary else "")]
    if entry.tags:
        lines.append("tags: " + ", ".join(entry.tags))
    for type_, target in entry.relations:
        lines.append(f"{type_} {target}")
    if entry.body:
        lines.append(entry.body)
    return "\n".join(lines)


class WorldIndex:
    """Embeddings of a project's World Bible in <root>/.cache/world_index.sqlite."""

    def __init__(self, root: Path, embedder: Embedder | None):
        self.root = Path(root)
        self.embedder = embedder
        self.entries: list[WorldEntry] = []
        self.embedded_last_refresh = 0  # how many entries the last refresh had to embed (tests, logs)
        db_path = self.root / ".cache" / "world_index.sqlite"
        db_path.parent.mkdir(parents=True, exist_ok=True)
        self.db = sqlite3.connect(db_path, check_same_thread=False)
        self.db.execute(
            "CREATE TABLE IF NOT EXISTS entries (key TEXT PRIMARY KEY, hash TEXT, model TEXT, vector BLOB)"
        )

    def refresh(self) -> None:
        """Rereads world/ and embeds what's new or changed; drops what's gone."""
        self.entries = load_entries(self.root)
        self.embedded_last_refresh = 0
        current = {e.key: e for e in self.entries}
        stored = {key: (h, model) for key, h, model in self.db.execute("SELECT key, hash, model FROM entries")}
        for key in stored.keys() - current.keys():
            self.db.execute("DELETE FROM entries WHERE key = ?", (key,))
        if self.embedder is None:
            self.db.commit()
            return
        stale = []
        for key, entry in current.items():
            digest = hashlib.sha256(entry.text().encode("utf-8")).hexdigest()
            if stored.get(key) != (digest, self.embedder.name):
                stale.append((key, digest, entry.text()))
        for start in range(0, len(stale), 32):
            batch = stale[start : start + 32]
            vectors = self.embedder.embed([text for _, _, text in batch])
            for (key, digest, _), vector in zip(batch, vectors):
                self.db.execute(
                    "INSERT OR REPLACE INTO entries (key, hash, model, vector) VALUES (?, ?, ?, ?)",
                    (key, digest, self.embedder.name, _pack(vector)),
                )
            self.embedded_last_refresh += len(batch)
        self.db.commit()

    def search(self, query: str, k: int = 5) -> tuple[list[SearchHit], str]:
        """The k entries most related to `query`, and which method ranked them."""
        try:
            self.refresh()
            if self.embedder is not None:
                return self._by_embedding(query, k), "embeddings"
        except Exception:  # an unreachable embedder must not break search
            self.entries = load_entries(self.root)
        return self._by_keywords(query, k), "keywords"

    def canon_for(self, query: str, k: int = 6, max_chars: int = 8000) -> str:
        """Grounding for a request: the k entries most related to it, plus every
        style entry (the tone guide always applies), as text for the prompt."""
        hits, _ = self.search(query, k)
        keys = [h.key for h in hits]
        keys += [e.key for e in self.entries if e.kind == "style" and e.key not in keys]
        by_key = {e.key: e for e in self.entries}
        parts, total = [], 0
        for key in keys:
            text = format_entry(by_key[key])
            if total + len(text) > max_chars:
                break
            parts.append(text)
            total += len(text)
        return "\n\n".join(parts)

    def _hit(self, entry: WorldEntry, score: float) -> SearchHit:
        return SearchHit(entry.key, entry.kind, entry.name, entry.summary, round(score, 4))

    def _by_embedding(self, query: str, k: int) -> list[SearchHit]:
        assert self.embedder is not None
        query_vector = self.embedder.embed([query])[0]
        vectors = {
            key: _unpack(blob)
            for key, blob in self.db.execute("SELECT key, vector FROM entries WHERE model = ?", (self.embedder.name,))
        }
        scored = [(self._hit(e, _cosine(query_vector, vectors[e.key]))) for e in self.entries if e.key in vectors]
        scored.sort(key=lambda hit: -hit.score)
        return scored[:k]

    def _by_keywords(self, query: str, k: int) -> list[SearchHit]:
        terms = set(_words(query))
        if not terms or not self.entries:
            return []
        # Fields weighted by how much a match there says: names and tags most.
        documents = []
        for entry in self.entries:
            counts: Counter[str] = Counter()
            for weight, text in ((3, entry.name), (2, " ".join(entry.tags)), (2, entry.summary), (1, entry.body)):
                for word in _words(text):
                    counts[word] += weight
            documents.append((entry, counts))
        n = len(documents)
        scored = []
        for entry, counts in documents:
            score = 0.0
            for term in terms:
                df = sum(1 for _, c in documents if term in c)
                if counts.get(term):
                    score += (1 + math.log(counts[term])) * math.log(1 + n / df)
            if score > 0:
                scored.append(self._hit(entry, score))
        scored.sort(key=lambda hit: -hit.score)
        return scored[:k]
