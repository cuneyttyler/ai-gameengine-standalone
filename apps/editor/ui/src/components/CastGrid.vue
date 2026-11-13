<script setup lang="ts">
// The cast timeline as a grid (planner ⑥b, and the build's Cast tab): a tab per quest (the main quest, then each side
// quest), a column for the start and for each of its stages, a row for its cutscenes and one per character. Each cell is
// short (what changes then, the cutscenes they're in; — not changed); a click shows it in full below. In the build each
// cell also says how it checked (✓ verified, ✗ failed and why, • built, to do) with Fix.
import { computed, ref, watch } from 'vue'

export interface CastCell {
  short: string // what the grid shows
  lines: { label: string; value: string }[] // what the detail shows
  cuts?: string[] // the cutscenes they're in then
  status?: string
  checks?: { check: string; ok: boolean | null; why?: string; played?: boolean }[]
  id?: string // its requirement (the build's)
  failed?: string
}
export interface CastRow {
  who: string
  name: string
  cells: Record<string, CastCell>
}
export interface CastQuest {
  id: string
  name?: string
  type?: string
  stages: { index: number; journal?: string; scene?: string }[]
}
const props = defineProps<{ quests: CastQuest[]; rows: CastRow[]; cutscenes?: CastRow; fixing?: boolean }>()
const emit = defineEmits<{ (e: 'fix', id: string): void }>()

const questId = ref('')
watch(() => props.quests.map((q) => q.id).join(','), () => {
  if (!props.quests.some((q) => q.id === questId.value)) questId.value = props.quests[0]?.id ?? ''
}, { immediate: true })
const quest = computed(() => props.quests.find((q) => q.id === questId.value))
// The start, then each stage as it begins (and once it's done, where anything changes then).
const columns = computed(() => {
  const q = quest.value
  const cols: { key: string; label: string; title: string }[] = [{ key: 'start', label: 'Start', title: 'Before the first quest stage' }]
  if (!q) return cols
  const used = new Set<string>()
  for (const r of [...props.rows, ...(props.cutscenes ? [props.cutscenes] : [])]) for (const k of Object.keys(r.cells)) used.add(k)
  for (const s of [...q.stages].sort((a, b) => a.index - b.index)) {
    cols.push({ key: `${q.id}:${s.index}:enter`, label: `${s.index}`, title: `Entering stage ${s.index}${s.journal ? ': ' + s.journal : ''}${s.scene ? ' (' + s.scene + ')' : ''}` })
    if (used.has(`${q.id}:${s.index}:complete`)) {
      cols.push({ key: `${q.id}:${s.index}:complete`, label: `${s.index} done`, title: `Once stage ${s.index} is done` })
    }
  }
  return cols
})
// A row is shown in a quest's tab when anything of it is in that quest (or at the start).
const shownRows = computed(() => props.rows.filter((r) => Object.keys(r.cells).some((k) => k === 'start' || k.startsWith(questId.value + ':'))))
const questCount = (id: string) => {
  let n = 0
  for (const r of props.rows) for (const k of Object.keys(r.cells)) if (k.startsWith(id + ':')) n++
  return n
}
const questFailed = (id: string) => {
  let n = 0
  for (const r of [...props.rows, ...(props.cutscenes ? [props.cutscenes] : [])]) {
    for (const [k, c] of Object.entries(r.cells)) if (k.startsWith(id + ':') && c.status === 'failed') n++
  }
  return n
}

const picked = ref<{ row: string; col: string } | null>(null)
const pickedCell = computed(() => {
  if (!picked.value) return null
  const row = picked.value.row === '#cutscenes' ? props.cutscenes : props.rows.find((r) => r.who === picked.value!.row)
  const cell = row?.cells[picked.value.col]
  const col = columns.value.find((c) => c.key === picked.value!.col)
  return row && cell ? { row, cell, col } : null
})
function pick(row: string, col: string) {
  picked.value = picked.value?.row === row && picked.value?.col === col ? null : { row, col }
}
function mark(c?: CastCell): string {
  return !c ? '' : c.status === 'verified' ? '✓' : c.status === 'failed' ? '✗' : c.status === 'built' ? '•' : ''
}
</script>

