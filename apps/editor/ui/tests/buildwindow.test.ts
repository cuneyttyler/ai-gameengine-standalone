// node --test (Node 22.18+ runs TypeScript directly): `npm test` in apps/editor/ui.
import assert from 'node:assert/strict'
import { test } from 'node:test'

import { amountText, countRange, layoutSketch, parseRange } from '../src/buildwindow.ts'

test('counts read in every form the Brief writes', () => {
  assert.deepEqual(countRange({ count: [20, 30] }), [20, 30])
  assert.deepEqual(countRange([5, 8]), [5, 8])
  assert.deepEqual(countRange(12), [12, 12])
  assert.deepEqual(countRange(null), [0, 0])
})

test('ranges typed in the window', () => {
  assert.deepEqual(parseRange('3'), [3, 3])
  assert.deepEqual(parseRange('3-5'), [3, 5])
  assert.deepEqual(parseRange(' 3 – 5 '), [3, 5])
  assert.deepEqual(parseRange('5 to 3'), [3, 5])
  assert.equal(parseRange('lots'), null)
  assert.equal(parseRange(''), null)
})

test('areas and sketches', () => {
  assert.equal(amountText({ area: [3600, 10000] }), '3600–10000')
  assert.equal(amountText({ area: [400, 400] }), '400')
  assert.equal(amountText(undefined), '—')
  for (const layout of ['organic', 'grid', 'radial', 'linear-along-road', 'linear-along-river', 'growth']) {
    assert.match(layoutSketch(layout), /^<(path|circle|rect)/)
  }
})
