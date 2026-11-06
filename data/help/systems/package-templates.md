# Package templates

Every built-in AI package template (Project ▸ Records ▸ package ▸ template): what the character does, the data it
takes, and the defaults used when a data field is left empty. The Records form shows each template's steps in words
under **What <template> does**.

## stay, travel, guard, wait and hide

Simple "be somewhere" templates. All use the package `location`.

- **stay:** walks to the location, then stands there. No data.
- **travel:** walks to the location and stays there once arrived.
- **guard:** walks to its post (the location) and stands there; the same steps as stay. Fighting intruders comes from
  the Behaviour component's aggression and perception, not from the template.
- **wait:** stands where it is. No data, no location needed. Use it with a condition ("wait here until the flag
  is set").
- **hide:** walks to the location, then stays hidden (invisible) there.

If the location can't be reached, the walk gives up after the character's stuck recovery (see "Behaviour component")
and the package carries on.

Example: a sentry package: template guard, location marker `gate-post`, schedule 06:00-22:00.

### Example: a sentry at the gate, and a thief who hides until dark

1. In Project ▸ Records, press **+** on the **package** group; **Id** `sentry-gate`.
2. **template** `guard`; **location ▸ marker** `gate-post`; **schedule ▸ from** `06:00`, **to** `22:00`. **Create**.
3. Make `thief-hide`: **template** `hide`, **location ▸ marker** `alley-crates`, **schedule** from `06:00` to `20:00`.
4. Put `sentry-gate` in Corporal Hale's Behaviour **packages**, and `thief-hide, thief-wander` in Sly Ned's.

```json
{ "template": "guard", "location": { "marker": "gate-post" }, "schedule": { "from": "06:00", "to": "22:00" } }
```

By day Hale stands at the gate post and Ned is invisible behind the crates; at 20:00 Ned appears and starts wandering.

Questions: how do I make a character stand in one place? what does the guard template do? how do I hide a character?

## wander

Strolls around the location, pausing between walks, again and again.

Steps: pick a free cell within `radius` of the location and walk to it (giving up after 12 seconds, or at once if there
is no way there), then wait between `waitMin` and `waitMax` seconds.

| data | default |
|---|---|
| `radius` | 3 cells |
| `waitMin` | 2 s |
| `waitMax` | 5 s |

Example: a chicken: template wander, location marker `yard`, data radius 4, waitMin 1, waitMax 3.

### Example: chickens in the farmyard

1. Place a Marker in the farmyard and name it `yard`.
2. In Project ▸ Records ▸ package, press **+**; **Id** `chicken-yard`; **template** `wander`; **location ▸ marker**
   `yard`; **data**: **radius** `4`, **waitMin** `1`, **waitMax** `3`. **Create**.

```json
{ "template": "wander", "location": { "marker": "yard" }, "data": { "radius": 4, "waitMin": 1, "waitMax": 3 } }
```

3. Select the chicken prefab's instance, set Behaviour **packages** to `chicken-yard`, and apply to the prefab so every
   chicken gets it.

Each chicken pecks about within four cells of the marker, stopping one to three seconds between hops, never in step with
the others.

Questions: how do I make a character wander? how far does wander go? how do I change the pause between walks?

## sandbox

Lives in an area: uses its smart objects, wanders, and makes idle remarks, picking one at random each time by weight.

1. **Use a smart object** (weight `objectsWeight`, default 3): the nearest free smart object within `radius` of the
   location with an affordance sit, eat, drink, read, warm or pray, used for its own duration.
2. **Wander** (weight `outsideWeight`, default 2): a walk within `radius`, then a wait of `waitMin` to `waitMax`
   seconds (defaults 2 and 6).
3. **Say an idle line** (weight `barkWeight`, default 1): one of its Speech component's idle lines (only near the
   player), then a 3-6 second wait.

| data | default |
|---|---|
| `radius` | 3 cells (wander), 10 cells (smart objects) |
| `objectsWeight` / `outsideWeight` / `barkWeight` | 3 / 2 / 1 (0: never) |
| `waitMin` / `waitMax` | 2 / 6 s |

Example: tavern patrons: sandbox at building `tavern`, radius 6, barkWeight 0 for a quiet one.

### Example: patrons at the Golden Goose, one of them quiet

1. Give the inn's chairs and tables SmartObject components (chairs with sit, tables with eat and drink).
2. In Project ▸ Records ▸ package, press **+**; **Id** `patron-goose`; **template** `sandbox`; **location ▸ building**
   `golden-goose`; **data ▸ radius** `6`. **Create**.
