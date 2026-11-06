# Bigger tasks in the AI panel

How the AI panel carries out a request as a task: the working copy, the plan and steps, outlines you approve first, reviewing each step, the token budget, stopping, and generating characters and walks four at a time.

## What a task is

When a request is a new feature with parts that build on each other (a new kind of enemy with its own behaviour, a prefab for it and copies placed; a farm; a quest line; a village's people), the AI answers it as a **task**. It plans 2 to 6 steps and gives only the first step's operations.

The editor applies each step to a **working copy**: a clone of the open scene with the same entity ids, copies of the project's scripts, prefabs, World Bible, dialogue, quests, schedules and cutscenes, and its own project settings. It then plays the working copy headless for 2 seconds, and sends the AI the results (operations that failed, script errors) and the working copy as the new context. The AI answers with the next step or fixes, and says it's done on the last one.

Nothing reaches your project until you review the combined result: a normal proposal card headed "Result of N steps — review it all here; nothing has been applied yet." Accepting it replays every step on the real scene as one undo step.

### Example: a new enemy, built as a task

1. Open the scene with the old mine and type in the AI panel:

   > Add a cave bat enemy that swoops at the player and flees when hurt, and put four of them in the mine

2. Press **Enter**. A task card appears: "Working — step 1 of about 3", with a plan such as 1. the bat's script, 2. the Cave Bat prefab using it, 3. four copies placed in the mine.
3. Wait while each step finishes with ✓. Your scene doesn't change meanwhile: the steps go to the working copy.
4. When it says "Done in 3 steps", a proposal card headed "Result of 3 steps — review it all here; nothing has been applied yet." lists every change of all three steps.
5. Untick anything you don't want and click **Apply**.

The script, the prefab and the four bats arrive in your project at once; Ctrl+Z on the scene takes the four bats back as one step, and the card's **Undo** also removes the script and the prefab.

Questions: what is a task in the AI panel? does a task change my project while it works? how do I review a task's result?

## The task card

While a task works, its card shows:
- the title: "Working — step 3 of about 5", "Outline — waiting for you", "Done in 4 steps", or "Stopped";
- the AI's first reply in plain words ("I'm animating 21 characters with a walk, four at a time: 6 steps.");
- the plan, numbered;
- each step done, with its summary and ✓, or "N to fix" when its operations failed or its scripts had errors (the AI fixes those in the next step);
- the step being made right now, with what it's doing (art or walks take minutes);
- "Changes go to a working copy; nothing reaches your project until you review the result."

A task that hits a problem it can't get past ends with what its earlier steps did, sent up for review with the reason ("The AI couldn't produce the next step."), so a whole village isn't lost.

### Example: following a farm task as it works

1. Ask "Make a small farm east of the village with a field of wheat, a scarecrow and chickens that wander in a pen".
2. The card reads "Working — step 2 of about 4". Step 1 shows "Lay out the field and the pen ✓".
3. Step 2 shows "Add the Chicken prefab with a wander script" with "1 to fix": its script raised an error when the working copy played for 2 seconds.
4. Step 3's summary says it rewrites the chicken script; it finishes with ✓.

The final review card includes the fixed script, not the broken one: steps marked "to fix" are repaired by the next step before anything reaches your project.

Questions: what does "N to fix" mean on a step? how far is my task? why did my task stop early?

## Outline first: approving before anything is made

A task that makes content you'll want a say in (people and their names, a story, lore, dialogue, a quest line: "make a story about this village and populate it") starts with an **outline** and a plan, and no changes. The AI looks first (the buildings, the World Bible) so the outline fits what's really there. The outline shows a title, the idea in a few lines, and one part per building or group saying who is there, what they do and their hook or gossip.

On the card:
- **Approve and make it**: the steps start, and the approved outline goes with every step so they keep to it (names, places, lines).
- **Change it**: type the changes in the box ("make the smith a woman", "two people at the inn", "less gossip about the mayor"); a revised outline comes back for approval.

You can also answer in the message box: "ok", "yes", "approve", "go ahead", "looks good", "do it", "make it", "fine", "great"... approve it; anything else is taken as a change to it.

Mechanical tasks (a new enemy, a farm's fields) start with their first step, no outline. The Planner's own feature work goes ahead without waiting.

### Example: people for Riverford, approved before they are made

1. Open the scene of the village Riverford and type:

   > Make a story about this village and populate it

2. The AI looks at the buildings and the World Bible first ("Looked at: ..."), then the card reads "Outline — waiting for you". The outline might be titled "The Miller's Debt", with a few lines of idea, and parts such as:
   - **The Mill**: Hobb Tanner, the miller, owes the mayor money; gossip: he waters the flour.
   - **The Smithy**: Garrick, the smith, forges for the garrison.
   - **The Leaky Bucket inn**: Vara Bell, the innkeeper, hears everything.
3. In the box under the outline type "make the smith a woman, and two people at the inn", then click **Change it**.
4. A revised outline comes back with Brenna the smith and a second person at the inn. Type "looks good" in the message box (or click **Approve and make it**).

The steps start, each keeping to the approved names and places, and the result comes up for review as one card.

Questions: why is the AI showing me an outline? how do I approve the outline? how do I change the outline?

## Review each step

In the AI panel's **⚙** settings: **Review each step of a bigger task (it waits for Continue after each)**. When on, the task pauses after every step and shows "Step N done on the working copy: summary (M changes). Nothing reaches the project until the whole is reviewed." Press **Continue** to go on to the next step, or **Stop**.

### Example: checking a quest line one step at a time

1. In the AI panel, click **⚙** and tick **Review each step of a bigger task (it waits for Continue after each)**.
2. Ask "Make a three-part quest line where the player clears the bandits from the old road".
3. After step 1 the card shows "Step 1 done on the working copy: writes the quest bandit_road with three stages (2 changes). Nothing reaches the project until the whole is reviewed."
4. It looks right: click **Continue**. After step 2 (the bandit leader's dialogue), read the summary again and click **Continue**.
5. If a step went the wrong way, click **Stop** instead: the working copy is discarded and your project is unchanged.

Questions: can I check each step of a task? how do I make the task wait between steps?

## Stopping a task, and its limits

**Stop** on the task card ends the task at once: the working copy is discarded and nothing is changed. Turning AI off also stops it ("AI was turned off.").

Built-in limits stop a runaway task:
- **40 steps**: "Stopped after 40 steps without finishing" (or "with problems left to fix");
- **the token budget**: the input tokens a task may use, 10 million by default. Change it in the AI panel's ⚙ settings: "A bigger task may use up to [N] million input tokens" (0.5 to 200). When used up: "Stopped: this task used its token budget (N input tokens)." What it did so far goes up for review.
- **8 lookup rounds**: in a request or a task step the AI may ask to look things up up to 8 times; the last time it's told to answer with what it has. If it still only asks: "The AI kept asking to look things up without answering; ask again, more precisely."

### Example: give a big task more room

1. You ask "Give every villager in all three towns a daily routine" and the task ends with "Stopped: this task used its token budget (10000000 input tokens)."
2. A review card still comes up with what the finished steps made: apply it to keep those routines.
3. Click **⚙** in the AI panel and set "A bigger task may use up to `25` million input tokens".
4. Ask "Give the remaining villagers a daily routine".

The new task carries on with the villagers that have none yet, under the higher budget.

### Example: stop a task that went the wrong way

1. Ask "Turn the courtyard into a market with stalls". The plan shows the stalls being placed over the fountain.
2. Click **Stop** on the task card.

The card shows "Stopped", the working copy is thrown away and your scene is as it was. Ask again with the detail you want ("...with stalls along the north wall, keep the fountain").

Questions: how do I stop the AI? what is the token budget? why did my task stop after 40 steps? how do I raise the task budget?

## Generating characters and walks from the AI panel

Any answer can ask for characters and animations to be made first, before its operations: a new character (from a description, 4 or 8 directions) or an animation (walk, run, attack... for a character sheet or a picture). Its operations then use the result, for example a person's prefab made with `prefab.createCharacter` from their character sheet (complete and animated in one operation) and placed with `scene.spawn`.

- **At most four characters and four animations an answer.** More than four ("animate all characters with walk", "give every villager a run") is always done as a task, four a step, until every one has it. The AI lists the project's character sheets first and leaves out those that already have the clip.
- **Reuse before generate**: the AI uses a character sheet, prefab or art the project already has, and doesn't animate a clip a sheet already has, unless you say to make it new ("a new look", "regenerate", "draw again").
- What was made is noted under "Made:" in the answer's notes.
- If something couldn't be made (the animator or art provider is off, a key missing), it says so and keeps what was made.

Art the project doesn't have (a texture for a prop, a tile) is also drawn first, by the art provider, when the AI lists it.

See also: ai-providers-and-keys.md for which provider makes characters, animations and art.

### Example: a new character placed by the well

1. Type:

   > Add an old fisherwoman named Maud who sits by the well, 4 directions

2. The AI finds no sheet for her, so the answer asks for a character to be made first. The strip and card show the art being made (this takes minutes).
3. The card arrives with `▸ New character prefabs/Maud.prefab` (made with `prefab.createCharacter` from the new sheet) and a spawn of Maud by the well. The notes list "Made: ..." with her character sheet.
4. Click **Apply**.

### Example: walks for the whole village

1. Type "animate all characters with walk" in a project with 11 character sheets, 3 of which already have a walk.
2. The AI answers as a task: "I'm animating 8 characters with a walk, four at a time: 2 steps."
3. Each step makes four walks; the review card at the end adds them to the 8 sheets.

The three that had a walk are left alone. Say "regenerate the walks" to have them drawn again too.

Questions: can the AI make characters? why does it animate only four at a time? how do I give every villager a walk animation?

## Splitting a request into parts

When a request is too big for one task, the AI answers with a **split**: 2 to 6 smaller requests in your words and why. On the card:
- **Do them all, step by step**: carries them out in order as one task, each checked before the next;
- or click one part to put it in the message box and send it alone.

### Example: a harbour update in parts

1. Ask "Add a harbour with boats, a fish market, a lighthouse that blinks at night, a smuggler quest and a tavern brawl".
2. The split card suggests, for example: "Add a harbour with a pier and three boats", "Add a fish market with two traders", "Make the lighthouse lamp blink at night", "Write a smuggler quest starting at the harbour", "Add a tavern brawl when the player insults the bouncer".
3. Click "Make the lighthouse lamp blink at night": it goes into the message box. Edit it to "...blink every 2 seconds at night" and press **Enter** to send just that one.
4. Later, for the rest, ask the whole thing again and click **Do them all, step by step**.

The parts then run as one task, each checked before the next, with a single review at the end.

Questions: what do I do with a split answer? can I run all the parts at once?

See also: ai-panel.md, ai-panel-review.md, ai-requests-tips.md.
