<script setup lang="ts">
// One docked panel (development_plan.md 5.6): sizes itself within its dock,
// shares a resize handle with the panel before it, and offers Move / Hide on
// a right-click of its header.
import { computed } from 'vue'
import { openContextMenu, separator, type MenuItem } from '../contextMenu'
import { keyLabel } from '../keys'
import { dockNames, layout, movePanel, saveLayout, setPanelVisible, visibleIn, type Dock, type PanelId } from '../layout'

const props = defineProps<{ id: PanelId }>()

const dock = computed(() => layout.dock[props.id])
const previous = computed(() => {
  const shown = visibleIn(dock.value)
  const at = shown.indexOf(props.id)
  return at > 0 ? shown[at - 1]! : null
})

function startResize(event: PointerEvent): void {
  const prev = previous.value
  if (!prev) return
  const handle = event.currentTarget as HTMLElement
  handle.setPointerCapture(event.pointerId)
  const across = dock.value === 'bottom' // side by side
  const move = (e: PointerEvent) => {
    const a = document.getElementById(`panel-${prev}`)?.getBoundingClientRect()
    const b = document.getElementById(`panel-${props.id}`)?.getBoundingClientRect()
    if (!a || !b) return
    const total = across ? a.width + b.width : a.height + b.height
    const start = across ? a.left : a.top
    const first = Math.min(Math.max((across ? e.clientX : e.clientY) - start, 80), total - 80)
    const weights = layout.weight[prev] + layout.weight[props.id]
    layout.weight[prev] = (weights * first) / total
    layout.weight[props.id] = weights - layout.weight[prev]
  }
  const up = () => {
    handle.removeEventListener('pointermove', move)
    handle.removeEventListener('pointerup', up)
    saveLayout()
  }
  handle.addEventListener('pointermove', move)
  handle.addEventListener('pointerup', up)
}

function onContextMenu(event: MouseEvent): void {
  const target = event.target as HTMLElement
  if (!target.closest('h2, .panel-header, .header, .tabs') || target.closest('input, textarea, select')) return
  event.preventDefault()
  const items: MenuItem[] = [
    ...(['left', 'bottom', 'right'] as Dock[]).map((d) => ({
      label: `${dock.value === d ? '✓ ' : ''}Dock ${dockNames[d].toLowerCase()}`,
      disabled: dock.value === d,
      action: () => movePanel(props.id, d),
    })),
    separator,
    { label: 'Hide', shortcut: keyLabel(`panel.${props.id}`), action: () => setPanelVisible(props.id, false) },
  ]
  openContextMenu(event.clientX, event.clientY, items)
}
</script>

<template>
  <div
    v-show="layout.visible[id]"
    :id="`panel-${id}`"
    class="panel-frame"
    :class="dock === 'bottom' ? 'across' : 'down'"
    :style="{ order: layout.order.indexOf(id), flex: `${layout.weight[id]} 1 0` }"
    @contextmenu="onContextMenu"
  >
    <div v-if="previous" class="handle" title="Drag to resize" @pointerdown="startResize"></div>
    <div class="content"><slot /></div>
  </div>
</template>

<style scoped>
.panel-frame {
  display: flex;
  min-width: 0;
  min-height: 0;
  overflow: hidden;
}
.panel-frame.down {
  flex-direction: column;
}
.panel-frame.across {
  flex-direction: row;
}
.content {
  flex: 1;
  display: flex;
  flex-direction: column;
  min-width: 0;
  min-height: 0;
  overflow: hidden;
}
.handle {
  flex-shrink: 0;
  background: #2a2a3a;
}
.down .handle {
  height: 4px;
  cursor: row-resize;
}
.across .handle {
  width: 4px;
  cursor: col-resize;
}
.handle:hover {
  background: #3b82f6;
}
</style>