3. **Duplicate** it as `patron-goose-quiet`, set **data ▸ barkWeight** `0` and **objectsWeight** `5`. **Create**.

```json
{ "template": "sandbox", "location": { "building": "golden-goose" },
  "data": { "radius": 6, "objectsWeight": 5, "barkWeight": 0 } }
```

4. Give the chatty patrons a Speech component with **idle** lines (`Another round!|This ale's gone flat.`).

The chatty patrons sit, eat, stroll and call out their lines when the player is near; the quiet one mostly sits and
never speaks.

Questions: what does sandbox do? how do I make characters sit and eat in an inn? how do I stop a character talking to
itself?

## live

Lives in and around a building: wanders round it, wanders in its rooms, and now and then uses something (an animated
part or a Reaction: a stove's "cook"). Set `location.building` to the house's name.

Each turn, one of three, by weight:
1. **Wander round the place** (weight `outsideWeight`, default 3): a walk within `radius` of the building, then a wait.
2. **Wander in its rooms** (weight `insideWeight`, default 2): a walk to a free cell inside the building's footprint,
   then a wait.
3. **Use something** (weight: its `actions`' weights added together; a single action counts 1, none 0): walk to a part
   from `actions` within `partRadius` (default 15) and use it for `duration` seconds (its own duration when it has one,
   else 5); if none is found, wait 2 seconds.

Waits are `waitMin` to `waitMax` (defaults 2 and 6). Example: `data.actions` = cook (weight 2), sweep (weight 1): uses
come 3 times in 8 against wandering's 3 + 2.

Older packages with a single `data.action` are moved into the `actions` list when opened in the Records form.

### Example: Vara Bell cooks and stays indoors

1. Make sure the inn (Name `golden-goose`) has a stove: an animated part or a Reaction whose input action or station is
   `cook`.
2. In Project ▸ Records ▸ package, press **+**; **Id** `vara-live`; **template** `live`; **location ▸ building**
   `golden-goose`.
3. **data ▸ actions**: one row, **action** `cook`, **weight** `2`. **data ▸ outsideWeight** `0`. **Create**.

```json
{
  "template": "live",
  "location": { "building": "golden-goose" },
  "data": { "actions": [{ "action": "cook", "weight": 2 }], "outsideWeight": 0 }
}
```

4. On Vara Bell, set Behaviour **packages** to `vara-live`.

Each turn she cooks with weight 2 against wandering her rooms with weight 2 (half the time each), and never goes round
the outside. Add `{"action": "sweep", "weight": 1}` and sweeping comes once in five turns.

Questions: how do I make a villager live in her house? how often does live cook? how do I make a character stay inside?

## use-part and tend

Templates for working at one thing again and again.

- **use-part:** walks to the animated part named in `actions` (each with a weight; the first tried is drawn by weight,
  the others tried if it isn't found) within `radius` (default 12) of the location, uses it as the player would, stays
  `duration` seconds (the part's own duration when it has one, else 5), turns it off again if it turned it on, then
  waits `waitMin` to `waitMax` (defaults 2 and 5). Nothing found: walks to the location and waits 6 seconds.
- **tend:** uses the Reaction (or Interactable, or else an animated part) named in `data.action` within `radius`
  (default 12): the nearest free one (not used up, not cooling down), walked to and used; then waits `waitMin` to
  `waitMax` (defaults 1 and 3). Nothing free: walks to the location and waits 6 seconds.

A part is found by any of its names: its input action or event, its station, its clip, the event it fires, its prompt's
words, or `door`.

Example: a farmer: template tend, location marker `fields`, data action `farm`, radius 12; a second package with action
`interact` harvests grown fields.

### Example: a miller who works the mill wheel, and a farmer who tends the fields

1. The mill has an animated part (the wheel's lever) whose input action is `mill`.
2. In Project ▸ Records ▸ package, press **+**; **Id** `miller-work`; **template** `use-part`; **location ▸ building**
   `mill`; **data ▸ actions**: `mill` weight 1; **data ▸ duration** `20`. **Create**.
3. Make `farmer-tend`: **template** `tend`; **location ▸ marker** `fields`; **data ▸ action** `farm`; **data ▸ radius**
   `12`. **Create**.

```json
{ "template": "tend", "location": { "marker": "fields" }, "data": { "action": "farm", "radius": 12 } }
```

The miller walks to the lever, pulls it, stays 20 seconds while the wheel turns, turns it off and rests 2-5 seconds.
The farmer goes from one free field to the next within 12 cells of the marker, working each one.

Questions: how do I make a character use a stove? how do I make a farmer work the fields? what's the difference between
use-part and tend?

## sleep, eat, work, vendor and use-object

Templates built on smart objects (entities with a SmartObject component: beds, chairs, tables, counters, forges).

- **sleep:** finds a free `bed` smart object with the sleep affordance within `radius` (default 12) of the character's
  `bed` marker (else its home), lies in it hidden until the package ends. No bed: walks to the location and stays
  hidden.
- **eat:** uses a smart object with the eat affordance within `radius` (default 10) of the location; none free: walks
  to the location and waits `duration` seconds (default 10).
- **work:** uses a smart object of type `data.object` (counter, forge, desk) with the work affordance within `radius`
  (default 10), again and again; none: walks to the location and waits `duration` (default 8).
- **vendor:** stands at a `counter` smart object with the work affordance within `radius` (default 10) for the whole
  package; none: walks to the location and stands.
- **use-object:** uses a smart object of type `data.object` within `radius` (default 12) of the location.

A smart object is used only when free: fewer users than its `slots`, and either no `owner` or this character as owner.

### Example: Brannoc's day at the forge, supper and bed

1. Give the smithy's forge a SmartObject (type `forge`, affordance work), the table one with eat, and Brannoc's bed one
   with sleep (type `bed`, **owner** Brannoc).
2. In Project ▸ Records ▸ package make three records:
   - `brannoc-work`: **template** `work`, **location ▸ building** `smithy`, **data ▸ object** `forge`, **schedule**
     `07:00`-`18:00`.
   - `brannoc-eat`: **template** `eat`, **location ▸ alias** `home`, **schedule** `18:00`-`19:00`.
   - `brannoc-sleep`: **template** `sleep`, **schedule** `22:00`-`06:00`.
3. On Brannoc, set Behaviour **packages** to `brannoc-sleep, brannoc-eat, brannoc-work`, **home** `brannoc-house`,
   **bed** `brannoc-bed`.

```json
{ "template": "work", "location": { "building": "smithy" }, "data": { "object": "forge" },
  "schedule": { "from": "07:00", "to": "18:00" } }
```

He works the forge all day, eats at six, and at ten lies in his own bed (hidden) until morning. No one else uses that
bed, because it's owned.

Questions: how do I make a character sleep in a bed? why won't my NPC use the counter? how do I make a shopkeeper stand
behind the counter?

## patrol and follow-path

Templates that walk routes.

- **follow-path:** walks a path drawn with the Path tool, by name in `data.route`. `data.mode`: `once` (to an end, or
  once round, then it stays there), `loop` (on and on), or `wander` (any way at every point, links walked both ways,
  never stopping: town folk). `data.from`: `nearest` (default), `start` or `end`. It stops where the path's points say
  (wait, a clip, a part used, effects) and chooses at forks by conditions, priority, weights or the default.
- **patrol:** with `data.route`, walks the named path for ever (as mode loop). With `data.path` instead (a list of
  marker names, the older way), walks the markers in order, waiting `data.duration` seconds at each (default 2), and
  starts over.

Example: a night watchman: template patrol, data route `market-loop`, schedule 20:00-06:00.

### Example: a watchman on the market loop, and a townswoman strolling the streets

1. Draw a closed path with the Path tool round the market and name it `market-loop`; draw an open network of streets
   named `town-streets`.
2. In Project ▸ Records ▸ package, press **+**; **Id** `watch-market`; **template** `patrol`; **data ▸ route**
   `market-loop`; **schedule** from `20:00` to `06:00`. **Create**.
3. Make `stroll-town`: **template** `follow-path`; **data ▸ route** `town-streets`; **mode** `wander`; **from**
   `nearest`. **Create**.

```json
{ "template": "follow-path", "data": { "route": "town-streets", "mode": "wander", "from": "nearest" } }
```

The watchman circles the market all night, pausing wherever the path's points tell him to; the townswoman joins the
street path at its nearest point and takes any way at each fork, for ever.

See also: Paths.

Questions: how do I make a guard patrol? how do I make a character walk a path? what are the follow-path modes?

## follow, accompany, escort and flee

Templates about another character.

- **follow:** follows `target` (player, a character's name or World Bible key, or a marker), keeping `data.distance`
  cells (default 2); it hurries when more than 4 cells further behind.
- **accompany:** follows the player at `data.distance` (default 2), for companions. Fighting beside the player comes
  from the Behaviour's aggression and assistance.
- **escort:** leads `target` to the location, waiting while the target is more than `data.distance` cells away (default
  4); it ends on arrival.
- **flee:** runs from `target` until `data.distance` cells away (default 8).

### Example: a guide who leads the player to the ruins, then joins as a companion

1. In Project ▸ Records ▸ package, press **+**; **Id** `elda-lead`; **template** `escort`; **target** `player`;
   **location ▸ marker** `ruins-entrance`; **data ▸ distance** `5`; **conditions**: quest `the-old-ruins` in state
   `active`. **Create**.
2. Make `elda-companion`: **template** `accompany`; **data ▸ distance** `2`; **conditions**: flag `elda_joined`.
3. On Elda, set Behaviour **packages** to `elda-lead, elda-companion, elda-home`.

```json
{ "template": "escort", "target": "player", "location": { "marker": "ruins-entrance" },
  "data": { "distance": 5 }, "conditions": [{ "quest": "the-old-ruins", "questState": "active" }] }
```

While the quest is active, Elda walks towards the ruins and stops whenever the player is more than five cells behind.
Once a dialogue sets `elda_joined`, she follows the player two cells back and fights beside them.

Questions: how do I make a companion? how do I make a character lead the player somewhere? how do I make someone run
away from the player?

## ambush, approach-player and script

- **ambush:** walks to the location and lies low there (drawn faint). When a player hostile to it comes within
  `data.radius` cells (default 3), it shows itself and attacks (combat takes over).
- **approach-player:** walks up to the player (within about 1.3 cells) and starts the dialogue in `data.dialogue`. Give
  the package a condition that stops it once talked (the dialogue sets a flag, the package requires the flag not set),
  or it approaches again later.
- **script:** runs the Lua file in `data.script`: its `run()` function, as a coroutine, while the package is active. See
  "Scripting" (scripted packages).

### Example: bandits lying in wait, and a beggar who comes to talk

1. In Project ▸ Records ▸ package, press **+**; **Id** `bandit-ambush`; **template** `ambush`; **location ▸ marker**
   `forest-bend`; **data ▸ radius** `4`. **Create**. Put it in the roadside bandits' Behaviour packages.
2. Make `beggar-approach`: **template** `approach-player`; **data ▸ dialogue** `dialogue/beggar.json`; **conditions**:
   flag `met_beggar`, **flagValue** unticked. **Create**.
3. In `dialogue/beggar.json`, have the first line's effects set the flag: `{"op": "set_flag", "flag": "met_beggar"}`.

```json
{ "template": "approach-player", "data": { "dialogue": "dialogue/beggar.json" },
  "conditions": [{ "flag": "met_beggar", "flagValue": false }] }
```

The bandits stand faint at the bend and leap out when the player comes within four cells. The beggar walks up once and
starts his dialogue; after it sets `met_beggar` he no longer approaches.

Questions: how do I make bandits ambush the player? how do I make an NPC walk up and talk to the player? how do I write a
package in Lua?

## custom, and copying a template

- **custom:** the package runs its own `procedure`, a behaviour tree written as JSON in the Records form's procedure
  editor (checked as you type, with an outline in words). See "Custom procedures".
- **Copy into a custom package…** (under a template's outline in the Records form) copies the current template's steps
  into the package's procedure and switches the template to custom, so you can change any step: add a `say` between
  walks, change a weight, add a cooldown. The package's data stays, so the copied `@data:` parameters keep working.

Templates can also be replaced for a whole project: a `procedure` record with a template's name is used instead of the
built-in one.

### Example: making Vara hum while she cooks

1. Open the package `vara-live` (template `live`) in Project ▸ Records ▸ package.
2. Under **What live does**, press **Copy into a custom package…**. The template becomes `custom`, and **procedure**
   now holds live's steps with `@data:` parameters such as `"@data:waitMin=2"`.
3. In the procedure editor, find live's third choice, the `selector` whose first child is the `use-part` step, and wrap
   that step in a sequence with a `say` after it:

```json
{ "type": "selector", "children": [
  { "type": "sequence", "children": [
    { "type": "use-part", "part": "@data:action", "parts": "@data:actions", "around": "@location",
      "radius": "@data:partRadius=15", "seconds": "@data:duration" },
    { "type": "say", "text": "La la la...|Nearly done!" }
  ] },
  { "type": "wait", "seconds": 2 }
] }
```

4. Check the outline in words underneath, then press **Save**.

Vara behaves as before, but after each cooking she says one of the two lines over her head when the player is near. Her
`data` (cook weight 2, outsideWeight 0) still applies.

Questions: how do I change what a template does? how do I make my own package steps? can I edit a built-in template?
