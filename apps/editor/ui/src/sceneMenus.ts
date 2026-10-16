// Right-click menus for scene content (development_plan.md 4.1): the same
// entity menu from the viewport and the Scene Tree, and a menu for empty
// ground. The host reports viewport right-clicks (viewport:contextMenu) with
// what's under the cursor; it has already selected it.
import { openContextMenu, separator, type MenuItem } from './contextMenu'
import { promptDialog } from './dialogs'
import { askAiAbout, editorState } from './editorState'
import { onHostMessage, sendToHost } from './ipc'

export interface MenuEntity {
  id: number
  name: string
  prefab: string | null
  tile: boolean
}

// Extra items other features add (copy/paste, resize, ...), given the target.
export const entityMenuExtensions: ((entity: MenuEntity) => MenuItem[])[] = []
export const groundMenuExtensions: ((cell: [number, number]) => MenuItem[])[] = []

export function entityMenu(entity: MenuEntity): MenuItem[] {
  const select = () => sendToHost({ type: 'entity:select', id: entity.id })
  const label = entity.name || (entity.tile ? 'Tile' : `Entity #${entity.id}`)
  return [
    { label: `Duplicate ${label}`, shortcut: 'Ctrl+D', action: () => (select(), sendToHost({ type: 'edit:duplicate' })) },
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
    { label: 'Delete', shortcut: 'Del', danger: true, action: () => (select(), sendToHost({ type: 'edit:delete' })) },
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
    { label: 'Frame scene', shortcut: 'F', action: () => sendToHost({ type: 'view:frame' }) },
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
    openContextMenu(x, y, entity ? [...entityMenu(entity).slice(0, 1), separator, ...items] : items)
  }
})
