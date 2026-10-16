<script setup lang="ts">
// The Reflection Inspector: every section and field is generated from the
// engine's reflection registry (apps/editor/EditorApp InspectorJson /
// SettingsJson) — names, types, descriptions, constraints. Project settings
// have their own window (ProjectSettingsDialog, 5.5).
import { reactive, ref } from 'vue'
import { askAiAbout, editorState } from '../editorState'
import { projectState } from '../projects'
import { onHostMessage, sendToHost } from '../ipc'
import FieldEditor, { type Choices, type FieldData } from './FieldEditor.vue'

interface ScriptProperty {
  name: string
  type: 'float' | 'bool' | 'string'
  value: number | boolean | string
  set: boolean // this entity has its own value (otherwise the script's default)
  overridden: boolean
}

interface ComponentData {
  name: string
  description: string
  removable: boolean
  derived: boolean
  fields: FieldData[]
  script?: { properties: ScriptProperty[]; error: string }
}

interface InspectorData {
  id: number
  label: string
  prefab: { path: string; name: string; missing: boolean; overrides: string[] } | null
  components: ComponentData[]
  addable: { name: string; description: string }[]
  choices: Choices
}

const current = ref<InspectorData | null>(null)
const collapsed = reactive<Record<string, boolean>>({})
// Last validation error per "Component.field" (or "Section.field").
const errors = reactive<Record<string, string>>({})
const newScriptName = ref('')
const creatingScript = ref(false)
const savingPrefab = ref(false)
const newPrefabName = ref('')

onHostMessage((message) => {
  if (message.type === 'inspector:data') {
    const data = message as unknown as InspectorData
    // While playing the host refreshes this 10x per second; don't clobber a
    // field the user is in the middle of typing into.
    const focused = document.activeElement as HTMLElement | null
    if (focused?.closest('.inspector') && current.value?.id === data.id && editorState.playing) {
      return
    }
    if (current.value?.id !== data.id) {
      Object.keys(errors).forEach((key) => delete errors[key])
    }
    current.value = data
  } else if (message.type === 'inspector:clear') {
    current.value = null
  } else if (message.type === 'inspector:fieldError') {
    const key = `${message.component}.${message.field}`
    if (message.error) {
      errors[key] = message.error as string
    } else {
      delete errors[key]
    }
  }
})

function setField(component: string, field: FieldData, value: unknown): void {
  if (current.value) {
    sendToHost({ type: 'inspector:setField', id: current.value.id, component, field: field.name, value })
  }
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

function numberFrom(event: Event): number {
  return Number((event.target as HTMLInputElement).value)
}
</script>

<template>
  <div class="panel inspector">
    <h2>Inspector</h2>

    <!-- Nothing selected. Project settings have their own window (5.5). -->
    <template v-if="!current">
      <p class="hint">Select something in the viewport or Scene Tree to inspect it.</p>
      <button class="settings-link" @click="projectState.showProjectSettings = true">Project Settings…</button>
    </template>

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
          <button
            class="collapse"
            :title="component.description"
            @click="collapsed[component.name] = !collapsed[component.name]"
          >
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
          <template v-for="field in component.fields" :key="field.name">
            <div class="field-row" :class="{ overridden: field.overridden }">
              <label :title="field.overridden ? `${field.description}\n(Changed on this copy)` : field.description">
                {{ field.name }}
                <button
                  v-if="editorState.ai.state === 'ready' && !editorState.playing"
                  class="ask-ai"
                  title="Ask AI about this field"
                  @click="askAiAbout(`${current.label}.${component.name}.${field.name}`)"
                >
                  ✦
                </button>
                <button
                  v-if="field.overridden && !editorState.playing"
                  class="revert"
                  title="Revert to the prefab's value"
                  @click="revert(`${component.name}.${field.name}`)"
                >
                  ↺
                </button>
              </label>
              <FieldEditor
                :field="field"
                :choices="current.choices"
                :disabled="component.derived"
                @change="setField(component.name, field, $event)"
                @open-script="openScript"
              />
            </div>
            <div v-if="errors[`${component.name}.${field.name}`]" class="field-error">
              {{ errors[`${component.name}.${field.name}`] }}
            </div>
          </template>

          <template v-if="component.script">
            <p v-if="component.script.error" class="script-error">{{ component.script.error }}</p>
            <div v-else-if="component.script.properties.length > 0" class="properties-label">Script properties</div>
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
                class="property-input"
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
                class="property-input"
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
          <option v-for="item in current.addable" :key="item.name" :value="item.name" :title="item.description">
            {{ item.name }}
          </option>
        </select>

        <div v-if="creatingScript" class="inline-form">
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
          <div v-if="savingPrefab" class="inline-form">
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
.settings-link {
  margin: 4px 0 0;
  height: 26px;
  padding: 0 10px;
  border: 1px solid #333;
  border-radius: 0;
  background: #11111b;
  color: #eee;
  font: inherit;
  cursor: pointer;
}
.settings-link:hover {
  background: #262637;
}
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
.hint {
  opacity: 0.5;
  font-size: 0.8em;
  margin: 0 0 8px;
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
  cursor: help;
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
.field-error {
  margin: -2px 0 6px;
  color: #f87171;
  font-size: 0.75em;
  text-align: right;
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
.property-input {
  width: 120px;
  padding: 0 4px;
}
.checkbox {
  width: 14px;
  height: 14px;
}
.ask-ai {
  width: 18px;
  height: 18px;
  padding: 0;
  border-color: transparent;
  background: none;
  color: #93b4f8;
  cursor: pointer;
  font-size: 0.85em;
  visibility: hidden;
}
.field-row:hover .ask-ai {
  visibility: visible;
}
.ask-ai:hover {
  border-color: #3b82f6;
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
.inline-form {
  display: flex;
  gap: 4px;
}
.inline-form input {
  flex: 1;
  padding: 0 4px;
}
.inline-form button,
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
