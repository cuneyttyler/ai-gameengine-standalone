# Routines and cutscenes

Daily routines (Project ▸ Story ▸ Routines and the Routine component), the game clock, markers, and cutscenes (Project
▸ Effects ▸ cutscenes, the timeline, tracks and keys, CutsceneTrigger).

## Daily routines

A routine is a simple day for an NPC, `schedules/<id>.json`, made in **Project ▸ Story ▸ Routines** (+ "New daily
routine"). Each **part of the day** has:

- **from**: a time (HH:MM); it lasts until the next part starts, and the last runs past midnight into the first;
- an activity: **stay at** (stand there), **wander around** (stroll within a radius of 1-20 cells), or **sleep at** (go
  there and disappear indoors; they can't be talked to until the next part);
- a scene: **here** (wherever they are) or **in <scene>**;
- a place: a Marker in that scene ("(missing)" when the marker isn't there).

**+ Part of the day** adds one. Give someone a **Routine** component (`schedule`, `speed` in cells per second) to follow
it. They find their way around walls (A* pathfinding).

While someone's day puts them in a scene other than the one being played, they're **away**: hidden, nobody to talk to.
Place the NPC, with the same Routine, in every scene their day visits.

Routines are the simple form. For more (using beds and counters, conditions, paths, following), use AI packages with
schedules instead (see "AI packages").

### Example: the baker's day

1. Place three Markers: `bakery-oven` and `baker-bed` in the town scene, and `market-square`.
2. Open **Project ▸ Story ▸ Routines** and press **+** ("New daily routine"); name it `baker`.
3. Parts of the day (**+ Part of the day** for each):
   - from `05:00`: **stay at**, here, `bakery-oven`;
   - from `11:00`: **wander around**, radius `4`, here, `market-square`;
   - from `21:00`: **sleep at**, here, `baker-bed`.
4. **Save**: it writes `schedules/baker.json`:

```json
{ "name": "baker", "entries": [
  { "from": "05:00", "activity": "stay", "place": "bakery-oven" },
  { "from": "11:00", "activity": "wander", "place": "market-square", "radius": 4 },
  { "from": "21:00", "activity": "sleep", "place": "baker-bed" }
] }
```

5. Select the baker and choose **Routine** from **+ Add component**: **schedule** `schedules/baker.json` (picked from the list),
   **speed** `1.5`.

In play she stands at the oven from five, strolls the square from eleven, and at nine goes to bed and disappears (no
talking to her) until five.

Questions: how do I give an NPC a daily routine? what's the difference between a routine and a package? why can't I talk
to a sleeping NPC?

## Markers and the game clock

**Markers** are entities with a Marker component (`name`): named places, invisible in play and labelled in the editor.
Routines, packages (location.marker, home, work, bed), patrols, quests (reach, escort), cutscenes and gateways use
them. Place one from the Inspector, or ask the AI.

**The game clock** starts at **Time.startHour** and runs at **Time.minutesPerSecond** game minutes per real second
(Project Settings). It shows top-right in play when anyone has a routine. Scripts read it with `time_of_day()` (hours,
e.g. 13.5) and `day()` (from 1). Package schedules and the "time of day" and "day" conditions use it.

### Example: a day that starts at dawn and lasts 24 real minutes

1. In Project Settings, set **Time.startHour** `6` and **Time.minutesPerSecond** `1` (one game hour per real minute).
2. Place a Marker in the square and set its **name** to `town-bell`.
3. A script on the bell tower rings the hours:

```lua
local last = -1
every(1, function()
  local hour = math.floor(time_of_day())
  if hour ~= last then
    last = hour
    if hour == 12 then play_sound("assets/audio/bell.wav") end
    if hour == 6 then notify("toast", "Day " .. day() .. " begins.") end
  end
end)
```

The game opens at 06:00; noon comes six real minutes later, and the bell rings. The clock shows top-right once anyone
has a routine.

Questions: how do I add a marker? how fast does the game clock run? how do I change the starting hour?

## Cutscenes: the timeline editor

A cutscene is `cutscenes/<id>.json`: a timeline of tracks with keys at times. Open **Project ▸ Effects** and its
cutscenes section.

- The list shows each cutscene with its length (or ⚠ with problems). Type an id in "new cutscene id" and press + to make
  one (it starts with one dialogue key).
- At the top: the id, a **name**, **letterbox** (black bars), **Save**, and **▶ Play** (saves, then plays the game with
  it).
- The **timeline** shows a ruler in seconds and a row per track; each key is a block at its time (`at`), as wide as its
  `seconds`. Click a key to edit it.
- **Add a track**: actor, dialogue, camera, audio, screen, logic, effect. A track's **+** adds a key after its last; ×
  removes the track. An actor track has a box for who it moves.
- The selected key is edited as JSON (the ops it can use are listed); **Apply** takes it (a key needs `at`), **Remove
  key** deletes it.

The world waits while a cutscene plays: no input, no AI, no combat. **Esc**, **Space** or **Enter** skips it: every key
left is applied at once (actors jump to their marks, logic runs), so the story ends up the same.

### Example: making the "rook-arrives" cutscene

1. Open **Project ▸ Effects** and go to its cutscenes section.
2. Type `rook-arrives` in "new cutscene id" and press **+**. It starts with one dialogue key.
3. **name** `Captain Rook arrives`; tick **letterbox**.
4. Press **Add a track ▸ actor**, and type `character/captain-rook` in its box. Press the track's **+**, click the new
   key and type `{"at": 0, "op": "move", "marker": "inn-door", "speed": 1.5, "seconds": 3}`; press **Apply**.
5. Click the dialogue key; set it to `{"at": 3, "speaker": "character/captain-rook", "text": "I need a word, stranger.",
   "seconds": 3}`; **Apply**.
6. Press **Save**, then **▶ Play**.

Rook walks to the inn door for three seconds, then his line shows. Press **Esc** in play and he jumps to the door with
the line skipped.

Questions: how do I make a cutscene? how do I edit cutscene keys? can the player skip cutscenes?

## Cutscene tracks and keys

| track | keys |
|---|---|
| actor (with `actor`) | `move` (to a `marker` or `x`/`y`, at `speed`), `place` (jump there), `face` (a marker, an actor or "player"), `show`, `hide`, `tween`, `animate` |
| dialogue | a line: `speaker`, `text`, `seconds`, `voice` |
| camera | `follow` (an `actor`), `move` (to a marker or x/y), `zoom` (`value`), `shake` |
| audio | `music` (`path`; "" for the scene's own), `sound` (`path`) |
| screen | `fade_out`, `fade_in` (`seconds`), `title` (`text`, `seconds`) |
| logic | `do` (effects, as dialogue's), `emit` (`event`, for scripts), `wait_input` (holds until a key) |
| effect | `play` |

Actors are named as quests name people: an Actor key, a World Bible key, an entity's Name, "player", or "@alias" (from
the quest that started it, else any active quest with that alias). `length` sets the running time when it should outlast
its last key.

Example: fade in over 1 s; title "Later that night" at 0.5 s; actor `@thief` moves to marker `cells` at 1 s; camera
follows `@thief`; at 4 s a dialogue line "Fine. You win."; at 6 s wait for input, then `set_stage thief 30`.

### Example: "Later that night", the thief's capture

`cutscenes/thief-caught.json`:

```json
{
  "name": "Later that night",
  "letterbox": true,
  "tracks": [
    { "type": "screen", "keys": [
      { "at": 0, "op": "fade_in", "seconds": 1 },
      { "at": 0.5, "op": "title", "text": "Later that night", "seconds": 2 } ] },
    { "type": "actor", "actor": "@thief", "keys": [
      { "at": 1, "op": "move", "marker": "cells", "speed": 1.2, "seconds": 3 } ] },
    { "type": "camera", "keys": [ { "at": 1, "op": "follow", "actor": "@thief" } ] },
    { "type": "dialogue", "keys": [
      { "at": 4, "speaker": "@thief", "text": "Fine. You win.", "seconds": 2 } ] },
    { "type": "audio", "keys": [ { "at": 0, "op": "music", "path": "assets/audio/night.ogg" } ] },
    { "type": "logic", "keys": [
      { "at": 6, "op": "wait_input" },
      { "at": 6.1, "do": [{ "op": "set_stage", "quest": "stolen-ring", "stage": 30 }] } ] }
  ]
}
```

The screen fades in under night music, the title shows, the thief (whoever fills the quest's `thief` role) walks to the
cells with the camera following, speaks, and after the player presses a key the quest moves to stage 30.

Questions: what can a cutscene do? how do I move a character in a cutscene? how do I show a title card? how do I run quest
effects at the end of a cutscene?

## Playing cutscenes

A cutscene plays from:
- a `play_cutscene` effect (a dialogue choice, a quest stage's "On reaching it", a Reaction's `do`);
- Lua `play_cutscene(id)` (false if it doesn't load); `skip_cutscene()` and `in_cutscene()`;
- a **CutsceneTrigger** component: `cutscene`, `radius` (how close the player must come, in cells), and `once` (only the
  first time, remembered in saves as the flag `cutscene:<id>`).

Scripts hear `cutscene_started` and `cutscene_ended` (text: the cutscene's id).

### Example: the cutscene plays once when the player reaches the inn

1. Place an empty entity at the inn's door and choose **CutsceneTrigger** from **+ Add component**: **cutscene**
   `rook-arrives`, **radius** `3`, **once** ticked.
2. To play another from a quest stage, add to its **On reaching it**: `{"op": "play_cutscene", "cutscene": "thief-caught"}`.
3. A script can follow up when it ends:

```lua
on("cutscene_ended", function(e)
  if e.text == "rook-arrives" then
    start_quest("quiet-the-courtyard")
  end
end)
```

Walking within three cells of the door plays `rook-arrives`; the flag `cutscene:rook-arrives` is set (and saved), so it
never plays again, and the quest starts when it ends.

Questions: how do I play a cutscene when the player walks somewhere? how do I play a cutscene from dialogue? does a
cutscene play again?
