// Menu items contributed by features outside the menu bar itself (projects,
// clipboard, grid, export), so MenuBar.vue doesn't need to know them all.
// Each feature owns its own slot and only ever replaces that one: a feature
// that rewrote a shared list while watching it re-ran itself forever (the
// blank-window bug, 2026-10-16).
import { reactive } from 'vue'
import type { MenuItem } from './contextMenu'

export const menuExtensions = reactive({
  file: {
    top: [] as MenuItem[], // projects.ts: New / Open / Recent
    scenes: [] as MenuItem[], // scenes.ts: New Scene…, Scenes…
    afterSave: [] as MenuItem[], // projects.ts: Save As, Close Project
    export: [] as MenuItem[], // exportGame.ts: Export Game…
    settings: [] as MenuItem[], // projects.ts: Preferences
  },
  edit: [] as MenuItem[],
  view: [] as MenuItem[],
})
