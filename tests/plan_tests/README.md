# Plan revision tests

These tests check revisions made in step ③ Plan, the way a user makes them in the editor.

## What one case does

1. A fresh project is made, and the case's request is built with the real AI service.
2. The build follows the Build panel's own steps: ① Confirm, then ② Layout's Plan, then ③ Plan's Approve.
3. The case's words go into the plan's revision box. Zones are named the way the map labels them.

## Cases

The cases are in [cases.json](cases.json):

| Case | What is revised |
|---|---|
| `add-10-houses` | "add 10 more houses to the {A}" in a village. |
| `add-5-workshops` | "add 5 more workshops to the {A}" in the craftsmen's zone. |
| `add-3-buildings` | "add 3 more buildings to the {A}" in the outer zone. |

## What is checked

A plan revision makes only the change that was asked for:

- The zone has **exactly** as many more buildings as asked.
- Every other zone keeps **exactly** its land.
- Every other zone keeps all of its things, each where it stood. Nothing moves, goes, or is added.

## Running them

Start the AI service. It must run without `AIGE_SERVICE_TOKEN`:

```
cd ai_service
.venv/Scripts/python -m uvicorn app.main:app --port 8811
```

Then run the tests:

```
AIGE_REVISE_PORT=8811 build/tests/Release/aige_tests --gtest_filter=PlanRevise.*
```

- `AIGE_REVISE_CASES=add-10-houses` runs only some of the cases.
- The model is the one chosen in your Preferences ▸ AI. `AIGE_REVISE_MODEL=<provider>|<model>` picks another.

## Results

Each case writes to `results/<case>/`:

- `before.png` and `after.png`: the plan's map;
- `report.json`: the zone found, the words sent, the AI's revision, the building counts, and each other zone's things and land compared;
- `run.log`.

`results/summary.json` lists every case's verdict. The generated projects are not kept in git.
