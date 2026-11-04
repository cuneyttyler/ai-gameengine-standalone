<script setup lang="ts">
// The Reflection Inspector: every section and field is generated from the
// engine's reflection registry (apps/editor/EditorApp InspectorJson /
// SettingsJson) — names, types, descriptions, constraints. Project settings
// have their own window (ProjectSettingsDialog, 5.5).
import { computed, onMounted, reactive, ref } from 'vue'
import { armPrefab, askAiAbout, editorState } from '../editorState'
import { replacePrefab } from '../sceneMenus'
import PrefabThumb from './PrefabThumb.vue'
import { projectState } from '../projects'
import { onHostMessage, sendToHost } from '../ipc'
import FieldEditor, { type Choices, type FieldData } from './FieldEditor.vue'
import InteriorEditor from './InteriorEditor.vue'
import PathEditor from './PathEditor.vue'
import ActionPicker from './ActionPicker.vue'
import RulesEditor from './RulesEditor.vue'
import CharacterRulesEditor from './CharacterRulesEditor.vue'
import ItemListEditor from './ItemListEditor.vue'
import SearchSelect from './SearchSelect.vue'
import { recordsAndWorld, requestRecords } from '../recordIds'
import { effectIds, playerClips, requestPlayerClips, sheetClips } from '../inputActions'
import PartActiveFields from './PartActiveFields.vue'
import { difficultyView, showTab } from '../paneltabs'

interface ScriptProperty {
  name: string
  type: 'float' | 'bool' | 'string'
  value: number | boolean | string
  set: boolean // this entity has its own value (otherwise the script's default)
  overridden: boolean
}

interface ComponentData {
  name: string
  label?: string // how it's shown (Interior: Building Plan)
  description: string
  removable: boolean
  derived: boolean
  fields: FieldData[]
  script?: { properties: ScriptProperty[]; error: string }
}

interface InspectorData {
  id: number
  label: string
  building?: boolean // the ontology says it's a building (a Building Plan, ways in)
  prefab: { path: string; name: string; missing: boolean; overrides: string[] } | null
  size: number | null // % of its prefab's size (prefab copies with a sprite)
  components: ComponentData[]
  addable: { name: string; label?: string; description: string }[]
  choices: Choices
}


// The Difficulty tab's preview of the selection at every level (16.2).
function showDifficultyPreview(): void {
  difficultyView.section = 'preview'
  showTab('difficulty')
}

const current = ref<InspectorData | null>(null)

// A Reaction's clips and effects, picked from what there is: its user's from the characters' clips, its own from its
// Animator's sheet (and the states it maps), effects from Project ▸ Effects and the presets. Null: not one of those.
function reactionChoices(field: string): { value: string; hint?: string }[] | null {
  if (field === 'userAnimation') return playerClips.value.map((c) => ({ value: c }))
  if (field === 'effect' || field === 'userEffect') return effectIds.value.map((e) => ({ value: e }))
  if (field !== 'animation' && field !== 'awayAnimation') return null
  const animator = current.value?.components.find((c) => c.name === 'Animator')
  const valueOf = (name: string) => String(animator?.fields.find((f) => f.name === name)?.value ?? '')
  const sheet = valueOf('sheet')
  const out = new Map<string, string>()
  for (const c of sheetClips.value[sheet] ?? []) out.set(c, 'clip')
  for (const pair of valueOf('clips').split(/[;,]/)) {
    const state = pair.split('=')[0]?.trim()
    if (state && !out.has(state)) out.set(state, 'state')
  }
  if (!animator) return [] // no Animator: nothing of its own plays (the list says so)
  return [...out].map(([value, hint]) => ({ value, hint }))
}
onMounted(() => { requestPlayerClips(); requestRecords() })
// A ground cell of a tile layer (a built place's ground), selected with the Select tool.
const cell = ref<{ layer: string; x: number; y: number; prefab: string; texture: string; solid: boolean } | null>(null)
// The selected cell's tile prefab, with its picture.
const cellPrefab = computed(() => (cell.value?.prefab ? editorState.prefabs.find((p) => p.path === cell.value!.prefab) : undefined))
// A box selection (drag on the ground): how many things and ground cells.
const many = ref<{ entities: number; cells: number } | null>(null)
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
      partShown.value = 0
    }
    current.value = data
    cell.value = null
  } else if (message.type === 'inspector:cell') {
    cell.value = message as unknown as NonNullable<typeof cell.value>
    current.value = null
    many.value = null
  } else if (message.type === 'inspector:many') {
    many.value = { entities: Number(message.entities ?? 0), cells: Number(message.cells ?? 0) }
    current.value = null
    cell.value = null
  } else if (message.type === 'inspector:clear') {
    current.value = null
    cell.value = null
    many.value = null
  } else if (message.type === 'inspector:fieldError') {
    const key = `${message.component}.${message.field}`
    if (message.error) {
      errors[key] = message.error as string
    } else {
      delete errors[key]
    }
  }
})

