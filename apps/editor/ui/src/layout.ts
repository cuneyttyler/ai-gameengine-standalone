// Where the panels are (development_plan.md 5.6, 6.8). The layout is a tree
// (docktree.ts): splits whose leaves are tab groups, holding the Scene, Assets
// and AI panels and the viewport, each of which can be dragged anywhere: onto
// another group as a tab, beside it, or along a window edge. Hidden panels
// leave the tree and come back where they were.
//
// Saved **per project**: every change goes to the host, and File ▸ Save writes
// it to <project>/editor-layout.json, restored when the project opens. The
// layout last used anywhere is also kept in localStorage, as the start for a
// project that has no saved layout yet. Every access is guarded and the
// default always works.
import { reactive, watch } from 'vue'
import {
  activate,
  allPanels,
  defaultTree,
  dropPanel,
  edgeArea,
  findGroup,
  groupOf,
  homeSide,
  isShown,
  panelsIn,
  parseTree,
  removePanel,
  resizePair,
  setShare,
  shareOf,
  type DockNode,
  type DropTarget,
  type PanelId,
  type Side,
} from './docktree'
import { onHostMessage, sendToHost } from './ipc'

export type { PanelId, Side, DropTarget } from './docktree'

export const panelIds: PanelId[] = allPanels
export const panelNames: Record<PanelId, string> = {
  viewport: 'Viewport',
  scene: 'Scene',
  assets: 'Assets',
  project: 'Project',
  generate: 'Generate',
  ai: 'AI',
  build: 'Build',
  planner: 'Planner',
}
export const sideNames: Record<Side, string> = { left: 'Left', top: 'Top', right: 'Right', bottom: 'Bottom' }

export interface Rect {
  x: number
  y: number
  width: number
  height: number
}

interface LayoutState {
  known?: PanelId[] // the panels there were when this was saved
  root: DockNode | null
  lastGroup: Partial<Record<PanelId, string>> // where a hidden panel was
  treeFraction: number // the Scene panel: the tree's share, the Inspector below or beside it
}

const KEY = 'aige.editor.layout3' // 3: the bottom panels under the left one too (older layouts start from the default)
const LAYOUT_VERSION = 3

function read(key: string): unknown {
  try {
    return JSON.parse(localStorage.getItem(key) ?? 'null')
  } catch {
    return null
  }
}

// From the 5.6 layout (three docks around the viewport): the same panels in
// the same places, now as a tree.
function migrate(): LayoutState | null {
  const old = read('aige.editor.docks') as {
    dock?: Record<string, string>
    visible?: Record<string, boolean>
    viewport?: boolean
    size?: Record<string, number>
    order?: string[]
    treeFraction?: number
  } | null
  if (!old || typeof old !== 'object' || !old.dock) return null
  const order = (Array.isArray(old.order) ? old.order : ['scene', 'assets', 'ai']).filter((id) =>
    ['scene', 'assets', 'ai'].includes(id),
  ) as PanelId[]
  const inDock = (dock: string) => order.filter((id) => old.dock?.[id] === dock && old.visible?.[id] !== false)
  let n = 0
  const group = (id: PanelId): DockNode => ({ kind: 'tabs', id: `g${++n}`, tabs: [id], active: id })
  const stack = (ids: PanelId[], dir: 'row' | 'column'): DockNode | null =>
    ids.length === 0 ? null : ids.length === 1 ? group(ids[0]!) : { kind: 'split', id: `s${++n}`, dir, children: ids.map(group), sizes: ids.map(() => 1 / ids.length) }
  const width = Math.max(800, window.innerWidth)
  const height = Math.max(600, window.innerHeight)
  const left = stack(inDock('left'), 'column')
  const right = stack(inDock('right'), 'column')
  const bottom = stack(inDock('bottom'), 'row')
  const view = old.viewport === false ? null : group('viewport')
  const centerParts = [view, bottom].filter(Boolean) as DockNode[]
  const bottomShare = Math.min(0.6, Math.max(0.15, (old.size?.bottom ?? 240) / height))
  const center: DockNode | null =
    centerParts.length === 2
      ? { kind: 'split', id: `s${++n}`, dir: 'column', children: centerParts, sizes: [1 - bottomShare, bottomShare] }
      : (centerParts[0] ?? null)
  const leftShare = Math.min(0.4, (old.size?.left ?? 320) / width)
  const rightShare = Math.min(0.4, (old.size?.right ?? 360) / width)
  const row = [left, center, right]
  const sizes = [leftShare, 1 - leftShare - rightShare, rightShare]
  const root = parseTree({
    kind: 'split',
    id: `s${++n}`,
    dir: 'row',
    children: row.filter(Boolean),
    sizes: sizes.filter((_, i) => row[i]),
  })
  return {
    root: addNewPanels(root ?? defaultTree(), undefined),
    lastGroup: {},
    treeFraction: typeof old.treeFraction === 'number' ? Math.min(0.9, Math.max(0.1, old.treeFraction)) : 0.45,
  }
}

