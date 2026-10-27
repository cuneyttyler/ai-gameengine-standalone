// Right-click menus for scene content (development_plan.md 4.1): the same
// entity menu from the viewport and the Scene Tree, and a menu for empty
// ground. The host reports viewport right-clicks (viewport:contextMenu) with
// what's under the cursor; it has already selected it.
import { openContextMenu, separator, type MenuItem } from './contextMenu'
import { promptDialog } from './dialogs'
import { armPrefab, askAiAbout, editorState } from './editorState'
import { onHostMessage, sendToHost } from './ipc'
import { keyLabel } from './keys'

export interface MenuEntity {
  id: number
  name: string
  prefab: string | null
  tile: boolean
}

// Extra items other features add (copy/paste, resize, ...), given the target.
export const entityMenuExtensions: ((entity: MenuEntity) => MenuItem[])[] = []
export const groundMenuExtensions: ((cell: [number, number]) => MenuItem[])[] = []

// Draw with a prefab: it becomes the brush (a tile painted, a thing placed).
export function drawItem(prefab: string | null): MenuItem[] {
  const p = prefab ? editorState.prefabs.find((x) => x.path === prefab) : undefined
  if (!prefab) return []
  return [{ label: `Draw with ${p?.name ?? prefab.split('/').pop()}`, disabled: editorState.playing, action: () => armPrefab(prefab) }]
}

// Replace prefab…: this thing a copy of another (or every copy of its prefab), in the Replace prefab dialog.
export function replacePrefab(entity: { id: number; prefab: string | null }): void {
  if (!entity.prefab) return
  editorState.replaceEntity = entity.id
  editorState.replacePrefab = entity.prefab
}

export function entityMenu(entity: MenuEntity): MenuItem[] {
  const select = () => sendToHost({ type: 'entity:select', id: entity.id })
  const label = entity.name || (entity.tile ? 'Tile' : `Entity #${entity.id}`)
  return [
    ...drawItem(entity.prefab),
    ...(entity.prefab && !entity.tile ? [{ label: 'Replace prefab…', disabled: editorState.playing, action: () => replacePrefab(entity) }] : []),
    { label: `Duplicate ${label}`, shortcut: keyLabel('edit.duplicate'), action: () => (select(), sendToHost({ type: 'edit:duplicate' })) },
    ...entityMenuExtensions.flatMap((extend) => extend(entity)),
    separator,
    {
      label: 'Save as prefab…',
      disabled: !!entity.prefab,
      action: async () => {
        const name = await promptDialog({
          title: 'Save as prefab',
          message: 'A prefab file name (letters, digits, - and _). This entity becomes its first copy.',
          value: (entity.name || 'prefab').toLowerCase().replace(/[^a-z0-9_-]+/g, '-'),
        })
        if (name) {
          select()
          sendToHost({ type: 'prefab:saveSelected', name })
        }
      },
    },
    {
      label: 'Revert to prefab',
      disabled: !entity.prefab,
      action: () => (select(), sendToHost({ type: 'prefab:revert', key: '' })),
    },
    {
      label: 'Ask AI about this…',
      disabled: editorState.ai.state !== 'ready',
      action: () => askAiAbout(label),
    },
    separator,
    { label: 'Delete', shortcut: keyLabel('edit.delete'), danger: true, action: () => (select(), sendToHost({ type: 'edit:delete' })) },
  ]
}

function groundMenu(cell: [number, number], brush: string): MenuItem[] {
  const prefab = editorState.prefabs.find((p) => p.path === brush)
  return [
    {
      label: prefab ? `${prefab.isTile ? 'Paint' : 'Place'} ${prefab.name} here` : 'Place the brush here',
      disabled: !prefab,
      action: () => sendToHost({ type: 'scene:placeAt', prefab: brush, gx: cell[0], gy: cell[1] }),
    },
    ...groundMenuExtensions.flatMap((extend) => extend(cell)),
    {
      label: 'Add a marker here…',
      action: async () => {
        const name = await promptDialog({
          title: 'Marker',
          message: 'A name for this place, for routines to send people to (e.g. village_well).',
          placeholder: 'village_well',
        })
        if (name) sendToHost({ type: 'scene:addMarker', name, gx: cell[0], gy: cell[1] })
      },
    },
    separator,
    { label: 'Frame scene', shortcut: keyLabel('view.frame'), action: () => sendToHost({ type: 'view:frame' }) },
  ]
}

onHostMessage((message) => {
  if (message.type !== 'viewport:contextMenu') return
  const entity = message.entity as MenuEntity | null
  const x = message.x as number
  const y = message.y as number
  if (entity && !entity.tile) {
    openContextMenu(x, y, entityMenu(entity))
  } else {
    // Ground (or a tile): the tile's own menu first, then what can go there.
    const cell = message.cell as [number, number]
    const items = groundMenu(cell, (message.brush as string) || editorState.brush)
    // A ground tile there (a tile entity's prefab, or a tile layer's cell): Draw with it first.
    const tilePrefab = entity?.prefab ?? ((message.cellPrefab as string) || null)
    const draw = drawItem(tilePrefab)
    const own = entity ? entityMenu(entity).filter((i) => i.label.startsWith('Duplicate')) : []
    openContextMenu(x, y, [...draw, ...own, ...(draw.length || own.length ? [separator] : []), ...items])
  }
})
