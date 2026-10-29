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
  interior?: boolean | null // a building with a cutaway interior: whether it shows now; null/absent: none
  interiorFloors?: number // its floors (one picture each)
  interiorFloor?: number // the one shown
  interiorScene?: string // a building whose interior is its own scene: that scene ("" not built yet)
  building?: boolean // it has a footprint (an Obstacle): it can have an interior scene
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
    ...(typeof entity.interior === 'boolean'
      ? [{ label: entity.interior ? 'Show exterior' : (entity.interiorFloors ?? 1) > 1 ? 'Show interior (ground floor)' : 'Show interior',
           shortcut: keyLabel('view.interior'), disabled: editorState.playing,
           action: () => sendToHost({ type: 'entity:interior', id: entity.id, shown: !entity.interior }) }]
      : []),
    // A building of several storeys: each floor's interior (H steps through them).
    ...Array.from({ length: Math.max(0, (entity.interiorFloors ?? 1) - 1) }, (_, i) => i + 2)
      .filter((k) => !(entity.interior && entity.interiorFloor === k))
      .map((k) => ({ label: `Show floor ${k}`, disabled: editorState.playing, action: () => sendToHost({ type: 'entity:interior', id: entity.id, shown: true, floor: k }) })),
    // Fewer floors than it was painted with (two storeys counted as three): its upper floors past that dropped.
    ...((entity.interiorFloors ?? 1) > 1
      ? [{ label: 'Floors', disabled: editorState.playing, submenu: Array.from({ length: (entity.interiorFloors ?? 1) - 1 }, (_, i) => i + 1)
            .map((k) => ({ label: `${k} ${k === 1 ? 'floor' : 'floors'}`, action: () => sendToHost({ type: 'interior:setFloors', id: entity.id, floors: k }) })) }]
      : []),
    // A part of its picture animated (a door, a window, a sign; a part of a painted scene): docs/assets/animations.md §4.
    ...(!entity.tile ? [{ label: 'Animate a part…', disabled: editorState.playing, action: () => (editorState.partDialog = entity.id) }] : []),
    // None yet: one added (on its prefab, so every copy has it).
    ...(!entity.tile && typeof entity.interior !== 'boolean' && entity.interiorScene === undefined
      ? [
          { label: 'Add interior: cutaway', disabled: editorState.playing, action: () => sendToHost({ type: 'interior:add', id: entity.id, kind: 'cutaway' }) },
          { label: 'Add interior: its own scene', disabled: editorState.playing, action: () => sendToHost({ type: 'interior:add', id: entity.id, kind: 'scene' }) },
        ]
      : []),
    // Its interior as its own scene (docs/places/interiors.md §2b): opened, or built in the Build panel.
    ...(entity.interiorScene
      ? [{ label: 'Open interior scene', disabled: editorState.playing, action: () => sendToHost({ type: 'entity:openInterior', id: entity.id }) }]
      : []),
    ...(entity.building && typeof entity.interior !== 'boolean'
      ? [{
          label: entity.interiorScene ? 'Build its interior scene again…' : 'Build its interior scene…',
          disabled: editorState.playing,
          action: () => sendToHost({ type: 'entity:buildInterior', id: entity.id }),
        }]
      : []),
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
    // High ground: characters walk between heights only where a ramp is (stairs, a slope, a ladder); elsewhere a cliff.
    { label: 'Ramp here (walk between heights)', action: () => sendToHost({ type: 'scene:rampHere', gx: cell[0], gy: cell[1] }) },
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
    // Buildings with interiors (Phase 27): all shown inside or out.
    const interiors = message.interiors as { count: number; shown: number } | undefined
    if (interiors?.count) {
      items.splice(items.length - 2, 0, interiors.shown < interiors.count
        ? { label: `Show interiors (${interiors.count})`, shortcut: keyLabel('view.interiors'), action: () => sendToHost({ type: 'interiors:showAll', shown: true }) }
        : { label: 'Show exteriors', shortcut: keyLabel('view.interiors'), action: () => sendToHost({ type: 'interiors:showAll', shown: false }) })
    }
    // A ground tile there (a tile entity's prefab, or a tile layer's cell): Draw with it first.
    const tilePrefab = entity?.prefab ?? ((message.cellPrefab as string) || null)
    const draw = drawItem(tilePrefab)
    const own = entity ? entityMenu(entity).filter((i) => i.label.startsWith('Duplicate')) : []
    // A built place's ground there: its picture made an asset (a tile prefab) to paint elsewhere.
    if (message.layerCell) {
      own.push({ label: 'Make an asset from this cell', action: () => sendToHost({ type: 'cell:makeAsset', x: cell[0], y: cell[1] }) })
    }
    openContextMenu(x, y, [...draw, ...own, ...(draw.length || own.length ? [separator] : []), ...items])
  }
})
