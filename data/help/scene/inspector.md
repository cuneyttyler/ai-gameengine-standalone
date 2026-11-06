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

### Example: looking over the cellar door

1. Click the cellar door of Vara's Inn in the viewport.
2. The Inspector shows its label, `Cellar door`, then **Size** and the **Prefab** header (it's a copy of the `door`
   prefab), then its components: Sprite, WorldTransform2D, Collider2D, **Reaction: Door, Chest, Sign** (Interactable)
   and Door.
3. Hover **Reaction: Door, Chest, Sign** to read what it does; hover a field name inside it for that field's meaning.
4. Click a ground cell beside the door: the Inspector now shows "Ground cell (x, y)" with its tile layer and tile
   prefab.
5. Press **Escape**: nothing is selected, and the Inspector offers **Project Settings…**.

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

### Example: making a wolf tougher and darker

1. Select `Grey wolf` in the Scene Tree.
2. In **Health**, type `30` in **max** and press Enter; set **current** to `30` too, so it starts unhurt.
3. Type `-5` in **max**: the box keeps the value within the field's range (at least 1).
4. In **Sprite**, drag the tint sliders down a little to darken the fur.
5. Not sure what a field means? Click the **✦** by it (with the AI service ready) and the AI panel explains it.
6. Press **Ctrl+Z** to undo the last change; each field edit is its own step.

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

### Example: sending the miller home before his marker exists

Old Hob the miller should go home to the mill, but you haven't placed its marker yet.
1. Select `Old Hob` and click **Behaviour ▸ home**.
2. Type `mill`: the list narrows to the markers whose names contain "mill" (none yet), and the box says "Search, or
   type a new one".
3. Type `mill_door` and press **Enter** (or click **Use "mill_door"**).
4. Later, place a marker named `mill_door` by the mill: Hob's home now points at it.
5. To clear the field again, open the list and pick "(none)".

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

### Example: making a scarecrow you can hit

1. Select the `Scarecrow` prop in the field.
2. Open **+ Add component** at the bottom of the Inspector and pick **Hurtbox**. If the scarecrow has no Health yet,
   it's refused with "Can't change …" saying what's missing: add **Health** first (max `10`), then Hurtbox.
3. Hits now land on it and take its health down in play.
4. Changed your mind? Click the **×** at the right of the Hurtbox header to remove it. Removing Health first would be
   refused while Hurtbox still needs it.

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

### Example: one bigger oak, then every oak a little greener

The village has twelve copies of the `oak` prefab.
1. Select the oak by the well and set **Size** to `130` %. Only this copy grows.
2. In its **Sprite**, change the tint to a greener colour: the field is highlighted, the Prefab header counts 1
   override, and the tooltip says "(Changed on this copy)".
3. Click **⤒** beside the tint: the value goes to the prefab, and all twelve oaks turn greener. The well oak keeps its
   own 130 % size.
4. On another oak you'd moved and recoloured by mistake, click **Revert all** (or right-click ▸ **Revert to prefab**):
   it looks like the prefab again.

Questions: what is an override? how do I apply changes to every copy of a prefab? how do I undo changes to one copy? what
does Revert to prefab do?

## Values by difficulty

Fields that the difficulty levels scale (health, damage, defense, speed, gold, loot, prices, counts, perception) have a
**⚖** button by their name, its tooltip naming the kind of scaling ("Scaled by difficulty (health)…"). It opens
**Project ▸ Difficulty** on its preview of the selected entity: that field's value at every level.

There you can set a value by hand for one level, which overrides the computed value. Hand-set values are stored on the
entity in a **DifficultyOverrides** component (`{"Health.max": {"hard": 20}}`), added and removed for you as values
are set and cleared; each change is one undo step.

### Example: a cave troll that is brutal on hard

1. Select `Cave troll` and find **Health ▸ max**, which has a **⚖** button.
2. Click **⚖**: Project ▸ Difficulty opens on the troll's preview, showing max health at every level.
3. Type `20` by hand for the hard level, beyond what the scaling would give.
4. Back in the Inspector, the troll has a new **DifficultyOverrides** component holding:

