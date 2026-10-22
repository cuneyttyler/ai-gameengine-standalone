// Keyboard shortcuts at runtime (development_plan.md 5.6): the user's keymap
// (defaults in keymap.ts, their changes kept by the host in UserPrefs), what
// each action does, and the one place keys turn into actions — for the web
// UI and for keys the viewport forwards (viewport:key).
import { reactive } from 'vue'
import { editorState, armPrefab } from './editorState'
import { onHostMessage, sendToHost } from './ipc'
import { actionFor, actions, comboOf, effectiveKeymap, label, type Keymap } from './keymap'
import { dockView, toggleMaximize, togglePanel, toggleViewport } from './layout'

export const keyState = reactive({
  overrides: {} as Keymap,
  map: effectiveKeymap({}),
})

export function setOverrides(overrides: Keymap): void {
  keyState.overrides = overrides
  keyState.map = effectiveKeymap(overrides)
  sendToHost({ type: 'keys:set', overrides })
}

// "Ctrl+S" for a menu or a tooltip; follows the user's bindings.
export function keyLabel(id: string): string {
  return label(keyState.map, id)
}

const send = (type: string, extra: Record<string, unknown> = {}) => () => sendToHost({ type, ...extra })

const handlers: Record<string, () => void> = {
  'file.save': send('scene:save'),
  'file.open': send('project:open'),
  'edit.undo': send('edit:undo'),
  'play.save': send('play:save'),
  'play.load': send('play:load'),
  'edit.redo': send('edit:redo'),
  'edit.copy': send('edit:copy'),
  'edit.paste': send('edit:paste'),
  'edit.duplicate': send('edit:duplicate'),
  'edit.delete': send('edit:delete'),
  'edit.deselect': () => {
    sendToHost({ type: 'tool:set', tool: 'select' })
    sendToHost({ type: 'entity:select', id: null })
  },
  'edit.bigger': send('edit:scale', { factor: 1.1 }),
  'edit.smaller': send('edit:scale', { factor: 1 / 1.1 }),
  'edit.biggerFine': send('edit:scale', { factor: 1.02 }),
  'edit.smallerFine': send('edit:scale', { factor: 1 / 1.02 }),
  'edit.rotate': send('edit:rotate', { direction: 1 }),
  'edit.rotateBack': send('edit:rotate', { direction: -1 }),
  'tool.select': send('tool:set', { tool: 'select' }),
  'tool.erase': send('tool:set', { tool: 'erase' }),
  'view.frame': send('view:frame'),
  'view.grid': send('view:grid'),
  'view.stats': send('view:stats'),
  'play.toggle': send('play:toggle'),
  'panel.scene': () => togglePanel('scene'),
  'panel.assets': () => togglePanel('assets'),
  'panel.project': () => togglePanel('project'),
  'panel.generate': () => togglePanel('generate'),
  'panel.ai': () => togglePanel('ai'),
  'panel.viewport': () => toggleViewport(),
  'panel.build': () => togglePanel('build'),
  'panel.planner': () => togglePanel('planner'),
  // The panel last clicked in (the viewport when none), or back to the whole layout.
  'panel.maximize': () => toggleMaximize(dockView.maximized ?? dockView.focused ?? 'viewport'),
  'help.shortcuts': () => (editorState.showShortcuts = !editorState.showShortcuts),
}

// For actions whose code lives elsewhere (projects.ts: New, Save As).
export function registerAction(id: string, run: () => void): void {
  handlers[id] = run
}

export function runAction(id: string): void {
  handlers[id]?.()
}

// Keys that aren't rebindable: arrows nudge, 1-9 pick a prefab.
function fixedKey(key: string, shift: boolean): boolean {
  const arrows: Record<string, [number, number]> = {
    ArrowLeft: [-1, 0],
    ArrowRight: [1, 0],
    ArrowUp: [0, -1],
    ArrowDown: [0, 1],
  }
  const arrow = arrows[key]
  if (arrow) {
    if (editorState.selectedId === null) return false
    const step = shift ? 0.1 : 1
    sendToHost({ type: 'edit:nudge', dx: arrow[0] * step, dy: arrow[1] * step })
    return true
  }
  if (key.length === 1 && key >= '1' && key <= '9') {
    const prefab = editorState.prefabs[Number(key) - 1]
    if (prefab) armPrefab(prefab.path)
    return true
  }
  return false
}

// Runs the action bound to `combo`, if it applies now. True if handled.
export function handleCombo(combo: string, typing: boolean): boolean {
  const id = actionFor(keyState.map, combo)
  if (!id) return false
  const def = actions.find((a) => a.id === id)!
  if (typing && !def.whileTyping) return false
  if (editorState.playing && !def.inPlay) return false
  runAction(id)
  return true
}

// While a shortcut is being recorded (the Shortcuts window), keys go there.
export const capture = reactive({ active: false })

window.addEventListener('keydown', (event) => {
  if (capture.active) return
  const target = event.target as HTMLElement | null
  const typing =
    !!target && (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA' || target.tagName === 'SELECT' || target.isContentEditable)
  if (!typing && !editorState.playing && !event.ctrlKey && !event.altKey && fixedKey(event.key, event.shiftKey)) {
    event.preventDefault()
    return
  }
  if (handleCombo(comboOf(event), typing)) {
    event.preventDefault()
    event.stopPropagation()
  }
})

onHostMessage((message) => {
  if (message.type === 'keys:map') {
    keyState.overrides = (message.overrides as Keymap) ?? {}
    keyState.map = effectiveKeymap(keyState.overrides)
  } else if (message.type === 'viewport:key') {
    // Pressed with the viewport focused (EditorApp::OnViewportKey/Char).
    handleCombo(message.combo as string, false)
  }
})

sendToHost({ type: 'keys:request' })
