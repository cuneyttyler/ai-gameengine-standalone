"""Image generation (development_plan.md 3.6) and the Asset Generator Studio's
endpoint logic: description -> prompt -> image -> sprite -> project file with a
.gen.json sidecar saying exactly where it came from."""

from __future__ import annotations

import json
import re
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Protocol

from .providers import ProviderError
from .sprites import SpriteOptions, make_sprite

SAFE_NAME = re.compile(r"^[a-z0-9][a-z0-9_-]{0,47}$")
KINDS = {
    # What each kind asks the image model for: one subject, isometric, on a
    # background the pipeline can remove.
    "object": "a single {description}, isometric 3/4 view, centered, whole object visible",
    "character": "a single {description}, full body, standing, isometric 3/4 view facing the viewer, centered",
}
STYLE = {
    "pixel": "pixel art, clean outlines, limited palette",
    "smooth": "clean hand-painted game art",
}
BACKDROP = ("isolated cutout floating on a plain pure white background: nothing under it, no ground, no floor, no grass, no base, no shadow, no text, no frame")


@dataclass
class GeneratedImage:
    data: bytes
    model: str
    prompt: str  # the prompt as the image model received it
    seed: int | None = None  # None: the provider doesn't expose one


class ImageGenerator(Protocol):
    name: str

    def generate(self, prompt: str) -> GeneratedImage: ...


class MistralImageGenerator:
    """FLUX through Mistral's Agents API (the image_generation tool). One agent
    is created on first use and reused by name."""

    name = "mistral"
    AGENT_NAME = "aige-sprite-generator"

    def __init__(self, api_key: str, model: str = "mistral-medium-latest", timeout_s: float = 90.0):
        from mistralai.client import Mistral

        self._client = Mistral(api_key=api_key)
        self._model = model
        self._timeout_ms = int(timeout_s * 1000)
        self._agent_id: str | None = None

    def _agent(self) -> str:
        if self._agent_id is None:
            for agent in self._client.beta.agents.list():
                if agent.name == self.AGENT_NAME:
                    self._agent_id = agent.id
                    break
        if self._agent_id is None:
            agent = self._client.beta.agents.create(
                model=self._model,
                name=self.AGENT_NAME,
                description="Draws single game sprites for the AIGE editor.",
                instructions="Always call the image generation tool exactly once with the user's prompt, unchanged.",
                tools=[{"type": "image_generation"}],
            )
            self._agent_id = agent.id
        return self._agent_id

    def generate(self, prompt: str) -> GeneratedImage:
        import httpx

        try:
            response = self._client.beta.conversations.start(
                agent_id=self._agent(), inputs=prompt, timeout_ms=self._timeout_ms
            )
        except Exception as error:
            raise ProviderError(f"Mistral image generation failed: {error}") from error
        used_prompt = prompt
        url = None
        for output in response.outputs:
            arguments = getattr(output, "arguments", None)
            if isinstance(arguments, str) and "prompt" in arguments:
                try:
                    used_prompt = json.loads(arguments).get("prompt", prompt)
                except json.JSONDecodeError:
                    pass
            content = getattr(output, "content", None)
            if isinstance(content, str):
                match = re.search(r"\((https://[^)\s]+)\)", content)
                url = url or (match.group(1) if match else None)
            elif isinstance(content, list):
                for chunk in content:
                    file_id = getattr(chunk, "file_id", None)
                    if file_id:
                        data = self._client.files.download(file_id=file_id).read()
                        return GeneratedImage(data, "flux (mistral image_generation)", used_prompt)
        if url is None:
            raise ProviderError("the image model didn't return an image")
        data = httpx.get(url, timeout=60).content
        return GeneratedImage(data, "flux (mistral image_generation)", used_prompt)


def build_prompt(description: str, kind: str, style: str, variant: str = "") -> str:
    subject = description.strip().rstrip(".")
    if variant:
        subject = f"{subject}, {variant.strip()}"
    return f"{KINDS[kind].format(description=subject)}, {STYLE.get(style, STYLE['pixel'])}, {BACKDROP}"


def generate_sprites(
    generator: ImageGenerator,
    root: Path,
    name: str,
    description: str,
    kind: str = "object",
    height: int = 48,
    style: str = "pixel",
    variants: list[str] | None = None,
) -> list[dict]:
    """Generates one sprite per variant (one if no variants) into
    <root>/assets/textures/generated/, each with a .gen.json sidecar.
    Returns what was written."""
    if not SAFE_NAME.match(name):
        raise ValueError("names are lowercase letters, digits, - and _ (up to 48)")
    if kind not in KINDS:
        raise ValueError(f"kind must be one of {', '.join(sorted(KINDS))}")
    if not description.strip():
        raise ValueError("describe what to draw")
    if not 8 <= height <= 512:
        raise ValueError("height must be 8-512 pixels")
    folder = root / "assets" / "textures" / "generated"
    folder.mkdir(parents=True, exist_ok=True)
    results = []
    for variant in variants or [""]:
        slug = re.sub(r"[^a-z0-9]+", "-", variant.lower()).strip("-")[:24]
        file_name = f"{name}-{slug}.png" if slug else f"{name}.png"
        prompt = build_prompt(description, kind, style, variant)
        started = time.time()
        image = generator.generate(prompt)
        png, processing = make_sprite(image.data, SpriteOptions(height=height, style=style))
        (folder / file_name).write_bytes(png)
        meta = {
            "description": description,
            "variant": variant or None,
            "family": name,
            "kind": kind,
            "prompt": prompt,
            "providerPrompt": image.prompt,
            "provider": generator.name,
            "model": image.model,
            "seed": image.seed,  # None: not exposed by the provider
            "processing": processing,
            "seconds": round(time.time() - started, 1),
            "created": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        }
        (folder / f"{file_name}.gen.json").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")
        results.append({"path": f"assets/textures/generated/{file_name}", "size": processing["size"], "meta": meta})
    return results
