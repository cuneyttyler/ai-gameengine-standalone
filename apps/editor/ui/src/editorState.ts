// Mirror of the host's editor state. The C++ side (apps/editor/EditorApp)
// is the single source of truth; components read this and send requests,
// never mutate it directly.
import { reactive } from 'vue'
import { onHostMessage, sendToHost } from './ipc'

// Everything placeable is a prefab, so there are three tools: pick, erase,
// and the prefab brush (paints tile prefabs, places everything else).
export type ToolId = 'select' | 'erase' | 'prefab'

// Whether the AI panel is expanded or collapsed to its rail: a per-user
// layout preference, so localStorage is enough. (Whether AI is on at all is
// the host's per-user preference, reported in ai:status.)
const AI_OPEN_KEY = 'aige.editor.aiOpen'

function loadAiOpen(): boolean {
  try {
    return localStorage.getItem(AI_OPEN_KEY) !== 'false'
  } catch {
    return true
  }
}

// Which panels are showing (development_plan.md 4.1): per-user layout, kept
// in localStorage. Ctrl+B / Ctrl+J / Ctrl+I toggle them.
const LAYOUT_PANELS_KEY = 'aige.editor.panels'

function loadPanels(): { left: boolean; bottom: boolean } {
  try {
    const saved = JSON.parse(localStorage.getItem(LAYOUT_PANELS_KEY) ?? 'null')
    return { left: saved?.left !== false, bottom: saved?.bottom !== false }
  } catch {
    return { left: true, bottom: true }
  }
}

export interface Prefab {
  path: string
  name: string
  error: string
  isTile: boolean
  isPlayer: boolean
  kind: 'tile' | 'character' | 'item' | 'other'
  texture: string
  uv: [number, number, number, number]
  size: [number, number]
}

export const editorState = reactive({
  tool: 'select' as ToolId,
  brush: '', // prefab path for the 'prefab' tool
  playing: false,
  dirty: false,
  canUndo: false,
  canRedo: false,
  sceneName: '',
  selectedId: null as number | null,
  notice: '',
  prefabs: [] as Prefab[],
  ai: {
    state: 'off' as 'off' | 'starting' | 'ready' | 'failed',
    text: 'AI is off',
    busy: false,
    enabled: false,
    // 'undecided' until the user answers the first-run question; until then nothing runs.
    choice: 'undecided' as 'undecided' | 'on' | 'off',
    reviewPolicy: 'reviewAll' as 'reviewAll' | 'autoApplyTweaks',
  },
  // Set by an Inspector field's "Ask AI"; the AI panel picks it up (n makes repeats distinct).
  aiAsk: { focus: '', n: 0 },
  aiOpen: loadAiOpen(),
  panels: loadPanels(),
  showShortcuts: false, // Help ▸ Keyboard Shortcuts
  grid: false, // tile borders on empty ground (G)
  brushScale: 1, // the prefab brush places copies at this size
})

function savePanels(): void {
  try {
    localStorage.setItem(LAYOUT_PANELS_KEY, JSON.stringify(editorState.panels))
  } catch {
    // Not persisting is fine.
  }
}

export function toggleLeftPanel(): void {
  editorState.panels.left = !editorState.panels.left
  savePanels()
}

export function toggleBottomPanel(): void {
  editorState.panels.bottom = !editorState.panels.bottom
  savePanels()
}

export function runShortcut(name: string): void {
  if (name === 'toggleLeft') toggleLeftPanel()
  else if (name === 'toggleBottom') toggleBottomPanel()
  else if (name === 'toggleAi') setAiOpen(!editorState.aiOpen)
  else if (name === 'help') editorState.showShortcuts = true
}

export function setAiOpen(open: boolean): void {
  editorState.aiOpen = open
  try {
    localStorage.setItem(AI_OPEN_KEY, String(open))
  } catch {
    // Not persisting is fine; the panel just opens next launch.
  }
}

// Turning AI off stops the service, so nothing is sent to the model.
export function setAiEnabled(enabled: boolean): void {
  sendToHost({ type: 'ai:setEnabled', enabled })
}

export function askAiAbout(focus: string): void {
  setAiOpen(true)
  editorState.aiAsk = { focus, n: editorState.aiAsk.n + 1 }
}

export function armPrefab(path: string): void {
  sendToHost({ type: 'tool:set', tool: 'prefab', prefab: path })
}

let noticeTimer: number | undefined

