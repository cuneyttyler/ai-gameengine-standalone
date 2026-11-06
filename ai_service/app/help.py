"""The editor's knowledge base (data/help/README.md): what the AI panel answers questions about the editor from.

Its pieces are each `## ` section of the Markdown files in data/help/ and, from the engine's own schema, one piece per
component, record type and settings section (so those never fall behind the code). A request is answered from the
pieces most related to it: ranked by embeddings (cached in ai_service/.cache/help_index.sqlite, one per piece, only
new or changed ones embedded) blended with keyword scores (BM25), or by keywords alone without an embedder.
"""

from __future__ import annotations

import hashlib
import math
import re
import sqlite3
import threading
from dataclasses import dataclass
from pathlib import Path

from .models import EngineSchema, RecordFieldSpec
from .world import Embedder, _cosine, _pack, _unpack

HELP_DIR = Path(__file__).resolve().parents[2] / "data" / "help"
CACHE = Path(__file__).resolve().parents[1] / ".cache" / "help_index.sqlite"

_WORD = re.compile(r"[a-z0-9]+")
# Words too common to tell pieces apart.
_STOP = set("a an and are as at be by can do does for from how i in is it its me my of on or the this to what when "
            "where which who why with you your".split())


def _words(text: str) -> list[str]:
    return [w for w in _WORD.findall(text.lower()) if w not in _STOP]


@dataclass
class HelpPiece:
    id: str      # "shell/menus.md#file-menu", "component/Sprite"
    title: str   # "Menus › File menu"
    text: str    # the whole piece, its title first
    source: str  # the file, or "engine schema"

    def digest(self) -> str:
        return hashlib.sha256(self.text.encode("utf-8")).hexdigest()


