<script setup lang="ts">
// Difficulty (development_plan.md 16.5, docs/difficulty.md): the levels and their
// category steps and modifiers, the category table, a preview of the selected
// entity at every level (with hand-set values per level), the combat preview,
// the curve over the game's scenes, and the level Play starts at. Levels and the
// table are records (records/difficulty, records/difficulty-table/default); the
// host computes every value with the engine's own model.
import { computed, onMounted, reactive, ref, watch } from 'vue'
import { onHostMessage, sendToHost } from '../ipc'
import { confirmDialog } from '../dialogs'
import { editorState } from '../editorState'
import { difficultyView } from '../paneltabs'

interface Modifier {
  target: string
  owner: string
  tag: string
  op: string
  value: number
}
interface Level {
  id: string
  name: string
  description: string
  order: number
  categories: Record<string, number>
  modifiers: Modifier[]
  xp: number
  permadeath: boolean
}
interface TableRow {
  category: string
  kind: string
  steps: number[]
}
interface PreviewField {
  field: string
  base: number
  kind: string
  category: string
  levels: { level: string; name: string; value: number; overridden: boolean; why: string[] }[]
}
interface CombatRow {
  level: string
  enemyHealth: number
  enemyDamage: number
  playerHealth: number
  playerDamage: number
  hitsToKillEnemy: number
  hitsToKillPlayer: number
  secondsToKillEnemy: number
  secondsToKillPlayer: number
}
interface Chapter {
  scene: string
  minLevel: number
  maxLevel: number
  enemyLevels: number[]
}

const levels = ref<Level[]>([])
const builtIn = ref(true)
const table = ref<TableRow[]>([])
const categories = ref<string[]>([])
const fields = ref<{ path: string; kind: string }[]>([])
const defaultLevel = ref('')
const playLevel = ref('')
const preview = ref<PreviewField[]>([])
const combat = ref<CombatRow[]>([])
const combatError = ref('')
const chapters = ref<Chapter[]>([])
const curveProblems = ref<{ scene: string; message: string }[]>([])
const section = ref<'levels' | 'table' | 'preview' | 'curve'>(difficultyView.section)
watch(() => difficultyView.section, (s) => (section.value = s))
watch(section, (s) => (difficultyView.section = s))
const selected = ref('')
const saveError = ref('')

const draft = reactive<Level & { originalId: string }>({
  id: '',
  originalId: '',
  name: '',
  description: '',
  order: 0,
  categories: {},
  modifiers: [],
  xp: 1,
  permadeath: false,
})

onHostMessage((message) => {
  if (message.type === 'difficulty:model') {
    levels.value = message.levels as Level[]
    builtIn.value = message.builtIn as boolean
    table.value = message.table as TableRow[]
    categories.value = message.categories as string[]
    fields.value = message.fields as { path: string; kind: string }[]
    defaultLevel.value = message.defaultLevel as string
    playLevel.value = (message.playLevel as string) || ''
    if (selected.value && levels.value.some((l) => l.id === selected.value)) pick(selected.value)
    else if (levels.value[0]) pick(levels.value[0].id)
  } else if (message.type === 'difficulty:preview') {
    if ((message.id as number) === editorState.selectedId) preview.value = message.fields as PreviewField[]
  } else if (message.type === 'difficulty:combat') {
    combat.value = message.rows as CombatRow[]
    combatError.value = (message.error as string) || ''
  } else if (message.type === 'difficulty:curve') {
    chapters.value = message.chapters as Chapter[]
    curveProblems.value = message.problems as { scene: string; message: string }[]
  } else if (message.type === 'records:saved') {
    if ((message.recordType as string)?.startsWith('difficulty')) {
      saveError.value = (message.error as string | null) ?? ''
      sendToHost({ type: 'difficulty:request' })
    }
  }
})

onMounted(() => sendToHost({ type: 'difficulty:request' }))

