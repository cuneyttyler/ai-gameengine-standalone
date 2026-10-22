<script setup lang="ts">
// The Project panel (development_plan.md 6.8): the game's project-wide
// content: its scenes and gateways, the World Bible, and the story (dialogue,
// quests, routines). None of it belongs to one scene.
import { panelTab, type ProjectTab } from '../paneltabs'
import ScenesPanel from './ScenesPanel.vue'
import RecordsPanel from './RecordsPanel.vue'
import DifficultyPanel from './DifficultyPanel.vue'
import UiPanel from './UiPanel.vue'
import FxPanel from './FxPanel.vue'
import MagicPanel from './MagicPanel.vue'
import StoryGraphPanel from './StoryGraphPanel.vue'
import StoryPanel from './StoryPanel.vue'
import WorldPanel from './WorldPanel.vue'

const tabs: { id: ProjectTab; label: string; title: string }[] = [
  { id: 'scenes', label: 'Scenes', title: "The project's scenes, the start scene, and gateways that lead nowhere" },
  { id: 'world', label: 'World', title: "World Bible: the game's canon (characters, places, factions…)" },
  { id: 'story', label: 'Story', title: 'Dialogue, quests and daily routines' },
  { id: 'records', label: 'Records', title: "Typed game data: items and the project's other record types" },
  { id: 'difficulty', label: 'Difficulty', title: 'Difficulty levels, the category table, previews and the curve' },
  { id: 'ui', label: 'UI', title: "The game's screens, theme and notifications (documents in ui/)" },
  { id: 'fx', label: 'Effects', title: 'Animation sheets, particles and effects, cutscenes' },
  { id: 'magic', label: 'Magic', title: 'Abilities and magic effects, with their cost and output' },
  { id: 'storygraph', label: 'Story graph', title: 'The story knowledge graph: facts over time, dependents and the story rules' },
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
    <RecordsPanel v-else-if="panelTab.project === 'records'" class="body" />
    <DifficultyPanel v-else-if="panelTab.project === 'difficulty'" class="body" />
    <UiPanel v-else-if="panelTab.project === 'ui'" class="body" />
    <FxPanel v-else-if="panelTab.project === 'fx'" class="body" />
    <MagicPanel v-else-if="panelTab.project === 'magic'" class="body" />
    <StoryGraphPanel v-else-if="panelTab.project === 'storygraph'" class="body" />
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
