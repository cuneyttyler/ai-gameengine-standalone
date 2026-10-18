<script setup lang="ts">
// The Build panel (development_plan.md 7.4, place-interaction.md A2): one
// build at a time, as a stepper: ① Request · ② Assets · ③ Plan · ④ Build ·
// ⑤ Result. Not modal: the viewport stays in view. The host (PlaceBuild)
// holds the Brief; every edit here goes back to it as (path, value).
import { computed, ref } from 'vue'
import { onHostMessage, sendToHost } from '../ipc'
import { activatePanel, setPanelVisible } from '../layout'
import CatalogThumb from './CatalogThumb.vue'
import { amountText, countRange, layoutSketch, parseRange, setShare, toggleShare, type Shares } from '../buildwindow'

type Rect = [number, number, number, number]
interface Thumb { id: string; path: string; rect: Rect }
interface Control {
  name: string
  type: string
  value: unknown
  options?: string[]
  min?: number
  max?: number
  unit?: string
  description?: string
}
interface Component {
  id: string
  name: string
  description: string
  level: string
  inBrief: boolean
  enabled: boolean
  amount: 'count' | 'density' | 'none'
  count: unknown
  density: string | null
  source: string | null
  thumbs: Thumb[]
  params?: Control[]
}
interface Mark { disputed: boolean; alternatives: unknown[]; readings: string[]; confidence: number }
interface Problem { where: string; message: string; warning: boolean }
interface PlanRow {
  component: string
  label: string
  slot: string
  essential: boolean
  chosen: string
  source: string
  reason: string
  candidates: { id: string; path: string; rect: Rect; name: string; fit: number; grade: string }[]
}
interface BuildWindow {
  round: number
  buildTypeName: string
  name: string
  size: { class: string | null; buildings: unknown; area: unknown; classes: { id: string; buildings: [number, number]; side: [number, number] }[] }
  layout: { value: Shares; options: string[]; description: string | null }
  walled: { value: string | null; options: string[] }
  features: { value: string[]; options: string[] }
  style: Record<string, { value: unknown; options?: string[] }>
  components: Component[]
  marks: Record<string, Mark>
  assumptions?: string[]
  questions?: string[]
  problems: Problem[]
  assetPlan?: { rows: PlanRow[]; summary: { project: number; generate: number } }
}
interface PlanRow {
  id: string
  module: string
  name: string
  level: string
  plots: unknown
  buildings: number
  area: unknown
  qualities: Record<string, unknown>
  parcels: { id: string; module: string; name: string; count: number }[]
  rationale: string
}
interface BuildState {
  id: string
  query: string
  stage: string
  round: number
  status: string
  errors: string[]
  name?: string
  window?: BuildWindow
  progress?: { zones: number; done: number }
  plan?: { rows: PlanRow[]; totals: { buildings: number; zones: number; area: unknown }; rationale: string }
  sketch?: { side: number; rects: { id: string; module: string; name: string; x: number; y: number; w: number; h: number }[] }
  problems?: Problem[]
  solved?: { stats: Record<string, number>; checks: { id: string; predicate: string; ok: boolean; detail?: unknown }[] }
}

const PROJECT_HOST = 'https://aige-project.local/'
const SOURCES = [
  { id: 'auto', label: 'auto', title: 'The project’s art when it fits, else generated (place-assets.md)' },
  { id: 'project', label: 'project', title: 'Only the project’s own art' },
  { id: 'library', label: 'AIGE library', title: 'The curated library (comes with 7.9)' },
  { id: 'web', label: 'web', title: 'Free art from the web, licence checked (comes with 7.9)' },
  { id: 'generate', label: 'generate', title: 'Made by the Art tool in the place’s style' },
]
const DENSITIES = ['none', 'rare', 'some', 'common', 'abundant']
const STEPS = [
  { id: 'request', label: '① Request' },
  { id: 'assets', label: '② Assets' },
  { id: 'plan', label: '③ Plan' },
  { id: 'build', label: '④ Build' },
  { id: 'result', label: '⑤ Result' },
]