function refreshSelection(): void {
  preview.value = []
  combat.value = []
  if (editorState.selectedId === null) return
  sendToHost({ type: 'difficulty:preview', id: editorState.selectedId })
  sendToHost({ type: 'difficulty:combat', id: editorState.selectedId })
}
watch(() => editorState.selectedId, () => section.value === 'preview' && refreshSelection())
watch(section, (s) => {
  if (s === 'preview') refreshSelection()
  if (s === 'curve') sendToHost({ type: 'difficulty:curve' })
})

function pick(id: string): void {
  const level = levels.value.find((l) => l.id === id)
  if (!level) return
  selected.value = id
  const copy = JSON.parse(JSON.stringify(level)) as Level
  Object.assign(draft, copy, { originalId: copy.id })
  for (const c of categories.value) draft.categories[c] ??= 3
  saveError.value = ''
}

function levelData(level: Level): Record<string, unknown> {
  return {
    name: level.name,
    description: level.description,
    order: level.order,
    categories: level.categories,
    modifiers: level.modifiers,
    xp: level.xp,
    permadeath: level.permadeath,
  }
}

function saveLevel(): void {
  sendToHost({ type: 'records:save', recordType: 'difficulty', id: draft.id.trim(), originalId: draft.originalId, data: levelData(draft) })
  selected.value = draft.id.trim()
}

// The built-in five become the project's own records, to edit.
function adoptBuiltIn(): void {
  for (const level of levels.value) {
    sendToHost({ type: 'records:save', recordType: 'difficulty', id: level.id, originalId: '', data: levelData(level) })
  }
}

function newLevel(): void {
  const order = Math.max(0, ...levels.value.map((l) => l.order)) + 1
  Object.assign(draft, {
    id: `level-${order}`,
    originalId: '',
    name: 'New level',
    description: '',
    order,
    categories: Object.fromEntries(categories.value.map((c) => [c, 3])),
    modifiers: [],
    xp: 1,
    permadeath: false,
  })
  selected.value = ''
}

async function removeLevel(): Promise<void> {
  if (!draft.originalId) return
  if (!(await confirmDialog({ message: `Delete the level ${draft.name}?`, confirmLabel: 'Delete', danger: true }))) return
  sendToHost({ type: 'records:delete', recordType: 'difficulty', id: draft.originalId })
  selected.value = ''
  setTimeout(() => sendToHost({ type: 'difficulty:request' }), 100)
}

function saveTable(): void {
  sendToHost({ type: 'records:save', recordType: 'difficulty-table', id: 'default', originalId: '', data: { rows: table.value } })
}

function setPlayLevel(level: string): void {
  playLevel.value = level
  sendToHost({ type: 'difficulty:play', level })
}

function setOverride(field: string, level: string, text: string): void {
  if (editorState.selectedId === null) return
  const value = text.trim() === '' ? null : Number(text)
  if (value !== null && Number.isNaN(value)) return
  sendToHost({ type: 'difficulty:override', id: editorState.selectedId, field, level, value })
}

const stepWords = ['easiest', 'easier', 'as designed', 'harder', 'hardest']
const tableByCategory = computed(() => {
  const out: Record<string, TableRow[]> = {}
  for (const row of table.value) (out[row.category] ??= []).push(row)
  return out
})
const curveMax = computed(() => Math.max(10, ...chapters.value.map((c) => Math.max(c.maxLevel, ...c.enemyLevels))))
function problemsFor(scene: string): string[] {
  return curveProblems.value.filter((p) => p.scene === scene).map((p) => p.message)
}
function fmt(n: number): string {
  return Number.isInteger(n) ? String(n) : n.toFixed(1)
}
</script>

