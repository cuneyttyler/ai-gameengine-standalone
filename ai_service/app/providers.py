"""Model providers. The service only needs "messages in, JSON text out"; each
adapter hides one vendor's SDK. Cloud first (Mistral); local models later
(docs/project/technical.md §10)."""

from __future__ import annotations

import time

from dataclasses import dataclass, field
from typing import Callable, Protocol

from . import progress
from .models import Usage


@dataclass
class Completion:
    text: str
    usage: Usage = field(default_factory=Usage)
    model: str = ""
    thinking: str = ""  # a reasoning model's thinking, returned apart from the answer (native mode)


class Provider(Protocol):
    name: str
    model: str

    def complete(self, messages: list[dict]) -> Completion: ...

    # Optional (place building, 7.4): `complete_text(messages)` answers in free
    # text, not JSON (the thinking pass); `reasoning = True` marks a model that
    # returns its thinking apart from its answer.


class ProviderError(Exception):
    """The provider couldn't answer (network, auth, quota). Shown to the user as is."""


class MistralProvider:
    name = "mistral"

    def __init__(self, api_key: str, model: str = "mistral-large-latest", timeout_s: float = 180.0):
        from mistralai.client import Mistral  # imported lazily so tests don't need the SDK configured

        self.model = model
        self._client = Mistral(api_key=api_key)
        self._timeout_ms = int(timeout_s * 1000)
        # Magistral models think apart from their answer (place-interaction.md B3, native mode).
        self.reasoning = "magistral" in model

    # What a call may write at most: a model that runs on (repeating itself) is cut off
    # instead of taking minutes. The answers are JSON a few thousand tokens long at most.
    MAX_ANSWER_TOKENS = 6000
    MAX_THINKING_TOKENS = 3000

    def complete(self, messages: list[dict]) -> Completion:
        return self._call(messages, json_mode=True, max_tokens=self.MAX_ANSWER_TOKENS)

    def complete_text(self, messages: list[dict]) -> Completion:
        """Free text: the thinking pass of a two-pass call."""
        return self._call(messages, json_mode=False, max_tokens=self.MAX_THINKING_TOKENS)

    # Mistral at capacity ("Service unavailable", 503; too many requests, 429): wait and ask
    # again, then ask a less loaded model, before saying there's no answer (2026-10-21).
    BACKOFF_S = (2.0, 5.0, 10.0)
    FALLBACK_MODELS = {"mistral-large-latest": "mistral-medium-latest", "magistral-medium-latest": "mistral-medium-latest"}

    @staticmethod
    def _busy(error: Exception) -> bool:
        text = str(error)
        return any(s in text for s in ("Status 503", "Status 429", "Service unavailable", "status_code=503", "status_code=429",
                                       "rate limit", "capacity"))

    def _call(self, messages: list[dict], json_mode: bool, max_tokens: int = 6000) -> Completion:
        options = {"response_format": {"type": "json_object"}} if json_mode else {}  # the prompt spells out the shape
        models = [self.model] + ([self.FALLBACK_MODELS[self.model]] if self.model in self.FALLBACK_MODELS else [])
        last: Exception | None = None
        result: Completion | None = None
        for model in models:
            for wait in (0.0,) + self.BACKOFF_S:
                if wait:
                    time.sleep(wait)
                try:
                    result = self._stream(model, messages, max_tokens, options, thinking_pass=not json_mode)
                    break
                except Exception as error:  # the SDK raises several types; all mean "no answer"
                    last = error
                    if not self._busy(error):
                        progress.call_ended()
                        raise ProviderError(f"Mistral request failed: {error}") from error
            if result is not None:
                break
        if result is None:
            progress.call_ended()
            raise ProviderError(f"Mistral request failed: busy after retries and {', '.join(models)}: {last}") from last
        progress.call_ended(result.usage.inputTokens, result.usage.outputTokens)
        return result

    def _stream(self, model: str, messages: list[dict], max_tokens: int, options: dict, thinking_pass: bool) -> Completion:
        """One streamed call: the text, as it comes, goes to the progress strips (app/progress.py)."""
        progress.call_started(thinking=thinking_pass)
        parts: list[str] = []
        thoughts: list[str] = []
        usage = Usage(requests=1)
        used = model
        events = self._client.chat.stream(model=model, messages=messages, temperature=0.2, max_tokens=max_tokens,
                                          timeout_ms=self._timeout_ms, **options)
        for event in events:
            chunk = getattr(event, "data", event)
            used = getattr(chunk, "model", None) or used
            if getattr(chunk, "usage", None):
                usage = Usage(inputTokens=getattr(chunk.usage, "prompt_tokens", 0) or 0,
                              outputTokens=getattr(chunk.usage, "completion_tokens", 0) or 0, requests=1)
            for choice in getattr(chunk, "choices", None) or []:
                content = getattr(choice.delta, "content", None)
                if isinstance(content, str):
                    parts.append(content)
                    progress.streamed(content, thinking=thinking_pass)
                    continue
                # Reasoning models answer in chunks: thinking chunks, then text.
                for piece in content or []:
                    if getattr(piece, "type", "") == "thinking":
                        for inner in getattr(piece, "thinking", []) or []:
                            text = getattr(inner, "text", "") or ""
                            thoughts.append(text)
                            progress.streamed(text, thinking=True)
                    elif hasattr(piece, "text"):
                        parts.append(piece.text or "")
                        progress.streamed(piece.text or "", thinking=thinking_pass)
        return Completion(text="".join(parts), usage=usage, model=used, thinking="".join(thoughts))


