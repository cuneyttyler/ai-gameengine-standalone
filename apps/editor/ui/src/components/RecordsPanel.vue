<script setup lang="ts">
// Records (development_plan.md 12.1): the game's typed data — items and every
// other record type the engine or the project's modules declare. Each record
// is a file, records/<type>/<id>.json (engine/records); the host reads,
// checks and writes them, this tab lists them by type and edits one at a time
// with a form generated from its type's fields.
import { computed, onMounted, reactive, ref } from 'vue'
import { onHostMessage, sendToHost } from '../ipc'
import { confirmDialog } from '../dialogs'
import RecordValue from './story/RecordValue.vue'
import { defaultFor, type RecordFieldSpec } from '../records'

interface RecordType {
  name: string
  description: string
  source: string
  fields: RecordFieldSpec[]
  defaults: Record<string, unknown>
}
interface RecordEntry {
  type: string
  id: string
  path: string
  data: Record<string, unknown>
  resolved: Record<string, unknown>
  usedBy: string[]
}

const types = ref<RecordType[]>([])
const records = ref<RecordEntry[]>([])
const problems = ref<{ key: string; message: string }[]>([])
const query = ref('')
const collapsed = reactive<Record<string, boolean>>({})
const selected = ref<{ type: string; id: string } | null>(null)
const saveError = ref('')

// The form edits a draft: `own` says which fields this record sets itself
// (the rest come from its base); `originalId` is '' for a new record.
const draft = reactive({
  type: '',
  originalId: '',
  id: '',
  base: '',
  own: {} as Record<string, boolean>,
  data: {} as Record<string, unknown>,
  provenance: undefined as unknown,
})
const snapshot = ref('')

onHostMessage((message) => {
  if (message.type === 'records:list') {
    types.value = message.types as RecordType[]
    records.value = message.records as RecordEntry[]
    problems.value = message.problems as { key: string; message: string }[]
  } else if (message.type === 'records:saved') {
    saveError.value = (message.error as string | null) ?? ''
    if (!saveError.value) {
      selected.value = { type: message.recordType as string, id: message.id as string }
      draft.originalId = message.id as string
      snapshot.value = JSON.stringify(draftOut())
    }
  }
})

onMounted(() => sendToHost({ type: 'records:request' }))

const typeOf = (name: string) => types.value.find((t) => t.name === name)
const find = (type: string, id: string) => records.value.find((r) => r.type === type && r.id === id)
const ids = computed(() => {
  const map: Record<string, string[]> = {}
  for (const t of types.value) map[t.name] = []
  for (const r of records.value) (map[r.type] ??= []).push(r.id)
  return map
})
const label = (r: RecordEntry) => (typeof r.resolved.name === 'string' && r.resolved.name) || r.id
const problemsFor = (key: string) => problems.value.filter((p) => p.key === key).map((p) => p.message)

const groups = computed(() => {
  const q = query.value.trim().toLowerCase()
  return types.value.map((t) => ({
    type: t,
    records: records.value
      .filter((r) => r.type === t.name)
      .filter((r) => !q || r.id.includes(q) || label(r).toLowerCase().includes(q) || JSON.stringify(r.data).toLowerCase().includes(q)),
  }))
})

const currentType = computed(() => typeOf(draft.type))
const baseRecord = computed(() => (draft.base ? find(draft.type, draft.base) : undefined))
const dirty = computed(() => JSON.stringify(draftOut()) !== snapshot.value)

function load(type: string, data: Record<string, unknown>, resolved: Record<string, unknown>, id: string, originalId: string): void {
  const t = typeOf(type)
  draft.type = type
  draft.id = id
  draft.originalId = originalId
  draft.base = typeof data.base === 'string' ? data.base : ''
  draft.provenance = data.provenance
  draft.own = {}
  draft.data = {}
  for (const field of t?.fields ?? []) {
    draft.own[field.name] = !draft.base || field.name in data
    draft.data[field.name] = structuredClone(field.name in data ? data[field.name] : resolved[field.name] ?? defaultFor(field))
  }
  saveError.value = ''
  snapshot.value = JSON.stringify(draftOut())
}

function select(type: string, id: string): void {
  const r = find(type, id)
  if (!r) return
  selected.value = { type, id }
  load(type, r.data, r.resolved, id, id)
}

function startNew(type: string): void {
  const t = typeOf(type)
  if (!t) return
  selected.value = { type, id: '' }
  load(type, structuredClone(t.defaults), t.defaults, '', '')
}

function duplicate(): void {
  const r = find(draft.type, draft.originalId)
  if (!r) return
  selected.value = { type: draft.type, id: '' }
  load(draft.type, structuredClone(r.data), r.resolved, uniqueId(r.id + '-copy'), '')
  snapshot.value = ''
}

