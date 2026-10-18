"""Building places with AI (development_plan.md 7.4, docs/place-interaction.md
Part B). Every call has the same shape:

    inputs -> THINK (free text, private) -> ANSWER (strict schema) -> validate -> (repair once)

The editor assembles the inputs (from the ontology) and validates the answers
in C++ (engine/places); this module makes the calls. Thinking is produced one
of three ways (B3): `native` (a reasoning model returns it apart from its
answer), `two-pass` (a free-text call, then the answer given the thinking: the
default) or `one-pass` (one JSON call whose first field is "thinking").

Interpret (step 1) drafts several Briefs, each from a different reading of the
request, so that where they disagree is where the request is open."""

from __future__ import annotations

import json
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field

from .models import Usage
from .providers import Provider, ProviderError

MODES = ("auto", "native", "two-pass", "one-pass")

# The readings of a request, one per draft: their disagreements show the user
# what the request leaves open.
READINGS = [
    ("literal", "Read the request literally: decide only what it says or clearly implies; leave everything else at "
                "the build type's defaults and say so in assumptions."),
    ("typical", "Read the request as an experienced game level designer would: what a place like this typically "
                "has, sized to what was asked."),
    ("generous", "Read the request generously: a rich, lively version of what was asked (more components, livelier "
                 "densities), never contradicting it."),
]

THINK_INSTRUCTIONS = """Think it through in plain text before anything is written down. No JSON yet.
Work through: what the request asks for and implies; which build type it is; what size, with numbers (use the
scale table: a size word is never left as a word); which components the place needs and how much of each; the
style; what you must assume because the request doesn't say. Then check yourself:
- does the size class match the numbers?
- is every component one the build type allows?
- does every field use the values its type allows?"""

BRIEF_SHAPE = """The Brief (schema aige.brief/1), as JSON:
{
  "brief": {
    "schema": "aige.brief/1",
    "buildType": "<one of the build types>",
    "name": "<a short place name>",
    "scale": {"class": "<a class of the build type's scale table>", "buildings": {"count": [low, high]}},
    "style": {"era": "...", "culture": "...", "biome": "<biome>", "condition": "<condition>", "tags": ["..."]},
    "shape": {"layout": "<a layout>", "description": "<one sentence, optional>"},
    "walled": "<none|palisade|stone>",
    "features": [{"module": "<e.g. river>"}],
    "components": {
      "<component module id>": {"enabled": true, "count": [low, high] or n, "density": "<none|rare|some|common|abundant>",
                                "assets": {"source": "auto"}, "params": {"<field>": <value of its type>}}
    }
  },
  "confidence": {"<field path, e.g. scale.class or components.garden.density>": <0 to 1>},
  "assumptions": ["<what you assumed because the request didn't say>"],
  "questions": ["<what the user should decide, if anything>"],
  "rationale": "<one or two sentences: why this reading>"
}
Only components listed for the build type. A disabled component is {"enabled": false} and nothing else.
"params" only uses the component's fields, with values of their type. Give confidence below 0.6 for anything
you guessed."""


@dataclass
class StepResult:
    answer: dict | None
    thinking: str = ""
    mode: str = ""
    usage: Usage = field(default_factory=Usage)
    errors: list[str] = field(default_factory=list)


def resolve_mode(provider: Provider, mode: str) -> str:
    if mode not in MODES or mode == "auto":
        if getattr(provider, "reasoning", False):
            return "native"
        return "two-pass" if hasattr(provider, "complete_text") else "one-pass"
    if mode == "two-pass" and not hasattr(provider, "complete_text"):
        return "one-pass"
    return mode


def _parse(text: str) -> tuple[dict | None, str]:
    try:
        value = json.loads(text)
    except json.JSONDecodeError:
        # Some models wrap JSON in prose or a fence: take the outermost object.
        start, end = text.find("{"), text.rfind("}")
        if start < 0 or end <= start:
            return None, "the answer wasn't JSON"
        try:
            value = json.loads(text[start:end + 1])
        except json.JSONDecodeError:
            return None, "the answer wasn't JSON"
    return (value, "") if isinstance(value, dict) else (None, "the answer wasn't a JSON object")


def think_then_answer(provider: Provider, task: str, inputs: str, answer_shape: str, mode: str = "auto",
                      think: bool = True) -> StepResult:
    """One step (B1): think in free text, then answer in `answer_shape`."""
    mode = resolve_mode(provider, mode) if think else "answer-only"
    result = StepResult(answer=None, mode=mode)
    answer_system = f"{task}\n\n{answer_shape}\n\nAnswer with the JSON object only."
    try:
        if mode == "two-pass":
            thought = provider.complete_text([  # type: ignore[attr-defined]
                {"role": "system", "content": f"{task}\n\n{THINK_INSTRUCTIONS}"},
                {"role": "user", "content": inputs}])
            result.thinking = thought.text.strip()
            result.usage.add(thought.usage)
            reply = provider.complete([
                {"role": "system", "content": answer_system},
                {"role": "user", "content": f"{inputs}\n\nYour reasoning so far:\n{result.thinking}\n\n"
                                            "Now write the answer, following your reasoning."}])
        elif mode == "one-pass":
            reply = provider.complete([
                {"role": "system", "content": f"{task}\n\n{THINK_INSTRUCTIONS}\n\n{answer_shape}\n\n"
                                              "Answer with one JSON object whose FIRST key is \"thinking\" (your "
                                              "reasoning, as a string), followed by the answer's keys."},
                {"role": "user", "content": inputs}])
        else:  # native, or an answer with no thinking (a repair)
            reply = provider.complete([
                {"role": "system", "content": answer_system},
                {"role": "user", "content": inputs}])
            result.thinking = reply.thinking
    except ProviderError as error:
        result.errors.append(str(error))
        return result
    result.usage.add(reply.usage)
    answer, problem = _parse(reply.text)
    if answer is None:
        result.errors.append(problem)
        return result
    if mode == "one-pass":
        result.thinking = str(answer.pop("thinking", ""))
    result.answer = answer
    return result


