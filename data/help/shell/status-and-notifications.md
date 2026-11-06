# Status bar, notifications and progress

The status bar along the bottom of the editor, the short notices it shows, the Notifications panel behind its bell,
the floating progress boxes, and the AI progress strips at the top of panels.

## The status bar

The **status bar** is the thin strip along the bottom of the editor window. From left to right it shows:

- **The scene's name**, with a dot (•) when it has unsaved changes.
- **A hint** for what you're doing. For the Select tool it reads "Click to select · drag to move · Del delete · Ctrl+D
  duplicate". Erase, the prefab brush, Path and Marker each have their own hint, and every hint ends with "right-drag
  pan · wheel zoom · F frame". During Play it reads "Playing · WASD move · Space or click attack · Esc stop · changes
  revert on stop".
- **The latest notice**, such as "Saved main.aige", "Copied — Ctrl+V pastes at the cursor" or "Brush size 120%". It
  disappears after about three and a half seconds.
- **The bell (🔔)**, which opens Notifications. A number on it counts what has finished since you last looked, and a
  spinning ◌ shows while work is running.

### Example: checking the brush size and a save at a glance

1. With "Place barrel" armed, press ] twice. The status bar's notice reads "Brush size 121%" for a few seconds.
2. Press Ctrl+S. The notice reads "Saved harbour_market.aige", and the dot after the scene name on the left of the
   status bar goes away.
3. Missed the notice? Click the bell (🔔): every notice is also listed in Notifications.

Questions: what does the bottom bar show? where did that message go? what does the number on the bell mean?

## The Notifications panel

Click the **bell** on the status bar to open **Notifications**. It's a panel half the window high at the bottom
right, listing what has been happening, newest first:

- the AI's calls, running and finished: art, characters, animations, backgrounds, a place's build steps, AI panel
  requests, checks, web searches;
- a part being animated (Animate a part…) and an interior's things being animated (Animate the interior…);
- every notice the editor has shown on the status bar.

Each entry has an icon (◌ running, ✓ done, ✕ failed, • information), a title, a detail line (its step or phase and
seconds), a progress bar when the work has counted steps, and how long ago it changed. The header counts how many are
running. **Clear** removes everything that has finished, and **✕** closes the panel. Opening the panel resets the
bell's count. The list keeps the last 120 entries.

### Example: did the blacksmith's art finish?

You asked for art for Bram Hollis, the blacksmith, and went on painting the forge yard.

1. The bell on the status bar shows a spinning ◌ while the job runs, then a 1 when it has finished.
2. Click the bell. Notifications opens at the bottom right; the newest entry is the art call, with ✓ (done) or ✕
   (failed), its detail line and how long ago it changed.
3. If it shows ✕, read its detail line for the reason and try again from the Generate panel.
4. Click Clear to remove the finished entries, then ✕ to close the panel. The bell's count is back to nothing.

Questions: how do I see what the AI is doing? where are old notices? how do I clear notifications? did my art
generation fail?

## Floating progress boxes

Some long jobs are started from a window you can close while they run, such as **Animate a part…** and **Animate the
interior…**. When that window is closed and the Notifications panel is closed too, a small **floating box** shows the
job at the bottom right: its title, its detail and a progress bar. Up to three boxes show at once. A box stays a few
seconds after the job finishes. Click a box to open Notifications, or click **Hide** to hide it (the job stays listed
in Notifications).

### Example: animating the mill wheel while you keep working

1. Right-click the watermill and click Animate a part…, pick the wheel and start the job.
2. Close the Animate a part window. With Notifications closed too, a small box at the bottom right shows the job's
   title, its detail and a progress bar.
3. Keep placing fences. The box is in your way? Click Hide: it goes, and the job is still listed in Notifications.
4. When the job finishes, the box (if shown) stays a few seconds, and the bell counts it.

Questions: what is the box at the bottom right? how do I hide the progress box? can I close the animate window while
it works?

## AI progress strips

Panels that call the AI (the AI panel, Build, Generate, the Planner and others) show an **AI progress strip** at
their top while a call of theirs is running. Nothing shows when nothing runs. Each call gets a line with:

- its name (for example "AI panel request", "Generating art", "Planning the place", "Consistency check");
- its phase: waiting for the model, thinking, writing, finishing, done or failed;
- its current step, when the request has several steps;
- the tokens written so far (hover to see thinking, answer and read tokens);
- how long it has taken, counting up each second.

Under the line, a bar fills as counted steps finish, or runs back and forth while the AI works. The editor gets these
reports from the AI service about once a second.

See also: preferences.md (AI models).

### Example: watching an AI panel request work

1. In the AI panel, type:

> "Add a herbalist's hut at the edge of the forest with Ilsa Fen, the herbalist, inside."

2. A strip appears at the top of the AI panel: "AI panel request", its phase (waiting for the model, then thinking,
   then writing), its current step, the tokens written so far and the seconds counting up.
3. Hover the token count to see how many were thinking, answer and read tokens.
4. When the phase reads done the strip goes away. If it sits at "waiting for the model" for long, the model may be
   busy or slow; see Preferences ▸ AI models.

Questions: how long has the AI been thinking? how many tokens did it use? why is the AI slow?
