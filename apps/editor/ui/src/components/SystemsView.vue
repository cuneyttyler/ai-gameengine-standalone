<script setup lang="ts">
// ⑦ Systems, for reading: each part of the design (economy, progression, difficulty, ...) as a
// card, and the records grouped by type as cards with their name, description, key values as
// chips and the rest of their fields below.
import { computed, ref } from 'vue'
import DataTable from './DataTable.vue'
import DocView from './DocView.vue'

interface SystemRecord {
  type: string
  id: string
  record?: Record<string, unknown>
}
const props = defineProps<{ systems: { summary?: Record<string, unknown>; records?: SystemRecord[] } }>()

const parts = computed(() => Object.entries(props.systems.summary ?? {}).filter(([, v]) => v !== null && v !== undefined && v !== ''))

// Records by type, in the order types first appear.
const groups = computed(() => {
  const out = new Map<string, SystemRecord[]>()
  for (const r of props.systems.records ?? []) {
    if (!out.has(r.type)) out.set(r.type, [])
    out.get(r.type)!.push(r)
  }
  return [...out.entries()]
})
const folded = ref<Record<string, boolean>>({})
function toggle(key: string) {
  folded.value = { ...folded.value, [key]: !folded.value[key] }
}

// "uniqueItems" -> "Unique items".
function label(key: string): string {
  const words = key.replace(/([a-z])([A-Z])/g, '$1 $2').replace(/[-_]/g, ' ')
  return words.charAt(0).toUpperCase() + words.slice(1).toLowerCase()
}
function plural(type: string, n: number): string {
  const name = label(type)
  return n === 1 ? name : name.endsWith('y') ? name.slice(0, -1) + 'ies' : name + 's'
}

// What a record card shows apart: its name, description, tags and short values as chips.
const shown = new Set(['name', 'description', 'icon', 'tags'])
function title(r: SystemRecord): string {
  const name = r.record?.name
  return typeof name === 'string' && name ? name : r.id
}
function chips(r: SystemRecord): [string, string][] {
  const out: [string, string][] = []
  for (const [k, v] of Object.entries(r.record ?? {})) {
    if (shown.has(k)) continue
    if (typeof v === 'number') out.push([label(k), String(v)])
    else if (typeof v === 'boolean') {
      if (v) out.push([label(k), ''])
    } else if (typeof v === 'string' && v && v.length <= 24) out.push([label(k), v])
  }
  return out
}
function rest(r: SystemRecord): Record<string, unknown> {
  const out: Record<string, unknown> = {}
  for (const [k, v] of Object.entries(r.record ?? {})) {
    if (shown.has(k) || typeof v === 'number' || typeof v === 'boolean' || (typeof v === 'string' && v.length <= 24)) continue
    out[k] = v
  }
  return out
}
function tags(r: SystemRecord): string[] {
  const t = r.record?.tags
  return Array.isArray(t) ? t.map(String) : []
}
</script>

<template>
  <div class="systems">
    <section v-if="parts.length">
      <h4 class="head" @click="toggle('__design')">
        <span class="caret">{{ folded.__design ? '▸' : '▾' }}</span> The design <span class="count">{{ parts.length }}</span>
      </h4>
      <div v-if="!folded.__design" class="stack">
        <div v-for="[key, value] in parts" :key="key" class="card part">
          <div class="card-title">{{ label(key) }}</div>
          <DataTable :value="value" />
        </div>
      </div>
    </section>

    <section v-for="[type, records] in groups" :key="type">
      <h4 class="head" @click="toggle(type)">
        <span class="caret">{{ folded[type] ? '▸' : '▾' }}</span> {{ plural(type, records.length) }} <span class="count">{{ records.length }}</span>
      </h4>
      <div v-if="!folded[type]" class="cards">
        <div v-for="r in records" :key="r.id" class="card record">
          <div class="record-head">
            <span class="card-title">{{ title(r) }}</span>
            <span class="id">{{ r.id }}</span>
          </div>
          <p v-if="typeof r.record?.description === 'string'" class="description">{{ r.record.description }}</p>
          <div v-if="chips(r).length" class="chips">
            <span v-for="[k, v] in chips(r)" :key="k" class="chip">
              <span class="chip-key">{{ k }}</span><span v-if="v" class="chip-value">{{ v }}</span>
            </span>
          </div>
          <div v-if="tags(r).length" class="tags">
            <span v-for="t in tags(r)" :key="t" class="tag">#{{ t }}</span>
          </div>
          <div v-if="Object.keys(rest(r)).length" class="more"><DocView :value="rest(r)" :depth="1" /></div>
        </div>
      </div>
    </section>
  </div>
</template>

<style scoped>
.systems {
  display: flex;
  flex-direction: column;
  gap: 14px;
}
.head {
  margin: 0 0 8px;
  padding: 4px 0;
  border-bottom: 1px solid #2a2a3a;
  cursor: pointer;
  user-select: none;
  display: flex;
  align-items: center;
  gap: 6px;
}
.head:hover {
  color: #c7d2fe;
}
.caret {
  width: 12px;
  color: #8a8aa0;
}
.count {
  font-size: 0.75em;
  color: #8a8aa0;
  border: 1px solid #3a3a50;
  padding: 0 5px;
}
.cards {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(260px, 1fr));
  gap: 8px;
}
/* The design's parts, one under the other, full width. */
.stack {
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.card {
  background: #1c1c28;
  border: 1px solid #2a2a3a;
  border-left: 3px solid #3b82f6;
  padding: 8px 10px;
  display: flex;
  flex-direction: column;
  gap: 6px;
  min-width: 0;
}
.card.part {
  border-left-color: #8b5cf6;
}
.card-title {
  font-weight: 600;
  color: #e4e4f0;
}
.record-head {
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  gap: 8px;
}
.id {
  font-family: monospace;
  font-size: 0.75em;
  color: #6a6a80;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.description {
  margin: 0;
  color: #b8b8cc;
  line-height: 1.4;
  font-size: 0.9em;
}
.chips,
.tags {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}
.chip {
  display: inline-flex;
  font-size: 0.78em;
  border: 1px solid #3a3a50;
}
.chip-key {
  padding: 1px 5px;
  color: #9a9ab0;
  background: #24243a;
}
.chip-value {
  padding: 1px 6px;
  color: #e4e4f0;
  font-variant-numeric: tabular-nums;
}
.tag {
  font-size: 0.75em;
  color: #93c5fd;
}
.more {
  border-top: 1px dashed #2a2a3a;
  padding-top: 6px;
  font-size: 0.9em;
}
</style>
