// node --test: `npm test` in apps/editor/ui.
import assert from 'node:assert/strict'
import { test } from 'node:test'

import { actionFor, bind, comboOf, effectiveKeymap, normalizeCombo, reset, unbind } from '../src/keymap.ts'

test('combos name letters, modifiers and keys one way', () => {
  assert.equal(comboOf({ key: 's', ctrlKey: true }), 'Ctrl+S')
  assert.equal(comboOf({ key: 'S', ctrlKey: true, shiftKey: true }), 'Ctrl+Shift+S')
  assert.equal(comboOf({ key: 'F5' }), 'F5')
  assert.equal(comboOf({ key: ' ' }), 'Space')
  assert.equal(comboOf({ key: 'Shift', shiftKey: true }), '') // a modifier alone
})

test('symbols are the character typed, whatever the layout needed', () => {
  assert.equal(comboOf({ key: '{', shiftKey: true }), '{') // Shift+[ on a US layout
  assert.equal(comboOf({ key: '[', ctrlKey: true, altKey: true }), '[') // AltGr+8 on a Turkish one
  assert.equal(comboOf({ key: '+', shiftKey: true }), '+')
})

test('loose spellings normalize', () => {
  assert.equal(normalizeCombo('shift+ctrl+z'), 'Ctrl+Shift+Z')
  assert.equal(normalizeCombo('esc'), 'Escape')
  assert.equal(normalizeCombo('Ctrl++'), 'Ctrl++')
})

test('defaults resolve, and panel toggles need no Ctrl', () => {
  const map = effectiveKeymap({})
  assert.equal(actionFor(map, 'Ctrl+S'), 'file.save')
  assert.equal(actionFor(map, 'Ctrl+Shift+Z'), 'edit.redo')
  assert.equal(actionFor(map, 'B'), 'panel.scene')
  assert.equal(actionFor(map, 'M'), 'panel.viewport')
  assert.equal(actionFor(map, 'Ctrl+B'), null)
  assert.equal(actionFor(map, '['), 'edit.smaller')
})

test('rebinding takes the keys from whatever had them, and only differences are kept', () => {
  let { overrides, takenFrom } = bind({}, 'view.grid', 'B', 'G')
  assert.equal(takenFrom, 'panel.scene')
  let map = effectiveKeymap(overrides)
  assert.equal(actionFor(map, 'B'), 'view.grid')
  assert.deepEqual(map['view.grid'], ['B']) // replaced G
  assert.deepEqual(map['panel.scene'], [])
  // Adding a second key keeps the first.
  ;({ overrides } = bind(overrides, 'panel.scene', 'Shift+B', null))
  map = effectiveKeymap(overrides)
  assert.deepEqual(map['panel.scene'], ['Shift+B'])
  // Back to the defaults: nothing stored.
  overrides = reset(reset(overrides, 'view.grid'), 'panel.scene')
  assert.deepEqual(overrides, {})
  assert.equal(actionFor(effectiveKeymap(overrides), 'G'), 'view.grid')
})

test('unbinding one of several keys', () => {
  const overrides = unbind({}, 'edit.redo', 'Ctrl+Shift+Z')
  const map = effectiveKeymap(overrides)
  assert.deepEqual(map['edit.redo'], ['Ctrl+Y'])
  assert.equal(actionFor(map, 'Ctrl+Shift+Z'), null)
})
