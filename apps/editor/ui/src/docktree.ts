// The editor's layout as a tree (development_plan.md 6.8): splits (rows or
// columns of children with size shares) whose leaves are tab groups. Every
// panel, the viewport included, lives in exactly one group or is hidden.
// Dragging a panel onto a group adds it as a tab; onto a group's side splits
// that group; onto the window's edge docks it along the whole edge.
//
// Each window edge has at most one **edge area** (a node tagged `edge`): docking
// a panel on an edge that already has one puts it next to what's there, along
// the edge, instead of adding another strip.
//
// Pure functions over plain data: no Vue, no DOM, so tests/docktree.test.ts
// can run them directly. Each returns a new tree; inputs are never changed.

export type PanelId = 'viewport' | 'scene' | 'assets' | 'project' | 'generate' | 'ai' | 'build' | 'planner'
export type Side = 'left' | 'right' | 'top' | 'bottom'
export type Direction = 'row' | 'column'

export interface TabsNode {
  kind: 'tabs'
  id: string
  tabs: PanelId[]
  active: PanelId
  edge?: Side // this node is the area docked on that window edge
}

export interface SplitNode {
  kind: 'split'
  id: string
  dir: Direction
  children: DockNode[]
  sizes: number[] // shares of the split, one per child, summing to 1
  edge?: Side
}

export type DockNode = TabsNode | SplitNode

export type DropTarget =
  | { kind: 'tab'; group: string; index?: number } // into a group's tabs (at index, else last)
  | { kind: 'side'; group: string; side: Side } // beside a group, splitting it
  | { kind: 'edge'; side: Side } // along a whole edge of the window

export const allPanels: PanelId[] = ['viewport', 'scene', 'assets', 'project', 'generate', 'ai', 'build', 'planner']

const EDGE_SHARE = 0.22 // a panel docked on a window edge
const SIDE_SHARE = 0.4 // a panel docked beside a group: its part of that group's space
const MIN_SHARE = 0.03

function clone<T>(value: T): T {
  return JSON.parse(JSON.stringify(value)) as T
}

function dirOf(side: Side): Direction {
  return side === 'left' || side === 'right' ? 'row' : 'column'
}

function before(side: Side): boolean {
  return side === 'left' || side === 'top'
}

// --- queries -----------------------------------------------------------------

export function groups(root: DockNode | null): TabsNode[] {
  if (!root) return []
  if (root.kind === 'tabs') return [root]
  return root.children.flatMap(groups)
}

export function panelsIn(root: DockNode | null): PanelId[] {
  return groups(root).flatMap((g) => g.tabs)
}

export function groupOf(root: DockNode | null, panel: PanelId): TabsNode | null {
  return groups(root).find((g) => g.tabs.includes(panel)) ?? null
}

export function findGroup(root: DockNode | null, id: string): TabsNode | null {
  return groups(root).find((g) => g.id === id) ?? null
}

// The area docked on a window edge, if there is one.
export function edgeArea(root: DockNode | null, side: Side): DockNode | null {
  if (!root) return null
  if (root.edge === side) return root
  if (root.kind === 'split') {
    for (const child of root.children) {
      const found = edgeArea(child, side)
      if (found) return found
    }
  }
  return null
}

// A panel is on screen when it's in the tree and its group's active tab.
export function isShown(root: DockNode | null, panel: PanelId): boolean {
  const group = groupOf(root, panel)
  return !!group && group.active === panel
}

function allIds(root: DockNode | null): string[] {
  if (!root) return []
  if (root.kind === 'tabs') return [root.id]
  return [root.id, ...root.children.flatMap(allIds)]
}

function newId(root: DockNode | null, prefix: 'g' | 's'): string {
  const taken = new Set(allIds(root))
  for (let n = 1; ; n++) {
    if (!taken.has(`${prefix}${n}`)) return `${prefix}${n}`
  }
}

// --- tidying -------------------------------------------------------------------