// A panel that didn't exist when a layout was saved (Project and Generate,
// split out of Assets in 6.8) joins Assets' group as a tab, so it's where its
// tabs used to be.
// Panels that stay hidden until they have something to show (the Build panel
// opens when a build starts, 7.4).
const startsHidden: PanelId[] = ['build', 'planner']

// Hidden on purpose (Ctrl+J), read before the layout is (it's loaded below, and dockView after it).
let bottomOut = (() => {
  try {
    return JSON.parse(localStorage.getItem('aige.editor.bottomHidden2') ?? 'null') !== null
  } catch {
    return false
  }
})()

// The bottom panels are always there: a layout without a bottom area (its tabs all closed or moved away) gets Assets
// docked along the bottom edge again.
function withBottom(root: DockNode | null): DockNode | null {
  if (!root || bottomOut || edgeArea(root, 'bottom')) return root
  return dropPanel(root, 'assets', { kind: 'edge', side: 'bottom' })
}

function addNewPanels(root: DockNode | null, known: PanelId[] | undefined): DockNode | null {
  const before = known ?? (['viewport', 'scene', 'assets', 'ai'] as PanelId[])
  let next = root
  for (const id of panelIds) {
    if (before.includes(id) || panelsIn(next).includes(id) || startsHidden.includes(id)) continue
    const assets = groupOf(next, 'assets')
    const active = assets?.active
    next = dropPanel(next, id, assets ? { kind: 'tab', group: assets.id } : { kind: 'edge', side: homeSide[id] })
    if (active) next = activate(next, active)
  }
  return next
}

function load(): LayoutState {
  const saved = read(KEY) as Partial<LayoutState> | null
  if (saved && typeof saved === 'object' && 'root' in saved) {
    const fresh = (saved as { version?: number }).version !== LAYOUT_VERSION
    const root = saved.root === null || fresh ? null : addNewPanels(parseTree(saved.root), saved.known)
    return {
      root: withBottom(root ?? defaultTree()),
      lastGroup: saved.lastGroup && typeof saved.lastGroup === 'object' ? saved.lastGroup : {},
      treeFraction: typeof saved.treeFraction === 'number' ? Math.min(0.9, Math.max(0.1, saved.treeFraction)) : 0.45,
    }
  }
  return migrate() ?? { root: defaultTree(), lastGroup: {}, treeFraction: 0.45 }
}

export const layout = reactive(load())

// What the editor measures and drags (App.vue fills these in).
export const dockView = reactive({
  groupRects: {} as Record<string, Rect>, // each tab group's whole box, in the shell's coordinates
  shell: { width: 0, height: 0 }, // the area the layout fills
  dragging: null as PanelId | null,
  // Double-clicking a tab: that panel covers the whole layout, the others are
  // out of sight (still mounted); double-clicking again brings them back.
  maximized: null as PanelId | null,
  focused: null as PanelId | null, // the panel last clicked in (the maximise shortcut's)
  // The bottom area hidden for now (Ctrl+J): taken out of the layout (so everything else takes its room, as when a
  // panel moves), what it held kept to put back, its panels, the one in front and its share of the height.
  bottomHidden: false,
})

