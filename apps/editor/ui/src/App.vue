<script setup lang="ts">
import { onMounted, ref } from 'vue'
import SceneTree from './components/SceneTree.vue'
import Inspector from './components/Inspector.vue'
import ViewportHole from './components/ViewportHole.vue'
import Toolbar from './components/Toolbar.vue'
import Splitter from './components/Splitter.vue'
import StatusBar from './components/StatusBar.vue'
import AssetBrowser from './components/AssetBrowser.vue'

// Panel sizes are per-user layout preferences, so localStorage is enough.
// Every access is guarded: storage can be unavailable, and the layout must
// still work with the defaults.
const LAYOUT_KEY = 'aige.editor.layout'
const MIN_SIDEBAR = 200
const MIN_VIEWPORT = 320
const MIN_PANEL = 80

const MIN_ASSET_PANEL = 120

const sidebarWidth = ref(320)
const treeFraction = ref(0.45) // Scene Tree's share of the side panel's height
const assetPanelHeight = ref(240)

const sidePanel = ref<HTMLElement | null>(null)

function onAssetPanelDrag(y: number): void {
  // The status bar (24px) sits below the asset panel.
  const height = window.innerHeight - 24 - y
  assetPanelHeight.value = clamp(height, MIN_ASSET_PANEL, window.innerHeight - 36 - 24 - MIN_VIEWPORT / 2)
}

function clamp(value: number, min: number, max: number): number {
  return Math.min(Math.max(value, min), Math.max(min, max))
}

function onSidebarDrag(x: number): void {
  sidebarWidth.value = clamp(x, MIN_SIDEBAR, window.innerWidth - MIN_VIEWPORT)
}

function onTreeDrag(y: number): void {
  if (!sidePanel.value) {
    return
  }
  const rect = sidePanel.value.getBoundingClientRect()
  const height = clamp(y - rect.top, MIN_PANEL, rect.height - MIN_PANEL)
  treeFraction.value = height / rect.height
}

function saveLayout(): void {
  try {
    localStorage.setItem(
      LAYOUT_KEY,
      JSON.stringify({
        sidebarWidth: sidebarWidth.value,
        treeFraction: treeFraction.value,
        assetPanelHeight: assetPanelHeight.value,
      }),
    )
  } catch {
    // Not persisting is fine; the layout just resets next launch.
  }
}

onMounted(() => {
  try {
    const saved = JSON.parse(localStorage.getItem(LAYOUT_KEY) ?? 'null')
    if (saved && typeof saved.sidebarWidth === 'number' && typeof saved.treeFraction === 'number') {
      sidebarWidth.value = clamp(saved.sidebarWidth, MIN_SIDEBAR, window.innerWidth - MIN_VIEWPORT)
      treeFraction.value = clamp(saved.treeFraction, 0.1, 0.9)
    }
    if (saved && typeof saved.assetPanelHeight === 'number') {
      assetPanelHeight.value = clamp(saved.assetPanelHeight, MIN_ASSET_PANEL, window.innerHeight / 2)
    }
  } catch {
    // Corrupt or unavailable storage: keep the defaults.
  }
})
</script>

<template>
  <div class="app-shell">
    <aside ref="sidePanel" class="side-panel" :style="{ width: `${sidebarWidth}px` }">
      <div class="panel-slot" :style="{ flexBasis: `${treeFraction * 100}%` }">
        <SceneTree />
      </div>
      <Splitter direction="vertical" @drag="onTreeDrag" @end="saveLayout" />
      <div class="panel-slot fill">
        <Inspector />
      </div>
    </aside>
    <Splitter direction="horizontal" @drag="onSidebarDrag" @end="saveLayout" />
    <main class="viewport-region">
      <Toolbar />
      <div class="viewport-slot">
        <ViewportHole />
      </div>
      <Splitter direction="vertical" @drag="onAssetPanelDrag" @end="saveLayout" />
      <div class="asset-slot" :style="{ height: `${assetPanelHeight}px` }">
        <AssetBrowser />
      </div>
      <StatusBar />
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
  flex-shrink: 0;
  display: flex;
  flex-direction: column;
  background: #181825;
  min-height: 0;
}
.panel-slot {
  display: flex;
  flex-direction: column;
  flex-grow: 0;
  flex-shrink: 0;
  min-height: 0;
  overflow: hidden;
}
.panel-slot.fill {
  flex: 1;
}
.viewport-region {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
}
.viewport-slot {
  flex: 1;
  min-height: 0;
}
.asset-slot {
  flex-shrink: 0;
  min-height: 0;
}
</style>
