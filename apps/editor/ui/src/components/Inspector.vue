<script setup lang="ts">
import { ref } from 'vue'
import { editorState } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'

type FieldType = 'float' | 'int' | 'bool' | 'string' | 'enum' | 'entity' | 'texture' | 'script' | 'audio'

interface Field {
  name: string
  type: FieldType
  value: number | boolean | string | null
  options?: string[]
  overridden: boolean // changed on this prefab copy
}

interface ScriptProperty {
  name: string
  type: 'float' | 'bool' | 'string'
  value: number | boolean | string
  set: boolean // this entity has its own value (otherwise the script's default)
  overridden: boolean
}

interface ComponentData {
  name: string
  removable: boolean
  derived: boolean
  fields: Field[]
  script?: { properties: ScriptProperty[]; error: string }
}

interface InspectorData {
  id: number
  label: string
  prefab: { path: string; name: string; missing: boolean; overrides: string[] } | null
  components: ComponentData[]
  addable: string[]
  choices: {
    entities: { id: number; label: string }[]
    textures: string[]
    audio: string[]
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

// Prefabs.
const savingPrefab = ref(false)
const newPrefabName = ref('')

function startSavePrefab(): void {
  newPrefabName.value = (current.value?.label ?? '').replace(/[^A-Za-z0-9_-]/g, '_').toLowerCase()
  savingPrefab.value = true
}

function savePrefab(): void {
  if (newPrefabName.value.trim()) {
    sendToHost({ type: 'prefab:saveSelected', name: newPrefabName.value.trim() })
  }
  savingPrefab.value = false
}

function revert(key: string): void {
  sendToHost({ type: 'prefab:revert', key })
}

// Script properties.
function setProperty(name: string, value: unknown): void {
  if (current.value && !(typeof value === 'number' && Number.isNaN(value))) {
    sendToHost({ type: 'script:setProperty', id: current.value.id, name, value })
  }
}

function resetProperty(name: string): void {
  if (current.value) {
    sendToHost({ type: 'script:resetProperty', id: current.value.id, name })
  }
}
</script>

<template>
  <div class="panel inspector">
    <h2>Inspector</h2>
    <p v-if="!current" class="placeholder">No entity selected.</p>
    <template v-else>
      <div class="entity-label">{{ current.label }}</div>

      <div v-if="current.prefab" class="prefab-header" :class="{ missing: current.prefab.missing }">
        <div class="prefab-title">
          <span class="prefab-kind">Prefab</span>
          <span :title="current.prefab.path">{{ current.prefab.name }}</span>
          <span v-if="current.prefab.missing" class="warning">(file missing)</span>
        </div>
        <div v-if="!editorState.playing" class="prefab-actions">
          <span class="override-count">
            {{ current.prefab.overrides.length }} override{{ current.prefab.overrides.length === 1 ? '' : 's' }}
          </span>
          <button
            :disabled="current.prefab.missing"
            title="Make this copy's values the prefab's; every other copy updates (keeping their own overrides)"
            @click="sendToHost({ type: 'prefab:applySelected' })"
          >
            Apply to prefab
          </button>
          <button
            :disabled="current.prefab.overrides.length === 0"
            title="Discard this copy's changes"
            @click="revert('')"
          >
            Revert all
          </button>
        </div>
      </div>

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
          <div v-for="field in component.fields" :key="field.name" class="field-row" :class="{ overridden: field.overridden }">
            <label :title="field.overridden ? 'Changed on this copy; prefab updates won\'t touch it' : undefined">
              {{ field.name }}
              <button
                v-if="field.overridden && !editorState.playing"
                class="revert"
                title="Revert to the prefab's value"
                @click="revert(`${component.name}.${field.name}`)"
              >
                ↺
              </button>
            </label>

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

            <select
              v-else-if="field.type === 'audio'"
              :value="field.value as string"
              :title="field.value as string"
              @change="setField(component.name, field, ($event.target as HTMLSelectElement).value)"
            >
              <option value="">(none)</option>
              <option v-for="sound in current.choices.audio" :key="sound" :value="sound">
                {{ fileName(sound) }}
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

          <template v-if="component.script">
            <p v-if="component.script.error" class="script-error">{{ component.script.error }}</p>
            <div v-else-if="component.script.properties.length > 0" class="properties-label">
              Script properties
            </div>
            <div
              v-for="property in component.script.properties"
              :key="property.name"
              class="field-row"
              :class="{ overridden: property.overridden, unset: !property.set }"
            >
              <label :title="property.set ? 'Set on this entity' : 'Script default'">
                {{ property.name }}
                <button
                  v-if="property.set && !editorState.playing"
                  class="revert"
                  title="Back to the default"
                  @click="resetProperty(property.name)"
                >
                  ↺
                </button>
              </label>
              <input
                v-if="property.type === 'float'"
                type="number"
                step="0.1"
                :value="property.value as number"
                @change="setProperty(property.name, numberFrom($event))"
              />
              <input
                v-else-if="property.type === 'bool'"
                type="checkbox"
                class="checkbox"
                :checked="property.value as boolean"
                @change="setProperty(property.name, ($event.target as HTMLInputElement).checked)"
              />
              <input
                v-else
                type="text"
                spellcheck="false"
                :value="property.value as string"
                @change="setProperty(property.name, ($event.target as HTMLInputElement).value)"
              />
            </div>
          </template>
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

        <template v-if="!current.prefab">
          <div v-if="savingPrefab" class="new-script">
            <input
              v-model="newPrefabName"
              type="text"
              placeholder="prefab name"
              spellcheck="false"
              @keydown.enter="savePrefab"
              @keydown.esc="savingPrefab = false"
            />
            <button @click="savePrefab">Save</button>
          </div>
          <button
            v-else
            class="link"
            title="Store this entity as a reusable prefab; it becomes the first copy"
            @click="startSavePrefab"
          >
            Save as prefab…
          </button>
        </template>
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
  display: flex;
  align-items: center;
  gap: 4px;
  opacity: 0.7;
  flex-shrink: 0;
}
.field-row.overridden {
  box-shadow: inset 2px 0 0 #3b82f6;
  padding-left: 6px;
}
.field-row.overridden label {
  opacity: 1;
  color: #93b4f8;
}
.field-row.unset input:not(:focus) {
  opacity: 0.6;
}
.revert {
  width: 18px;
  height: 18px;
  padding: 0;
  border-color: transparent;
  background: none;
  cursor: pointer;
  font-size: 0.9em;
}
.revert:hover {
  border-color: #3b82f6;
}
.prefab-header {
  border: 1px solid #3b82f6;
  background: #1f2440;
  padding: 6px 8px;
  margin-bottom: 8px;
  font-size: 0.85em;
}
.prefab-header.missing {
  border-color: #ef4444;
}
.prefab-title {
  display: flex;
  gap: 6px;
  align-items: center;
}
.prefab-kind {
  font-size: 0.8em;
  text-transform: uppercase;
  opacity: 0.6;
}
.warning {
  color: #ef4444;
}
.prefab-actions {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 4px;
  margin-top: 6px;
}
.prefab-actions button {
  padding: 0 8px;
  white-space: nowrap;
  cursor: pointer;
}
.prefab-actions button:disabled {
  opacity: 0.4;
  cursor: default;
}
.override-count {
  flex-basis: 100%;
  opacity: 0.7;
  white-space: nowrap;
}
.properties-label {
  margin: 8px 0 4px;
  font-size: 0.75em;
  text-transform: uppercase;
  opacity: 0.6;
}
.script-error {
  color: #ef4444;
  font-size: 0.8em;
  word-break: break-word;
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
