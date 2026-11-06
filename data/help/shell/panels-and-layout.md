# Panels and layout

How the AIGE editor window is laid out: the eight panels, tab groups, moving and docking panels by dragging their
tabs, hiding and showing them, maximising one, the bottom area, and resetting or saving the layout.

## The editor window and its panels

The editor window has, from top to bottom, the **menu bar**, the panel area, and the **status bar**. The panel area
holds eight panels:

- **Viewport**: the scene view, with the toolbar on top (M).
- **Scene**: the Scene Tree (scenes and the open scene's contents) with the Inspector (B).
- **Assets**: Prefabs, Assets, Characters, Animations, Catalog, Library and Web Search tabs (J).
- **Project**: Scenes, World, Story, story graph, Records, Scripts, Difficulty, UI, Effects and Magic tabs (P).
- **Generate**: Level, Art, Character and Animate tabs (K).
- **AI**: the AI panel, where you ask the AI to change the game (I).
- **Build**: building a place step by step (U). It stays hidden until it has something to show, and opens when a
  build starts.
- **Planner**: designing a whole game (Y). Also hidden at first.

The **default layout**: Scene down the whole left side, the AI panel down the right side, and the Viewport in the
middle with a bottom area under it holding Assets, Project and Generate as tabs.

Questions: what panels are there? where is the Inspector? why can't I see the Build panel?

## Tab groups and moving panels

Panels sit in **tab groups**. Each group has a strip of tabs on top, and the front tab's panel fills the group. Click
a tab to bring that panel to the front.

To move a panel, **drag its tab**. A blue marker shows where it will land:

- **Onto another group's tab strip**: it joins that group as a tab, at the spot between the tabs where you drop it.
- **Onto the middle of a group**: it joins that group as a tab.
- **Near a side of a group**: it splits that group and sits beside it, taking about half the space.
- **Along an edge of the window**: it docks along that whole edge. Each edge has at most one docked area, so a second
  panel docked on the same edge goes next to what's already there.

Press **Esc** during the drag to cancel. A drop that wouldn't change anything is ignored. You can also right-click a
tab, or use View ▸ <panel>, and choose **Dock left/top/right/bottom edge**.

**Splitters**: drag the thin bar between two areas to resize them. Each side keeps at least about 90 pixels. Panels
are never rebuilt when you move them, so their state (scroll position, what you've typed) stays.

Questions: how do I move a panel? how do I put two panels side by side? how do I make a panel a tab of another? how
do I resize panels?

## Hiding and showing panels

- **Hide** a panel with its shortcut (B, J, P, K, I, M, U, Y), with View ▸ <panel> ▸ Hide, or with Hide in its tab's
  right-click menu. A hidden panel leaves the layout and everything else takes its room.
- **Show** it again with the same shortcut, with View ▸ <panel> ▸ Show, or with its button on the **side rail**. The
  side rail is a narrow column at the left of the window that lists every hidden panel. Hover a button to see its
  shortcut. When the AI is off, the AI panel's rail button reads "AI off".
- The panel shortcuts toggle: a panel on screen hides; a panel behind another tab comes to the front; a hidden panel
  comes back.

A panel you show again goes back where it was. If nothing else changed meanwhile, the whole layout is restored
exactly. Otherwise it goes into its old group, or beside its old neighbour at its old size, or along its usual window
edge. If every panel is hidden, the window says "Every panel is hidden" and shows a Show button for each panel plus
Reset layout.

Questions: how do I hide a panel? where did my panel go? how do I get a hidden panel back? what is the side rail?

## The bottom area and Ctrl+J

The area under the viewport (Assets, Project and Generate in the default layout) is the **bottom area**. **Ctrl+J**
hides the whole area, so the viewport takes the full height, and Ctrl+J again brings it back with its tabs, front tab
and height as they were. Ctrl+J works even while you're typing.

Hiding the only panel left in the bottom area hides the area (as Ctrl+J does) rather than removing it. Showing any of
its panels brings the area back. The layout always keeps a bottom area: if you close or move all of its tabs, Assets
is docked along the bottom again.

Questions: how do I give the viewport the full height? what does Ctrl+J do? how do I get the Assets panel back?

## Maximising a panel

**Double-click a panel's tab** (or choose Maximize in its tab menu, or press **F11**) and that panel fills the whole
panel area. The other panels are out of sight but still open. A strip at the top shows the panel's name and the hint
"Double-click the tab (or F11) to bring the other panels back". F11 acts on the panel you last clicked in, or the
viewport if you haven't clicked in one.

Pressing another panel's shortcut while one is maximised shows that panel and brings the whole layout back. Dropping a
dragged panel also ends maximising.

Questions: how do I make a panel full screen? how do I un-maximise? what does F11 do?

## Saving and resetting the layout

The layout is saved **with each project**. Moving a panel counts as an unsaved change: **File ▸ Save** writes the
layout to `editor-layout.json` in the project folder, and it's restored when you open the project. If the scene itself
has no changes, a moved layout is kept on close without asking. The layout you last used anywhere is also kept, and a
project with no saved layout starts from it.

**View ▸ Reset layout** (also in every tab's right-click menu, and on the "Every panel is hidden" screen) puts every
panel back in the default layout.

Questions: is my panel layout saved? why does moving a panel mark the scene unsaved? how do I reset the layout?
