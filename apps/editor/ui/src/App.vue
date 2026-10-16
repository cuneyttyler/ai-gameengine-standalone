<script setup lang="ts">
// The editor's frame (development_plan.md 4.1, 5.6): the menu bar, three
// docks (left, right, bottom) around the viewport, and the dialogs. Panels
// are teleported into whichever dock they're in (layout.ts), so moving one
// keeps its state — drafts, the AI conversation.
import { computed, onMounted, ref } from 'vue'
import { editorState } from './editorState'
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
import PanelFrame from './components/PanelFrame.vue'
import ScenePanel from './components/ScenePanel.vue'
import { keyLabel } from './keys'
import {
  layout,
  panelNames,
  panelsIn,
  saveLayout,
  setPanelVisible,
  showViewport,
  visibleIn,
  type Dock,
  type PanelId,
} from './layout'

const MIN_SIDE = 200
const MIN_BOTTOM = 120
const MIN_VIEWPORT = 320

const shown = computed(() => ({
  left: visibleIn('left').length > 0,
  right: visibleIn('right').length > 0,
  bottom: visibleIn('bottom').length > 0,
}))
function hiddenIn(dock: Dock): PanelId[] {
  return panelsIn(dock).filter((id) => !layout.visible[id])
}

const leftDock = ref<HTMLElement | null>(null)
const rightDock = ref<HTMLElement | null>(null)
const center = ref<HTMLElement | null>(null)

function clamp(value: number, min: number, max: number): number {
  return Math.min(Math.max(value, min), Math.max(min, max))
}

function onLeftDrag(x: number): void {
  const start = leftDock.value?.getBoundingClientRect().left ?? 0
  layout.size.left = clamp(x - start, MIN_SIDE, window.innerWidth - layout.size.right - MIN_VIEWPORT)
}
function onRightDrag(x: number): void {
  const end = rightDock.value?.getBoundingClientRect().right ?? window.innerWidth
  layout.size.right = clamp(end - x, MIN_SIDE, window.innerWidth - layout.size.left - MIN_VIEWPORT)
}
function onBottomDrag(y: number): void {
  // The status bar (24px) sits below the bottom dock.
  const end = (center.value?.getBoundingClientRect().bottom ?? window.innerHeight) - 24
  layout.size.bottom = clamp(end - y, MIN_BOTTOM, window.innerHeight - 140)
}

onMounted(() => {
  if (!layout.viewport) showViewport(false) // tell the host to hide the native window
  layout.size.left = clamp(layout.size.left, MIN_SIDE, window.innerWidth / 2)
  layout.size.right = clamp(layout.size.right, MIN_SIDE, window.innerWidth / 2)
  layout.size.bottom = clamp(layout.size.bottom, MIN_BOTTOM, window.innerHeight / 2)
})
</script>