interface HiddenBottom {
  panels: PanelId[]
  active: PanelId | null
  share: number
}
let hiddenBottom: HiddenBottom | null = (() => {
  try {
    const kept = JSON.parse(localStorage.getItem('aige.editor.bottomHidden2') ?? 'null')
    return kept && Array.isArray(kept.panels) ? (kept as HiddenBottom) : null
  } catch {
    return null
  }
})()
dockView.bottomHidden = hiddenBottom !== null

function keepHidden(): void {
  try {
    localStorage.setItem('aige.editor.bottomHidden2', JSON.stringify(hiddenBottom))
  } catch {
    // a convenience: not kept is fine
  }
}

export function toggleBottom(): void {
  if (!hiddenBottom) {
    const area = edgeArea(layout.root, 'bottom')
    const panels = panelsIn(area)
    if (!area || !panels.length) return
    const front = panels.find((p) => isFront(p)) ?? null
    hiddenBottom = { panels, active: front, share: shareOf(layout.root, area.id) }
    let next = layout.root
    for (const p of panels) next = removePanel(next, p)
    dockView.bottomHidden = bottomOut = true
    keepHidden()
    setRoot(next)
    return
  }
  // Back along the bottom edge: its first panel docked there, the rest its tabs, at the height it had.
  const kept = hiddenBottom
  hiddenBottom = null
  dockView.bottomHidden = bottomOut = false
  keepHidden()
  const panels = kept.panels.filter((p) => !isOpen(p))
  let next = layout.root
  if (panels.length) {
    next = dropPanel(next, panels[0]!, { kind: 'edge', side: 'bottom' })
    const group = groupOf(next, panels[0]!)
    for (const p of panels.slice(1)) if (group) next = dropPanel(next, p, { kind: 'tab', group: group.id })
    if (kept.active && panels.includes(kept.active)) next = activate(next, kept.active)
    const area = edgeArea(next, 'bottom')
    if (area && kept.share > 0) next = setShare(next, area.id, kept.share)
  }
  setRoot(next)
}

export function toggleMaximize(id: PanelId): void {
  dockView.maximized = dockView.maximized === id || !isOpen(id) ? null : id
}

function snapshot(): LayoutState {
  return { known: [...panelIds], root: layout.root, lastGroup: layout.lastGroup, treeFraction: layout.treeFraction, version: LAYOUT_VERSION } as LayoutState
}

export function saveLayout(): void {
  const state = snapshot()
  try {
    localStorage.setItem(KEY, JSON.stringify(state))
  } catch {
    // Not persisting here is fine: the project keeps its own copy.
  }
  sendToHost({ type: 'layout:set', layout: JSON.parse(JSON.stringify(state)) })
}

// The project's own layout, when it has one, replaces the starting one.
function applySaved(saved: unknown): void {
  if (!saved || typeof saved !== 'object' || !('root' in saved)) return
  const s = saved as Partial<LayoutState>
  if ((s as { version?: number }).version !== LAYOUT_VERSION) return // laid out before the bottom panels moved: the default
  const root = s.root === null ? null : addNewPanels(parseTree(s.root), s.known)
  if (s.root !== null && !root) return
  layout.root = withBottom(root)
  layout.lastGroup = s.lastGroup && typeof s.lastGroup === 'object' ? s.lastGroup : {}
  if (typeof s.treeFraction === 'number') layout.treeFraction = Math.min(0.9, Math.max(0.1, s.treeFraction))
  try {
    localStorage.setItem(KEY, JSON.stringify(snapshot()))
  } catch {
    // fine
  }
}

onHostMessage((message) => {
  if (message.type === 'layout:state') applySaved(message.layout)
  else if (message.type === 'panel:focused') dockView.focused = message.id as PanelId // the viewport, clicked in its own window
})
sendToHost({ type: 'layout:request' })

