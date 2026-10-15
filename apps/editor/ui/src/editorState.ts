// Mirror of the host's editor state. The C++ side (apps/editor/EditorApp)
// is the single source of truth; components read this and send requests,
// never mutate it directly.
import { reactive } from 'vue'
import { onHostMessage, sendToHost } from './ipc'

export type ToolId =
  | 'select'
  | 'tile:grass'
  | 'tile:dirt'
  | 'tile:wall'
  | 'place:npc'
  | 'place:player'
  | 'erase'
  | 'place:sprite'
  | 'tile:asset'
  | 'place:enemy'
  | 'place:pickup'

export const editorState = reactive({
  tool: 'select' as ToolId,
  playing: false,
  dirty: false,
  canUndo: false,
  canRedo: false,
  sceneName: '',
  brush: { asset: '', tile: 0, solid: false },
  selectedId: null as number | null,
  notice: '',
})

let noticeTimer: number | undefined

onHostMessage((message) => {
  if (message.type === 'editor:state') {
    editorState.tool = message.tool as ToolId
    editorState.playing = message.playing as boolean
    editorState.dirty = message.dirty as boolean
    editorState.canUndo = message.canUndo as boolean
    editorState.canRedo = message.canRedo as boolean
    editorState.sceneName = message.sceneName as string
    editorState.brush = message.brush as typeof editorState.brush
  } else if (message.type === 'selection:changed') {
    editorState.selectedId = (message.id as number | null) ?? null
  } else if (message.type === 'notify') {
    editorState.notice = message.text as string
    window.clearTimeout(noticeTimer)
    noticeTimer = window.setTimeout(() => (editorState.notice = ''), 3000)
  }
})

// Shortcuts while the web UI has focus. The native viewport handles the
// same keys itself when it has focus (EditorApp::OnViewportKey).
const toolKeys: Record<string, ToolId> = {
  v: 'select',
  '1': 'tile:grass',
  '2': 'tile:dirt',
  '3': 'tile:wall',
  n: 'place:npc',
  p: 'place:player',
  e: 'erase',
  x: 'place:enemy',
  h: 'place:pickup',
}

window.addEventListener('keydown', (event) => {
  const target = event.target as HTMLElement | null
  const typing = !!target && (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA' || target.isContentEditable)
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
  } else if (toolKeys[key]) {
    sendToHost({ type: 'tool:set', tool: toolKeys[key] })
  }
})
