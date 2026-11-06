# Behaviour component and character AI

The components that make a character act on its own: Behaviour (packages, home/work/bed, how it fights), Perception,
SmartObject, Speech and Actor; how characters notice, fight, flee and search; how they walk, open doors, and recover
when stuck.

## The Behaviour component

Add **Behaviour** in the Inspector to a character to give it AI packages. Its fields:

| field | meaning |
|---|---|
| `packages` | package record ids in order; the first whose schedule and conditions hold runs |
| `home`, `work`, `bed` | markers: where `@home`, `@work` and `@bed` are (bed: else the nearest free bed) |
| `aggression` | Peaceful: never fights. Defensive: fights when attacked. Aggressive: attacks enemies it notices. Frenzied: attacks anyone |
| `confidence` | how soon it runs when hurt: Cowardly, Cautious, Average, Brave, Foolhardy |
| `assistance` | who it helps in a fight: Nobody, Allies, Friends |
| `combatStyle` | a combat-style record (empty: melee, runs at 20% health) |
| `speed` | walking speed in cells per second (default 1.6) |

The **packages** field is a list editor: each package with its number in the order, its template, ↑ and ↓ to move it
("Earlier: checked before the ones below"), ✎ to open it in Project ▸ Records, × to remove it from this character, and
"+ Add a package…" (a searchable list of the project's packages with their template and description). A package id with
no record shows "missing".

On a prefab, set Behaviour on the prefab so every copy shares it.

### Example: setting up Vara Bell, the innkeeper

1. Select Vara Bell in the Hierarchy and in the Inspector, choose **Behaviour** from **+ Add component**.
2. In **packages**, press **+ Add a package…** and pick `vara-sleep`, then `vara-work`, then `vara-live`. Use ↑ and ↓
   so the order is 1 `vara-sleep`, 2 `vara-work`, 3 `vara-live`.
3. Set **home** to the marker `vara-room`, **work** to `goose-counter` and **bed** to `vara-bed`.
4. **aggression** `Defensive`, **confidence** `Cautious`, **assistance** `Allies`, **speed** `1.4`.
5. Press ✎ beside `vara-work` to check it in Project ▸ Records.

In Play she sleeps at night, serves by day and lives in the inn otherwise; she only fights if attacked, and runs early
when hurt.

Questions: how do I give a character AI? what are home, work and bed? how do I reorder a character's packages? what does
aggression do?

## Perception: how characters notice the player

The **Perception** component sets what a character notices: `sight` (cells, default 7), `angle` (the cone it sees in,
degrees, default 140) and `hearing` (cells it hears an ordinary noise from, default 6). Without it, a character sees 6
cells in a 140° cone and hears 6 cells.

- Only a player hostile to the character (by team, faction or disposition) is watched for.
- **Sight:** within range, inside the cone the character faces (anything within 1 cell is seen regardless), with no
  solid cell in between. Awareness rises faster the closer the player is.
- **Hearing:** noises (swings, shots, doors) within hearing range times their loudness raise awareness to at least 0.6
  and mark where it came from.
- Unnoticed, awareness drains slowly.
- At full awareness an **Aggressive** or **Frenzied** character goes into combat, says its Speech `alert` line, and calls
  for help (allies within its combat style's `callForHelp` cells, default 6, that aren't Peaceful and whose assistance
  isn't Nobody join in).

Perception's sight and hearing are scaled by difficulty (the stealth category).

Lua: `awareness_of([id])` returns 0-1.

### Example: a sharp-eyed watchtower archer and a dozing gate guard

1. Select the archer on the watchtower; **+ Add component ▸ Perception**: **sight** `14`, **angle** `200`, **hearing**
   `8`. In Behaviour set **aggression** `Aggressive`.
2. Select the gate guard; **+ Add component ▸ Perception**: **sight** `4`, **angle** `90`, **hearing** `3`;
   **aggression** `Aggressive`, **assistance** `Allies`.
3. Give the archer a Speech **alert** line: `Intruder at the gate!`.
4. A script on the guard can show how close he is to noticing:

```lua
every(1, function()
  if awareness_of() > 0.5 then say("Hm? Who's there?") end
end)
```

In Play the archer spots the player from 14 cells and shouts; the guard and other allies within six cells join in.
Sneaking past the guard's back works, since he sees only 90° in front of him.

Questions: how far do enemies see? how do I make guards notice the player? why doesn't my enemy attack? how do I make
enemies call for help?

## Combat, fleeing and searching

When combat starts (noticed while Aggressive or Frenzied, called for help, or attacked), the package pauses and the
character fights:

- **Attacked:** a Peaceful or Cowardly character flees; others fight back.
- **Melee** (the default, or a style with range melee, or no RangedAttack): it closes in at a run and swings its
  MeleeAttack when in reach.
- **Ranged and kiting** (a combat style with range ranged or kiting, and a RangedAttack): it keeps `keepDistance` cells
  (default 60% of its range), backing off or closing in, and shoots.
- **Fleeing:** below its flee threshold of health it runs (Speech `flee` line). The threshold is the combat style's
  `fleeAt` (default 0.2), changed by confidence: Cowardly ×2.5 + 0.2, Cautious ×1.5, Average as is, Brave ×0.5,
  Foolhardy never. It stops fleeing at 10 cells away.
- **Searching:** with the target out of sight for 6 seconds, it walks to where the target was last seen, looks for a
  few seconds, then returns to its package.
- A dead or vanished target ends combat; the package resumes.

Characters with an **Abilities** component also cast by their abilities' AI hints (see "Combat, abilities and magic").

### Example: a wolf that flees when hurt, and a brave bandit chief

1. Select the wolf; in Behaviour set **aggression** `Aggressive` and **confidence** `Cautious`; leave **combatStyle**
   empty (melee, fleeAt 0.2).
2. Select the bandit chief; **aggression** `Aggressive`, **confidence** `Foolhardy`.
3. Press Play and fight both.

The wolf closes in and bites; below 30% health (0.2 × 1.5) it runs, its Speech `flee` line showing, until 10 cells away.
The chief fights to the death. Break line of sight behind a house for six seconds and either one walks to where it last
saw you, looks round, then goes back to its package.

Questions: how do I make an enemy run away when hurt? how does a ranged enemy fight? what happens when an enemy loses
sight of the player?

## Smart objects: beds, chairs, counters

An entity with a **SmartObject** component is something characters use. Packages (sleep, eat, work, vendor,
use-object, sandbox, and the `use-object` step) find them by type and affordance.

| field | meaning |
|---|---|
| `type` | bed, chair, table, counter, anvil, fireplace, well, bookshelf, altar… |
| `affordances` | what can be done with it, comma-separated: sleep, sit, eat, drink, work, read, pray, warm |
| `slots` | how many can use it at once (default 1) |
| `owner` | only this character uses it (a name or World Bible key; empty: anyone) |
| `minDuration`, `maxDuration` | a use lasts between these seconds (defaults 4 and 12) |

A character claims an object as it walks to it, so two don't head for the same chair. Using a sleep object puts the
character in it, hidden, until the package ends.

Built places give their objects affordances from their catalog role: bed → sleep; chair, stool, bench, throne → sit;
pew → sit, pray; table → eat; counter, workbench, anvil, stall, training-dummy, weapon-rack → work; desk → work, read;
fireplace → warm; stove → work, warm; campfire → warm, eat; well, fountain, trough → drink; bookshelf, lectern → read;
altar, shrine → pray.

### Example: Vara's own bed and a bench for two

1. Select the bed in Vara's room; **+ Add component ▸ SmartObject**: **type** `bed`, **affordances** `sleep`, **slots**
   `1`, **owner** `Vara Bell`.
2. Select the bench by the fire: **type** `bench`, **affordances** `sit`, **slots** `2`, **minDuration** `10`,
   **maxDuration** `30`.
3. Give Vara a `sleep` package (22:00-06:00) and the patrons a `sandbox` package at the inn.

At night Vara walks to her bed and lies in it, hidden; no patron can take it, since it's hers. Two patrons at a time sit
on the bench for 10 to 30 seconds; a third looks for another seat.

Questions: how do I make a bed an NPC can sleep in? what is a smart object? how do I reserve a bed for one character?

## Speech lines (the Speech component)

The **Speech** component (`Barks` in files) holds what a character says by itself, over its head (Project Settings ▸
UI ▸ speech: bubble, subtitle or both). Each field can hold alternatives separated by `|`:

- `alert`: when it notices the player as an enemy;
- `hit`: when it's hurt;
- `idle`: now and then, near the player (within about 5 cells, every 25-50 seconds; also the sandbox template's
  remarks and a `say` step's `@idle`);
- `flee`: when it runs.

Example idle: "Lovely weather.|Mind the mud.|Have you seen my cat?"

Lua `say(text)` (old name `bark`) and the effect `{"op": "say", "text": "..."}` speak from scripts and rules.

### Example: a goblin with a battle cry and a fishwife who chats

1. Select the goblin; **+ Add component ▸ Speech**: **alert** `Shinies! Get it!|Stabby time!`, **hit** `Ow!|Yeep!`,
   **flee** `Run away!`.
2. Select Old Marta at the fish stall; **Speech ▸ idle** `Fresh herring!|Mind the mud.|Have you seen my cat?`.
3. In Project Settings ▸ UI, set **speech** to `both` (bubble and subtitle).
4. To make Marta speak from a quest step, add the effect:

```json
{ "op": "say", "text": "You found my cat!", "who": "Old Marta" }
```

The goblin shouts one of its alert lines when it spots the player, yelps when hit, and cries out when it runs. Marta
calls out one of her lines every half minute or so when the player is near.

Questions: how do I make NPCs say random lines? how do I give an enemy a battle cry? where are speech bubbles set?

## The Actor component: one character across scenes

The **Actor** component marks the same character in every scene: its `key` (a World Bible key or a unique name) and the
`prefab` it's made from when it walks into a scene it wasn't placed in. When a package's location names another scene
(`location.scene`), the character is hidden in the current one while that package is active.

### Example: Tomas the farmer, at the farm by day and at the town market on Saturdays

1. Select Tomas in `scenes/farm.aige`; **+ Add component ▸ Actor**: **key** `tomas`, **prefab**
   `prefabs/tomas.prefab`.
2. Make the package `tomas-market` (template `stay`, **location ▸ building** `market-stall-2`, **location ▸ scene**
   `scenes/town.aige`, **schedule** `10:00`-`14:00`, **days** `6`) and put it first in his Behaviour packages.
3. Don't place Tomas in the town scene.

On day 6 at 10:00 Tomas disappears from the farm; walk to town and he's there at stall 2, made from his prefab. At 14:00
he's back at the farm.

Questions: how do I keep a character the same across scenes? what does Actor's key do?

## Walking, doors and stairs

Characters plan their way on the scene's grid (A*) and walk it at their Behaviour `speed`:

- **Doors:** a closed Door on the way counts as open for planning; the character uses it when it gets there, as the
  player would (a locked one opens with a key from the character's own inventory). A building's animated door opens as
  a character comes to it, and the path goes through the building's doorway. If a door stays shut for 3 seconds, the
  character looks for another way. A locked animated door without its key blocks the way.
- **Walls:** paths go round a painted room's thin walls and in through its doorways, never through a wall.
- **Heights:** no step up or down a cliff except where a **Ramp** component is (stairs, a ramp, a ladder).
- A character asleep in its bed doesn't open doors.

### Example: a jailer who unlocks the cell door with his key

1. Select the cell door; it has a **Door** and an **Interactable** with **locked** ticked and **key** `cell-key`.
2. Select Jailer Grubb; in Inspector ▸ Inventory set **items** to `cell-key`.
3. Give Grubb a `stay` package with **location ▸ marker** `inside-cell`.
4. For the upper walkway, select the stairs and **+ Add component ▸ Ramp** so characters can climb there.

In Play Grubb plans through the door as if it were open, unlocks and opens it when he reaches it, and walks into the cell.
A villager sent to the same marker without the key stands at the door for 3 seconds, then looks for another way.

Questions: can NPCs open doors? why does my NPC stand at a door? can characters use keys? how do characters climb
between heights?

## Stuck recovery

A character that stays near one spot while trying to reach a goal recovers in stages (walking the long way round a
building to its door isn't "stuck", since it keeps moving):

1. after 1.5 seconds it plans its way again;
2. after 3 seconds it's moved onto the next open cell of its way (never through a wall);
3. after 5 seconds it steps out to the nearest open spot within two cells;
4. after 8 seconds, if the player can't see it (not within 10 cells of both it and its goal), it's put at the goal;
   otherwise it gives the goal up, the step walking there fails, and the package does something else.

A character caught on the corner of a narrow doorway slips along its planned way rather than pushing against the corner.

If characters often get stuck in one place, check the building's footprint and doorway (Edit building), Obstacles in the
way, and that the goal isn't inside a solid cell.

### Example: the baker keeps getting stuck at the bakery door

1. Press Play and **F4**. The baker's label shows `baker-work`, but she shuffles at the bakery's corner, then appears
   inside (she was put at her goal after 8 seconds, out of the player's sight).
2. Stop Play. Select the bakery and choose **Edit building**: its doorway is drawn one cell left of the door picture, so
   the path leads into the wall.
3. Move the doorway under the door and save.
4. Also check the Hierarchy for an Obstacle (a barrel) placed in front of the door, and move it aside.

In Play she now walks in through the door. If the player stands close by, she'd have given up instead, and her package
would have moved on to its next step.

Questions: what happens when an NPC gets stuck? why did my character teleport? why does my character give up walking
somewhere?
