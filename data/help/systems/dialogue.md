# Dialogue

Writing conversations in Project ▸ Story: the Talk component, dialogue files, nodes, choices, different starts,
conditions, effects, skill checks, the graph view, how talking works in play, and writing dialogue with the AI.

## Making a character talk: the Talk component

To let the player talk to someone, add a **Talk** component to the character in the Inspector:

| field | meaning |
|---|---|
| `dialogue` | the conversation, `dialogue/<id>.json` (picked from a list) |
| `character` | who they are in the World Bible (`character/villager`): their name on screen, and what "talk to" quest objectives point at |
| `range` | how close the player must be, in cells (0.3-10) |

In play, when the player is within range, the prompt shows "E  Talk to <name>". Pressing **E** (the interact action)
starts the conversation. The world pauses while talking; a conversation can't be paused away.

A character's name on screen comes from its World Bible entry ("Old Tamsin"), so quests and dialogue can refer to the
canon name.

Questions: how do I make an NPC talkable? why can't I talk to my NPC? how close does the player need to be to talk?

## The Story tab and dialogue files

Open **Project ▸ Story** ("Dialogue, quests and daily routines"). The left column has three groups, each with a **+**:
**Dialogue**, **Routines** and **Quests**.

- Each dialogue row shows its id, ⚠ when it has problems, or "n×" for how many things in the open scene use it (hover
  to see "Talk on …", or "Not used by anything in the open scene").
- Press **+** next to Dialogue to start a new one; set its file name in **File** (`dialogue/<id>.json`).
- At the top: **Speaker** (who talks by default, from the World Bible's characters), **Starts at** (the first node), and
  a **List / Graph** switch.
- **Save** (Ctrl+S) writes the file through the engine's own reader and writer; **Revert** goes back; **Delete…**
  deletes it. Problems are listed at the top of the form.

Dialogue, quests and routines belong to the project, not a scene: progress, flags and the clock carry on through
gateways.

Questions: where do I write dialogue? how do I make a new conversation? where are dialogue files saved?

## Nodes: lines and what follows

A conversation is a set of **nodes**. Each node is one line:

- an **id** (renaming it updates the links that point at it);
- who says it: the dialogue's speaker by default, **The player**, or another World Bible character (plain text such as
  "A guard" is shown as written);
- the **text** ("What they say");
- an optional **portrait** (an image shown above the box) and **voice** (a sound played with the line);
- then either **then** "end the conversation" / "go to <node>" (a line without choices; → jumps to that node in the
  form), or **choices** (tick "choices");
- its own effects (run when the node is shown).

**+ Node** adds a node; × deletes one. The node marked "start" is where the conversation begins (unless a different
start applies).

In play, **Space**, **Enter** or **E** continue past a line without choices.

Questions: how do I add a line to a dialogue? how do I end a conversation? can the player have lines? how do I add a
portrait or voice to a line?

## Choices

A node with **choices** ticked shows numbered options for the player (up to 9; "+ Choice" is disabled at 9). Each
choice has:

- the **text** ("What the player can say");
- where it goes: → a node, or "ends";
- **conditions**: all must hold, or the choice is hidden;
- a **skill check** (optional): tick "skill check", give the player's attribute and a difficulty; passing goes to the
  choice's node and runs its effects, failing goes to the "else" node (or ends) and skips the effects. The choice shows
  "[Speech 5]".
- **effects**: run when it's picked.

In play, **1-9** pick a choice; **Up/Down** and **Enter** do the same.

Example: "Surely the keep can pay more." with a check speech ≥ 5, else → `refused`, and the effect set variable
`reward` to 50.

Questions: how do I add choices? how do I hide a choice unless a quest is active? how do I make a persuasion check? how
many choices can a node have?

## Different starts: greeting the player differently

**+ Different start when…** adds a row "Instead start at <node>" with conditions. Different starts are checked in order
before **Starts at**; the first whose conditions hold wins.

This is how a character greets you differently once a quest is under way:

- instead start at `thanks` when quest `quiet-the-courtyard` is completed;
- instead start at `progress` when quest `quiet-the-courtyard` is active;
- otherwise start at `greet`.

Questions: how do I make an NPC say something different after a quest? how do I change the first line of a dialogue?

## Dialogue conditions

Conditions appear on choices, different starts, quest branches, packages' conditions, Reactions' and parts' `if`,
recipes' `if` and path forks. All in a list must hold. The editor offers:

| kind | holds when |
|---|---|
| flag is set / flag isn't set | a story flag (`{"flag": "x"}`, `{"flag": "x", "is": false}`) |
| quest is | a quest is inactive, active, completed or failed, optionally at a stage |
| variable | a story variable compares (`==`, `!=`, `<`, `<=`, `>`, `>=`), or is set and not 0/false/"" |
| player has item | carries at least a count of an item |
| player has gold | at least that much gold |
| player level | at least that level |
| attribute | the player's attribute at least (or below) a number |
| time of day | the clock is in a range of hours (may wrap past midnight: 20 to 6) |
| day | it's that day or later |
| reputation with | the player's reputation with a faction at least (or below) a number |

Written by hand, `{"difficulty": "story"}` or `{"difficulty": {"enemies": {"atLeast": 4}}}` test the difficulty.

Questions: what conditions can dialogue use? how do I check if the player has an item? how do I check the time of day in
dialogue?

## Dialogue effects

Effects appear on nodes, choices, quest stages (on reaching), quest completion and failure, Reactions' and parts' `do`,
recipes, path points and links, and UI buttons. The editor offers:

set flag, clear flag; start quest, complete quest, fail quest, complete objective, set quest stage; set variable, add to
variable; give item, take item, give gold, give experience; change reputation (with a faction, and its members'
disposition); play cutscene; open a screen, close a screen; a character starts doing (`push_package`), a character stops
doing (`pop_package`); a character says (`say`); send an event (`send_event`).

`push_package`, `pop_package` and `say` take an optional `who` (a character's name); without it, the character or thing
the rule runs on.

Example: the choice "I'll do it." with effects start quest `quiet-the-courtyard` and set flag `took_job`.

Questions: what can a dialogue choice do? how do I give the player an item from dialogue? how do I start a quest from
dialogue? can dialogue make an NPC follow the player?

## The graph view

Switch **List / Graph** to **Graph** to see the conversation as boxes and arrows: each node a box with its id and the
start of its text, arrows for "then" links and choices (labelled), failed skill checks drawn differently, the start node
marked. Click a box to show that node's form. **+ Node** adds a node in the graph view.

Questions: can I see my dialogue as a graph? how do I find where a node leads?

## Dialogue checks and writing with the AI

The Story tab reports problems with ⚠: files that don't parse, links to nodes that don't exist, nodes that can't be
reached, more than 9 choices on a node, unknown quests, objectives or effects, and World Bible keys that don't exist.

**With the AI panel:** ask "what would Tamsin say about the keep?" or "give the guard a conversation that offers the
thief quest". The AI sees every World Bible entry by name and gets the full text of related ones plus the tone guide.
It answers with `write_dialogue` (and `write_quest`) operations shown in Diff Review as the files they'll write, checked
as the Story tab checks them. New named characters or places it invents are added to the World Bible in the same
proposal.

**From Lua:** `start_dialogue(path)` starts a conversation with the script's entity as the speaker; the `talk` event
fires when the player talks to someone.

See also: Quests, Schedules and cutscenes, Scripting.

Questions: why does my dialogue show a warning? can the AI write dialogue? how do I start a conversation from a script?
