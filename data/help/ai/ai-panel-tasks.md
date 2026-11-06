# Bigger tasks in the AI panel

How the AI panel carries out a request as a task: the working copy, the plan and steps, outlines you approve first, reviewing each step, the token budget, stopping, and generating characters and walks four at a time.

## What a task is

When a request is a new feature with parts that build on each other (a new kind of enemy with its own behaviour, a prefab for it and copies placed; a farm; a quest line; a village's people), the AI answers it as a **task**. It plans 2 to 6 steps and gives only the first step's operations.

The editor applies each step to a **working copy**: a clone of the open scene with the same entity ids, copies of the project's scripts, prefabs, World Bible, dialogue, quests, schedules and cutscenes, and its own project settings. It then plays the working copy headless for 2 seconds, and sends the AI the results (operations that failed, script errors) and the working copy as the new context. The AI answers with the next step or fixes, and says it's done on the last one.

Nothing reaches your project until you review the combined result: a normal proposal card headed "Result of N steps — review it all here; nothing has been applied yet." Accepting it replays every step on the real scene as one undo step.

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

Questions: what does "N to fix" mean on a step? how far is my task? why did my task stop early?

## Outline first: approving before anything is made

A task that makes content you'll want a say in (people and their names, a story, lore, dialogue, a quest line: "make a story about this village and populate it") starts with an **outline** and a plan, and no changes. The AI looks first (the buildings, the World Bible) so the outline fits what's really there. The outline shows a title, the idea in a few lines, and one part per building or group saying who is there, what they do and their hook or gossip.

On the card:
- **Approve and make it**: the steps start, and the approved outline goes with every step so they keep to it (names, places, lines).
- **Change it**: type the changes in the box ("make the smith a woman", "two people at the inn", "less gossip about the mayor"); a revised outline comes back for approval.

You can also answer in the message box: "ok", "yes", "approve", "go ahead", "looks good", "do it", "make it", "fine", "great"... approve it; anything else is taken as a change to it.

Mechanical tasks (a new enemy, a farm's fields) start with their first step, no outline. The Planner's own feature work goes ahead without waiting.

Questions: why is the AI showing me an outline? how do I approve the outline? how do I change the outline?

## Review each step

In the AI panel's **⚙** settings: **Review each step of a bigger task (it waits for Continue after each)**. When on, the task pauses after every step and shows "Step N done on the working copy: summary (M changes). Nothing reaches the project until the whole is reviewed." Press **Continue** to go on to the next step, or **Stop**.

Questions: can I check each step of a task? how do I make the task wait between steps?

## Stopping a task, and its limits

**Stop** on the task card ends the task at once: the working copy is discarded and nothing is changed. Turning AI off also stops it ("AI was turned off.").

Built-in limits stop a runaway task:
- **40 steps**: "Stopped after 40 steps without finishing" (or "with problems left to fix");
- **the token budget**: the input tokens a task may use, 10 million by default. Change it in the AI panel's ⚙ settings: "A bigger task may use up to [N] million input tokens" (0.5 to 200). When used up: "Stopped: this task used its token budget (N input tokens)." What it did so far goes up for review.
- **8 lookup rounds**: in a request or a task step the AI may ask to look things up up to 8 times; the last time it's told to answer with what it has. If it still only asks: "The AI kept asking to look things up without answering; ask again, more precisely."

Questions: how do I stop the AI? what is the token budget? why did my task stop after 40 steps? how do I raise the task budget?

## Generating characters and walks from the AI panel

Any answer can ask for characters and animations to be made first, before its operations: a new character (from a description, 4 or 8 directions) or an animation (walk, run, attack... for a character sheet or a picture). Its operations then use the result, for example a person's prefab made with `prefab.createCharacter` from their character sheet (complete and animated in one operation) and placed with `scene.spawn`.

- **At most four characters and four animations an answer.** More than four ("animate all characters with walk", "give every villager a run") is always done as a task, four a step, until every one has it. The AI lists the project's character sheets first and leaves out those that already have the clip.
- **Reuse before generate**: the AI uses a character sheet, prefab or art the project already has, and doesn't animate a clip a sheet already has, unless you say to make it new ("a new look", "regenerate", "draw again").
- What was made is noted under "Made:" in the answer's notes.
- If something couldn't be made (the animator or art provider is off, a key missing), it says so and keeps what was made.

Art the project doesn't have (a texture for a prop, a tile) is also drawn first, by the art provider, when the AI lists it.

See also: ai-providers-and-keys.md for which provider makes characters, animations and art.

Questions: can the AI make characters? why does it animate only four at a time? how do I give every villager a walk animation?

## Splitting a request into parts

When a request is too big for one task, the AI answers with a **split**: 2 to 6 smaller requests in your words and why. On the card:
- **Do them all, step by step**: carries them out in order as one task, each checked before the next;
- or click one part to put it in the message box and send it alone.

Questions: what do I do with a split answer? can I run all the parts at once?

See also: ai-panel.md, ai-panel-review.md, ai-requests-tips.md.
