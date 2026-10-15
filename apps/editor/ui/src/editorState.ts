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

export interface Prefab {
  path: string
  name: string
  error: string
  isTile: boolean
  isPlayer: boolean
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
  ai: { state: 'off' as 'off' | 'starting' | 'ready' | 'failed', text: 'AI is off', busy: false, enabled: true },
  // Set by an Inspector field's "Ask AI"; the AI panel picks it up (n makes repeats distinct).
  aiAsk: { focus: '', n: 0 },
  aiOpen: loadAiOpen(),
})

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
  } else if (message.type === 'selection:changed') {
    editorState.selectedId = (message.id as number | null) ?? null
  } else if (message.type === 'ai:status') {
    editorState.ai.state = message.state as typeof editorState.ai.state
    editorState.ai.text = message.text as string
    editorState.ai.enabled = (message.enabled as boolean | undefined) ?? true
  } else if (message.type === 'ai:busy') {
    editorState.ai.busy = message.busy as boolean
  } else if (message.type === 'prefabs:list') {
    editorState.prefabs = message.prefabs as Prefab[]
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