<template>
  <div class="difficulty ed-form">
    <div class="topbar">
      <div class="view-switch">
        <button :class="{ on: section === 'levels' }" @click="section = 'levels'">Levels</button>
        <button :class="{ on: section === 'table' }" @click="section = 'table'">Category table</button>
        <button :class="{ on: section === 'preview' }" @click="section = 'preview'">Preview</button>
        <button :class="{ on: section === 'curve' }" @click="section = 'curve'">Curve</button>
      </div>
      <span class="spacer"></span>
      <label class="muted small">Play at</label>
      <select :value="playLevel" title="The level Play starts at" @change="setPlayLevel(($event.target as HTMLSelectElement).value)">
        <option value="">default ({{ defaultLevel }})</option>
        <option v-for="l in levels" :key="l.id" :value="l.id">{{ l.name }}</option>
      </select>
    </div>

    <!-- Levels --------------------------------------------------------------->
    <div v-if="section === 'levels'" class="split">
      <aside class="list">
        <button v-for="l in levels" :key="l.id" class="row" :class="{ active: selected === l.id }" @click="pick(l.id)">
          <span class="name">{{ l.name }}</span>
          <span v-if="l.id === defaultLevel" class="muted small">default</span>
        </button>
        <button v-if="!builtIn" class="small-button" @click="newLevel">+ Level</button>
      </aside>
      <section class="editor">
        <p v-if="builtIn" class="note">
          The built-in levels (no <code>records/difficulty</code> yet).
          <button class="small-button" @click="adoptBuiltIn">Make them this project's levels</button>
        </p>
        <ul v-if="saveError" class="problems"><li>{{ saveError }}</li></ul>
        <div class="form-row">
          <label>Id</label><input v-model="draft.id" class="id" spellcheck="false" :disabled="builtIn" />
          <label class="inline">Name</label><input v-model="draft.name" class="grow" :disabled="builtIn" />
          <label class="inline">Order</label><input v-model.number="draft.order" type="number" class="num" :disabled="builtIn" />
        </div>
        <div class="form-row">
          <label>Description</label><input v-model="draft.description" class="grow" :disabled="builtIn" />
        </div>
        <div class="section-label">Category steps</div>
        <div v-for="c in categories" :key="c" class="step-row">
          <span class="cat">{{ c }}</span>
          <div class="steps">
            <button
              v-for="s in 5"
              :key="s"
              :class="{ on: draft.categories[c] === s }"
              :title="stepWords[s - 1]"
              :disabled="builtIn"
              @click="draft.categories[c] = s"
            >
              {{ s }}
            </button>
          </div>
          <span class="muted small">{{ stepWords[(draft.categories[c] ?? 3) - 1] }}</span>
        </div>
        <div class="section-label">Modifiers (on top of the steps)</div>
        <div v-for="(m, i) in draft.modifiers" :key="i" class="step-row">
          <select v-model="m.target" :disabled="builtIn">
            <option v-for="f in fields" :key="f.path" :value="f.path">{{ f.path }}</option>
          </select>
          <select v-model="m.owner" :disabled="builtIn">
            <option value="any">anyone's</option>
            <option value="player">the player's</option>
            <option value="enemy">enemies'</option>
            <option value="ally">allies'</option>
          </select>
          <input v-model="m.tag" class="id" placeholder="name contains (boss)" spellcheck="false" :disabled="builtIn" />
          <select v-model="m.op" :disabled="builtIn">
            <option value="mul">×</option>
            <option value="add">+</option>
            <option value="set">=</option>
            <option value="min">at least</option>
            <option value="max">at most</option>
          </select>
          <input v-model.number="m.value" type="number" step="0.1" class="num" :disabled="builtIn" />
          <button v-if="!builtIn" title="Remove" @click="draft.modifiers.splice(i, 1)">×</button>
        </div>
        <button
          v-if="!builtIn"
          class="small-button"
          @click="draft.modifiers.push({ target: fields[0]?.path ?? 'Health.max', owner: 'enemy', tag: '', op: 'mul', value: 1.2 })"
        >
          + Modifier
        </button>
        <div class="form-row">
          <label>Experience</label>
          <input v-model.number="draft.xp" type="number" step="0.05" min="0" class="num" :disabled="builtIn" />
          <span class="muted small">× rewards</span>
          <label class="inline toggle"><input v-model="draft.permadeath" type="checkbox" :disabled="builtIn" /> one life</label>
        </div>
        <div v-if="!builtIn" class="actions">
          <button class="primary" :disabled="!draft.id.trim()" @click="saveLevel">{{ draft.originalId ? 'Save' : 'Create' }}</button>
          <span class="spacer"></span>
          <button v-if="draft.originalId" @click="removeLevel">Delete…</button>
        </div>
        <p class="muted small">
          The default level, changing it during play, adaptive difficulty and how enemies' levels follow the player
          are in Project Settings ▸ Difficulty.
        </p>
      </section>
    </div>

    <!-- Category table ------------------------------------------------------->
    <section v-else-if="section === 'table'" class="editor">
      <p class="muted small">Multipliers per step (1 easiest … 5 hardest). A level picks a step per category.</p>
      <table class="grid">
        <thead>
          <tr><th>category</th><th>kind</th><th v-for="s in 5" :key="s">{{ s }}</th></tr>
        </thead>
        <tbody>
          <template v-for="(rows, category) in tableByCategory" :key="category">
            <tr v-for="row in rows" :key="category + row.kind">
              <td>{{ category }}</td>
              <td>{{ row.kind }}</td>
              <td v-for="(v, i) in row.steps" :key="i">
                <input v-model.number="row.steps[i]" type="number" step="0.05" class="num" />
              </td>
            </tr>
          </template>
        </tbody>
      </table>
      <div class="actions"><button class="primary" @click="saveTable">Save table</button></div>
    </section>

    <!-- Preview -------------------------------------------------------------->
    <section v-else-if="section === 'preview'" class="editor">
      <p v-if="editorState.selectedId === null" class="muted">Select an entity to see its values at every level.</p>
      <template v-else>
        <p v-if="!preview.length" class="muted">The selection has no fields difficulty scales.</p>
        <table v-else class="grid">
          <thead>
            <tr>
              <th>field</th><th>base</th>
              <th v-for="l in preview[0]!.levels" :key="l.level">{{ l.name }}</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="f in preview" :key="f.field">
              <td :title="f.category + ' · ' + f.kind">{{ f.field }}</td>
              <td>{{ fmt(f.base) }}</td>
              <td v-for="l in f.levels" :key="l.level" :title="l.why.join('\n') || 'unchanged'">
                <input
                  :value="fmt(l.value)"
                  class="num"
                  :class="{ overridden: l.overridden }"
                  @change="setOverride(f.field, l.level, ($event.target as HTMLInputElement).value)"
                />
                <button v-if="l.overridden" class="reset" title="Back to the computed value" @click="setOverride(f.field, l.level, '')">↺</button>
              </td>
            </tr>
          </tbody>
        </table>
        <p class="muted small">Type a value to set it by hand for that level only (a boss's Hard health); ↺ goes back.</p>
        <div class="section-label">Combat preview: the selection against the player</div>
        <p v-if="combatError" class="muted">{{ combatError }}</p>
        <table v-else-if="combat.length" class="grid">
          <thead>
            <tr><th>level</th><th>player hits to kill it</th><th>its hits to kill the player</th><th>seconds (you / it)</th></tr>
          </thead>
          <tbody>
            <tr v-for="r in combat" :key="r.level">
              <td>{{ levels.find((l) => l.id === r.level)?.name ?? r.level }}</td>
              <td>{{ r.hitsToKillEnemy }} <span class="muted small">({{ fmt(r.enemyHealth) }} hp, {{ fmt(r.playerDamage) }}/hit)</span></td>
              <td>{{ r.hitsToKillPlayer }} <span class="muted small">({{ fmt(r.playerHealth) }} hp, {{ fmt(r.enemyDamage) }}/hit)</span></td>
              <td>{{ fmt(r.secondsToKillEnemy) }} / {{ fmt(r.secondsToKillPlayer) }}</td>
            </tr>
          </tbody>
        </table>
      </template>
    </section>

    <!-- Curve ---------------------------------------------------------------->
    <section v-else class="editor">
      <p class="muted small">
        The scenes in the order the gateways lead from the start scene, with their Zone band (bar) and enemies' levels
        (dots). Bands should rise and not jump.
      </p>
      <div v-for="c in chapters" :key="c.scene" class="chapter">
        <span class="scene" :title="c.scene">{{ c.scene.replace('scenes/', '') }}</span>
        <div class="track">
          <div
            v-if="c.minLevel > 0"
            class="band"
            :style="{ left: ((c.minLevel - 1) / curveMax) * 100 + '%', width: ((c.maxLevel - c.minLevel + 1) / curveMax) * 100 + '%' }"
          ></div>
          <span v-for="(e, i) in c.enemyLevels" :key="i" class="dot" :style="{ left: ((e - 0.5) / curveMax) * 100 + '%' }"></span>
        </div>
        <span class="muted small">{{ c.minLevel > 0 ? `${c.minLevel}–${c.maxLevel}` : 'no zone' }}</span>
        <ul v-if="problemsFor(c.scene).length" class="problems">
          <li v-for="p in problemsFor(c.scene)" :key="p">{{ p }}</li>
        </ul>
      </div>
      <button class="small-button" @click="sendToHost({ type: 'difficulty:curve' })">Check again</button>
    </section>
  </div>
</template>

<style scoped>
.difficulty {
  display: flex;
  flex-direction: column;
  height: 100%;
  min-height: 0;
}
.topbar {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 6px 8px;
  border-bottom: 1px solid #2a2a3a;
}
.view-switch {
  display: flex;
}
.view-switch button {
  padding: 2px 10px;
}
.view-switch button.on,
.steps button.on {
  background: #313148;
  border-color: #3b82f6;
}
.split {
  display: flex;
  flex: 1;
  min-height: 0;
}
.list {
  width: 180px;
  flex-shrink: 0;
  overflow-y: auto;
  border-right: 1px solid #2a2a3a;
  display: flex;
  flex-direction: column;
}
.row {
  display: flex;
  gap: 6px;
  padding: 3px 8px !important;
  border: none !important;
  background: none !important;
  text-align: left;
}
.row:hover {
  background: #262637 !important;
}
.row.active {
  background: #313148 !important;
}
.name {
  flex: 1;
}
.editor {
  flex: 1;
  min-width: 0;
  overflow: auto;
  padding: 8px 10px;
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.form-row,
.step-row,
.actions {
  display: flex;
  align-items: center;
  gap: 6px;
  flex-wrap: wrap;
}
.form-row label {
  width: 80px;
  font-size: 0.85em;
  opacity: 0.75;
}
.form-row label.inline {
  width: auto;
  margin-left: 8px;
}
.cat {
  width: 80px;
  font-size: 0.9em;
}
.steps {
  display: flex;
}
.steps button {
  width: 28px;
  padding: 2px 0;
}
.id {
  width: 130px;
}
.grow {
  flex: 1;
  min-width: 160px;
}
.num {
  width: 60px;
}
.num.overridden {
  border-color: #fbbf24;
  color: #fbbf24;
}
.reset {
  padding: 0 4px;
}
.grid {
  border-collapse: collapse;
  font-size: 0.9em;
}
.grid th,
.grid td {
  border: 1px solid #2a2a3a;
  padding: 2px 6px;
  text-align: left;
  white-space: nowrap;
}
.section-label {
  margin-top: 6px;
  font-size: 0.8em;
  text-transform: uppercase;
  letter-spacing: 0.04em;
  opacity: 0.7;
}
.note {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}
.chapter {
  display: grid;
  grid-template-columns: 140px 1fr 70px;
  align-items: center;
  gap: 8px;
}
.chapter .problems {
  grid-column: 1 / -1;
}
.track {
  position: relative;
  height: 14px;
  background: #15151f;
  border: 1px solid #2a2a3a;
}
.band {
  position: absolute;
  top: 2px;
  bottom: 2px;
  background: #3b82f6;
  opacity: 0.6;
}
.dot {
  position: absolute;
  top: 3px;
  width: 6px;
  height: 6px;
  margin-left: -3px;
  background: #fbbf24;
}
.scene {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.problems {
  margin: 0;
  padding-left: 18px;
  color: #f59e0b;
  font-size: 0.9em;
}
.small-button {
  align-self: flex-start;
  font-size: 0.85em;
}
.spacer {
  flex: 1;
}
.muted {
  opacity: 0.6;
}
.small {
  font-size: 0.8em;
}
</style>