function uniqueId(base: string): string {
  let id = base
  for (let n = 2; ids.value[draft.type]?.includes(id); n++) id = `${base}-${n}`
  return id
}

// The record as it will be written: its own fields, plus base and provenance.
function draftOut(): Record<string, unknown> {
  const out: Record<string, unknown> = {}
  if (draft.base) out.base = draft.base
  for (const field of currentType.value?.fields ?? []) {
    if (!draft.base || draft.own[field.name]) out[field.name] = draft.data[field.name]
  }
  if (draft.provenance !== undefined) out.provenance = draft.provenance
  return out
}

function setOwn(name: string, own: boolean): void {
  draft.own[name] = own
  if (!own) {
    const field = currentType.value?.fields.find((f) => f.name === name)
    draft.data[name] = structuredClone(baseRecord.value?.resolved[name] ?? (field ? defaultFor(field) : ''))
  }
}

function save(): void {
  saveError.value = ''
  sendToHost({ type: 'records:save', recordType: draft.type, id: draft.id.trim(), originalId: draft.originalId, data: draftOut() })
}

function revert(): void {
  if (draft.originalId) select(draft.type, draft.originalId)
  else selected.value = null
}

async function remove(): Promise<void> {
  const r = find(draft.type, draft.originalId)
  if (!r) return
  const users = r.usedBy.length ? `\n\nStill used by: ${r.usedBy.join(', ')}` : ''
  if (!(await confirmDialog({ message: `Delete ${r.type} "${label(r)}" (${r.path})? This deletes its file.${users}`, confirmLabel: 'Delete', danger: true }))) return
  sendToHost({ type: 'records:delete', recordType: r.type, id: r.id })
  selected.value = null
}

function onKeydown(event: KeyboardEvent): void {
  if ((event.ctrlKey || event.metaKey) && event.key === 's') {
    event.preventDefault()
    save()
  }
}

function goTo(key: string): void {
  const [type, id] = key.split('/')
  if (type && id) select(type, id)
}
</script>

<template>
  <div class="records">
    <aside class="list">
      <input v-model="query" class="search" placeholder="Filter records…" spellcheck="false" />
      <div class="groups">
        <p v-if="!types.length" class="muted pad">No record types.</p>
        <div v-for="group in groups" :key="group.type.name" class="group">
          <div class="group-head" :title="group.type.description">
            <button class="toggle" @click="collapsed[group.type.name] = !collapsed[group.type.name]">
              {{ collapsed[group.type.name] ? '▸' : '▾' }} {{ group.type.name }}
            </button>
            <span class="count">{{ group.records.length }}</span>
            <button class="add" :title="`New ${group.type.name}`" @click="startNew(group.type.name)">+</button>
          </div>
          <template v-if="!collapsed[group.type.name]">
            <button
              v-for="r in group.records"
              :key="r.type + '/' + r.id"
              class="row"
              :class="{ active: selected?.type === r.type && selected?.id === r.id }"
              :title="r.path"
              @click="select(r.type, r.id)"
            >
              <span class="name">{{ label(r) }}</span>
              <span v-if="r.data.base" class="tag">: {{ r.data.base }}</span>
              <span v-if="problemsFor(r.type + '/' + r.id).length" class="warn" :title="problemsFor(r.type + '/' + r.id).join('\n')">⚠</span>
            </button>
          </template>
        </div>
      </div>
    </aside>

    <section v-if="!selected" class="editor empty">
      <p>
        Records are the game's data that isn't placed in a scene: items, and the other types listed here. Pickups,
        inventories, loot and scripts refer to records by id; the AI can read and write them.
      </p>
      <p class="muted">Pick a record, or + to add one. Each is a file in <code>records/&lt;type&gt;/</code>.</p>
      <ul v-if="problems.length" class="problems">
        <li v-for="p in problems" :key="p.key + p.message">
          <button class="link" @click="goTo(p.key)">{{ p.key }}</button>: {{ p.message }}
        </li>
      </ul>
    </section>

    <section v-else class="editor" @keydown="onKeydown">
      <div class="form-row">
        <label>Type</label>
        <span class="type-name" :title="currentType?.description">{{ draft.type }}</span>
        <label class="narrow">Id</label>
        <input v-model="draft.id" spellcheck="false" :title="`File name: records/${draft.type}/<id>.json`" placeholder="lower-case-id" />
      </div>
      <div class="form-row">
        <label title="Another record of this type whose fields this one inherits">Base</label>
        <select v-model="draft.base" class="wide">
          <option value="">— none —</option>
          <option v-for="id in (ids[draft.type] ?? []).filter((i) => i !== draft.originalId)" :key="id" :value="id">{{ id }}</option>
        </select>
      </div>
      <div v-for="field in currentType?.fields ?? []" :key="field.name" class="form-row top">
        <label :title="field.description">
          {{ field.name }}<span v-if="field.required" class="req">*</span>
        </label>
        <input
          v-if="draft.base"
          type="checkbox"
          class="own"
          :checked="draft.own[field.name]"
          :title="draft.own[field.name] ? 'Set here (untick to inherit from the base)' : 'Inherited from the base (tick to set here)'"
          @change="setOwn(field.name, ($event.target as HTMLInputElement).checked)"
        />
        <RecordValue
          :field="field"
          :value="draft.data[field.name]"
          :ids="ids"
          :disabled="!!draft.base && !draft.own[field.name]"
          @update="draft.data[field.name] = $event"
        />
      </div>
      <ul v-if="draft.originalId && problemsFor(draft.type + '/' + draft.originalId).length" class="problems">
        <li v-for="message in problemsFor(draft.type + '/' + draft.originalId)" :key="message">{{ message }}</li>
      </ul>
      <div v-if="saveError" class="problems">{{ saveError }}</div>
      <div v-if="draft.originalId && find(draft.type, draft.originalId)?.usedBy.length" class="muted">
        Used by:
        <button v-for="key in find(draft.type, draft.originalId)?.usedBy" :key="key" class="link" @click="goTo(key)">{{ key }}</button>
      </div>
      <div class="actions">
        <button class="primary" :disabled="!draft.id.trim()" title="Save (Ctrl+S)" @click="save">
          {{ draft.originalId ? 'Save' : 'Create' }}
        </button>
        <button :disabled="!dirty && !!draft.originalId" @click="revert">{{ draft.originalId ? 'Revert' : 'Cancel' }}</button>
        <span v-if="dirty" class="muted">Unsaved changes</span>
        <button v-if="draft.originalId" :disabled="dirty" title="A copy under a new id" @click="duplicate">Duplicate</button>
        <span class="spacer"></span>
        <span v-if="draft.originalId" class="muted path">records/{{ draft.type }}/{{ draft.originalId }}.json</span>
        <button v-if="draft.originalId" class="danger" @click="remove">Delete…</button>
      </div>
    </section>
  </div>
