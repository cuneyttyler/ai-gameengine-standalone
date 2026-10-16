// Editor shortcuts as data (development_plan.md 5.6): every rebindable action,
// its default keys, and the user's own bindings on top. Keys are written as
// "Ctrl+Shift+S", "F5", "R", "[" — the typed character for symbols, so any
// keyboard layout works ('[' is AltGr+8 on some). Both the web UI and the
// viewport (which forwards its keys, EditorApp::OnViewportKey) dispatch
// through here, so a binding changed in Help ▸ Keyboard Shortcuts applies
// everywhere. Pure functions, tested by tests/keymap.test.ts.

export interface ActionDef {
  id: string
  label: string
  group: string
  keys: string[] // defaults
  inPlay?: boolean // also while playing (other keys belong to the game)
  whileTyping?: boolean // also inside a text field
}

export const actions: ActionDef[] = [
  { id: 'file.save', label: 'Save the scene', group: 'File', keys: ['Ctrl+S'], whileTyping: true },
  { id: 'file.saveAs', label: 'Save project as…', group: 'File', keys: ['Ctrl+Shift+S'] },
  { id: 'file.open', label: 'Open project…', group: 'File', keys: ['Ctrl+O'] },
  { id: 'file.new', label: 'New project…', group: 'File', keys: ['Ctrl+N'] },
  { id: 'edit.undo', label: 'Undo', group: 'Edit', keys: ['Ctrl+Z'] },
  { id: 'edit.redo', label: 'Redo', group: 'Edit', keys: ['Ctrl+Y', 'Ctrl+Shift+Z'] },
  { id: 'edit.copy', label: 'Copy the selection', group: 'Edit', keys: ['Ctrl+C'] },
  { id: 'edit.paste', label: 'Paste at the cursor', group: 'Edit', keys: ['Ctrl+V'] },
  { id: 'edit.duplicate', label: 'Duplicate the selection', group: 'Edit', keys: ['Ctrl+D'] },
  { id: 'edit.delete', label: 'Delete the selection', group: 'Edit', keys: ['Delete', 'Backspace'] },
  { id: 'edit.deselect', label: 'Deselect, back to the Select tool', group: 'Edit', keys: ['Escape'] },
  { id: 'edit.bigger', label: 'Bigger: the brush (and its preview) while one is armed, else the selection', group: 'Edit', keys: [']', '+'] },
  { id: 'edit.smaller', label: 'Smaller: the brush while one is armed, else the selection', group: 'Edit', keys: ['[', '-'] },
  { id: 'edit.biggerFine', label: 'A little bigger', group: 'Edit', keys: ['}'] },
  { id: 'edit.smallerFine', label: 'A little smaller', group: 'Edit', keys: ['{'] },
  { id: 'edit.rotate', label: 'Turn a flat image a quarter (the brush while painting)', group: 'Edit', keys: ['R'] },
  { id: 'tool.select', label: 'Select tool', group: 'Tools', keys: ['V'] },
  { id: 'tool.erase', label: 'Erase tool', group: 'Tools', keys: ['E'] },
  { id: 'view.frame', label: 'Frame the scene', group: 'View', keys: ['F'] },
  { id: 'view.grid', label: 'Tile borders on empty ground', group: 'View', keys: ['G'] },
  { id: 'view.stats', label: 'Frame stats', group: 'View', keys: ['F3'], inPlay: true },
  { id: 'panel.scene', label: 'Show / hide the Scene panel', group: 'Panels', keys: ['B'] },
  { id: 'panel.assets', label: 'Show / hide the Assets panel', group: 'Panels', keys: ['J'] },
  { id: 'panel.ai', label: 'Show / hide the AI panel', group: 'Panels', keys: ['I'] },
  { id: 'panel.viewport', label: 'Show / hide the viewport', group: 'Panels', keys: ['M'] },
  { id: 'play.toggle', label: 'Play / stop', group: 'Play', keys: ['F5'], inPlay: true },
  { id: 'help.shortcuts', label: 'Shortcuts', group: 'Help', keys: ['F1'], inPlay: true },
]

// Shown in the list but not rebindable.
export const fixedShortcuts: { group: string; keys: string; label: string }[] = [
  { group: 'Edit', keys: 'Arrows', label: 'Nudge the selection a cell (Shift: a tenth)' },
  { group: 'Tools', keys: '1 – 9', label: 'Pick prefab 1 – 9 as the brush' },
  { group: 'Tools', keys: 'Alt+wheel', label: 'Resize: the brush while one is armed, else the selection' },
  { group: 'Tools', keys: 'Right-click', label: 'Menu for what is under the cursor' },
  { group: 'Tools', keys: 'Right-drag / wheel', label: 'Pan / zoom' },
  { group: 'Play', keys: 'WASD / arrows', label: 'Move' },
  { group: 'Play', keys: 'Space / click', label: 'Attack' },
  { group: 'Play', keys: 'E', label: 'Talk' },
  { group: 'Play', keys: '1 – 4, Up/Down + Enter', label: 'Choose in a conversation' },
  { group: 'Play', keys: 'Esc', label: 'Stop playing' },
]