const state = ref<BuildState | null>(null)
const query = ref('')
const showAll = ref(false)

onHostMessage((message) => {
  if (message.type === 'build:state') {
    state.value = message.stage ? (message as unknown as BuildState) : null
  } else if (message.type === 'build:open') {
    setPanelVisible('build', true)
    activatePanel('build')
  }
})
sendToHost({ type: 'build:request' })

const win = computed(() => state.value?.window ?? null)
const stage = computed(() => state.value?.stage ?? '')
const errors = computed(() => (win.value?.problems ?? []).filter((p) => !p.warning))
const warnings = computed(() => (win.value?.problems ?? []).filter((p) => p.warning))
const shownComponents = computed(() =>
  (win.value?.components ?? []).filter((c) => showAll.value || c.inBrief),
)
const activeStep = computed(() =>
  stage.value === 'assets' ? 'assets' : ['planning', 'plan', 'planned'].includes(stage.value) ? 'plan' : 'request',
)
const ZONE_COLOURS = ['#a6e3a1', '#fab387', '#cba6f7', '#f9e2af', '#94e2d5', '#f38ba8']
const planErrors = computed(() => (state.value?.problems ?? []).filter((p) => !p.warning))
const selectedZone = ref<string | null>(null)

function edit(path: string, value: unknown): void {
  sendToHost({ type: 'build:edit', path, value })
}
function mark(path: string): Mark | undefined {
  return win.value?.marks[path]
}
function markClass(path: string): Record<string, boolean> {
  const m = mark(path)
  return { disputed: !!m?.disputed, low: !!m && !m.disputed && m.confidence < 0.6 }
}
function show(value: unknown): string {
  if (value === null || value === undefined) return '—'
  if (Array.isArray(value)) return value.join('–')
  if (typeof value === 'object') return JSON.stringify(value)
  return String(value)
}
function problemFor(prefix: string): string {
  const p = (win.value?.problems ?? []).find((x) => x.where === prefix || x.where.startsWith(prefix + '.'))
  return p ? p.message : ''
}
function urlOf(path: string): string {
  return `${PROJECT_HOST}${path.split('/').map(encodeURIComponent).join('/')}`
}
function start(): void {
  if (query.value.trim()) sendToHost({ type: 'build:start', query: query.value.trim() })
}
function setBuildings(which: 0 | 1, text: string): void {
  const range = countRange(win.value?.size.buildings)
  const n = Math.max(1, Math.round(Number(text)))
  if (!Number.isFinite(n)) return
  range[which] = n
  if (range[0] > range[1]) range[which === 0 ? 1 : 0] = n
  edit('scale.buildings', { count: range })
}
function setCount(c: Component, text: string): void {
  const range = parseRange(text)
  if (range) edit(`components.${c.id}.count`, range[0] === range[1] ? range[0] : range)
}
function toggleFeature(f: string): void {
  const now = win.value?.features.value ?? []
  edit('features', now.includes(f) ? now.filter((x) => x !== f) : [...now, f])
}
function setParam(c: Component, p: Control, value: unknown): void {
  edit(`components.${c.id}.params.${p.name}`, value)
}
function toggleInSet(c: Component, p: Control, option: string): void {
  const now = Array.isArray(p.value) ? (p.value as string[]) : []
  setParam(c, p, now.includes(option) ? now.filter((x) => x !== option) : [...now, option])
}
function mixOf(p: Control): Record<string, number> {
  const v = p.value as Record<string, unknown> | null
  const weights = (v && typeof v === 'object' && 'mix' in v ? v.mix : v) as Record<string, number> | null
  return weights && typeof weights === 'object' ? weights : {}
}
function setMix(c: Component, p: Control, key: string, text: string): void {
  const mix = { ...mixOf(p) }
  const n = Number(text)
  if (!text.trim() || n <= 0) delete mix[key]
  else mix[key] = Math.min(1, n)
  // Weights add up to 1: the others share what's left in proportion.
  const others = Object.keys(mix).filter((k) => k !== key)
  const rest = Math.max(0, 1 - (mix[key] ?? 0))
  const total = others.reduce((s, k) => s + (mix[k] ?? 0), 0)
  for (const k of others) mix[k] = total > 0 ? Math.round(((mix[k] ?? 0) / total) * rest * 100) / 100 : Math.round((rest / others.length) * 100) / 100
  setParam(c, p, { mix })
}
function sizeText(value: unknown, side: 'w' | 'h'): string {
  const v = value as Record<string, unknown> | null
  return v && typeof v === 'object' ? show(v[side]) : ''
}
function setSize(c: Component, p: Control, side: 'w' | 'h', text: string): void {
  const range = parseRange(text)
  if (!range) return
  const now = { ...((p.value as Record<string, unknown>) ?? {}) }
  now[side] = range[0] === range[1] ? range[0] : range
  setParam(c, p, now)
}
</script>