function setRoot(root: DockNode | null): void {
  layout.root = withBottom(root)
  saveLayout()
}

// In the layout at all (possibly as a background tab).
export function isOpen(id: PanelId): boolean {
  return panelsIn(layout.root).includes(id)
}

// On screen: open and its group's front tab.
export function isFront(id: PanelId): boolean {
  return isShown(layout.root, id)
}

export function hiddenPanels(): PanelId[] {
  return panelIds.filter((id) => !isOpen(id))
}

// Showing brings a panel to the front, back where it was if that group still
// exists, else along its usual window edge. Hiding takes it out of the layout.
export function setPanelVisible(id: PanelId, on: boolean): void {
  // Showing the panel that's maximised keeps it so (a build started in a full-window Build
  // panel); showing or hiding any other brings the whole layout back.
  if (on && isOpen(id) && dockView.maximized === id) {
    setRoot(activate(layout.root, id))
    return
  }
  dockView.maximized = null
  if (on) {
    // A panel of the hidden bottom area: the area shown again.
    if (hiddenBottom && hiddenBottom.panels.includes(id)) {
      toggleBottom()
      setRoot(activate(layout.root, id))
      return
    }
    if (isOpen(id)) {
      setRoot(activate(layout.root, id))
      return
    }
    const back = layout.lastGroup[id]
    const target: DropTarget = back && findGroup(layout.root, back) ? { kind: 'tab', group: back } : { kind: 'edge', side: homeSide[id] }
    setRoot(dropPanel(layout.root, id, target))
    return
  }
  const group = groupOf(layout.root, id)
  if (!group) return
  // The bottom area's only panel: the area hidden (Ctrl+J), not taken away (it's always there).
  if (inBottom(id) && panelsIn(edgeArea(layout.root, 'bottom')).length === 1) {
    if (!dockView.bottomHidden) toggleBottom()
    return
  }
  layout.lastGroup[id] = group.id
  setRoot(removePanel(layout.root, id))
}

// In the bottom area (its group there).
function inBottom(id: PanelId): boolean {
  return panelsIn(edgeArea(layout.root, 'bottom')).includes(id)
}

// The panel shortcuts (B, J, I, M): a panel on screen hides; one behind
// another tab comes to the front; a hidden one comes back.
export function togglePanel(id: PanelId): void {
  // Another panel full-window: this one isn't seen, whatever its tab says, so its shortcut shows it (the layout back).
  if (dockView.maximized && dockView.maximized !== id) {
    setPanelVisible(id, true)
    return
  }
  setPanelVisible(id, !isFront(id))
}

export function toggleViewport(): void {
  togglePanel('viewport')
}

export function showViewport(on: boolean): void {
  setPanelVisible('viewport', on)
}

export function movePanel(id: PanelId, side: Side): void {
  setRoot(dropPanel(layout.root, id, { kind: 'edge', side }))
}

export function applyDrop(id: PanelId, target: DropTarget): void {
  dockView.maximized = null
  setRoot(dropPanel(layout.root, id, target))
}

export function activatePanel(id: PanelId): void {
  setRoot(activate(layout.root, id))
}

// During a splitter drag (saved when it ends, App.vue).
export function resizeSplit(splitId: string, index: number, first: number): void {
  layout.root = resizePair(layout.root, splitId, index, first)
}

export function resetLayout(): void {
  dockView.maximized = null
  layout.lastGroup = {}
  setRoot(defaultTree())
}

// The native viewport window floats above the page, so it is hidden whenever
// its tab isn't in front, and while a panel is dragged (the drop markers must
// show over its area).
let lastSent: boolean | null = null
export function syncViewportWindow(): void {
  const max = dockView.maximized
  const on = (max ? max === 'viewport' : isFront('viewport')) && dockView.dragging === null
  if (on !== lastSent) {
    lastSent = on
    sendToHost({ type: 'viewport:visible', on })
  }
}
watch(() => [layout.root, dockView.dragging, dockView.maximized], syncViewportWindow, { deep: true })
