# AI providers, models and keys

Which AI providers the editor uses (Mistral, Claude, OpenRouter, PixelLab, BFL, AutoSprite), where their keys go, how to choose the model that writes, checks, looks, draws and paints, and how the editor starts the AI service.

## The AI service and how the editor starts it

The editor's AI features run in a local Python service (in the `ai_service` folder). The editor starts it automatically when AI is on and stops it on exit; you only set it up once:

1. Install [uv](https://docs.astral.sh/uv/) (it manages Python and packages in `ai_service/.venv`, nothing system-wide).
2. In `ai_service`, run `uv sync`.
3. Put your provider key(s) in `ai_service/.env` (see "Keys").

The editor launches it with uv on a local port (8765 by default) and waits up to 45 seconds (the first launch may need uv to set up). It then sends the engine's schema. A service already running on the port (started by hand with `uv run uvicorn app.main:app --host 127.0.0.1 --port 8765`) is reused. Each editor gives its own service a secret token, and the service only answers this machine and that editor, so a web page or another editor can't use it; a second editor starts its own on another port.

Without a key the service still runs; requests answer with a message saying how to configure one, and the editor works normally without AI.

Questions: how does the AI service start? do I need Python? what is uv for? which port does the AI use?

## Keys

API keys go in `ai_service/.env` (git-ignored: never commit it), one per line as `NAME=your key`. They never leave this computer except to the service that uses them. The names:

- `MISTRAL_API_KEY`: Mistral, the default writer; also the default canon checker (Mistral Medium), vision model (Mistral Large 4), image generation and World Bible search by meaning (embeddings).
- `ANTHROPIC_API_KEY` (or `ANTHROPIC_AUTH_TOKEN`): Claude as the writer.
- `OPENROUTER_API_KEY`: OpenRouter, for the writer, the canon check or vision.
- `PIXELLAB_API_KEY`: PixelLab, for art, characters and animations.
- `BFL_API_KEY`: Black Forest Labs (FLUX) models, for art, backgrounds, painting places, interiors and video animation.
- `AUTO_SPRITE_API_KEY` (or `AUTOSPRITE_API_KEY`): AutoSprite, for humanoid animation.

File ▸ Preferences… ▸ AI ▸ Keys reminds you where they go. In the provider lists, a provider without its key is marked as such.

Optional settings in the same file: `MISTRAL_MODEL`, `CLAUDE_MODEL`, `CLAUDE_EFFORT`, `OPENROUTER_MODEL`, `MISTRAL_CHECK_MODEL`, `MISTRAL_VISION_MODEL`, `OPENROUTER_CHECK_MODEL`, `OPENROUTER_VISION_MODEL`, `AIGE_PROVIDER`, `AIGE_ART_PROVIDER`, `AIGE_CHECK_PROVIDER`, `AIGE_VISION_PROVIDER`.

Questions: where do I put my API key? what is MISTRAL_API_KEY? which keys do I need? is my key sent anywhere?

## Choosing the language model (the writer)

File ▸ Preferences… ▸ AI ▸ **Language model**:

- **Provider**: Mistral (the default), Claude or OpenRouter.
- **Model**: empty means "the default model" (Mistral: mistral-large-latest; Claude: claude-opus-5-5; OpenRouter: openai/gpt-6-luna). The list is fetched from the provider (**↻** asks again); you can type any model id. OpenRouter's free models (`:free`) are listed first; they cost nothing but are rate limited.

Changing the provider or model restarts the service with it; the status then shows "AI ready (model)". A project can have its own provider and model in Project Settings ▸ AI; the Preferences page then says "This project uses its own: ...".

Questions: how do I use Claude instead of Mistral? how do I change the AI model? can I use OpenRouter? which model is the default?

## Checks: the canon check and vision

File ▸ Preferences… ▸ AI ▸ **Checks**:

- **Canon check**: reads new writing against the World Bible and flags contradictions. Auto is Mistral Medium (the default); or Mistral or OpenRouter with a model (OpenRouter's default GPT-6 Luna; free: nvidia/nemotron-3-ultra-550b-a55b:free).
- **Vision**: looks at pictures (generated art, maps, doors, storeys, the built place). Auto is Claude itself when it writes, else Mistral Large 4; or Mistral or OpenRouter (default GPT-6 Luna; free: nvidia/nemotron-3.5-super-vl:free).

The page shows what is in use and any error. A busy free model is retried after a wait.

Questions: which model checks the World Bible? how do I change the vision model? can I use free models?

## Who draws, paints and animates

File ▸ Preferences… ▸ **Art and painting** (the app's choices; a project can have its own in Project Settings ▸ AI, and a build its own in Build ▸ Assets):

- **Prefab art**: Follow the provider (Mistral's images with Mistral; Claude draws nothing, so Mistral if its key is there, else PixelLab), Mistral, PixelLab, BFL FLUX.2 klein, BFL FLUX.3 Image, BFL FLUX.2 pro. Draws every piece of an Assembled build in one style and the Art tool's single pieces.
- **Background**: BFL FLUX.3 Image (keeps the map best, the default), FLUX.2 max (as good, slower), FLUX.2 pro, FLUX.2 klein (fast, loose). Paints a place's ground, roads and floors as one picture.
- **Humanoid animation**: AutoSprite (a character's 8 directions at once; the default) or PixelLab Animate with text v3 (8 frames, up to 256 px; slow).
- **Non-humanoid animation**: PixelLab Animate with text v3 (the default) or BFL FLUX video (a 5 s clip, 12 frames taken from it): a door opening, a chest, a lever, a flag.
- **Paint model**: who paints a place whole for the Paint build method: FLUX-3-Image (keeps the layout best), FLUX-2-Klein-4b (fast, a rough draft), FLUX-2-Pro, FLUX-2-Max, FLUX-3-Image-to-Pro, FLUX-3-Image-to-Klein.

When a project has its own choice, the page shows "This project uses its own: ..." with **Use this choice in the project**, which clears the project's choice so it follows Preferences.

Characters for Generate ▸ Character are made by PixelLab.

Questions: who draws the art? how do I change the background model? which provider animates characters? why does my project ignore my art preference?

See also: ai-panel.md, ai-requests-tips.md, build-panel.md.
