<script setup lang="ts">
// The Scene tree: the project's scenes, the open one expanded into its
// entities (development_plan.md 5.3). Clicking another scene opens it, in the
// same project and window; the + button makes a new one.
import { computed, nextTick, onMounted, reactive, ref, watch } from 'vue'
import { editorState } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'
import { openContextMenu } from '../contextMenu'
import { entityMenu } from '../sceneMenus'
import { newScene, openScene, sceneList, sceneMenu } from '../scenes'

// Scenes before the open one, the open one, and those after, so the open
// scene's entities sit right under its row.
const openIndex = computed(() => sceneList.scenes.findIndex((s) => s.open))
const before = computed(() => (openIndex.value < 0 ? sceneList.scenes : sceneList.scenes.slice(0, openIndex.value)))
const current = computed(() => (openIndex.value < 0 ? null : sceneList.scenes[openIndex.value]!))
const after = computed(() => (openIndex.value < 0 ? [] : sceneList.scenes.slice(openIndex.value + 1)))
const openCollapsed = ref(false)

function showMenu(entity: EntitySummary, event: MouseEvent): void {
  select(entity)
  openContextMenu(event.clientX, event.clientY,
                  entityMenu({ id: entity.id, name: entity.label, prefab: entity.prefab, tile: entity.kind === 'Tile' }))
}

interface EntitySummary {
  id: number
  kind: string
  label: string
  prefab: string | null
}

const entities = ref<EntitySummary[]>([])

// Tiles vastly outnumber everything else, so they get their own group,
// collapsed by default.
const groups = computed(() => [
  { name: 'Characters', items: entities.value.filter((e) => ['Player', 'Enemy', 'NPC', 'Scripted NPC'].includes(e.kind)) },
  { name: 'Props', items: entities.value.filter((e) => ['Prop', 'Pickup', 'Decoration'].includes(e.kind)) },
  { name: 'Tiles', items: entities.value.filter((e) => e.kind === 'Tile') },
])
const collapsed = reactive<Record<string, boolean>>({ Tiles: true })

onHostMessage((message) => {
  if (message.type === 'scene:update') {
    entities.value = message.entities as EntitySummary[]
  }
})

function select(entity: EntitySummary): void {
  sendToHost({ type: 'entity:select', id: entity.id })
}

// Selecting in the viewport should reveal the entity here too.
const listEl = ref<HTMLElement | null>(null)
watch(
  () => editorState.selectedId,
  async (id) => {
    if (id === null) {
      return
    }
    const group = groups.value.find((g) => g.items.some((e) => e.id === id))
    if (group && collapsed[group.name]) {
      collapsed[group.name] = false
    }
    await nextTick()
    listEl.value?.querySelector(`[data-id="${id}"]`)?.scrollIntoView({ block: 'nearest' })
  },
)

onMounted(() => {
  sendToHost({ type: 'scene:request' })
})
</script>

