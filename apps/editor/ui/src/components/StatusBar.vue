<script setup lang="ts">
import { computed } from 'vue'
import { editorState } from '../editorState'
import { activities, openPanel, panelOpen, unseen } from '../activity'
import NotificationsPanel from './NotificationsPanel.vue'

const hints: Record<string, string> = {
  select: 'Click to select · drag to move · Del delete · Ctrl+D duplicate',
  erase: 'Click or drag to erase',
  prefab: 'Tile prefabs: click or drag to paint · others: click to place · 1-9 pick from the palette',
  path: 'Click to add a point · click a point to branch from it · Shift+click a point to join · drag to move · Enter finishes',
  marker: 'Click a cell to add a marker · click a marker to select it',
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
    <button class="bell" :class="{ open: panelOpen, busy: activities.some((a) => a.state === 'running') }"
            title="Notifications: what's been happening (art, characters, animations, backgrounds, the AI's work)" @click="openPanel()">
      <span class="dot" v-if="activities.some((a) => a.state === 'running')">◌</span>
      🔔<span v-if="unseen" class="count">{{ unseen }}</span>
    </button>
    <NotificationsPanel />
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
.bell {
  margin-left: auto;
  height: 20px;
  padding: 0 6px;
  display: flex;
  align-items: center;
  gap: 3px;
  background: none;
  border: 1px solid transparent;
  font-size: 11px;
}
.bell.open,
.bell:hover {
  border-color: #3b82f6;
}
.bell .dot {
  color: #93c5fd;
  animation: spin 1.2s linear infinite;
  display: inline-block;
}
@keyframes spin {
  to {
    transform: rotate(360deg);
  }
}
.bell .count {
  background: #3b82f6;
  color: white;
  padding: 0 4px;
  font-size: 10px;
}
.notice {
  margin-left: auto;
  color: #93b4f8;
}
</style>
