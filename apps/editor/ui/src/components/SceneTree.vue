<script setup lang="ts">
import { computed, nextTick, onMounted, reactive, ref, watch } from 'vue'
import { editorState } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'
import { openContextMenu } from '../contextMenu'
import { entityMenu } from '../sceneMenus'

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
    <h2>Scene Tree</h2>
    <p v-if="entities.length === 0" class="placeholder">No entities.</p>
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
</template>

<style scoped>
.panel {
  padding: 12px;
  flex: 1;
  overflow: auto;
}
h2 {
  font-size: 0.85em;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  opacity: 0.7;
  margin: 0 0 8px;
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
