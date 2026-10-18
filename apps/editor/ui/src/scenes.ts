// Scenes (development_plan.md 5.3): a project holds many scenes, one open in
// the editor at a time. The list and its actions are shared by the Scene
// panel (scenes at the top of its tree, the open one expanded), the Scene
// Manager tab, and File ▸ New Scene… / Scenes….
import { reactive, watch } from 'vue'
import { openContextMenu, separator, type MenuItem } from './contextMenu'
import { confirmDialog, promptDialog } from './dialogs'
import { editorState } from './editorState'
import { onHostMessage, sendToHost } from './ipc'
import { menuExtensions } from './menus'

export interface SceneEntry {
  path: string
  name: string
  start: boolean // the game starts here
  open: boolean // open in the editor
  markers: string[]
}

export const sceneList = reactive({
  scenes: [] as SceneEntry[],
  problems: [] as string[], // gateways that lead nowhere
})

onHostMessage((message) => {
  if (message.type === 'scenes:list') {
    sceneList.scenes = message.scenes as SceneEntry[]
    sceneList.problems = (message.problems as string[]) ?? []
  } else if (message.type === 'scene:changed' || message.type === 'file:moved') {
    sendToHost({ type: 'scenes:request' })
  }
})
sendToHost({ type: 'scenes:request' })

// Lets menus switch the bottom panel to a tab (AssetBrowser watches it).
export const bottomTab = reactive({ request: '', n: 0 })

export function showTab(tab: string): void {
  bottomTab.request = tab
  bottomTab.n++
}

export async function newScene(): Promise<void> {
  const name = await promptDialog({
    title: 'New scene',
    message:
      'A name for the new scene in this project (saved as scenes/<name>.aige). It is added to the Scene panel and opened, empty: paint its ground, then add a Marker and a Gateway to connect it to the other scenes.',
    placeholder: 'cellar',
    confirmLabel: 'Create',
  })
  if (name?.trim()) sendToHost({ type: 'scene:new', name: name.trim() })
}

export function openScene(scene: SceneEntry): void {
  if (!scene.open && !editorState.playing) sendToHost({ type: 'scene:open', path: scene.path })
}

export async function renameScene(scene: SceneEntry): Promise<void> {
  const name = await promptDialog({
    title: 'Rename scene',
    message: `New name for ${scene.path}. Gateways that lead to it, and the start scene setting, follow.`,
    value: scene.name,
    confirmLabel: 'Rename',
  })
  if (name?.trim() && name.trim() !== scene.name) sendToHost({ type: 'scene:rename', path: scene.path, name: name.trim() })
}

export async function duplicateScene(scene: SceneEntry): Promise<void> {
  const name = await promptDialog({
    title: 'Duplicate scene',
    message: `A name for the copy of ${scene.path}.`,
    value: `${scene.name}-copy`,
    confirmLabel: 'Duplicate',
  })
  if (name?.trim()) sendToHost({ type: 'scene:duplicate', path: scene.path, name: name.trim() })
}

export async function deleteScene(scene: SceneEntry): Promise<void> {
  if (
    await confirmDialog({
      title: 'Delete scene',
      message: `Delete ${scene.path}? This deletes the file.`,
      confirmLabel: 'Delete',
      danger: true,
    })
  ) {
    sendToHost({ type: 'scene:delete', path: scene.path })
  }
}

export function sceneMenu(scene: SceneEntry, event: MouseEvent): void {
  const items: MenuItem[] = [
    { label: 'Open', disabled: scene.open || editorState.playing, action: () => openScene(scene) },
    { label: 'Set as start scene', disabled: scene.start, action: () => sendToHost({ type: 'scene:setStart', path: scene.path }) },
    separator,
    { label: 'Rename…', disabled: editorState.playing, action: () => renameScene(scene) },
    { label: 'Duplicate…', action: () => duplicateScene(scene) },
    separator,
    { label: 'New scene…', disabled: editorState.playing, action: newScene },
    separator,
    { label: 'Delete…', danger: true, disabled: scene.start || scene.open, action: () => deleteScene(scene) },
  ]
  openContextMenu(event.clientX, event.clientY, items)
}

function updateMenu(): void {
  menuExtensions.file.scenes = [
    { label: 'New Scene…', disabled: editorState.playing, action: newScene },
    { label: 'Scenes…', action: () => showTab('scenes') },
    separator,
  ]
}

watch(() => editorState.playing, updateMenu, { immediate: true })
