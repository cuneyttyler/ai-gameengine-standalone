<script setup lang="ts">
import { ref } from 'vue'
import { onHostMessage, sendToHost } from '../ipc'

interface InspectorData {
  id: number
  components: Record<string, Record<string, number | string>>
}

const current = ref<InspectorData | null>(null)

onHostMessage((message) => {
  if (message.type === 'inspector:data') {
    current.value = message as unknown as InspectorData
  } else if (message.type === 'inspector:clear') {
    current.value = null
  }
})

function setField(component: string, field: string, rawValue: string): void {
  if (!current.value) {
    return
  }
  const value = Number(rawValue)
  if (Number.isNaN(value)) {
    return
  }
  sendToHost({ type: 'inspector:setField', id: current.value.id, component, field, value })
}
</script>

<template>
  <div class="panel">
    <h2>Inspector</h2>
    <p v-if="!current" class="placeholder">No entity selected.</p>
    <div v-else>
      <div v-for="(fields, componentName) in current.components" :key="componentName" class="component">
        <h3>{{ componentName }}</h3>
        <div v-for="(value, field) in fields" :key="field" class="field-row">
          <label>{{ field }}</label>
          <input
            v-if="typeof value === 'number'"
            type="number"
            step="0.05"
            :value="value"
            @change="setField(componentName, String(field), ($event.target as HTMLInputElement).value)"
          />
          <span v-else class="readonly-value">{{ value }}</span>
        </div>
      </div>
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
h3 {
  font-size: 0.8em;
  opacity: 0.8;
  margin: 12px 0 4px;
}
.field-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 8px;
  margin-bottom: 4px;
  font-size: 0.85em;
}
.field-row label {
  opacity: 0.7;
}
.field-row input {
  width: 90px;
  background: #11111b;
  border: 1px solid #333;
  color: #eee;
  padding: 2px 4px;
}
.readonly-value {
  opacity: 0.6;
  font-size: 0.85em;
  word-break: break-all;
  text-align: right;
}
.placeholder {
  opacity: 0.5;
  font-size: 0.9em;
}
</style>
