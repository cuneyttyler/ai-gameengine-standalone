# AI packages

How characters act on their own with AI packages: what a package is, the package stack, where (location), whom
(target), when (schedule and conditions), flags, the package data and `@data` defaults, and how the AI panel writes
packages. Each template's details are in "Package templates"; writing your own steps is in "Custom procedures".

## What an AI package is

A **package** is a record (Project ▸ Records ▸ package, saved as `records/package/<id>.json`) that says what a character
does, where and when: "serve behind the counter from 08:00 to 22:00", "sleep in the bed at night", "wander round house-4
and sometimes cook", "follow the player".

A package has:
- `template`: the built-in behaviour it runs (stay, travel, wander, sandbox, live, sleep, eat, work, vendor, use-part,
  tend, patrol, follow-path, guard, follow, accompany, escort, flee, hide, ambush, wait, use-object, approach-player,
  custom, script). Required.
- `description`: what it's for, in a line.
- `location`: where (a building or thing by name, a character, a marker, an alias, a point, a scene).
- `target`: whom (for follow, escort, flee, custom and script).
- `schedule`: when (from/to times and days).
- `conditions`: only while these hold (story flags, quests).
- `flags`: mustComplete and others.
- `data`: the template's own parameters (radius, waits, weights, actions, route, distance…).
- `procedure`: its own steps, for template `custom` only.

A character uses packages through its **Behaviour** component's `packages` list. The same package can be shared by any
number of characters (each runs its own copy).

Questions: what is an AI package? how do I make a character do things on its own? where are packages stored?

## The package stack: which package runs

A character's Behaviour lists package ids in order ("innkeeper-work, innkeeper-evening, innkeeper-sleep"). The
**first** package whose schedule and conditions hold is the active one; the rest wait.

- The stack is looked at again about once a second, and immediately after a package is pushed or popped. When the
  chosen package changes, the character drops what it was using, forgets its place in the old procedure and starts the
  new one (the event `package_started` is sent).
- A package flagged **mustComplete** finishes its current run before another takes over.
- Packages **pushed** on top (by a quest's or a Reaction's `push_package` effect, or Lua `push_package(id, package)`)
  are checked before the Behaviour's list until popped (`pop_package`). Pushing the same package twice has no effect.
- When nothing holds, the character stands where it is.
- If the active package's location names another scene, the character is hidden in this one (it is "away").

Order matters: put narrow packages (a quest's escort, night-time sleep) above broad ones (an all-day wander), so the
broad one fills the gaps.

Example stack for an innkeeper: 1 `innkeeper-sleep` (22:00-06:00), 2 `innkeeper-work` (08:00-20:00), 3 `innkeeper-live`
(no schedule: everything else).

Questions: which package does a character run? why isn't my package running? how do I give a character a daily
schedule? how do I make a quest change what a character does?

## Package location: where it happens

`location` is where the package happens, used as "@location" by the template's steps (walk to it, wander around it,
look for things near it). It applies to stay, travel, wander, sandbox, live, sleep, eat, work, vendor, use-part, tend,
guard, hide, ambush, use-object, escort, custom and script. Fill one of:

- `building`: a building or thing by its Name ("house-4", a stall): around it, or in its rooms for templates that go
  inside (live, and wander steps with `inside`).
