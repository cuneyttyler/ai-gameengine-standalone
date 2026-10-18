<script setup lang="ts">
// An empty, transparent placeholder — the native Vulkan viewport window
// (engine/editor/ViewportWindow) is positioned exactly over this element's
// screen rect by the host, on top of the WebView2 surface in z-order. This
// component never renders anything itself; it only reports where it is.
import { onBeforeUnmount, onMounted, ref } from 'vue'
import { sendToHost } from '../ipc'

const holeEl = ref<HTMLDivElement | null>(null)
let observer: ResizeObserver | null = null

function reportBounds() {
  if (!holeEl.value) {
    return
  }
  const rect = holeEl.value.getBoundingClientRect()
  if (rect.width === 0 || rect.height === 0) return // hidden; the host keeps its last place
  sendToHost({
    type: 'viewport:bounds',
    x: Math.round(rect.left),
    y: Math.round(rect.top),
    width: Math.round(rect.width),
    height: Math.round(rect.height),
  })
}

onMounted(() => {
  reportBounds()
  observer = new ResizeObserver(() => reportBounds())
  if (holeEl.value) {
    observer.observe(holeEl.value)
  }
  window.addEventListener('resize', reportBounds)
  // The layout can move the viewport without resizing it (6.8).
  window.addEventListener('aige:layout', reportBounds)
})

onBeforeUnmount(() => {
  observer?.disconnect()
  window.removeEventListener('resize', reportBounds)
  window.removeEventListener('aige:layout', reportBounds)
})
</script>

<template>
  <div ref="holeEl" class="viewport-hole"></div>
</template>

<style scoped>
.viewport-hole {
  width: 100%;
  height: 100%;
}
</style>
