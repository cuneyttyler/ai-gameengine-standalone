// Projects (development_plan.md 4.2): File ▸ New / Open / Recent / Save As,
// and Preferences. The host picks folders with the system dialog and
// relaunches the editor on the chosen project (after asking about unsaved
// changes).
import { reactive, watch } from 'vue'
import { separator, type MenuItem } from './contextMenu'
import { confirmDialog, promptDialog } from './dialogs'
import { editorState } from './editorState'
import { onHostMessage, sendToHost } from './ipc'
import { menuExtensions } from './menus'
import { keyLabel, keyState, registerAction } from './keys'

export const projectState = reactive({
  current: '',
  name: '',
  recent: [] as string[],
  openLast: false, // start in the last project instead of the welcome window (5.4)
  showPreferences: false,
  showProjectSettings: false, // File ▸ Project Settings (5.5)
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

// File ▸ Recent Projects ▸ (a submenu, 5.5): the full path shows on hover.
function recentItems(): MenuItem[] {
  const others = projectState.recent.filter((p) => p !== projectState.current).slice(0, 10)
  return [
    {
      label: 'Recent Projects',
      disabled: editorState.playing,
      submenu: others.map((path) => ({
        label: baseName(path),
        shortcut: path,
        action: () => sendToHost({ type: 'project:open', path }),
      })),
    },
  ]
}

function updateMenus(): void {
  menuExtensions.file.top = [
    { label: 'New Project…', shortcut: keyLabel('file.new'), disabled: editorState.playing, action: newProject },
    { label: 'Open Project…', shortcut: keyLabel('file.open'), disabled: editorState.playing, action: () => sendToHost({ type: 'project:open' }) },
    ...recentItems(),
    separator,
  ]
  menuExtensions.file.afterSave = [
    { label: 'Save Project As…', shortcut: keyLabel('file.saveAs'), disabled: editorState.playing, action: saveAs },
    { label: 'Close Project', action: () => sendToHost({ type: 'project:close' }) },
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
    projectState.openLast = !!message.openLast
    updateMenus()
  }
})

// Ctrl+N / Ctrl+Shift+S by default; rebindable (keys.ts).
registerAction('file.new', () => {
  if (!editorState.playing) void newProject()
})
registerAction('file.saveAs', () => {
  if (!editorState.playing) void saveAs()
})

updateMenus()
watch(() => keyState.map, updateMenus) // rebound keys show in the menus
sendToHost({ type: 'project:request' })