// Drops empty groups, collapses one-child splits, merges a split into its parent
// when they run the same way, and keeps every split's sizes valid and summing to 1.
export function normalize(root: DockNode | null): DockNode | null {
  if (!root) return null
  if (root.kind === 'tabs') {
    if (root.tabs.length === 0) return null
    if (!root.tabs.includes(root.active)) root.active = root.tabs[0]!
    return root
  }
  const children: DockNode[] = []
  const sizes: number[] = []
  root.children.forEach((child, i) => {
    const kept = normalize(child)
    if (!kept) return
    const share = Number.isFinite(root.sizes[i]) && root.sizes[i]! > 0 ? root.sizes[i]! : 1 / root.children.length
    if (kept.kind === 'split' && kept.dir === root.dir && !kept.edge) {
      const inner = kept.sizes.reduce((a, b) => a + b, 0) || 1
      kept.children.forEach((grand, j) => {
        children.push(grand)
        sizes.push((share * kept.sizes[j]!) / inner)
      })
    } else {
      children.push(kept)
      sizes.push(share)
    }
  })
  if (children.length === 0) return null
  if (children.length === 1) {
    const only = children[0]!
    if (root.edge && !only.edge) only.edge = root.edge // the area lives on in its last part
    return only
  }
  const total = sizes.reduce((a, b) => a + b, 0)
  root.children = children
  root.sizes = sizes.map((s) => Math.max(MIN_SHARE, s / total))
  const fixed = root.sizes.reduce((a, b) => a + b, 0)
  root.sizes = root.sizes.map((s) => s / fixed)
  return root
}

// --- changes -------------------------------------------------------------------

export function removePanel(root: DockNode | null, panel: PanelId): DockNode | null {
  const next = clone(root)
  const group = groupOf(next, panel)
  if (!group) return next
  const at = group.tabs.indexOf(panel)
  group.tabs.splice(at, 1)
  if (group.active === panel && group.tabs.length) group.active = group.tabs[Math.max(0, at - 1)]!
  return normalize(next)
}

function replace(root: DockNode, id: string, make: (node: DockNode) => DockNode): DockNode {
  if (root.id === id) return make(root)
  if (root.kind === 'split') {
    root.children = root.children.map((child) => replace(child, id, make))
  }
  return root
}

function parentOf(root: DockNode, id: string): SplitNode | null {
  if (root.kind !== 'split') return null
  for (const child of root.children) {
    if (child.id === id) return root
    const found = parentOf(child, id)
    if (found) return found
  }
  return null
}

// Whether a drop would change anything (so the UI can ignore it).
export function isNoOp(root: DockNode | null, panel: PanelId, target: DropTarget): boolean {
  const source = groupOf(root, panel)
  if (!source) return false
  if (target.kind === 'tab') {
    if (target.group !== source.id) return false
    return target.index === undefined || target.index === source.tabs.indexOf(panel) || target.index === source.tabs.indexOf(panel) + 1
  }
  if (target.kind === 'side') {
    return target.group === source.id && source.tabs.length === 1
  }
  return false
}

