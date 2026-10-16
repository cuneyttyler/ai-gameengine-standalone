// Prefab-card menu items contributed by features (remove, rename, move).
import type { MenuItem } from './contextMenu'
import type { Prefab } from './editorState'

export const prefabMenuExtensions: ((prefab: Prefab) => MenuItem[])[] = []
