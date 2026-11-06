# AIGE editor knowledge base

What the AI panel answers questions about the editor from (retrieval-augmented: the most related sections are found
for each request and given to the model; it can search for more with the `search_help` tool). The AI service indexes
every `.md` file here except this one (ai_service/app/help.py). Components, record types and project settings are
added from the engine's own descriptions at run time, so they aren't written here.

## Format

- One file per topic, in a folder per area: `shell/`, `scene/`, `ai/`, `generate/`, `systems/`.
- The file starts with `# Title` and one or two sentences saying what it covers.
- Each `## Section` is one retrievable piece: it must make sense on its own (name the panel or feature again, don't
  write "as above"). 80 to 400 words. A long subject gets several sections, not one huge one.
- Write for the person making a game with the editor, in plain words: what it is, where it is (menu paths with ▸:
  "Project ▸ Records ▸ Packages", panel names, button labels exactly as on screen), how to use it step by step, what
  each option does, shortcuts, what happens behind the scenes when it matters, common problems and their fixes, and
  related topics ("See also: ...").
- Only what the editor really does: written from its source (apps/editor/ui/src, apps/editor, engine) and docs/. No
  guesses; leave out what you can't confirm.
- Example questions a section answers can follow its text as a line `Questions: how do I ...? where is ...?` (they
  help retrieval).
- Each section ends with one or more worked examples, each a `### Example: <what it does>` subsection, as in the
  User Guide: a concrete case with real-sounding names ("an inn where you can cook", "Vara Bell, the innkeeper"),
  numbered steps with the exact clicks, labels and values (1. In the Project panel, click Records ▸ Packages, then
  New…), and the result to expect. Where it fits: the words to type in the AI panel (as a quote), a record's or
  procedure's JSON, or a Lua snippet, in fenced code blocks. Examples may take a section past 400 words.
- `index.json` gives the website's docs (/docs) their order: the areas, their titles and intros, and each area's
  pages in reading order. A new file is listed there too.
