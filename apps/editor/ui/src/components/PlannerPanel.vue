<script setup lang="ts">
// The AI Planner (development_plan.md 22, docs/ai/ai-planner.md §6): one view per stage — the
// Game Brief's confirmation window, the outline (story, cast, places, stacked), the story's acts and scene
// board, the cast, quests and places, systems, visuals and the validation report — and on
// every item Why?, Regenerate, Edit and Lock. Documents whose inputs changed show as stale;
// "Regenerate stale" redoes exactly those. A place's draft Brief opens in the Build panel.
import { computed, ref, watch } from 'vue'
import AiProgress from './AiProgress.vue'
import { onHostMessage, sendToHost } from '../ipc'
import { activatePanel, setPanelVisible } from '../layout'

interface Stage {
  id: string
  label: string
  status: string
  done: number
  total: number
  stale: number
  review: boolean
}
interface WindowField {
  path: string
  label: string
  type: string
  value: unknown
  options?: string[]
  asked: boolean
  assumed: boolean
  mark?: { confidence?: number; differ?: unknown[]; guessed?: boolean }
  min?: number
  max?: number
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
  } else if (message.type === 'planner:document') {
    item.value = { doc: message.doc as string, content: message.content }
    itemText.value = JSON.stringify(message.content, null, 2)
    why.value = null
  } else if (message.type === 'planner:why') {
    why.value = message.why as Record<string, any>
  }
})
sendToHost({ type: 'planner:request' })

const stages = computed(() => (state.value.stages ?? []) as Stage[])
const windowTabs = computed(() => (state.value.window ?? []) as WindowTab[])
const currentTab = computed(() => windowTabs.value.find((t) => t.id === tab.value) ?? windowTabs.value[0])
const acts = computed(() => (state.value.acts ?? []) as Act[])
const findings = computed(() => ((state.value.report?.findings ?? []) as Finding[]))
const staleCount = computed(() => (state.value.stale ?? []).length as number)
const waiting = computed(() => state.value.status === 'waiting' || state.value.status === 'failed')
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