class ClaudeProvider:
    """Anthropic's Claude (Claude Opus 5.5 by default). It thinks before it
    answers (adaptive thinking), so steps run in native mode: the thinking comes
    back apart from the answer (a summary of it). Sees images, so it also
    looks at maps and art."""

    name = "claude"
    reasoning = True

    def __init__(self, api_key: str | None = None, model: str = "claude-opus-5-5", effort: str = "high",
                 timeout_s: float = 600.0):
        import anthropic  # imported lazily so tests don't need the SDK configured

        self.model = model
        self.effort = effort
        self._anthropic = anthropic
        self._client = anthropic.Anthropic(api_key=api_key, timeout=timeout_s) if api_key else anthropic.Anthropic(timeout=timeout_s)

    def complete(self, messages: list[dict]) -> Completion:
        return self._call(messages)

    def complete_text(self, messages: list[dict]) -> Completion:
        return self._call(messages)

    def at(self, effort: str) -> "ClaudeProvider":
        """The same model at another effort (medium for a quick change in the AI panel)."""
        other = object.__new__(ClaudeProvider)
        other.__dict__.update(self.__dict__)
        other.effort = effort
        return other

    @staticmethod
    def _convert(messages: list[dict]) -> tuple[str, list[dict]]:
        """The service's chat messages (OpenAI-style, as Mistral takes them) as
        Claude's: system text apart, images as base64 blocks."""
        system, out = [], []
        for message in messages:
            role, content = message.get("role", "user"), message.get("content", "")
            if role == "system":
                system.append(content if isinstance(content, str) else
                              "\n".join(c.get("text", "") for c in content if isinstance(c, dict)))
                continue
            if isinstance(content, list):
                blocks = []
                for chunk in content:
                    if chunk.get("type") == "image_url":
                        url = chunk.get("image_url", "")
                        url = url.get("url", "") if isinstance(url, dict) else url
                        header, _, data = url.partition(",")
                        media = header.removeprefix("data:").split(";")[0] or "image/png"
                        blocks.append({"type": "image", "source": {"type": "base64", "media_type": media, "data": data}})
                    elif chunk.get("type") == "text":
                        blocks.append({"type": "text", "text": chunk.get("text", "")})
                content = blocks
            out.append({"role": "assistant" if role == "assistant" else "user", "content": content})
        return "\n\n".join(system), out

    def _call(self, messages: list[dict]) -> Completion:
        system, converted = self._convert(messages)
        progress.call_started()
        try:
            # Streamed (a long plan takes a while); a declined request is retried
            # on another model by the server (refusal fallbacks).
            with self._client.beta.messages.stream(
                model=self.model,
                max_tokens=64000,
                system=system or self._anthropic.NOT_GIVEN,
                messages=converted,
                thinking={"type": "adaptive", "display": "summarized"},
                output_config={"effort": self.effort},
                betas=["server-side-fallback-2026-07-01"],
                fallbacks="default",
            ) as stream:
                # The thinking and the answer as they come, for the progress strips.
                for event in stream:
                    if getattr(event, "type", "") != "content_block_delta":
                        continue
                    delta = event.delta
                    kind = getattr(delta, "type", "")
                    if kind == "thinking_delta":
                        progress.streamed(getattr(delta, "thinking", "") or "", thinking=True)
                    elif kind == "text_delta":
                        progress.streamed(getattr(delta, "text", "") or "")
                response = stream.get_final_message()
        except self._anthropic.APIStatusError as error:
            progress.call_ended()
            raise ProviderError(f"Claude request failed ({error.status_code}): {error.message}") from error
        except self._anthropic.APIConnectionError as error:
            progress.call_ended()
            raise ProviderError(f"Claude request failed: {error}") from error
        progress.call_ended(response.usage.input_tokens or 0, response.usage.output_tokens or 0)
        if response.stop_reason == "refusal":
            raise ProviderError("Claude declined the request")
        text = "".join(b.text for b in response.content if b.type == "text")
        thinking = "\n".join(b.thinking for b in response.content if b.type == "thinking" and b.thinking)
        usage = Usage(inputTokens=response.usage.input_tokens or 0, outputTokens=response.usage.output_tokens or 0, requests=1)
        return Completion(text=text, usage=usage, model=response.model or self.model, thinking=thinking)


class ScriptedProvider:
    """For tests and offline development: answers from a list (or a function
    of the messages), recording every request it receives."""

    name = "scripted"
    model = "scripted"

    def __init__(self, answers: list[str] | Callable[[list[dict]], str]):
        self._answers = answers
        self.requests: list[list[dict]] = []
        self.text_requests: list[int] = []

    def complete_text(self, messages: list[dict]) -> Completion:
        self.text_requests.append(len(self.requests))  # which request was free text
        return self.complete(messages)

    def complete(self, messages: list[dict]) -> Completion:
        self.requests.append(messages)
        progress.call_started()
        if callable(self._answers):
            text = self._answers(messages)
        elif self._answers:
            text = self._answers.pop(0)
        else:
            progress.call_ended()
            raise ProviderError("scripted provider has no answers left")
        progress.streamed(text)
        progress.call_ended(100, 50)
        return Completion(text=text, usage=Usage(inputTokens=100, outputTokens=50, requests=1), model=self.model)


class MistralEmbedder:
    """Text embeddings for the World Bible index (app/world.py)."""

    def __init__(self, api_key: str, model: str = "mistral-embed", timeout_s: float = 30.0):
        from mistralai.client import Mistral

        self.name = model
        self._client = Mistral(api_key=api_key)
        self._timeout_ms = int(timeout_s * 1000)

    def embed(self, texts: list[str]) -> list[list[float]]:
        try:
            response = self._client.embeddings.create(model=self.name, inputs=texts, timeout_ms=self._timeout_ms)
        except Exception as error:
            raise ProviderError(f"Mistral embeddings failed: {error}") from error
        return [item.embedding for item in response.data]
