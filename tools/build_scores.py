"""Writes docs/build-scores.md from the build test's runs (docs/build-fixes.md §5).

Each run is a folder tests/quality/t2/<kind>-r<n>/ written by LiveBuildTest.APlannerPlaceBuild:
scores.json, the checks' answers (2-inspect-<round>.json, 3-inspect.json, 5/6/7-inspect.json), what
was asked of them (3-inspect-request.json, <tag>-inspect-request.json: the items of the place's layout,
its PLL), the images of each step and the place's own folder in project/places/<id>.

For every run: the scores of each step, every image, and per check a table of every item type (listed
in the layout or seen by the vision model) with how many the layout has, how many the model saw, and
its placement, fit and quality (score, verdict, why). Then each kind's runs side by side.

    python tools/build_scores.py [runs dir] [out file]
"""

from __future__ import annotations

import json
import re
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RUNS = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "tests" / "quality" / "t2"
OUT = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "docs" / "build-scores.md"
STEPS = [("layout", "② Layout"), ("plan", "③ Plan"), ("build", "⑤ Build"), ("rebuild", "⑤ Build after the redo"),
         ("reverted", "⑤ Build, redo undone")]
ASPECTS = ["layout", "placement", "fit", "quality"]


def load(path: Path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def rel(path: Path) -> str:
    return Path("..", path.relative_to(ROOT)).as_posix()


def cell(text: object, most: int = 140) -> str:
    s = " ".join(str(text).split()).replace("|", "/")
    return s if len(s) <= most else s[: most - 1] + "…"


def norm(kind: str) -> str:
    """An item type as a plain word: codes, counts and brackets off ("barrel (A)" -> "barrel")."""
    k = re.sub(r"\([^)]*\)", "", kind.lower())
    k = re.sub(r"[/,&+]| and ", " ", k)
    return " ".join(re.sub(r"[^a-z0-9 -]+", "", k).split())


def words(kind: str) -> set[str]:
    """Its words, singular ("doors" -> "door"): for matching what the model named to what was placed."""
    return {w[:-1] if w.endswith("s") and len(w) > 3 else w for w in norm(kind).replace("-", " ").split()}


def score_line(scores: dict) -> str:
    return " / ".join(f"{scores[a]:g}" if isinstance(scores.get(a), (int, float)) else "–" for a in ASPECTS)


def pll_items(request: dict | None) -> Counter:
    """What the layout placed: the items of a check's request (summed over its windows)."""
    counts: Counter = Counter()
    if not request:
        return counts
    groups = [request.get("expected", {}).get("items", [])] + [w.get("items", []) for w in request.get("windows", [])]
    for group in groups:
        for item in group or []:
            counts[norm(item.get("type", ""))] += int(item.get("count", 0))
    return counts


def solved_items(folder: Path) -> Counter:
    """What the layout placed, from the run's place itself (runs before requests were kept)."""
    counts: Counter = Counter()
    for solved in (folder / "project" / "places").glob("*/layout.solved.json"):
        data = load(solved) or {}
        for o in data.get("objects", []):
            kind = "building" if o.get("layer") == "building" else (o.get("slot") or "object")
            counts[norm(kind)] += 1
        break
    return counts


def item_table(answer: dict, request: dict | None, folder: Path | None = None) -> list[str]:
    pll = pll_items(request) or (solved_items(folder) if folder else Counter())
    seen: Counter = Counter()
    for e in answer.get("identified", []):
        try:
            seen[norm(e.get("type", ""))] += int(e.get("count", 0) or 0)
        except (TypeError, ValueError):
            seen[norm(e.get("type", ""))] += 0
    entries = {aspect: [e for e in answer.get(aspect, []) if isinstance(e, dict)] for aspect in ("placement", "fit", "quality")}
    kinds = sorted(set(pll) | set(seen))
    if not kinds:
        kinds = sorted({norm(e.get("type", "")) for es in entries.values() for e in es})
    verdicts: dict[str, dict[str, dict]] = {}
    for kind in kinds:
        mine = words(kind)
        for aspect, es in entries.items():
            hits = [e for e in es if mine & words(e.get("type", ""))]
            if not hits:
                continue
            # Several windows (or a grouped name): the worst verdict and lowest score, every reason.
            scores = [e.get("score") for e in hits if isinstance(e.get("score"), (int, float))]
            bad = [e for e in hits if not e.get("ok")]
            verdicts.setdefault(kind, {})[aspect] = {"ok": not bad, "score": min(scores) if scores else None,
                                                     "why": (bad or hits)[0].get("why", "")}

    def matched(kind: str, table: dict) -> object:
        if kind in table:
            return table[kind]
        for other in table:  # "light" listed, "wall torch light" seen
            if kind and other and (kind in other.split() or other in kind.split()):
                return table[other]
        return None

    def verdict(v: dict | None) -> str:
        if not v:
            return "–"
        mark = "ok" if v.get("ok") else "**no**"
        score = v.get("score")
        return f"{score}/10 {mark}" if isinstance(score, (int, float)) else mark

    rows = ["| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |",
            "|---|---|---|---|---|---|---|"]
    for kind in kinds:
        v = verdicts.get(kind) or {}
        why = next((e.get("why", "") for a in ("placement", "fit", "quality") if (e := v.get(a)) and not e.get("ok")), "")
        if not why:
            why = next((e.get("why", "") for a in ("placement", "fit", "quality") if (e := v.get(a))), "")
        n_pll = pll.get(kind) or sum(n for k, n in pll.items() if words(k) & words(kind))
        n_seen = seen.get(kind) or sum(n for k, n in seen.items() if words(k) & words(kind))
        rows.append(f"| {cell(kind, 40)} | {n_pll or '–'} | {n_seen or '–'} | {verdict(v.get('placement'))} | "
                    f"{verdict(v.get('fit'))} | {verdict(v.get('quality'))} | {cell(why)} |")
    return rows


def problems(answer: dict) -> list[str]:
    out = []
    for p in answer.get("problems", [])[:12]:
        sev = "error" if p.get("severity") == "error" else "warning"
        out.append(f"- *{sev}* — {cell(p.get('target', ''), 40)}: {cell(p.get('issue', ''), 200)}")
    return out


def run_section(folder: Path) -> tuple[list[str], dict]:
    scores = load(folder / "scores.json") or {}
    lines = [f"### {folder.name}", ""]
    lines.append(f"Place: `{scores.get('place', '?')}` ({scores.get('buildType', '?')}). "
                 f"Art: {scores.get('assets', {}).get('pieces', '–')} pieces, "
                 f"{scores.get('assets', {}).get('placeholders', '–')} placeholders, "
                 f"{scores.get('assets', {}).get('refusedFound', '–')} found pieces refused by the check.")
    lines.append("")
    lines += ["| Step | layout / placement / fit / quality | errors | warnings |", "|---|---|---|---|"]
    for key, label in STEPS:
        s = scores.get(key)
        if isinstance(s, dict) and s:
            lines.append(f"| {label} | {score_line(s.get('scores', {}))} | {s.get('errors', '–')} | {s.get('warnings', '–')} |")
    rounds = scores.get("layoutRounds") or []
    if rounds:
        lines.append("")
        lines.append("Layout check, round by round: " + "; ".join(
            f"{r.get('round')}: {score_line(r.get('scores', {}))} ({r.get('errors', 0)} errors)" for r in rounds))
    lines.append("")
    # The images, step by step.
    images = sorted(p for p in folder.glob("*.png"))
    if images:
        lines.append("Images: " + " · ".join(f"[{p.stem}]({rel(p)})" for p in images))
        lines.append("")
    main = folder / "5-scene.png"
    if main.exists():
        lines.append(f"![{folder.name} build]({rel(main)})")
        lines.append("")
    # Each check: its summary, its problems, its items.
    checks = [(f"2-inspect-{r.get('round')}.json", None, f"② Layout check, round {r.get('round')}") for r in rounds]
    checks += [("3-inspect.json", "3-inspect-request.json", "③ Plan check")]
    for tag, label in (("5", "⑤ Build check"), ("6", "⑤ Build check after the redo"), ("7", "⑤ Build check, redo undone")):
        checks.append((f"{tag}-inspect.json", f"{tag}-inspect-request.json", label))
    for answer_file, request_file, label in checks:
        answer = load(folder / answer_file)
        if not isinstance(answer, dict) or not answer:
            continue
        request = load(folder / request_file) if request_file else None
        lines.append(f"#### {label}")
        lines.append("")
        if not answer.get("ok", True):
            lines.append(f"The check failed: {cell(answer.get('errors', ''), 300)}")
            lines.append("")
            continue
        lines.append(f"Scores (layout / placement / fit / quality): {score_line(answer.get('scores', {}))}. "
                     f"{cell(answer.get('summary', ''), 600)}")
        lines.append("")
        lines += problems(answer)
        lines.append("")
        if answer.get("identified") or answer.get("placement"):
            # A layout check judges zones (its own list); the plan and build checks the layout's items.
            lines += item_table(answer, request, folder if request_file else None)
            lines.append("")
    return lines, scores


def main() -> None:
    runs = sorted((p for p in RUNS.iterdir() if p.is_dir() and re.match(r".+-r\d+$", p.name)),
                  key=lambda p: (p.name.rsplit("-r", 1)[0], int(p.name.rsplit("-r", 1)[1])))
    by_kind: dict[str, list[tuple[str, dict]]] = {}
    body: list[str] = []
    for folder in runs:
        section, scores = run_section(folder)
        if not scores:
            continue
        by_kind.setdefault(folder.name.rsplit("-r", 1)[0], []).append((folder.name, scores))
        body += section
    out = ["# Build scores", "",
           "Written by `tools/build_scores.py` from the build test's runs (`tests/quality/t2/`); how they're made and",
           "what each fix was for: `docs/build-fixes.md`. Every score is the vision model's (Mistral Large 4), out of 10,",
           "judged as a strict art director (10: a shipped, hand-crafted game). Its scores vary by a point or two between",
           "looks at the same image; read trends across runs, not single numbers.", "",
           "## Run against run", ""]
    for kind, rows in by_kind.items():
        out.append(f"### {kind}")
        out.append("")
        out.append("| Run | " + " | ".join(label for _, label in STEPS) + " |")
        out.append("|---|" + "---|" * len(STEPS))
        for name, scores in rows:
            cells = []
            for key, _ in STEPS:
                s = scores.get(key)
                cells.append(score_line(s.get("scores", {})) + f" ({s.get('errors', '–')} err)" if isinstance(s, dict) and s else "–")
            out.append(f"| {name} | " + " | ".join(cells) + " |")
        out.append("")
    out += ["## Each run", ""] + body
    OUT.write_text("\n".join(out) + "\n", encoding="utf-8")
    print(f"{OUT}: {len(runs)} runs")


if __name__ == "__main__":
    main()
