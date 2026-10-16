// Menu items contributed by features outside the menu bar itself (projects,
// clipboard, grid, export), so MenuBar.vue doesn't need to know them all.
import { reactive } from 'vue'
import type { MenuItem } from './contextMenu'

export const menuExtensions = reactive({
  file: { top: [] as MenuItem[], afterSave: [] as MenuItem[], settings: [] as MenuItem[] },
  edit: [] as MenuItem[],
  view: [] as MenuItem[],
})
