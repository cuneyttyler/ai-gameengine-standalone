<script setup lang="ts">
// The editor's frame (development_plan.md 4.1, 5.6, 6.8): the menu bar, the
// layout tree of tab groups (DockNode), and the dialogs. Every panel, the
// viewport included, is mounted once in the panel layer below and laid over
// the body of the group it's in, so dragging it to another group, a side or
// a window edge never resets it (drafts, the AI conversation, scroll).
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { editorState } from './editorState'
import { bodyRect, dragView, scheduleMeasure, setShell } from './dockdrag'
import { groupOf } from './docktree'
import StatusBar from './components/StatusBar.vue'
import AssetBrowser from './components/AssetBrowser.vue'
import AiPanel from './components/AiPanel.vue'
import MenuBar from './components/MenuBar.vue'
import DialogHost from './components/DialogHost.vue'
import ContextMenuHost from './components/ContextMenuHost.vue'
import ShortcutsDialog from './components/ShortcutsDialog.vue'
import PreferencesDialog from './components/PreferencesDialog.vue'
import ProjectSettingsDialog from './components/ProjectSettingsDialog.vue'
import PackImportDialog from './components/PackImportDialog.vue'
import DockNode from './components/DockNode.vue'
import ScenePanel from './components/ScenePanel.vue'
import ViewportPanel from './components/ViewportPanel.vue'
import { keyLabel } from './keys'
import {
  dockView,
  hiddenPanels,
  isFront,
  layout,
  panelIds,
  panelNames,
  resetLayout,
  setPanelVisible,
  syncViewportWindow,
  type PanelId,
} from './layout'

const shell = ref<HTMLElement | null>(null)

// Where each panel is drawn: its group's body, or nowhere.
function hostStyle(id: PanelId): Record<string, string> {
  const group = groupOf(layout.root, id)
  const body = group ? bodyRect(group.id) : null
  if (!group || !body || !isFront(id)) return { display: 'none' }
  return {
    left: `${body.x}px`,
    top: `${body.y}px`,
    width: `${body.width}px`,
    height: `${body.height}px`,
  }
}

const hidden = computed(() => hiddenPanels())

// The tree changed shape: the groups' boxes have to be measured again once
// the DOM has caught up.
watch(() => layout.root, scheduleMeasure, { deep: true, flush: 'post' })

onMounted(() => {
  setShell(shell.value)
  syncViewportWindow()
})
onBeforeUnmount(() => setShell(null))
</script>

<template>
  <div class="app-frame">
    <MenuBar />
    <div class="app-shell">
      <div v-if="hidden.length" class="rails">
        <button
          v-for="id in hidden"
          :key="id"
          class="rail"
          :title="`Show the ${panelNames[id]} ${id === 'viewport' ? '' : 'panel '}(${keyLabel(`panel.${id}`)})`"
          @click="setPanelVisible(id, true)"
        >
          <span v-if="id === 'ai'" class="rail-mark" :class="{ off: editorState.ai.choice === 'off' }">✦</span>
          <span class="rail-label">{{ id === 'ai' && editorState.ai.choice === 'off' ? 'AI off' : panelNames[id] }}</span>
          <span v-if="id === 'ai' && editorState.ai.busy" class="rail-busy"></span>
        </button>
      </div>

      <div ref="shell" class="dock-shell">
        <DockNode v-if="layout.root" :node="layout.root" />
        <div v-else class="empty">
          <p>Every panel is hidden.</p>
          <div class="empty-buttons">
            <button v-for="id in panelIds" :key="id" @click="setPanelVisible(id, true)">Show {{ panelNames[id] }}</button>
            <button class="primary" @click="resetLayout()">Reset layout</button>
          </div>
        </div>

        <!-- The panels, each over its group's body. Always mounted: hiding or moving keeps their state. -->
        <div class="panel-host" :style="hostStyle('viewport')"><ViewportPanel /></div>
        <div class="panel-host" :style="hostStyle('scene')"><ScenePanel /></div>
        <div class="panel-host" :style="hostStyle('assets')"><AssetBrowser /></div>
        <div class="panel-host" :style="hostStyle('ai')"><AiPanel /></div>

        <!-- While dragging: where the panel will land, and its name under the pointer. -->
        <template v-if="dockView.dragging">
          <div class="drag-catcher"></div>
          <div
            v-if="dragView.marker"
            class="drop-marker"
            :style="{
              left: `${dragView.marker.x}px`,
              top: `${dragView.marker.y}px`,
              width: `${dragView.marker.width}px`,
              height: `${dragView.marker.height}px`,
            }"
          ></div>
          <div class="drag-label" :style="{ left: `${dragView.x + 14}px`, top: `${dragView.y + 12}px` }">
            {{ panelNames[dockView.dragging] }}
          </div>
        </template>
      </div>
    </div>
    <StatusBar />
  </div>

  <ContextMenuHost />
  <ShortcutsDialog />
  <PreferencesDialog />
  <ProjectSettingsDialog />
  <PackImportDialog />
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
.dock-shell {
  position: relative;
  flex: 1;
  min-width: 0;
  min-height: 0;
  display: flex;
  background: #11111a;
}
.panel-host {
  position: absolute;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  min-width: 0;
  min-height: 0;
}
.panel-host > :deep(*) {
  flex: 1;
  min-height: 0;
  min-width: 0;
}
.drag-catcher {
  position: absolute;
  inset: 0;
  cursor: grabbing;
  z-index: 10;
}
.drop-marker {
  box-sizing: border-box;
  position: absolute;
  z-index: 11;
  background: rgba(59, 130, 246, 0.25);
  border: 2px solid #3b82f6;
  pointer-events: none;
  transition: left 60ms, top 60ms, width 60ms, height 60ms;
}
.drag-label {
  position: absolute;
  z-index: 12;
  padding: 3px 8px;
  background: #3b82f6;
  color: #fff;
  font-size: 0.8em;
  pointer-events: none;
  white-space: nowrap;
}
.empty {
  margin: auto;
  text-align: center;
  opacity: 0.8;
}
.empty-buttons {
  display: flex;
  gap: 6px;
  justify-content: center;
}
.rails {
  flex-shrink: 0;
  display: flex;
  flex-direction: column;
  background: #181825;
  border-right: 1px solid #333;
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
.rail:hover {
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
</style>
