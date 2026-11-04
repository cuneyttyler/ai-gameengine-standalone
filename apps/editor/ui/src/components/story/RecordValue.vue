<script setup lang="ts">
// One record field's editor (development_plan.md 12.1), generated from the
// record type's schema like the Inspector's fields: values, references to
// other records, lists and objects (which nest this same editor).
import { computed } from 'vue'
import { defaultFor, fieldShown, type RecordFieldSpec } from '../../records'
import SearchSelect from '../SearchSelect.vue'
import { optionsFor } from '../../lookups'
import { editorState } from '../../editorState'

const props = defineProps<{
  field: RecordFieldSpec
  value: unknown
  ids: Record<string, string[]> // record type -> ids, for reference pickers
  disabled?: boolean
  root?: Record<string, unknown> // the whole record: which of an object's fields apply (their `when`)
}>()
const emit = defineEmits<{ update: [value: unknown] }>()

const single = computed<RecordFieldSpec>(() => ({ ...props.field, list: false }))
// Picked from a searchable list: scripts, paths, markers, named things, dialogues (RecordField::choices).
const picks = computed(() => {
  if (props.field.list) return null
  // A reference to another record: its type's ids.
  if (props.field.type === 'record') return (props.ids[props.field.recordType ?? ''] ?? []).map((id) => ({ value: id, hint: props.field.recordType }))
  return optionsFor((props.field as { choices?: string }).choices, props.field.type)
})
const items = computed(() => (Array.isArray(props.value) ? (props.value as unknown[]) : []))
const object = computed(() =>
  props.value && typeof props.value === 'object' && !Array.isArray(props.value)
    ? (props.value as Record<string, unknown>)
    : {},
)

function setItem(index: number, value: unknown): void {
  const next = [...items.value]
  next[index] = value
  emit('update', next)
}
function removeItem(index: number): void {
  const next = [...items.value]
  next.splice(index, 1)
  emit('update', next)
}
function addItem(): void {
  emit('update', [...items.value, defaultFor(props.field, true)])
}
function setSub(name: string, value: unknown): void {
  emit('update', { ...object.value, [name]: value })
}
function emitNumber(event: Event): void {
  const text = (event.target as HTMLInputElement).value
  const value = props.field.type === 'int' ? parseInt(text, 10) : Number(text)
  if (!Number.isNaN(value)) emit('update', value)
}
</script>

<template>
  <div v-if="field.list" class="list">
    <div v-for="(item, i) in items" :key="i" class="item">
      <RecordValue :field="single" :value="item" :ids="ids" :disabled="disabled" :root="root" @update="setItem(i, $event)" />
      <button class="remove" title="Remove" :disabled="disabled" @click="removeItem(i)">×</button>
    </div>
    <button class="add" :disabled="disabled" @click="addItem">+ Add</button>
  </div>
  <div v-else-if="field.type === 'object'" class="object">
    <div v-for="sub in (field.fields ?? []).filter((f) => fieldShown(f, root ?? {}))" :key="sub.name" class="sub">
      <label :title="sub.description">{{ sub.name }}<span v-if="sub.required" class="req">*</span></label>
      <RecordValue :field="sub" :value="object[sub.name]" :ids="ids" :disabled="disabled" :root="root" @update="setSub(sub.name, $event)" />
    </div>
  </div>
  <span v-else-if="picks" class="pick">
    <SearchSelect
      :model-value="(value as string) ?? ''"
      :options="picks"
      :disabled="disabled"
      custom
      empty="(none)"
      @update:model-value="emit('update', $event)"
    />
    <button v-if="field.type === 'script'" class="edit" :disabled="!value" title="Open it in the script editor"
            @click="editorState.scriptEditor = value as string">✎</button>
  </span>
  <select
    v-else-if="field.type === 'record'"
    :value="value ?? ''"
    :disabled="disabled"
    @change="emit('update', ($event.target as HTMLSelectElement).value)"
  >
    <option value="">— none —</option>
    <option v-for="id in ids[field.recordType ?? ''] ?? []" :key="id" :value="id">{{ id }}</option>
    <option v-if="value && !(ids[field.recordType ?? ''] ?? []).includes(value as string)" :value="value">
      {{ value }} (missing)
    </option>
  </select>
  <select
    v-else-if="field.type === 'enum'"
    :value="value"
    :disabled="disabled"
    @change="emit('update', ($event.target as HTMLSelectElement).value)"
  >
    <option v-for="option in field.options ?? []" :key="option" :value="option">{{ option }}</option>
  </select>
  <input
    v-else-if="field.type === 'bool'"
    type="checkbox"
    :checked="value === true"
    :disabled="disabled"
    @change="emit('update', ($event.target as HTMLInputElement).checked)"
  />
  <input
    v-else-if="field.type === 'float' || field.type === 'int'"
    type="number"
    class="number"
    :step="field.type === 'int' ? 1 : 0.05"
    :min="field.min"
    :max="field.max"
    :value="value as number"
    :disabled="disabled"
    @change="emitNumber"
  />
  <input
    v-else
    class="text"
    spellcheck="false"
    :value="(value as string) ?? ''"
    :placeholder="field.type === 'string' ? '' : field.type + ' path'"
    :disabled="disabled"
    @change="emit('update', ($event.target as HTMLInputElement).value)"
  />
</template>

<style scoped>
.pick {
  display: flex;
  gap: 4px;
  flex: 1;
  min-width: 0;
}
.pick > :first-child {
  flex: 1;
  min-width: 0;
}
.edit {
  height: 22px;
  min-width: 24px;
  padding: 0 4px;
}
.list,
.object {
  display: flex;
  flex-direction: column;
  gap: 3px;
  flex: 1;
  min-width: 0;
}
.object {
  border-left: 2px solid #2a2a3a;
  padding-left: 6px;
}
.item {
  display: flex;
  align-items: flex-start;
  gap: 4px;
}
.sub {
  display: flex;
  align-items: flex-start;
  gap: 6px;
}
.sub label {
  width: 90px;
  flex-shrink: 0;
  font-size: 0.85em;
  opacity: 0.75;
  padding-top: 3px;
}
.req {
  color: #f59e0b;
}
.remove {
  width: 22px;
  padding: 0;
}
.add {
  align-self: flex-start;
}
.number {
  width: 90px;
}
.text {
  flex: 1;
  min-width: 0;
}
</style>
