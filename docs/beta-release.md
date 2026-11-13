# The Beta Release

The beta is the editor packaged to run on a tester's machine, **without our API keys**. Each tester uses the AI with
their own accounts, setting their keys in **File ▸ Preferences ▸ Beta Settings**.

## Making it

```
cmake --build build --config Release --target editor sandbox
python tools/package_beta.py --zip
```

`tools/package_beta.py` makes `dist/AIGE-Beta-<date>/` (and its `.zip`):

| Folder | What | From |
|---|---|---|
| `editor.exe`, `shaders/` | the editor | `build/apps/editor/Release` (not its `editor.exe.WebView2` profile) |
| `ui/` | its interface | `apps/editor/ui/dist` |
| `player/` | the game player that File ▸ Export ships | `build/apps/sandbox/Release` |
| `data/` | the engine's data: places, story, fx, magic, ui, planner, help | `data/` |
| `ai_service/` | the AI service: `app/`, `pyproject.toml`, `uv.lock`, README | `ai_service/`, **without** `.env`, `.venv`, `.cache`, `tests` |
| `python/` | the service's own Python, every package it uses installed | a standalone CPython 3.12 (`uv python install`), and `uv.lock`'s packages with the `segment` extra (`uv pip install`, copied, not linked) |
| `docs/` | the manuals | `docs/manuals` |
| `README-BETA.txt` | what a tester needs, and how to start | |

**No keys in it.** After copying, every file is searched for each value in `ai_service/.env`, as text and as UTF-16
(how the exe holds strings), and for anything that looks like a key (`sk-ant-…`, `sk-or-v1-…`). If anything is
found, the packaging stops and the package is removed.

## Running from the package

A packaged editor finds its files **beside itself**: `engine/install/InstallPath.h`'s `InstalledOr(built,
"data/places")` is that folder next to the exe when it exists. Otherwise it's the source tree's, compiled in, as a
build run from the build tree (and the tests) always uses. The same goes for the UI, the AI service, `data/story`,
`data/fx`, `data/magic`, `data/ui`, `data/planner` and `docs`. The AI service reads `data/` from the folder above it,
which the package keeps.

The tester needs Windows 10 or 11 with the WebView2 Runtime, and nothing else.

**The bundled Python.** `python/` is a standalone CPython: it runs from any folder, with no install and no
registry entries. The service's packages are in its own `site-packages`, exactly as `uv.lock` pins them, with
the `segment` extra (torch, for click-to-select), as the editor's own launch has it.

When `python/python.exe` is beside `editor.exe`, the editor starts the service with it:
`python -m uvicorn app.main:app` in `ai_service/`, with `PYTHONNOUSERSITE=1` so the user's own packages don't
mix in. A build run from the build tree has no `python/` and keeps `uv run` as before.

The packaging checks the bundled Python by importing the service on it, with no keys. torch makes the package
about 1 GB (`--no-python` leaves the Python out, and testers then need uv).

## Beta Settings

Preferences ▸ **Beta Settings** has two parts.

**Models in use.** Each job the app gives a model, with:
- its kind (text, vision, embeddings, image, animation);
- the provider and model the open project uses;
- whether that provider's key is set;
- **Change**, which opens the page where it's chosen.

The jobs are: writing (the AI panel, builds, the Planner), the canon check, vision, help search, prefab art,
backgrounds, painting places, humanoid animation and non-humanoid animation.

**API keys.** One row per provider the service can use: Mistral, Anthropic (Claude), OpenRouter, Black Forest Labs
(FLUX), PixelLab and AutoSprite. Each row has:
- what the key is for;
- whether it's set (only its last four characters are shown);
- **Set API Key** (a password field), **Remove**, and **Get a key** (the provider's page).

A key set there:
- is stored in `%LOCALAPPDATA%\aige\api-keys.json`, encrypted with Windows DPAPI for the current user
  (`engine/ai_bridge/ApiKeys`), so another user or another machine can't read it;
- is given to the AI service in its environment when the editor starts it (`AiClient::Options::environment`). It's
  used over the same variable in the system's environment, and over `ai_service/.env` (which `load_dotenv` doesn't
  let override);
- starts the AI again with it, since a key is read when the service starts.

In a source build, a key from `ai_service/.env` shows as "From ai_service/.env". Development works as before.

Without a key for a language model, the service still starts, and the AI's status says to set one in Beta Settings.
