# To-do

The working list, kept up to date as items move. Order is the order they're done in. See also
[../testing/end-user-tests.md](../testing/end-user-tests.md) §22 (what v1.0 still needs).

## In progress

- [ ] Build the editor with the last changes (open doorways, walking inside buildings, Animate a part waiting, characters
      by AutoSprite / objects by PixelLab): written and committed, the build stopped before it finished
- [ ] FirstGame: Animate all again (each door into its own files, kept with its picture for reuse)

## Still to do

- [ ] Doors in Paint scenes: open / use / locked and several doors (painted doors still open by proximity only)
- [ ] A door closes only after it has finished opening (a quick walk past cuts the clip)
- [ ] The finder's boxes on long walls (it puts every door at one height; a second, close look per door)
- [ ] Cliff faces drawn where high ground drops (blocked already, not yet shown)
- [ ] The magenta-rod hand points, run with AutoSprite and checked (spends credits: with the user)
- [ ] AI panel: web search for assets from words; `look` while playing; a playtest holding several keys at once
- [ ] Tests: build and run when asked (the C++ door tests and the schema fixture need the new fields)

## Done (this round)

- [x] Doors animated by PixelLab (asked with their background: no holes), clipped to the doorway; the in-engine swing
      when PixelLab fails; FLUX video as the other non-humanoid animator; characters by AutoSprite
- [x] Build ▸ Animate in two steps: ① Identify doors (every ground-level door, double doors, open doorways; check, Draw
      the doors, Clear, Use these doors), ② Animate; Animate all again; each door its own files
- [x] Several doors a building, each its own overlay, way of opening, lock and doorway
- [x] Doors open the project's way or their own: use (shortcuts, double click, a prompt) or auto; locked doors need
      the key; NPCs open doors on their way; the way in is where the doors are drawn
- [x] Doors placed where they're drawn (scaled as the building is), mirrored on a mirrored copy
- [x] Open doorways as entry points; inside a building all of it walkable
- [x] Animated buildings reused: doors kept with the picture, given to every prefab of it, carried through the libraries
- [x] Animate a part: a door (PixelLab), an open doorway, another object (PixelLab, described), a character (AutoSprite);
      Find the door; it waits for the result
- [x] Art checks on the quick vision model; scene switch asks to save only when changed

## Done (earlier)

- [x] AI panel as an agent: tools, tasks, routing, questions, file undo, scene / asset / prefab-component commands
- [x] Actions, the Action component, interact as the default action; effects on actions
- [x] Animation: clips by state, attack variants, points, attachments; AutoSprite characters (8 directions a pack)
- [x] Solid roles, buildings' blocked area fitted to their art; scene types; build from a picture; art style setting
- [x] Stairs between floors, high ground; harbour place type; ground draw order
