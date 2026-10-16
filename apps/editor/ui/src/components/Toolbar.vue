<script setup lang="ts">
import { computed } from 'vue'
import { armPrefab, editorState, type ToolId } from '../editorState'
import { sendToHost } from '../ipc'
import PrefabThumb from './PrefabThumb.vue'

const brush = computed(() => editorState.prefabs.find((p) => p.path === editorState.brush) ?? null)

function setTool(id: ToolId): void {
  sendToHost({ type: 'tool:set', tool: id })
}

function setBrushScale(percent: number): void {
  if (!Number.isFinite(percent) || percent <= 0) return
  sendToHost({ type: 'tool:set', tool: editorState.tool, scale: percent / 100 })
}

function send(type: string): void {
  sendToHost({ type })
}
</script>

<template>
  <div class="toolbar" :class="{ playing: editorState.playing }">
    <div class="group" :class="{ disabled: editorState.playing }">
      <button
        :class="{ active: editorState.tool === 'select' }"
        title="Select, move, delete (V)"
        :disabled="editorState.playing"
        @click="setTool('select')"
      >
        Select
      </button>
      <button
        class="brush"
        :class="{ active: editorState.tool === 'prefab' }"
        :disabled="editorState.playing || !brush"
        :title="
          brush
            ? `${brush.isTile ? 'Paint' : 'Place'} ${brush.name} — pick another in the Prefabs panel or press 1-9`
            : 'Pick a prefab in the Prefabs panel (or press 1-9) to paint or place it'
        "
        @click="brush && armPrefab(brush.path)"
      >
        <PrefabThumb v-if="brush" :prefab="brush" :box="18" />
        <span v-if="brush">{{ brush.isTile ? 'Paint' : 'Place' }} {{ brush.name }}</span>
        <span v-else class="muted">No prefab picked</span>
      </button>
      <button
        v-if="brush && brush.flat"
        class="turn"
        :title="`Turn what the brush paints a quarter (R) — now ${editorState.brushRotation * 90}°`"
        @click="sendToHost({ type: 'edit:rotate' })"
      >
        ↻ {{ editorState.brushRotation * 90 }}°
      </button>
      <label v-if="brush" class="size" title="Size of what the brush places (% of the prefab)">
        <input
          type="number"
          min="10"
          max="1000"
          step="10"
          :value="Math.round(editorState.brushScale * 100)"
          @change="setBrushScale(Number(($event.target as HTMLInputElement).value))"
        />%
      </label>
      <button
        :class="{ active: editorState.tool === 'erase' }"
        title="Erase (E)"
        :disabled="editorState.playing"
        @click="setTool('erase')"
      >
        Erase
      </button>
    </div>

    <div class="group">
      <button :disabled="!editorState.canUndo" title="Undo (Ctrl+Z)" @click="send('edit:undo')">Undo</button>
      <button :disabled="!editorState.canRedo" title="Redo (Ctrl+Y)" @click="send('edit:redo')">Redo</button>
      <button :disabled="editorState.playing" title="Save (Ctrl+S)" @click="send('scene:save')">
        Save<span v-if="editorState.dirty" class="dirty-dot" aria-label="unsaved changes"></span>
      </button>
      <button title="Frame scene (F)" @click="send('view:frame')">Frame</button>
    </div>

    <div class="spacer"></div>

    <button
      class="play-button"
      :class="{ active: editorState.playing }"
      :title="editorState.playing ? 'Stop (Esc in viewport)' : 'Play'"
      @click="send(editorState.playing ? 'play:stop' : 'play:start')"
    >
      <span class="icon" aria-hidden="true">{{ editorState.playing ? '■' : '▶' }}</span>
      {{ editorState.playing ? 'Stop' : 'Play' }}
    </button>
  </div>
</template>

<style scoped>
.toolbar {
  display: flex;
  align-items: center;
  gap: 12px;
  height: 36px;
  padding: 0 8px;
  background: #181825;
  border-bottom: 1px solid #333;
  overflow-x: auto;
  scrollbar-width: none;
}
.toolbar.playing {
  border-bottom-color: #3b82f6;
}
.group {
  display: flex;
  gap: 2px;
}
.group.disabled {
  opacity: 0.4;
}
.spacer {
  flex: 1;
}
button {
  display: flex;
  align-items: center;
  gap: 6px;
  height: 26px;
  padding: 0 10px;
  border: 1px solid #333;
  border-radius: 0;
  background: #11111b;
  color: #eee;
  font: inherit;
  font-size: 0.8em;
  white-space: nowrap;
  cursor: pointer;
}
button:hover:not(:disabled) {
  background: #262637;
}
button:disabled {
  opacity: 0.4;
  cursor: default;
}
button.active {
  background: #313148;
  border-color: #3b82f6;
}
.brush {
  min-width: 140px;
}
.muted {
  opacity: 0.6;
}
.size {
  display: flex;
  align-items: center;
  gap: 2px;
  font-size: 0.8em;
  opacity: 0.85;
}
.size input {
  width: 52px;
  height: 22px;
  border: 1px solid #333;
  border-radius: 0;
  background: #11111b;
  color: #eee;
  font: inherit;
  padding: 0 4px;
}
.dirty-dot {
  width: 6px;
  height: 6px;
  background: #3b82f6;
}
.play-button.active {
  background: #3b82f6;
  border-color: #3b82f6;
}
.icon {
  font-size: 0.8em;
}
</style>