function start() {
  if (!request.value.trim()) return
  sendToHost({ type: 'planner:start', request: request.value })
}
function replace() {
  sendToHost({ type: 'planner:start', request: askReplace.value, replace: true })
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
    <div v-if="!state.active" class="start">
      <h3>Plan a game</h3>
      <p class="muted">
        Describe the game: the Planner reads it three ways, asks you to confirm a Game Brief, then designs the story, cast,
        quests, places, systems and interface, checking each step.
      </p>
      <textarea v-model="request" rows="4" placeholder="A short dark-fantasy action RPG about a disgraced warden recovering a stolen amulet, three towns and a dungeon, about four hours" />
      <div class="row">
        <button class="primary" :disabled="!request.trim()" @click="start">Plan</button>
        <button v-if="state.hasPlan" @click="sendToHost({ type: 'planner:request' })">Open the project's plan</button>
      </div>
    </div>

    <template v-else>
      <div class="stepper">
        <button
          v-for="s in stages"
          :key="s.id"
          :class="['step', s.status, { on: view === s.id }]"
          :title="`${s.label}: ${s.status}${s.total ? ` (${s.done}/${s.total})` : ''}`"
          @click="view = s.id"
        >
          {{ s.label }} <span class="mark">{{ statusMark(s.status) }}</span>
          <span v-if="s.stale" class="stale">{{ s.stale }}</span>
          <span v-if="s.review" class="badge">review</span>
        </button>
      </div>
      <div class="bar">
        <span :class="['status', state.status]">{{ state.message }}</span>
        <span class="spacer"></span>
        <span class="muted" :title="`Read ${(state.usage?.inputTokens ?? 0).toLocaleString()} tokens, wrote ${(state.usage?.outputTokens ?? 0).toLocaleString()}`">
          {{ state.usage?.calls ?? 0 }} AI calls · {{ (state.usage?.outputTokens ?? 0).toLocaleString() }} tokens written · {{ state.decisions }} decisions
        </span>
        <button v-if="staleCount" @click="sendToHost({ type: 'planner:regenerateStale' })">Regenerate stale ({{ staleCount }})</button>
        <button v-if="state.status === 'working'" @click="sendToHost({ type: 'planner:pause', on: true })">Pause</button>
        <button v-if="state.status === 'paused'" @click="sendToHost({ type: 'planner:pause', on: false })">Resume</button>
        <button v-if="waiting" class="primary" @click="sendToHost({ type: state.stage === 'concept' ? 'planner:confirm' : 'planner:approve' })">
          Continue
        </button>
        <!-- ⑩–⑬ only once the design has a baseline (⑨ Validate passed) or a build has begun -->
        <button v-if="state.baseline || state.build?.active" :class="['step-build', { on: view === 'build' }]" @click="view = 'build'">⑩–⑬ Build</button>
      </div>
      <AiProgress :paths="['/planner/']" />
      <div v-if="askReplace" class="notice">
        The project already has a plan. Replace it with a new one for "{{ askReplace }}"?
        <button @click="replace">Replace</button><button @click="askReplace = ''">Keep it</button>
      </div>

      <div class="body">
        <div class="main">
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
            <div v-for="a in acts" :key="a.id" class="act">
              <h4>
                {{ a.id }} <span class="muted">{{ a.summary }}</span>
                <a href="#" @click.prevent="open(a.doc)">open</a>
              </h4>
              <div v-for="ch in a.chapters" :key="ch.id">
                <div class="muted chapter">{{ ch.id }} · {{ ch.question }}</div>
                <table class="grid">
                  <tbody>
                    <tr v-for="s in ch.scenes" :key="s.id" @click="open(s.doc)">
                      <td>{{ s.id }}</td>
                      <td>{{ s.mode }}</td>
                      <td>{{ s.kernel ? 'kernel' : '' }}{{ s.branch ? ' · ' + s.branch : '' }}</td>
                      <td>{{ s.location }}</td>
                      <td class="wide">{{ s.summary || s.goal }}</td>
                      <td>
                        <span v-if="!s.written" class="muted">card</span>
                        <span v-else-if="(s.issues ?? []).length" class="warn" :title="(s.issues ?? []).join('\n')">⚠ {{ (s.issues ?? []).length }}</span>
                        <span v-else>✓</span>
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
                  <span v-else-if="((r.problems as string[] | undefined) ?? []).length || (r.issues ?? []).length" class="warn">⚠</span>
                  <span v-else>✓</span>
                  <span v-if="r.stale" class="badge warn">stale</span><span v-if="r.locked" class="badge">locked</span>
                </td>
              </tr>
            </tbody>
          </table>

          <template v-else-if="view === 'systems'">
            <div v-if="state.systems" class="section">
              <a href="#" @click.prevent="open('systems')">open</a>
              <div v-for="(v, k) in state.systems.summary ?? {}" :key="k"><b>{{ k }}</b>: {{ show(v) }}</div>
              <table class="grid">
                <tbody>
                  <tr v-for="(r, i) in state.systems.records ?? []" :key="i"><td>{{ r.type }}</td><td>{{ r.id }}</td><td class="wide">{{ show(r.record) }}</td></tr>
                </tbody>
              </table>
            </div>
            <div v-else class="muted">Not yet.</div>
          </template>

          <template v-else-if="view === 'visuals'">
            <div v-if="state.ui" class="section">
              <a href="#" @click.prevent="open('ui')">open</a>
              <div><b>Theme</b>: {{ state.ui.theme }}</div>
              <div><b>Screens</b>: {{ (state.ui.screens ?? []).join(', ') }}</div>
              <div><b>HUD</b>: {{ (state.ui.hud ?? []).join(', ') }}</div>
            </div>
            <div v-else class="muted">Not yet.</div>
          </template>

          <template v-else-if="view === 'build'">
            <div v-if="!state.build?.active" class="section">
              <p>Build every place of the design, place the characters, bind the quests, write the game's files and playtest it.</p>
              <div class="row">
                <button class="primary" :disabled="!state.baseline" @click="sendToHost({ type: 'planner:buildGame', mode: 'automatic' })">Build automatically</button>
                <button :disabled="!state.baseline" @click="sendToHost({ type: 'planner:buildGame', mode: 'manual' })">Build each place myself</button>
              </div>
              <p v-if="!state.baseline" class="muted">The design needs a baseline first (⑨ Validate with no errors).</p>
            </div>
            <template v-else>
              <div class="muted">{{ state.build.message }}</div>
              <table class="grid">
                <tbody>
                  <tr v-for="r in state.build.rows ?? []" :key="r.place">
                    <td><b>{{ r.place }}</b> <span class="muted">{{ r.scene }}</span></td>
                    <td>{{ r.buildType }}</td>
                    <td>{{ r.mode }}</td>
                    <td :class="{ err: r.status === 'failed', warn: r.status === 'placeholder' || r.status === 'needs-you' }">{{ r.status }}</td>
                    <td class="wide muted">{{ r.error }}</td>
                    <td>
                      <button v-if="r.build" @click="sendToHost({ type: 'planner:buildRow', place: r.place, action: 'open' })">Open build</button>
                      <button v-if="r.status === 'needs-you'" @click="sendToHost({ type: 'planner:buildRow', place: r.place, action: 'done' })">Built</button>
                      <button v-if="r.status === 'failed' || r.status === 'placeholder'" @click="sendToHost({ type: 'planner:buildRow', place: r.place, action: 'retry' })">Retry</button>
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
              <b>{{ state.report.summary?.errors }} errors, {{ state.report.summary?.warnings }} warnings, {{ state.report.summary?.accepted }} accepted</b>
              <span v-if="state.baseline" class="muted"> · baseline {{ state.baseline }}</span>
              <table class="grid">
                <tbody>
                  <tr v-for="f in findings" :key="f.id">
                    <td>{{ f.rule }}</td>
                    <td :class="f.severity === 'error' ? 'err' : 'warn'">{{ f.status === 'accepted' ? 'accepted' : f.severity }}</td>
                    <td>{{ f.subject }}</td>
                    <td class="wide">{{ f.message }}<span v-if="f.reason" class="muted"> — {{ f.reason }}</span></td>
                    <td>
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
        <div v-if="item" class="item">
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
          <div v-if="item.content === null || item.content === undefined" class="empty muted">
            “{{ item.doc }}” hasn’t been written yet.
          </div>
          <template v-else>
            <textarea v-model="itemText" class="doc" spellcheck="false"></textarea>
            <div class="row"><button @click="saveItem">Save changes</button></div>
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
  height: 45%;
  min-height: 160px;
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
