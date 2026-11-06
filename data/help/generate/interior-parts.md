# Animated parts, doors and interiors

Animating only the part of a picture that moves (a door, a window, a sign, a fireplace inside a building) instead of the
whole picture: Animate a part…, Build ▸ Animate for a place's doors and used things, Animate the interior… for the
moving things inside a building, and how interiors themselves are generated.

## Animate a part…: a door, a window, a sign

Right-click a building, any thing with a picture, or a painted scene's picture in the viewport ▸ **Animate a part…**.
The window shows the picture:

1. **It's**: **a door (opened from its own pixels)**, **an open doorway (its entry point, nothing to animate)**,
   **another object: a sign, a window, a flag (PixelLab)** or **a character (AutoSprite)**.
2. Drag a box round the part that moves, or press **Find the door** (the vision model looks for it).
3. For another object or a character, **Describe the animation** ("the sign swings gently in the wind").
4. **Activation**: as the project's doors (Project Settings ▸ Game ▸ doorsOpen; doors only), with the door shortcuts or
   a double click, by itself as someone comes near, an input action near it, a game event, or always playing (a loop).
   Depending on the choice: the input action, the event's name, **Sends event** (sent each time the player activates
   it), **While active** settings and a **Prompt** shown near it ("Open the door|Close the door": one for each way).
5. For a door: **Locked**, with the key's item id.

Press **Animate**. The box is cut out of the picture, animated, and placed over it as an **AnimatedPart** on the thing
(on its prefab, so every copy has it). The window shows the result playing over the picture; closing it doesn't stop the
work ("Close (it goes on)"), and a floating box in the corner follows it.

Questions: how do I animate just the door of a building? how do I make a sign swing? how do I animate part of a painted scene? what does Find the door do?

## How doors are opened: from their own pixels

A building's door is not redrawn by a model from scratch: the **doorway is fitted to the picture** from the box (its
sides at the door frame's strongest vertical edges, its top and bottom along the wall's isometric slope, on either face
of the building). Then:

- With **Non-humanoid animation** set to **PixelLab** (the default, needs `PIXELLAB_API_KEY`), the doorway's crop is
  animated by PixelLab Animate with text v3 ("the door swings slowly open inward…, the frame, the arch and the wall stay
  still; no people"), with its background kept, each frame laid over a dark open doorway and cut to the doorway, so a
  post or railing in front stays drawn. 12 frames a second.
- With **BFL FLUX video**, the same crop is animated by FLUX video.
- When neither can (no key, or the model fails), the door is opened by the engine itself from its own pixels: the panel
  swings inward on its hinge, darker as it turns, over a dark doorway, 24 frames at 20 fps, free, the art kept exactly.
  A warning says so ("…couldn't animate it: opened from its own pixels instead").

A double door (two leaves, or a door wider than three quarters of its height) opens as two halves. **Close** is the open
frames played backwards; each is held at its last frame. A door finishes opening before it shuts.

Questions: why does my door animation look like the original art? which model animates doors? how do double doors work? why did it say opened from its own pixels instead?

## Build ▸ Animate: a place's doors and used things

After a place's art is made, the Build panel's **Animate** tab (the panel moves there when the art is made; **To
Animate →**) animates what has to move, in two steps:

1. **① Identify doors (N)**: each building's doors are looked for in its picture by the vision model (every
   ground-level door, each looked at again close up), and shown with their boxes, status "door found: check it". Click a
   picture to see it big; where a box is off or a door is missing, **Draw the doors** (a box each, threshold to top and
   frame to frame; **Clear** to start again; tick **Open doorways** for archways) and **Use these doors**. **Identify**
   on a row looks again.
2. **② Animate (N)**: each identified door is opened (see "How doors are opened") and each thing that's used — a chest,
   a door, a gate, a portcullis, a drawbridge, a lever, a sarcophagus, a cupboard, a wardrobe, a cage — is animated whole
   ("the chest's lid swings open", "the lever is pulled down") by the non-humanoid model.

Each row shows the picture with its box, where it is, what triggers it, its status (to do, identifying its doors…, door
found: check it, animating…, animated, failed, skipped), and **Animate** / **Again** and **Skip** / **Include**. The bar
shows progress; **Stop after this one** stops between targets; **Animate all again** redoes everything. A door becomes an
AnimatedPart on the building's prefab; a used thing's prefab gets an Animator (`prop`) and an Interactable whose verb
plays it. Open archways are kept as ways in, with nothing to animate.

