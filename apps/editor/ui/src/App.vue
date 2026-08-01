<script setup lang="ts">
import SceneTree from './components/SceneTree.vue'
import Inspector from './components/Inspector.vue'
import ViewportHole from './components/ViewportHole.vue'
</script>

<template>
  <div class="app-shell">
    <aside class="side-panel">
      <SceneTree />
      <Inspector />
    </aside>
    <main class="viewport-region">
      <ViewportHole />
    </main>
  </div>
</template>

<style scoped>
.app-shell {
  display: flex;
  width: 100vw;
  height: 100vh;
}
.side-panel {
  width: 320px;
  flex-shrink: 0;
  display: flex;
  flex-direction: column;
  background: #181825;
  border-right: 1px solid #333;
}
.viewport-region {
  flex: 1;
}
</style>