- `entity`: a character or thing by its name (or a character's World Bible key): near it.
- `marker`: a Marker's name.
- `alias`: the character's own place: `home`, `work` or `bed` (from its Behaviour component).
- `x`, `y`: a point in cells.
- `scene`: the scene it's in (empty: wherever the character is). In another scene the character is away from this one.

Pickers in the form list the scene's named things, markers and scenes. The place is found in that order: building or
entity, then alias, then marker, then the point; with none of them, the character's `home` marker, else where it
stands. Name places by things in the scene, never by guessed coordinates; renaming a thing breaks packages that name it.

Questions: how do I tell a package where to go? what is location.building? can a package send a character to another
scene? what are aliases home, work and bed?

## Package target: whom it follows or flees

`target` says whom: `player`, a character's name or World Bible key, or a marker. It's read by follow, escort, flee,
custom and script (accompany always follows the player).

- follow: walks after the target, keeping `data.distance` cells (default 2).
- escort: leads the target to the location, waiting when it falls more than `data.distance` behind (default 4).
- flee: runs from the target until `data.distance` cells away (default 8).
- custom: steps can use "@target".

Example: a dog package with template follow, target `Vara`, data.distance 1.5. A companion uses accompany, which follows
the player at `data.distance` (default 2).

Questions: how do I make a character follow the player? how do I make a character follow another character? what does
target do?

## Package schedule and conditions: when it runs

- **schedule:** `from` and `to` as HH:MM on the game clock (empty: all day; a range may wrap past midnight, 22:00 to
  06:00), and `days`: the days of the week 1-7 written as digits, for example `1,2,3,4,5` (empty or `all`: every day).
  Day 1 of the game is weekday 1.
- **conditions:** a list; all must hold. The form offers a story `flag` that is set (or not, with `flagValue` off), or a
  `quest` in a state (active, inactive, completed). The other condition forms dialogue uses (items, gold, level, time,
  day, attributes, faction standing, story variables, difficulty) also work when written in the record's JSON.

Example: "guard-night" with schedule 20:00-06:00 and the condition flag `alarm_raised` not set; "guard-alarm" above it
with the condition flag `alarm_raised` set. The guard switches as soon as the flag changes.

The game clock starts at Time.startHour and runs at Time.minutesPerSecond (Project Settings).

Questions: how do I make a package run only at night? how do I make a package run only on weekdays? can a package depend
on a quest? how do I write package conditions?

## Package flags

`flags` holds:
- `mustComplete`: the package finishes its current run before another package takes over (a delivery that shouldn't
  stop halfway). This is the flag the runtime acts on.
- `interruptible` (on by default) and `run` (runs rather than walks) are stored in the record; walking speed comes from
  the Behaviour component's `speed`.

Combat, fleeing and searching take over from any package when perception or an attack calls for it (see "Behaviour
component"); the package resumes afterwards.

Questions: what does mustComplete do? how do I stop a package being interrupted? how do I make a character run?

## Package data and "@data" defaults

`data` holds the template's own parameters. The form shows only those the current template reads, and the template's
outline (**What <template> does**) shows each value: the package's own, or "(data.<name>, the template's)" when it's left
empty and the template's default applies.

Inside templates and procedures, a parameter written `"@data:<name>"` reads the package's `data.<name>`;
`"@data:<name>=<default>"` falls back to the default when the package leaves it empty. For example live uses
`"@data:waitMin=2"`, `"@data:waitMax=6"`, `"@data:outsideWeight=3"`, `"@data:insideWeight=2"` and
`"@data:partRadius=15"`. The default may be a number, true or false, or text.

What counts as empty: missing, an empty text, an empty list, or 0. So setting `waitMin` to 0 gives the template's
default, not zero. Weights are the exception: a weight of 0 (outsideWeight, insideWeight, objectsWeight, barkWeight)
means never.

The same `@data:` syntax works in any procedure you write, so a custom package can take its own parameters.

Questions: what does @data mean? what happens if I leave a data field empty? why does waitMin 0 not stop the waiting?
how do I turn off wandering in live?

## The AI panel writes packages

Ask the AI panel, for example "make Vara wander around her house and cook" or "have the guard walk the market loop at
night". It:

1. Writes a `package` record (`write_record`) and puts its id in the character's `Behaviour.packages`. On a character
   made from a prefab it sets the prefab's field, so every copy gets it. It keeps the packages the character has unless
   asked to replace them; an edit writes the record again whole.
2. Prefers a template set by data, as the Creation Kit does: `live` with `location.building` and `data.actions` such as
   cook, or `follow` with `target` and `data.distance`. It writes its own `procedure` (template custom) when the steps
   call for it, and a `script` package when the request has an order, choices or recipes.
3. Can make an animation clip the character lacks and name it in the package.

Before you see the proposal, the AI service checks each package: a template has what it needs (an action for use-part,
live and tend; a target for follow, escort and flee; a path for patrol; a script for script), a custom procedure parses
with known steps, buildings and entities it names exist in the scene, routes name an existing path, and every package a
Behaviour lists exists or is written in the same answer. Review it in Diff Review like any change.

Questions: can the AI give a character a routine? how does the AI write packages? what does the AI check in packages?

## Debugging packages

- **F4 in Play** ("Show what every character is doing") shows each character's active package, or combat, fleeing,
  searching or idle.
- In Play, characters on a path have a green line to the point they're walking to.
- In Project ▸ Records, a package's ⚠ lists its problems; Inspector ▸ Behaviour marks a package id that doesn't exist as
  "missing" ("No such package: make it in Project ▸ Records ▸ package").
- Common problems: the package's schedule or conditions don't hold (an earlier package in the stack wins instead); its
  location names a thing that was renamed; a template is missing its data (a use-part package with no action, a follow
  package with an empty target); nothing usable is within the radius (a smart object, a part or a Reaction), so the
  template's fallback (walk to the place and wait) runs instead.

See also: Package templates, Behaviour component, Paths.

Questions: why is my character standing still? how do I see which package a character runs? how do I debug AI
behaviour?
