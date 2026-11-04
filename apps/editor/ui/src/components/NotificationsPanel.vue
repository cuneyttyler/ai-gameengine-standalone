<script setup lang="ts">
// Notifications (the status bar's bell): a panel half the window high at the bottom right, listing what's been
// happening, newest first; while it's closed, floating boxes for work started from a window that's now closed (a part
// being animated), each with Hide.
import { computed, onBeforeUnmount, ref } from 'vue'
import { activities, clearFinished, floats, openPanel, panelOpen } from '../activity'

const tick = ref(0) // the boxes' "just finished" times run out
const timer = window.setInterval(() => tick.value++, 1000)
onBeforeUnmount(() => window.clearInterval(timer))

const boxes = computed(() => {
  void tick.value
  return activities.filter(floats).slice(0, 3)
})
function ago(ms: number): string {
  void tick.value
  const s = Math.max(0, Math.round((Date.now() - ms) / 1000))
  return s < 60 ? `${s} s ago` : s < 3600 ? `${Math.round(s / 60)} min ago` : `${Math.round(s / 3600)} h ago`
}
const icon = (state: string) => ({ running: '◌', done: '✓', failed: '✕', info: '•' })[state] ?? '•'
</script>

<template>
  <div v-if="panelOpen" class="panel">
    <div class="head">
      <strong>Notifications</strong>
      <span class="muted small grow">{{ activities.filter((a) => a.state === 'running').length }} running</span>
      <button class="small" title="Remove what's finished" @click="clearFinished">Clear</button>
      <button class="small" title="Close" @click="openPanel(false)">✕</button>
    </div>
    <div class="list">
      <p v-if="!activities.length" class="muted pad">Nothing yet: art, characters, animations, backgrounds and the AI's work show here.</p>
      <div v-for="a in activities" :key="a.id" class="item" :class="a.state">
        <span class="icon" :class="{ spin: a.state === 'running' }">{{ icon(a.state) }}</span>
        <div class="body">
          <div class="title">{{ a.title }}</div>
          <div v-if="a.detail" class="detail">{{ a.detail }}</div>
          <div v-if="a.state === 'running' && a.total" class="bar"><div class="fill" :style="{ width: `${Math.min(100, ((a.done ?? 0) / a.total) * 100)}%` }"></div></div>
        </div>
        <span class="when">{{ ago(a.updated) }}</span>
      </div>
    </div>
  </div>
  <div v-else class="boxes">
    <div v-for="a in boxes" :key="a.id" class="box" :class="a.state">
      <span class="icon" :class="{ spin: a.state === 'running' }">{{ icon(a.state) }}</span>
      <div class="body" @click="openPanel(true)">
        <div class="title">{{ a.title }}</div>
        <div class="detail">{{ a.detail }}</div>
        <div v-if="a.state === 'running'" class="bar"><div v-if="a.total" class="fill" :style="{ width: `${Math.min(100, ((a.done ?? 0) / a.total) * 100)}%` }"></div><div v-else class="run"></div></div>
      </div>
      <button class="small" title="Hide this box (it stays in Notifications)" @click="a.hidden = true">Hide</button>
    </div>
  </div>
</template>

<style scoped>
.panel {
  position: fixed;
  right: 0;
  bottom: 24px;
  width: 380px;
  height: 50vh;
  background: var(--panel, #1e1e2a);
  border: 1px solid var(--border, #3a3a4a);
  border-right: none;
  border-bottom: none;
  display: flex;
  flex-direction: column;
  z-index: 70;
  box-shadow: -4px -4px 16px rgba(0, 0, 0, 0.35);
}
.head {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 6px 8px;
  border-bottom: 1px solid #2f2f40;
}
.grow {
  flex: 1;
}
.list {
  flex: 1;
  overflow-y: auto;
}
.item,
.box {
  display: flex;
  gap: 8px;
  align-items: flex-start;
  padding: 6px 8px;
  border-bottom: 1px solid #262633;
}
.icon {
  width: 14px;
  text-align: center;
  color: #93c5fd;
}
.done .icon {
  color: #4ade80;
}
.failed .icon {
  color: #f87171;
}
.info .icon {
  color: #9ca3af;
}
.spin {
  animation: spin 1.2s linear infinite;
  display: inline-block;
}
@keyframes spin {
  to {
    transform: rotate(360deg);
  }
}
.body {
  flex: 1;
  min-width: 0;
}
.title {
  font-size: 12px;
}
.detail {
  font-size: 11px;
  opacity: 0.65;
  overflow: hidden;
  text-overflow: ellipsis;
}
.when {
  font-size: 10px;
  opacity: 0.45;
  white-space: nowrap;
}
.bar {
  height: 3px;
  background: #2a2a3a;
  margin-top: 4px;
  overflow: hidden;
  position: relative;
}
.fill {
  height: 100%;
  background: #3b82f6;
}
.run {
  position: absolute;
  width: 30%;
  height: 100%;
  background: #3b82f6;
  animation: run 1.4s ease-in-out infinite;
}
@keyframes run {
  from {
    left: -30%;
  }
  to {
    left: 100%;
  }
}
.boxes {
  position: fixed;
  right: 8px;
  bottom: 32px;
  display: flex;
  flex-direction: column;
  gap: 6px;
  z-index: 70;
}
.box {
  width: 320px;
  background: var(--panel, #1e1e2a);
  border: 1px solid var(--border, #3a3a4a);
  box-shadow: 0 4px 14px rgba(0, 0, 0, 0.4);
  cursor: default;
}
.box .body {
  cursor: pointer;
}
.muted {
  opacity: 0.6;
}
.small {
  font-size: 11px;
}
.pad {
  padding: 10px;
}
</style>
