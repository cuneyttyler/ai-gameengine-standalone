<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { onHostMessage, sendToHost } from '../ipc'

interface EntitySummary {
  id: number
  kind: string
}

const entities = ref<EntitySummary[]>([])
const selectedId = ref<number | null>(null)

onHostMessage((message) => {
  if (message.type === 'scene:update') {
    entities.value = message.entities as EntitySummary[]
    if (!entities.value.some((e) => e.id === selectedId.value)) {
      selectedId.value = null
    }
  }
})

function select(entity: EntitySummary): void {
  selectedId.value = entity.id
  sendToHost({ type: 'entity:select', id: entity.id })
}

onMounted(() => {
  sendToHost({ type: 'scene:request' })
})
</script>

<template>
  <div class="panel">
    <h2>Scene Tree</h2>
    <p v-if="entities.length === 0" class="placeholder">No entities.</p>
    <ul v-else class="entity-list">
      <li
        v-for="entity in entities"
        :key="entity.id"
        :class="{ selected: entity.id === selectedId }"
        @click="select(entity)"
      >
        {{ entity.kind }} <span class="id">#{{ entity.id }}</span>
      </li>
    </ul>
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
.entity-list {
  list-style: none;
  margin: 0;
  padding: 0;
}
.entity-list li {
  padding: 4px 8px;
  cursor: pointer;
  font-size: 0.9em;
}
.entity-list li:hover {
  background: #262637;
}
.entity-list li.selected {
  background: #313148;
}
.id {
  opacity: 0.5;
  font-size: 0.85em;
}
</style>
