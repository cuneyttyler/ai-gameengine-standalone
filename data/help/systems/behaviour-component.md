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

Questions: how do I make NPCs say random lines? how do I give an enemy a battle cry? where are speech bubbles set?

## The Actor component: one character across scenes

The **Actor** component marks the same character in every scene: its `key` (a World Bible key or a unique name) and the
`prefab` it's made from when it walks into a scene it wasn't placed in. When a package's location names another scene
(`location.scene`), the character is hidden in the current one while that package is active.

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

Questions: what happens when an NPC gets stuck? why did my character teleport? why does my character give up walking
somewhere?