function setSize(percent: number): void {
  if (current.value && Number.isFinite(percent) && percent > 0) {
    sendToHost({ type: 'entity:setSize', id: current.value.id, percent })
  }
}

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

// AnimatedPart: which of its parts is shown (0: the component's own fields; k: the k-th of "more"), all edited with one form.
interface MorePart { if: string; do: string; end: string; clip: string; closeClip: string; x: number; y: number; width: number; height: number; opens: string; trigger: string; prompt: string; fires: string; sheet: string; duration: number; playerClip: string; playerLoop: boolean; holdsPlayer: boolean; range: number; locked: boolean; key: string; consumeKey: boolean; door: boolean; floor: number }
const partShown = ref(0)
const partsField = (component: { fields: FieldData[] }) => component.fields.find((f) => f.name === 'more')
function moreParts(component: { fields: FieldData[] }): MorePart[] {
  try {
    const list = JSON.parse(String(partsField(component)?.value ?? '') || '[]')
    return Array.isArray(list) ? list : []
  } catch {
    return []
  }
}
const partIndex = (component: { fields: FieldData[] }) => Math.min(partShown.value, moreParts(component).length)
function partOf(component: { fields: FieldData[] }): MorePart {
  const k = partIndex(component)
  if (k > 0) return moreParts(component)[k - 1]!
  return Object.fromEntries(component.fields.map((f) => [f.name, f.value])) as unknown as MorePart
}
function setPart(component: { fields: FieldData[] }, key: keyof MorePart, value: unknown): void {
  const k = partIndex(component)
  if (k === 0) {
    const own = component.fields.find((f) => f.name === key)
    if (own) setField('AnimatedPart', own, value)
    return
  }
  const field = partsField(component)
  const list = moreParts(component) as unknown as Record<string, unknown>[]
  if (!field || !list[k - 1]) return
  list[k - 1]![key] = value
  setField('AnimatedPart', field, JSON.stringify(list))
}
// Its picture and clips (where it sits on the picture, what it plays): rarely changed by hand.
const partPlacement: { key: keyof MorePart; number?: boolean }[] = [
  { key: 'sheet' }, { key: 'clip' }, { key: 'closeClip' }, { key: 'x', number: true }, { key: 'y', number: true },
  { key: 'width', number: true }, { key: 'height', number: true },
]
// What this copy changed of its parts, to give to the prefab or take back.
const changedFields = (component: { fields: FieldData[] }) => component.fields.filter((f) => f.overridden).map((f) => f.name)
function eachChanged(component: { fields: FieldData[] }, send: (key: string) => void): void {
  for (const name of changedFields(component)) send(`AnimatedPart.${name}`)
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

    <!-- A box selection: how much is in it, and delete it all. -->
    <template v-if="many && !current && !cell">
      <div class="entity-label">{{ many.entities }} thing(s), {{ many.cells }} ground cell(s)</div>
      <p class="hint">Selected by dragging on the ground. Delete removes them all (one undo step each for things and ground).</p>
      <button :disabled="!many.entities && !many.cells" @click="sendToHost({ type: 'edit:delete' })">Delete them (Delete)</button>
    </template>

    <!-- A ground cell (a built place's tile layer): what's there, and erase it. -->
    <template v-else-if="cell && !current">
      <div class="entity-label">Ground cell ({{ cell.x }}, {{ cell.y }})</div>
      <p class="hint">In the tile layer “{{ cell.layer }}”{{ cell.solid ? ', blocking' : '' }}.</p>
      <div v-if="cellPrefab" class="tile-prefab">
        <PrefabThumb :prefab="cellPrefab" :box="64" />
        <div>
          <div>{{ cellPrefab.name }}</div>
          <div class="hint">{{ cellPrefab.path }}</div>
        </div>
      </div>
      <p v-else-if="cell.prefab" class="hint">Tile: {{ cell.prefab }}</p>
      <p v-else-if="cell.texture" class="hint">Image: {{ cell.texture }}</p>
      <div class="row-actions">
        <button v-if="cell.prefab" :disabled="editorState.playing" title="Paint with this tile" @click="armPrefab(cell.prefab)">Draw with it</button>
        <button @click="sendToHost({ type: 'edit:delete' })">Erase this cell (Delete)</button>
      </div>
    </template>

    <!-- Nothing selected. Project settings have their own window (5.5). -->
    <template v-else-if="!current">
      <p class="hint">Select something in the viewport or Scene Tree to inspect it.</p>
      <button class="settings-link" @click="projectState.showProjectSettings = true">Project Settings…</button>
    </template>

    <template v-else>
      <div class="entity-label">{{ current.label }}</div>

      <label v-if="current.size !== null" class="size-row" title="Its size as a % of the prefab's: sprite and collision together ([ ] and Alt+wheel too)">
        <span>Size</span>
        <input
          type="number"
          min="10"
          max="1000"
          step="5"
          :value="current.size"
          :disabled="editorState.playing"
          @change="setSize(Number(($event.target as HTMLInputElement).value))"
        />
        <span class="unit">% of the prefab</span>
      </label>

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
          <button :disabled="current.prefab.missing" title="Put a copy of another prefab in its place" @click="replacePrefab({ id: current.id, prefab: current.prefab.path })">
            Replace prefab…
          </button>
          <button :disabled="current.prefab.missing" title="Draw with this prefab" @click="armPrefab(current.prefab.path)">Draw with it</button>
        </div>
      </div>

      <section v-for="component in current.components" :key="component.name" class="component">
        <header>
          <button
            class="collapse"
            :title="component.description"
            @click="collapsed[component.name] = !collapsed[component.name]"
          >
            {{ collapsed[component.name] ? '▸' : '▾' }} {{ component.label ?? component.name }}
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
          <!-- Facing: a direction on screen (the editor works out the world facing for the grid). -->
          <div v-if="component.name === 'Facing'" class="field-row">
            <label>turn to</label>
            <select :value="''" :disabled="editorState.playing" title="Which side of it shows (R and Shift+R turn it an eighth)" @change="sendToHost({ type: 'entity:face', id: current!.id, direction: ($event.target as HTMLSelectElement).value })">
              <option value="">a direction…</option>
              <option v-for="d in ['south', 'south-east', 'east', 'north-east', 'north', 'north-west', 'west', 'south-west']" :key="d" :value="d">{{ d }}</option>
            </select>
          </div>
          <!-- Path: its points and forks edited as what they are (the raw JSON below is the same fields). -->
          <PathEditor
            v-if="component.name === 'Path'"
            :fields="component.fields"
            :disabled="editorState.playing"
            :id="current.id"
            @set="(name, value) => setField('Path', component.fields.find((f) => f.name === name)!, value)"
          />
          <!-- Interior: its floors, stairs and ways in edited as what they are (the raw lists below are the same fields). -->
          <InteriorEditor
            v-if="component.name === 'Interior'"
            :fields="component.fields"
            :choices="current.choices"
            :disabled="editorState.playing"
            :id="current.id"
            @set="(name, value) => setField('Interior', component.fields.find((f) => f.name === name)!, value)"
          />
          <!-- AnimatedPart: its doors listed (its own fields are the first; the others are in "more"), edited in the building editor. -->
          <div v-if="component.name === 'AnimatedPart'" class="field-row doors-summary">
            <span>{{ 1 + (String(component.fields.find((f) => f.name === 'more')?.value ?? '').match(/"sheet"/g)?.length ?? 0) }}
              {{ current.building ? 'part(s): doors and others' : 'moving part(s)' }}</span>
            <button :disabled="editorState.playing" title="See each part on its picture, what activates it and what the player is shown"
                    @click="editorState.buildingEditor = current!.id">
              {{ current.building && current.components.some((s) => s.name === 'Interior') ? 'Edit doors…' : 'Edit parts…' }}</button>
          </div>
          <!-- More than one part: each picked, and seen and set with the one form below. -->
          <div v-if="component.name === 'AnimatedPart' && moreParts(component).length" class="field-row part-tabs">
            <button v-for="(p, k) in [null, ...moreParts(component)]" :key="k" :class="{ active: Math.min(partShown, moreParts(component).length) === k }"
                    @click="partShown = k">
              {{ (k === 0 ? component.fields.find((f) => f.name === 'sheet')?.value : p?.sheet) === 'spot' ? 'Spot'
                : (k === 0 ? component.fields.find((f) => f.name === 'door')?.value : p?.door) && current.building ? 'Door' : 'Part' }} {{ k + 1 }}
            </button>
          </div>
          <template v-if="component.name === 'AnimatedPart'">
            <div class="field-row">
              <label title="What sets it moving">activation</label>
              <select :value="partOf(component).opens" :disabled="editorState.playing" @change="setPart(component, 'opens', ($event.target as HTMLSelectElement).value)">
                <option value="project">as the project's doors</option>
                <option value="use">on use</option>
                <option value="auto">by itself, as someone comes near</option>
                <option value="action">an input action, near it</option>
                <option value="event">a game event</option>
                <option value="always">always playing (a loop)</option>
                <option v-if="partOf(component).door" value="open">always open (a doorway)</option>
              </select>
            </div>
            <div v-if="partOf(component).opens === 'event'" class="field-row">
              <label title="The event that plays it">trigger</label>
              <input :value="partOf(component).trigger" spellcheck="false" :disabled="editorState.playing" placeholder="bridge_down"
                     @change="setPart(component, 'trigger', ($event.target as HTMLInputElement).value)" />
            </div>
            <div v-if="['action', 'always'].includes(partOf(component).opens)" class="field-row">
              <label :title="partOf(component).opens === 'always' ? 'Optional: pressed near it, it sends its event' : 'Pressed near it: it plays'">input action</label>
              <ActionPicker :model-value="partOf(component).trigger" :disabled="editorState.playing"
                            @update:model-value="setPart(component, 'trigger', $event)" />
            </div>
            <div v-if="!['auto', 'event'].includes(partOf(component).opens)" class="field-row">
              <label title="Sent each time the player activates it: scripts, triggers and quests react to it">sends event</label>
              <input :value="partOf(component).fires ?? ''" spellcheck="false" :disabled="editorState.playing" placeholder="optional (cooked)"
                     @change="setPart(component, 'fires', ($event.target as HTMLInputElement).value)" />
            </div>
            <div v-if="['project', 'use', 'action'].includes(partOf(component).opens) || (partOf(component).opens === 'always' && partOf(component).trigger)" class="field-row">
              <label title="Shown near it; 'shut|open' for each way">prompt</label>
              <input :value="partOf(component).prompt" :disabled="editorState.playing" placeholder="Pull the lever"
                     @change="setPart(component, 'prompt', ($event.target as HTMLInputElement).value)" />
            </div>
            <div v-if="!['auto', 'always', 'open'].includes(partOf(component).opens)" class="field-row">
              <PartActiveFields :duration="partOf(component).duration ?? 0" :player-clip="partOf(component).playerClip ?? ''"
                                :player-loop="partOf(component).playerLoop ?? true" :holds-player="partOf(component).holdsPlayer ?? false"
                                :action="['action', 'always'].includes(partOf(component).opens) ? partOf(component).trigger : ''"
                                :disabled="editorState.playing" style="width: 100%" @set="(k, v) => setPart(component, k, v)" />
            </div>
            <div class="field-row">
              <label title="How near (in cells) one must be to use it">range</label>
              <input type="number" step="0.1" min="0" :value="partOf(component).range" :disabled="editorState.playing"
                     @change="setPart(component, 'range', Number(($event.target as HTMLInputElement).value))" />
            </div>
            <div v-if="(partOf(component).floor ?? 0) > 0 || !partOf(component).door" class="field-row">
              <label title="Which picture it's on: 0 the outside, k the interior's floor k">floor</label>
              <input type="number" step="1" min="0" :value="partOf(component).floor ?? 0" :disabled="editorState.playing"
                     @change="setPart(component, 'floor', Math.max(0, Math.round(Number(($event.target as HTMLInputElement).value))))" />
            </div>
            <div class="field-row">
              <label title="It won't move without its key">locked</label>
              <input type="checkbox" :checked="partOf(component).locked" :disabled="editorState.playing"
                     @change="setPart(component, 'locked', ($event.target as HTMLInputElement).checked)" />
            </div>
            <div v-if="partOf(component).locked" class="field-row">
              <label title="The item that unlocks it">key</label>
              <input :value="partOf(component).key" spellcheck="false" :disabled="editorState.playing"
                     @change="setPart(component, 'key', ($event.target as HTMLInputElement).value)" />
            </div>
            <div v-if="partOf(component).locked" class="field-row">
              <label title="The key is used up unlocking it">key used up</label>
              <input type="checkbox" :checked="partOf(component).consumeKey" :disabled="editorState.playing"
                     @change="setPart(component, 'consumeKey', ($event.target as HTMLInputElement).checked)" />
            </div>
            <div v-if="current.building" class="field-row">
              <label title="A way in of the building: blocked while shut">door</label>
              <input type="checkbox" :checked="partOf(component).door" :disabled="editorState.playing"
                     @change="setPart(component, 'door', ($event.target as HTMLInputElement).checked)" />
            </div>
            <div class="rule-block" title="It can be activated only while these hold">
              <label>Only if</label>
              <RulesEditor :key="partIndex(component) + 'if'" mode="if" :value="partOf(component).if" :disabled="editorState.playing" @update="setPart(component, 'if', $event)" />
            </div>
            <div class="rule-block" title="Run each time the player activates it">
              <label>When activated</label>
              <RulesEditor :key="partIndex(component) + 'do'" mode="do" :value="partOf(component).do" :disabled="editorState.playing" @update="setPart(component, 'do', $event)" />
            </div>
            <div class="rule-block" title="Run when it stops: used again, its duration over, or left">
              <label>When it stops</label>
              <RulesEditor :key="partIndex(component) + 'end'" mode="do" :value="partOf(component).end" :disabled="editorState.playing" @update="setPart(component, 'end', $event)" />
            </div>
            <details class="part-placement">
              <summary>Picture and clips</summary>
              <div v-for="row in partPlacement" :key="row.key" class="field-row">
                <label>{{ row.key }}</label>
                <input :type="row.number ? 'number' : 'text'" spellcheck="false" :value="partOf(component)[row.key]" :disabled="editorState.playing"
                       @change="setPart(component, row.key, row.number ? Number(($event.target as HTMLInputElement).value) : ($event.target as HTMLInputElement).value)" />
              </div>
            </details>
            <!-- What this copy changed of its parts (any of them): given to the prefab, or taken back. -->
            <div v-if="changedFields(component).length && !editorState.playing" class="field-row overridden">
              <span :title="changedFields(component).join(', ')">Parts changed on this copy</span>
              <span>
                <button class="revert" title="Apply these to the prefab: every copy gets them" @click="eachChanged(component, (key) => sendToHost({ type: 'prefab:applySelected', key }))">⤒</button>
                <button class="revert" title="Revert to the prefab's parts" @click="eachChanged(component, revert)">↺</button>
              </span>
            </div>
            <div v-for="f in component.fields.filter((f) => errors[`AnimatedPart.${f.name}`])" :key="f.name" class="field-error">{{ errors[`AnimatedPart.${f.name}`] }}</div>
          </template>
          <template v-for="field in component.name === 'AnimatedPart' ? [] : component.fields.filter((f) => component.name !== 'Interior' || !['floors', 'stairs', 'entries', 'texture', 'solids', 'layout'].includes(f.name))" :key="field.name">
            <div class="field-row" :class="{ overridden: field.overridden }">
              <label :title="field.overridden ? `${field.description}\n(Changed on this copy)` : field.description">
                {{ (component.name === 'AnimatedPart' || component.name === 'DoorPart') && field.name === 'opens' ? 'activation' : field.name }}
                <button
                  v-if="editorState.ai.state === 'ready' && !editorState.playing"
                  class="ask-ai"
                  title="Ask AI about this field"
                  @click="askAiAbout(`${current.label}.${component.name}.${field.name}`)"
                >
                  ✦
                </button>
                <button
                  v-if="field.scalable"
                  class="ask-ai"
                  :title="`Scaled by difficulty (${field.scalable}): see and set its value at every level`"
                  @click="showDifficultyPreview"
                >
                  ⚖
                </button>
                <button
                  v-if="field.overridden && !editorState.playing"
                  class="revert"
                  title="Apply this value to the prefab: every copy gets it (a copy sized on its own keeps its share)"
                  @click="sendToHost({ type: 'prefab:applySelected', key: `${component.name}.${field.name}` })"
                >
                  ⤒
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
              <RulesEditor
                v-if="['Action', 'Interactable'].includes(component.name) && ['if', 'do', 'end', 'interrupted'].includes(field.name)"
                :mode="field.name === 'if' ? 'if' : 'do'"
                :value="field.value"
                :disabled="editorState.playing"
                @update="setField(component.name, field, $event)"
              />
              <ItemListEditor
                v-else-if="component.name === 'Action' && ['requires', 'takes', 'gives', 'accepts'].includes(field.name)"
                :value="field.value"
                :counts="field.name !== 'accepts'"
                :disabled="editorState.playing"
                @update="setField(component.name, field, $event)"
              />
              <SearchSelect
                v-else-if="component.name === 'FactionMember' && field.name === 'faction'"
                :model-value="String(field.value ?? '')"
                :options="recordsAndWorld('faction')"
                :disabled="editorState.playing"
                empty="none"
                custom
                @update:model-value="setField(component.name, field, $event)"
              />
              <SearchSelect
                v-else-if="component.name === 'Action' && reactionChoices(field.name)"
                :model-value="String(field.value ?? '')"
                :options="reactionChoices(field.name) ?? []"
                :disabled="editorState.playing"
                empty="none"
                custom
                @update:model-value="setField(component.name, field, $event)"
              />
              <ActionPicker
                v-else-if="component.name === 'Action' && field.name === 'action'"
                :model-value="String(field.value ?? '')"
                :disabled="editorState.playing"
                @update:model-value="setField(component.name, field, $event)"
              />
              <CharacterRulesEditor
                v-else-if="component.name === 'Responses' && field.name === 'rules'"
                :value="field.value"
                :disabled="editorState.playing"
                @update="setField(component.name, field, $event)"
              />
              <FieldEditor
                v-else
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
          <option v-for="item in [...current.addable].sort((a, b) => (a.label ?? a.name).localeCompare(b.label ?? b.name))" :key="item.name" :value="item.name" :title="item.description">
            {{ item.label ?? item.name }}
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
.size-row {
  display: flex;
  align-items: center;
  gap: 8px;
  margin: 0 0 8px;
  font-size: 0.85em;
}
.size-row > span:first-child {
  opacity: 0.8;
  width: 40px;
}
.size-row input {
  width: 72px;
  height: 24px;
  padding: 0 6px;
  border: 1px solid #333;
  border-radius: 0;
  background: #11111b;
  color: #eee;
  font: inherit;
}
.size-row .unit {
  opacity: 0.55;
}
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
.rule-block {
  display: flex;
  flex-direction: column;
  gap: 2px;
  padding: 3px 0;
}
.part-placement summary {
  cursor: pointer;
  opacity: 0.75;
  padding: 2px 0;
}
.field-row.part-tabs {
  justify-content: flex-start;
  flex-wrap: wrap;
  gap: 2px;
}
.part-tabs button.active {
  background: var(--accent, #3b6fd6);
  color: #fff;
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
.tile-prefab {
  display: flex;
  gap: 8px;
  align-items: center;
  margin: 6px 0;
}
.row-actions {
  display: flex;
  gap: 6px;
  flex-wrap: wrap;
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
