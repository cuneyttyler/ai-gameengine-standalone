<script setup lang="ts">
import { nextTick, ref, watch } from 'vue'
import { closeContextMenu, menuState, runMenuItem, type MenuItem } from '../contextMenu'
import { trackOverlay, untrackOverlay } from '../overlay'
import { onHostMessage } from '../ipc'

const menu = ref<HTMLElement | null>(null)
const position = ref({ x: 0, y: 0 })
// The open submenu (5.5): its items and where it sits, beside its item.
const sub = ref<HTMLElement | null>(null)
const subItems = ref<MenuItem[] | null>(null)
const subPosition = ref({ x: 0, y: 0 })
const subOwner = ref(-1)

async function hover(item: MenuItem, index: number, event: MouseEvent): Promise<void> {
  if (!item.submenu || item.disabled) {
    closeSub()
    return
  }
  if (subOwner.value === index) return
  closeSub()
  const r = (event.currentTarget as HTMLElement).getBoundingClientRect()
  subItems.value = item.submenu
  subOwner.value = index
  subPosition.value = { x: r.right, y: r.top - 4 }
  await nextTick()
  if (!sub.value) return
  const s = sub.value.getBoundingClientRect()
  subPosition.value = {
    // Flip to the left when there's no room on the right.
    x: r.right + s.width > window.innerWidth - 4 ? Math.max(4, r.left - s.width) : r.right,
    y: Math.min(r.top - 4, window.innerHeight - s.height - 4),
  }
  await nextTick()
  trackOverlay(sub.value)
}

function closeSub(): void {
  if (sub.value) untrackOverlay(sub.value)
  subItems.value = null
  subOwner.value = -1
}

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
    } else {
      closeSub()
      if (menu.value) untrackOverlay(menu.value)
    }
  },
)

// Moving across the menu bar swaps the items while open.
watch(
  () => menuState.items,
  () => closeSub(),
)

window.addEventListener('mousedown', (event) => {
  const target = event.target as Node
  if (menuState.open && menu.value && !menu.value.contains(target) && !(sub.value && sub.value.contains(target))) {
    closeContextMenu()
  }
})
window.addEventListener('blur', () => closeContextMenu())
// A click in the viewport (a native window over the page: the page never sees it) closes it too.
onHostMessage((message) => {
  if (message.type === 'panel:focused' && message.id === 'viewport') closeContextMenu()
})

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
      <button
        v-else
        :disabled="item.disabled"
        :class="{ danger: item.danger, open: subOwner === i }"
        @click="runMenuItem(item)"
        @mouseenter="hover(item, i, $event)"
      >
        <span>{{ item.label }}</span>
        <span v-if="item.submenu" class="shortcut">▸</span>
        <span v-else-if="item.shortcut" class="shortcut">{{ item.shortcut }}</span>
      </button>
    </template>
  </div>
  <div
    v-if="menuState.open && subItems"
    ref="sub"
    class="menu"
    :style="{ left: subPosition.x + 'px', top: subPosition.y + 'px' }"
    @contextmenu.prevent
  >
    <template v-for="(item, i) in subItems" :key="i">
      <div v-if="item.separator" class="separator"></div>
      <button v-else :disabled="item.disabled" :class="{ danger: item.danger }" :title="item.shortcut" @click="runMenuItem(item)">
        <span>{{ item.label }}</span>
      </button>
    </template>
    <div v-if="subItems.length === 0" class="empty">Nothing yet</div>
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
button.open {
  background: #313148;
}
.empty {
  padding: 5px 12px;
  opacity: 0.5;
}
.separator {
  height: 1px;
  margin: 4px 0;
  background: #2a2a3a;
}
</style>
