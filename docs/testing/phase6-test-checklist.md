# Phase 6 — Manual Test Checklist

The user-test checkpoints from Phase 6 (`development_plan.md`, "AI that understands the
art"), for one end-of-phase pass. Automated tests cover the logic: the grid styles, tile
border analysis, pack scale and prefab plans, autotiling, the village layout, roles and
build, and turning and mirroring. This list covers what tests can't judge: whether the art
is understood, and how the result looks.

The pack used throughout: `C:\Users\cuney\Documents\AIGE\medieval tileset\PNG`, the one
from the village test that started this phase.

**Before you start**
- **Rebuild:** close the editor, then run `cmake --build build --config Debug`.
- **AI service:** it must be running, with the Mistral key in `ai_service/.env`, for the
  analysis (6.2) and the AI village (6.5). Vision uses `MISTRAL_VISION_MODEL`
  (default `mistral-medium-latest`).
- **Use a new project** for this pass: File ▸ New project. The demo stays as it is.

## 6.1 — Grid style

- [ ] File ▸ New project: you're asked **Isometric** or **Top-down (square cells)**.
      Choose **Top-down** for the medieval pack: its tiles were drawn from above.
- [ ] Ground paints as squares edge to edge; characters walk up, down, left and right
      along the grid; things lower on screen draw in front.
- [ ] Project settings ▸ Grid ▸ style: switch to Isometric and back. The scene
      re-lays itself each time, without a restart.

## 6.2 — Asset understanding

- [ ] Import the pack's PNG folder into Assets, then select everything ▸ **Analyze**.
      A notification reports how many assets were described.
- [ ] Pick a few assets with meaningless names ("Ground 37", "Asset 12"). The details
      say what each one is, its kind, its tags, its size in cells and whether it blocks.
      Check that they're right.
- [ ] Road tiles: the details show **edges** (8 terrains) and a **center**. A
      straight dirt road running left–right has dirt at W and E.
- [ ] **Rename to the suggested name** on one asset: the file is renamed and prefabs
      using it still work. Nothing is renamed unless you ask.

## 6.3 — Pack import as prefabs

- [ ] Assets ▸ right-click the pack's folder ▸ **Import as prefabs**. The review window
      lists every image with its kind and size in cells.
- [ ] Set the pack scale (pixels per cell) once. A house comes out about 2–3 cells wide,
      a barrel under one, a ground tile exactly one; nothing is huge or tiny.
- [ ] Import. Tiles land in the palette's Tiles group, props elsewhere. Place one of
      each: the sizes look right next to the player.
- [ ] Ask the AI: "make a prefab of the well". The proposal shows a **prefab.create**
      with sensible cells.

## 6.4 — Autotiling

- [ ] Pick a dirt road tile as the brush. The toolbar shows **Auto-fit dirt** (ticked).
      Paint a road with a turn and a T-junction: the straights, corners, ends and
      junction pieces are chosen and turned for you.
- [ ] Paint a 4×4 dirt area: the rim and corners fit, and the middle is plain dirt.
- [ ] Erase a piece from the middle of a road: that cell becomes grass and its
      neighbours re-fit (the road now has two ends). Ctrl+Z restores it.
- [ ] Untick Auto-fit: painting places exactly the tile you picked.
- [ ] Ask the AI for "a dirt road from the player going east 15 cells, then north 10".
      The proposal uses **terrain.paintPath**, not single tiles, and the road fits.

## 6.5 — Village generator (most important)

- [ ] Level tab ▸ **Village**. The preview lists which prefabs fill each role (house,
      well, fence, tree, bush, rock, decoration, entrance, landmark) and what the pack
      lacks, plus a map. Check that the roles are right: no wall used as a house, no
      tree used as a fence.
- [ ] Houses **12**, seed 1 ▸ **Build village**. Check:
  - [ ] streets with fitted road pieces (no wrong pieces) and a square with the well;
  - [ ] houses at a sensible size, on plots along the streets, kept apart;
  - [ ] fenced gardens behind the houses (pieces running the right way);
  - [ ] barrels and carts by the doors, banners where the roads leave, a tower or
        windmill by the west entrance, and trees, bushes and rocks around the edge.
- [ ] The 🎲 button gives a different layout; houses 25 gives a clearly bigger village
      that grows in both directions, not one long street.
- [ ] Ctrl+Z removes the whole village in one step.
- [ ] Ask the AI: **"build a relatively large village east of the player"**. The
      proposal is one **level.village** (about 15–25 houses), and its review shows the
      same roles and map. Apply it and compare with your first test of this phase.

## 6.6 — Turning and facing

- [ ] Brush on a flat road piece: **R** turns it clockwise, **Shift+R** back, and
      **Shift+wheel** either way. The see-through preview turns with it. The toolbar
      shows ↻ 90°.
- [ ] Brush on a house (upright): **R** toggles **⇋ mirrored** / **as drawn**, and the
      preview flips.
- [ ] No brush, with a placed cart selected: R mirrors it; Ctrl+Z undoes it. Select a
      flat tile: R turns it.
- [ ] Help ▸ Keyboard Shortcuts: "Turn" and "Turn the other way" can be rebound.
- [ ] Ask the AI to "face the cart by the well the other way": the proposal uses
      **entity.mirror**.

**Phase 6 exit criterion:** from an unfamiliar pack, the AI imports everything at sensible
sizes, lays roads with the right pieces, and builds a village of the requested size that
uses the pack's houses, well, fences and decorations, on the grid the art was drawn for.
