# AI providers, models and keys

Which AI providers the editor uses (Mistral, Claude, OpenRouter, PixelLab, BFL, AutoSprite), where their keys go, how to choose the model that writes, checks, looks, draws and paints, and how the editor starts the AI service.

## The AI service and how the editor starts it

The editor's AI features run in a local Python service (in the `ai_service` folder). The editor starts it automatically when AI is on and stops it on exit; you only set it up once:

1. Install [uv](https://docs.astral.sh/uv/) (it manages Python and packages in `ai_service/.venv`, nothing system-wide).
2. In `ai_service`, run `uv sync`.
3. Put your provider key(s) in `ai_service/.env` (see "Keys").

The editor launches it with uv on a local port (8765 by default) and waits up to 45 seconds (the first launch may need uv to set up). It then sends the engine's schema. A service already running on the port (started by hand with `uv run uvicorn app.main:app --host 127.0.0.1 --port 8765`) is reused. Each editor gives its own service a secret token, and the service only answers this machine and that editor, so a web page or another editor can't use it; a second editor starts its own on another port.

Without a key the service still runs; requests answer with a message saying how to configure one, and the editor works normally without AI.

### Example: setting up the AI service on a new computer

1. Install uv from https://docs.astral.sh/uv/.
2. Open a terminal in the project's `ai_service` folder and run:

   ```
   uv sync
   ```

3. Create `ai_service/.env` with your key (see "Keys"), for example `MISTRAL_API_KEY=...`.
4. Start the editor, press **I** and click **Turn on AI**.

The AI panel's header shows "Starting the AI service…" (the first launch can take a while as uv sets up), then "AI ready (mistral-large-latest)". If it says "The AI service didn't start within 45s (is uv installed? see ai_service/README.md)", check that `uv` runs in a terminal and that step 2 finished.

### Example: running the service by hand to watch its log

1. In `ai_service`, run `uv run uvicorn app.main:app --host 127.0.0.1 --port 8765`.
2. Start the editor with AI on.

The editor finds the service already on port 8765 and reuses it, so its requests and errors print in your terminal.

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

### Example: an .env for writing with Claude and painting places with FLUX

To write with Claude, keep Mistral for the canon check, vision and World Bible search, and have BFL paint places and interiors and AutoSprite animate people, `ai_service/.env` holds:

```
ANTHROPIC_API_KEY=sk-ant-...
MISTRAL_API_KEY=...
BFL_API_KEY=...
AUTO_SPRITE_API_KEY=...
```

1. Save the file.
2. In File ▸ Preferences… ▸ AI ▸ **Language model**, set **Provider** to Claude.

The service restarts with Claude and the header shows "AI ready (claude-opus-5-5)". In the provider lists, PixelLab and OpenRouter are marked as having no key, since they aren't in the file.

Questions: where do I put my API key? what is MISTRAL_API_KEY? which keys do I need? is my key sent anywhere?

## Choosing the language model (the writer)

File ▸ Preferences… ▸ AI ▸ **Language model**:

- **Provider**: Mistral (the default), Claude or OpenRouter.
- **Model**: empty means "the default model" (Mistral: mistral-large-latest; Claude: claude-opus-5-5; OpenRouter: openai/gpt-6-luna). The list is fetched from the provider (**↻** asks again); you can type any model id. OpenRouter's free models (`:free`) are listed first; they cost nothing but are rate limited.

Changing the provider or model restarts the service with it; the status then shows "AI ready (model)". A project can have its own provider and model in Project Settings ▸ AI; the Preferences page then says "This project uses its own: ...".

### Example: trying a free OpenRouter model

1. Add `OPENROUTER_API_KEY=...` to `ai_service/.env` and save.
2. Open File ▸ Preferences… ▸ AI ▸ **Language model** and set **Provider** to OpenRouter.
3. Click **↻** next to **Model** to fetch the list. The free models (ending in `:free`) are at the top; pick one, or type a model id.

The service restarts and the AI panel's header shows "AI ready (<the model>)". Free models cost nothing but are rate limited, so a busy hour may make requests slow.

### Example: one project on its own model

1. Open the project "Harbour Tales" and go to Project Settings ▸ AI.
2. Set its provider to Mistral and its model to `mistral-large-latest`.

Your other projects keep using Claude from Preferences. With Harbour Tales open, File ▸ Preferences… ▸ AI shows "This project uses its own: ..." to remind you.

Questions: how do I use Claude instead of Mistral? how do I change the AI model? can I use OpenRouter? which model is the default?

## Checks: the canon check and vision

File ▸ Preferences… ▸ AI ▸ **Checks**:

- **Canon check**: reads new writing against the World Bible and flags contradictions. Auto is Mistral Medium (the default); or Mistral or OpenRouter with a model (OpenRouter's default GPT-6 Luna; free: nvidia/nemotron-3-ultra-550b-a55b:free).
- **Vision**: looks at pictures (generated art, maps, doors, storeys, the built place). Auto is Claude itself when it writes, else Mistral Large 4; or Mistral or OpenRouter (default GPT-6 Luna; free: nvidia/nemotron-3.5-super-vl:free).

The page shows what is in use and any error. A busy free model is retried after a wait.

### Example: canon check on a free model

1. Make sure `OPENROUTER_API_KEY` is in `ai_service/.env`.
2. In File ▸ Preferences… ▸ AI ▸ **Checks**, set **Canon check** to OpenRouter with the model `nvidia/nemotron-3-ultra-550b-a55b:free`.
3. Ask the AI panel for a dialogue for one of your World Bible characters.

The card shows "Checking against the World Bible…" as before; the progress strip's "Consistency check" line is now the free model's work. If it is busy, the check waits and tries again; if it fails, the card says "Couldn't check against the World Bible" with the reason.

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

### Example: faster drafts while trying layouts

1. You're trying several layouts for a fishing village and don't want to wait for the best painting each time. In File ▸ Preferences… ▸ **Art and painting**, set **Paint model** to FLUX-2-Klein-4b and **Background** to FLUX.2 klein.
2. Build and repaint the village a few times in the Build panel; each painting comes back quicker, as a rough draft.
3. Once the layout is settled, set them back to FLUX-3-Image and FLUX.3 Image and paint it once more for the final look.

### Example: a project that ignores your art preference

1. You chose PixelLab for **Prefab art** in Preferences, yet the project keeps drawing with BFL.
2. Open File ▸ Preferences… ▸ **Art and painting**: it says "This project uses its own: ...".
3. Click **Use this choice in the project**.

The project's own choice is cleared, and it follows your Preferences from now on.

Questions: who draws the art? how do I change the background model? which provider animates characters? why does my project ignore my art preference?

See also: ai-panel.md, ai-requests-tips.md, build-panel.md.
