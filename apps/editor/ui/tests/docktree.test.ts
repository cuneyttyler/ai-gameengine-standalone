// node --test (Node 22.18+ runs TypeScript directly): `npm test` in apps/editor/ui.
import assert from 'node:assert/strict'
import { test } from 'node:test'

import {
  defaultTree,
  dropPanel,
  edgeArea,
  groupOf,
  groups,
  isShown,
  normalize,
  panelsIn,
  parseTree,
  removePanel,
  resizePair,
  type DockNode,
  type SplitNode,
} from '../src/docktree.ts'

// The default layout with only its four original panels (Project and Generate,
// tabs beside Assets since 6.8, left out), so these tests read as before.
function baseTree(): DockNode {
  return removePanel(removePanel(defaultTree(), 'project'), 'generate')!
}

function sizesValid(node: DockNode | null): boolean {
  if (!node || node.kind === 'tabs') return true
  const sum = node.sizes.reduce((a, b) => a + b, 0)
  return Math.abs(sum - 1) < 1e-9 && node.sizes.length === node.children.length && node.children.every(sizesValid)
}

test('the default layout has every panel once', () => {
  const tree = defaultTree()
  assert.deepEqual(panelsIn(tree).sort(), ['ai', 'assets', 'generate', 'project', 'scene', 'viewport'])
  assert.deepEqual(groupOf(tree, 'assets')!.tabs, ['assets', 'project', 'generate']) // tabs at the bottom
  assert.ok(sizesValid(tree))
})

test('dropping AI onto the viewport makes them tabs, AI in front', () => {
  const tree = dropPanel(baseTree(), 'ai', { kind: 'tab', group: 'g2' })
  const group = groupOf(tree, 'viewport')!
  assert.deepEqual(group.tabs, ['viewport', 'ai'])
  assert.equal(group.active, 'ai')
  assert.equal(isShown(tree, 'viewport'), false) // behind the AI tab
  // The right column it left is gone; the top split collapses to two children.
  assert.equal((tree as SplitNode).children.length, 2)
  assert.ok(sizesValid(tree))
})

test('dropping beside a group splits it the right way', () => {
  const tree = dropPanel(baseTree(), 'scene', { kind: 'side', group: 'g3', side: 'right' }) // beside Assets
  const assets = groupOf(tree, 'assets')!
  const scene = groupOf(tree, 'scene')!
  assert.notEqual(assets.id, scene.id)
  // Assets and Scene now share a row under the viewport.
  const findParent = (node: DockNode, id: string): SplitNode | null => {
    if (node.kind !== 'split') return null
    if (node.children.some((c) => c.id === id)) return node
    for (const c of node.children) {
      const found = findParent(c, id)
      if (found) return found
    }
    return null
  }
  const parent = findParent(tree!, scene.id)!
  assert.equal(parent.dir, 'row')
  assert.equal(parent.children.findIndex((c) => c.id === scene.id), parent.children.findIndex((c) => c.id === assets.id) + 1)
  assert.ok(sizesValid(tree))
})

test('a side drop into a split running the same way joins it instead of nesting', () => {
  // The viewport column runs top to bottom; dropping AI below Assets adds a third row to it.
  const tree = dropPanel(baseTree(), 'ai', { kind: 'side', group: 'g3', side: 'bottom' })
  const column = groups(tree).map((g) => g.tabs[0])
  assert.deepEqual(column, ['scene', 'viewport', 'assets', 'ai'])
  const root = tree as SplitNode
  const center = root.children[1] as SplitNode
  assert.equal(center.dir, 'column')
  assert.equal(center.children.length, 3)
  assert.ok(sizesValid(tree))
})

test('docking on a window edge spans the whole edge', () => {
  const tree = dropPanel(baseTree(), 'assets', { kind: 'edge', side: 'top' }) as SplitNode
  assert.equal(tree.dir, 'column')
  assert.deepEqual((tree.children[0] as { tabs: string[] }).tabs, ['assets'])
  assert.ok(tree.sizes[0]! < 0.5)
  assert.ok(sizesValid(tree))

  assert.equal(edgeArea(tree, 'top')!.id, groupOf(tree, 'assets')!.id)
})

