# Interiors

Status: **in development (2026-10-27), Phase 27**. The milestones are in `docs/project/development_plan.md` Phase 27;
§10 below says what is built.

**The idea.** When the player goes into a building, its inside shows, in one of two ways. Both are used in the same
project and the same scene, each building with its own kind (`Interior.kind`):

- **Cutaway** (houses, workshops, most buildings): the building is drawn again as a cutaway, in place. The roof and the
  walls facing the camera are gone, and its rooms, floor and furniture show. The back walls and the building's outline
  stay, so it still sits in the same spot with the town around it. Each such building has one complementing asset,
  its **interior**, paired with its **exterior**. Both are drawn on the same canvas, so they swap in place.
- **Scene** (big, many-roomed buildings: inns, temples, manors, keeps; hotels, schools, hospitals): the interior is a
  scene of its own, built by the place builder's `interior` build type (rooms, doors, furniture). Stepping into the
  building's doorway goes there; its way out comes back to the doorway. Such a building has **no** cutaway asset.

An asset without an interior can get one any time: the Assets panel's right-click **Generate interior** (§5).

**Always BFL, never skipped.** An interior is an *edit* of the exterior picture, which needs a model that takes a
reference image. The app relies on BFL for this, as for back views and variants. There is no fallback that skips
interiors when another provider is set, because Mistral and PixelLab can't take a reference image.

## 1. Which things have one: the ontology

- A role's look has `has_interior` (`data/places/roles.json`). It's set on `building`, and every role under it inherits
  it through `broader` (`Ontology::RoleLook`).
- A role turns it off by setting `"has_interior": false` (a site marker, a landmark, a ruin).
- A role's look has `interior_kind`: `cutaway` on `building` (inherited), `scene` on the big many-roomed ones (manor,
  inn, temple, keep, chapel, barracks; hotel, mall, supermarket, school, hospital, police and fire station, city hall,
  modern church, office and apartment towers, transit station). The asset plan's look carries both keys to the scene.
- **Per building type, per build:** the Build panel's Assets step has an **Interiors** table, one row per building type
  in the place whose role has `has_interior` (others aren't listed): **Cutaway**, **Scene (no interior)** or **None**,
  its role's kind marked "(default)". Kept in `places/<id>/interiors.json` ({role: kind}, `build:interiorChoice`);
  `InteriorKindOf` gives the build's choice, else the role's kind. The asset plan's look carries it (so the art step
  makes cutaways only for Cutaway types, and the scene gives Scene types their doorway), and painted places read it too
  (`InteriorRequests`, `PairInteriorMarks`: a painted Scene building gets a doorway entity, `Interior{kind: scene, cells}`,
  its walk cells solid but the doorway).
- `CutMatchedArt` marks a piece `interior: true` when its layer is `building` and its role has a *cutaway* interior.
  That flag is what the art step reads: a scene interior's building gets no cutaway (its inside is built, not painted).

## 2b. Scene interiors

- **Engine.** `Interior{kind: "scene", scene, marker, door}` on the building. Its collision is its whole footprint but
  the doorway (`ForEachSceneInteriorCell`); `GameSession::CheckGateways` treats the doorway as a gateway to `scene`'s
  `marker`. Not built yet (`scene` empty): stepping in says "Its interior isn't built yet". The Inspector edits it, so a
  designer can turn any building into either kind.
- **Built places.** `PlaceObject` gives a building whose role's `interior_kind` is `scene` an unlinked scene Interior
  (its door from the plan's `facing`).
- **Linking** (`LinkInteriorOutside`, `LinkInteriorInside`, Interiors.h):
  - outside: the building's Interior points at the interior scene and its arrival marker, and a marker stands a step out
    of its doorway, where one comes out;
  - inside: a gateway at the interior's entrance (the open cell nearest the middle of its front row) back to that
    marker, and the arrival marker two steps in. Linking again keeps one way out.
- **From the Planner.** A plan's place `inside` another whose build type is `interior` (the Rusty Anchor inn inside
  Millbrook) is entered through a building's door (`GameBuild::ConnectScenes`): the unlinked scene-kind building whose
  role is named in the interior's name or prompt, else the biggest scene-kind one, else the biggest building. Without a
  building, the gateway goes on the scene's edge as before.
- **From the editor.** A building's right-click **Build its interior scene…** makes `scenes/interiors/<scene>-<role>.aige`,
  links the doorway to it, opens it and starts the Build panel there with "the interior of the <role>" (the interior
  build type). The build's folder keeps `interior-of.json`; when it's built, `LinkBuiltInterior` puts its way out at its
  entrance. **Open interior scene** opens a linked one.

## 2. Making the interior (`matched.py make_interiors`)

It runs inside `make_with_bfl`, after the exteriors and back views. It runs for both art methods (words and sheets) and
shows on the progress list as **Interiors: N buildings cut away to show their rooms**. It takes each piece with
`interior`, except back views, whose exterior exists and whose interior doesn't yet. A re-run only makes the missing ones.

