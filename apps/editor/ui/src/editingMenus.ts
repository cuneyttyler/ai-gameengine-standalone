// Scene editing menu entries (development_plan.md 4.3): Edit ▸ Copy/Paste,
// View ▸ Tile grid, entity Bigger/Smaller/Copy, ground Paste here, and the
// prefab card's Delete.
import { watch } from 'vue'
import { prefabMenuExtensions } from './assetMenus'
import { editorState } from './editorState'
import { keyLabel, keyState } from './keys'
import { sendToHost } from './ipc'
import { menuExtensions } from './menus'
import { entityMenuExtensions, groundMenuExtensions } from './sceneMenus'

function updateMenus(): void {
  menuExtensions.edit = [
    { label: 'Copy', shortcut: keyLabel('edit.copy'), disabled: editorState.selectedId === null, action: () => sendToHost({ type: 'edit:copy' }) },
    { label: 'Paste at cursor', shortcut: keyLabel('edit.paste'), action: () => sendToHost({ type: 'edit:paste' }) },
    { label: 'Bigger', shortcut: keyLabel('edit.bigger'), disabled: editorState.selectedId === null, action: () => sendToHost({ type: 'edit:scale', factor: 1.1 }) },
    { label: 'Smaller', shortcut: keyLabel('edit.smaller'), disabled: editorState.selectedId === null, action: () => sendToHost({ type: 'edit:scale', factor: 1 / 1.1 }) },
  ]
  menuExtensions.view = [
    { label: 'Frame stats', shortcut: keyLabel('view.stats'), action: () => sendToHost({ type: 'view:stats' }) },
    { label: `${editorState.grid ? '✓ ' : ''}Tile grid on empty ground`, shortcut: keyLabel('view.grid'), action: () => sendToHost({ type: 'view:grid' }) },
  ]
}

watch(() => [editorState.selectedId, editorState.grid, keyState.map], updateMenus, { immediate: true })

entityMenuExtensions.push((entity) => [
  ...(entity.tile
    ? []
    : [{ label: 'Copy', shortcut: keyLabel('edit.copy'), action: () => (sendToHost({ type: 'entity:select', id: entity.id }), sendToHost({ type: 'edit:copy' })) }]),
  // Tiles too: the image resizes, the tile stays one cell (5.2).
  { label: 'Bigger', shortcut: keyLabel('edit.bigger'), action: () => (sendToHost({ type: 'entity:select', id: entity.id }), sendToHost({ type: 'edit:scale', factor: 1.1 })) },
  { label: 'Smaller', shortcut: keyLabel('edit.smaller'), action: () => (sendToHost({ type: 'entity:select', id: entity.id }), sendToHost({ type: 'edit:scale', factor: 1 / 1.1 })) },
  { label: 'Turn a quarter', shortcut: keyLabel('edit.rotate'), action: () => (sendToHost({ type: 'entity:select', id: entity.id }), sendToHost({ type: 'edit:rotate' })) },
])

groundMenuExtensions.push((cell) => [
  { label: 'Paste here', shortcut: keyLabel('edit.paste'), action: () => sendToHost({ type: 'edit:paste', gx: cell[0], gy: cell[1] }) },
])

prefabMenuExtensions.push((prefab) => [
  prefab.flat
    ? {
        label: 'Stand upright (isometric art)',
        disabled: editorState.playing,
        action: () => sendToHost({ type: 'prefab:setFlat', path: prefab.path, flat: false }),
      }
    : {
        label: 'Lay flat on the grid (top-down art)',
        disabled: editorState.playing,
        action: () => sendToHost({ type: 'prefab:setFlat', path: prefab.path, flat: true }),
      },
  {
    label: 'Delete prefab…',
    danger: true,
    confirm: `Delete ${prefab.path}? Its copies in the scene stay, as ordinary entities. Quests that count this prefab will show a problem.`,
    action: () => sendToHost({ type: 'prefab:remove', path: prefab.path }),
  },
])
