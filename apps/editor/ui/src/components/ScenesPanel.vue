<script setup lang="ts">
// Scene Manager (development_plan.md 5.3): the project's scenes, which one is
// open and which one the game starts in, and gateways that lead nowhere.
import { onMounted, ref } from 'vue'
import { openContextMenu, separator, type MenuItem } from '../contextMenu'
import { confirmDialog, promptDialog } from '../dialogs'
import { editorState } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'
import { newScene } from '../scenes'

interface SceneEntry {
  path: string
  name: string
  start: boolean
  open: boolean
  markers: string[]
}

const scenes = ref<SceneEntry[]>([])
const problems = ref<string[]>([])
const selected = ref<string | null>(null)

onHostMessage((message) => {
  if (message.type === 'scenes:list') {
    scenes.value = message.scenes as SceneEntry[]
    problems.value = (message.problems as string[]) ?? []
  } else if (message.type === 'scene:changed' || message.type === 'file:moved') {
    sendToHost({ type: 'scenes:request' })
  }
})

onMounted(() => sendToHost({ type: 'scenes:request' }))

async function rename(scene: SceneEntry): Promise<void> {
  const name = await promptDialog({
    title: 'Rename scene',
    message: `New name for ${scene.path}. Gateways that lead to it, and the start scene setting, follow.`,
    value: scene.name,
    confirmLabel: 'Rename',
  })
  if (name?.trim() && name.trim() !== scene.name) sendToHost({ type: 'scene:rename', path: scene.path, name: name.trim() })
}

async function duplicate(scene: SceneEntry): Promise<void> {
  const name = await promptDialog({
    title: 'Duplicate scene',
    message: `A name for the copy of ${scene.path}.`,
    value: `${scene.name}-copy`,
    confirmLabel: 'Duplicate',
  })
  if (name?.trim()) sendToHost({ type: 'scene:duplicate', path: scene.path, name: name.trim() })
}

async function remove(scene: SceneEntry): Promise<void> {
  if (
    await confirmDialog({
      title: 'Delete scene',
      message: `Delete ${scene.path}? This deletes the file.`,
      confirmLabel: 'Delete',
      danger: true,
    })
  ) {
    sendToHost({ type: 'scene:delete', path: scene.path })
  }
}

function open(scene: SceneEntry): void {
  if (!scene.open) sendToHost({ type: 'scene:open', path: scene.path })
}

function menu(scene: SceneEntry, event: MouseEvent): void {
  selected.value = scene.path
  const items: MenuItem[] = [
    { label: 'Open', disabled: scene.open || editorState.playing, action: () => open(scene) },
    { label: 'Set as start scene', disabled: scene.start, action: () => sendToHost({ type: 'scene:setStart', path: scene.path }) },
    separator,
    { label: 'Rename…', disabled: editorState.playing, action: () => rename(scene) },
    { label: 'Duplicate…', action: () => duplicate(scene) },
    separator,
    { label: 'Delete…', danger: true, disabled: scene.start || scene.open, action: () => remove(scene) },
  ]
  openContextMenu(event.clientX, event.clientY, items)
}
</script>

<template>
  <div class="scenes">
    <div class="list">
      <div class="toolbar">
        <button :disabled="editorState.playing" @click="newScene">New scene…</button>
        <span class="muted">Double-click to open · right-click for more</span>
      </div>
      <button
        v-for="scene in scenes"
        :key="scene.path"
        class="row"
        :class="{ open: scene.open, selected: selected === scene.path }"
        :title="scene.path"
        @click="selected = scene.path"
        @dblclick="open(scene)"
        @contextmenu.prevent="menu(scene, $event)"
      >
        <span class="name">{{ scene.name }}</span>
        <span v-if="scene.start" class="badge start" title="The game starts here">start</span>
        <span v-if="scene.open" class="badge" title="Open in the editor">open</span>
        <span class="markers" :title="scene.markers.join(', ')">
          {{ scene.markers.length }} {{ scene.markers.length === 1 ? 'marker' : 'markers' }}
        </span>
      </button>
    </div>
    <aside class="problems">
      <div class="title">Gateways</div>
      <p v-if="problems.length === 0" class="muted">
        Every gateway leads somewhere. Place the Gateway prefab, then set its scene and marker in the Inspector.
      </p>
      <p v-for="problem in problems" :key="problem" class="problem">⚠ {{ problem }}</p>
    </aside>
  </div>
</template>

<style scoped>
.scenes {
  display: flex;
  height: 100%;
  min-height: 0;
}
.list {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: auto;
  padding: 6px 8px;
  gap: 2px;
}
.toolbar {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 4px;
}
button {
  height: 24px;
  padding: 0 8px;
  border: 1px solid #333;
  border-radius: 0;
  background: #11111b;
  color: #eee;
  font: inherit;
  cursor: pointer;
}
button:hover:not(:disabled) {
  background: #262637;
}
button:disabled {
  opacity: 0.4;
  cursor: default;
}
.row {
  display: flex;
  align-items: center;
  gap: 8px;
  text-align: left;
  height: 28px;
  flex-shrink: 0;
}
.row.selected {
  border-color: #3b82f6;
}
.row.open .name {
  font-weight: 600;
}
.name {
  flex: 1;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.badge {
  font-size: 0.75em;
  text-transform: uppercase;
  padding: 1px 6px;
  border: 1px solid #444;
  opacity: 0.8;
}
.badge.start {
  border-color: #3b82f6;
  color: #93c5fd;
}
.markers {
  font-size: 0.8em;
  opacity: 0.55;
  width: 80px;
  text-align: right;
}
.problems {
  width: 320px;
  flex-shrink: 0;
  border-left: 1px solid #333;
  padding: 8px;
  overflow: auto;
}
.title {
  font-size: 0.85em;
  opacity: 0.7;
  text-transform: uppercase;
  letter-spacing: 0.04em;
  margin-bottom: 6px;
}
.problem {
  color: #f59e0b;
  margin: 0 0 6px;
}
.muted {
  opacity: 0.55;
  font-size: 0.85em;
}
</style>
