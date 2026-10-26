<script setup lang="ts">
// A design value as tables: an object as field | value rows (a nested object as a table in its
// cell), a list of words as tags, a list of objects as a table with a column per field.
// Recursive.
import { computed } from 'vue'

defineOptions({ name: 'DataTable' })
const props = defineProps<{ value: unknown }>()

type Row = Record<string, unknown>
const isObject = (v: unknown): v is Row => typeof v === 'object' && v !== null && !Array.isArray(v)

const kind = computed(() => {
  const v = props.value
  if (v === null || v === undefined || v === '') return 'empty'
  if (Array.isArray(v)) {
    if (v.length === 0) return 'empty'
    if (v.every(isObject)) return 'rows'
    return v.every((x) => !isObject(x) && !Array.isArray(x)) ? 'tags' : 'list'
  }
  return isObject(v) ? 'fields' : 'scalar'
})
const fields = computed(() =>
  Object.entries(isObject(props.value) ? props.value : {}).filter(([, v]) => v !== null && v !== undefined && v !== ''),
)
// A list of objects: every field any of them has, in first-seen order.
const columns = computed(() => {
  const out: string[] = []
  for (const r of Array.isArray(props.value) ? (props.value as Row[]) : []) {
    for (const k of Object.keys(r)) if (!out.includes(k)) out.push(k)
  }
  return out
})

function label(key: string): string {
  const words = key.replace(/([a-z])([A-Z])/g, '$1 $2').replace(/[-_]/g, ' ')
  return words.charAt(0).toUpperCase() + words.slice(1).toLowerCase()
}
function scalar(v: unknown): string {
  if (typeof v === 'boolean') return v ? 'yes' : 'no'
  return String(v)
}
const simple = (v: unknown) => !isObject(v) && !Array.isArray(v)
</script>

<template>
  <span v-if="kind === 'empty'" class="muted">—</span>
  <span v-else-if="kind === 'scalar'" :class="{ num: typeof value === 'number' }">{{ scalar(value) }}</span>
  <span v-else-if="kind === 'tags'" class="tags">
    <span v-for="(t, i) in value as unknown[]" :key="i" class="tag">{{ scalar(t) }}</span>
  </span>
  <table v-else-if="kind === 'rows'" class="dt rows">
    <thead>
      <tr><th v-for="c in columns" :key="c">{{ label(c) }}</th></tr>
    </thead>
    <tbody>
      <tr v-for="(r, i) in value as Row[]" :key="i">
        <td v-for="c in columns" :key="c" :class="{ num: typeof r[c] === 'number' }">
          <template v-if="simple(r[c])">{{ r[c] === undefined || r[c] === null ? '' : scalar(r[c]) }}</template>
          <DataTable v-else :value="r[c]" />
        </td>
      </tr>
    </tbody>
  </table>
  <ul v-else-if="kind === 'list'" class="list">
    <li v-for="(x, i) in value as unknown[]" :key="i"><DataTable :value="x" /></li>
  </ul>
  <table v-else class="dt fields">
    <tbody>
      <tr v-for="[k, v] in fields" :key="k">
        <th>{{ label(k) }}</th>
        <td :class="{ num: typeof v === 'number', nested: !simple(v) }"><DataTable :value="v" /></td>
      </tr>
    </tbody>
  </table>
</template>

<style scoped>
.dt {
  border-collapse: collapse;
  width: 100%;
  font-size: 0.9em;
}
.dt th,
.dt td {
  border: 1px solid #2a2a3a;
  padding: 3px 8px;
  text-align: left;
  vertical-align: top;
}
.fields > tbody > tr > th {
  width: 160px;
  color: #9a9ab0;
  font-weight: normal;
  background: #20202e;
  white-space: nowrap;
}
.rows thead th {
  color: #9a9ab0;
  font-weight: normal;
  background: #24243a;
  white-space: nowrap;
}
.rows tbody tr:nth-child(even) td {
  background: #1f1f2c;
}
td.nested {
  padding: 4px;
}
.num {
  font-variant-numeric: tabular-nums;
  color: #e4e4f0;
}
.tags {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}
.tag {
  border: 1px solid #3a3a50;
  padding: 0 6px;
  font-size: 0.9em;
}
.list {
  margin: 0;
  padding-left: 16px;
}
.muted {
  color: #8a8aa0;
}
</style>
