<script setup lang="ts">
// The AI Planner (development_plan.md 22, docs/ai/ai-planner.md §6): one view per stage — the
// Game Brief's confirmation window, the outline (story, cast, places, stacked), the story's acts and scene
// board, the cast, quests and places, systems, visuals and the validation report — and on
// every item Why?, Regenerate, Edit and Lock. Documents whose inputs changed show as stale;
// "Regenerate stale" redoes exactly those. A place's draft Brief opens in the Build panel.
import { computed, ref, watch } from 'vue'
import AiProgress from './AiProgress.vue'
import { onHostMessage, sendToHost } from '../ipc'
import DocView from './DocView.vue'
import SystemsView from './SystemsView.vue'
import DataTable from './DataTable.vue'
import { activatePanel, setPanelVisible } from '../layout'
import Splitter from './Splitter.vue'

interface Stage {
  id: string
  label: string
  status: string
  done: number
  total: number
  stale: number
  review: boolean
  failed?: number
}
interface FailedJob {
  id: string
  kind: string
  subject: string
  stage: string
  error: string
  doc: string
}
interface WindowField {
  path: string
  label: string
  type: string
  value: unknown
  options?: string[]
  asked: boolean
  assumed: boolean
  fromStory?: boolean // a number the story decided (written back after the outline): editable, the story goes stale
  mark?: { confidence?: number; differ?: unknown[]; guessed?: boolean }
  min?: number
  max?: number
  lines?: number // text of several lines: a box
}
interface WindowTab {
  id: string
  label: string
  fields: WindowField[]
}
interface Row {
  id: string
  doc: string
  name?: string
  stale?: boolean
  locked?: boolean
  issues?: string[]
  notes?: string[] // what a later stage resolves: information, not a warning
  [key: string]: unknown
}
interface SceneRow extends Row {
  mode: string
  kernel: boolean
  branch: string
  location: string
  goal: string
  summary: string
  written: boolean
  // planned, queued, writing, rewrite-queued, rewriting, failed, written
  progress: string
  job: string
  error: string
  attempt: number
  criticNote: string
}
interface Chapter {
  id: string
  question: string
  scenes: SceneRow[]
}
interface Act {
  id: string
  doc: string
  summary: string
  chapters: Chapter[]
}
interface Finding {
  remedy?: { how: string; do: unknown } // a known fix ("add the journal window", "make tamsin again")
  fixJob?: string // the job fixing it, while it's being fixed
  step?: string // the stage it comes from ("④ Characters")
  id: string
  rule: string
  severity: string
  subject: string
  message: string
  status: string
  reason?: string
}

const state = ref<Record<string, any>>({ active: false })
const view = ref('')
const tab = ref('game')
const request = ref('')
const askReplace = ref('')
const item = ref<{ doc: string; content: unknown } | null>(null)
const itemText = ref('')
const why = ref<Record<string, any> | null>(null)

onHostMessage((message) => {
  if (message.type === 'planner:state') {
    state.value = message as Record<string, any>
    if (!view.value || view.value === 'start') view.value = (state.value.stage as string) || 'concept'
  } else if (message.type === 'planner:open') {
    setPanelVisible('planner', true)
    activatePanel('planner')
  } else if (message.type === 'planner:askReplace') {
    askReplace.value = (message.request as string) || ''
    replaceChoices.value = (message.choices as Record<string, unknown>) ?? {}
  } else if (message.type === 'planner:document') {
    item.value = { doc: message.doc as string, content: message.content }
    itemText.value = JSON.stringify(message.content, null, 2)
    why.value = null
    if (item.value.doc !== lastDoc) rawJson.value = false // a new document opens readable
    lastDoc = item.value.doc
  } else if (message.type === 'planner:why') {
    why.value = message.why as Record<string, any>
  }
})
sendToHost({ type: 'planner:request' })