test('docking on an edge that has an area puts the panel next to what is there', () => {
  // The default layout has Scene docked left: AI joins it, below it, in one left area.
  const tree = dropPanel(baseTree(), 'ai', { kind: 'edge', side: 'left' }) as SplitNode
  const area = edgeArea(tree, 'left') as SplitNode
  assert.equal(area.kind, 'split')
  assert.equal(area.dir, 'column')
  assert.deepEqual(area.children.map((c) => (c as { tabs: string[] }).tabs[0]), ['scene', 'ai'])
  assert.equal(tree.children[0]!.id, area.id) // still the left column of the window
  assert.ok(sizesValid(tree))
})

test('every panel on the top edge makes one top row, not stacked strips', () => {
  let tree: DockNode | null = baseTree()
  for (const id of ['scene', 'assets', 'ai'] as const) tree = dropPanel(tree, id, { kind: 'edge', side: 'top' })
  const area = edgeArea(tree, 'top') as SplitNode
  assert.equal(area.dir, 'row')
  assert.deepEqual(area.children.map((c) => (c as { tabs: string[] }).tabs[0]), ['scene', 'assets', 'ai'])
  // The viewport, the only panel left elsewhere, sits under the top row.
  assert.equal((tree as SplitNode).dir, 'column')
  assert.equal((tree as SplitNode).children.length, 2)
  assert.ok(sizesValid(tree))
  // Moving the viewport up too: everything is in the top row; it fills the window.
  tree = dropPanel(tree, 'viewport', { kind: 'edge', side: 'top' })
  assert.equal(tree!.kind, 'split')
  assert.equal((tree as SplitNode).dir, 'row')
  assert.deepEqual(panelsIn(tree), ['scene', 'assets', 'ai', 'viewport'])
})

test('the edge tag follows the area when panels leave it or it is split', () => {
  let tree = dropPanel(baseTree(), 'ai', { kind: 'edge', side: 'left' }) // left area: scene + ai
  tree = removePanel(tree, 'scene') // the area is just AI now
  assert.deepEqual((edgeArea(tree, 'left') as { tabs: string[] }).tabs, ['ai'])
  // Splitting the bottom area's group from its side: the area grows to hold both.
  tree = dropPanel(tree, 'scene', { kind: 'side', group: groupOf(tree, 'assets')!.id, side: 'right' })
  const bottom = edgeArea(tree, 'bottom')!
  assert.equal(bottom.kind, 'split')
  assert.deepEqual(panelsIn(bottom), ['assets', 'scene'])
  // Docking on the bottom edge again joins that area.
  tree = dropPanel(tree, 'ai', { kind: 'edge', side: 'bottom' })
  assert.deepEqual(panelsIn(edgeArea(tree, 'bottom')), ['assets', 'scene', 'ai'])
  assert.equal(edgeArea(tree, 'left'), null) // the left area left with AI
})

test('reordering tabs within a group', () => {
  let tree = dropPanel(baseTree(), 'ai', { kind: 'tab', group: 'g2' })
  tree = dropPanel(tree, 'scene', { kind: 'tab', group: 'g2' })
  assert.deepEqual(groupOf(tree, 'viewport')!.tabs, ['viewport', 'ai', 'scene'])
  tree = dropPanel(tree, 'scene', { kind: 'tab', group: 'g2', index: 0 })
  assert.deepEqual(groupOf(tree, 'viewport')!.tabs, ['scene', 'viewport', 'ai'])
  tree = dropPanel(tree, 'scene', { kind: 'tab', group: 'g2', index: 3 })
  assert.deepEqual(groupOf(tree, 'viewport')!.tabs, ['viewport', 'ai', 'scene'])
})

