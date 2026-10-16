// node --test (Node 22.18+ runs TypeScript directly): `npm test` in apps/editor/ui.
import assert from 'node:assert/strict'
import { test } from 'node:test'

import { clickItem, emptySelection, keepShown, renamed, selectAll } from '../src/selection.ts'

const shown = ['a', 'b', 'c', 'd', 'e']

test('a plain click picks one and sets the anchor', () => {
  const s = clickItem({ picked: ['a', 'b'], anchor: 'a' }, shown, 'c', {})
  assert.deepEqual(s, { picked: ['c'], anchor: 'c' })
})

test('ctrl+click adds and removes, in list order', () => {
  let s = clickItem(emptySelection, shown, 'd', {})
  s = clickItem(s, shown, 'b', { ctrl: true })
  assert.deepEqual(s.picked, ['b', 'd'])
  s = clickItem(s, shown, 'd', { ctrl: true })
  assert.deepEqual(s.picked, ['b'])
})

test('shift+click picks the range from the anchor, either way', () => {
  let s = clickItem(emptySelection, shown, 'b', {})
  s = clickItem(s, shown, 'd', { shift: true })
  assert.deepEqual(s.picked, ['b', 'c', 'd'])
  s = clickItem(s, shown, 'a', { shift: true }) // same anchor, other direction
  assert.deepEqual(s.picked, ['a', 'b'])
  assert.equal(s.anchor, 'b')
})

test('ctrl+shift adds a range to what is picked', () => {
  let s = clickItem(emptySelection, shown, 'a', {})
  s = clickItem(s, shown, 'd', { ctrl: true })
  s = clickItem(s, shown, 'e', { ctrl: true, shift: true })
  assert.deepEqual(s.picked, ['a', 'd', 'e'])
})

test('shift without an anchor is a plain click', () => {
  assert.deepEqual(clickItem(emptySelection, shown, 'c', { shift: true }).picked, ['c'])
})

test('ctrl+a picks what is shown, and a filter keeps only what is still shown', () => {
  const filtered = ['b', 'd']
  let s = selectAll(filtered)
  assert.deepEqual(s.picked, ['b', 'd'])
  s = keepShown(selectAll(shown), filtered)
  assert.deepEqual(s.picked, ['b', 'd'])
  assert.equal(s.anchor, 'b')
})

test('a moved item stays picked under its new path', () => {
  const s = renamed({ picked: ['a', 'b'], anchor: 'b' }, 'b', 'x/b')
  assert.deepEqual(s, { picked: ['a', 'x/b'], anchor: 'x/b' })
})