const stages = computed(() => (state.value.stages ?? []) as Stage[])
const windowTabs = computed(() => (state.value.window ?? []) as WindowTab[])
const currentTab = computed(() => windowTabs.value.find((t) => t.id === tab.value) ?? windowTabs.value[0])
const acts = computed(() => (state.value.acts ?? []) as Act[])
// The findings still to deal with; those being fixed are off the list (the next report decides).
const allFindings = computed(() => ((state.value.report?.findings ?? []) as Finding[]))
const findings = computed(() => allFindings.value.filter((f) => f.status !== 'fixed')) // a fixed one leaves the list
const fixingCount = computed(() => findings.value.filter((f) => f.status === 'fixing').length)
// The row numbers (1-based, as the table shows them) of the findings whose fix is running now.
// The row numbers (as the table shows them) of every finding being fixed.
const fixingRows = computed(() => findings.value.flatMap((f, i) => (f.status === 'fixing' ? [i + 1] : [])))
// 1, 2, 3, 5, 7, 8, 9 -> "1–3, 5, 7–9".
function ranges(rows: number[]): string {
  const out: string[] = []
  for (let i = 0; i < rows.length; ) {
    let j = i
    while (j + 1 < rows.length && rows[j + 1] === rows[j]! + 1) j++
    out.push(j - i >= 2 ? `${rows[i]}–${rows[j]}` : rows.slice(i, j + 1).join(', '))
    i = j + 1
  }
  return out.join(', ')
}
const fixingNow = computed(() => {
  const running = new Set((state.value.runningJobs ?? []) as string[])
  return findings.value.flatMap((f, i) => (f.status === 'fixing' && f.fixJob && running.has(f.fixJob) ? [i + 1] : []))
})
// A fix running (findings still fixing, or the plan re-checking): no second one on top.
const fixBusy = computed(() => fixingCount.value > 0 || (state.value.stage === 'validate' && state.value.status === 'working'))
const openErrors = computed(() => findings.value.filter((f) => f.status !== 'accepted' && f.severity === 'error').length) // fixing ones still count
const openWarnings = computed(() => findings.value.filter((f) => f.status !== 'accepted' && f.severity !== 'error').length)
const acceptedCount = computed(() => findings.value.filter((f) => f.status === 'accepted').length)
const fixable = computed(() => findings.value.filter((f) => f.status === 'open' && f.remedy))
// ⑧'s design: theme, windows, HUD first; bookkeeping left out.
const uiInOrder = computed(() => {
  const ui = (state.value.ui ?? {}) as Record<string, unknown>
  const out: Record<string, unknown> = {}
  for (const k of ['theme', 'screens', 'hud', 'notifications', 'mapStyle']) if (k in ui) out[k] = ui[k]
  for (const [k, v] of Object.entries(ui)) {
    if (!(k in out) && !['schema', 'revision', 'madeBy', 'inputs', 'rationale', 'locked'].includes(k)) out[k] = v
  }
  return out
})
// ⑩: the place being built now, how far along the places are, and which can be tried again.
const buildRows = computed(() => (state.value.build?.rows ?? []) as { place: string; status: string; step?: string; planPhase?: string }[])
const buildingRow = computed(() => buildRows.value.find((r) => r.status === 'building'))
const buildDone = computed(() => buildRows.value.filter((r) => r.status === 'built' || r.status === 'placeholder').length)
function rebuildPlace(place: string) {
  if (window.confirm(`Build ${place.replace(/^location\//, '')} again from the start? Its scene is replaced.`))
    sendToHost({ type: 'planner:buildRow', place, action: 'rebuild' })
}
const buildRetryable = computed(() => buildRows.value.filter((r) => r.status === 'failed' || r.status === 'placeholder'))
// The art strategy's options, as words.
function artSourceName(source: string): string {
  return (
    ({
      search: 'Search: the project, the library, the web, then made by the Art tool',
      generate: 'Generate: made by the Art tool',
      library: 'Library: the libraries, else made by the Art tool',
      project: 'The project: its own art, else made by the Art tool',
      auto: "Auto: by each piece's importance",
    } as Record<string, string>)[source] ?? source
  )
}
function restartBuilds() {
  if (window.confirm('Build every place again from the start, with the art setting as it is now?')) sendToHost({ type: 'planner:restartBuilds' })
}
function retryFailedPlaces() {
  for (const r of buildRetryable.value) sendToHost({ type: 'planner:buildRow', place: r.place, action: 'retry' })
}
// The place builder's step, named as the Build panel names it (its activeStep): the AI drafting
// the first plan is still ① Confirm there.
function stepName(step: string, phase = ''): string {
  if (['request', 'assets', 'interpreting'].includes(step) || (step === 'planning' && phase === 'root')) return '① Confirm'
  if (step === 'layout') return '② Layout'
  if (['planning', 'plan', 'planned', 'revising'].includes(step)) return '③ Plan'
  if (['sourcing', 'sourced'].includes(step)) return '④ Assets'
  if (['building', 'built'].includes(step)) return '⑤ Result'
  return step
}
const fixableHow = computed(() => fixable.value.map((f) => f.remedy?.how ?? '').join('\n'))
const fixableErrors = computed(() => fixable.value.filter((f) => f.severity === 'error').length)
const fixableWarnings = computed(() => fixable.value.length - fixableErrors.value)
// What "Fix now" fixes: errors first (they stop the build), else what there is.
const fixScope = ref('errors')
watch(fixableErrors, (n) => {
  if (n === 0 && fixScope.value === 'errors') fixScope.value = '*'
}, { immediate: true })
const staleCount = computed(() => (state.value.stale ?? []).length as number)
const failedJobs = computed(() => (state.value.failedJobs ?? []) as FailedJob[])
const failedHere = computed(() => failedJobs.value.filter((j) => j.stage === view.value))
// A stage not reached yet has nothing to show.
function reachable(s: Stage): boolean {
  return s.status !== 'pending' && s.status !== 'skipped'
}
// The editor's "Raw JSON" of an item, for whoever wants it.
const rawJson = ref(false)
let lastDoc = ''
function retryJob(id: string) {
  sendToHost({ type: 'planner:retryJob', id })
}
function jobName(j: FailedJob): string {
  const what = { outline: 'The outline', act: 'Act', scene: 'Scene', critic: 'Review of act', character: 'Character', quests: 'The quests', place: 'Place', systems: 'The systems', visuals: 'The interface', review: 'The review', interpret: 'Reading' }[j.kind] ?? j.kind
  return j.subject && j.subject !== j.kind ? `${what} ${j.subject}` : what
}
const waiting = computed(() => state.value.status === 'waiting' || state.value.status === 'failed')
// The places are being built (⑩–⑬, not finished): Continue goes on with them, not back to ⑨.
const buildGoing = computed(() => !!state.value.build?.active && state.value.build?.stage !== 'done')
const outline = computed(() => (state.value.outline ?? {}) as Record<string, any>)
const hasOutline = computed(() => Object.keys(outline.value).length > 0)
const outlineStage = computed(() => stages.value.find((s) => s.id === 'outline'))
// The outline's sections the user folded (remembered in this browser).
const folded = ref<Record<string, boolean>>(loadFolded())
function loadFolded(): Record<string, boolean> {
  try {
    return JSON.parse(localStorage.getItem('planner.outline.folded') ?? '{}') as Record<string, boolean>
  } catch {
    return {}
  }
}
function toggleFold(section: string) {
  folded.value = { ...folded.value, [section]: !folded.value[section] }
  try {
    localStorage.setItem('planner.outline.folded', JSON.stringify(folded.value))
  } catch {
    /* not kept */
  }
}
watch(
  () => state.value.stage,
  (s) => {
    if (s) view.value = s as string
  },
)

