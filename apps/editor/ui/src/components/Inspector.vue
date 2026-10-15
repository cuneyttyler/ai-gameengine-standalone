<script setup lang="ts">
import { ref } from 'vue'
import { editorState } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'

type FieldType = 'float' | 'int' | 'bool' | 'string' | 'enum' | 'entity' | 'texture' | 'script'

interface Field {
  name: string
  type: FieldType
  value: number | boolean | string | null
  options?: string[]
}

interface ComponentData {
  name: string
  removable: boolean
  derived: boolean
  fields: Field[]
}

interface InspectorData {
  id: number
  label: string
  components: ComponentData[]
  addable: string[]
  choices: {
    entities: { id: number; label: string }[]
    textures: string[]
    scripts: string[]
  }
}

const current = ref<InspectorData | null>(null)
const collapsed = ref<Record<string, boolean>>({})
const newScriptName = ref('')
const creatingScript = ref(false)

onHostMessage((message) => {
  if (message.type === 'inspector:data') {
    const data = message as unknown as InspectorData
    // While playing the host refreshes this 10x per second; don't clobber a
    // field the user is in the middle of typing into.
    const focused = document.activeElement as HTMLElement | null
    if (focused?.closest('.inspector') && current.value?.id === data.id && editorState.playing) {
      return
    }
    current.value = data
  } else if (message.type === 'inspector:clear') {
    current.value = null
  }
})

function setField(component: string, field: Field, value: unknown): void {
  if (!current.value) {
    return
  }
  if ((field.type === 'float' || field.type === 'int') && (typeof value !== 'number' || Number.isNaN(value))) {
    return
  }
  sendToHost({ type: 'inspector:setField', id: current.value.id, component, field: field.name, value })
}

function numberFrom(event: Event): number {
  return Number((event.target as HTMLInputElement).value)
}

function addComponent(event: Event): void {
  const select = event.target as HTMLSelectElement
  if (current.value && select.value) {
    sendToHost({ type: 'component:add', id: current.value.id, component: select.value })
  }
  select.value = ''
}

function removeComponent(name: string): void {
  if (current.value) {
    sendToHost({ type: 'component:remove', id: current.value.id, component: name })
  }
}

function createScript(): void {
  if (current.value && newScriptName.value.trim()) {
    sendToHost({ type: 'script:create', id: current.value.id, name: newScriptName.value.trim() })
  }
  newScriptName.value = ''
  creatingScript.value = false
}

function openScript(path: string): void {
  sendToHost({ type: 'script:open', path })
}

function fileName(path: string): string {
  return path.split('/').pop() ?? path
}
</script>