<template>
  <div class="build">
    <div class="header">
      <div class="steps">
        <button
          v-for="s in STEPS"
          :key="s.id"
          :class="{ active: activeStep === s.id }"
          :disabled="s.id === 'plan' ? !['planning', 'plan', 'planned'].includes(stage) : !win || (s.id !== 'request' && s.id !== 'assets') || (s.id === 'assets' && stage === 'request' && errors.length > 0)"
          :title="s.id === 'plan' || s.id === 'build' || s.id === 'result' ? 'Comes with the next milestones (7.5–7.7)' : ''"
          @click="sendToHost({ type: 'build:step', step: s.id })"
        >
          {{ s.label }}
        </button>
      </div>
      <span v-if="state" class="title">{{ state.name || state.query }}</span>
    </div>

    <!-- No build yet, or the last one is over: ask for one. -->
    <div v-if="!state || stage === 'cancelled' || stage === 'failed'" class="body start">
      <p v-if="stage === 'failed'" class="error">{{ state?.errors.join(' · ') }}</p>
      <p v-else-if="stage === 'cancelled'" class="dim">The build was cancelled.</p>
      <p class="dim">Describe a place to build. You'll confirm the details before anything is made.</p>
      <div class="ask">
        <input v-model="query" placeholder="e.g. a large village by a river, with gardens and a market" @keydown.enter="start" />
        <button class="primary" :disabled="!query.trim()" @click="start">Start</button>
      </div>
    </div>

    <div v-else-if="stage === 'interpreting'" class="body start">
      <p>{{ state?.status || 'Reading your request…' }}</p>
      <p class="dim">“{{ state?.query }}”: three readings are drafted, then compared, so you see what the request leaves open.</p>
      <button @click="sendToHost({ type: 'build:cancel' })">Stop</button>
    </div>

    <div v-else-if="stage === 'planning'" class="body start">
      <p>{{ state?.status }}</p>
      <div v-if="state?.progress?.zones" class="bar"><i :style="{ width: `${(100 * state.progress.done) / state.progress.zones}%` }" /></div>
      <p class="dim">The zones are planned first, then each zone's parcels; gardens, yards and the like follow from the module rules.</p>
      <button @click="sendToHost({ type: 'build:cancel' })">Stop</button>
    </div>

    <!-- ③ Plan: what will be built, before anything is. -->
    <div v-else-if="(stage === 'plan' || stage === 'planned') && state?.plan" class="body plan-view">
      <p class="headline">
        {{ state.plan.totals.buildings }} buildings in {{ state.plan.totals.zones }} zones
        <span class="dim">· {{ amountText(state.plan.totals.area) }} cells²</span>
      </p>
      <p v-if="state.plan.rationale" class="dim">{{ state.plan.rationale }}</p>
      <div class="plan-split">
        <svg v-if="state.sketch" class="sketch" :viewBox="`0 0 ${state.sketch.side} ${state.sketch.side}`">
          <rect x="0" y="0" :width="state.sketch.side" :height="state.sketch.side" class="site" />
          <g v-for="(r, i) in state.sketch.rects" :key="r.id" @click="selectedZone = r.id">
            <rect :x="r.x" :y="r.y" :width="r.w" :height="r.h" :fill="ZONE_COLOURS[i % 6]" :class="{ picked: selectedZone === r.id }" />
            <text :x="r.x + r.w / 2" :y="r.y + r.h / 2" :font-size="state.sketch.side / 28">{{ r.name }}</text>
          </g>
        </svg>
        <table class="components">
          <thead><tr><th>Part</th><th>Buildings</th><th>Area</th><th>Holds</th><th>Character</th></tr></thead>
          <tbody>
            <tr v-for="row in state.plan.rows" :key="row.id" :class="{ picked: selectedZone === row.id }" @click="selectedZone = row.id">
              <td :title="row.rationale">{{ row.name }} <span class="dim">{{ row.id }}</span></td>
              <td>{{ row.buildings || '—' }}</td>
              <td>{{ amountText(row.area) }}</td>
              <td class="small">{{ row.parcels.map((p) => `${p.count} ${p.name.toLowerCase()}`).join(', ') || '—' }}</td>
              <td class="small">{{ Object.entries(row.qualities).map(([k, v]) => `${k} ${show(v)}`).join(' · ') }}</td>
            </tr>
          </tbody>
        </table>
      </div>
      <p v-if="selectedZone" class="dim why">{{ state.plan.rows.find((r) => r.id === selectedZone)?.rationale }}</p>
      <p v-if="!state.solved" class="dim">The sketch shows sizes only; where each part goes is worked out when it's built. The site is outlined in the viewport.</p>
      <section v-else class="note">
        <strong>Laid out:</strong> {{ state.solved.stats.regions }} areas, {{ state.solved.stats.objects }} objects,
        {{ state.solved.stats.paths }} roads, {{ state.solved.stats.lines }} fences and walls; the buildings are outlined in the viewport.
        <ul>
          <li v-for="c in state.solved.checks" :key="c.id" :class="c.ok ? 'ok' : 'error'">
            {{ c.ok ? '✓' : '✗' }} {{ c.predicate }} <span class="dim">{{ c.id }}</span>
            <span v-if="c.detail !== undefined" class="dim">· {{ show(c.detail) }}</span>
          </li>
        </ul>
      </section>
      <section v-if="state.problems?.length" class="problems">
        <div v-for="p in state.problems" :key="p.where + p.message" :class="p.warning ? 'warning' : 'error'">{{ p.where }}: {{ p.message }}</div>
      </section>
      <div class="actions">
        <button @click="sendToHost({ type: 'build:cancel' })">Cancel</button>
        <button v-if="stage === 'plan'" @click="sendToHost({ type: 'build:step', step: 'assets' })">Back</button>
        <button v-if="stage === 'plan'" class="primary" :disabled="planErrors.length > 0" @click="sendToHost({ type: 'build:approve' })">
          Approve plan
        </button>
        <span v-else class="dim">{{ state.status }}</span>
      </div>
    </div>

    <!-- ① Request and ② Assets: the confirmation window. -->
    <div v-else-if="win" class="body window">
      <section v-if="win.questions?.length || Object.values(win.marks).some((m) => m.disputed)" class="note">
        <strong>To decide:</strong> fields marked
        <span class="tag disputed">differ</span> were read differently;
        <span class="tag low">guessed</span> ones are the AI's guesses.
        <ul v-if="win.questions?.length">
          <li v-for="q in win.questions" :key="q">{{ q }}</li>
        </ul>
      </section>

      <template v-if="win.round === 1">
        <div class="grid">
          <label>Name</label>
          <div><input :value="win.name" @change="edit('name', ($event.target as HTMLInputElement).value)" /> <span class="dim">{{ win.buildTypeName }}</span></div>

          <label>Size</label>
          <div :class="markClass('scale.class')">
            <select :value="win.size.class" @change="edit('scale.class', ($event.target as HTMLSelectElement).value)">
              <option v-for="c in win.size.classes" :key="c.id" :value="c.id">
                {{ c.id.replace('-', ' ') }} ({{ c.buildings[0] }}–{{ c.buildings[1] }} buildings)
              </option>
            </select>
            buildings
            <input class="num" :value="countRange(win.size.buildings)[0]" @change="setBuildings(0, ($event.target as HTMLInputElement).value)" />
            –
            <input class="num" :value="countRange(win.size.buildings)[1]" @change="setBuildings(1, ($event.target as HTMLInputElement).value)" />
            <span class="dim">area {{ amountText(win.size.area) }} cells²</span>
            <div v-if="mark('scale.class')?.disputed || mark('scale.buildings.count')?.disputed" class="alts">
              other readings:
              <button v-for="(alt, i) in mark('scale.class')?.alternatives ?? []" :key="'c' + i" @click="edit('scale.class', alt)">
                {{ mark('scale.class')?.readings[i] }}: {{ show(alt) }}
              </button>
              <button v-for="(alt, i) in mark('scale.buildings.count')?.alternatives ?? []" :key="'b' + i" @click="edit('scale.buildings', { count: alt })">
                {{ mark('scale.buildings.count')?.readings[i] }}: {{ show(alt) }} buildings
              </button>
            </div>
          </div>

          <label title="One street pattern or several; the plan gives each ward one of them">Layout</label>
          <div :class="markClass('shape.layout')">
            <div class="layouts">
              <div v-for="l in win.layout.options" :key="l" class="layout" :class="{ active: l in win.layout.value }">
                <button :title="l in win.layout.value ? 'Leave this pattern out' : 'Add this pattern'" @click="edit('shape.layout', toggleShare(win.layout.value, l))">
                  <svg viewBox="0 0 40 28" width="40" height="28" v-html="layoutSketch(l)" />
                  <span>{{ l.replace(/-/g, ' ') }}</span>
                </button>
                <span v-if="l in win.layout.value && Object.keys(win.layout.value).length > 1" class="share">
                  <input
                    class="num"
                    :value="Math.round((win.layout.value[l] ?? 0) * 100)"
                    @change="edit('shape.layout', setShare(win.layout.value, l, Number(($event.target as HTMLInputElement).value) / 100))"
                  />%
                </span>
              </div>
            </div>
            <input
              class="wide"
              :value="win.layout.description ?? ''"
              placeholder="Where each goes, if it matters: e.g. a grid core inside the walls, organic lanes outside"
              @change="edit('shape.description', ($event.target as HTMLInputElement).value)"
            />
          </div>

          <label>Walls</label>
          <div :class="markClass('walled')">
            <select :value="win.walled.value ?? 'none'" @change="edit('walled', ($event.target as HTMLSelectElement).value)">
              <option v-for="w in win.walled.options" :key="w" :value="w">{{ w }}</option>
            </select>
          </div>

          <label>Features</label>
          <div class="chips">
            <button v-for="f in win.features.options" :key="f" :class="{ active: win.features.value.includes(f) }" @click="toggleFeature(f)">{{ f }}</button>
          </div>

          <label>Style</label>
          <div class="style">
            <span v-for="(s, key) in win.style" :key="key" :class="markClass('style.' + key)">
              <template v-if="s.options">
                {{ key }}
                <select :value="s.value ?? ''" @change="edit('style.' + key, ($event.target as HTMLSelectElement).value)">
                  <option value="">—</option>
                  <option v-for="o in s.options" :key="o" :value="o">{{ o }}</option>
                </select>
              </template>
            </span>
          </div>
        </div>

        <table class="components">
          <thead>
            <tr><th>Component</th><th>On</th><th>Amount</th><th>Art</th><th>Source</th></tr>
          </thead>
          <tbody>
            <tr v-for="c in shownComponents" :key="c.id" :class="{ off: !c.enabled, ...markClass('components.' + c.id + '.enabled') }">
              <td :title="c.description">{{ c.name }} <span class="dim">{{ c.level }}</span>
                <div v-if="problemFor('components.' + c.id)" class="error small">{{ problemFor('components.' + c.id) }}</div>
              </td>
              <td><input type="checkbox" :checked="c.enabled" @change="edit(`components.${c.id}.enabled`, ($event.target as HTMLInputElement).checked)" /></td>
              <td :class="markClass(`components.${c.id}.${c.amount === 'count' ? 'count' : 'density'}`)">
                <template v-if="c.enabled && c.amount === 'count'">
                  <input class="range" :value="show(c.count) === '—' ? '' : show(c.count)" placeholder="any" @change="setCount(c, ($event.target as HTMLInputElement).value)" />
                </template>
                <template v-else-if="c.enabled && c.amount === 'density'">
                  <select :value="c.density ?? ''" @change="edit(`components.${c.id}.density`, ($event.target as HTMLSelectElement).value)">
                    <option value="">default</option>
                    <option v-for="d in DENSITIES" :key="d" :value="d">{{ d }}</option>
                  </select>
                </template>
                <span v-else class="dim">—</span>
              </td>
              <td class="thumbs">
                <CatalogThumb v-for="t in c.thumbs" :key="t.id" :src="urlOf(t.path)" :rect="t.rect" :size="28" :title="t.id" />
                <span v-if="c.enabled && c.thumbs.length === 0" class="dim">none in the project</span>
              </td>
              <td>
                <select v-if="c.enabled" :value="c.source ?? 'auto'" @change="edit(`components.${c.id}.assets.source`, ($event.target as HTMLSelectElement).value)">
                  <option v-for="s in SOURCES" :key="s.id" :value="s.id" :title="s.title">{{ s.label }}</option>
                </select>
              </td>
            </tr>
          </tbody>
        </table>
        <button class="link" @click="showAll = !showAll">{{ showAll ? 'Only the components in the request' : 'Every component this place can have' }}</button>
      </template>

      <template v-else>
        <section v-for="c in win.components.filter((x) => x.enabled && x.params?.length)" :key="c.id" class="params">
          <h4>{{ c.name }}</h4>
          <div class="grid">
            <template v-for="p in c.params" :key="p.name">
              <label :title="p.description">{{ p.name.replace(/-/g, ' ') }}</label>
              <div :class="markClass(`components.${c.id}.params.${p.name}`)">
                <select v-if="p.type === 'enum' || p.type === 'density'" :value="p.value ?? ''" @change="setParam(c, p, ($event.target as HTMLSelectElement).value)">
                  <option v-for="o in p.options" :key="o" :value="o">{{ o }}</option>
                </select>
                <input v-else-if="p.type === 'bool'" type="checkbox" :checked="!!p.value" @change="setParam(c, p, ($event.target as HTMLInputElement).checked)" />
                <span v-else-if="p.type === 'enumSet'" class="chips">
                  <button v-for="o in p.options" :key="o" :class="{ active: Array.isArray(p.value) && (p.value as string[]).includes(o) }" @click="toggleInSet(c, p, o)">{{ o }}</button>
                </span>
                <span v-else-if="p.type === 'mix'" class="mix">
                  <span v-for="o in p.options" :key="o">
                    {{ o }} <input class="num" :value="mixOf(p)[o] ?? ''" placeholder="0" @change="setMix(c, p, o, ($event.target as HTMLInputElement).value)" />
                  </span>
                </span>
                <span v-else-if="p.type === 'size'">
                  w <input class="range" :value="sizeText(p.value, 'w')" @change="setSize(c, p, 'w', ($event.target as HTMLInputElement).value)" />
                  h <input class="range" :value="sizeText(p.value, 'h')" @change="setSize(c, p, 'h', ($event.target as HTMLInputElement).value)" />
                  <span class="dim">cells</span>
                </span>
                <input v-else class="range" :value="show(p.value) === '—' ? '' : show(p.value)" @change="setParam(c, p, parseRange(($event.target as HTMLInputElement).value) ?? ($event.target as HTMLInputElement).value)" />
                <span v-if="p.unit" class="dim">{{ p.unit }}</span>
              </div>
            </template>
          </div>
        </section>

        <section v-if="win.assetPlan" class="plan">
          <h4>
            Assets: {{ win.assetPlan.summary.project }} from the project, {{ win.assetPlan.summary.generate }} to generate
          </h4>
          <table class="components">
            <thead><tr><th>Needed</th><th>For</th><th>Candidates</th><th>Source</th><th>Why</th></tr></thead>
            <tbody>
              <tr v-for="row in win.assetPlan.rows" :key="row.component + row.label + row.slot">
                <td>{{ row.label }}<span v-if="!row.essential" class="dim"> (optional)</span></td>
                <td class="dim">{{ row.component }}</td>
                <td class="thumbs">
                  <CatalogThumb v-for="c in row.candidates" :key="c.id" :src="urlOf(c.path)" :rect="c.rect" :size="28" :title="`${c.name}: fit ${c.fit} (${c.grade})`" />
                  <span v-if="row.candidates.length === 0" class="dim">none</span>
                </td>
                <td>
                  <select :value="row.chosen" @change="edit(`components.${row.component}.assets.source`, ($event.target as HTMLSelectElement).value)">
                    <option v-for="s in SOURCES" :key="s.id" :value="s.id" :title="s.title">{{ s.id === 'auto' ? `auto (${row.source})` : s.label }}</option>
                  </select>
                </td>
                <td class="dim">{{ row.reason }}</td>
              </tr>
            </tbody>
          </table>
        </section>
      </template>

      <section v-if="win.assumptions?.length" class="note">
        <strong>Assumed:</strong>
        <ul><li v-for="a in win.assumptions" :key="a">{{ a }}</li></ul>
      </section>
      <section v-if="errors.length || warnings.length" class="problems">
        <div v-for="p in errors" :key="p.where + p.message" class="error">{{ p.where }}: {{ p.message }}</div>
        <div v-for="p in warnings" :key="p.where + p.message" class="warning">{{ p.where }}: {{ p.message }}</div>
      </section>

      <div class="actions">
        <button @click="sendToHost({ type: 'build:cancel' })">Cancel</button>
        <button v-if="win.round === 2" @click="sendToHost({ type: 'build:step', step: 'request' })">Back</button>
        <button
          v-if="win.round === 1"
          class="primary"
          :disabled="errors.length > 0"
          @click="sendToHost({ type: 'build:step', step: 'assets' })"
        >
          Continue →
        </button>
        <button v-else class="primary" :disabled="errors.length > 0" @click="sendToHost({ type: 'build:confirm' })">Build</button>
      </div>
    </div>
  </div>