<template>
  <div class="cast">
    <div class="tabs">
      <button v-for="q in quests" :key="q.id" :class="{ on: q.id === questId }" :title="q.name" @click="questId = q.id; picked = null">
        {{ q.type === 'main' || q.id === 'q-main' ? 'Main quest' : q.name || q.id }}
        <span v-if="questFailed(q.id)" class="tab-count err">{{ questFailed(q.id) }} ✗</span>
        <span v-else-if="questCount(q.id)" class="tab-count">{{ questCount(q.id) }}</span>
      </button>
    </div>
    <div class="scroll">
      <table class="grid">
        <thead>
          <tr>
            <th class="who">Character</th>
            <th v-for="c in columns" :key="c.key" :title="c.title">{{ c.label }}</th>
          </tr>
        </thead>
        <tbody>
          <tr v-if="cutscenes" class="cuts">
            <td class="who"><b>Cutscenes</b></td>
            <td v-for="c in columns" :key="c.key" :class="{ on: picked?.row === '#cutscenes' && picked?.col === c.key, err: cutscenes.cells[c.key]?.status === 'failed', ok: cutscenes.cells[c.key]?.status === 'verified', empty: !cutscenes.cells[c.key] }"
              @click="cutscenes.cells[c.key] && pick('#cutscenes', c.key)">
              <template v-if="cutscenes.cells[c.key]"><span class="mark">{{ mark(cutscenes.cells[c.key]) }}</span>▶ {{ cutscenes.cells[c.key]!.short }}</template>
            </td>
          </tr>
          <tr v-for="r in shownRows" :key="r.who">
            <td class="who"><b>{{ r.name }}</b></td>
            <td v-for="c in columns" :key="c.key" :class="{ on: picked?.row === r.who && picked?.col === c.key, err: r.cells[c.key]?.status === 'failed', ok: r.cells[c.key]?.status === 'verified', empty: !r.cells[c.key] }"
              :title="r.cells[c.key] ? 'Click for the detail' : 'not changed'" @click="r.cells[c.key] && pick(r.who, c.key)">
              <template v-if="r.cells[c.key]">
                <span class="mark">{{ mark(r.cells[c.key]) }}</span>{{ r.cells[c.key]!.short }}
                <span v-if="r.cells[c.key]!.cuts?.length" class="cut" :title="'In: ' + r.cells[c.key]!.cuts!.join(', ')">▶</span>
              </template>
              <template v-else>—</template>
            </td>
          </tr>
        </tbody>
      </table>
    </div>
    <!-- The picked cell in full -->
    <div v-if="pickedCell" class="detail">
      <div class="row">
        <b>{{ pickedCell.row.name }}</b><span class="muted">&nbsp;· {{ pickedCell.col?.title }}</span>
        <span class="spacer"></span>
        <span v-if="pickedCell.cell.status" :class="{ ok: pickedCell.cell.status === 'verified', err: pickedCell.cell.status === 'failed' }">{{ pickedCell.cell.status }}</span>
        <button v-if="pickedCell.cell.status === 'failed' && pickedCell.cell.id" :disabled="fixing"
          title="The AI decides how to make it so (a stage effect, an AI package, dialogue, a script, the cutscene) and does it"
          @click="emit('fix', pickedCell.cell.id!)">Fix</button>
        <button class="link" @click="picked = null">close</button>
      </div>
      <table class="grid lines">
        <tbody>
          <tr v-for="(l, i) in pickedCell.cell.lines" :key="i"><td class="label">{{ l.label }}</td><td class="wide">{{ l.value }}</td></tr>
          <tr v-if="pickedCell.cell.cuts?.length"><td class="label">Cutscenes</td><td class="wide">{{ pickedCell.cell.cuts.join(' · ') }}</td></tr>
          <tr v-for="(k, i) in pickedCell.cell.checks ?? []" :key="'k' + i">
            <td class="label">check</td>
            <td class="wide" :class="{ ok: k.ok === true, err: k.ok === false }">
              {{ k.ok === true ? '✓' : k.ok === false ? '✗' : '?' }} {{ k.check }}{{ k.played ? ' (played)' : '' }}{{ k.why ? ': ' + k.why : k.ok === null ? ': a test tells' : '' }}</td>
          </tr>
          <tr v-if="pickedCell.cell.failed"><td class="label">failed</td><td class="wide err">{{ pickedCell.cell.failed }}</td></tr>
        </tbody>
      </table>
    </div>
  </div>
</template>

<style scoped>
.cast .scroll {
  overflow-x: auto;
  overflow-y: visible;
  max-width: 100%;
  padding-bottom: 6px; /* room for the scrollbar */
}
.cast table.grid {
  border-collapse: collapse;
  width: max-content;
}
.cast td,
.cast th {
  white-space: nowrap;
  max-width: 180px;
  overflow: hidden;
  text-overflow: ellipsis;
  vertical-align: top;
  font-size: 0.92em;
}
.cast th {
  text-align: left;
}
.cast td.who,
.cast th.who {
  position: sticky;
  left: 0;
  z-index: 1;
  background: var(--panel-bg, #1e1e2a);
}
.cast td {
  cursor: pointer;
}
.cast td.empty {
  cursor: default;
  opacity: 0.45;
}
.cast td.on {
  outline: 1px solid #6aa9ff;
  outline-offset: -1px;
}
.cast tr.cuts td {
  font-style: italic;
}
.cast .mark {
  font-weight: bold;
  margin-right: 3px;
}
.cast .cut {
  margin-left: 4px;
  opacity: 0.8;
}
.cast .tab-count {
  margin-left: 4px;
  opacity: 0.75;
  font-size: 0.9em;
}
.cast .detail {
  margin-top: 8px;
  border: 1px solid #2a2a3a;
  padding: 6px 8px;
}
.cast .detail td.label {
  width: 110px;
  opacity: 0.7;
  white-space: nowrap;
}
.cast .detail td.wide {
  white-space: normal;
  max-width: none;
  cursor: default;
}
</style>
