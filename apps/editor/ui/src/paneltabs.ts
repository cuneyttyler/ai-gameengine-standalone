// Which tab each tabbed panel shows (development_plan.md 6.8):
// - Assets: Prefabs and Assets, the files the scene is made from, and the
//   Catalog: what the builder understood about the art (7.3);
// - Project: Scenes, World, Story and Records, the game's project-wide content;
// - Generate: Level and Art, the generators.
// Menus ask for a tab by name (File ▸ Scenes…); the panel that owns it comes
// to the front.
import { reactive } from 'vue'
import { setPanelVisible, type PanelId } from './layout'

export type AssetsTab = 'prefabs' | 'assets' | 'catalog' | 'library' | 'web'
export type ProjectTab = 'scenes' | 'world' | 'story' | 'records'
export type GenerateTab = 'level' | 'art'
export type AnyTab = AssetsTab | ProjectTab | GenerateTab

export const panelTab = reactive({
  assets: 'prefabs' as AssetsTab,
  project: 'scenes' as ProjectTab,
  generate: 'level' as GenerateTab,
})

const owner: Record<AnyTab, 'assets' | 'project' | 'generate'> = {
  prefabs: 'assets',
  assets: 'assets',
  catalog: 'assets',
  library: 'assets',
  web: 'assets',
  scenes: 'project',
  world: 'project',
  story: 'project',
  records: 'project',
  level: 'generate',
  art: 'generate',
}

export function showTab(tab: AnyTab): void {
  const panel = owner[tab]
  ;(panelTab as Record<string, string>)[panel] = tab
  setPanelVisible(panel as PanelId, true)
}