For each building:

1. **Canvas.** The exterior (`pieces/<slug>.png`) goes on a 1024×1024 magenta canvas, scaled to at most 78% of it and
   centred.
2. **Edit.** `painter.repaint(canvas, INTERIOR_PROMPT)` with no other references. There's one call per building and up
   to 4 run at once.
3. **Back to the exterior's frame.** The same box is cut from the result and resized to the exterior's exact size.
4. **Mask.** A pixel is cleared when it is background (flood-filled from the border, `_background`) or outside the
   exterior's own outline (exterior alpha < 128). The interior can never spill past the building.
5. Saved as `pieces/<slug>-interior.png`: the same size as `pieces/<slug>.png`, pixel for pixel in register with it.

### The look (`INTERIOR_PROMPT`)

Approved on 2026-10-27, from `builder_tests/words_test/interiors.py` (`interiors.jpg`): a **dollhouse cutaway**.

- The roof is gone.
- The two walls facing the camera (front-left and front-right) are removed *entirely*, with their windows, doors,
  shutters, timbers and gables. Only a low sill marks the floor's front edges.
- **Every storey stays.** The upper floor, the stairs and the back walls at full height are kept, so a two-storey house
  still reads as two storeys.
- The rooms, inner walls and furniture follow the building's purpose (`purpose`, or its role), lit warmly, in the
  place's style.

Rejected along the way:

| Version | What went wrong |
|---|---|
| front walls cut to knee height | the walls, and especially their windows, still stood in front of the rooms |
| ground floor only, one storey high | the rooms were clear, but two-storey buildings became one storey and no longer matched their exterior |

## 3. Cutting and pairing (`MatchedArt.cpp CutMatchedArt`)

- An exterior is cut from its own picture with a crop box, then framed so its footprint sits on the grid. When
  `pieces/<slug>-interior.png` exists and is the same size, it goes through the *same* steps: key colour cleared,
  `Clean`, the same `CropImage` box, and the same framing offset. The two pictures come out the same size and aligned.
