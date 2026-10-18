<script setup lang="ts">
// The Project panel (development_plan.md 6.8): the game's project-wide
// content: its scenes and gateways, the World Bible, and the story (dialogue,
// quests, routines). None of it belongs to one scene.
import { panelTab, type ProjectTab } from '../paneltabs'
import ScenesPanel from './ScenesPanel.vue'
import StoryPanel from './StoryPanel.vue'
import WorldPanel from './WorldPanel.vue'

const tabs: { id: ProjectTab; label: string; title: string }[] = [
  { id: 'scenes', label: 'Scenes', title: "The project's scenes, the start scene, and gateways that lead nowhere" },
  { id: 'world', label: 'World', title: "World Bible: the game's canon (characters, places, factions…)" },
  { id: 'story', label: 'Story', title: 'Dialogue, quests and daily routines' },
]
</script>

<template>
  <div class="tabbed-panel">
    <div class="header">
      <div class="tabs">
        <button
          v-for="t in tabs"
          :key="t.id"
          :class="{ active: panelTab.project === t.id }"
          :title="t.title"
          @click="panelTab.project = t.id"
        >
          {{ t.label }}
        </button>
      </div>
    </div>
    <ScenesPanel v-if="panelTab.project === 'scenes'" class="body" />
    <WorldPanel v-else-if="panelTab.project === 'world'" class="body" />
    <StoryPanel v-else class="body" />
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
  height: 32px;
  padding: 0 8px;
  border-bottom: 1px solid #333;
  flex-shrink: 0;
}
.tabs {
  display: flex;
}
.tabs button {
  border-bottom-width: 2px;
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