<template>
  <div class="panel">
    <div class="head">
      <h2>Scenes</h2>
      <button class="add" :disabled="editorState.playing" title="New scene in this project" @click="newScene">+</button>
    </div>
    <button
      v-for="scene in before"
      :key="scene.path"
      class="scene-row"
      :title="`${scene.path} — click to open`"
      :disabled="editorState.playing"
      @click="openScene(scene)"
      @contextmenu.prevent="sceneMenu(scene, $event)"
    >
      <span class="chevron">▸</span><span class="scene-name">{{ scene.name }}</span>
      <span v-if="scene.start" class="star" title="The game starts here">★</span>
    </button>
    <button
      v-if="current"
      class="scene-row open"
      :title="`${current.path} — open`"
      @click="openCollapsed = !openCollapsed"
      @contextmenu.prevent="sceneMenu(current, $event)"
    >
      <span class="chevron">{{ openCollapsed ? '▸' : '▾' }}</span><span class="scene-name">{{ current.name }}</span>
      <span v-if="current.start" class="star" title="The game starts here">★</span>
      <span class="count">{{ entities.length }}</span>
    </button>
    <div v-if="!openCollapsed" class="open-scene">
    <p v-if="entities.length === 0" class="placeholder">
      Empty scene. Paint its ground with a tile prefab from Assets ▸ Prefabs, place things, and connect it to other
      scenes with a Marker and a Gateway.
    </p>
    <div v-else ref="listEl">
      <template v-for="group in groups" :key="group.name">
        <div v-if="group.items.length > 0" class="group">
          <button class="group-header" @click="collapsed[group.name] = !collapsed[group.name]">
            <span class="chevron">{{ collapsed[group.name] ? '▸' : '▾' }}</span>
            {{ group.name }}
            <span class="count">{{ group.items.length }}</span>
          </button>
          <ul v-if="!collapsed[group.name]" class="entity-list">
            <li
              v-for="entity in group.items"
              :key="entity.id"
              :data-id="entity.id"
              :class="{ selected: entity.id === editorState.selectedId }"
              @click="select(entity)"
              @contextmenu.prevent="showMenu(entity, $event)"
            >
              {{ entity.label }}
            </li>
          </ul>
        </div>
      </template>
    </div>
    </div>
    <button
      v-for="scene in after"
      :key="scene.path"
      class="scene-row"
      :title="`${scene.path} — click to open`"
      :disabled="editorState.playing"
      @click="openScene(scene)"
      @contextmenu.prevent="sceneMenu(scene, $event)"
    >
      <span class="chevron">▸</span><span class="scene-name">{{ scene.name }}</span>
      <span v-if="scene.start" class="star" title="The game starts here">★</span>
    </button>
  </div>
</template>

<style scoped>
.panel {
  padding: 12px;
  flex: 1;
  overflow: auto;
}
.head {
  display: flex;
  align-items: center;
  margin: 0 0 6px;
}
h2 {
  font-size: 0.85em;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  opacity: 0.7;
  margin: 0;
  flex: 1;
}
.add {
  width: 22px;
  height: 20px;
  padding: 0;
  border: 1px solid #333;
  border-radius: 0;
  background: #11111b;
  color: #ddd;
  font: inherit;
  cursor: pointer;
}
.add:hover:not(:disabled) {
  background: #262637;
}
.scene-row {
  display: flex;
  align-items: center;
  gap: 6px;
  width: 100%;
  padding: 4px 4px;
  border: 0;
  border-radius: 0;
  background: none;
  color: #ccc;
  font: inherit;
  font-size: 0.85em;
  cursor: pointer;
  text-align: left;
}
.scene-row:hover:not(:disabled) {
  background: #262637;
}
.scene-row:disabled {
  opacity: 0.5;
  cursor: default;
}
.scene-row.open {
  color: #fff;
  font-weight: 600;
}
.scene-name {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.star {
  color: #93c5fd;
  font-size: 0.85em;
}
.open-scene {
  padding-left: 10px;
  border-left: 1px solid #2a2a3a;
  margin: 0 0 4px 8px;
}
.placeholder {
  opacity: 0.5;
  font-size: 0.9em;
}
.group-header {
  display: flex;
  align-items: center;
  gap: 6px;
  width: 100%;
  padding: 4px 4px;
  border: 0;
  background: none;
  color: #eee;
  font: inherit;
  font-size: 0.8em;
  opacity: 0.8;
  cursor: pointer;
  text-align: left;
}
.group-header:hover {
  background: #262637;
}
.chevron {
  width: 10px;
}
.count {
  margin-left: auto;
  opacity: 0.5;
}
.entity-list {
  list-style: none;
  margin: 0 0 6px;
  padding: 0;
}
.entity-list li {
  padding: 3px 8px 3px 20px;
  cursor: pointer;
  font-size: 0.85em;
}
.entity-list li:hover {
  background: #262637;
}
.entity-list li.selected {
  background: #313148;
  box-shadow: inset 2px 0 0 #3b82f6;
}
</style>
