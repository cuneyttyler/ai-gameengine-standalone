<script setup lang="ts">
// Help ▸ Keyboard Shortcuts (F1), also Preferences ▸ Keyboard shortcuts:
// every shortcut, editable (development_plan.md 5.6). Click a key to record a
// replacement, + to add one, × to remove it; a key another action had moves
// over (and it says so). Saved per user by the host.
import { computed, nextTick, ref, watch } from 'vue'
import { editorState } from '../editorState'
import { capture, keyState, setOverrides } from '../keys'
import { actions, bind, comboOf, fixedShortcuts, reset, unbind } from '../keymap'
import { trackOverlay, untrackOverlay } from '../overlay'

const backdrop = ref<HTMLElement | null>(null)
const recording = ref<{ id: string; replacing: string | null } | null>(null)
const notice = ref('')

const groups = computed(() => {
  const names = [...new Set([...actions.map((a) => a.group), ...fixedShortcuts.map((f) => f.group)])]
  return names.map((name) => ({
    name,
    actions: actions.filter((a) => a.group === name),
    fixed: fixedShortcuts.filter((f) => f.group === name),
  }))
})

watch(
  () => editorState.showShortcuts,
  async (open) => {
    await nextTick()
    if (open && backdrop.value) {
      trackOverlay(backdrop.value)
      backdrop.value.focus()
    } else if (!open && backdrop.value) {
      untrackOverlay(backdrop.value)
    }
  },
)

function close(): void {
  stopRecording()
  if (backdrop.value) untrackOverlay(backdrop.value)
  editorState.showShortcuts = false
}

function record(id: string, replacing: string | null): void {
  notice.value = ''
  recording.value = { id, replacing }
  capture.active = true // keys.ts stays out of the way
}

function stopRecording(): void {
  recording.value = null
  capture.active = false
}

function onKeydown(event: KeyboardEvent): void {
  if (!recording.value) {
    if (event.key === 'Escape') {
      event.stopPropagation()
      close()
    }
    return
  }
  event.preventDefault()
  event.stopPropagation()
  if (event.key === 'Escape' && !event.ctrlKey && !event.shiftKey && !event.altKey) {
    stopRecording() // Esc cancels; bind Escape itself from the list's defaults (Reset)
    return
  }
  const combo = comboOf(event)
  if (!combo) return // a modifier on its own: wait for the key
  const { id, replacing } = recording.value
  const result = bind(keyState.overrides, id, combo, replacing)
  setOverrides(result.overrides)
  if (result.takenFrom) {
    const from = actions.find((a) => a.id === result.takenFrom)
    notice.value = `${combo} was "${from?.label}" — that action has lost it.`
  }
  stopRecording()
}

function remove(id: string, combo: string): void {
  setOverrides(unbind(keyState.overrides, id, combo))
}

function resetOne(id: string): void {
  setOverrides(reset(keyState.overrides, id))
}

function resetAll(): void {
  setOverrides({})
  notice.value = 'Every shortcut is back to its default.'
}

function changed(id: string): boolean {
  return id in keyState.overrides
}
</script>

<template>
  <div
    v-if="editorState.showShortcuts"
    ref="backdrop"
    class="backdrop"
    tabindex="-1"
    @mousedown.self="close"
    @keydown="onKeydown"
  >
    <div class="sheet">
      <header>
        <h3>Keyboard shortcuts</h3>
        <span class="spacer"></span>
        <button :disabled="Object.keys(keyState.overrides).length === 0" @click="resetAll">Reset all</button>
        <button title="Close (Esc)" @click="close">×</button>
      </header>
      <p class="muted">
        Click a key to change it, + to add another, × to remove one. Yours, on this computer; they work in the viewport
        and the panels alike.
      </p>
      <p v-if="recording" class="recording">Press the new keys… (Esc cancels)</p>
      <p v-else-if="notice" class="notice">{{ notice }}</p>
      <div class="groups">
        <section v-for="group in groups" :key="group.name">
          <h4>{{ group.name }}</h4>
          <div v-for="action in group.actions" :key="action.id" class="row">
            <span class="keys">
              <span v-for="combo in keyState.map[action.id]" :key="combo" class="key">
                <button
                  class="kbd"
                  :class="{ active: recording?.id === action.id && recording?.replacing === combo }"
                  title="Click, then press the new keys"
                  @click="record(action.id, combo)"
                >
                  {{ combo }}
                </button>
                <button class="x" title="Remove this key" @click="remove(action.id, combo)">×</button>
              </span>
              <button
                class="add"
                :class="{ active: recording?.id === action.id && recording?.replacing === null }"
                title="Add a key"
                @click="record(action.id, null)"
              >
                +
              </button>
            </span>
            <span class="label">{{ action.label }}</span>
            <button v-if="changed(action.id)" class="reset" title="Back to the default" @click="resetOne(action.id)">
              Reset
            </button>
          </div>
          <div v-for="item in group.fixed" :key="item.keys + item.label" class="row fixed">
            <span class="keys"><kbd>{{ item.keys }}</kbd></span>
            <span class="label">{{ item.label }}</span>
          </div>
        </section>
      </div>
    </div>
  </div>
</template>

<style scoped>
.backdrop {
  position: fixed;
  inset: 0;
  z-index: 1000;
  background: rgba(5, 5, 10, 0.55);
  display: flex;
  align-items: center;
  justify-content: center;
  outline: none;
}
.sheet {
  width: min(900px, 94vw);
  max-height: 88vh;
  overflow-y: auto;
  background: #181825;
  border: 1px solid #333;
  border-top: 2px solid #3b82f6;
  padding: 14px 18px;
}
header {
  display: flex;
  align-items: center;
  gap: 6px;
}
h3 {
  margin: 0;
}
.spacer {
  flex: 1;
}
h4 {
  margin: 14px 0 6px;
  font-size: 0.8em;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  opacity: 0.6;
}
.groups {
  columns: 2;
  column-gap: 28px;
}
section {
  break-inside: avoid;
}
.row {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 2px 0;
  font-size: 0.9em;
}
.keys {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
  min-width: 150px;
}
.key {
  display: inline-flex;
}
button {
  height: 22px;
  padding: 0 8px;
  border: 1px solid #333;
  border-radius: 0;
  background: #11111b;
  color: #eee;
  font: inherit;
  cursor: pointer;
}
button:hover:not(:disabled) {
  background: #262637;
}
button:disabled {
  opacity: 0.4;
  cursor: default;
}
.kbd,
kbd {
  font-family: ui-monospace, Consolas, monospace;
  font-size: 0.9em;
}
kbd {
  padding: 1px 6px;
  border: 1px solid #333;
  background: #11111b;
  white-space: nowrap;
}
.x {
  padding: 0 5px;
  border-left: none;
  opacity: 0.6;
}
.add {
  padding: 0 7px;
  opacity: 0.6;
}
.active {
  border-color: #3b82f6;
  background: #313148;
  opacity: 1;
}
.label {
  flex: 1;
}
.reset {
  font-size: 0.8em;
  opacity: 0.8;
}
.fixed {
  opacity: 0.75;
}
.muted {
  opacity: 0.6;
  font-size: 0.9em;
  margin: 6px 0;
}
.recording {
  color: #93c5fd;
  margin: 6px 0;
}
.notice {
  color: #f59e0b;
  margin: 6px 0;
}
</style>
