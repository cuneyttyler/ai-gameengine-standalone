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

## 2. Making the interiors: one a floor (`matched.py make_interiors`, `make_floors`)

It runs inside `make_with_bfl`, after the exteriors and back views, for both art methods, and shows on the progress list
as **Interiors: N buildings cut away to show their rooms**. It takes each piece with `interior` (a Cutaway type), except
back views, whose exterior exists and whose interior doesn't yet. A re-run only makes the missing ones. The same
procedure serves **Generate interior** (`/assets/interior`) and painted places (`paint_interiors`).

For each building:

1. **Its storeys** (`count_storeys`): the vision model is shown the exterior and says how many floors one walks on
   (1-4; an attic counts only with windows or dormers of its own). One without a vision model.
2. **Each floor's room painted by itself** (`paint_room`, `room_prompt`): from a **blockout** of that room alone
   (`room_blockout`, centred on a 1024 magenta canvas, then moved back), the exterior beside it as a design reference
   (image 2: its walls' materials, framing, windows, colours), the floors at once. The blockout, in the exterior's frame:
   - the floor's storey a GREEN box from its level to the next (a storey: what stands above the footprint's back corner,
     seventeen twentieths of it walls, shared by the storeys: `_floor_frame`); its floor CYAN ("fill it with its rooms
     and furniture": the ground floor its main rooms and entrance, the floors above its other rooms);
   - its stairs up a RED ramp from its level to the next, at the far end: odd floors against the back-left wall rising
     left to right, even floors against the back-right wall rising right to left, each top at the upper corner
     (`stairs_ramp`; not on the top floor);
   - where the floor below's stairs come up a flat YELLOW box against that floor's wall at the upper corner, a third of
     the room deep (`stairs_opening`): "a large flat woven rug … right next to the wall, nothing between the rug and the
     wall … no bed or furniture in the upper corner" (asked to stay empty, the model filled it; a rug it leaves be);
   - on the ground floor each door, found on the exterior by the vision model (`vision.locate_doors`; Mistral answers
     boxes in thousandths of the picture), a BLUE box: its entrance there.
   The prompt says **what the building is**: its ontology role's description (`roles.json` "description", each
   building role's: what its rooms hold, its era, its lights: "a goods warehouse: rows of crates, barrels and sacks…",
   "a modern office tower: open-plan desks with computers…"; the editor sends its picture's role, `EditorApp::
   InteriorKind`; built places their piece's role, `role_description`), lit and decorated as such a building is in its
   time (candles and lanterns, or lamps and ceiling lights; rugs, pictures, plants, banners), and **the user's design**
   when given (the Redesign dialog's description: "The interior's design, as asked: …").
3. **Laid on the exterior** (`lay_floor`): floor k's picture is the exterior's own storeys below it (cut at its front
   edge on its level), the floor below's room seen a little darker through a hole where its staircase comes up (half
   the yellow box both ways from the upper corner, its own stairs up kept over it: `stairs_hole`), its room on top. So
   each floor stands exactly as high as the exterior says, and the stairs from below are that floor's real stairs. The
   ground floor is its room alone.
4. Tried and dropped: painting the whole building cut open (the model painted the wrong number of storeys below, split
   floors in two levels, copied the floor below when shown it); the stairs from below drawn as a ramp or a box (it drew
   stairs going up, or put the opening mid-floor); a masked BFL fill to clear the area (it painted a bed back);
   FLUX.2 klein (copied the blockout's colours, lost the style).
5. Saved beside the exterior's piece: `pieces/<slug>-interior.png` (floor 1), `-interior-2.png`, `-interior-3.png`…,
   each exactly its size.

### What the tests showed (`builder_tests/interior_test`, FirstGame's exteriors)

| Finding | Answer |
|---|---|
| A cutaway of the whole building (every storey shown at once): the upper floor's boards hide the ground floor | one interior a floor |
| Front walls cut to knee height: the walls, and their windows, still stand before the rooms | the floor's front walls gone *entirely*, down to the floor |
| The ground floor came out 10-25% smaller each side | not smaller: painted from higher up (its base rises at 0.71 against the exterior's 0.55); `fit_floor` brings it to the angle |
| Telling the model "same size, same corners" | no better: the fit is measured, not asked for |
| Measuring the footprint in the lowest quarter of the picture | a short ground floor holds only its front point there: scaled up and then clipped ("overcut"); the V's corners instead |
| Clipping to the exterior's outline | cut back walls and stairs; only outside the footprint's columns and below its base now |

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
- while its interior shows, its whole footprint is walkable (`TileGrid::inside_`), whatever the place's layers or a prop
  on it say; the ring of cells round it is its walls, open in front of each passable doorway.

### Showing it

- **Play:** `GameSession::StepGameplay` calls `UpdateInteriors(registry, player)` each step. A building's `shown` is
  true while the player's cell is inside its footprint, so walking in through the door shows the interior and walking
  out shows the exterior.
- `ApplyInteriors` swaps the sprite. When shown, it keeps the exterior in `exteriorTexture` and draws the interior.
  When not shown, it puts the exterior back.
- **Editor:** `ApplyInteriors` runs every frame when not playing, so *shown* (from the Inspector, a menu or the
  shortcut) previews it at once. The swap is a view, not an edit: it doesn't mark the scene changed, and Play sets
  `shown` from where the player stands.

### Floors

- `Interior.floors`: the upper floors' pictures (`;` between); `texture` is the ground floor's; `floor` the one shown.
  The exterior's `.meta` keeps them as `interior=` (floor 1) and `interiorFloors=`; each floor's has `exteriorOf=` and
  `floor=`. Built places, Add interior and the editor's repair on opening a scene (`RepairInteriorArt`: pictures made
  again are picked up from the pairing) fill them in.
- **Play:** in through the door shows floor 1. Each floor change has its **stairs** cell (`stairs`, "x,y;x,y": floor 1
  to 2, 2 to 3…; missing ones: from the back corner three fifths along the back-left wall for changes 1, 3…, the back-right for 2, 4… (the corner left out) — where the interior pictures draw each
  staircase). Walked onto from the floor below it goes up, from the floor above down (stepping off before it goes
  again); out of the building, the exterior, and floor 1 next time. Right-click inside a building showing a floor ▸
  **Stairs here** sets that floor's stairs to the cell under the cursor (Assembled and Paint alike).
- **Everyone's floor** (`OnFloor`, runtime): every character inside a building has its own floor, its stairs taking it
  up or down as the player's do. It stands that many storeys higher (its sprite's lift, eased: it climbs), a storey
  being `Interior.storeyHeight` pixels (0: from its picture: what stands above its footprint's diamond, three fifths of
  it, shared by its storeys). One on another floor than the one shown is hidden (in a building the player isn't in:
  above the ground floor). The building the player is in shows the player's floor.
- **Editor:** H steps through exterior, floor 1, floor 2… and back; the right-click menu has **Show floor k**.
- **Redesign interiors…** (Inspector ▸ Building Plan) asks for a description of the interior (empty: one that fits the
  building) and paints its floors again from its exterior, as many as it has
  now, with a new seed; its prefab (or itself) takes them, its stairs listed none (the defaults are where they are painted).

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
- **Inspector ▸ Interior** (`InteriorEditor.vue`): its floors (each floor's picture, one removed, one added), a summary of
  its stairs and ways in, and **Edit building…**: the building editor (§5c).
- **Viewport right-click** on a building with a cutaway interior: **Floors ▸ N** (fewer floors than it was painted with:
  its upper floors' pictures past that dropped). On the ground: **Ramp here** (a Ramp: characters walk between heights
  there).
- **Build panel ▸ Assets ▸ Interiors:** each building type's **Floors** (Auto: counted from its picture; or 1-4), used
  when its interiors are painted. The count itself counts only storeys of wall below the eaves (dormers, gable windows,
  an attic aren't one).
- Host messages:
  - `entity:interior {id?, shown?}`: the selection or `id`; toggles when `shown` is absent;
  - `interiors:showAll {shown}`.
  - `viewport:contextMenu` sends `entity.interior` (true, false, or null for none) and `interiors {count, shown}`.

### 5b. Ways in

A building's ways in are where its doors are drawn (each door's doorway: its footprint's edge cell nearest it), else
its door side's middle. Set by hand (`Interior.entries`, cells relative to its footprint's top-left, so a moved copy
keeps them), they're its only ways in: each door uses the nearest one, a way in without a door is always open, and its
door side's middle is shut unless it's one of them. Inside, the whole footprint is walkable; its walls are the ring of
cells round it, open in front of each way in.

### 5c. The building editor

(The Interior component shows in the Inspector as **Building Plan**; scenes and prefabs keep its name, `Interior`.)

Inspector ▸ Interior ▸ **Edit building…** (`BuildingEditor.vue`, `interior:art` / `interior:setArt`): the building's
exterior and each floor's interior as tabs, its footprint's cells drawn over them as the scene draws them (diamonds in the
pictures' pixels), and a brush (size, Draw, Erase, Clear) for two layers: **Stairs** (on floor *k*: the stairs from it up
to *k+1*; several cells may be one staircase, `x,y|x,y` in `stairs`) and **Ways in** (cells of its edge). Blue cells are
stairs (paler on the floor above: where they come up), green ways in. **Find the stairs (AI)** asks the vision model which
corner each floor's stairs are in. **Doors** shows each of its doors (AnimatedPart) as a box on the picture, coloured by how
it opens (yellow on use, teal by itself, green an open doorway, red locked); clicking one shows how it opens, its lock
and key, and removes it. Inspector ▸ AnimatedPart lists how many doors it has, with **Edit doors…**. Saved on this
building (its copy: an override).

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
