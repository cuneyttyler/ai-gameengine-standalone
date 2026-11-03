<script setup lang="ts">
// A list of items with counts ("hoe, seeds*2", the engine's text form), each picked from the project's items with a
// search: a Reaction's requires, takes, gives and accepts. Gold counts as an item ("gold*5").
import { computed, onMounted } from 'vue'
import { recordsByType, requestRecords } from '../recordIds'
import SearchSelect from './SearchSelect.vue'

const props = defineProps<{ value: unknown; disabled?: boolean; counts?: boolean }>()
const emit = defineEmits<{ (e: 'update', text: string): void }>()

onMounted(requestRecords)
const options = computed(() => [{ value: 'gold', label: 'Gold', hint: 'currency' }, ...(recordsByType.value.item ?? [])])

interface Entry {
  item: string
  count: number
}
const entries = computed<Entry[]>(() =>
  String(props.value ?? '')
    .split(',')
    .map((part) => part.trim())
    .filter(Boolean)
    .map((part) => {
      const star = part.indexOf('*')
      return star < 0 ? { item: part, count: 1 } : { item: part.slice(0, star).trim(), count: Math.max(1, parseInt(part.slice(star + 1), 10) || 1) }
    }),
)
function write(list: Entry[]): void {
  emit('update', list.filter((e) => e.item).map((e) => (e.count > 1 ? `${e.item}*${e.count}` : e.item)).join(', '))
}
function set(i: number, patch: Partial<Entry>): void {
  write(entries.value.map((e, k) => (k === i ? { ...e, ...patch } : e)))
}
</script>

<template>
  <div class="item-list">
    <div v-for="(e, i) in entries" :key="i" class="entry">
      <SearchSelect :model-value="e.item" :options="options" :disabled="disabled" custom @update:model-value="(v) => set(i, { item: v })" />
      <input v-if="counts !== false" class="count" type="number" min="1" :value="e.count" :disabled="disabled" title="How many"
             @change="set(i, { count: Number(($event.target as HTMLInputElement).value) })" />
      <button class="small" :disabled="disabled" title="Remove" @click="write(entries.filter((_, k) => k !== i))">×</button>
    </div>
    <button class="small" :disabled="disabled" @click="write([...entries, { item: options[1]?.value ?? 'gold', count: 1 }])">+ Item</button>
    <p v-if="!options.length || options.length === 1" class="muted">No items yet: create them in Project ▸ Records.</p>
  </div>
</template>

<style scoped>
.item-list { display: flex; flex-direction: column; gap: 3px; flex: 1; }
.entry { display: flex; gap: 4px; align-items: center; }
.count { width: 52px; }
.muted { opacity: 0.6; font-size: 0.85em; margin: 0; }
</style>