Questions: what does Build ▸ Animate do? how do I fix a door box that's wrong? how do I skip animating something? how do I stop Build ▸ Animate?

## What activates an animated part

An **AnimatedPart** (Inspector) holds the part's sheet, its box, its clips and what plays it, its **activation**:

| activation | plays when |
|---|---|
| `project` | as Project Settings ▸ Game ▸ **doorsOpen** says (`use` or `auto`) |
| `use` | the player's door shortcuts (Project Settings ▸ Game ▸ **doorActions**, `interact` by default: E, Space, the pad's A) or a double click (**doorDoubleClick**), near it; others open it as they come |
| `auto` | anyone comes near; it closes once they've gone |
| `action` | the player presses the named input action near it; again, it goes back (a lever) |
| `event` | the named game event fires (a script's `emit`, a trigger zone's); again, it goes back |
| `always` | on and on, from the start (a mill's sails, a fire) |
| `open` | never: an open doorway, always passable |

A door can be **locked** with a **key** (and **consumeKey**): it stays shut, its doorway blocked, and "Locked: needs …"
shows when the player tries it; carrying the key, it unlocks for good. A building's door blocks the way in while shut.
The Inspector's AnimatedPart row opens **Edit doors…** (a building) or **Edit parts…** (anything else).

Questions: how do I make a door open automatically? how do I lock a door with a key? how do I make a lever play when pressed? how do I make a part always animate?

## Generating a building's interior

A building's cutaway **interior** is a picture per floor: the building drawn again with its roof and front walls gone,
its rooms and furniture showing, on the same canvas as the outside so they swap in place. Interiors are always made by
**BFL** (an edit of the exterior picture, which needs a model that takes a reference image; Mistral and PixelLab can't),
so `BFL_API_KEY` is needed whatever the art model.

- **Generate interior**: right-click a picture in the Assets panel. It is written beside it as `<name>-interior.png`
  (floors above as `-interior-2.png`…) and paired with the outside.
- **Add interior: cutaway** (right-click a building in the viewport, or a prefab in the Assets panel): generates the
  interior first when it has none, then adds it to the prefab.
- **Redesign interiors…** (Inspector ▸ Building Plan): paints the floors again from a description of your own (empty:
  one that fits the building), with a new seed.
- Builds make interiors for building types set to Cutaway (Build ▸ Assets ▸ Interiors).

The vision model counts the storeys (1–4); each floor is painted from a blockout with its stairs and doors. **H** shows
the selected building's interior; **Shift+H** every building's. See the interiors topics for the details.

Questions: how do I generate the inside of a building? why does interior generation need BFL? how do I redo an interior? how do I see a building's interior?

## Animate the interior…: fireplaces, clocks, stoves

A cutaway interior's moving things are animated where they stand, as parts of the building on their floor. Right-click
a building with a cutaway ▸ **Animate the interior…** (needs AI on), or Inspector ▸ Building Plan ▸ **Animate the
interior…**.

1. **Find the moving things**: the vision model looks at each floor (up to four at once) for the interior objects the
   ontology says such a building holds (a smithy's forge, a house's fireplace, a lab's tesla coil; any interior object
   that moves when the building's kind isn't known); then the language model writes how each one moves, fitted to what
   is drawn, and whether anything would visibly move at all (an unlit candle wouldn't).
2. **Check them**: each floor has a tab with its picture and boxes (yellow to animate, grey left still, green animated,
   red failed). Each row has a tick, the kind and label, the **motion** (editable), the **Clip**, what **Plays** it
   (always (a loop), when the player uses it, as someone comes near; with a Prompt and seconds for "when used"),
   **Animate** (this one now) and **✕** (remove from the list). Drag a box round a thing it missed, pick **It's** (the
   kind) and **Add**. Parts the building already has show as "Animated before".
3. **Animate N ticked**: one at a time, each box is cut from its floor's picture, animated by the **Non-humanoid
   animation** model (PixelLab or FLUX video), looping unless its clip is open, close or break, and put on the building
   as an AnimatedPart on that floor. **Stop** returns the waiting ones to the list (the one in flight finishes).

A part plays only while its floor shows. One building is worked on at a time; closing the window doesn't stop the work.
The AI panel can open this window on a building for you.

See also: Animate tab, Models and providers, Troubleshooting generation.

Questions: how do I animate a fireplace inside a building? how do I add something the interior finder missed? why isn't my interior part playing? how do I stop animating the interior?
