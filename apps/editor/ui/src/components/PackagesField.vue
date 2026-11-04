<script setup lang="ts">
// Inspector ▸ Behaviour ▸ packages: the character's packages in order (the first whose schedule and conditions hold
// runs), each moved up or down, removed, or opened in the Records panel; another added from a searchable list.
import { computed, onMounted, ref } from 'vue'
import SearchSelect from './SearchSelect.vue'
import { lookups, requestLookups } from '../lookups'
import { panelTab } from '../paneltabs'
import { activatePanel } from '../layout'
import { editorState } from '../editorState'

const props = defineProps<{ value: string; disabled?: boolean }>()
const emit = defineEmits<{ change: [value: string] }>()

const list = computed(() => props.value.split(',').map((p) => p.trim()).filter(Boolean))
const adding = ref('')
onMounted(requestLookups)

const options = computed(() =>
  lookups.packages
    .filter((p) => !list.value.includes(p.id))
    .map((p) => ({ value: p.id, hint: [p.template, p.description].filter(Boolean).join(' · ') })),
)
const about = (id: string) => lookups.packages.find((p) => p.id === id)

function write(next: string[]): void {
  emit('change', next.join(', '))
}
function add(id: string): void {
  if (id && !list.value.includes(id)) write([...list.value, id])
  adding.value = ''
}
function move(i: number, by: number): void {
  const next = [...list.value]
  const j = i + by
  if (j < 0 || j >= next.length) return
  ;[next[i], next[j]] = [next[j]!, next[i]!]
  write(next)
}
function remove(i: number): void {
  write(list.value.filter((_, k) => k !== i))
}
function openRecord(id: string): void {
  panelTab.project = 'records'
  activatePanel('project')
  editorState.openRecord = { type: 'package', id }
}
</script>

<template>
  <div class="packages">
    <div v-for="(id, i) in list" :key="id" class="item" :class="{ missing: !about(id) }">
      <span class="order">{{ i + 1 }}</span>
      <span class="id" :title="about(id)?.description || (about(id) ? '' : 'No such package: make it in Project ▸ Records ▸ package')">{{ id }}</span>
      <span class="hint">{{ about(id)?.template ?? 'missing' }}</span>
      <button class="tiny" :disabled="disabled || i === 0" title="Earlier: checked before the ones below" @click="move(i, -1)">↑</button>
      <button class="tiny" :disabled="disabled || i === list.length - 1" title="Later" @click="move(i, 1)">↓</button>
      <button class="tiny" :disabled="!about(id)" title="Open it in Project ▸ Records" @click="openRecord(id)">✎</button>
      <button class="tiny" :disabled="disabled" title="Remove from this character" @click="remove(i)">×</button>
    </div>
    <SearchSelect
      :model-value="adding"
      :options="options"
      :disabled="disabled"
      placeholder="+ Add a package…"
      empty="+ Add a package…"
      @update:model-value="add"
    />
  </div>
</template>

<style scoped>
.packages {
  display: flex;
  flex-direction: column;
  gap: 2px;
  flex: 1;
  min-width: 0;
}
.item {
  display: flex;
  align-items: center;
  gap: 4px;
  padding: 1px 4px;
  border: 1px solid #2a2a3a;
  background: #1c1c25;
}
.item.missing .id {
  color: #f59e0b;
}
.order {
  font-size: 10px;
  opacity: 0.5;
  width: 12px;
}
.id {
  flex: 1;
  font-size: 12px;
  overflow: hidden;
  text-overflow: ellipsis;
}
.hint {
  font-size: 10px;
  opacity: 0.55;
}
.tiny {
  height: 18px;
  min-width: 18px;
  padding: 0 3px;
  font-size: 11px;
}
</style>