def _slug(text: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")


def markdown_pieces(folder: Path = HELP_DIR) -> list[HelpPiece]:
    """Each `## ` section of each file (its file's title before its own); a file without sections is one piece."""
    pieces: list[HelpPiece] = []
    if not folder.is_dir():
        return pieces
    for file in sorted(folder.rglob("*.md")):
        if file.name.lower() == "readme.md":
            continue
        rel = file.relative_to(folder).as_posix()
        text = file.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
        title = next((line[2:].strip() for line in text.splitlines() if line.startswith("# ")), file.stem)
        parts = re.split(r"(?m)^## ", text)
        intro = re.sub(r"(?m)^# .*$", "", parts[0]).strip()
        if len(parts) == 1:
            pieces.append(HelpPiece(rel, title, f"{title}\n{intro}", rel))
            continue
        for part in parts[1:]:
            heading, _, body = part.partition("\n")
            heading = heading.strip()
            body = body.strip()
            if not body:
                continue
            full = f"{title} › {heading}"
            pieces.append(HelpPiece(f"{rel}#{_slug(heading)}", full, f"{full}\n{body}", rel))
    return pieces


def _record_fields(fields: list[RecordFieldSpec], depth: int = 0) -> list[str]:
    lines = []
    for f in fields:
        kind = "list of " + f.type if f.list else f.type
        if f.recordType:
            kind += f" (a {f.recordType} record)"
        extra = []
        if f.options:
            extra.append("one of " + ", ".join(o or "(empty)" for o in f.options))
        if f.default not in (None, "", []):
            extra.append(f"default {f.default}")
        if f.min is not None:
            extra.append(f"at least {f.min:g}")
        if f.max is not None:
            extra.append(f"at most {f.max:g}")
        if f.choices:
            extra.append(f"picked from {f.choices}")
        if f.when:
            extra.append(f"only when {f.when.get('field')} is " + ", ".join(f.when.get("values", [])))
        lines.append("  " * depth + f"- {f.name} ({kind}): {f.description}" + (f" [{'; '.join(extra)}]" if extra else ""))
        if f.fields:
            lines += _record_fields(f.fields, depth + 1)
    return lines


def schema_pieces(schema: EngineSchema | None) -> list[HelpPiece]:
    """A piece per component (Inspector ▸ Add Component), record type (Project ▸ Records) and settings section
    (Project Settings), from the engine's own descriptions."""
    if schema is None:
        return []
    pieces = []
    for c in schema.components:
        lines = [f"Component {c.name} (Inspector ▸ Add Component ▸ {c.name})", c.description]
        if c.dependencies:
            lines.append("Needs: " + ", ".join(c.dependencies))
        if not c.userEditable:
            lines.append("Set by the engine, not edited by hand.")
        lines.append("Fields:")
        for f in c.fields:
            extra = []
            if f.options:
                extra.append("one of " + ", ".join(o or "(empty)" for o in f.options))
            if f.min is not None:
                extra.append(f"at least {f.min:g}")
            if f.max is not None:
                extra.append(f"at most {f.max:g}")
            if f.scalable:
                extra.append(f"can scale with difficulty ({f.scalable})")
            lines.append(f"- {f.name} ({f.type}): {f.description}" + (f" [{'; '.join(extra)}]" if extra else ""))
        pieces.append(HelpPiece(f"component/{c.name}", f"Component {c.name}", "\n".join(lines), "engine schema"))
    for r in schema.records:
        lines = [f"Record type {r.name} (Project ▸ Records ▸ {r.name}; files records/{r.name}/<id>.json)", r.description,
                 "Fields:"] + _record_fields(r.fields)
        pieces.append(HelpPiece(f"record/{r.name}", f"Record type {r.name}", "\n".join(lines), "engine schema"))
    for s in schema.settings:
        lines = [f"Project Settings ▸ {s.name} (File ▸ Project Settings; saved in settings.aige)", s.description, "Settings:"]
        for f in s.fields:
            opts = f" [one of {', '.join(o or '(empty)' for o in f.options)}]" if f.options else ""
            lines.append(f"- {f.name} ({f.type}): {f.description}{opts}")
        pieces.append(HelpPiece(f"settings/{s.name}", f"Project Settings ▸ {s.name}", "\n".join(lines), "engine schema"))
    return pieces


class HelpIndex:
    """The knowledge base's pieces and their embeddings; search blends both rankings."""

    def __init__(self, embedder: Embedder | None, folder: Path = HELP_DIR, cache: Path = CACHE):
        self.embedder = embedder
        self.folder = folder
        self.schema: EngineSchema | None = None
        self.pieces: list[HelpPiece] = []
        self._stamp: tuple = ()
        self._lock = threading.Lock()
        cache.parent.mkdir(parents=True, exist_ok=True)
        self.db = sqlite3.connect(cache, check_same_thread=False)
        self.db.execute("CREATE TABLE IF NOT EXISTS pieces (hash TEXT, model TEXT, vector BLOB, PRIMARY KEY (hash, model))")
        self._idf: dict[str, float] = {}
        self._docs: list[list[str]] = []
        self._avg = 1.0

    def set_schema(self, schema: EngineSchema | None) -> None:
        with self._lock:
            self.schema = schema
            self._stamp = ()

    def refresh(self) -> None:
        """Reread the files when they changed (or the schema did), and rebuild the keyword index."""
        files = sorted(self.folder.rglob("*.md")) if self.folder.is_dir() else []
        stamp = (id(self.schema), tuple((str(f), f.stat().st_mtime_ns) for f in files))
        if stamp == self._stamp:
            return
        self.pieces = markdown_pieces(self.folder) + schema_pieces(self.schema)
        # Titles count twice: a piece named for what's asked is the likeliest answer.
        self._docs = [_words(p.title) * 2 + _words(p.text) for p in self.pieces]
        n = max(1, len(self._docs))
        df: dict[str, int] = {}
        for doc in self._docs:
            for w in set(doc):
                df[w] = df.get(w, 0) + 1
        self._idf = {w: math.log(1 + (n - d + 0.5) / (d + 0.5)) for w, d in df.items()}
        self._avg = sum(len(d) for d in self._docs) / n
        self._stamp = stamp

    def _embed_missing(self) -> dict[str, list[float]]:
        """Every piece's vector, embedding the ones not cached yet (32 at a time)."""
        assert self.embedder is not None
        model = self.embedder.name
        have = {h: _unpack(v) for h, v in self.db.execute("SELECT hash, vector FROM pieces WHERE model = ?", (model,))}
        missing = [(p.digest(), p.text[:6000]) for p in self.pieces if p.digest() not in have]
        seen: set[str] = set()
        missing = [(h, t) for h, t in missing if not (h in seen or seen.add(h))]
        for start in range(0, len(missing), 32):
            batch = missing[start:start + 32]
            vectors = self.embedder.embed([t for _, t in batch])
            for (h, _), v in zip(batch, vectors):
                self.db.execute("INSERT OR REPLACE INTO pieces (hash, model, vector) VALUES (?, ?, ?)", (h, model, _pack(v)))
                have[h] = v
        self.db.commit()
        return have

    def _bm25(self, query: str) -> list[float]:
        terms = _words(query)
        scores = []
        for doc in self._docs:
            counts: dict[str, int] = {}
            for w in doc:
                counts[w] = counts.get(w, 0) + 1
            s = 0.0
            for t in terms:
                f = counts.get(t, 0)
                if f:
                    s += self._idf.get(t, 0.0) * f * 2.2 / (f + 1.2 * (0.25 + 0.75 * len(doc) / self._avg))
            scores.append(s)
        return scores

    def search(self, query: str, k: int = 6) -> tuple[list[tuple[HelpPiece, float]], str]:
        """The k pieces most related to `query` with their scores, and how they were ranked."""
        with self._lock:
            self.refresh()
            if not self.pieces or not query.strip():
                return [], "none"
            keyword = self._bm25(query)
            top = max(keyword) or 1.0
            keyword = [s / top for s in keyword]
            method = "keywords"
            blended = keyword
            if self.embedder is not None:
                try:
                    vectors = self._embed_missing()
                    q = self.embedder.embed([query])[0]
                    semantic = [_cosine(q, vectors[p.digest()]) if p.digest() in vectors else 0.0 for p in self.pieces]
                    lo, hi = min(semantic), max(semantic)
                    semantic = [(s - lo) / (hi - lo) if hi > lo else 0.0 for s in semantic]
                    blended = [0.65 * s + 0.35 * w for s, w in zip(semantic, keyword)]
                    method = "embeddings+keywords"
                except Exception:  # an unreachable embedder: keywords alone
                    pass
            ranked = sorted(zip(self.pieces, blended), key=lambda pair: -pair[1])
            return [(p, round(s, 4)) for p, s in ranked[:k] if s > 0], method

    def help_for(self, query: str, k: int = 6, max_chars: int = 9000) -> str:
        """The pieces most related to `query`, as text for a prompt, within a size budget."""
        hits, _ = self.search(query, k)
        out, total = [], 0
        for piece, _ in hits:
            block = f"[{piece.id}]\n{piece.text}"
            if total + len(block) > max_chars:
                break
            out.append(block)
            total += len(block)
        return "\n\n".join(out)


_QUESTION = re.compile(r"\?|^\s*(how|what|where|why|when|which|who|can|could|is|are|does|do|did|should|would|"
                       r"explain|tell me|help|show me|describe|what's|whats|is there|i can't|i cannot)\b", re.I)


def looks_like_question(text: str) -> bool:
    """A question about the editor more likely than a change (the knowledge base is added to such requests)."""
    return bool(_QUESTION.search(text.strip()))
