<script setup lang="ts">
// Help ▸ Keyboard Shortcuts (F1), built from shortcuts.ts.
import { nextTick, ref, watch } from 'vue'
import { editorState } from '../editorState'
import { trackOverlay, untrackOverlay } from '../overlay'
import { shortcutGroups } from '../shortcuts'

const backdrop = ref<HTMLElement | null>(null)

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
  if (backdrop.value) untrackOverlay(backdrop.value)
  editorState.showShortcuts = false
}
</script>

<template>
  <div
    v-if="editorState.showShortcuts"
    ref="backdrop"
    class="backdrop"
    tabindex="-1"
    @mousedown.self="close"
    @keydown.esc.stop="close"
  >
    <div class="sheet">
      <header>
        <h3>Keyboard shortcuts</h3>
        <button title="Close (Esc)" @click="close">×</button>
      </header>
      <div class="groups">
        <section v-for="group in shortcutGroups" :key="group.name">
          <h4>{{ group.name }}</h4>
          <div v-for="item in group.items" :key="item.keys" class="row">
            <kbd>{{ item.keys }}</kbd>
            <span>{{ item.action }}</span>
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
  width: min(860px, 92vw);
  max-height: 86vh;
  overflow-y: auto;
  background: #181825;
  border: 1px solid #333;
  border-top: 2px solid #3b82f6;
  padding: 14px 18px;
}
header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}
h3 {
  margin: 0;
}
header button {
  border: 1px solid #333;
  border-radius: 0;
  background: #11111b;
  color: #eee;
  width: 26px;
  height: 26px;
  cursor: pointer;
}
.groups {
  columns: 2;
  column-gap: 28px;
  margin-top: 10px;
}
section {
  break-inside: avoid;
  margin-bottom: 12px;
}
h4 {
  margin: 0 0 6px;
  font-size: 0.8em;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  opacity: 0.6;
}
.row {
  display: flex;
  gap: 10px;
  padding: 2px 0;
  font-size: 0.9em;
}
kbd {
  min-width: 128px;
  font-family: inherit;
  color: #fbbf24;
}
</style>
