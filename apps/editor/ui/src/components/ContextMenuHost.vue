<script setup lang="ts">
import { nextTick, ref, watch } from 'vue'
import { closeContextMenu, menuState, runMenuItem } from '../contextMenu'
import { trackOverlay, untrackOverlay } from '../overlay'

const menu = ref<HTMLElement | null>(null)
const position = ref({ x: 0, y: 0 })

watch(
  () => menuState.open,
  async (open) => {
    if (open) {
      position.value = { x: menuState.x, y: menuState.y }
      await nextTick()
      if (!menu.value) return
      // Keep it on screen.
      const r = menu.value.getBoundingClientRect()
      position.value = {
        x: Math.min(menuState.x, window.innerWidth - r.width - 4),
        y: Math.min(menuState.y, window.innerHeight - r.height - 4),
      }
      await nextTick()
      trackOverlay(menu.value)
      menu.value.focus()
    } else if (menu.value) {
      untrackOverlay(menu.value)
    }
  },
)

window.addEventListener('mousedown', (event) => {
  if (menuState.open && menu.value && !menu.value.contains(event.target as Node)) closeContextMenu()
})
window.addEventListener('blur', () => closeContextMenu())

function onKeydown(event: KeyboardEvent): void {
  event.stopPropagation()
  if (event.key === 'Escape') closeContextMenu()
}
</script>

<template>
  <div
    v-if="menuState.open"
    ref="menu"
    class="menu"
    tabindex="-1"
    :style="{ left: position.x + 'px', top: position.y + 'px' }"
    @keydown="onKeydown"
    @contextmenu.prevent
  >
    <template v-for="(item, i) in menuState.items" :key="i">
      <div v-if="item.separator" class="separator"></div>
      <button v-else :disabled="item.disabled" :class="{ danger: item.danger }" @click="runMenuItem(item)">
        <span>{{ item.label }}</span>
        <span v-if="item.shortcut" class="shortcut">{{ item.shortcut }}</span>
      </button>
    </template>
  </div>
</template>

<style scoped>
.menu {
  position: fixed;
  z-index: 900;
  min-width: 200px;
  padding: 4px 0;
  background: #181825;
  border: 1px solid #3a3a4a;
  box-shadow: 0 8px 30px rgba(0, 0, 0, 0.45);
  outline: none;
  font-size: 0.85em;
}
button {
  display: flex;
  justify-content: space-between;
  gap: 24px;
  width: 100%;
  padding: 5px 12px;
  border: none;
  background: none;
  color: #eee;
  font: inherit;
  text-align: left;
  cursor: pointer;
}
button:hover:not(:disabled) {
  background: #313148;
}
button:disabled {
  opacity: 0.4;
  cursor: default;
}
button.danger {
  color: #fca5a5;
}
.shortcut {
  opacity: 0.5;
}
.separator {
  height: 1px;
  margin: 4px 0;
  background: #2a2a3a;
}
</style>
