// Every editor shortcut in one table (development_plan.md 4.1/4.3): the Help ▸
// Keyboard Shortcuts reference is built from it, so it can't drift from what
// the keys do. The keys themselves are handled in editorState.ts (web UI
// focused) and EditorApp::OnViewportKey (viewport focused).
export interface Shortcut {
  keys: string
  action: string
}

export const shortcutGroups: { name: string; items: Shortcut[] }[] = [
  {
    name: 'File',
    items: [
      { keys: 'Ctrl+S', action: 'Save the scene' },
      { keys: 'Ctrl+Shift+S', action: 'Save project as…' },
      { keys: 'Ctrl+O', action: 'Open project…' },
      { keys: 'Ctrl+N', action: 'New project…' },
    ],
  },
  {
    name: 'Edit',
    items: [
      { keys: 'Ctrl+Z / Ctrl+Y', action: 'Undo / redo' },
      { keys: 'Ctrl+C / Ctrl+V', action: 'Copy / paste the selection (pastes at the cursor)' },
      { keys: 'Ctrl+D', action: 'Duplicate the selection' },
      { keys: 'Del', action: 'Delete the selection' },
      { keys: 'Arrows', action: 'Nudge the selection one cell (Shift: a tenth)' },
      { keys: '[ / ]', action: 'Make the selection smaller / bigger (Alt+wheel too)' },
      { keys: 'Esc', action: 'Deselect, back to the Select tool' },
    ],
  },
  {
    name: 'Tools',
    items: [
      { keys: 'V', action: 'Select' },
      { keys: 'E', action: 'Erase' },
      { keys: '1 – 9', action: 'Pick prefab 1 – 9 as the brush' },
      { keys: 'Right-click', action: 'Menu for what is under the cursor' },
      { keys: 'Right-drag / wheel', action: 'Pan / zoom' },
    ],
  },
  {
    name: 'View',
    items: [
      { keys: 'F', action: 'Frame the scene' },
      { keys: 'G', action: 'Show tile borders on empty ground' },
      { keys: 'Ctrl+B', action: 'Show / hide the Scene panel (left)' },
      { keys: 'Ctrl+J', action: 'Show / hide the bottom panel' },
      { keys: 'Ctrl+I', action: 'Show / hide the AI panel' },
      { keys: 'F3', action: 'Frame stats in play' },
    ],
  },
  {
    name: 'Play',
    items: [
      { keys: 'F5', action: 'Play / stop' },
      { keys: 'WASD / arrows', action: 'Move' },
      { keys: 'Space / click', action: 'Attack' },
      { keys: 'E', action: 'Talk' },
      { keys: '1 – 4, Up/Down + Enter', action: 'Choose in a conversation' },
      { keys: 'Esc', action: 'Stop playing' },
    ],
  },
  { name: 'Help', items: [{ keys: 'F1', action: 'This list' }] },
]