onHostMessage((message) => {
  if (message.type === 'editor:state') {
    editorState.tool = message.tool as ToolId
    editorState.brush = message.brush as string
    editorState.playing = message.playing as boolean
    editorState.dirty = message.dirty as boolean
    editorState.canUndo = message.canUndo as boolean
    editorState.canRedo = message.canRedo as boolean
    editorState.sceneName = message.sceneName as string
    editorState.grid = (message.grid as boolean) ?? false
    editorState.brushScale = (message.brushScale as number) ?? 1
  } else if (message.type === 'selection:changed') {
    editorState.selectedId = (message.id as number | null) ?? null
  } else if (message.type === 'ai:status') {
    editorState.ai.state = message.state as typeof editorState.ai.state
    editorState.ai.text = message.text as string
    editorState.ai.enabled = message.enabled as boolean
    editorState.ai.choice = message.choice as typeof editorState.ai.choice
    editorState.ai.reviewPolicy = message.reviewPolicy as typeof editorState.ai.reviewPolicy
  } else if (message.type === 'ai:busy') {
    editorState.ai.busy = message.busy as boolean
  } else if (message.type === 'prefabs:list') {
    editorState.prefabs = message.prefabs as Prefab[]
  } else if (message.type === 'ui:shortcut') {
    // Pressed while the viewport had focus.
    runShortcut(message.name as string)
  } else if (message.type === 'notify') {
    editorState.notice = message.text as string
    window.clearTimeout(noticeTimer)
    noticeTimer = window.setTimeout(() => (editorState.notice = ''), 3500)
  }
})

// Shortcuts while the web UI has focus. The native viewport handles the
// same keys itself when it has focus (EditorApp::OnViewportKey).
window.addEventListener('keydown', (event) => {
  const target = event.target as HTMLElement | null
  const typing =
    !!target && (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA' || target.isContentEditable)
  const key = event.key.toLowerCase()

  if (event.key === 'F1') {
    event.preventDefault()
    runShortcut('help')
    return
  }
  if (event.key === 'F5') {
    event.preventDefault()
    sendToHost({ type: 'play:toggle' })
    return
  }
  // Scene editing (4.3), when not typing.
  if (!typing && !editorState.playing) {
    if (event.ctrlKey && (key === 'c' || key === 'v')) {
      event.preventDefault()
      sendToHost({ type: key === 'c' ? 'edit:copy' : 'edit:paste' })
      return
    }
    if (!event.ctrlKey && (key === '[' || key === ']')) {
      event.preventDefault()
      const step = event.shiftKey ? 1.02 : 1.1
      sendToHost({ type: 'edit:scale', factor: key === ']' ? step : 1 / step })
      return
    }
    if (!event.ctrlKey && key === 'g') {
      sendToHost({ type: 'view:grid' })
      return
    }
    const arrows: Record<string, [number, number]> = {
      arrowleft: [-1, 0],
      arrowright: [1, 0],
      arrowup: [0, -1],
      arrowdown: [0, 1],
    }
    if (arrows[key] && editorState.selectedId !== null && !event.ctrlKey && !event.altKey) {
      event.preventDefault()
      const step = event.shiftKey ? 0.1 : 1
      sendToHost({ type: 'edit:nudge', dx: arrows[key][0] * step, dy: arrows[key][1] * step })
      return
    }
  }
  if (event.ctrlKey && !typing) {
    const layout: Record<string, string> = { b: 'toggleLeft', j: 'toggleBottom', i: 'toggleAi' }
    if (layout[key]) {
      event.preventDefault()
      runShortcut(layout[key])
      return
    }
  }
  if (event.ctrlKey) {
    const command: Record<string, string> = {
      s: 'scene:save',
      z: event.shiftKey ? 'edit:redo' : 'edit:undo',
      y: 'edit:redo',
      d: 'edit:duplicate',
    }
    // Inside a text field, only Save is ours; undo there means "undo typing".
    if (command[key] && (!typing || key === 's')) {
      event.preventDefault()
      sendToHost({ type: command[key] })
    }
    return
  }
  if (typing || event.altKey) {
    return
  }
  if (key === 'delete') {
    sendToHost({ type: 'edit:delete' })
  } else if (key === 'f') {
    sendToHost({ type: 'view:frame' })
  } else if (key === 'v') {
    sendToHost({ type: 'tool:set', tool: 'select' })
  } else if (key === 'e') {
    sendToHost({ type: 'tool:set', tool: 'erase' })
  } else if (key >= '1' && key <= '9') {
    const prefab = editorState.prefabs[Number(key) - 1]
    if (prefab) {
      armPrefab(prefab.path)
    }
  }
})
