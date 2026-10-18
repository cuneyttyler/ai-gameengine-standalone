<script setup lang="ts">
// The Scene panel: the Scene Tree and the Inspector, one above the other when
// the panel is tall and side by side when it's wide (development_plan.md 5.6, 6.8).
import { computed, ref } from 'vue'
import { bodyRect } from '../dockdrag'
import { groupOf } from '../docktree'
import { layout, saveLayout } from '../layout'
import Inspector from './Inspector.vue'
import SceneTree from './SceneTree.vue'
import Splitter from './Splitter.vue'

const el = ref<HTMLElement | null>(null)
const across = computed(() => {
  const group = groupOf(layout.root, 'scene')
  const r = group ? bodyRect(group.id) : null
  return !!r && r.width > r.height * 1.3
})

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