test('no-op drops change nothing', () => {
  const tree = baseTree()
  assert.deepEqual(dropPanel(tree, 'ai', { kind: 'tab', group: 'g4' }), tree)
  assert.deepEqual(dropPanel(tree, 'ai', { kind: 'side', group: 'g4', side: 'left' }), tree)
  assert.deepEqual(dropPanel(tree, 'ai', { kind: 'tab', group: 'nope' }), tree)
})

test('a panel can split its own group when it has company', () => {
  let tree = dropPanel(baseTree(), 'ai', { kind: 'tab', group: 'g2' })
  tree = dropPanel(tree, 'ai', { kind: 'side', group: 'g2', side: 'right' })
  assert.notEqual(groupOf(tree, 'ai')!.id, groupOf(tree, 'viewport')!.id)
  assert.equal(isShown(tree, 'viewport'), true)
  assert.ok(sizesValid(tree))
})

test('removing every panel empties the tree; showing one again works', () => {
  let tree: DockNode | null = baseTree()
  for (const id of ['scene', 'assets', 'ai', 'viewport'] as const) tree = removePanel(tree, id)
  assert.equal(tree, null)
  tree = dropPanel(tree, 'ai', { kind: 'edge', side: 'right' })
  assert.deepEqual(panelsIn(tree), ['ai'])
})

test('inputs are never changed', () => {
  const tree = baseTree()
  const copy = JSON.stringify(tree)
  dropPanel(tree, 'ai', { kind: 'tab', group: 'g2' })
  removePanel(tree, 'scene')
  resizePair(tree, 's1', 0, 0.5)
  assert.equal(JSON.stringify(tree), copy)
})

test('resizing a pair keeps the rest of the split', () => {
  const tree = resizePair(baseTree(), 's1', 0, 0.5) as SplitNode
  assert.ok(Math.abs(tree.sizes[0]! - tree.sizes[1]!) < 1e-9)
  assert.ok(Math.abs(tree.sizes[2]! - 0.22) < 1e-9)
})

test('saved trees are checked: duplicates, unknown panels and junk are dropped', () => {
  const saved = {
    kind: 'split',
    id: 's1',
    dir: 'row',
    sizes: [1, 1, 'x'],
    children: [
      { kind: 'tabs', id: 'g1', tabs: ['scene', 'hacker', 'scene'], active: 'nope' },
      { kind: 'tabs', id: 'g2', tabs: ['viewport'], active: 'viewport' },
      { kind: 'tabs', id: 'g3', tabs: [], active: 'ai' },
    ],
  }
  const tree = parseTree(saved) as SplitNode
  assert.deepEqual(panelsIn(tree), ['scene', 'viewport'])
  assert.equal(groupOf(tree, 'scene')!.active, 'scene')
  assert.ok(sizesValid(tree))
  assert.equal(parseTree('garbage'), null)
  assert.equal(parseTree({ kind: 'split', id: 's1', dir: 'diagonal', children: [] }), null)
})

test('normalize flattens one-child and same-direction splits', () => {
  const tree = normalize({
    kind: 'split',
    id: 's1',
    dir: 'row',
    sizes: [0.5, 0.5],
    children: [
      { kind: 'split', id: 's2', dir: 'row', sizes: [0.5, 0.5], children: [
        { kind: 'tabs', id: 'g1', tabs: ['scene'], active: 'scene' },
        { kind: 'tabs', id: 'g2', tabs: ['ai'], active: 'ai' },
      ] },
      { kind: 'split', id: 's3', dir: 'column', sizes: [1], children: [
        { kind: 'tabs', id: 'g3', tabs: ['viewport'], active: 'viewport' },
      ] },
    ],
  }) as SplitNode
  assert.equal(tree.children.length, 3)
  assert.deepEqual(tree.sizes.map((s) => Math.round(s * 100) / 100), [0.25, 0.25, 0.5])
  assert.equal(tree.children[2]!.kind, 'tabs')
})
