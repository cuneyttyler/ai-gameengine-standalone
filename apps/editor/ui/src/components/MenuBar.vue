<script setup lang="ts">
import { projectState } from '../projects'
// The menu bar (development_plan.md 4.1): File, Edit, View, Play, Help. Its
// dropdowns are context menus (contextMenu.ts) opened under the title, so
// they're cut out of the viewport like any other overlay.
import { computed, ref } from 'vue'
import { closeContextMenu, menuState, openContextMenu, separator, type MenuItem } from '../contextMenu'
import { editorState } from '../editorState'
import { sendToHost } from '../ipc'
import { menuExtensions } from '../menus'
import { keyLabel, runAction } from '../keys'
import { dockNames, layout, movePanel, panelIds, panelNames, togglePanel, toggleViewport, type Dock } from '../layout'

// View ▸ <panel> ▸ Show / Left / Bottom / Right (5.6).
function panelItems(): MenuItem[] {
  return panelIds.map((id) => ({
    label: `${layout.visible[id] ? '✓ ' : ''}${panelNames[id]} panel`,
    submenu: [
      { label: layout.visible[id] ? 'Hide' : 'Show', shortcut: keyLabel(`panel.${id}`), action: () => togglePanel(id) },
      separator,
      ...(['left', 'bottom', 'right'] as Dock[]).map((dock) => ({
        label: `${layout.dock[id] === dock ? '✓ ' : ''}Dock ${dockNames[dock].toLowerCase()}`,
        action: () => movePanel(id, dock),
      })),
    ],
  }))
}

const open = ref<string | null>(null)

const menus = computed<Record<string, MenuItem[]>>(() => ({
  File: [
    ...menuExtensions.file.top,
    ...menuExtensions.file.scenes,
    { label: 'Save', shortcut: keyLabel('file.save'), disabled: editorState.playing, action: () => sendToHost({ type: 'scene:save' }) },
    ...menuExtensions.file.afterSave,
    ...menuExtensions.file.export,
    separator,
    { label: 'Project Settings…', action: () => (projectState.showProjectSettings = true) },
    ...menuExtensions.file.settings,
    separator,
    { label: 'Exit', action: () => sendToHost({ type: 'app:quit' }) },
  ],
  Edit: [
    { label: 'Undo', shortcut: keyLabel('edit.undo'), disabled: !editorState.canUndo, action: () => sendToHost({ type: 'edit:undo' }) },
    { label: 'Redo', shortcut: keyLabel('edit.redo'), disabled: !editorState.canRedo, action: () => sendToHost({ type: 'edit:redo' }) },
    separator,
    ...menuExtensions.edit,
    {
      label: 'Duplicate',
      shortcut: keyLabel('edit.duplicate'),
      disabled: editorState.selectedId === null,
      action: () => sendToHost({ type: 'edit:duplicate' }),
    },
    {
      label: 'Delete',
      shortcut: keyLabel('edit.delete'),
      disabled: editorState.selectedId === null,
      action: () => sendToHost({ type: 'edit:delete' }),
    },
  ],
  View: [
    ...panelItems(),
    { label: `${layout.viewport ? '✓ ' : ''}Viewport`, shortcut: keyLabel('panel.viewport'), action: () => toggleViewport() },
    separator,
    { label: 'Frame scene', shortcut: keyLabel('view.frame'), action: () => sendToHost({ type: 'view:frame' }) },
    ...menuExtensions.view,
  ],
  Play: [
    {
      label: editorState.playing ? 'Stop' : 'Play',
      shortcut: keyLabel('play.toggle'),
      action: () => sendToHost({ type: editorState.playing ? 'play:stop' : 'play:start' }),
    },
  ],
  Help: [
    { label: 'Keyboard Shortcuts…', shortcut: keyLabel('help.shortcuts'), action: () => runAction('help.shortcuts') },
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
