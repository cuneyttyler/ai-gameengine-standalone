<script setup lang="ts">
// The viewport as a panel (development_plan.md 6.8): the toolbar over the
// hole the native viewport window is laid into. While a panel is being
// dragged the native window is hidden (layout.ts), and a placeholder shows
// where it is.
import { dockView } from '../layout'
import Toolbar from './Toolbar.vue'
import ViewportHole from './ViewportHole.vue'
</script>

<template>
  <div class="viewport-panel">
    <Toolbar />
    <div class="slot">
      <ViewportHole />
      <div v-if="dockView.dragging" class="placeholder">Viewport</div>
    </div>
  </div>
</template>

<style scoped>
.viewport-panel {
  display: flex;
  flex-direction: column;
  width: 100%;
  height: 100%;
  min-width: 0;
  min-height: 0;
}
.slot {
  position: relative;
  flex: 1;
  min-height: 0;
  background: #11111a;
}
.placeholder {
  position: absolute;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #555;
  font-size: 1.1em;
  letter-spacing: 0.1em;
  pointer-events: none;
}
</style>
