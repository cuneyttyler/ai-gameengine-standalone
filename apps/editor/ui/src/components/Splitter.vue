<script setup lang="ts">
// A drag handle between two panels. Emits the pointer's absolute client
// position while dragging; the parent owns the actual sizes and clamping.
// Pointer capture keeps the drag alive when the cursor passes over the
// native viewport window, which would otherwise swallow mouse events.
import { ref } from 'vue'

const props = defineProps<{ direction: 'horizontal' | 'vertical' }>()
const emit = defineEmits<{ drag: [position: number]; end: [] }>()

const dragging = ref(false)

function onPointerDown(event: PointerEvent): void {
  dragging.value = true
  ;(event.currentTarget as HTMLElement).setPointerCapture(event.pointerId)
  event.preventDefault()
}

function onPointerMove(event: PointerEvent): void {
  if (!dragging.value) {
    return
  }
  emit('drag', props.direction === 'horizontal' ? event.clientX : event.clientY)
}

function onPointerUp(event: PointerEvent): void {
  if (!dragging.value) {
    return
  }
  dragging.value = false
  ;(event.currentTarget as HTMLElement).releasePointerCapture(event.pointerId)
  emit('end')
}
</script>

<template>
  <div
    class="splitter"
    :class="[direction, { dragging }]"
    @pointerdown="onPointerDown"
    @pointermove="onPointerMove"
    @pointerup="onPointerUp"
    @pointercancel="onPointerUp"
  ></div>
</template>

<style scoped>
.splitter {
  flex-shrink: 0;
  background: #333;
  touch-action: none;
}
.splitter.horizontal {
  width: 4px;
  cursor: col-resize;
}
.splitter.vertical {
  height: 4px;
  cursor: row-resize;
}
.splitter:hover,
.splitter.dragging {
  background: #3b82f6;
}
</style>