<template>
  <div class="panel inspector">
    <h2>Inspector</h2>
    <p v-if="!current" class="placeholder">No entity selected.</p>
    <template v-else>
      <div class="entity-label">{{ current.label }}</div>

      <section v-for="component in current.components" :key="component.name" class="component">
        <header>
          <button class="collapse" @click="collapsed[component.name] = !collapsed[component.name]">
            {{ collapsed[component.name] ? '▸' : '▾' }} {{ component.name }}
          </button>
          <span v-if="component.derived" class="derived" title="Computed every frame from the world position">
            derived
          </span>
          <button
            v-if="component.removable && !editorState.playing"
            class="remove"
            :title="`Remove ${component.name}`"
            @click="removeComponent(component.name)"
          >
            ×
          </button>
        </header>

        <div v-if="!collapsed[component.name]" class="fields">
          <div v-for="field in component.fields" :key="field.name" class="field-row">
            <label>{{ field.name }}</label>

            <input
              v-if="field.type === 'float' || field.type === 'int'"
              type="number"
              :step="field.type === 'int' ? 1 : 0.05"
              :value="field.value as number"
              :disabled="component.derived"
              @change="setField(component.name, field, numberFrom($event))"
            />

            <input
              v-else-if="field.type === 'bool'"
              type="checkbox"
              class="checkbox"
              :checked="field.value as boolean"
              @change="setField(component.name, field, ($event.target as HTMLInputElement).checked)"
            />

            <input
              v-else-if="field.type === 'string'"
              type="text"
              spellcheck="false"
              :value="field.value as string"
              @change="setField(component.name, field, ($event.target as HTMLInputElement).value)"
            />

            <select
              v-else-if="field.type === 'enum'"
              :value="field.value as number"
              @change="setField(component.name, field, Number(($event.target as HTMLSelectElement).value))"
            >
              <option v-for="(option, index) in field.options" :key="option" :value="index">{{ option }}</option>
            </select>

            <select
              v-else-if="field.type === 'entity'"
              :value="field.value ?? ''"
              @change="
                setField(
                  component.name,
                  field,
                  ($event.target as HTMLSelectElement).value === ''
                    ? null
                    : Number(($event.target as HTMLSelectElement).value),
                )
              "
            >
              <option value="">(none)</option>
              <option v-for="entity in current.choices.entities" :key="entity.id" :value="entity.id">
                {{ entity.label }} #{{ entity.id }}
              </option>
            </select>

            <select
              v-else-if="field.type === 'texture'"
              :value="field.value as string"
              :title="field.value as string"
              @change="setField(component.name, field, ($event.target as HTMLSelectElement).value)"
            >
              <option value="">(built-in)</option>
              <option v-for="texture in current.choices.textures" :key="texture" :value="texture">
                {{ fileName(texture) }}
              </option>
            </select>

            <span v-else-if="field.type === 'script'" class="script-field">
              <select
                :value="field.value as string"
                :title="field.value as string"
                @change="setField(component.name, field, ($event.target as HTMLSelectElement).value)"
              >
                <option value="">(none)</option>
                <option v-for="script in current.choices.scripts" :key="script" :value="script">
                  {{ fileName(script) }}
                </option>
              </select>
              <button v-if="field.value" title="Open in your editor" @click="openScript(field.value as string)">
                Open
              </button>
            </span>
          </div>
        </div>
      </section>

      <div v-if="!editorState.playing" class="footer">
        <select v-if="current.addable.length > 0" class="add" @change="addComponent">
          <option value="">+ Add component</option>
          <option v-for="name in current.addable" :key="name" :value="name">{{ name }}</option>
        </select>

        <div v-if="creatingScript" class="new-script">
          <input
            v-model="newScriptName"
            type="text"
            placeholder="script name"
            spellcheck="false"
            @keydown.enter="createScript"
            @keydown.esc="creatingScript = false"
          />
          <button @click="createScript">Create</button>
        </div>
        <button v-else class="link" @click="creatingScript = true">+ New script…</button>
      </div>
    </template>
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
.entity-label {
  font-weight: 600;
  margin-bottom: 8px;
}
.component {
  border-top: 1px solid #2a2a3a;
  padding: 4px 0 6px;
}
header {
  display: flex;
  align-items: center;
  gap: 6px;
}
.collapse {
  flex: 1;
  border: 0;
  background: none;
  color: #eee;
  font: inherit;
  font-size: 0.8em;
  font-weight: 600;
  text-align: left;
  padding: 4px 0;
  cursor: pointer;
  opacity: 0.85;
}
.derived {
  font-size: 0.7em;
  opacity: 0.5;
  text-transform: uppercase;
}
.remove {
  width: 20px;
  height: 20px;
  padding: 0;
  border: 1px solid transparent;
  border-radius: 0;
  background: none;
  color: #eee;
  opacity: 0.5;
  cursor: pointer;
}
.remove:hover {
  opacity: 1;
  border-color: #ef4444;
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
  flex-shrink: 0;
}
input,
select,
button {
  height: 22px;
  border: 1px solid #333;
  border-radius: 0;
  background: #11111b;
  color: #eee;
  font: inherit;
}
.field-row input[type='number'],
.field-row input[type='text'] {
  width: 120px;
  padding: 0 4px;
}
.field-row select {
  width: 128px;
  min-width: 0;
}
.field-row input:disabled {
  opacity: 0.5;
}
.checkbox {
  width: 14px;
  height: 14px;
}
.script-field {
  display: flex;
  gap: 2px;
}
.script-field select {
  width: 84px;
}
.script-field button {
  padding: 0 6px;
  cursor: pointer;
}
.footer {
  display: flex;
  flex-direction: column;
  gap: 6px;
  border-top: 1px solid #2a2a3a;
  padding-top: 8px;
}
.add {
  width: 100%;
}
.new-script {
  display: flex;
  gap: 4px;
}
.new-script input {
  flex: 1;
  padding: 0 4px;
}
.new-script button,
.link {
  padding: 0 8px;
  cursor: pointer;
}
.link {
  align-self: flex-start;
}
button:hover {
  background: #262637;
}
</style>
