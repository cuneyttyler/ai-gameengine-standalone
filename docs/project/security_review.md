# Security and code review (Phase 11)

Status: **2026-10-21**. What was checked, what was found and fixed, and what is left as a
recommendation. The threat model: one user on their own computer; the editor, its WebView UI
and the local AI service are trusted code; what isn't trusted is **other software on the
machine** (above all web pages in the user's browser, which can reach `127.0.0.1`), **content
from the web** (OpenGameArt pages and files), and **text a model writes** (it shapes plans,
file names and UI text).

## 1. The local AI service

| Check | Finding | Done |
|---|---|---|
| Where it listens | `127.0.0.1:8765` only (`AiClient::Options::launchCommand`) | kept |
| Who may call it | **Anyone on the machine**: a web page could POST to it (spending the user's model credits, writing generated or downloaded files into the project). | **Fixed**: the editor makes a 128-bit token per session, gives it to the process it starts (`AIGE_SERVICE_TOKEN`, in that process's environment only) and sends it on every request (`X-AIGE-Token`); the service answers nothing else (`hmac.compare_digest`). A custom header also makes a browser's cross-origin request need a preflight, which the service doesn't grant. `/health` stays open (it says nothing). |
| DNS rebinding | A page could reach it by a name that resolves to 127.0.0.1 | **Fixed**: with the token on, the Host header must be `127.0.0.1`, `localhost` or `::1` (403 otherwise). |
| Paths in requests | `describe_assets`, `review_layout` and web search check that a path stays inside the project; `_describe_batch` joined `item.path` unchecked (only reached with checked items) | **Fixed** (checked there too) |
| Keys | In `ai_service/.env` (git-ignored); never logged; errors name the missing variable, not its value | kept |
| Tests | `tests/test_security.py`: open without the variable; with it, no token / a wrong one refused, the right one answered, another host refused | added |

## 2. Web fetching (OpenGameArt)

Only `https://opengameart.org/sites/default/files/*.png` links are downloaded; each at most
4 MB, with the PNG signature checked and the image decoded (Pillow's decompression-bomb limit)
before it's kept; only allowed licences (CC0, CC-BY, OGA-BY; CC-BY-SA if allowed); file names
reduced to `[a-z0-9-]`, written under the project's `assets/web/<folder>` (checked to stay
inside). No change needed.

## 3. The editor's file actions from the UI

Paths the UI names are relative to the project. `Project::Resolve` doesn't refuse `..`, so
each action must:

| Message | Before | Now |
|---|---|---|
| `asset:delete`, `story:delete` | `assets/` (or a story folder) and no `..` | kept |
| `file:move` | `CheckMove`: no `..`, `\`, root or drive | kept |
| `prefab:remove` | only a prefab the library knows | kept |
| `scene:delete` | **any existing file** (`DeleteScene` checked only that it exists) | **Fixed**: `scenes/…` and `Project::IsInside` |
| `folder:rename`, `folder:remove` | unchecked | **Fixed**: `Project::IsInside` on both |
| `library:addFromProject` | (new) | no `..` |
| `build:removeOlder` | (new) | only folders under `places/` that hold a build's `request.json`, ids without `/`, `\`, `.` |
| User ontology, user library | (new) | ids are slugs; files only in the user's folders |

`Project::IsInside(path)`: non-empty, `/`-separated, no `..`, no leading `/`, no drive (`:`).

## 4. Memory safety

The recurring bug (nine times now): iterating over a temporary JSON value, e.g.
`for (x : j.value("a", json()).items())` or `for (x : Load()["folders"])`: the temporary is gone
before the loop body runs. Four were found by the existing source rule (in this session's code),
three more by hand in the new user library and its test. The source rule now has a second check
(`SourceRulesTest.NoLoopOverATemporarysElement`) for range-fors over a by-value call's element
(`State()`, `Load()`, `Snapshot()`, `Card()`), over the engine, the editor and the tests.

And a crash: the scene build called an empty `std::function` (a pixels-per-cell callback) for
generated art (`PlanPackImport`): now guarded, with a fallback size.

## 5. Recommendations (not done)

- **`Project::Resolve` itself could refuse paths outside the project** (returning an empty
  path), instead of each action checking; that touches every caller, so it's left for a
  deliberate change with its own tests.
- **WebView content**: the UI uses `v-html` only for SVG it builds itself (layout pattern
  sketches from pattern names). If pattern or module names become user-defined (Phase 9 allows
  new modules, not new patterns), escape them there.
- **The service's dependencies** (`uv.lock`): audit them regularly (e.g. `pip-audit`).
- **The AIGE web service** (sharing, `asset_management.md` §5) will need authentication,
  upload limits, content review and a withdrawal path; none of it exists yet.
- **Code review**: the largest files (`PlaceBuild.cpp` ~3,800 lines, `EditorApp.cpp`
  ~4,600, `BuildPanel.vue` ~2,500) would be easier to keep correct split by concern (the
  build's history, its layout step, its asset step; the editor's message handlers by area).
