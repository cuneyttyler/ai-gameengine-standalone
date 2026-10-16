// Where the panels are (development_plan.md 5.6): the Scene, Assets and AI
// panels each dock left, right or bottom, show or hide, and the viewport can
// hide too. Per-user, so localStorage; every access is guarded and the
// defaults always work. Replaces the fixed left/bottom/right layout of 4.1.
import { reactive } from 'vue'
import { sendToHost } from './ipc'

export type PanelId = 'scene' | 'assets' | 'ai'
export type Dock = 'left' | 'right' | 'bottom'

export const panelIds: PanelId[] = ['scene', 'assets', 'ai']
export const panelNames: Record<PanelId, string> = { scene: 'Scene', assets: 'Assets', ai: 'AI' }
export const dockNames: Record<Dock, string> = { left: 'Left', bottom: 'Bottom', right: 'Right' }

interface LayoutState {
  dock: Record<PanelId, Dock>
  visible: Record<PanelId, boolean>
  viewport: boolean
  size: Record<Dock, number> // width of a side dock, height of the bottom one
  weight: Record<PanelId, number> // share of its dock when it has company
  order: PanelId[] // within a dock: earlier is first (top / left)
  treeFraction: number // the Scene panel: the tree's share, the Inspector below or beside it
}

const KEY = 'aige.editor.docks'

function defaults(): LayoutState {
  return {
    dock: { scene: 'left', assets: 'bottom', ai: 'right' },
    visible: { scene: true, assets: true, ai: true },
    viewport: true,
    size: { left: 320, right: 360, bottom: 240 },
    weight: { scene: 1, assets: 1, ai: 1 },
    order: ['scene', 'assets', 'ai'],
    treeFraction: 0.45,
  }
}

function read(key: string): unknown {
  try {
    return JSON.parse(localStorage.getItem(key) ?? 'null')
  } catch {
    return null
  }
}

function load(): LayoutState {
  const state = defaults()
  const saved = read(KEY) as Partial<LayoutState> | null
  if (saved && typeof saved === 'object') {
    for (const id of panelIds) {
      const dock = saved.dock?.[id]
      if (dock === 'left' || dock === 'right' || dock === 'bottom') state.dock[id] = dock
      if (typeof saved.visible?.[id] === 'boolean') state.visible[id] = saved.visible[id]!
      if (typeof saved.weight?.[id] === 'number' && saved.weight[id]! > 0) state.weight[id] = saved.weight[id]!
    }
    for (const dock of ['left', 'right', 'bottom'] as Dock[]) {
      if (typeof saved.size?.[dock] === 'number') state.size[dock] = saved.size[dock]!
    }
    if (typeof saved.viewport === 'boolean') state.viewport = saved.viewport
    if (Array.isArray(saved.order) && saved.order.length === 3 && panelIds.every((id) => saved.order!.includes(id))) {
      state.order = saved.order as PanelId[]
    }
    if (typeof saved.treeFraction === 'number') state.treeFraction = Math.min(0.9, Math.max(0.1, saved.treeFraction))
    return state
  }
  // From the 4.1 layout: which panels were open, and their sizes.
  const panels = read('aige.editor.panels') as { left?: boolean; bottom?: boolean; viewport?: boolean } | null
  if (panels) {
    state.visible.scene = panels.left !== false
    state.visible.assets = panels.bottom !== false
    state.viewport = panels.viewport !== false
  }
  try {
    state.visible.ai = localStorage.getItem('aige.editor.aiOpen') !== 'false'
  } catch {
    // keep the default
  }
  const sizes = read('aige.editor.layout') as Record<string, number> | null
  if (sizes) {
    if (typeof sizes.sidebarWidth === 'number') state.size.left = sizes.sidebarWidth
    if (typeof sizes.aiPanelWidth === 'number') state.size.right = sizes.aiPanelWidth
    if (typeof sizes.assetPanelHeight === 'number') state.size.bottom = sizes.assetPanelHeight
    if (typeof sizes.treeFraction === 'number') state.treeFraction = sizes.treeFraction
  }
  return state
}

export const layout = reactive(load())

export function saveLayout(): void {
  try {
    localStorage.setItem(KEY, JSON.stringify(layout))
  } catch {
    // Not persisting is fine; the layout resets next launch.
  }
}

// The panels docked at `dock`, in order (shown or not).
export function panelsIn(dock: Dock): PanelId[] {
  return layout.order.filter((id) => layout.dock[id] === dock)
}

export function visibleIn(dock: Dock): PanelId[] {
  return panelsIn(dock).filter((id) => layout.visible[id])
}

export function setPanelVisible(id: PanelId, on: boolean): void {
  layout.visible[id] = on
  saveLayout()
}

export function togglePanel(id: PanelId): void {
  setPanelVisible(id, !layout.visible[id])
}

// Moves a panel to another dock, last there, and shows it.
export function movePanel(id: PanelId, dock: Dock): void {
  layout.dock[id] = dock
  layout.order = [...layout.order.filter((p) => p !== id), id]
  layout.visible[id] = true
  saveLayout()
}

// M (5.5): the bottom dock (if it has anything) takes the viewport's space.
export function showViewport(on: boolean): void {
  layout.viewport = on
  sendToHost({ type: 'viewport:visible', on })
  saveLayout()
}

export function toggleViewport(): void {
  showViewport(!layout.viewport)
}