// The time the AI spent generating (calls in flight, overlapping ones once): 42s, 3m 05s, 1h 12m.
function duration(seconds: number): string {
  const s = Math.round(seconds)
  if (s < 60) return `${s}s`
  if (s < 3600) return `${Math.floor(s / 60)}m ${String(s % 60).padStart(2, '0')}s`
  return `${Math.floor(s / 3600)}h ${String(Math.floor((s % 3600) / 60)).padStart(2, '0')}m`
}
// The item pane (a scene's, a document's details) under the step: as tall as the user drags it (kept).
const bodyEl = ref<HTMLElement | null>(null)
const itemHeight = ref(Number(localStorage.getItem('planner.itemHeight')) || 320)
function resizeItem(y: number): void {
  const box = bodyEl.value?.getBoundingClientRect()
  if (!box) return
  itemHeight.value = Math.round(Math.max(120, Math.min(box.height - 80, box.bottom - y)))
  try {
    localStorage.setItem('planner.itemHeight', String(itemHeight.value))
  } catch {
    /* not kept */
  }
}
// The home page's Choices (as the Build panel's): Brief fields picked from the story ontology's options, sent with the
// request; the readings keep them. One option for an enum (again: unpicked), any number for a set.
type Choice = { path: string; label: string; type: string; options: string[] }
const choicesOpen = ref(false)
const picked = ref<Record<string, string | string[]>>({})
function isPicked(c: Choice, o: string): boolean {
  const v = picked.value[c.path]
  return Array.isArray(v) ? v.includes(o) : v === o
}
function pick(c: Choice, o: string): void {
  const next = { ...picked.value }
  if (c.type === 'enumSet') {
    const now = Array.isArray(next[c.path]) ? [...(next[c.path] as string[])] : []
    const at = now.indexOf(o)
    if (at >= 0) now.splice(at, 1)
    else now.push(o)
    if (now.length) next[c.path] = now
    else delete next[c.path]
  } else if (next[c.path] === o) {
    delete next[c.path]
  } else {
    next[c.path] = o
  }
  picked.value = next
}
const replaceChoices = ref<Record<string, unknown>>({})
// The home page (⌂ on the step bar): a new plan, and the project's plans (the current one, the earlier ones).
const home = ref(false)
// How the plan's places are built (Project Settings ▸ Places ▸ method): the plan's, from the project's by default.
const BUILD_METHODS = [
  { id: 'Assembled', title: 'Each building, tree and prop gets its own art, put together on the grid over a painted background; every piece can be moved and edited.' },
  { id: 'Paint', title: "Each place is painted whole as one picture (as Baldur's Gate's areas are); where one can walk and what a character goes behind are pre-filled from the plan and refined by you. Only doors, chests, items and characters get art of their own." },
]
function setBuildMethod(method: string): void {
  sendToHost({ type: 'planner:setBuildMethod', method })
}
type PlanRow = { id: string; request: string; stage: string; updated: number; name?: string }
const removing = ref<PlanRow | null>(null)
function openPlan(id: string): void {
  home.value = false
  if (id) sendToHost({ type: 'planner:openPlan', id })
  else sendToHost({ type: 'planner:request' })
}
function removePlan(): void {
  if (!removing.value) return
  sendToHost(removing.value.id ? { type: 'planner:removePlan', id: removing.value.id } : { type: 'planner:remove' })
  removing.value = null
}
function planWhen(seconds: number): string {
  return seconds ? new Date(seconds * 1000).toLocaleString([], { day: 'numeric', month: 'short', hour: '2-digit', minute: '2-digit' }) : ''
}
function start() {
  if (!request.value.trim()) return
  home.value = false // the plan's page: its Brief, or the question whether to replace the current plan
  sendToHost({ type: 'planner:start', request: request.value, choices: { ...picked.value }, buildMethod: state.value.buildMethod || '' })
}
function replace() {
  sendToHost({ type: 'planner:start', request: askReplace.value, replace: true, choices: replaceChoices.value })
  askReplace.value = ''
}
function open(doc: string) {
  sendToHost({ type: 'planner:document', doc })
}
function askWhy(doc: string) {
  sendToHost({ type: 'planner:why', doc })
}
function saveItem() {
  if (!item.value) return
  try {
    sendToHost({ type: 'planner:edit', doc: item.value.doc, pointer: '', value: JSON.parse(itemText.value) })
  } catch {
    sendToHost({ type: 'notify', text: 'That isn’t valid JSON' })
  }
}
function setField(f: WindowField, raw: unknown) {
  let value: unknown = raw
  if (f.type === 'int') value = Math.round(Number(raw))
  else if (f.type === 'number') value = Number(raw)
  else if (f.type === 'list') value = String(raw).split(',').map((s) => s.trim()).filter(Boolean)
  sendToHost({ type: 'planner:editBrief', path: f.path, value })
}
function toggleSet(f: WindowField, option: string) {
  const now = Array.isArray(f.value) ? [...(f.value as string[])] : []
  const at = now.indexOf(option)
  if (at >= 0) now.splice(at, 1)
  else now.push(option)
  sendToHost({ type: 'planner:editBrief', path: f.path, value: now })
}
function setRange(f: WindowField, index: number, raw: string) {
  const now = Array.isArray(f.value) ? [...(f.value as number[])] : [0, 0]
  now[index] = Number(raw)
  sendToHost({ type: 'planner:editBrief', path: f.path, value: now })
}
function accept(f: Finding) {
  const reason = window.prompt(`Keep "${f.message}" as it is — why?`)
  if (reason) sendToHost({ type: 'planner:accept', finding: f.id, reason })
}
function show(value: unknown): string {
  if (value === null || value === undefined) return ''
  return typeof value === 'string' ? value : JSON.stringify(value)
}
function statusMark(status: string): string {
  return { done: '✓', working: '…', waiting: '?', failed: '✗', paused: 'Ⅱ', skipped: '–' }[status] ?? ''
}
</script>