- Written beside each other: `<folder>/<slug>.png` and `<folder>/<slug>-interior.png`.
- The catalog (each asset's `.meta`) keeps the pair both ways:
  - the exterior has `interior=<interior path>`;
  - the interior has `exteriorOf=<exterior path>`, plus name "<role> interior", kind and layer `building`, and the
    exterior's `cells`, perspective and source.
- An interior has no `roles`, so a place's needs never pick it on its own.
- The cut report counts them (`report.interiors`).

## 4. In the scene: the `Interior` component

```cpp
struct Interior {
    std::string texture;         // the interior picture
    std::string door = "s";      // the doorway's side: s, e, n, w
    bool shown = false;          // showing the interior now
    std::string exteriorTexture; // the exterior, kept while the interior shows
};
```

- It's in the Inspector (`EngineReflection.cpp`), so a designer can give any building an interior by hand: pick the
  texture and the door side, and tick *shown* to preview it. Entering a building needs no Lua and no event: the
  component is the behaviour.
- **Built places:** `PlaceScene` `PlaceObject` adds the component to a building whose sprite texture's `.meta` names an
  `interior`. The door comes from the plan object's `facing`.

### Collision

A building without an interior blocks its whole footprint. With one (`TileGrid`, `ForEachInteriorWall`):

- only the edge cells of its footprint (the Obstacle's left/right/back around its cell) block;
- the middle cell of the `door` side stays open;
- the inside is walkable.

### Showing it

- **Play:** `GameSession::StepGameplay` calls `UpdateInteriors(registry, player)` each step. A building's `shown` is
  true while the player's cell is inside its footprint, so walking in through the door shows the interior and walking
  out shows the exterior.
- `ApplyInteriors` swaps the sprite. When shown, it keeps the exterior in `exteriorTexture` and draws the interior.
  When not shown, it puts the exterior back.
- **Editor:** `ApplyInteriors` runs every frame when not playing, so *shown* (from the Inspector, a menu or the
  shortcut) previews it at once. The swap is a view, not an edit: it doesn't mark the scene changed, and Play sets
  `shown` from where the player stands.

## 5. In the editor

- **Assets panel:** the right-click menu on an asset with a pair shows **Go to interior** or **Go to exterior**, which
  selects and scrolls to the other asset. On an image without one: **Generate interior** (`asset:makeInterior`,
  `/assets/interior`): the picture cut away by BFL (`matched.cutaway`), written beside it as `<name>-interior.png`, and
  the two paired in the catalog. On a prefab, the item goes to the prefab drawn with the paired texture, or to
  the asset when no prefab uses it.
- **Asset details:** shows the pair and sets it by hand ("Interior: …", any project image). This writes both
  catalog fields.
- **Viewport:**
  - right-click on a building with a cutaway interior: **Show interior** or **Show exterior**; on a scene-kind one:
    **Open interior scene** (when built) and **Build its interior scene…**;
  - right-click on the ground: **Show interiors (N)** or **Show exteriors**, for every building;
  - **H** (`view.interior`): the selected building's interior or exterior; **Shift+H** (`view.interiors`): every
    building's in the scene (all inside when any isn't, else all outside). Both rebindable, and in **View**.
- **Add interior** (a prefab's right-click in the Assets panel, a building's in the viewport; `interior:add`): one
  step. **Cutaway**: its picture's interior (generated first by BFL when it has none, then added when it comes);
  **its own scene**: a scene interior (then Build its interior scene…). On the prefab, so every copy has it (the
  entity itself when it's no prefab's), with a footprint (Obstacle) from its art's `footprint`/`cells` when it has none.
- **Repair** (`RepairInteriorArt`, on opening a scene and after a build): a building drawn with an interior picture
  (art chosen before interiors were kept out of the catalog: `CatalogEntriesFor` skips anything with `exteriorOf`)
  gets its exterior back and an Interior for the picture.
- Host messages:
  - `entity:interior {id?, shown?}`: the selection or `id`; toggles when `shown` is absent;
  - `interiors:showAll {shown}`.
  - `viewport:contextMenu` sends `entity.interior` (true, false, or null for none) and `interiors {count, shown}`.

## 6. Libraries

A pair stays a pair:

- **Your library:** keeping an exterior keeps its interior with it, and the item records its `interior`.
- **Into a project:** copying an item copies both files, and their `.meta` pair them under the new paths.
- **AIGE library:** works the same way, so its built-in buildings come with their interiors.

## 7. The Paint method

A painted place has no building assets: the buildings are part of the one painting. The interior is still an edit of
the building's own pixels, so it lines up the same way.

1. **After ⑤ Mark.** For each building whose role has an interior, its region is its occluder mask, or failing that
   its object's pixels in `blockout-ids.png`. That region's box is cut from `painting.png`.
2. **Edit.** BFL repaints the crop on the same canvas with the same `INTERIOR_PROMPT`, and the result is masked to the
   region (`marks/<id>-interior.png`). ⑤ Mark's Continue asks for it (`PlaceBuild::PaintInteriors`, `/places/interiors`,
   the occluders `InteriorRequests` picks) while ⑥ Assets goes on; Build waits for it. When it comes,
   `PairInteriorMarks` gives each occluder with a picture its `interior`, `cells` (its building's footprint) and `door`
   (its facing) in marks.json.
3. **Scene.** An entity at the building's place gets a sprite of the cutaway and an `Interior` whose exterior is
   *nothing*: outside, the painting shows through, and inside, the cutaway covers it.
   - A new `Interior.hides` lists the occluder ids to hide while the interior shows, so the house front stops being
     drawn over the player.
   - An `Interior` with `cells` (its footprint, since it has no Obstacle) is a painted one: `ApplyInteriors` draws its
     sprite only while shown (alpha), and hides every sprite whose PlaceNode statement starts with `hides`
     (`painting#<id>/`, its occluder's strips).
4. **Walk.** The building's walk cells become its walls with a doorway, as in §4. The door side comes from the plan
   object's `facing`.

## 8. AI panel

- **Context:** each building's `Interior` (its texture, its door, whether it shows), and how many interiors the scene
  has.
- **Ops:** show or hide an interior, set its texture or door, and pair two assets.
- **Build card:** "N buildings have interiors".
- **Checks:** a building whose role has an interior but has none (its art made before Phase 27, or an edit that failed)
  is listed, with **Make interiors** to make the missing ones.

## 9. Limits

- One interior per building. The upper floor is drawn but isn't a separate level to walk on, and the player walks on
  the ground floor.
- The door is the middle cell of one side. A building whose real door sits elsewhere is fixed in the Inspector
  (`door`), or later by marking it.
- Each building costs one BFL edit, and a failed edit leaves that building closed until the next run makes it.

## 10. Status

| Part | State |
|---|---|
| Ontology `has_interior` | built |
| `make_interiors` with the approved look | built, tested live (`builder_tests/words_test/interiors.py`) |
| Cutting and catalog pairing | built |
| `Interior` component, collision, Play, editor preview | built |
| Built places attach it | built |
| Assets panel and viewport menus, **H** | built |
| Asset details pair setter (`asset:setInterior`) | built |
| Library pairing (`UserLibrary::Add`, `CopyToProject`) | built; the AIGE library's catalog carries the same fields |
| Paint method (`/places/interiors`, `PaintInteriors`, `PairInteriorMarks`, the scene entity, the walls) | built, not yet run live |
| AI panel (the prompt's Interior entry; context has components and catalog fields; the build card's count) | built |
| Scene interiors (kind, collision, doorway gateway, linking, Planner, Build its interior scene…) | built |
| Generate interior (Assets panel) | built |
| C++ tests (`tests/interiors_test.cpp`) | built, passing |
