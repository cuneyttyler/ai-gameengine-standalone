# Layout revision tests

These tests check revisions made in step ② Layout, the way a user makes them in the editor.

## What one case does

1. A fresh project is made, and the case's request is built with the real AI service.
2. The build follows the Build panel's own steps: ① Confirm, Next, then Build.
3. That takes it to ② Layout.
4. The case's words go into the revision box. Zones are named the way the map labels them:
   `{A}` becomes, for example, "residential ward".

## Cases

The cases are in [cases.json](cases.json):

| Case | What is revised |
|---|---|
| `swap-by-where` | Two zones change places, said the long way ("move A to where B is now and B to where A is now"). |
| `swap-by-word` | Two zones change places, said in one word ("swap A and B"). |
| `move-north` | One zone is put north of another. |
| `grow` | One zone is made bigger. |
| `grow-double` | One zone is made twice as big. |
| `shrink` | One zone is made smaller. |

## What is checked

- **The change that was asked for:**
  - a swap: each zone stands on at least 80% of the other's old land;
  - a move: the zone's middle is in the direction asked;
  - a resize: the zone's size changes by the ratio given, where it stands. Grown, it still covers at least 80% of its old land; shrunk, at least 80% of it is inside its old land.
- **Only that change:**
  - every zone the request doesn't name, and that doesn't touch a named zone, keeps its land (`othersKeep`);
  - zones that touch a named zone may give up some land when it grows or moves (`touchingKeep`).
- No zone is lost.

## Running them

Start the AI service. It must run without `AIGE_SERVICE_TOKEN`:

```
cd ai_service
.venv/Scripts/python -m uvicorn app.main:app --port 8811
```

Then run the tests:

```
AIGE_REVISE_PORT=8811 build/tests/Release/aige_tests --gtest_filter=LayoutRevise.*
```

- `AIGE_REVISE_CASES=grow,shrink` runs only some of the cases.
- The model is the one chosen in your Preferences ▸ AI. `AIGE_REVISE_MODEL=mistral|mistral-medium-latest` picks another.

Each run makes its project in the system temp folder (`aige_revise_tests/`), apart from the results. The AI service keeps a project's world index open, so the folder can't be cleared while it runs.

## Results

Each case writes to `results/<case>/`:

- `before.png` and `after.png`: the zones map;
- `report.json`: the zones found, the words sent, the AI's revision, each zone's land before and after, and the failures;
- `run.log`.

`results/summary.json` lists every case's verdict.
