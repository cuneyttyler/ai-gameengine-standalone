<script setup lang="ts">
import { computed } from 'vue'
import { editorState } from '../editorState'

const hints: Record<string, string> = {
  select: 'Click to select · drag to move · Del delete · Ctrl+D duplicate',
  'tile:grass': 'Click or drag to paint grass',
  'tile:dirt': 'Click or drag to paint dirt',
  'tile:wall': 'Click or drag to paint walls',
  'place:npc': 'Click a cell to place an NPC',
  'place:player': 'Click a cell to place the player (moves the existing one)',
  erase: 'Click or drag to erase',
  'place:sprite': 'Click a cell to place the selected asset as a prop',
  'tile:asset': 'Click or drag to paint tiles from the selected tileset',
  'place:enemy': 'Click a cell to place an enemy (chases and attacks the player)',
  'place:pickup': 'Click a cell to place a health potion',
}

const hint = computed(() =>
  editorState.playing
    ? 'Playing · WASD move · Space or click attack · Esc stop · changes revert on stop'
    : `${hints[editorState.tool] ?? ''} · right-drag pan · wheel zoom · F frame`,
)
</script>

<template>
  <div class="status-bar">
    <span class="scene">{{ editorState.sceneName }}{{ editorState.dirty ? ' •' : '' }}</span>
    <span class="hint">{{ hint }}</span>
    <span v-if="editorState.notice" class="notice">{{ editorState.notice }}</span>
  </div>
</template>

<style scoped>
.status-bar {
  display: flex;
  align-items: center;
  gap: 16px;
  height: 24px;
  padding: 0 8px;
  background: #181825;
  border-top: 1px solid #333;
  font-size: 0.75em;
  white-space: nowrap;
  overflow: hidden;
}
.scene {
  opacity: 0.85;
}
.hint {
  opacity: 0.55;
  overflow: hidden;
  text-overflow: ellipsis;
}
.notice {
  margin-left: auto;
  color: #93b4f8;
}
</style>
