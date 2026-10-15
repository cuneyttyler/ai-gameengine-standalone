<script setup lang="ts">
import { computed } from 'vue'
import { editorState, type ToolId } from '../editorState'
import { sendToHost } from '../ipc'

interface ToolButton {
  id: ToolId
  label: string
  key: string
  swatch?: string
}

const tools: ToolButton[] = [
  { id: 'select', label: 'Select', key: 'V' },
  { id: 'tile:grass', label: 'Grass', key: '1', swatch: '#468c3c' },
  { id: 'tile:dirt', label: 'Dirt', key: '2', swatch: '#785a3c' },
  { id: 'tile:wall', label: 'Wall', key: '3', swatch: '#2f4d2a' },
  { id: 'place:npc', label: 'NPC', key: 'N', swatch: '#3c5ac8' },
  { id: 'place:player', label: 'Player', key: 'P', swatch: '#c83c3c' },
  { id: 'place:enemy', label: 'Enemy', key: 'X', swatch: '#c88c8c' },
  { id: 'place:pickup', label: 'Potion', key: 'H', swatch: '#4cff66' },
  { id: 'erase', label: 'Erase', key: 'E' },
]

const brushName = computed(() => {
  const name = editorState.brush.asset.split('/').pop() ?? ''
  return editorState.brush.tile > 0 ? `${name} #${editorState.brush.tile}` : name
})

function setTool(id: ToolId): void {
  sendToHost({ type: 'tool:set', tool: id })
}

function send(type: string): void {
  sendToHost({ type })
}
</script>

<template>
  <div class="toolbar" :class="{ playing: editorState.playing }">
    <div class="group" :class="{ disabled: editorState.playing }">
      <button
        v-for="tool in tools"
        :key="tool.id"
        class="tool"
        :class="{ active: editorState.tool === tool.id }"
        :title="`${tool.label} (${tool.key})`"
        :disabled="editorState.playing"
        @click="setTool(tool.id)"
      >
        <span v-if="tool.swatch" class="swatch" :style="{ background: tool.swatch }"></span>
        {{ tool.label }}
      </button>
    </div>

    <div
      v-if="editorState.tool === 'place:sprite' || editorState.tool === 'tile:asset'"
      class="brush"
      :title="editorState.brush.asset"
    >
      <span class="brush-kind">{{ editorState.tool === 'tile:asset' ? 'Tile brush' : 'Prop' }}</span>
      {{ brushName }}
      <span v-if="editorState.tool === 'tile:asset' && editorState.brush.solid" class="brush-kind">solid</span>
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
.swatch {
  width: 10px;
  height: 10px;
  border: 1px solid rgba(255, 255, 255, 0.25);
}
.brush {
  display: flex;
  align-items: center;
  gap: 6px;
  height: 26px;
  padding: 0 10px;
  border: 1px solid #3b82f6;
  background: #313148;
  font-size: 0.8em;
  white-space: nowrap;
}
.brush-kind {
  opacity: 0.6;
  text-transform: uppercase;
  font-size: 0.85em;
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
