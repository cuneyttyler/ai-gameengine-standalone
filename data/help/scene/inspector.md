# The Inspector: entities, components and fields

How the Inspector shows the selected entity, how to edit fields, add and remove components, use the searchable lists,
work with prefab copies (overrides, Apply to prefab, Revert), see values by difficulty, set script properties, and the
special editors for Reactions, packages, character rules and items.

## What the Inspector shows

The **Inspector** (in the Scene panel) shows whatever is selected:
- **One entity**: its label at the top, then (for a prefab's copy with a sprite) its **Size**, the **Prefab** header,
  then one section per component, and at the bottom **+ Add component**, **+ New script…** and **Save as prefab…**.
- **A ground cell** of a built place: "Ground cell (x, y)", its tile layer, whether it blocks, the tile prefab (with
  picture), **Draw with it** and **Erase this cell (Delete)**.
- **A box selection**: how many things and ground cells, and **Delete them (Delete)**.
- **Nothing**: a hint and **Project Settings…**.

Every section and field comes from the engine's own descriptions, so names, types, ranges and options are always the
engine's. Hover a component's name to read what it does; hover a field's name for its description.

Components are shown by their label where they have one: Interior as **Building Plan**, Action as **Reaction**,
Interactable as **Reaction: Door, Chest, Sign**, AnimatedPart as **Reaction: Moving Part**, Responses as **Reaction:
Character Rules**, Barks as **Speech**, Faction as **Team**.

While the game plays, the Inspector refreshes live (ten times a second) and editing is limited; most buttons are hidden.

Questions: what does the inspector show? why is the inspector empty? how do I see what a component does?

## Editing fields

Each field has an editor matching its type:
- **numbers**: a number box, kept within the field's range; 0–1 values (tints, opacity, texture regions) also get a
  slider;
- **on/off**: a checkbox;
- **text**: a text box (changes apply when you leave the box or press Enter);
- **a choice of options** (Team, Collider shape, Weather…): a searchable list;
- **a picture**: a searchable list of the project's textures; "(built-in)" uses the placeholder look;
- **a sound**: the project's audio files;
- **a prefab**: the project's prefabs (not tiles);
- **another entity**: the scene's entities by name and #id;
- **a script**: the project's Lua scripts, with **✎** to open it in the script editor;
- **a name of something** (a scene, a marker, a dialogue, a record): picked from the project's list (see "Searchable
  lists").

Every edit is checked by the engine and is one undo step (Ctrl+Z). A value that isn't allowed is put back, and the
reason shows in red under the field. Collapse a component with the **▾** before its name.

The **✦** by a field (when the AI service is ready) asks the AI panel about that field. Editing a Name changes the
Scene Tree's label at once.

Questions: how do I change a value? why did my value change back? how do I pick a texture for a sprite? what does the ✦
button do?

## Searchable lists

Long lists (prefabs, pictures, entities, items, scenes, markers, dialogues, records, options) use a **search select**:
click it, type words to narrow the list (every word must match the name, value or hint), move with **↑/↓**, press
**Enter** to pick, **Escape** to close. At most 300 entries show at once; "Nothing matches" when none do.

Some lists accept a value that isn't in them yet: the search box says "Search, or type a new one", and **Use "…"** (or
Enter) takes what you typed. That's how you name an item, faction or marker before it exists. An empty entry, "(none)"
or "none", clears the field.

Fields that name something are filled from the project: Gateway's scene (scenes), marker fields (the markers),
Talk's dialogue (dialogues) and character (World Bible characters), Routine's schedule, CutsceneTrigger's cutscene,
Container's and Merchant's loot (loot records), Spawner's encounter, FactionMember's faction, Behaviour's combatStyle.

Questions: how do I search in a dropdown? can I type a value that isn't in the list? how do I clear a field?

## Adding and removing components

- **Add**: the **+ Add component** list at the bottom of the Inspector lists every component this entity can take,
  sorted by label (hover one for its description). Pick one and it's added with default values. Some components need
  others (for example Hurtbox needs WorldTransform2D and Health; Door needs Interactable; Animator and AnimatedPart need
  a Sprite); a component that can't be added says why ("Can't change …: …").
- **Remove**: the **×** at the right of a component's header removes it. Structural components (such as Tile, which makes an
  entity a ground cell) can't be added or removed by hand and have no ×. Removing a component another one needs may be
  refused, with the reason.
- A component marked **derived** (ScreenTransform2D for anything with a world position) is computed every frame from
  the world position, and its fields are read-only.

Adding or removing a component on a prefab's copy is recorded as an override of that copy (see "Prefab copies").
Adding and removing are each one undo step and are disabled while playing.

Questions: how do I add a component? how do I remove a component? why can't I edit ScreenTransform2D? what does derived
mean?

