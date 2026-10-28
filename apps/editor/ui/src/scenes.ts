// Scenes (development_plan.md 5.3): a project holds many scenes, one open in
// the editor at a time. The list and its actions are shared by the Scene
// panel (scenes at the top of its tree, the open one expanded), the Scene
// Manager tab, and File ▸ New Scene… / Scenes….
import { reactive, watch } from 'vue'
import { openContextMenu, separator, type MenuItem } from './contextMenu'
import { confirmDialog, promptDialog, promptWithChoice } from './dialogs'
import { editorState } from './editorState'
import { onHostMessage, sendToHost } from './ipc'
import { menuExtensions } from './menus'
import { showTab } from './paneltabs'

export interface SceneEntry {
  path: string
  name: string
  start: boolean // the game starts here
  open: boolean // open in the editor
  markers: string[]
  location: { key: string; name: string; summary: string } | null // the World Bible place it shows (6.9)
  type?: string // how it's built: Assembled or Paint (docs/places/scene-types.md); the Build panel and the Planner use it
}

export const SCENE_TYPES = [
  { value: 'Assembled', label: 'Assembled', description: 'each building, tree and prop its own art, put together on the grid; every piece movable' },
  { value: 'Paint', label: 'Paint', description: 'the place painted whole as one picture (or one you give), then marked: where one walks, what one goes behind' },
]

export const sceneList = reactive({
  scenes: [] as SceneEntry[],
  problems: [] as string[], // gateways that lead nowhere
  defaultType: 'Assembled', // the project's (Project Settings ▸ Places ▸ Method): a new scene's
})

onHostMessage((message) => {
  if (message.type === 'scenes:list') {
    sceneList.scenes = message.scenes as SceneEntry[]
    sceneList.problems = (message.problems as string[]) ?? []
    sceneList.defaultType = (message.defaultType as string) ?? 'Assembled'
  } else if (message.type === 'scene:changed' || message.type === 'file:moved') {
    sendToHost({ type: 'scenes:request' })
  }
})
sendToHost({ type: 'scenes:request' })

export { showTab } from './paneltabs'

export async function newScene(): Promise<void> {
  const answer = await promptWithChoice({
    title: 'New scene',
    message:
      'A name for the new scene in this project (saved as scenes/<name>.aige), and how it is built: the Build panel and the Planner build it that way. It is added to the Scene panel and opened, empty.',
    placeholder: 'cellar',
    confirmLabel: 'Create',
    choices: SCENE_TYPES,
    choice: sceneList.defaultType || 'Assembled',
  })
  if (answer?.value.trim()) sendToHost({ type: 'scene:new', name: answer.value.trim(), sceneType: answer.choice })
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
    {
      label: `Type: ${scene.type ?? 'Assembled'}`,
      disabled: editorState.playing,
      submenu: SCENE_TYPES.map((t) => ({
        label: (scene.type ?? 'Assembled') === t.value ? `✓ ${t.label}` : t.label,
        action: () => sendToHost({ type: 'scene:setType', path: scene.path, sceneType: t.value }),
      })),
    },
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
