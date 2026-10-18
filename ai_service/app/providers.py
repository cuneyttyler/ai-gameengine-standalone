"""Model providers. The service only needs "messages in, JSON text out"; each
adapter hides one vendor's SDK. Cloud first (Mistral); local models later
(docs/technical.md §10)."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable, Protocol

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

    def __init__(self, api_key: str, model: str = "mistral-large-latest", timeout_s: float = 90.0):
        from mistralai.client import Mistral  # imported lazily so tests don't need the SDK configured

        self.model = model
        self._client = Mistral(api_key=api_key)
        self._timeout_ms = int(timeout_s * 1000)
        # Magistral models think apart from their answer (place-interaction.md B3, native mode).
        self.reasoning = "magistral" in model

    def complete(self, messages: list[dict]) -> Completion:
        return self._call(messages, json_mode=True)

    def complete_text(self, messages: list[dict]) -> Completion:
        """Free text: the thinking pass of a two-pass call."""
        return self._call(messages, json_mode=False)

    def _call(self, messages: list[dict], json_mode: bool) -> Completion:
        try:
            options = {"response_format": {"type": "json_object"}} if json_mode else {}  # the prompt spells out the shape
            response = self._client.chat.complete(
                model=self.model,
                messages=messages,
                temperature=0.2,
                timeout_ms=self._timeout_ms,
                **options,
            )
        except Exception as error:  # the SDK raises several types; all mean "no answer"
            raise ProviderError(f"Mistral request failed: {error}") from error
        choice = response.choices[0]
        content = choice.message.content
        thinking = ""
        if isinstance(content, str):
            text = content
        else:
            # Reasoning models answer in chunks: thinking chunks, then text.
            parts, thoughts = [], []
            for chunk in content or []:
                kind = getattr(chunk, "type", "")
                if kind == "thinking":
                    for inner in getattr(chunk, "thinking", []) or []:
                        thoughts.append(getattr(inner, "text", "") or "")
                elif hasattr(chunk, "text"):
                    parts.append(chunk.text or "")
            text, thinking = "".join(parts), "\n".join(thoughts)
        usage = Usage(
            inputTokens=getattr(response.usage, "prompt_tokens", 0) or 0,
            outputTokens=getattr(response.usage, "completion_tokens", 0) or 0,
            requests=1,
        )
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
        if callable(self._answers):
            text = self._answers(messages)
        elif self._answers:
            text = self._answers.pop(0)
        else:
            raise ProviderError("scripted provider has no answers left")
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