## Prefab copies: overrides, Apply to prefab, Revert

A thing placed from a prefab is a **copy** (an instance). The Inspector shows a **Prefab** header with its name (hover
for the file), "(file missing)" if the prefab was deleted, and the number of **overrides**: fields changed on this copy
only. Changed fields are highlighted, and their tooltip says "(Changed on this copy)".

The header's buttons:
- **Apply to prefab**: this copy's values become the prefab's; every other copy updates, keeping its own overrides.
- **Revert all**: discards this copy's changes (disabled with no overrides).
- **Replace prefab…**: put a copy of another prefab in its place (Replace prefab dialog).
- **Draw with it**: the prefab becomes the brush.

Beside each changed field:
- **⤒** applies just that value to the prefab (every copy gets it; a copy sized on its own keeps its share of the size);
- **↺** reverts just that field to the prefab's value.

The right-click menu's **Revert to prefab** is the same as Revert all. The **Size** row shows the copy's size as a %
of the prefab's (10–1000), sprite and collision together, like [ and ].

Questions: what is an override? how do I apply changes to every copy of a prefab? how do I undo changes to one copy? what
does Revert to prefab do?

## Values by difficulty

Fields that the difficulty levels scale (health, damage, defense, speed, gold, loot, prices, counts, perception) have a
**⚖** button by their name, its tooltip naming the kind of scaling ("Scaled by difficulty (health)…"). It opens
**Project ▸ Difficulty** on its preview of the selected entity: that field's value at every level.

There you can set a value by hand for one level, which overrides the computed value. Hand-set values are stored on the
entity in a **DifficultyOverrides** component (`{"Health.max": {"hard": 20}}`), added and removed for you as values
are set and cleared; each change is one undo step.

Questions: how do I make an enemy stronger on hard? what is the ⚖ button? what is DifficultyOverrides?

## Scripts on an entity

**+ New script…** at the bottom of the Inspector asks for a script name, creates the Lua file and attaches it (a
LuaScript component). A LuaScript's **path** field picks an existing script; **✎** opens it in the script editor.

The script's `properties` table becomes per-entity settings: the section lists them under **Script properties** as
number, checkbox or text boxes. A property set on this entity has its own value; one not set shows the script's
default. **↺** returns it to the default. If the script has an error, it shows in place of the properties. Changing a
property while playing applies at once.

See also: project-panel.md (Project ▸ Scripts).

Questions: how do I add a script to an object? how do I set a script's properties per entity? where do script errors
show?

## Saving an entity as a prefab

When the selected entity isn't a prefab's copy, the Inspector's footer has **Save as prefab…**: type a name (letters,
digits, `-` and `_`; it starts as the entity's name) and **Save** (Enter; Escape cancels). The entity is written as
`prefabs/<name>.prefab` and becomes that prefab's first copy. A name already used is refused: "already exists — select
one of its copies and use Apply". The same is in the right-click menu (**Save as prefab…**).

See also: prefabs.md.

Questions: how do I save an object as a prefab? why can't I save as prefab? where are prefabs saved?

## Special editors: Reactions, items, packages and character rules

Some fields have their own editors instead of raw text:
- **Reaction (Action) and Reaction: Door, Chest, Sign (Interactable)**: **if** is a list of conditions and **do**,
  **end**, **interrupted** lists of effects, picked from lists as in the story editor (flags, quests and stages,
  items…). Text that isn't a list stays editable as text, marked.
- **Reaction ▸ requires, takes, gives, accepts**: an item list, each item picked from the project's items (and Gold)
  with a count (accepts has no counts); **+ Item** adds one, **×** removes. No items yet: "create them in Project ▸
  Records".
- **Reaction ▸ action**: the input action picker; **userAnimation** the characters' clips; **animation**,
  **awayAnimation** the clips of its own Animator's sheet; **effect**, **userEffect** the project's effects.
- **Behaviour ▸ packages**: the character's packages in order (the first whose schedule and conditions hold runs).
  Each row: ↑/↓ to reorder, **✎** opens it in Project ▸ Records, **×** removes; **+ Add a package…** adds from a
  searchable list. A missing one is marked "missing".
- **Reaction: Character Rules (Responses)**: one card per rule: **When** (an event, `quest:<id>:<stage>`, or
  `action:<name>`), **Says**, **Clip**, **Range**, **Only once**, **Only if** and **Then**; **Add a rule**, **Remove this
  rule**.
- **FactionMember ▸ faction**: a searchable list of faction records and World Bible factions.
- **Path**, **Building Plan**, **Reaction: Moving Part**, **Facing**: see markers-and-paths.md,
  buildings-and-interiors.md and components-guide.md.

Questions: how do I set conditions on an action? how do I give items with an action? how do I order a character's
packages? how do I make a character react to an event?
