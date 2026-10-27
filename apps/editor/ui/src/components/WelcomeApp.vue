<script setup lang="ts">
import logo from '../assets/aige-logo-dark.svg' // the AIGE logo (docs/brand)
// The welcome window (development_plan.md 5.4): shown when the editor starts
// without a project. Everything here asks the host (apps/editor/Launcher),
// which starts the editor on the chosen project.
import { computed, onMounted, ref } from 'vue'
import { onHostMessage, sendToHost } from '../ipc'
import DialogHost from './DialogHost.vue'
import NewProjectDialog from './NewProjectDialog.vue'

interface Recent {
  path: string
  name: string
  opened: string
  exists: boolean
}

const recent = ref<Recent[]>([])
const openLast = ref(false)
const demo = ref('')
const error = ref('')
const busy = ref(false) // a project is opening: the window closes in a moment

onHostMessage((message) => {
  if (message.type === 'welcome:info') {
    recent.value = (message.recent as Recent[]) ?? []
    openLast.value = !!message.openLast
    demo.value = (message.demo as string) ?? ''
  } else if (message.type === 'welcome:error') {
    error.value = message.text as string
    busy.value = false
  }
})

onMounted(() => sendToHost({ type: 'welcome:request' }))

const available = computed(() => recent.value.filter((r) => r.exists))
const missing = computed(() => recent.value.filter((r) => !r.exists))

function open(project: Recent): void {
  if (!project.exists || busy.value) return
  error.value = ''
  busy.value = true
  sendToHost({ type: 'welcome:open', path: project.path })
}

function send(type: string): void {
  error.value = ''
  sendToHost({ type })
}

// New Project: every choice in one window.
const showNew = ref(false)
function newProject(): void {
  error.value = ''
  showNew.value = true
}

function forget(project: Recent): void {
  sendToHost({ type: 'welcome:forget', path: project.path })
}

function setOpenLast(on: boolean): void {
  sendToHost({ type: 'prefs:openLast', on })
}

function when(iso: string): string {
  if (!iso) return ''
  const date = new Date(iso)
  return Number.isNaN(date.getTime()) ? '' : date.toLocaleString()
}
</script>

<template>
  <div class="welcome">
    <section class="start">
      <h1 class="brand"><img :src="logo" alt="AIGE" /><span class="sr">AIGE: AI Game Engine</span></h1>
      <p class="muted">Open a project to start, or make a new one.</p>
      <div class="actions">
        <button class="primary" @click="newProject">New Project…</button>
        <button @click="send('welcome:openFolder')">Open Folder…</button>
        <button :title="demo" @click="send('welcome:demo')">Open the demo</button>
      </div>
      <p v-if="error" class="error">{{ error }}</p>
      <p v-if="busy" class="muted">Opening…</p>
      <label class="check">
        <input type="checkbox" :checked="openLast" @change="setOpenLast(($event.target as HTMLInputElement).checked)" />
        Open the last project directly at launch (File ▸ Close Project brings this window back)
      </label>
    </section>
    <section class="recent">
      <div class="title">Recent projects</div>
      <p v-if="recent.length === 0" class="muted">None yet.</p>
      <div
        v-for="project in available"
        :key="project.path"
        class="item"
        role="button"
        tabindex="0"
        :title="project.path"
        @click="open(project)"
        @keydown.enter="open(project)"
      >
        <div class="name">{{ project.name }}</div>
        <div class="path">{{ project.path }}</div>
        <div v-if="project.opened" class="opened">{{ when(project.opened) }}</div>
        <button class="forget" title="Remove from the list (the folder stays)" @click.stop="forget(project)">×</button>
      </div>
      <div v-for="project in missing" :key="project.path" class="item missing" :title="project.path">
        <div class="name">{{ project.name }} <span class="badge">missing</span></div>
        <div class="path">{{ project.path }}</div>
        <button class="forget" title="Remove from the list" @click.stop="forget(project)">×</button>
      </div>
    </section>
    <DialogHost />
    <NewProjectDialog :open="showNew" flow="welcome" @close="showNew = false" />
  </div>
</template>

<style scoped>
.brand {
  margin: 0 0 6px;
}
.brand img {
  height: 64px;
  display: block;
}
.sr {
  position: absolute;
  width: 1px;
  height: 1px;
  overflow: hidden;
  clip: rect(0 0 0 0);
}
.welcome {
  display: flex;
  height: 100vh;
  background: #181825;
  color: #eee;
}
.start {
  width: 380px;
  flex-shrink: 0;
  padding: 40px 32px;
  border-right: 1px solid #333;
  display: flex;
  flex-direction: column;
  gap: 12px;
}
h1 {
  font-size: 1.4em;
  font-weight: 600;
  margin: 0;
}
.actions {
  display: flex;
  flex-direction: column;
  gap: 6px;
  margin-top: 12px;
}
button {
  height: 32px;
  padding: 0 12px;
  border: 1px solid #333;
  border-radius: 0;
  background: #11111b;
  color: #eee;
  font: inherit;
  text-align: left;
  cursor: pointer;
}
button:hover {
  background: #262637;
}
button.primary {
  border-color: #3b82f6;
}
.check {
  display: flex;
  gap: 8px;
  align-items: flex-start;
  margin-top: auto;
  font-size: 0.85em;
  opacity: 0.8;
}
.recent {
  flex: 1;
  padding: 40px 24px;
  overflow: auto;
}
.title {
  font-size: 0.85em;
  opacity: 0.7;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  margin-bottom: 10px;
}
.item {
  position: relative;
  padding: 10px 40px 10px 12px;
  border: 1px solid #2a2a3a;
  background: #15151f;
  margin-bottom: 6px;
  cursor: pointer;
}
.item:hover,
.item:focus {
  border-color: #3b82f6;
  outline: none;
}
.item.missing {
  opacity: 0.5;
  cursor: default;
}
.item.missing:hover {
  border-color: #2a2a3a;
}
.name {
  font-weight: 600;
}
.path,
.opened {
  font-size: 0.8em;
  opacity: 0.6;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.forget {
  position: absolute;
  top: 8px;
  right: 8px;
  width: 24px;
  height: 24px;
  padding: 0;
  text-align: center;
  border-color: transparent;
  background: transparent;
}
.badge {
  font-size: 0.7em;
  text-transform: uppercase;
  border: 1px solid #555;
  padding: 0 4px;
  font-weight: 400;
}
.muted {
  opacity: 0.6;
  margin: 0;
}
.error {
  color: #f59e0b;
  margin: 0;
}
</style>
