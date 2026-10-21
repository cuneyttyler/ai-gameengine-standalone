# Asset management: the user's library, generated art, sharing

Status: **2026-10-21**, development_plan.md Phase 10. §1–§3 are built; §4's question is built
and its upload waits for the AIGE web service (§5).

A project's art lives in the project (`assets/`). Until now, art made by the Art tool did too:
when the project was deleted, the art went with it, and another project couldn't reach it. The
**user library** is the user's own art, outside every project, organised as they like and used
by any project.

---

## 1. Where things are

| What | Where | Whose |
|---|---|---|
| A project's art | `<project>/assets/` | the project |
| Generated art, as made | `<project>/assets/textures/generated/` (+ `.meta`, `.gen.json`) | the project, and kept in the library (§3) |
| The user library | `%LOCALAPPDATA%\aige\library\` (`library.json`, `files/`) | the user, on this computer |
| The AIGE library (curated) | `<project>/library/catalog.json` today; the AIGE service later (§5) | everyone |
| The user's ontology | `%LOCALAPPDATA%\aige\ontology\` (Phase 9) | the user |

`library.json` (`aige.user-library/1`):

```json
{
  "schema": "aige.user-library/1",
  "sharing": "unset | ask | always | never",
  "folders": [{"id": "houses", "name": "Houses", "parent": ""}],
  "items": [{
    "id": "5469a4ea6e428557", "name": "stone-house", "file": "5469a4ea-house-1.png",
    "folder": "houses", "source": "generated | web | aige | project",
    "licence": "", "author": "", "url": "", "origin": "Village",
    "created": 1792534887, "hash": "5469a4ea6e428557",
    "shared": "unasked | yes | no",
    "meta": {"name": "stone-house", "roles": "house", "description": "…"}
  }]
}
```

- An item's **id is its content hash** (FNV-1a 64 of the file): the same picture is kept once,
  whichever project or session it comes from.
- Each file keeps its `.meta` (what the catalog knows: roles, kind, size, description) and
  `.gen.json` (how it was made: prompt, model, tries), copied beside it.
- Writes go to `library.json.tmp` first, then replace the file: a crash never leaves it half
  written.

## 2. The Library tab (Assets panel)

- **Folders**: made, renamed, deleted (what's inside moves up a level; nothing is deleted),
  nested. A breadcrumb shows where you are; the panel's search looks through the whole library.
- **Items**: a grid of thumbnails; click for details (source, the project it came from, its
  description and licence, sharing); double-click or **Add to the project** copies it into the
  open project (`assets/library/<file>`, with its `.meta`), where the catalog and the Build
  panel find it like any other art.
- **Into the library**: right-click an asset (or several) in the Assets tab: **Keep in my
  library**. Art from the web keeps its licence, author and URL (from its `.meta`), so credits
  follow it into any project (`CREDITS.md`).
- **Export / Import**: a folder (or the whole library) is written as `<name>.aige-library/`
  (`library.json` and `files/`), and such a folder is read back into a folder named after it.
  This is how a user moves their art to another computer, or gives a set to someone.

## 3. Generated art is never lost

Every time the project's assets are refreshed (after the Art tool or a build's sourcing makes
something, and when the Library tab opens), what's in `assets/textures/generated/` and not yet
in the library is copied into its **Generated** folder (`UserLibrary::KeepGenerated`), with the
project it came from. Derived copies (`-seamless.png` ground tiles, made from a kept original)
are left out. So:
- art made in a project survives the project;
- a build in another project finds it (Add to the project, or a library catalog later);
- it can be exported like any other item.

## 4. Sharing generated art: when and how the user is asked

**The goal**: art users generate could make the AIGE library richer for everyone, but only
with their clear, revocable consent, and never as a surprise.

**When to ask.** Not at install (nothing to share yet, and the question means nothing), not in
the middle of a build (the user is thinking about their place), and not with a modal that blocks
work. The question appears **in the Library tab, the first time it holds generated art**: the
moment the user can see what would be shared. It stays there, non-blocking, until answered.

**The question** (three answers, none pre-selected as a trick):

> **Share the art you generate?** Everything the Art tool makes is kept here, for every
> project. You can also offer it to other AIGE users through the AIGE library (credited to
> you; nothing is uploaded until its service is online).
> [Offer it by default] [I'll decide for each] [Keep it private]

- **Offer it by default** (`sharing: always`): asks for confirmation once, explaining what it
  means (offered, credited, uploaded only when the service exists, revocable per item). New
  generated items are `shared: yes`.
- **I'll decide for each** (`ask`): items stay `unasked` until the user offers or keeps each
  (its menu, its details); the footer counts the undecided.
- **Keep it private** (`never`): items are `shared: no`. Nothing is ever offered.

**Always changeable**: the Library tab's footer has the choice as a select; each generated item
can be offered or made private on its own (a ↗ mark shows the offered ones). Changing the
default applies to items still `unasked`, not to ones the user decided.

**What is offered** is only the user's **generated** art: never art from the web or the AIGE
library (not theirs to share), never project art they added. Each offered item carries what
made it (its `.gen.json`: prompt, model), its catalog description and the user's chosen credit.

**What isn't done**: nothing is uploaded today; there's no telemetry; nothing about a project
other than the item and its origin's name leaves the computer.

## 5. With the AIGE web service (later)

When the AIGE service exists (development_plan.md, Later: AIGE asset library):
- the user signs in (optional); items with `shared: yes` are uploaded once, with their
  `.meta` and `.gen.json`, under the user's credit, and marked `uploaded`;
- making an item private after upload asks the service to withdraw it;
- the service reviews uploads (licence: the user's grant, e.g. CC-BY with their name; content
  policy) before they appear in the AIGE library;
- the AIGE library is browsed from the Library tab (its own section), and an item from it is
  `source: aige`, with its licence and author, like any other.

## 6. Code

- `engine/assets/UserLibrary.{h,cpp}`: the library: add (deduplicated), folders, move, rename,
  remove, sharing, into a project, keep generated, export, import.
- `apps/editor/EditorApp.cpp`: `library:*` messages, `KeepGenerated` on every asset refresh,
  the `aige-library.local` host for the panel's thumbnails (`main.cpp`).
- `apps/editor/ui/src/components/LibraryTab.vue`: the tab; `AssetBrowser.vue`: the tab
  button and "Keep in my library".
- `tests/user_library_test.cpp`: generated art kept once (not its seamless copy), sharing
  chosen and per item, folders, into a project, exported and imported.
