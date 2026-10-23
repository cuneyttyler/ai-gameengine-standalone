<script setup lang="ts">
// A plan document for reading, not as JSON: fields with plain names, text as text, lists of
// words as chips, lists of things as cards, nested parts indented. The bookkeeping fields
// (revision, madeBy, inputs) stay out of the way. Recursive.
import { computed } from 'vue'

defineOptions({ name: 'DocView' })
const props = withDefaults(defineProps<{ value: unknown; depth?: number }>(), { depth: 0 })

// Bookkeeping, not content.
const hidden = new Set(['revision', 'madeBy', 'inputs', 'schema', 'locked', 'rationale', 'call', 'stubFrom'])

const kind = computed(() => {
  const v = props.value
  if (v === null || v === undefined || v === '') return 'empty'
  if (Array.isArray(v)) {
    if (v.length === 0) return 'empty'
    return v.every((x) => typeof x !== 'object' || x === null) ? 'words' : 'cards'
  }
  if (typeof v === 'object') return 'object'
  if (typeof v === 'boolean') return 'bool'
  if (typeof v === 'number') return 'number'
  return 'text'
})

const fields = computed(() =>
  Object.entries((props.value ?? {}) as Record<string, unknown>).filter(
    ([k, v]) => !hidden.has(k) && v !== null && v !== undefined && v !== '' && !(Array.isArray(v) && v.length === 0),
  ),
)

// "needsParts" -> "Needs parts"; "act-1" stays.
function label(key: string): string {
  const words = key.replace(/([a-z])([A-Z])/g, '$1 $2').replace(/[_]/g, ' ')
  return words.charAt(0).toUpperCase() + words.slice(1).toLowerCase()
}
// "character/elias-veyne" -> "elias-veyne" (the full id on hover).
function shortId(v: unknown): string {
  const s = String(v)
  return /^[a-z]+\/[\w-]+$/.test(s) ? s.slice(s.indexOf('/') + 1) : s
}
// A card's heading: its name, title or id, whichever it has.
function heading(o: Record<string, unknown>): string {
  for (const k of ['name', 'title', 'id', 'who', 'p']) {
    if (typeof o[k] === 'string' && o[k]) return shortId(o[k])
  }
  return ''
}
function rest(o: Record<string, unknown>): Record<string, unknown> {
  const h = heading(o)
  const out: Record<string, unknown> = {}
  for (const [k, v] of Object.entries(o)) {
    if (typeof v === 'string' && shortId(v) === h && ['name', 'title', 'id', 'who', 'p'].includes(k)) continue
    out[k] = v
  }
  return out
}
// A short value sits on the label's line; a long text gets its own lines.
function inline(v: unknown): boolean {
  if (typeof v === 'number' || typeof v === 'boolean') return true
  if (typeof v === 'string') return v.length <= 60
  return Array.isArray(v) && v.every((x) => typeof x !== 'object' || x === null) && v.join(', ').length <= 80
}
</script>

<template>
  <span v-if="kind === 'empty'" class="muted">—</span>
  <span v-else-if="kind === 'bool'">{{ value ? 'yes' : 'no' }}</span>
  <span v-else-if="kind === 'number'" class="num">{{ value }}</span>
  <span v-else-if="kind === 'text'" class="text" :title="String(value)">{{ shortId(value) }}</span>
  <span v-else-if="kind === 'words'" class="chips">
    <span v-for="(w, i) in value as unknown[]" :key="i" class="chip" :title="String(w)">{{ shortId(w) }}</span>
  </span>
  <div v-else-if="kind === 'cards'" class="cards">
    <div v-for="(c, i) in value as unknown[]" :key="i" class="card">
      <template v-if="c && typeof c === 'object' && !Array.isArray(c)">
        <div v-if="heading(c as Record<string, unknown>)" class="card-title">{{ heading(c as Record<string, unknown>) }}</div>
        <DocView :value="rest(c as Record<string, unknown>)" :depth="depth + 1" />
      </template>
      <DocView v-else :value="c" :depth="depth + 1" />
    </div>
  </div>
  <div v-else :class="['fields', { nested: depth > 0 }]">
    <div v-for="[k, v] in fields" :key="k" :class="['field', { block: !inline(v) }]">
      <span class="key">{{ label(k) }}</span>
      <DocView :value="v" :depth="depth + 1" />
    </div>
  </div>
</template>

<style scoped>
.fields {
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.fields.nested {
  padding-left: 10px;
  border-left: 2px solid #2a2a3a;
}
.field {
  display: flex;
  gap: 8px;
  align-items: baseline;
}
.field.block {
  flex-direction: column;
  gap: 2px;
}
.key {
  color: #8a8aa0;
  font-size: 0.85em;
  min-width: 110px;
  flex-shrink: 0;
}
.text {
  white-space: pre-wrap;
  line-height: 1.4;
}
.num {
  font-variant-numeric: tabular-nums;
}
.chips {
  display: flex;
  flex-wrap: wrap;
  gap: 3px;
}
.chip {
  padding: 0 6px;
  border: 1px solid #3a3a50;
  font-size: 0.85em;
}
.cards {
  display: flex;
  flex-direction: column;
  gap: 4px;
  width: 100%;
}
.card {
  border: 1px solid #2a2a3a;
  padding: 4px 8px;
  background: #1c1c28;
}
.card-title {
  font-weight: 600;
  margin-bottom: 2px;
}
.muted {
  color: #8a8aa0;
}
</style>