<template>
  <div class="app-frame">
    <MenuBar />
    <div class="app-shell">
      <div v-if="hiddenIn('left').length" class="rails side">
        <button
          v-for="id in hiddenIn('left')"
          :key="id"
          class="rail"
          :title="`Show the ${panelNames[id]} panel (${keyLabel(`panel.${id}`)})`"
          @click="setPanelVisible(id, true)"
        >
          <span v-if="id === 'ai'" class="rail-mark" :class="{ off: editorState.ai.choice === 'off' }">✦</span>
          <span class="rail-label">{{ panelNames[id] }}</span>
        </button>
      </div>
      <aside v-show="shown.left" id="dock-left" ref="leftDock" class="dock side" :style="{ width: `${layout.size.left}px` }"></aside>
      <Splitter v-show="shown.left" direction="horizontal" @drag="onLeftDrag" @end="saveLayout" />

      <main ref="center" class="viewport-region">
        <Toolbar />
        <!-- v-show: the hole keeps reporting (0 x 0 while hidden; the host hides the native window). -->
        <div v-show="layout.viewport" class="viewport-slot">
          <ViewportHole />
        </div>
        <button v-if="!layout.viewport" class="strip" :title="`Show the viewport (${keyLabel('panel.viewport')})`" @click="showViewport(true)">
          ▾ Viewport
        </button>
        <p v-if="!layout.viewport && !shown.bottom" class="empty-center">
          The viewport is hidden. Panels docked at the bottom take its place here.
        </p>
        <Splitter v-show="shown.bottom && layout.viewport" direction="vertical" @drag="onBottomDrag" @end="saveLayout" />
        <div
          v-show="shown.bottom"
          id="dock-bottom"
          class="dock bottom"
          :class="{ fill: !layout.viewport }"
          :style="layout.viewport ? { height: `${layout.size.bottom}px` } : {}"
        ></div>
        <div v-if="hiddenIn('bottom').length" class="strips">
          <button
            v-for="id in hiddenIn('bottom')"
            :key="id"
            class="strip"
            :title="`Show the ${panelNames[id]} panel (${keyLabel(`panel.${id}`)})`"
            @click="setPanelVisible(id, true)"
          >
            ▴ {{ panelNames[id] }}
          </button>
        </div>
        <StatusBar />
      </main>

      <Splitter v-show="shown.right" direction="horizontal" @drag="onRightDrag" @end="saveLayout" />
      <aside v-show="shown.right" id="dock-right" ref="rightDock" class="dock side" :style="{ width: `${layout.size.right}px` }"></aside>
      <div v-if="hiddenIn('right').length" class="rails side right">
        <button
          v-for="id in hiddenIn('right')"
          :key="id"
          class="rail"
          :title="`Show the ${panelNames[id]} panel (${keyLabel(`panel.${id}`)})`"
          @click="setPanelVisible(id, true)"
        >
          <span v-if="id === 'ai'" class="rail-mark" :class="{ off: editorState.ai.choice === 'off' }">✦</span>
          <span class="rail-label">{{ id === 'ai' && editorState.ai.choice === 'off' ? 'AI off' : panelNames[id] }}</span>
          <span v-if="id === 'ai' && editorState.ai.busy" class="rail-busy"></span>
        </button>
      </div>
    </div>
  </div>

  <!-- The panels, wherever they're docked. -->
  <Teleport defer :to="`#dock-${layout.dock.scene}`">
    <PanelFrame id="scene"><ScenePanel /></PanelFrame>
  </Teleport>
  <Teleport defer :to="`#dock-${layout.dock.assets}`">
    <PanelFrame id="assets"><AssetBrowser /></PanelFrame>
  </Teleport>
  <Teleport defer :to="`#dock-${layout.dock.ai}`">
    <PanelFrame id="ai"><AiPanel /></PanelFrame>
  </Teleport>

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
.dock {
  display: flex;
  min-width: 0;
  min-height: 0;
  background: #181825;
}
.dock.side {
  flex-shrink: 0;
  flex-direction: column;
}
.dock.bottom {
  flex-shrink: 0;
  flex-direction: row;
}
.dock.bottom.fill {
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
.rails {
  flex-shrink: 0;
  display: flex;
  flex-direction: column;
  background: #181825;
  border-right: 1px solid #333;
}
.rails.right {
  border-right: none;
  border-left: 1px solid #333;
}
.rail {
  width: 30px;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8px;
  padding: 10px 0;
  border: none;
  border-bottom: 1px solid #2a2a3a;
  border-radius: 0;
  background: transparent;
  color: #eee;
  font: inherit;
  cursor: pointer;
}
.rail:hover,
.strip:hover {
  background: #262637;
}
.rail-mark {
  color: #3b82f6;
}
.rail-mark.off {
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
.strips {
  display: flex;
  flex-shrink: 0;
  border-top: 1px solid #333;
}
.strip {
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
.strips .strip {
  border-top: none;
  border-right: 1px solid #333;
}
.empty-center {
  flex: 1;
  margin: 0;
  padding: 24px;
  opacity: 0.5;
}
</style>
