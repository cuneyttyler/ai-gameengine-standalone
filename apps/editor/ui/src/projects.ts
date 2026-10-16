// Projects (development_plan.md 4.2): File ▸ New / Open / Recent / Save As,
// and Preferences. The host picks folders with the system dialog and
// relaunches the editor on the chosen project (after asking about unsaved
// changes).
import { reactive } from 'vue'
import { separator, type MenuItem } from './contextMenu'
import { confirmDialog, promptDialog } from './dialogs'
import { editorState } from './editorState'
import { onHostMessage, sendToHost } from './ipc'
import { menuExtensions } from './menus'

export const projectState = reactive({
  current: '',
  name: '',
  recent: [] as string[],
  showPreferences: false,
})

function baseName(path: string): string {
  return path.replace(/[\\/]+$/, '').split(/[\\/]/).pop() ?? path
}

async function newProject(): Promise<void> {
  const name = await promptDialog({
    title: 'New project',
    message: 'A name for the project. Next you pick the folder to create it in.',
    placeholder: 'my-game',
  })
  if (!name?.trim()) return
  const starter = await confirmDialog({
    title: 'Starting content',
    message:
      "Start with the demo's art, prefabs and scripts as a palette? (Its scene, story and canon aren't copied.)",
    confirmLabel: 'Use the demo palette',
    cancelLabel: 'Start empty',
  })
  sendToHost({ type: 'project:new', name: name.trim(), starter })
}

async function saveAs(): Promise<void> {
  const name = await promptDialog({
    title: 'Save project as',
    message: 'A name for the copy. Next you pick where to put it; the editor then opens the copy.',
    value: `${projectState.name}-copy`,
  })
  if (name?.trim()) sendToHost({ type: 'project:saveAs', name: name.trim() })
}

function recentItems(): MenuItem[] {
  const others = projectState.recent.filter((p) => p !== projectState.current).slice(0, 8)
  return others.map((path) => ({
    label: `Recent: ${baseName(path)}`,
    shortcut: path.length > 40 ? '…' + path.slice(-38) : path,
    action: () => sendToHost({ type: 'project:open', path }),
  }))
}

function updateMenus(): void {
  menuExtensions.file.top = [
    { label: 'New Project…', shortcut: 'Ctrl+N', disabled: editorState.playing, action: newProject },
    { label: 'Open Project…', shortcut: 'Ctrl+O', disabled: editorState.playing, action: () => sendToHost({ type: 'project:open' }) },
    ...recentItems(),
    separator,
  ]
  menuExtensions.file.afterSave = [
    { label: 'Save Project As…', shortcut: 'Ctrl+Shift+S', disabled: editorState.playing, action: saveAs },
  ]
  menuExtensions.file.settings = [
    { label: 'Preferences…', action: () => (projectState.showPreferences = true) },
  ]
}

onHostMessage((message) => {
  if (message.type === 'project:info') {
    projectState.current = message.current as string
    projectState.name = baseName(projectState.current)
    projectState.recent = (message.recent as string[]) ?? []
    updateMenus()
  }
})

window.addEventListener('keydown', (event) => {
  if (!event.ctrlKey || editorState.playing) return
  const key = event.key.toLowerCase()
  if (key === 'n') {
    event.preventDefault()
    newProject()
  } else if (key === 'o') {
    event.preventDefault()
    sendToHost({ type: 'project:open' })
  } else if (key === 's' && event.shiftKey) {
    event.preventDefault()
    event.stopImmediatePropagation()
    saveAs()
  }
}, true)

// The viewport forwards these too (EditorApp::OnViewportKey).
onHostMessage((message) => {
  if (message.type !== 'ui:shortcut') return
  if (message.name === 'newProject') newProject()
  else if (message.name === 'saveAs') saveAs()
})

updateMenus()
sendToHost({ type: 'project:request' })
