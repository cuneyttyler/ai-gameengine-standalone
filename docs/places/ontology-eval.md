# Ontology check: how 32 scene prompts are read

Date: 2026-10-21. `tests/prompts/scene_prompts.txt` holds 32 prompts for different kinds of
space; `LiveBuildTest.ReadThePrompts` (`AIGE_BRIEF_EVAL="<prompts>|<out>|<port>"`) reads each by
the engine's own words (`NamedBuildType`, `FallbackDraft`: free) and the starred ones once by
the AI (`/places/interpret`, one reading: 11 calls). Results: `tests/quality/briefs-*.json`.

## 1. What was wrong

| Prompt | Read as | Why | Fixed |
|---|---|---|---|
| a **small** courtyard with an inner chamber | dungeon | "small" is a dungeon size class: a size word counted as naming a build type | size words (small, medium, large, huge, tiny) name no build type |
| any courtyard | — | no build type for it | new build type `courtyard` (below) |
| the sewers under a city | settlement | settlement's rank (+10) beat the dungeon's word | the place named **first** is the subject (+3); rank only breaks ties |
| a stone castle with a keep, **a courtyard** | courtyard | a rank bonus | courtyard rank 0; castle named first |
| inside a temple; a wizard's tower, three floors | — | no words for them | interior: "inside", "tower" |
| a swamp, a graveyard, ruins, an oasis, an island, a mountain pass, a battlefield | settlement (the default) | no words for them | wilderness: swamp, marsh, bog, island, oasis, desert, mountain(s), pass, gorge, ruin(s), graveyard, cemetery, battlefield, jungle, glade, meadow; the wilderness can hold a graveyard |
| a formal palace garden | settlement | — | courtyard: "palace", "palace courtyard", "riad" |
| any prompt, without the AI | settlement | `FallbackDraft` kept its own copy of the scoring | one scorer (`NamedBuild`) |

After the fixes the engine alone gives each of the 32 prompts a sensible build type, and the AI
agrees with it on all 11 it read.

## 2. The courtyard

A build type of its own (`courtyard`, root `courtyard-complex`), laid out by the interior
engine: an open, paved **court** in the middle (a fountain, benches, plants, lights) and the
rooms round it. Its kinds and their rooms (`floor-level`'s programme):

| kind | ground floor |
|---|---|
| courtyard | court, hall, chamber, storage |
| cloister | court, nave, library, cells, storage |
| manor-court | court, living room, chamber, kitchen, storage |
| palace-court | court, great hall, chamber ×2, kitchen, storage |

New modules: `court` (the open court), `chamber` (an inner chamber). Biomes: `tropical`,
`mountain`.

## 3. What the AI's Briefs still show

- Features named that aren't modules: `underground-lake`, `bear-den` (a cave), `coast`, `hill`
  (an island, a castle, a city: features of the build type's list but not modules). The
  validator leaves them out; the ontology needs a cave's lake and a beast's den as dungeon
  rooms, and `coast` / `hill` as site features the layout knows.
- An island has nothing for its shipwreck and lighthouse: landmark modules for them.
- Its components came back empty from one reading (to check: the interpret answer may put them
  where the summary doesn't look).

These go with each kind of place's own pass (`docs/places/place-refine.md`'s method: the look of a
reference game, its features, built and measured), starting with dungeons and caves (Diablo).