export type Keymap = Record<string, string[]>

const MODIFIER_KEYS = new Set(['Control', 'Shift', 'Alt', 'Meta', 'AltGraph', 'CapsLock', 'OS'])
const NAMED: Record<string, string> = { ' ': 'Space', Esc: 'Escape', Del: 'Delete' }

export interface KeyLike {
  key: string
  ctrlKey?: boolean
  shiftKey?: boolean
  altKey?: boolean
  metaKey?: boolean
}

// "Ctrl+Shift+S" for a key event; "" for a modifier on its own.
export function comboOf(event: KeyLike): string {
  if (MODIFIER_KEYS.has(event.key)) return ''
  let key = NAMED[event.key] ?? event.key
  let ctrl = !!(event.ctrlKey || event.metaKey)
  let alt = !!event.altKey
  let shift = !!event.shiftKey
  if (key.length === 1) {
    if (/[a-z]/i.test(key)) {
      key = key.toUpperCase()
    } else {
      // A symbol is named by the character typed: Shift (or AltGr, which
      // reports Ctrl+Alt) is part of how the layout makes it.
      shift = false
      if (ctrl && alt) ctrl = alt = false
    }
  }
  return [ctrl && 'Ctrl', alt && 'Alt', shift && 'Shift', key].filter(Boolean).join('+')
}

// Accepts loose spellings ("ctrl+s", "shift+ctrl+z") and writes them the one way.
export function normalizeCombo(text: string): string {
  const parts = text.split('+').map((p) => p.trim())
  // "Ctrl++" splits into ["Ctrl", "", ""]: the key is "+".
  let key = parts.pop() ?? ''
  if (key === '' && parts.length > 0 && parts[parts.length - 1] === '') {
    parts.pop()
    key = '+'
  }
  const mods = new Set(parts.map((p) => p.toLowerCase()))
  return comboOf({
    key: key.length === 1 ? key : (Object.keys(NAMED).find((k) => k.toLowerCase() === key.toLowerCase()) ?? key),
    ctrlKey: mods.has('ctrl') || mods.has('control'),
    altKey: mods.has('alt'),
    shiftKey: mods.has('shift'),
  })
}

// The defaults with the user's changes on top (unknown actions dropped).
export function effectiveKeymap(overrides: Keymap): Keymap {
  const map: Keymap = {}
  for (const action of actions) {
    const own = overrides[action.id]
    map[action.id] = (own ?? action.keys).map(normalizeCombo).filter(Boolean)
  }
  return map
}

// Which action a key combination runs, if any.
export function actionFor(keymap: Keymap, combo: string): string | null {
  if (!combo) return null
  for (const [id, keys] of Object.entries(keymap)) {
    if (keys.includes(combo)) return id
  }
  return null
}

// Binds `combo` to `id` — in place of its key `replacing`, or as one more —
// taking it away from whatever action had it. Returns the overrides to save
// and which action lost the keys.
export function bind(
  overrides: Keymap,
  id: string,
  combo: string,
  replacing: string | null,
): { overrides: Keymap; takenFrom: string | null } {
  const current = effectiveKeymap(overrides)
  const takenFrom = Object.entries(current).find(([other, keys]) => other !== id && keys.includes(combo))?.[0] ?? null
  const next: Keymap = { ...overrides }
  if (takenFrom) next[takenFrom] = current[takenFrom]!.filter((k) => k !== combo)
  const own = current[id] ?? []
  const at = replacing === null ? -1 : own.indexOf(replacing)
  next[id] =
    at >= 0
      ? own.map((k, i) => (i === at ? combo : k)).filter((k, i, all) => all.indexOf(k) === i)
      : own.includes(combo)
        ? own
        : [...own, combo]
  return { overrides: prune(next), takenFrom }
}

export function unbind(overrides: Keymap, id: string, combo: string): Keymap {
  const current = effectiveKeymap(overrides)
  return prune({ ...overrides, [id]: (current[id] ?? []).filter((k) => k !== combo) })
}

export function reset(overrides: Keymap, id?: string): Keymap {
  if (!id) return {}
  const next = { ...overrides }
  delete next[id]
  return next
}

// Only what differs from the defaults is kept.
function prune(overrides: Keymap): Keymap {
  const out: Keymap = {}
  for (const action of actions) {
    const own = overrides[action.id]
    if (own && own.join('|') !== action.keys.map(normalizeCombo).join('|')) out[action.id] = own
  }
  return out
}

export function label(keymap: Keymap, id: string): string {
  return (keymap[id] ?? []).join(' / ')
}