// Moves (or shows) a panel to a drop target. Returns the new tree; an
// impossible target (a group that no longer exists) leaves the tree as it is.
export function dropPanel(root: DockNode | null, panel: PanelId, target: DropTarget): DockNode | null {
  if (isNoOp(root, panel, target)) return clone(root)
  if (target.kind === 'tab') {
    const source = groupOf(root, panel)
    if (source && source.id === target.group && target.index !== undefined) {
      // Reordering within its own group.
      const next = clone(root)!
      const group = findGroup(next, target.group)!
      const from = group.tabs.indexOf(panel)
      group.tabs.splice(from, 1)
      group.tabs.splice(target.index > from ? target.index - 1 : target.index, 0, panel)
      group.active = panel
      return next
    }
  }
  let next = removePanel(root, panel)
  if (target.kind !== 'edge' && !findGroup(next, target.group)) return clone(root)

  if (target.kind === 'tab') {
    const group = findGroup(next, target.group)!
    const index = target.index === undefined ? group.tabs.length : Math.min(target.index, group.tabs.length)
    group.tabs.splice(index, 0, panel)
    group.active = panel
    return normalize(next)
  }

  const fresh: TabsNode = { kind: 'tabs', id: newId(next, 'g'), tabs: [panel], active: panel }
  if (target.kind === 'edge') {
    const dir = dirOf(target.side)
    if (!next) return { ...fresh, edge: target.side }
    const area = edgeArea(next, target.side)
    if (area) {
      // Next to what's already docked there, along the edge (left to right on the
      // top and bottom, top to bottom on the sides).
      const along: Direction = dir === 'row' ? 'column' : 'row'
      if (area.kind === 'split' && area.dir === along) {
        const n = area.children.length
        area.sizes = area.sizes.map((s) => (s * n) / (n + 1))
        area.children.push(fresh)
        area.sizes.push(1 / (n + 1))
        return normalize(next)
      }
      const wrapperId = newId(next, 's')
      next = replace(next, area.id, (node) => {
        const inner = { ...node }
        delete inner.edge
        return { kind: 'split', id: wrapperId, dir: along, children: [inner, fresh], sizes: [0.5, 0.5], edge: target.side }
      })
      return normalize(next)
    }
    fresh.edge = target.side
    if (next.kind === 'split' && next.dir === dir) {
      next.sizes = next.sizes.map((s) => s * (1 - EDGE_SHARE))
      if (before(target.side)) {
        next.children.unshift(fresh)
        next.sizes.unshift(EDGE_SHARE)
      } else {
        next.children.push(fresh)
        next.sizes.push(EDGE_SHARE)
      }
      return normalize(next)
    }
    const split: SplitNode = {
      kind: 'split',
      id: newId(next, 's'),
      dir,
      children: before(target.side) ? [fresh, next] : [next, fresh],
      sizes: before(target.side) ? [EDGE_SHARE, 1 - EDGE_SHARE] : [1 - EDGE_SHARE, EDGE_SHARE],
    }
    return normalize(split)
  }

  // Beside a group: join its parent split if that runs the same way, else wrap it.
  const dir = dirOf(target.side)
  const parent = parentOf(next!, target.group)
  if (parent && parent.dir === dir) {
    const at = parent.children.findIndex((c) => c.id === target.group)
    const share = parent.sizes[at]!
    parent.sizes[at] = share * (1 - SIDE_SHARE)
    const insertAt = before(target.side) ? at : at + 1
    parent.children.splice(insertAt, 0, fresh)
    parent.sizes.splice(insertAt, 0, share * SIDE_SHARE)
    return normalize(next)
  }
  const splitId = newId(next, 's')
  next = replace(next!, target.group, (node) => {
    const inner = { ...node }
    const edge = inner.edge
    delete inner.edge
    const split: SplitNode = {
      kind: 'split',
      id: splitId,
      dir,
      children: before(target.side) ? [fresh, inner] : [inner, fresh],
      sizes: before(target.side) ? [SIDE_SHARE, 1 - SIDE_SHARE] : [1 - SIDE_SHARE, SIDE_SHARE],
    }
    if (edge) split.edge = edge // the edge area grows to hold both
    return split
  })
  return normalize(next)
}

export function activate(root: DockNode | null, panel: PanelId): DockNode | null {
  const next = clone(root)
  const group = groupOf(next, panel)
  if (group) group.active = panel
  return next
}

// New shares for a split's two neighbouring children after a splitter drag:
// `first` is the left/top child's new share of the pair (0-1).
export function resizePair(root: DockNode | null, splitId: string, index: number, first: number): DockNode | null {
  const next = clone(root)
  const visit = (node: DockNode | null): void => {
    if (!node || node.kind !== 'split') return
    if (node.id === splitId && index >= 0 && index + 1 < node.sizes.length) {
      const pair = node.sizes[index]! + node.sizes[index + 1]!
      const a = Math.min(Math.max(first, 0.05), 0.95) * pair
      node.sizes[index] = a
      node.sizes[index + 1] = pair - a
      return
    }
    node.children.forEach(visit)
  }
  visit(next)
  return next
}