</template>

<style scoped>
.records {
  display: flex;
  min-height: 0;
  height: 100%;
}
.list {
  display: flex;
  flex-direction: column;
  width: 240px;
  flex-shrink: 0;
  border-right: 1px solid #2a2a3a;
  min-height: 0;
}
.search {
  margin: 6px;
}
.groups {
  flex: 1;
  overflow-y: auto;
}
.group-head {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 4px 6px;
  font-size: 0.8em;
  text-transform: uppercase;
  letter-spacing: 0.04em;
  opacity: 0.75;
}
.group-head .toggle {
  border: none;
  background: none;
  padding: 0;
  font: inherit;
  text-transform: inherit;
  color: inherit;
  cursor: pointer;
}
.count {
  opacity: 0.6;
}
.add {
  margin-left: auto;
  width: 20px;
  height: 18px;
  padding: 0;
}
.row {
  display: flex;
  align-items: center;
  gap: 6px;
  width: 100%;
  padding: 3px 6px 3px 18px;
  border: none;
  background: none;
  color: inherit;
  font: inherit;
  text-align: left;
  cursor: pointer;
}
.row:hover {
  background: #262637;
}
.row.active {
  background: #313148;
}
.name {
  flex: 1;
  min-width: 0;
  overflow: hidden;
  white-space: nowrap;
  text-overflow: ellipsis;
}
.tag {
  font-size: 0.75em;
  opacity: 0.55;
}
.warn {
  color: #f59e0b;
}
.editor {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 6px;
  padding: 8px 10px;
  overflow-y: auto;
}
.editor.empty {
  max-width: 560px;
}
.form-row {
  display: flex;
  align-items: center;
  gap: 8px;
}
.form-row.top {
  align-items: flex-start;
}
.form-row label {
  width: 90px;
  flex-shrink: 0;
  font-size: 0.85em;
  opacity: 0.75;
  padding-top: 3px;
}
.form-row label.narrow {
  width: auto;
}
.type-name {
  font-weight: 600;
}
.own {
  margin-top: 4px;
}
.req {
  color: #f59e0b;
}
.wide {
  flex: 1;
}
.actions {
  display: flex;
  align-items: center;
  gap: 6px;
  margin-top: 4px;
}
.spacer {
  flex: 1;
}
.path {
  font-size: 0.8em;
}
.problems {
  margin: 0;
  padding: 6px 6px 6px 22px;
  border: 1px solid #f59e0b55;
  background: #f59e0b14;
  color: #fcd34d;
}
.muted {
  opacity: 0.6;
}
.pad {
  padding: 6px;
}
.link {
  border: none;
  background: none;
  color: #60a5fa;
  padding: 0 2px;
  font: inherit;
  cursor: pointer;
}
</style>