# --- Interpret ------------------------------------------------------------------------

def _clean_draft(answer: dict, build_types: list[str]) -> tuple[dict | None, list[str]]:
    """The draft's shape, cleaned: the C++ validator checks its contents."""
    brief = answer.get("brief")
    if not isinstance(brief, dict):
        return None, ["the answer has no brief"]
    brief["schema"] = "aige.brief/1"
    if build_types and brief.get("buildType") not in build_types:
        if len(build_types) == 1:
            brief["buildType"] = build_types[0]
        else:
            return None, [f"unknown build type {brief.get('buildType')!r}"]
    if not isinstance(brief.get("components"), dict):
        brief["components"] = {}
    confidence = {}
    for path, value in (answer.get("confidence") or {}).items() if isinstance(answer.get("confidence"), dict) else []:
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            confidence[str(path)] = round(min(max(float(value), 0.0), 1.0), 2)
    strings = lambda key: [" ".join(str(s).split())[:300] for s in answer.get(key) or [] if str(s).strip()][:12] \
        if isinstance(answer.get(key), list) else []
    return {"brief": brief, "confidence": confidence, "assumptions": strings("assumptions"),
            "questions": strings("questions"), "rationale": " ".join(str(answer.get("rationale", "")).split())[:400]}, []


def interpret_inputs(query: str, context: dict, reading: str) -> str:
    return (f"The request: {query.strip()}\n\nReading: {reading}\n\n"
            f"What can be built (build types, their components with fields, scale tables, vocabularies):\n"
            f"{json.dumps(context, ensure_ascii=False)}")


def interpret(provider: Provider, query: str, context: dict, drafts: int = 3, mode: str = "auto",
              workers: int = 3) -> dict:
    """Drafts up to len(READINGS) Briefs for `query`, in parallel."""
    build_types = [b.get("id") for b in context.get("buildTypes", []) if isinstance(b, dict) and b.get("id")]
    task = ("You turn a request for a game place (a village, a town, a castle...) into a Brief: the confirmed "
            "description of what to build, in the build type's own terms.")
    readings = READINGS[:max(1, min(drafts, len(READINGS)))]

    def one(reading: tuple[str, str]) -> dict:
        step = think_then_answer(provider, task, interpret_inputs(query, context, reading[1]), BRIEF_SHAPE, mode)
        draft = {"reading": reading[0], "mode": step.mode, "thinking": step.thinking,
                 "usage": step.usage.model_dump(), "errors": list(step.errors)}
        if step.answer is not None:
            cleaned, problems = _clean_draft(step.answer, build_types)
            draft["errors"] += problems
            if cleaned:
                draft.update(cleaned)
        return draft

    with ThreadPoolExecutor(max_workers=max(1, workers)) as pool:
        results = list(pool.map(one, readings))
    usage = Usage()
    for r in results:
        usage.add(Usage(**r["usage"]))
    good = [r for r in results if "brief" in r]
    errors = [f"{r['reading']}: {e}" for r in results for e in r["errors"]]
    return {"ok": bool(good), "drafts": results, "errors": errors if not good else [], "warnings": errors if good else [],
            "usage": usage.model_dump()}


def repair_brief(provider: Provider, query: str, context: dict, brief: dict, problems: list[dict]) -> dict:
    """The one repair round (B1.5): the draft and its exact violations, back once."""
    build_types = [b.get("id") for b in context.get("buildTypes", []) if isinstance(b, dict) and b.get("id")]
    listed = "\n".join(f"- {p.get('where', '?')}: {p.get('message', '')}" for p in problems if isinstance(p, dict))
    inputs = (f"The request: {query.strip()}\n\nYour Brief:\n{json.dumps(brief, ensure_ascii=False)}\n\n"
              f"It breaks these rules:\n{listed}\n\nFix exactly these, change nothing else.\n\n"
              f"What can be built:\n{json.dumps(context, ensure_ascii=False)}")
    task = "You correct a Brief for a game place so that it follows its build type's rules."
    step = think_then_answer(provider, task, inputs, BRIEF_SHAPE, think=False)
    out = {"ok": False, "errors": list(step.errors), "usage": step.usage.model_dump()}
    if step.answer is not None:
        cleaned, problems_left = _clean_draft(step.answer, build_types)
        out["errors"] += problems_left
        if cleaned:
            out.update(cleaned)
            out["ok"] = True
    return out
