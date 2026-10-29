<script setup lang="ts">
// One node of the layout tree (development_plan.md 6.8): a split, drawn as a
// row or column of its children with a splitter between each pair, or a tab
// group, drawn as its tab strip over an empty body. Panels aren't drawn here:
// they live in one layer (App.vue) laid over their group's body, so moving a
// panel never re-mounts it.
import { onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { openContextMenu, separator, type MenuItem } from '../contextMenu'
import type { DockNode } from '../docktree'
import { pressTab, registerGroup, unregisterGroup } from '../dockdrag'
import { keyLabel } from '../keys'
import {
  dockView,
  movePanel,
  panelNames,
  resetLayout,
  resizeSplit,
  saveLayout,
  setPanelVisible,
  sideNames,
  toggleMaximize,
  type PanelId,
  type Side,
} from '../layout'
import Splitter from './Splitter.vue'

const props = defineProps<{ node: DockNode }>()

const MIN_PX = 90

// --- splits ---------------------------------------------------------------------

const childEls = ref<HTMLElement[]>([])

function onSplitDrag(index: number, position: number): void {
  if (props.node.kind !== 'split') return
  const a = childEls.value[index]?.getBoundingClientRect()
  const b = childEls.value[index + 1]?.getBoundingClientRect()
  if (!a || !b) return
  const row = props.node.dir === 'row'
  const start = row ? a.left : a.top
  const total = row ? b.right - a.left : b.bottom - a.top
  if (total <= 2 * MIN_PX) return
  const first = Math.min(Math.max(position - start, MIN_PX), total - MIN_PX) / total
  resizeSplit(props.node.id, index, first)
}

// --- tab groups -----------------------------------------------------------------

const groupEl = ref<HTMLElement | null>(null)
let registered: string | null = null

function register(): void {
  if (registered && groupEl.value) unregisterGroup(registered, groupEl.value)
  registered = null
  if (props.node.kind === 'tabs' && groupEl.value) {
    registerGroup(props.node.id, groupEl.value)
    registered = props.node.id
  }
}
onMounted(register)
watch(() => props.node.id, register, { flush: 'post' })
onBeforeUnmount(() => {
  if (registered && groupEl.value) unregisterGroup(registered, groupEl.value)
})

function tabMenu(panel: PanelId, event: MouseEvent): void {
  event.preventDefault()
  const items: MenuItem[] = [
    ...(['left', 'top', 'right', 'bottom'] as Side[]).map((side) => ({
      label: `Dock ${sideNames[side].toLowerCase()} edge`,
      action: () => movePanel(panel, side),
    })),
    separator,
    { label: 'Hide', shortcut: keyLabel(`panel.${panel}`), action: () => setPanelVisible(panel, false) },
    { label: 'Maximize (double-click the tab)', action: () => toggleMaximize(panel) },
    separator,
    { label: 'Reset layout', action: () => resetLayout() },
  ]
  openContextMenu(event.clientX, event.clientY, items)
}
</script>

<template>
  <div v-if="node.kind === 'split'" class="split" :class="node.dir">
    <template v-for="(child, i) in node.children" :key="child.id">
      <Splitter
        v-if="i > 0 && !(dockView.bottomHidden && child.edge === 'bottom')"
        :direction="node.dir === 'row' ? 'horizontal' : 'vertical'"
        @drag="(p: number) => onSplitDrag(i - 1, p)"
        @end="saveLayout"
      />
      <div
        :ref="(el) => { if (el) childEls[i] = el as HTMLElement }"
        class="cell"
        :style="dockView.bottomHidden && child.edge === 'bottom' ? { display: 'none' } : { flex: `${node.sizes[i]} 1 0` }"
      >
        <DockNode :node="child" />
      </div>
    </template>
  </div>

  <div v-else ref="groupEl" class="group" :class="{ target: dockView.dragging !== null }">
    <div class="tabs">
      <button
        v-for="panel in node.tabs"
        :key="panel"
        :data-tab="panel"
        class="tab"
        :class="{ active: panel === node.active, moving: dockView.dragging === panel }"
        :title="`${panelNames[panel]} — drag to move it: onto a panel to make tabs, to a side to split, to the window's edge to dock. Double-click to fill the window.`"
        @pointerdown="pressTab(panel, $event)"
        @dblclick="toggleMaximize(panel)"
        @contextmenu="tabMenu(panel, $event)"
      >
        {{ panelNames[panel] }}
      </button>
    </div>
    <div class="body"></div>
  </div>
</template>

<style scoped>
.split {
  display: flex;
  width: 100%;
  height: 100%;
  min-width: 0;
  min-height: 0;
}
.split.row {
  flex-direction: row;
}
.split.column {
  flex-direction: column;
}
.cell {
  display: flex;
  min-width: 0;
  min-height: 0;
  overflow: hidden;
}
.group {
  display: flex;
  flex-direction: column;
  width: 100%;
  height: 100%;
  min-width: 0;
  min-height: 0;
  background: #181825;
}
.tabs {
  height: 26px;
  flex-shrink: 0;
  display: flex;
  align-items: stretch;
  overflow: hidden;
  background: #14141f;
  border-bottom: 1px solid #2a2a3a;
}
.tab {
  flex-shrink: 0;
  padding: 0 12px;
  border: none;
  border-right: 1px solid #2a2a3a;
  border-radius: 0;
  background: transparent;
  color: #9a9ab0;
  font: inherit;
  font-size: 0.8em;
  cursor: grab;
  user-select: none;
  touch-action: none;
}
.tab:hover {
  background: #1e1e2e;
  color: #ddd;
}
.tab.active {
  background: #181825;
  color: #fff;
  box-shadow: inset 0 2px 0 #3b82f6;
}
.tab.moving {
  opacity: 0.5;
}
.body {
  flex: 1;
  min-height: 0;
}
</style>