// --- defaults, saving, loading -------------------------------------------------

// The editor's first layout: Scene on the left, the viewport with Assets under
// it in the middle, AI on the right.
// The scene tree and the viewport side by side, the bottom panels under both of them (the left panel's foot
// included), the AI panel the whole height on the right.
export function defaultTree(): DockNode {
  return {
    kind: 'split',
    id: 's1',
    dir: 'row',
    sizes: [0.78, 0.22],
    children: [
      {
        kind: 'split',
        id: 's2',
        dir: 'column',
        sizes: [0.72, 0.28],
        children: [
          {
            kind: 'split',
            id: 's3',
            dir: 'row',
            sizes: [0.26, 0.74],
            children: [
              { kind: 'tabs', id: 'g1', tabs: ['scene'], active: 'scene', edge: 'left' },
              { kind: 'tabs', id: 'g2', tabs: ['viewport'], active: 'viewport' },
            ],
          },
          { kind: 'tabs', id: 'g3', tabs: ['assets', 'project', 'generate'], active: 'assets', edge: 'bottom' },
        ],
      },
      { kind: 'tabs', id: 'g4', tabs: ['ai'], active: 'ai', edge: 'right' },
    ],
  }
}

// Where a hidden panel goes back when shown and its old group is gone.
export const homeSide: Record<PanelId, Side> = {
  viewport: 'top',
  scene: 'left',
  assets: 'bottom',
  project: 'bottom',
  generate: 'bottom',
  ai: 'right',
  build: 'bottom',
  planner: 'bottom',
}

// A saved tree, checked: unknown panels are dropped, duplicates removed, and
// anything malformed gives null (the caller falls back to the default).
export function parseTree(value: unknown): DockNode | null {
  const seen = new Set<PanelId>()
  const ids = new Set<string>()
  const edges = new Set<Side>()
  const edgeOf = (v: unknown): Side | undefined => {
    if ((v === 'left' || v === 'right' || v === 'top' || v === 'bottom') && !edges.has(v)) {
      edges.add(v)
      return v
    }
    return undefined
  }
  const walk = (v: unknown): DockNode | null => {
    if (!v || typeof v !== 'object') return null
    const node = v as Record<string, unknown>
    const id = typeof node.id === 'string' && !ids.has(node.id) ? node.id : null
    if (!id) return null
    ids.add(id)
    if (node.kind === 'tabs' && Array.isArray(node.tabs)) {
      const tabs: PanelId[] = []
      for (const t of node.tabs as unknown[]) {
        if (typeof t === 'string' && (allPanels as string[]).includes(t) && !seen.has(t as PanelId)) {
          seen.add(t as PanelId)
          tabs.push(t as PanelId)
        }
      }
      const active = tabs.includes(node.active as PanelId) ? (node.active as PanelId) : tabs[0]
      if (!tabs.length) return null
      const group: TabsNode = { kind: 'tabs', id, tabs, active: active! }
      const edge = edgeOf(node.edge)
      if (edge) group.edge = edge
      return group
    }
    if (node.kind === 'split' && (node.dir === 'row' || node.dir === 'column') && Array.isArray(node.children)) {
      const sizes = Array.isArray(node.sizes) ? (node.sizes as unknown[]).map((s) => (typeof s === 'number' ? s : 0)) : []
      const children: DockNode[] = []
      const kept: number[] = []
      ;(node.children as unknown[]).forEach((c, i) => {
        const child = walk(c)
        if (child) {
          children.push(child)
          kept.push(sizes[i] ?? 0)
        }
      })
      const split: SplitNode = { kind: 'split', id, dir: node.dir, children, sizes: kept }
      const edge = edgeOf(node.edge)
      if (edge) split.edge = edge
      return split
    }
    return null
  }
  return normalize(walk(value))
}
