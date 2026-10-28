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
    { label: 'Bring forward', shortcut: keyLabel('edit.forward'), disabled: editorState.selectedId === null, action: () => sendToHost({ type: 'edit:order', delta: 1 }) },
    { label: 'Send backward', shortcut: keyLabel('edit.backward'), disabled: editorState.selectedId === null, action: () => sendToHost({ type: 'edit:order', delta: -1 }) },
    { label: 'Bring to front', shortcut: keyLabel('edit.toFront'), disabled: editorState.selectedId === null, action: () => sendToHost({ type: 'edit:order', to: 'front' }) },
    { label: 'Send to back', shortcut: keyLabel('edit.toBack'), disabled: editorState.selectedId === null, action: () => sendToHost({ type: 'edit:order', to: 'back' }) },
  ]
  menuExtensions.view = [
    { label: 'Frame stats', shortcut: keyLabel('view.stats'), action: () => sendToHost({ type: 'view:stats' }) },
    { label: 'Physics shapes (colliders, triggers, bodies)', action: () => sendToHost({ type: 'view:physics' }) },
    { label: `${editorState.grid ? '✓ ' : ''}Tile grid on empty ground`, shortcut: keyLabel('view.grid'), action: () => sendToHost({ type: 'view:grid' }) },
    // Interiors (docs/places/interiors.md): the selected building's, or every building's in the scene.
    {
      label: "Selected building's interior / exterior",
      shortcut: keyLabel('view.interior'),
      disabled: editorState.selectedId === null || editorState.playing,
      action: () => sendToHost({ type: 'entity:interior' }),
    },
    { label: 'All interiors / exteriors', shortcut: keyLabel('view.interiors'), disabled: editorState.playing, action: () => sendToHost({ type: 'interiors:showAll' }) },
    { label: 'Floor up', shortcut: keyLabel('view.floorUp'), disabled: editorState.playing, action: () => sendToHost({ type: 'interiors:floor', delta: 1 }) },
    { label: 'Floor down', shortcut: keyLabel('view.floorDown'), disabled: editorState.playing, action: () => sendToHost({ type: 'interiors:floor', delta: -1 }) },
  ]
}

watch(() => [editorState.selectedId, editorState.grid, editorState.playing, keyState.map], updateMenus, { immediate: true })

entityMenuExtensions.push((entity) => [
  ...(entity.tile
    ? []
    : [{ label: 'Copy', shortcut: keyLabel('edit.copy'), action: () => (sendToHost({ type: 'entity:select', id: entity.id }), sendToHost({ type: 'edit:copy' })) }]),
  // Tiles too: the image resizes, the tile stays one cell (5.2).
  { label: 'Bigger', shortcut: keyLabel('edit.bigger'), action: () => (sendToHost({ type: 'entity:select', id: entity.id }), sendToHost({ type: 'edit:scale', factor: 1.1 })) },
  { label: 'Smaller', shortcut: keyLabel('edit.smaller'), action: () => (sendToHost({ type: 'entity:select', id: entity.id }), sendToHost({ type: 'edit:scale', factor: 1 / 1.1 })) },
  { label: 'Turn a quarter', shortcut: keyLabel('edit.rotate'), action: () => (sendToHost({ type: 'entity:select', id: entity.id }), sendToHost({ type: 'edit:rotate' })) },
  ...(entity.tile
    ? []
    : [
        { label: 'Bring forward (ground)', shortcut: keyLabel('edit.forward'), action: () => (sendToHost({ type: 'entity:select', id: entity.id }), sendToHost({ type: 'edit:order', delta: 1 })) },
        { label: 'Send backward (ground)', shortcut: keyLabel('edit.backward'), action: () => (sendToHost({ type: 'entity:select', id: entity.id }), sendToHost({ type: 'edit:order', delta: -1 })) },
      ]),
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
