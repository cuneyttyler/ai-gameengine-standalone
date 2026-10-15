<script setup lang="ts">
import { ref } from 'vue'
import { onHostMessage, sendToHost } from '../ipc'

// Mirrors the host's play state rather than toggling locally: the host can
// also leave Play mode on its own (Esc in the viewport), and play:state is
// the single source of truth either way.
const playing = ref(false)

onHostMessage((message) => {
  if (message.type === 'play:state') {
    playing.value = message.playing as boolean
  }
})

function togglePlay(): void {
  sendToHost({ type: playing.value ? 'play:stop' : 'play:start' })
}
</script>

<template>
  <div class="toolbar" :class="{ playing }">
    <button class="play-button" :class="{ active: playing }" @click="togglePlay">
      <span class="icon" aria-hidden="true">{{ playing ? '■' : '▶' }}</span>
      {{ playing ? 'Stop' : 'Play' }}
    </button>
    <span v-if="playing" class="hint">WASD / arrows to move · Esc to stop · changes revert on stop</span>
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
}
.toolbar.playing {
  border-bottom-color: #3b82f6;
}
.play-button {
  display: flex;
  align-items: center;
  gap: 6px;
  height: 26px;
  padding: 0 12px;
  border: 1px solid #333;
  border-radius: 0;
  background: #11111b;
  color: #eee;
  font: inherit;
  font-size: 0.85em;
  cursor: pointer;
}
.play-button:hover {
  background: #262637;
}
.play-button.active {
  background: #3b82f6;
  border-color: #3b82f6;
}
.icon {
  font-size: 0.8em;
}
.hint {
  font-size: 0.8em;
  opacity: 0.6;
}
</style>
