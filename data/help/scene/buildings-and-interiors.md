# Buildings, interiors, doors and moving parts

How buildings work in a scene: their footprint, cutaway interiors and interiors that are scenes of their own, showing
interiors and floors in the editor, the Building Plan in the Inspector, the building editor (stairs, ways in, solid
things, doors), Animate a part…, Animate the interior…, and walking inside in play.

## Buildings and their footprint

A building is a thing with a Sprite and an **Obstacle**: the Obstacle's **left**, **right** and **back** say how many
cells beside and behind its own cell it covers, its **footprint**. A building without an interior blocks its whole
footprint. When a scene opens, buildings' footprints are fitted to what their art covers (a footprint row the walls
cover a third of counts as solid), so the player can't walk over a building's back.

A building on several cells is drawn in front of a character who is behind any part of it and behind one who is in
front of any part of it (by the footprint point nearest them). A character inside a building whose interior isn't
shown is drawn behind its walls.

Whether something counts as a building comes from the ontology (its picture's roles, kind or name reaching the role
`building`). Only buildings are offered a Building Plan's doors and ways in.

Questions: why can my player walk through a building? how do I make a building block the right cells? why is my
character drawn over the house?

## Two kinds of interior: cutaway and scene

When the player goes into a building its inside shows, one of two ways (the **Building Plan**'s **kind**):
- **cutaway** (houses, workshops, most buildings): the building is drawn again cut away, in place: roof and the walls
  facing the camera gone, its rooms, floor and furniture showing, the back walls kept. One picture per floor (storey).
- **scene** (big, many-roomed buildings: inns, temples, manors, keeps, hotels, schools, hospitals): the interior is a
  scene of its own. Stepping into its doorway goes there; its way out comes back to the doorway.

Give a building an interior:
- right-click it in the viewport (or a prefab card in Assets ▸ Prefabs) ▸ **Add interior: cutaway** or **Add interior:
  its own scene**. It goes on the prefab, so every copy has it (the entity itself if it's no prefab's copy), with a
  footprint from its art if it had none. A cutaway uses its picture's interior; if there's none yet it's generated first
  (BFL), then added when it comes.
- Assets ▸ Assets ▸ right-click an image ▸ **Generate interior**: the picture cut away by BFL, saved beside it as
  `<name>-interior.png`, and the two paired. **Go to interior** / **Go to exterior** jumps between a pair; the details'
  **Interior** select pairs any two images by hand (**Unpair** undoes it).
- Built places add interiors to buildings whose role has one (Build panel ▸ Assets ▸ Interiors).

Interiors are always made with BFL (an edit of the exterior picture needs a model that takes a reference image).

Questions: how do I add an interior to a house? what is a cutaway interior? how do I make a building that leads to
another scene? why does Generate interior need BFL?

## Showing interiors and floors in the editor

In the editor an interior shows as a preview; it doesn't change the scene and Play decides from where the player is.
- **H** (View ▸ **Selected building's interior / exterior**): the selected building's interior, each floor in turn,
  then its exterior.
- **Shift+H** (View ▸ **All interiors / exteriors**): every building's interior, or every exterior.
- **PageUp** / **PageDown** (View ▸ **Floor up** / **Floor down**): a floor up or down, for the selected building or
  every building.
- Right-click a building: **Show interior** (**Show interior (ground floor)** with several floors), **Show exterior**,
  **Show floor k**; **Floors ▸ N** sets how many floors it has (fewer than it was painted with: the upper floors past
  that are dropped).
- Right-click the ground: **Show interiors (N)** / **Show exteriors**.
- The Building Plan's **shown** and **floor** fields do the same.

Questions: how do I see inside a building in the editor? how do I look at the second floor? how do I show all
interiors?

## The Building Plan in the Inspector

The **Interior** component shows in the Inspector as **Building Plan**:
- **Floors (N)**: each floor's picture (Floor 1 is `texture`, upper floors are `floors`), a thumbnail and a picture
  picker; **✕** removes an upper floor ("two storeys counted as three"); **Add a floor** picks a picture and **Add**.
- **Stairs and ways in**: a summary ("Stairs: N of M drawn" or "its corners"; "Ways in: drawn" or "where its doors
  are").
- **Edit building…**: the building editor.
- **Redesign interiors…**: asks for a description of the interior you want ("a cosy candle-lit family home"; empty: one
  that fits the building) and paints its floors again from its exterior with BFL, as many as it has; its pictures are
  replaced on every copy of its prefab. A bar shows progress.
- **Animate the interior…**: see "Animate the interior".
- The other fields: **kind** (cutaway or scene), **scene** and **marker** (a scene interior's scene and arrival marker),
  **door** (the doorway's side s, e, n, w), **shown**, **exteriorTexture**, **cells** and **hides** (painted buildings),
  **storeyHeight** (pixels one stands higher each floor; 0: from the picture), **floor**.

Questions: how do I add a floor to a building? how do I change an interior picture? how do I redesign an interior? how do
I set which side the door is on?

## The building editor: stairs, ways in, solid things, doors

**Building Plan ▸ Edit building…** (or **Edit doors…** / **Edit parts…** on the Reaction: Moving Part section) opens the
building editor: tabs for the exterior and each floor, the building's footprint cells drawn over the picture, and
layers:
- **Stairs** (on floor k, the stairs from it up to k+1): paint the cells. Blue; paler on the floor above where they come
  up. **Find the stairs (AI)** asks the vision model which corner each floor's stairs are in.
- **Ways in** (on Floor 1): quarter cells of its edge, green. One goes in only where they're painted (a door half a cell
  wide: two quarters). None drawn: where its doors are, else the middle of its door side.
- **Solid** (on a floor): quarter cells one can't walk through, red: furniture, a counter.
- **Doors and parts (N)**: each door or moving part as a box, coloured by activation (yellow on use, teal by itself,
  green an open doorway, red locked). Click one to set **A door (a way in)**, **Activation**, event or input action,
  **Sends event**, while-active settings, **Prompt**, **Locked**, **Key**, **The key is used up**, or **Remove this
  part**. **Add a spot (no animation)**: drag a box for a place used like a part with nothing animated (a stove one
  cooks at).

Tools: **Draw**, **Erase**, **Brush** size, **Clear**. Press **Save** (it's saved on this building: on a prefab's copy,
an override) or **Cancel**.

Questions: how do I set where the stairs are? how do I choose where the player can enter a building? how do I make
furniture block movement inside? how do I lock a door?

## Doors and moving parts: Animate a part…

Right-click any thing (a building, a sign, a painted scene's picture) ▸ **Animate a part…**. In the dialog:
1. **It's**: **a door** (opened from its own pixels, the art kept), **an open doorway** (its entry point, nothing to
   animate), **another object** (a sign, a window, a flag: PixelLab, from the motion you describe), or **a character**
   (AutoSprite).
2. Drag a box round the part, or **Find the door** (the vision model looks for it).
3. For another object or a character: **Describe the animation** ("the sign swings gently in the wind").
4. **Activation**: as the project's doors (Project Settings ▸ Game ▸ doorsOpen), with the door shortcuts or a double
   click, by itself as someone comes near, an input action near it, a game event, or always playing. Then **Sends
   event**, **While active** (duration, the player's clip, held still), **Prompt** ("Open the door|Close the door"), and
   for a door **Locked** with the key's item id.
5. **Animate**. It waits and shows what it made; closing it lets the work go on.

The part goes on the building as a **Reaction: Moving Part** (AnimatedPart); on a prefab's copy every copy gets it. A
building's doors are also kept with its picture, so new prefabs of the same picture get them. Build ▸ Animate does
this for a whole built place.

Questions: how do I animate a door? how do I make a sign swing? how do I make a doorway with no door? how do I make a
door open with a key?

## Animate the interior

Right-click a building with a cutaway ▸ **Animate the interior…**, or **Building Plan ▸ Animate the interior…** (needs
the AI service). It finds the things inside that would move (a fireplace, a clock, a stove, a ceiling fan) among the
interior objects the ontology says such a building holds:
- **Find the moving things**: the vision model looks at each floor; the LLM writes how each moves. Each floor has a tab
  with its boxes (yellow to animate, grey left still, green animated, red failed).
- Each part's row: a tick, its kind and label, its motion (editable), clip, what plays it (always, when used, as someone
  comes near; prompt and seconds for "when used"), **Animate** and **✕**.
- Draw a box round something it missed and pick its kind to add it. Parts the building already has are listed as
  "animated before".
- **Animate N ticked** animates them one at a time (PixelLab or FLUX video: Preferences ▸ AI ▸ Non-humanoid
  animation) and puts each on its floor (a moving part with that floor). A part plays only while its floor shows.

One building is worked on at a time; closing the window doesn't stop it.

Questions: how do I animate a fireplace inside a house? how do I make interiors come alive? why doesn't my clock move
in play?

## Interiors that are scenes of their own

For a building whose Building Plan kind is **scene**:
- Right-click it ▸ **Build its interior scene…**: makes `scenes/interiors/<scene>-<role>.aige`, links the doorway to
  it, opens it and starts the Build panel there with "the interior of the <role>". When built, its way out is put at
  its entrance.
- **Open interior scene** opens a linked one.
- Linked, the building's doorway works as a gateway to the interior's arrival marker; a marker a step out of the
  doorway is where one comes back out. Collision is its whole footprint except the doorway.
- Not built yet: stepping in during play says its interior isn't built yet. A doorway leading to a missing scene or
  marker shows on Project ▸ Scenes ▸ Gateways.

You can set **scene** and **marker** in the Building Plan by hand to point the doorway at any scene.

Questions: how do I make an inn with its own interior level? how do I open a building's interior scene? why does it say
the interior isn't built yet?

## Walking inside in play

With a cutaway interior:
- Walking in through the door shows the interior (floor 1); walking out shows the exterior.
- Inside, the whole footprint is walkable; its walls are the ring of cells round it, open at each way in, with a gap as
  wide as the door where the door is narrower than its cell. A shut door blocks its way in; solid things inside block
  while that floor shows.
- **Stairs**: walked onto from the floor below takes one up, from the floor above down. Every character inside has its
  own floor, stands a storey higher per floor, and one on another floor than the one shown is hidden.
- Doors open as their activation says (Project Settings ▸ Game ▸ doorsOpen, doorActions, doorDoubleClick: by default
  the interact key or a double click near it); characters open doors on their way. A locked door says it needs its key;
  carrying the key, it unlocks for good.

Questions: how does the player go inside a building? why can't I walk into the house? how do stairs work? how do doors
open in play?
