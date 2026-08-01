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
})

onBeforeUnmount(() => {
  observer?.disconnect()
  window.removeEventListener('resize', reportBounds)
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
