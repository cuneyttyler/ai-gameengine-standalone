// Scene editing menu entries (development_plan.md 4.3): Edit ▸ Copy/Paste,
// View ▸ Tile grid, entity Bigger/Smaller/Copy, ground Paste here, and the
// prefab card's Delete.
import { watch } from 'vue'
import { prefabMenuExtensions } from './assetMenus'
import { editorState } from './editorState'
import { sendToHost } from './ipc'
import { menuExtensions } from './menus'
import { entityMenuExtensions, groundMenuExtensions } from './sceneMenus'

function updateMenus(): void {
  menuExtensions.edit = [
    { label: 'Copy', shortcut: 'Ctrl+C', disabled: editorState.selectedId === null, action: () => sendToHost({ type: 'edit:copy' }) },
    { label: 'Paste at cursor', shortcut: 'Ctrl+V', action: () => sendToHost({ type: 'edit:paste' }) },
    { label: 'Bigger', shortcut: ']', disabled: editorState.selectedId === null, action: () => sendToHost({ type: 'edit:scale', factor: 1.1 }) },
    { label: 'Smaller', shortcut: '[', disabled: editorState.selectedId === null, action: () => sendToHost({ type: 'edit:scale', factor: 1 / 1.1 }) },
  ]
  menuExtensions.view = [
    { label: `${editorState.grid ? '✓ ' : ''}Tile grid on empty ground`, shortcut: 'G', action: () => sendToHost({ type: 'view:grid' }) },
  ]
}

watch(() => [editorState.selectedId, editorState.grid], updateMenus, { immediate: true })

entityMenuExtensions.push((entity) =>
  entity.tile
    ? []
    : [
        { label: 'Copy', shortcut: 'Ctrl+C', action: () => (sendToHost({ type: 'entity:select', id: entity.id }), sendToHost({ type: 'edit:copy' })) },
        { label: 'Bigger', shortcut: ']', action: () => (sendToHost({ type: 'entity:select', id: entity.id }), sendToHost({ type: 'edit:scale', factor: 1.1 })) },
        { label: 'Smaller', shortcut: '[', action: () => (sendToHost({ type: 'entity:select', id: entity.id }), sendToHost({ type: 'edit:scale', factor: 1 / 1.1 })) },
      ],
)

groundMenuExtensions.push((cell) => [
  { label: 'Paste here', shortcut: 'Ctrl+V', action: () => sendToHost({ type: 'edit:paste', gx: cell[0], gy: cell[1] }) },
])

prefabMenuExtensions.push((prefab) => [
  {
    label: 'Delete prefab…',
    danger: true,
    confirm: `Delete ${prefab.path}? Its copies in the scene stay, as ordinary entities. Quests that count this prefab will show a problem.`,
    action: () => sendToHost({ type: 'prefab:remove', path: prefab.path }),
  },
])
