"""What the model calls in flight are doing, for the editor's progress strips: each request the
editor makes is one entry (begun by the middleware in main.py), and the providers add to it as
the model streams its answer. The editor reads them with GET /progress about once a second.

Tokens are counted as they arrive (about four characters a token) and replaced by the provider's
own count when a call ends."""

from __future__ import annotations

import contextvars
import itertools
import threading
import time
from dataclasses import dataclass, field

_lock = threading.Lock()
_ids = itertools.count(1)
_active: dict[int, "Entry"] = {}
_recent: list["Entry"] = []  # finished in the last few seconds: the strip shows them done, briefly
_current: contextvars.ContextVar[int | None] = contextvars.ContextVar("aige_progress", default=None)

RECENT_S = 4.0
CHARS_PER_TOKEN = 4.0


@dataclass
class Entry:
    id: int
    path: str
    label: str = ""
    phase: str = "waiting"  # waiting, thinking, answering, repairing, done, failed
    started: float = field(default_factory=time.monotonic)
    ended: float = 0.0
    calls: int = 0  # model calls made for this request so far
    thinking_chars: int = 0
    answer_chars: int = 0
    input_tokens: int = 0
    output_tokens: int = 0  # finished calls' real counts
    live_chars: int = 0  # the call streaming now, not yet counted in output_tokens
    repairing: bool = False
    step: str = ""  # what a request of several steps is doing now ("Painting the detail windows: 2 of 6")
    done: int = 0
    total: int = 0  # its steps (0: not counted)
    plan: list = field(default_factory=list)  # its steps known beforehand: [{id, label, pieces, state, made}]

    def view(self) -> dict:
        now = self.ended or time.monotonic()
        return {
            "step": self.step,
            "done": self.done,
            "total": self.total,
            "plan": self.plan,
            "id": self.id,
            "path": self.path,
            "label": self.label,
            "phase": self.phase,
            "seconds": round(now - self.started, 1),
            "calls": self.calls,
            "inputTokens": self.input_tokens,
            "outputTokens": self.output_tokens + int(self.live_chars / CHARS_PER_TOKEN),
            "thinkingTokens": int(self.thinking_chars / CHARS_PER_TOKEN),
            "answerTokens": int(self.answer_chars / CHARS_PER_TOKEN),
        }


def begin(path: str, label: str = "") -> contextvars.Token:
    entry = Entry(id=next(_ids), path=path, label=label)
    with _lock:
        _active[entry.id] = entry
    return _current.set(entry.id)


def end(token: contextvars.Token, ok: bool = True) -> None:
    entry_id = _current.get()
    _current.reset(token)
    with _lock:
        entry = _active.pop(entry_id, None) if entry_id is not None else None
        if entry is None:
            return
        entry.ended = time.monotonic()
        entry.phase = "done" if ok else "failed"
        entry.output_tokens += int(entry.live_chars / CHARS_PER_TOKEN)
        entry.live_chars = 0
        _recent.append(entry)
        if entry.calls == 0:
            _recent.remove(entry)  # no model call (a quick lookup): not worth showing


def _entry() -> Entry | None:
    entry_id = _current.get()
    return _active.get(entry_id) if entry_id is not None else None


def label(text: str | None) -> None:
    """A readable name for the request in flight ("② Outline", "Character: Mira")."""
    if not text:
        return
    with _lock:
        if (entry := _entry()) is not None:
            entry.label = str(text)[:80]


def reporter():
    """A request's steps, reported from any thread (a pool's workers don't see the request's context):
    report(done, total, step, plan); a value left None stays as it was."""
    entry_id = _current.get()

    def report(done: int | None = None, total: int | None = None, step: str | None = None, plan: list | None = None) -> None:
        with _lock:
            if entry_id is None or (entry := _active.get(entry_id)) is None:
                return
            if done is not None:
                entry.done = done
            if total is not None:
                entry.total = total
            if step is not None:
                entry.step = step
            if plan is not None:
                entry.plan = plan
            entry.calls = max(entry.calls, 1)  # shown on the strips, though its calls aren't streamed
    return report


def repairing(on: bool = True) -> None:
    """The step is asking again with its answer's problems listed."""
    with _lock:
        if (entry := _entry()) is not None:
            entry.repairing = on
            entry.phase = "repairing" if on else entry.phase


def call_started(thinking: bool = False) -> None:
    with _lock:
        if (entry := _entry()) is not None:
            entry.calls += 1
            entry.live_chars = 0
            entry.phase = "repairing" if entry.repairing else ("thinking" if thinking else "waiting")


def streamed(text: str, thinking: bool = False) -> None:
    """Text the model just wrote (its thinking, or its answer)."""
    if not text:
        return
    with _lock:
        if (entry := _entry()) is None:
            return
        entry.live_chars += len(text)
        if thinking:
            entry.thinking_chars += len(text)
            if not entry.repairing:
                entry.phase = "thinking"
        else:
            entry.answer_chars += len(text)
            if not entry.repairing:
                entry.phase = "answering"


def call_ended(input_tokens: int = 0, output_tokens: int = 0) -> None:
    """A model call finished: its real counts replace the estimate."""
    with _lock:
        if (entry := _entry()) is None:
            return
        entry.input_tokens += input_tokens
        entry.output_tokens += output_tokens or int(entry.live_chars / CHARS_PER_TOKEN)
        entry.live_chars = 0


def snapshot() -> dict:
    now = time.monotonic()
    with _lock:
        _recent[:] = [e for e in _recent if now - e.ended < RECENT_S]
        active = [e.view() for e in _active.values() if e.calls > 0 or now - e.started > 1.0]
        recent = [e.view() for e in _recent]
    return {"ok": True, "calls": active + recent, "active": len(active)}