</template>

<style scoped>
.build {
  display: flex;
  flex-direction: column;
  height: 100%;
  background: #181825;
  font-size: 0.85em;
  color: #cdd6f4;
}
.header {
  display: flex;
  align-items: center;
  gap: 12px;
  height: 32px;
  padding: 0 8px;
  border-bottom: 1px solid #333;
  flex-shrink: 0;
}
.steps {
  display: flex;
}
.steps button {
  border: 0;
  border-bottom: 2px solid transparent;
  background: transparent;
  color: #a6adc8;
  padding: 6px 10px;
}
.steps button.active {
  color: #cdd6f4;
  border-bottom-color: #89b4fa;
}
.steps button:disabled {
  color: #585b70;
}
.title {
  font-weight: 600;
}
.body {
  flex: 1;
  overflow: auto;
  padding: 10px 12px;
}
.start {
  max-width: 720px;
}
.ask {
  display: flex;
  gap: 6px;
}
.ask input {
  flex: 1;
}
input,
select,
button {
  background: #1e1e2e;
  color: #cdd6f4;
  border: 1px solid #45475a;
  border-radius: 0;
  font: inherit;
  padding: 2px 6px;
}
button:hover:not(:disabled) {
  border-color: #89b4fa;
}
button:disabled {
  opacity: 0.5;
}
button.primary {
  background: #89b4fa;
  color: #11111b;
  border-color: #89b4fa;
}
button.link {
  border: 0;
  background: none;
  color: #89b4fa;
  padding: 4px 0;
}
.num {
  width: 52px;
}
.range {
  width: 80px;
}
.dim {
  color: #7f849c;
}
.error {
  color: #f38ba8;
}
.warning {
  color: #f9e2af;
}
.small {
  font-size: 0.9em;
}
.grid {
  display: grid;
  grid-template-columns: 110px 1fr;
  gap: 6px 12px;
  align-items: center;
  margin-bottom: 10px;
}
.grid > label {
  color: #a6adc8;
}
.disputed {
  outline: 1px solid #f9e2af;
  outline-offset: 2px;
}
.low {
  outline: 1px dashed #fab387;
  outline-offset: 2px;
}
.tag {
  padding: 0 4px;
}
.tag.disputed {
  outline: 1px solid #f9e2af;
}
.tag.low {
  outline: 1px dashed #fab387;
}
.alts {
  margin-top: 4px;
  color: #a6adc8;
}
.alts button {
  margin-left: 4px;
}
.layouts,
.chips {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}
.layout {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
}
.share .num {
  width: 40px;
}
.wide {
  width: 100%;
  margin-top: 6px;
}
.layouts button {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
  padding: 4px 6px;
  min-width: 70px;
}
.layout.active button,
.chips button.active {
  border-color: #89b4fa;
  background: #313244;
}
.layouts svg {
  stroke: #a6adc8;
  fill: none;
}
.style span {
  margin-right: 10px;
}
.components {
  border-collapse: collapse;
  width: 100%;
  margin: 6px 0;
}
.components th {
  text-align: left;
  color: #a6adc8;
  font-weight: normal;
  border-bottom: 1px solid #333;
  padding: 3px 6px;
}
.components td {
  border-bottom: 1px solid #262637;
  padding: 3px 6px;
  vertical-align: middle;
}
.components tr.off td {
  color: #6c7086;
}
.thumbs {
  display: flex;
  gap: 3px;
  align-items: center;
}
.note {
  background: #1e1e2e;
  border-left: 3px solid #89b4fa;
  padding: 6px 10px;
  margin: 8px 0;
}
.note ul {
  margin: 4px 0 0 16px;
  padding: 0;
}
.problems {
  margin: 8px 0;
}
.params h4,
.plan h4 {
  margin: 10px 0 6px;
  font-weight: 600;
}
.mix span {
  margin-right: 8px;
}
.bar {
  height: 4px;
  background: #313244;
  margin: 6px 0 10px;
}
.bar i {
  display: block;
  height: 100%;
  background: #89b4fa;
}
.headline {
  font-size: 1.1em;
  font-weight: 600;
  margin: 0 0 4px;
}
.plan-split {
  display: flex;
  gap: 12px;
  align-items: flex-start;
}
.sketch {
  width: 260px;
  height: 260px;
  flex-shrink: 0;
}
.sketch .site {
  fill: #11111b;
  stroke: #89b4fa;
  stroke-width: 0.6;
}
.sketch rect:not(.site) {
  fill-opacity: 0.35;
  stroke: #11111b;
  stroke-width: 0.4;
  cursor: pointer;
}
.sketch rect.picked {
  fill-opacity: 0.7;
}
.sketch text {
  fill: #cdd6f4;
  text-anchor: middle;
  dominant-baseline: middle;
  pointer-events: none;
}
.components tr.picked td {
  background: #313244;
}
.ok {
  color: #a6e3a1;
}
.why {
  margin: 6px 0;
}
.actions {
  display: flex;
  justify-content: flex-end;
  gap: 6px;
  margin-top: 10px;
  padding-top: 8px;
  border-top: 1px solid #333;
}
</style>