<template>
  <div class="planner ed-form">
    <div v-if="!state.active || home" class="start">
      <h3>Plan a game</h3>
      <p class="muted">
        Describe the game: the Planner reads it three ways, asks you to confirm a Game Brief, then designs the story, cast,
        quests, places, systems and interface, checking each step. A new plan keeps the current one below, to open again.
      </p>
      <label class="method-row">
        Build method
        <select :value="state.buildMethod || state.projectBuildMethod || 'Assembled'" @change="setBuildMethod(($event.target as HTMLSelectElement).value)">
          <option v-for="m in BUILD_METHODS" :key="m.id" :value="m.id" :title="m.title">{{ m.id }}</option>
        </select>
        <span class="help" :title="BUILD_METHODS.find((m) => m.id === (state.buildMethod || state.projectBuildMethod))?.title">?</span>
        <span class="muted">{{ (state.buildMethod || state.projectBuildMethod) === state.projectBuildMethod ? "the project's" : 'this plan only' }}</span>
      </label>
      <textarea v-model="request" rows="4" placeholder="A short dark-fantasy action RPG about a disgraced warden recovering a stolen amulet, three towns and a dungeon, about four hours" />
      <section v-if="state.choices?.length" class="choices">
        <button class="toggle" @click="choicesOpen = !choicesOpen">
          {{ choicesOpen ? '▾' : '▸' }} Choices <span class="muted">{{ Object.keys(picked).length ? Object.keys(picked).length + ' chosen: kept as you chose them' : 'from the story ontology, sent with the request' }}</span>
        </button>
        <div v-if="choicesOpen" class="choice-rows">
          <div v-for="c in state.choices" :key="c.path" class="choice-row">
            <span class="label">{{ c.label }}</span>
            <div class="chips">
              <button v-for="o in c.options" :key="o" :class="['chip', { on: isPicked(c, o) }]" @click="pick(c, o)">{{ o }}</button>
            </div>
          </div>
          <button v-if="Object.keys(picked).length" class="small" @click="picked = {}">Clear the choices</button>
        </div>
      </section>
      <div class="row">
        <button class="primary" :disabled="!request.trim()" @click="start">Plan</button>
        <button v-if="state.active && home" @click="home = false">Back to the plan</button>
      </div>
      <section v-if="state.plans?.length" class="plans">
        <h4>Your plans</h4>
        <div v-for="p in state.plans" :key="p.id || 'current'" class="plan-row" :class="{ current: !p.id }">
          <div class="what">
            <strong>{{ p.name || p.request || 'A plan' }}</strong>
            <span class="muted">{{ p.id ? 'earlier' : 'current' }} · {{ p.stage || 'started' }} · {{ planWhen(p.updated) }}</span>
            <span v-if="p.name && p.request" class="muted small">{{ p.request }}</span>
          </div>
          <button class="primary small" @click="openPlan(p.id)">Open</button>
          <button class="small" title="Deletes this plan (asks first)" @click="removing = p">Remove</button>
        </div>
      </section>
      <div v-if="removing" class="ask-remove">
        <strong>Remove “{{ removing.name || removing.request || 'this plan' }}”?</strong>
        <p class="muted">
          Deletes the plan's documents (the Game Brief, story, cast, quests, places…) and its history.
          What it already built into scenes stays there. This can't be undone.
        </p>
        <div class="row">
          <button @click="removing = null">Keep it</button>
          <button class="primary" @click="removePlan">Remove it</button>
        </div>
      </div>
    </div>

    <template v-else>
      <div class="stepper">
        <button class="step home-step" title="Home: your plans, and a new one" @click="home = true">⌂</button>
        <button
          v-for="s in stages"
          :key="s.id"
          :class="['step', s.status, { on: view === s.id }]"
          :title="reachable(s) ? `${s.label}: ${s.status}${s.total ? ` (${s.done}/${s.total})` : ''}` : `${s.label}: not reached yet`"
          :disabled="!reachable(s)"
          @click="view = s.id"
        >
          {{ s.label }} <span class="mark">{{ statusMark(s.status) }}</span>
          <span v-if="s.failed" class="failed-count" :title="`${s.failed} failed: Retry in the stage`">{{ s.failed }} ✗</span>
          <span v-if="s.stale" class="stale">{{ s.stale }}</span>
          <span v-if="s.review" class="badge">review</span>
        </button>
        <!-- ⑩–⑬ the last step: greyed until the design has a baseline (⑨ Validate passed) -->
        <button
          :class="['step', { on: view === 'build' }]"
          :disabled="!(state.baseline || state.build?.active)"
          :title="state.baseline || state.build?.active
            ? 'Build the places, place the characters, bind the quests, write the files and playtest'
            : 'Available once ⑨ Validate passes. To build one place now: open it in ⑥ Places and choose Build…'"
          @click="view = 'build'"
        >
          ⑩–⑬ Build
        </button>
      </div>
      <div class="bar">
        <span :class="['status', state.status]">{{ state.message }}</span>
        <span class="spacer"></span>
        <span class="muted" :title="`Read ${(state.usage?.inputTokens ?? 0).toLocaleString()} tokens, wrote ${(state.usage?.outputTokens ?? 0).toLocaleString()}`">
          {{ state.usage?.calls ?? 0 }} AI calls · {{ (state.usage?.outputTokens ?? 0).toLocaleString() }} tokens written · {{ duration(state.usage?.seconds ?? 0) }} generating · {{ state.decisions }} decisions
        </span>
        <button v-if="failedJobs.length" class="retry" @click="sendToHost({ type: 'planner:retryFailed' })">Retry failed ({{ failedJobs.length }})</button>
        <button v-if="staleCount" @click="sendToHost({ type: 'planner:regenerateStale' })">Regenerate stale ({{ staleCount }})</button>
        <button v-if="state.status === 'working'" @click="sendToHost({ type: 'planner:pause', on: true })">Pause</button>
        <button v-if="state.status === 'paused'" @click="sendToHost({ type: 'planner:pause', on: false })">Resume</button>
        <button v-if="buildGoing && state.build.status !== 'working'" class="primary" title="Go on from the next place that isn't built; those that are stay"
          @click="sendToHost({ type: 'planner:continueBuilds' })">Continue building</button>
        <button v-else-if="waiting && !buildGoing" class="primary" @click="sendToHost({ type: state.stage === 'concept' ? 'planner:confirm' : 'planner:approve' })">
          Continue
        </button>
      </div>
      <AiProgress :paths="['/planner/']" />
      <div v-if="askReplace" class="notice">
        The project already has a plan. Replace it with a new one for "{{ askReplace }}"?
        <button @click="replace">Replace</button><button @click="askReplace = ''">Keep it</button>
      </div>

      <div ref="bodyEl" class="body">
        <div class="main">
          <!-- What failed in this stage: why, and Retry -->
          <div v-if="failedHere.length" class="failed-list">
            <div v-for="j in failedHere" :key="j.id" class="failed-row">
              <span class="err">✗ {{ jobName(j) }}</span>
              <span class="muted why-failed" :title="j.error">{{ j.error || 'failed' }}</span>
              <span class="spacer"></span>
              <button v-if="j.doc" @click="open(j.doc)">Open</button>
              <button class="primary" @click="retryJob(j.id)">Retry</button>
            </div>
          </div>
          <!-- ① Concept: the confirmation window -->
          <template v-if="view === 'concept'">
            <div class="tabs">
              <button v-for="t in windowTabs" :key="t.id" :class="{ on: currentTab?.id === t.id }" @click="tab = t.id">{{ t.label }}</button>
            </div>
            <table v-if="currentTab" class="grid form">
              <tbody>
                <tr v-for="f in currentTab.fields" :key="f.path" :class="{ asked: f.asked }">
                  <td class="label">
                    {{ f.label }}
                    <span v-if="f.mark?.differ" class="badge warn" :title="`Other readings: ${show(f.mark.differ)}`">differ</span>
                    <span v-else-if="f.mark?.guessed" class="badge warn">guessed</span>
                    <span v-if="f.assumed" class="badge" title="From the genre profile or the scope table">assumed</span>
                    <span v-if="f.fromStory" class="badge" title="Decided while the story was written; change it and the story is marked stale (Regenerate stale)">from the story</span>
                  </td>
                  <td>
                    <select v-if="f.type === 'enum'" :value="f.value as string" @change="setField(f, ($event.target as HTMLSelectElement).value)">
                      <option v-for="o in f.options" :key="o" :value="o">{{ o }}</option>
                    </select>
                    <div v-else-if="f.type === 'enumSet'" class="chips">
                      <button
                        v-for="o in f.options"
                        :key="o"
                        :class="['chip', { on: Array.isArray(f.value) && (f.value as string[]).includes(o) }]"
                        @click="toggleSet(f, o)"
                      >{{ o }}</button>
                    </div>
                    <input v-else-if="f.type === 'bool'" type="checkbox" :checked="!!f.value" @change="setField(f, ($event.target as HTMLInputElement).checked)" />
                    <span v-else-if="f.type === 'range'" class="range">
                      <input type="number" :value="(f.value as number[] | undefined)?.[0]" @change="setRange(f, 0, ($event.target as HTMLInputElement).value)" />
                      –
                      <input type="number" :value="(f.value as number[] | undefined)?.[1]" @change="setRange(f, 1, ($event.target as HTMLInputElement).value)" />
                    </span>
                    <input
                      v-else-if="f.type === 'int' || f.type === 'number'"
                      type="number"
                      :min="f.min"
                      :max="f.max"
                      :step="f.type === 'int' ? 1 : 0.25"
                      :value="f.value as number"
                      @change="setField(f, ($event.target as HTMLInputElement).value)"
                    />
                    <textarea
                      v-else-if="f.type === 'text' && f.lines"
                      :rows="f.lines"
                      :value="show(f.value)"
                      @change="setField(f, ($event.target as HTMLTextAreaElement).value)"
                    />
                    <input
                      v-else
                      :value="Array.isArray(f.value) ? (f.value as string[]).join(', ') : show(f.value)"
                      @change="setField(f, ($event.target as HTMLInputElement).value)"
                    />
                  </td>
                </tr>
              </tbody>
            </table>
            <div v-if="(state.toDecide ?? []).length" class="section">
              <h4>To decide</h4>
              <ul>
                <li v-for="(d, i) in state.toDecide" :key="i">{{ d.question }} <span class="muted">— assumed {{ d.assumed }}</span></li>
              </ul>
            </div>
          </template>

          <!-- ② Outline: the story, then the cast, then the places, one under the other -->
          <template v-else-if="view === 'outline'">
            <div v-if="!hasOutline" class="empty">
              <template v-if="outlineStage?.status === 'working'">
                <b>Writing the outline…</b>
                <p class="muted">The AI is drafting the story, cast and places ({{ state.jobs?.running ?? 0 }} running). It shows here when done.</p>
              </template>
              <template v-else-if="outlineStage?.status === 'failed'">
                <b class="err">The outline couldn’t be written.</b>
                <p class="muted">{{ state.message }}</p>
                <button class="primary" @click="sendToHost({ type: 'planner:regenerate', doc: 'outline' })">Try again</button>
              </template>
              <template v-else-if="state.stage && state.stage !== 'concept'">
                <b>No outline yet.</b>
                <p class="muted">The plan is past the outline but none was saved.</p>
                <button class="primary" @click="sendToHost({ type: 'planner:regenerate', doc: 'outline' })">Write the outline</button>
              </template>
              <template v-else>
                <b>No outline yet.</b>
                <p class="muted">It’s written after the Game Brief is confirmed: check ① Concept and press Continue.</p>
                <button @click="view = 'concept'">Go to ① Concept</button>
              </template>
            </div>
            <template v-else>
              <section class="sect">
                <h4 class="fold" @click="toggleFold('story')">
                  <span class="caret">{{ folded.story ? '▸' : '▾' }}</span> Story
                  <a href="#" @click.prevent.stop="open('outline')">open</a>
                </h4>
                <template v-if="!folded.story">
                  <p><b>{{ outline.story?.logline }}</b></p>
                  <p class="muted">{{ outline.story?.synopsis }}</p>
                  <div v-for="a in outline.story?.acts ?? []" :key="a.id" class="card"><b>{{ a.id }}</b> {{ a.summary }}</div>
                </template>
              </section>
              <section class="sect">
                <h4 class="fold" @click="toggleFold('cast')">
                  <span class="caret">{{ folded.cast ? '▸' : '▾' }}</span> Cast <span class="muted">{{ (outline.cast ?? []).length }}</span>
                </h4>
                <div v-if="!folded.cast" class="cards">
                  <div v-for="c in outline.cast ?? []" :key="c.id" class="card">
                    <b>{{ c.name }}</b> <span class="badge">{{ c.importance }}</span><span v-if="c.player" class="badge">player</span>
                    <div class="muted">{{ (c.narrativeRoles ?? []).join(', ') }} {{ (c.gameplayRoles ?? []).join(', ') }}</div>
                    <div>{{ c.line }}</div>
                  </div>
                </div>
              </section>
              <section class="sect">
                <h4 class="fold" @click="toggleFold('places')">
                  <span class="caret">{{ folded.places ? '▸' : '▾' }}</span> Places <span class="muted">{{ (outline.places ?? []).length }}</span>
                </h4>
                <div v-if="!folded.places" class="cards">
                  <div v-for="p in outline.places ?? []" :key="p.id" class="card">
                    <b>{{ p.name || p.id }}</b> <span class="badge">{{ p.kind }}</span>
                    <div>{{ p.line }}</div>
                  </div>
                </div>
              </section>
              <section v-if="(outline.connections ?? []).length" class="sect">
                <h4 class="fold" @click="toggleFold('connections')">
                  <span class="caret">{{ folded.connections ? '▸' : '▾' }}</span> Connections
                  <span class="muted">{{ outline.connections.length }}</span>
                </h4>
                <template v-if="!folded.connections">
                  <div v-for="(c, i) in outline.connections" :key="i" class="muted">
                    {{ c.from }} → {{ c.to }} ({{ c.kind }}){{ c.gate ? ' · opens with ' + c.gate : '' }}
                  </div>
                </template>
              </section>
            </template>
          </template>

          <!-- ③ Story: acts, chapters, the scene board -->
          <template v-else-if="view === 'story'">
            <div v-if="state.storyProgress?.total" class="story-progress">
              <span>Scenes written: <b>{{ state.storyProgress.written }}</b> of {{ state.storyProgress.total }}</span>
              <div class="meter"><div :style="{ width: (100 * state.storyProgress.written) / state.storyProgress.total + '%' }"></div></div>
              <span class="muted">Each act is planned first (its scene cards), then its scenes are written one by one.</span>
            </div>
            <div v-for="a in acts" :key="a.id" class="act">
              <h4>
                {{ a.id }} <span class="muted">{{ a.summary }}</span>
                <a href="#" @click.prevent="open(a.doc)">open</a>
              </h4>
              <div v-for="ch in a.chapters" :key="ch.id">
                <div class="muted chapter">{{ ch.id }} · {{ ch.question }}</div>
                <table class="grid">
                  <tbody>
                    <tr v-for="s in ch.scenes" :key="s.id" :class="['scene-row', s.progress]" @click="open(s.doc)">
                      <td>{{ s.id }}</td>
                      <td>{{ s.mode }}</td>
                      <td>{{ s.kernel ? 'kernel' : '' }}{{ s.branch ? ' · ' + s.branch : '' }}</td>
                      <td>{{ s.location }}</td>
                      <td class="wide">{{ s.summary || s.goal }}</td>
                      <td class="scene-state">
                        <span v-if="s.progress === 'planned'" class="muted" title="Its card is planned; the scene isn't written yet">planned</span>
                        <span v-else-if="s.progress === 'queued'" class="muted" title="Waiting its turn: scenes are written in order">queued</span>
                        <span v-else-if="s.progress === 'writing'" class="busy">writing…</span>
                        <span v-else-if="s.progress === 'rewrite-queued'" class="busy" :title="`The act’s review suggested an improvement: ${s.criticNote}`">improvement queued</span>
                        <span v-else-if="s.progress === 'rewriting'" class="busy" :title="`The act’s review suggested an improvement: ${s.criticNote}`">improving…</span>
                        <template v-else-if="s.progress === 'failed'">
                          <span class="err" :title="s.error">✗ failed</span>
                          <button class="mini" @click.stop="retryJob(s.job)">Retry</button>
                        </template>
                        <template v-else>
                          <span class="ok" :title="s.criticNote ? `Improved after the act’s review: ${s.criticNote}` : 'Written'">✓ Completed{{ s.criticNote ? " (improved)" : "" }}</span>
                          <span v-if="(s.issues ?? []).length" class="warn" :title="(s.issues ?? []).join('\n')"> ⚠ {{ (s.issues ?? []).length }}</span>
                          <span v-if="(s.notes ?? []).length" class="info" :title="(s.notes ?? []).join('\n')"> ℹ {{ (s.notes ?? []).length }}</span>
                        </template>
                        <span v-if="s.stale" class="badge warn">stale</span><span v-if="s.locked" class="badge">locked</span>
                      </td>
                    </tr>
                  </tbody>
                </table>
              </div>
            </div>
          </template>

          <!-- ④ ⑤ ⑥ boards -->
          <table v-else-if="view === 'characters' || view === 'quests' || view === 'places'" class="grid">
            <tbody>
              <tr v-for="r in (state[view] ?? []) as Row[]" :key="r.id" @click="open(r.doc)">
                <td><b>{{ r.name || r.id }}</b> <span class="muted">{{ r.id }}</span></td>
                <td v-if="view === 'characters'">{{ r.importance }}{{ r.player ? ' · player' : '' }}</td>
                <td v-if="view === 'quests'">{{ r.type }} · {{ r.stages }} stages · {{ r.motivation }}</td>
                <td v-if="view === 'places'">{{ r.kind }}</td>
                <td class="wide">{{ view === 'places' ? r.purpose : view === 'characters' ? r.line : r.strategy }}</td>
                <td>
                  <span v-if="view === 'characters' && !r.detailed" class="muted">to detail</span>
                  <span v-else-if="view === 'places' && !r.described" class="muted">to describe</span>
                  <span v-else-if="((r.problems as string[] | undefined) ?? []).length || (r.issues ?? []).length" class="warn"
                    :title="[...((r.problems as string[] | undefined) ?? []), ...(r.issues ?? [])].join('\n')">⚠</span>
                  <span v-else class="ok">✓ Completed</span>
                  <span v-if="((r.notes as string[] | undefined) ?? []).length" class="info" :title="((r.notes as string[] | undefined) ?? []).join('\n')">
                    ℹ {{ ((r.notes as string[] | undefined) ?? []).length }}</span>
                  <span v-if="r.stale" class="badge warn">stale</span><span v-if="r.locked" class="badge">locked</span>
                </td>
              </tr>
            </tbody>
          </table>

          <template v-else-if="view === 'systems'">
            <div v-if="state.systems" class="section">
              <div class="row"><span class="spacer"></span><a href="#" @click.prevent="open('systems')">open the document</a></div>
              <SystemsView :systems="state.systems" />
            </div>
            <div v-else class="muted">Not yet.</div>
          </template>

          <template v-else-if="view === 'visuals'">
            <div v-if="state.ui" class="section">
              <h4 class="fold">The interface <a href="#" @click.prevent="open('ui')">open</a></h4>
              <p class="muted">
                Chosen from the engine's interface kit: the theme every window wears, which windows the game has and what's
                on screen while playing. The windows themselves are made in that theme when the game is built (⑫).
              </p>
              <!-- In a reading order (the document's own keys come alphabetically), then anything else it has -->
              <DataTable :value="uiInOrder" />
            </div>
            <div v-else class="muted">Not yet.</div>
          </template>

          <template v-else-if="view === 'build'">
            <!-- How the places find their art: the project's setting, shared with the Build panel -->
            <div class="art-strategy">
              <h4>Art for the places</h4>
              <div>
                <b>{{ artSourceName(state.sourcing?.defaultSource ?? 'search') }}</b>
                <span class="muted"> · web search {{ state.sourcing?.webSearch ? 'on' : 'off' }} · share-alike {{ state.sourcing?.shareAlike ? 'allowed' : 'not allowed' }}</span>
              </div>
              <p class="muted">
                Set in the Game Brief: ① Concept ▸ Plan ▸ Art for the places
                <a href="#" @click.prevent="view = 'concept'; tab = 'plan'">change it</a>.
                A change is used by the builds started after it (Restart builds).
              </p>
            </div>
            <div v-if="!state.build?.active" class="section">
              <p>Build every place of the design, place the characters, bind the quests, write the game's files and playtest it.</p>
              <div class="row">
                <button class="primary" :disabled="!state.baseline" @click="sendToHost({ type: 'planner:buildGame', mode: 'automatic' })">Build automatically</button>
                <button :disabled="!state.baseline" @click="sendToHost({ type: 'planner:buildGame', mode: 'manual' })">Build each place myself</button>
              </div>
              <p v-if="!state.baseline" class="muted">The design needs a baseline first (⑨ Validate with no errors).</p>
            </div>
            <template v-else>
              <div class="build-now">
                <template v-if="buildingRow">
                  <span class="busy">Building <b>{{ buildingRow.place }}</b></span>
                  <span v-if="buildingRow.step" class="muted"> — {{ stepName(buildingRow.step, buildingRow.planPhase) }}</span>
                </template>
                <span v-else class="muted">{{ state.build.message }}</span>
                <span class="muted"> · {{ buildDone }} of {{ (state.build.rows ?? []).length }} places done</span>
                <span class="spacer"></span>
                <button v-if="buildRetryable.length" class="retry" @click="retryFailedPlaces">Retry failed ({{ buildRetryable.length }})</button>
                <button v-if="state.build.status === 'working'" class="stop" @click="sendToHost({ type: 'planner:stopBuilds' })">Stop building</button>
                <button v-else-if="state.build.stage !== 'done'" class="primary" title="Go on from the next place that isn't built; those that are stay"
                  @click="sendToHost({ type: 'planner:continueBuilds' })">Continue building</button>
                <button title="Build every place again from the start, with the art setting as it is now" @click="restartBuilds">Restart builds</button>
              </div>
              <table class="grid">
                <tbody>
                  <tr v-for="r in state.build.rows ?? []" :key="r.place" :class="{ 'building-now': r.status === 'building' }">
                    <td><b>{{ r.place }}</b> <span class="muted">{{ r.scene }}</span></td>
                    <td>{{ r.buildType }}</td>
                    <td>{{ r.mode }}</td>
                    <td :class="{ err: r.status === 'failed', warn: r.status === 'placeholder' || r.status === 'needs-you', busy: r.status === 'building' }">
                      {{ r.status === 'building' ? `building… ${r.step ? '(' + stepName(r.step, r.planPhase) + ')' : ''}` : r.status }}
                    </td>
                    <td class="wide muted">{{ r.error }}</td>
                    <td>
                      <button v-if="r.build" @click="sendToHost({ type: 'planner:buildRow', place: r.place, action: 'open' })">Open build</button>
                      <button v-if="r.status === 'needs-you'" @click="sendToHost({ type: 'planner:buildRow', place: r.place, action: 'done' })">Built</button>
                      <button v-if="r.status === 'failed' || r.status === 'placeholder'" @click="sendToHost({ type: 'planner:buildRow', place: r.place, action: 'retry' })">Retry</button>
                      <button v-if="r.status === 'built' || r.status === 'needs-you'" title="Build this place again from the start, with the art setting as it is now"
                        @click="rebuildPlace(r.place)">Rebuild</button>
                      <button v-if="r.status === 'failed'" @click="sendToHost({ type: 'planner:buildRow', place: r.place, action: 'placeholder' })">Placeholder</button>
                    </td>
                  </tr>
                </tbody>
              </table>
              <template v-if="(state.build.bindings?.aliases ?? []).length">
                <h4>⑪ Bindings</h4>
                <table class="grid">
                  <tbody>
                    <tr v-for="(a, i) in state.build.bindings.aliases" :key="i">
                      <td>{{ a.quest }}</td><td>{{ a.alias }}</td><td :class="{ err: a.how === 'failed' }">{{ a.how }}</td>
                      <td class="wide muted">{{ a.scene }} {{ a.marker || a.entity }} {{ a.reason }}</td>
                    </tr>
                  </tbody>
                </table>
              </template>
              <template v-if="state.build.playtest?.quests">
                <h4>⑫ Playtest — {{ state.build.playtest.summary?.passed }}/{{ state.build.playtest.summary?.total }} checks</h4>
                <div v-for="q in state.build.playtest.quests" :key="q.id">
                  <b>{{ q.id }}</b>: <span :class="{ err: q.result !== 'completed' }">{{ q.result }}</span> <span class="muted">{{ q.reason }}</span>
                </div>
                <div v-for="(c, i) in (state.build.playtest.checks ?? []).filter((c: any) => !c.ok)" :key="'c' + i" class="err">✗ {{ c.check }} {{ c.detail }}</div>
              </template>
              <template v-if="state.build.result">
                <h4>⑬ Result</h4>
                <p><b>{{ state.build.result.headline }}</b></p>
                <table class="grid">
                  <tbody>
                    <tr v-for="(a, i) in state.build.result.askedVsMade" :key="i"><td>{{ a.what }}</td><td>asked {{ a.asked }}</td><td>made {{ a.made }}</td></tr>
                  </tbody>
                </table>
                <div class="row"><button class="primary" @click="sendToHost({ type: 'planner:play' })">Play from the start</button></div>
              </template>
            </template>
          </template>

          <template v-else-if="view === 'validate'">
            <div v-if="state.report" class="section">
              <b>{{ openErrors }} errors, {{ openWarnings }} warnings, {{ acceptedCount }} accepted</b>
              <span v-if="fixingCount" class="busy">
                · Fixing {{ fixingRows.length === 1 ? 'item' : 'items' }} {{ ranges(fixingRows) }}<template v-if="fixingNow.length">
                  · now {{ fixingNow.length === 1 ? 'item' : 'items' }} {{ ranges(fixingNow) }}</template>
              </span>
              <span v-if="state.baseline" class="muted"> · baseline {{ state.baseline }}</span>
              <span v-if="fixable.length || fixBusy" class="fix-all" :title="fixableHow">
                <label for="fix-scope">Fix</label>
                <select id="fix-scope" v-model="fixScope" :disabled="fixBusy">
                  <option value="errors" :disabled="!fixableErrors">Errors ({{ fixableErrors }})</option>
                  <option value="warnings" :disabled="!fixableWarnings">Warnings ({{ fixableWarnings }})</option>
                  <option value="*">Both ({{ fixable.length }})</option>
                </select>
                <button class="primary" :disabled="fixBusy" :title="fixBusy ? 'A fix is in progress' : ''" @click="sendToHost({ type: 'planner:fixFindings', ids: [fixScope] })">Fix now</button>
                <button v-if="fixBusy" class="stop" title="Stop every fix: what isn't fixed yet goes back to open" @click="sendToHost({ type: 'planner:stopFixes' })">Stop fix</button>
              </span>
              <p class="muted">
                Errors stop the build until they're fixed or accepted; warnings don't. The Planner fixes what has a known
                remedy on its own once, then checks again. Fix does it for one finding; Accept keeps it as it is, with your reason.
              </p>
              <table class="grid">
                <tbody>
                  <tr v-for="(f, i) in findings" :key="f.id" :class="{ 'is-fixing': f.status === 'fixing', 'fixing-now': fixingNow.includes(i + 1) }">
                    <td class="row-number">{{ i + 1 }}</td>
                    <td class="step-cell">{{ f.step || '—' }}</td>
                    <td class="muted">{{ f.rule }}</td>
                    <td :class="f.severity === 'error' ? 'err' : 'warn'">{{ f.status === 'accepted' ? 'accepted' : f.severity }}</td>
                    <td>{{ f.subject }}</td>
                    <td class="wide">{{ f.message }}<span v-if="f.reason" class="muted"> — {{ f.reason }}</span></td>
                    <td class="finding-actions">
                      <span v-if="f.status === 'fixing'" class="busy">fixing…</span>
                      <button v-if="f.status === 'open' && f.remedy" class="primary" :title="f.remedy.how"
                        :disabled="fixBusy" @click="sendToHost({ type: 'planner:fixFindings', ids: [f.id] })">Fix</button>
                      <button v-if="f.status === 'open'" @click="accept(f)">Accept…</button>
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>
            <div v-else class="muted">Validation runs when the design is complete.</div>
          </template>
        </div>

        <!-- The item: its document, Why?, Regenerate, Edit, Lock -->
        <Splitter v-if="item" direction="vertical" @drag="resizeItem" />
        <div v-if="item" class="item" :style="{ height: itemHeight + 'px' }">
          <div class="item-bar">
            <b>{{ item.doc }}</b>
            <span class="spacer"></span>
            <button @click="askWhy(item.doc)">Why?</button>
            <button @click="sendToHost({ type: 'planner:regenerate', doc: item.doc })">Regenerate</button>
            <button @click="sendToHost({ type: 'planner:lock', doc: item.doc, on: !(item.content as any)?.locked })">
              {{ (item.content as any)?.locked ? 'Unlock' : 'Lock' }}
            </button>
            <button v-if="item.doc.startsWith('places/')" @click="sendToHost({ type: 'planner:buildPlace', doc: item.doc })">Build…</button>
            <button @click="item = null">×</button>
          </div>
          <div v-if="why" class="why">
            <div><b>Why?</b> {{ why.rationale || 'No rationale was recorded.' }}</div>
            <div v-if="why.decision" class="muted">{{ why.decision.id }} · {{ why.decision.actor }} · {{ why.decision.kind }} · {{ why.decision.time }}</div>
            <div v-for="(d, i) in why.history ?? []" :key="i" class="muted">{{ d.id }} {{ d.actor }} {{ d.kind }} {{ d.reason || '' }}</div>
          </div>
          <div v-if="item.content === null || item.content === undefined" class="empty">
            <span class="muted">This hasn’t been written yet.</span>
            <button class="primary" @click="sendToHost({ type: 'planner:regenerate', doc: item.doc })">Write it</button>
          </div>
          <template v-else-if="!rawJson">
            <div class="readable"><DocView :value="item.content" /></div>
            <div class="row"><button @click="rawJson = true">Edit JSON</button></div>
          </template>
          <template v-else>
            <textarea v-model="itemText" class="doc" spellcheck="false"></textarea>
            <div class="row">
              <button class="primary" @click="saveItem">Save changes</button>
              <button @click="rawJson = false">Back to the readable view</button>
            </div>
          </template>
        </div>
      </div>
    </template>
  </div>
</template>

<style scoped>
.planner {
  display: flex;
  flex-direction: column;
  height: 100%;
  min-height: 0;
}
.start {
  padding: 12px;
  display: flex;
  flex-direction: column;
  gap: 8px;
  max-width: 720px;
}
.choices .toggle {  background: none;  border: none;  padding: 4px 0;  cursor: pointer;  color: inherit;}.choice-rows {  display: flex;  flex-direction: column;  gap: 4px;  margin: 4px 0 6px;}.choice-row {  display: flex;  gap: 8px;  align-items: flex-start;}.choice-row .label {  width: 110px;  flex: none;  opacity: 0.7;  padding-top: 3px;}
.plans h4 {
  margin: 10px 0 4px;
}
.plan-row {
  display: flex;
  gap: 8px;
  align-items: center;
  padding: 6px 4px;
  border-bottom: 1px solid #2a2a3a;
}
.plan-row.current .what strong {
  color: #89b4fa;
}
.plan-row .what {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
}
.method-row {
  display: flex;
  align-items: center;
  gap: 6px;
  margin: 4px 0 8px;
}
.help {
  display: inline-block;
  width: 14px;
  height: 14px;
  line-height: 14px;
  text-align: center;
  font-size: 10px;
  border: 1px solid currentColor;
  opacity: 0.7;
  cursor: help;
}
.home-step {
  min-width: 28px;
}
.ask-remove {
  border: 1px solid #f38ba8;
  padding: 8px 10px;
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.ask-remove p {
  margin: 0;
}
.row {
  display: flex;
  gap: 6px;
}
.stepper {
  display: flex;
  flex-wrap: wrap;
  border-bottom: 1px solid #2a2a3a;
}
.step {
  padding: 4px 8px;
  border: none;
  border-right: 1px solid #2a2a3a;
  background: transparent;
}
.step.on {
  background: #313148;
  box-shadow: inset 0 -2px 0 #3b82f6;
}
.step.done .mark {
  color: #4ade80;
}
.step.failed .mark {
  color: #f87171;
}
.step.waiting .mark {
  color: #fbbf24;
}
.stale {
  margin-left: 4px;
  padding: 0 4px;
  background: #78350f;
  font-size: 0.8em;
}
.bar {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 4px 8px;
  border-bottom: 1px solid #2a2a3a;
}
.status.failed {
  color: #f87171;
}
.status.waiting {
  color: #fbbf24;
}
.spacer {
  flex: 1;
}
.notice {
  padding: 6px 10px;
  color: #93c5fd;
  border-bottom: 1px solid #2a2a3a;
  display: flex;
  gap: 6px;
  align-items: center;
}
.body {
  flex: 1;
  min-height: 0;
  display: flex;
  flex-direction: column;
}
.main {
  flex: 1;
  min-width: 0;
  min-height: 0;
  overflow: auto;
  padding: 8px 10px;
}
.item {
  flex: none;
  border-top: 1px solid #2a2a3a;
  display: flex;
  flex-direction: column;
  min-height: 0;
}
.item-bar {
  display: flex;
  gap: 4px;
  align-items: center;
  padding: 4px 6px;
  border-bottom: 1px solid #2a2a3a;
}
.why {
  padding: 6px;
  border-bottom: 1px solid #2a2a3a;
  font-size: 0.9em;
}
.doc {
  flex: 1;
  min-height: 0;
  font-family: monospace;
  font-size: 0.85em;
  resize: none;
  border: none;
  border-bottom: 1px solid #2a2a3a;
}
.tabs {
  display: flex;
  margin-bottom: 6px;
}
.tabs button.on {
  background: #313148;
  border-color: #3b82f6;
}
.grid {
  border-collapse: collapse;
  font-size: 0.9em;
  width: 100%;
}
.grid td {
  border: 1px solid #2a2a3a;
  padding: 2px 6px;
}
.grid tbody tr {
  cursor: pointer;
}
.grid .wide {
  width: 50%;
}
.form .label {
  width: 30%;
  white-space: nowrap;
}
.form tr.asked .label {
  font-weight: bold;
}
.chips {
  display: flex;
  flex-wrap: wrap;
  gap: 3px;
}
.chip {
  padding: 0 6px;
  font-size: 0.85em;
}
.chip.on {
  background: #313148;
  border-color: #3b82f6;
}
.range input {
  width: 70px;
}
.sect {
  margin-bottom: 12px;
}
.fix-all {
  margin-left: 14px;
  display: inline-flex;
  align-items: center;
  gap: 6px;
}
.fix-all label {
  color: #9a9ab0;
}
.fix-all select {
  min-width: 150px;
}
tr.is-fixing td {
  opacity: 0.6;
}
tr.fixing-now td {
  opacity: 1;
  background: #1d2433;
}
.row-number {
  color: #6a6a80;
  text-align: right;
  font-variant-numeric: tabular-nums;
  width: 1%;
}
.stop {
  border-color: #f87171;
  color: #fca5a5;
}
.step-cell {
  white-space: nowrap;
}
.finding-actions {
  white-space: nowrap;
}
.art-strategy {
  border: 1px solid #2a2a3a;
  padding: 6px 10px;
  margin-bottom: 10px;
}
.art-strategy h4 {
  margin: 0 0 6px;
}
.art-strategy .row {
  align-items: center;
  gap: 8px;
  margin-bottom: 4px;
}
.art-strategy .check {
  display: flex;
  align-items: center;
  gap: 6px;
}
.build-now {
  display: flex;
  align-items: baseline;
  gap: 4px;
  margin-bottom: 6px;
}
.build-now .spacer {
  flex: 1;
}
tr.building-now td {
  background: #1d2433;
}
.story-progress {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 4px 10px;
  margin-bottom: 8px;
}
.story-progress .muted {
  flex-basis: 100%;
  font-size: 0.85em;
}
.meter {
  width: 160px;
  height: 6px;
  background: #2a2a3a;
}
.meter div {
  height: 100%;
  background: #4ade80;
}
.scene-state {
  white-space: nowrap;
}
.scene-row.writing,
.scene-row.rewriting {
  background: #1d2433;
}
.scene-row.failed {
  background: #2a1c1f;
}
.busy {
  color: #93c5fd;
}
.info {
  color: #93c5fd;
  cursor: help;
}
.ok {
  color: #4ade80;
}
button.mini {
  padding: 0 6px;
  margin-left: 6px;
  font-size: 0.85em;
}
.step:disabled {
  opacity: 0.4;
  cursor: default;
}
.failed-count {
  color: #f87171;
  font-size: 0.8em;
  margin-left: 3px;
}
.retry {
  border-color: #f87171;
}
.failed-list {
  border: 1px solid #5b2a2a;
  background: #2a1c1f;
  margin-bottom: 8px;
}
.failed-row {
  display: flex;
  gap: 8px;
  align-items: center;
  padding: 3px 6px;
}
.failed-row + .failed-row {
  border-top: 1px solid #3a2a2e;
}
.why-failed {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  min-width: 0;
}
.readable {
  flex: 1;
  min-height: 0;
  overflow: auto;
  padding: 6px 10px;
  border-bottom: 1px solid #2a2a3a;
}
.fold {
  cursor: pointer;
  user-select: none;
  margin: 0 0 6px;
  padding: 3px 0;
  border-bottom: 1px solid #2a2a3a;
}
.fold:hover {
  color: #c7d2fe;
}
.caret {
  display: inline-block;
  width: 12px;
  color: #8a8aa0;
}
.cards {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(240px, 1fr));
  gap: 4px;
}
.cards .card {
  margin-bottom: 0;
}
.empty {
  padding: 12px;
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 6px;
}
.card {
  border: 1px solid #2a2a3a;
  padding: 4px 6px;
  margin-bottom: 4px;
}
.chapter {
  margin: 4px 0 2px;
}
.badge {
  font-size: 0.75em;
  padding: 0 4px;
  border: 1px solid #3a3a50;
  margin-left: 3px;
}
.badge.warn,
.warn {
  color: #f59e0b;
}
.err {
  color: #f87171;
}
.muted {
  color: #8a8aa0;
}
a {
  color: #93c5fd;
  text-decoration: none;
  margin-left: 6px;
}
</style>
