// Scenes (development_plan.md 5.3): File ▸ New Scene… and Scenes…, shared
// with the Scene Manager tab.
import { reactive, watch } from 'vue'
import { promptDialog } from './dialogs'
import { editorState } from './editorState'
import { sendToHost } from './ipc'
import { menuExtensions } from './menus'

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
      'A name for the scene (it becomes scenes/<name>.aige). It opens empty: paint its ground, then add a Marker and a Gateway to connect it.',
    placeholder: 'cellar',
    confirmLabel: 'Create',
  })
  if (name?.trim()) sendToHost({ type: 'scene:new', name: name.trim() })
}

function updateMenu(): void {
  const items = (menuExtensions.file.top ?? []).filter((i) => i.label !== 'New Scene…' && i.label !== 'Scenes…')
  menuExtensions.file.top = [
    ...items,
    { label: 'New Scene…', disabled: editorState.playing, action: newScene },
    { label: 'Scenes…', action: () => showTab('scenes') },
  ]
}

watch(() => [editorState.playing, menuExtensions.file.top?.length], updateMenu, { immediate: true })