```json
{"Health.max": {"hard": 20}}
```

Clear the hand-set value and the component is removed again.

Questions: how do I make an enemy stronger on hard? what is the ⚖ button? what is DifficultyOverrides?

## Scripts on an entity

**+ New script…** at the bottom of the Inspector asks for a script name, creates the Lua file and attaches it (a
LuaScript component). A LuaScript's **path** field picks an existing script; **✎** opens it in the script editor.

The script's `properties` table becomes per-entity settings: the section lists them under **Script properties** as
number, checkbox or text boxes. A property set on this entity has its own value; one not set shows the script's
default. **↺** returns it to the default. If the script has an error, it shows in place of the properties. Changing a
property while playing applies at once.

See also: project-panel.md (Project ▸ Scripts).

### Example: a skeleton that flees when hurt, braver in the crypt

1. Select `Skeleton` and click **+ New script…**; name it `skeleton_flee`. The file is created and a LuaScript is
   attached.
2. Click **✎** to open it and write its properties at the top:

```lua
properties = {
    flee_health = 3,   -- flee when health drops below this
    taunt = "",        -- a sound to play when it spots the player
}

function update(dt)
    if get_health()[1] < properties.flee_health then
        -- run away
    end
end
```

3. Save the script. Back in the Inspector, **Script properties** shows `flee_health` (a number box, 3) and `taunt` (a
   text box).
4. On the crypt's skeleton, set `flee_health` to `1`: only that one is braver. **↺** returns it to 3.

Questions: how do I add a script to an object? how do I set a script's properties per entity? where do script errors
show?

## Saving an entity as a prefab

When the selected entity isn't a prefab's copy, the Inspector's footer has **Save as prefab…**: type a name (letters,
digits, `-` and `_`; it starts as the entity's name) and **Save** (Enter; Escape cancels). The entity is written as
`prefabs/<name>.prefab` and becomes that prefab's first copy. A name already used is refused: "already exists — select
one of its copies and use Apply". The same is in the right-click menu (**Save as prefab…**).

See also: prefabs.md.

### Example: saving the herb cart as a prefab

1. Select the herb cart you built by hand in the market (it isn't a copy of anything).
2. In the Inspector's footer click **Save as prefab…**; the name starts as `Herb cart`. Type `herb-cart` and press
   **Enter**.
3. `prefabs/herb-cart.prefab` is written, the cart shows a **Prefab** header with 0 overrides, and `herb-cart` is in
   Assets ▸ Prefabs.
4. Try saving another cart as `herb-cart`: it's refused with "already exists — select one of its copies and use
   Apply".

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

### Example: a shrine that takes an offering

The shrine in the forest should give a blessing charm when the player offers a candle.
1. Select `Forest shrine`, which has a **Reaction** (Action) component.
2. In **Reaction ▸ takes**, click **+ Item**, pick `Candle` and set the count to `1`.
3. In **Reaction ▸ gives**, click **+ Item**, pick `Blessing charm`, count `1`.
4. In **do**, add an effect that sets the flag `shrine_blessed`; in **if**, add the condition that the flag
   `shrine_blessed` is not set, so it only works once.
5. If the item list says to "create them in Project ▸ Records", make the two items there first.

### Example: the innkeeper thanks you for her ring

1. Select `Vara Bell` and add **Reaction: Character Rules** from **+ Add component**.
2. Click **Add a rule**. Set **When** to `quest:lost_ring:returned` (the stage of the quest "The Lost Ring" where the
   ring is given back), **Says** to "You found it! Drinks are on the house tonight.", **Range** to `6` and tick
   **Only once**.
3. In play, when that quest stage is reached near her, she says the line, once.

Questions: how do I set conditions on an action? how do I give items with an action? how do I order a character's
packages? how do I make a character react to an event?
