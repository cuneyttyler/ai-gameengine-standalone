<script setup lang="ts">
// The Generate panel (development_plan.md 6.8): the generators, a dungeon
// (or village) from parameters, and art from a description.
import { panelTab, type GenerateTab } from '../paneltabs'
import AnimatePanel from './AnimatePanel.vue'
import ArtPanel from './ArtPanel.vue'
import CharacterPanel from './CharacterPanel.vue'
import LevelPanel from './LevelPanel.vue'

const tabs: { id: GenerateTab; label: string; title: string }[] = [
  { id: 'level', label: 'Level', title: 'Generate a dungeon or a village into the scene' },
  { id: 'art', label: 'Art', title: 'Generate sprites and tiles from a description' },
  { id: 'character', label: 'Character', title: 'Generate a character seen from 4 or 8 directions (PixelLab)' },
  { id: 'animate', label: 'Animate', title: 'Animate an image of the project: walking, attacking… (PixelLab)' },
]
</script>

<template>
  <div class="tabbed-panel">
    <div class="header">
      <div class="tabs">
        <button
          v-for="t in tabs"
          :key="t.id"
          :class="{ active: panelTab.generate === t.id }"
          :title="t.title"
          @click="panelTab.generate = t.id"
        >
          {{ t.label }}
        </button>
      </div>
    </div>
    <LevelPanel v-if="panelTab.generate === 'level'" class="body" />
    <ArtPanel v-else-if="panelTab.generate === 'art'" class="body" />
    <CharacterPanel v-else-if="panelTab.generate === 'character'" class="body" />
    <AnimatePanel v-else class="body" />
  </div>
</template>

<style scoped>
.tabbed-panel {
  display: flex;
  flex-direction: column;
  height: 100%;
  background: #181825;
  font-size: 0.85em;
}
.header {
  display: flex;
  align-items: center;
  gap: 6px;
  height: 32px;
  padding: 0 8px;
  border-bottom: 1px solid #333;
  flex-shrink: 0;
}
.tabs {
  display: flex;
  margin-right: 8px;
}
/* The same tab buttons as the Assets panel's (AssetBrowser). */
.tabs button {
  height: 24px;
  padding: 0 8px;
  border: 1px solid #333;
  border-bottom-width: 2px;
  border-radius: 0;
  background: #11111b;
  color: #eee;
  font: inherit;
  cursor: pointer;
  white-space: nowrap;
}
.tabs button:hover {
  background: #262637;
}
.tabs button.active {
  border-bottom-color: #3b82f6;
  background: #262637;
}
.body {
  flex: 1;
  display: flex;
  min-height: 0;
}
</style>
