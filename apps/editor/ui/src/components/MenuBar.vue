<script setup lang="ts">
// The menu bar (development_plan.md 4.1): File, Edit, View, Play, Help. Its
// dropdowns are context menus (contextMenu.ts) opened under the title, so
// they're cut out of the viewport like any other overlay.
import { computed, ref } from 'vue'
import { closeContextMenu, menuState, openContextMenu, separator, type MenuItem } from '../contextMenu'
import { editorState, runShortcut, setAiOpen } from '../editorState'
import { sendToHost } from '../ipc'
import { menuExtensions } from '../menus'

const open = ref<string | null>(null)

const menus = computed<Record<string, MenuItem[]>>(() => ({
  File: [
    ...menuExtensions.file.top,
    { label: 'Save', shortcut: 'Ctrl+S', disabled: editorState.playing, action: () => sendToHost({ type: 'scene:save' }) },
    ...menuExtensions.file.afterSave,
    separator,
    {
      label: 'Project Settings',
      action: () => {
        // With nothing selected the Inspector shows the project's settings.
        sendToHost({ type: 'entity:select', id: null })
        if (!editorState.panels.left) runShortcut('toggleLeft')
      },
    },
    ...menuExtensions.file.settings,
    separator,
    { label: 'Exit', action: () => sendToHost({ type: 'app:quit' }) },
  ],
  Edit: [
    { label: 'Undo', shortcut: 'Ctrl+Z', disabled: !editorState.canUndo, action: () => sendToHost({ type: 'edit:undo' }) },
    { label: 'Redo', shortcut: 'Ctrl+Y', disabled: !editorState.canRedo, action: () => sendToHost({ type: 'edit:redo' }) },
    separator,
    ...menuExtensions.edit,
    {
      label: 'Duplicate',
      shortcut: 'Ctrl+D',
      disabled: editorState.selectedId === null,
      action: () => sendToHost({ type: 'edit:duplicate' }),
    },
    {
      label: 'Delete',
      shortcut: 'Del',
      disabled: editorState.selectedId === null,
      action: () => sendToHost({ type: 'edit:delete' }),
    },
  ],
  View: [
    { label: `${editorState.panels.left ? '✓ ' : ''}Scene panel`, shortcut: 'Ctrl+B', action: () => runShortcut('toggleLeft') },
    {
      label: `${editorState.panels.bottom ? '✓ ' : ''}Bottom panel`,
      shortcut: 'Ctrl+J',
      action: () => runShortcut('toggleBottom'),
    },
    { label: `${editorState.aiOpen ? '✓ ' : ''}AI panel`, shortcut: 'Ctrl+I', action: () => setAiOpen(!editorState.aiOpen) },
    separator,
    { label: 'Frame scene', shortcut: 'F', action: () => sendToHost({ type: 'view:frame' }) },
    ...menuExtensions.view,
  ],
  Play: [
    {
      label: editorState.playing ? 'Stop' : 'Play',
      shortcut: 'F5',
      action: () => sendToHost({ type: editorState.playing ? 'play:stop' : 'play:start' }),
    },
  ],
  Help: [
    { label: 'Keyboard Shortcuts', shortcut: 'F1', action: () => runShortcut('help') },
    { label: 'Documentation', action: () => sendToHost({ type: 'help:docs' }) },
  ],
}))

function show(name: string, event: MouseEvent): void {
  const r = (event.currentTarget as HTMLElement).getBoundingClientRect()
  open.value = name
  openContextMenu(r.left, r.bottom, menus.value[name] ?? [])
}

function toggle(name: string, event: MouseEvent): void {
  if (menuState.open && open.value === name) {
    closeContextMenu()
    open.value = null
  } else {
    show(name, event)
  }
}

// Moving across the bar with a menu open switches menus, like a desktop app.
function hover(name: string, event: MouseEvent): void {
  if (menuState.open && open.value !== null && open.value !== name) show(name, event)
}
</script>

<template>
  <nav class="menubar" @mousedown.stop>
    <button
      v-for="name in Object.keys(menus)"
      :key="name"
      :class="{ active: menuState.open && open === name }"
      @click="toggle(name, $event)"
      @mouseenter="hover(name, $event)"
    >
      {{ name }}
    </button>
    <span class="title">{{ editorState.sceneName }}<span v-if="editorState.dirty"> •</span></span>
  </nav>
</template>

<style scoped>
.menubar {
  display: flex;
  align-items: stretch;
  height: 26px;
  flex-shrink: 0;
  background: #11111b;
  border-bottom: 1px solid #262636;
  font-size: 0.85em;
}
button {
  padding: 0 10px;
  border: none;
  background: none;
  color: #ddd;
  font: inherit;
  cursor: pointer;
}
button:hover,
button.active {
  background: #262637;
}
.title {
  margin-left: auto;
  padding: 0 12px;
  align-self: center;
  opacity: 0.5;
}
</style>
