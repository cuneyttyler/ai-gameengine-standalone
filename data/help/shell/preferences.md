# Preferences

File ▸ Preferences: your own settings on this computer, not part of the project. Covers every page and option:
General, Viewport, AI (on/off, review policy, language model, checks, keys), Art and painting (prefab art,
background, humanoid and non-humanoid animation, paint model), New projects and Keyboard.

## Opening Preferences and where they're kept

Open **File ▸ Preferences…**. The AI panel's ⚙ button opens it too. The window has its pages on the left (General,
Viewport, AI, Art and painting, New projects, Keyboard) and the chosen page on the right, each setting with its control
and a line saying what it does. Close it with × or Esc, or by clicking outside it.

Preferences are **yours, on this computer**. They aren't part of the project, and they're kept in your user
preferences file (`%LOCALAPPDATA%\aige\editor.json`). Changes apply at once; there's no OK button. Several AI choices
can also be set per project in **Project Settings ▸ AI**. When a project has its own choice, Preferences says "This
project uses its own: …", and for art, background and animation there's a **Use this choice in the project** button
that clears the project's override so it follows your preference.

See also: project-settings.md.

Questions: where are the preferences? are preferences saved in the project? why does my preference not apply to this
project?

## General page

- **Launch ▸ Open the last project**: when ticked, the editor opens your last project directly at launch instead of
  the welcome window. File ▸ Close Project still brings the welcome window back. The same box is on the welcome
  window.
- **Launch ▸ This project**: shows the full path of the open project.
- **Interface ▸ Interface scale**: 80%, 90%, 100%, 110%, 125% or 150%. It scales the panels' text and controls, but
  not the viewport.
- **Recovery ▸ Keep unsaved changes**: every minute, every 2 minutes (the default), every 5 minutes, every 10
  minutes, or never. The editor keeps a copy of the open scene's unsaved changes this often, and offers it back if
  the editor closes without saving (a crash).

See also: projects.md (recovery).

Questions: how do I make the interface bigger? how do I skip the welcome screen? how often is crash recovery saved?

## Viewport page

- **Grid ▸ Show the grid at start**: shows the tile borders on empty ground when the editor opens. View ▸ Tile grid
  (G) turns it on and off at any time.
- **Camera ▸ Zoom speed**: slow, medium, normal (the default) or fast. How far one wheel step zooms.
- **Camera ▸ Invert the wheel**: wheel-up zooms out.
- **Camera ▸ Moving the camera**: a reminder. Drag empty ground with the Select tool, or drag with the right or middle
  button. Shift+drag draws a selection box.

Questions: how do I change zoom speed? how do I invert mouse wheel zoom? how do I hide the grid at startup?

## AI page: the AI and its changes

- **The AI ▸ Use AI**: when it's off, the AI service isn't started and nothing is sent to any model. The AI panel's
  rail button then reads "AI off", and AI features are greyed out.
- **The AI ▸ Its changes** (the review policy):
  - **Review every change before it's applied** (the default): every AI proposal waits for you to accept it.
  - **Apply small tweaks directly (with Undo)**: a new value for an existing field, project setting or script property
    applies at once, as long as it can be put back exactly. Anything structural still waits for review: adding or
    removing components, placing, painting, deleting, scripts, prefab edits, and changing which script runs or which
    prefab a copy belongs to.

Questions: how do I turn the AI off? how do I stop reviewing every AI change? what counts as a small tweak?

## AI page: language model, checks and keys

- **Language model ▸ Provider**: Mistral, Claude or OpenRouter. "(no key)" marks a provider whose API key is missing.
  Switching provider starts on that provider's default model.
- **Language model ▸ Model**: a searchable list of the provider's models. Leave it empty for "the default model", or
  type any model name. **↻** asks the provider for its list again. Free OpenRouter models are marked "free". If the
  project has its own model (Project Settings ▸ AI), that's noted here.
- **Checks ▸ Canon check**: the model that reads new writing against the World Bible and flags contradictions.
  "Mistral Medium (default)", or pick Mistral or OpenRouter and a model.
- **Checks ▸ Vision**: the model that looks at pictures (generated art, maps, doors, storeys). The default is Claude
  itself when Claude writes, otherwise Mistral Large 4.
  OpenRouter's default for both checks is GPT-6 Luna. Its free models (":free") cost nothing but are rate limited, and
  they're listed first. "In use:" shows the model actually used.
- **Keys ▸ API keys**: keys go in `ai_service/.env` (MISTRAL_API_KEY, ANTHROPIC_API_KEY, OPENROUTER_API_KEY,
  PIXELLAB_API_KEY, BFL_API_KEY). They're only sent to that service.

Questions: how do I switch to Claude? where do I put my API key? which model checks contradictions? how do I pick a
vision model?

## Art and painting page

These are the app's choices. A project can have its own (Project Settings ▸ AI), and a build its own (Build ▸
Assets).

- **Art ▸ Prefab art**: who draws prefab art (every piece in one style for Assembled builds, and the Art tool's single
  pieces). The choices are **Follow the provider** (Claude draws nothing, so with Claude it falls to Mistral, else
  PixelLab), Mistral, PixelLab, BFL FLUX.2 klein, BFL FLUX.3 Image or BFL FLUX.2 pro.
- **Characters, animations**: a note: characters (Generate ▸ Character) are drawn by the art model when it's a BFL
  one, else by PixelLab; animations by the Humanoid and Non-humanoid animation choices.
- **Painting ▸ Background**: who paints a place's background (its ground, roads and floors as one picture). The
  choices are BFL FLUX.3 Image (keeps the map best), FLUX.2 max (as good, slower), FLUX.2 pro, or FLUX.2 klein (fast,
  loose).
- **Humanoid animation**: AutoSprite (its motions or your own; a character's 8 directions at once) or PixelLab Animate
  with text v3 (8 frames, up to 256 px; slow).
- **Non-humanoid animation**: things like doors, chests, levers and flags. PixelLab Animate with text v3 (keeps the
  thing as drawn; 8 frames) or BFL FLUX video (12 frames taken from a 5 s clip).
- **Paint model**: who paints a place whole for the Paint build method when the project doesn't say. The choices are
  FLUX-3-Image (keeps the layout best), FLUX-2-Klein-4b (fast, a rough draft), FLUX-2-Pro, FLUX-2-Max,
  FLUX-3-Image-to-Pro and FLUX-3-Image-to-Klein.

Questions: which model draws prefabs? how do I change the background painter? which tool animates characters? what
does the paint model do?

## New projects and Keyboard pages

- **New projects ▸ Places ▸ Build method**: what File ▸ New Project starts with. **Paint** (each place painted whole,
  then marked by you; the default) or **Assembled** (each piece's art put together over a painted background). You can
  still change it in the New Project window, and later in Project Settings ▸ Places.
- **Keyboard ▸ Key bindings**: the **Keyboard shortcuts…** button opens Help ▸ Keyboard Shortcuts, where you can see
  and rebind every action.

See also: projects.md, shortcuts.md.

Questions: how do I make new projects use Assembled? where do I change key bindings?
