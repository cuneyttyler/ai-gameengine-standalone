<script setup lang="ts">
// The Scene panel: the Scene Tree and the Inspector, one above the other in
// a side dock and side by side at the bottom (development_plan.md 5.6).
import { computed, ref } from 'vue'
import { layout, saveLayout } from '../layout'
import Inspector from './Inspector.vue'
import SceneTree from './SceneTree.vue'
import Splitter from './Splitter.vue'

const el = ref<HTMLElement | null>(null)
const across = computed(() => layout.dock.scene === 'bottom')

function onDrag(position: number): void {
  if (!el.value) return
  const r = el.value.getBoundingClientRect()
  const fraction = across.value ? (position - r.left) / r.width : (position - r.top) / r.height
  layout.treeFraction = Math.min(0.9, Math.max(0.1, fraction))
}
</script>

<template>
  <div ref="el" class="scene-panel" :class="{ across }">
    <div class="slot" :style="{ flexBasis: `${layout.treeFraction * 100}%` }">
      <SceneTree />
    </div>
    <Splitter :direction="across ? 'horizontal' : 'vertical'" @drag="onDrag" @end="saveLayout" />
    <div class="slot fill">
      <Inspector />
    </div>
  </div>
</template>

<style scoped>
.scene-panel {
  display: flex;
  flex-direction: column;
  flex: 1;
  min-height: 0;
  min-width: 0;
  background: #181825;
}
.scene-panel.across {
  flex-direction: row;
}
.slot {
  display: flex;
  flex-direction: column;
  flex-grow: 0;
  flex-shrink: 0;
  min-height: 0;
  min-width: 0;
  overflow: hidden;
}
.slot.fill {
  flex: 1;
}
</style>
