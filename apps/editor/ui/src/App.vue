<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { editorState, setAiOpen } from './editorState'
import SceneTree from './components/SceneTree.vue'
import Inspector from './components/Inspector.vue'
import ViewportHole from './components/ViewportHole.vue'
import Toolbar from './components/Toolbar.vue'
import Splitter from './components/Splitter.vue'
import StatusBar from './components/StatusBar.vue'
import AssetBrowser from './components/AssetBrowser.vue'
import AiPanel from './components/AiPanel.vue'
import MenuBar from './components/MenuBar.vue'
import DialogHost from './components/DialogHost.vue'
import ContextMenuHost from './components/ContextMenuHost.vue'
import ShortcutsDialog from './components/ShortcutsDialog.vue'
import PreferencesDialog from './components/PreferencesDialog.vue'
import ProjectSettingsDialog from './components/ProjectSettingsDialog.vue'
import { showViewport, toggleBottomPanel, toggleLeftPanel } from './editorState'

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
// The AI panel collapses to a thin rail; open/closed is remembered (editorState).
const aiPanelWidth = ref(360)
const MIN_AI_PANEL = 260
const aiCollapsed = computed(() => !editorState.aiOpen)

function onAiPanelDrag(x: number): void {
  aiPanelWidth.value = clamp(window.innerWidth - x, MIN_AI_PANEL, window.innerWidth - sidebarWidth.value - MIN_VIEWPORT)
}

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
        aiPanelWidth: aiPanelWidth.value,
      }),
    )
  } catch {
    // Not persisting is fine; the layout just resets next launch.
  }
}

onMounted(() => {
  if (!editorState.panels.viewport) showViewport(false)
  try {
    const saved = JSON.parse(localStorage.getItem(LAYOUT_KEY) ?? 'null')
    if (saved && typeof saved.sidebarWidth === 'number' && typeof saved.treeFraction === 'number') {
      sidebarWidth.value = clamp(saved.sidebarWidth, MIN_SIDEBAR, window.innerWidth - MIN_VIEWPORT)
      treeFraction.value = clamp(saved.treeFraction, 0.1, 0.9)
    }
    if (saved && typeof saved.assetPanelHeight === 'number') {
      assetPanelHeight.value = clamp(saved.assetPanelHeight, MIN_ASSET_PANEL, window.innerHeight / 2)
    }
    if (saved && typeof saved.aiPanelWidth === 'number') {
      aiPanelWidth.value = clamp(saved.aiPanelWidth, MIN_AI_PANEL, window.innerWidth / 2)
    }
  } catch {
    // Corrupt or unavailable storage: keep the defaults.
  }
})
</script>

<template>
  <div class="app-frame">
  <MenuBar />
  <div class="app-shell">
    <button v-if="!editorState.panels.left" class="left-rail" title="Show the Scene panel (Ctrl+B)" @click="toggleLeftPanel">
      <span class="rail-label">Scene</span>
    </button>
    <aside v-show="editorState.panels.left" ref="sidePanel" class="side-panel" :style="{ width: `${sidebarWidth}px` }">
      <div class="panel-slot" :style="{ flexBasis: `${treeFraction * 100}%` }">
        <SceneTree />
      </div>
      <Splitter direction="vertical" @drag="onTreeDrag" @end="saveLayout" />
      <div class="panel-slot fill">
        <Inspector />
      </div>
    </aside>
    <Splitter v-show="editorState.panels.left" direction="horizontal" @drag="onSidebarDrag" @end="saveLayout" />
    <main class="viewport-region">
      <Toolbar />
      <!-- v-show: the hole keeps reporting (0 x 0 while hidden; the host hides the native window). -->
      <div v-show="editorState.panels.viewport" class="viewport-slot">
        <ViewportHole />
      </div>
      <button
        v-if="!editorState.panels.viewport"
        class="bottom-rail"
        title="Show the viewport (Ctrl+M)"
        @click="showViewport(true)"
      >
        ▾ Viewport
      </button>
      <Splitter
        v-show="editorState.panels.bottom && editorState.panels.viewport"
        direction="vertical"
        @drag="onAssetPanelDrag"
        @end="saveLayout"
      />
      <!-- v-show: hiding keeps the tabs' state (drafts in Story, World, Art). -->
      <div
        v-show="editorState.panels.bottom"
        class="asset-slot"
        :class="{ fill: !editorState.panels.viewport }"
        :style="editorState.panels.viewport ? { height: `${assetPanelHeight}px` } : {}"
        <AssetBrowser />
      </div>
      <button v-if="!editorState.panels.bottom" class="bottom-rail" title="Show the bottom panel (Ctrl+J)" @click="toggleBottomPanel">
        ▴ Prefabs · Assets · World · Story · Level · Art
      </button>
      <StatusBar />
    </main>
    <button
      v-if="aiCollapsed"
      class="ai-rail"
      :class="{ off: editorState.ai.choice === 'off' }"
      title="Open the AI panel"
      @click="setAiOpen(true)"
    >
      <span class="rail-mark">✦</span>
      <span class="rail-label">{{ editorState.ai.choice === 'off' ? 'AI off' : 'AI' }}</span>
      <span v-if="editorState.ai.busy" class="rail-busy"></span>
    </button>
    <!-- v-show, not v-if: collapsing keeps the conversation and pending proposals. -->
    <Splitter v-show="!aiCollapsed" direction="horizontal" @drag="onAiPanelDrag" @end="saveLayout" />
    <aside v-show="!aiCollapsed" class="ai-slot" :style="{ width: `${aiPanelWidth}px` }">
      <AiPanel />
    </aside>
  </div>
  </div>
  <ContextMenuHost />
  <ShortcutsDialog />
  <PreferencesDialog />
  <ProjectSettingsDialog />
  <DialogHost />
</template>

<style scoped>
.app-frame {
  display: flex;
  flex-direction: column;
  width: 100vw;
  height: 100vh;
}
.app-shell {
  display: flex;
  flex: 1;
  min-height: 0;
}
.left-rail {
  flex-shrink: 0;
  width: 26px;
  border: none;
  border-right: 1px solid #333;
  border-radius: 0;
  background: #181825;
  color: #eee;
  cursor: pointer;
  padding: 10px 0;
  display: flex;
  justify-content: center;
}
.left-rail:hover,
.bottom-rail:hover {
  background: #262637;
}
.bottom-rail {
  flex-shrink: 0;
  height: 22px;
  border: none;
  border-top: 1px solid #333;
  border-radius: 0;
  background: #181825;
  color: #aaa;
  font: inherit;
  font-size: 0.8em;
  cursor: pointer;
  text-align: left;
  padding: 0 10px;
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
.ai-slot {
  flex-shrink: 0;
  min-width: 0;
}
.ai-rail {
  flex-shrink: 0;
  width: 32px;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8px;
  padding: 10px 0;
  border: none;
  border-left: 1px solid #333;
  border-radius: 0;
  background: #181825;
  color: #eee;
  font: inherit;
  cursor: pointer;
}
.ai-rail:hover {
  background: #262637;
}
.rail-mark {
  color: #3b82f6;
}
.ai-rail.off .rail-mark {
  color: #666;
}
.rail-label {
  writing-mode: vertical-rl;
  font-size: 0.75em;
  letter-spacing: 0.1em;
  opacity: 0.7;
}
.rail-busy {
  width: 6px;
  height: 6px;
  background: #3b82f6;
}
.asset-slot {
  flex-shrink: 0;
  min-height: 0;
}
.asset-slot.fill {
  flex: 1;
}
</style>
