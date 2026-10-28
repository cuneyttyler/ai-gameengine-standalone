# To-do

The working list, kept up to date as items move. Order is the order they're done in. See also
[../testing/end-user-tests.md](../testing/end-user-tests.md) §22 (what v1.0 still needs).

## In progress

- [ ] Build, run the tests, commit this round (everything under "Done (this round)")

## Still to do

- [ ] Cliff faces drawn where high ground drops (blocked already, not yet shown)
- [ ] The magenta-rod hand points, run with AutoSprite and checked (spends credits: with the user)
- [ ] AI panel: web search for assets from words; `look` while playing; a playtest holding several keys at once

## Done (this round)

- [x] AI panel as an agent: tools (find / read / what_is_at / check / look / playtest pressing actions), the tool
      loop, tasks up to 20 steps, Review each step, split requests run as one task, routing to the Build panel and
      the Planner, clarifying questions, every selected entity sent, Undo for the files it writes
- [x] Agent actions: scene.create (with a type) / setType / delete, file.move (rename), asset.import / delete,
      input.addAction / removeAction, write / delete records, components on prefabs (add / remove), Generate
      (characters, animations) before its operations, editor actions (select, frame, open, play)
- [x] Harbour place type (docks, piers, warehouses, fish market)
- [x] Ground draw order: `Sprite.order`, Edit ▸ Bring forward / Send backward, Ctrl+] / Ctrl+[
- [x] Stairs between floors (Interiors, Paint's Mark, Stairs here), high ground (heights, slopes, cliffs block)
- [x] Animated parts (doors, chests) and Build ▸ Animate (Assembled: Assets ▸ Animate; Paint: ⑥ Animate), their
      triggers decided (near for doors, interact for chests)
- [x] NPCs opening doors on their way; cliff-aware paths
- [x] Effects on actions, custom effects (FX ▸ New)
- [x] Open the demo: made from the arpg template

## Done (earlier)

- [x] Actions: custom input actions, the Action component, Act for the player, characters (act, tend) and scripts
- [x] Interact the default action; attack a component added later
- [x] Animation: clips by state, Plays on, attack variants, any clip by name; points, Attachment, Equipment shown;
      the sheet editor's point tool; AutoSprite as the animator (8 directions a pack, outline stripped)
- [x] Solid roles (roles.json `solid`), buildings' blocked area fitted to their art, draw order by the footprint,
      built scenes repaired on opening
- [x] Scene types (Assembled / Paint), build from a picture (as the painting or a reference)
- [x] Art style as a project setting, set by the planner, in every art prompt
- [x] Inspector ▸ Add component sorted; the AI panel's prompt knows actions, clips, attachments
- [x] docs/testing/end-user-tests.md: the v1.0 end-user tests
