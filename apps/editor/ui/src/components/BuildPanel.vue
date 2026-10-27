<script setup lang="ts">
// The Build panel (development_plan.md 7.4, place-interaction.md A2): one
// build at a time, as a stepper: ① Request · ② Assets · ③ Plan · ④ Build ·
// ⑤ Result. Not modal: the viewport stays in view. The host (PlaceBuild)
// holds the Brief; every edit here goes back to it as (path, value).
import { computed, ref, watch } from 'vue'
import AiProgress from './AiProgress.vue'
import { aiCalls, callName } from '../aiProgress'
import { onHostMessage, sendToHost } from '../ipc'
import { openContextMenu } from '../contextMenu'
import { editorState } from '../editorState'
import { activatePanel, setPanelVisible } from '../layout'
import CatalogThumb from './CatalogThumb.vue'
import { amountText, countRange, layoutSketch, parseRange, setShare, toggleShare, type Shares } from '../buildwindow'

type Rect = [number, number, number, number]
// A kind of building a zone holds: how many the user set, or about how many the plan makes.
interface ZoneKind {
  module: string
  name: string
  count?: number
  estimate?: number
}
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
  unit?: string // what the amount counts: ward(s), house(s), how common
  holds?: { field: string; value: unknown; unit: string } // a zone: its buildings, each
  kinds?: ZoneKind[] // a zone: its kinds of building, each with how many
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
// The Build panel's choices (structured input beside the words): from the ontology (BuildChoices).
interface Choice {
  path: string
  label: string
  values?: string[]
  type?: 'count' | 'bool'
  min?: number
  max?: number
  hint?: string
  value?: unknown
}
interface ChoiceType {
  id: string
  name: string
  unit: string
  default: boolean
  classes: { id: string; label: string; count: [number, number] }[]
  fields: Choice[]
  features: { id: string; label: string }[]
  parts: { id: string; label: string; level: string; description: string }[]
}

interface BuildWindow {
  round: number
  buildTypeName: string
  name: string
  size: { class: string | null; buildings: unknown; area: unknown; unit?: string; classes: { id: string; buildings: [number, number]; side: [number, number] }[] }
  layout: { value: Shares; options: string[]; description: string | null }
  walled: { value: string | null; options: string[] }
  settings?: Choice[] // the root's own settings (7.10: a dungeon's levels, a building's floors)
  features: { value: string[]; options: string[] }
  style: Record<string, { value: unknown; options?: string[] }>
  components: Component[]
  marks: Record<string, Mark>
  assumptions?: string[]
  questions?: string[]
  decisions?: { question: string; assumed: string; path?: string; options?: string[]; current?: unknown }[]
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
  rel?: { type: string; to: string; weight?: number }[] // where it's placed, and why (Why?)
  notes?: string[] // what the fixes did to it
}
interface BuildState {
  fromPlanner?: boolean // started by the AI Planner: its art strategy is the Game Brief's
  busy?: boolean // working off the UI thread (planning, laying out)
  revisingFrom?: string
  // The place's background: its ground painted whole (or a picture picked for it), laid cell by cell.
  background?: { path: string; cellPx: number; picked?: boolean; provider?: string; calls?: number }
  // What the background is painted from (the planner's, editable): the place and its look, a line per kind of ground.
  backgroundDescription?: string
  backgroundMaking?: boolean
  afterBackground?: string // the art asked for (find, matched): started once the background is in
  // ③ Matched art: where it is, who made it, and what came of it.
  matched?: {
    state: 'making' | 'made' | 'cut' | 'failed'
    provider?: string
    error?: string
    report?: { pieces: number; grounds: number; flagged: number; skipped: { key: string; why: string }[] }
  }
  // The layout form (the place's layout fields): each with its values, what each means, its value now.
  layoutForm?: {
    field: string
    type: string
    description?: string
    default?: any
    value?: any
    values?: string[]
    meaning?: Record<string, string>
  }[]
  questions?: { text: string; about?: string; zone?: string }[] // ② Assets' confirming questions
  directions?: string
  decisionAnswers?: Record<string, string>
  // ③ Layout: the zones and where they go.
  layout?: {
    unit: string
    image?: string // the zones' own map
    view?: boolean // looked at while the plan stands: changing it discards the plan
    map?: string[]
    zones: {
      id: string
      module: string
      name: string
      level: string
      description: string
      placement: string
      rationale: string
      rel: { type: string; to: string }[]
      count?: unknown
      where?: string
      countField?: string
      kinds?: ZoneKind[]
      hasStyle?: boolean
      style?: unknown
      drawn?: unknown
      painted?: boolean
      area?: unknown
      space?: { cells: number; needed: number; ok: boolean }
    }[]
    stale?: boolean
    quality?: { zone: string; check: string; text: string }[]
    sketched?: boolean
    main?: unknown
    patterns?: string[]
    side?: number
    regions?: { id: string; cells: [number, number, number][] }[]
    paths?: { class: string; cells: [number, number][] }[]
  }
  planPhase?: string
  confirmProgress?: {
    steps: { id: string; label: string; state: 'done' | 'active' | 'waiting'; detail: string[] }[]
    fraction: number
    since: number
    now: number
  }
  id: string
  query: string
  stage: string
  round: number
  status: string
  errors: string[]
  notices?: string[] // what the build did on its own when something failed
  name?: string
  window?: BuildWindow
  progress?: { zones?: number; done: number; total?: number }
  // ④ Build's art (7.9): each piece found or made, try by try, as it comes.
  sourcing?: {
    key: string
    role: string
    description: string
    action: string
    attempts: number
    done: boolean
    passed: boolean
    made: { path: string; attempt: number; seen?: string; ok?: boolean }[]
    request?: string
    alternatives?: string
    verdict?: string // kept | rejected, the user's word on generated art
  }[]
  sourced?: NonNullable<BuildState['sourcing']> // what was made for this build (Refine after it)
  // ③ Assets: what each part's art is (the project's, the library's, made, or a placeholder).
  assetNeeds?: {
    key: string
    role: string
    concepts?: string[]
    source: string
    entry?: string
    name?: string
    why?: string
    uses?: number
    component?: string
    componentName?: string
    policy?: string
    override?: string // this need's own source ('' : its component's)
    title?: string // its name, e.g. "Cottage house"
    description?: string // what it's for (the search and the Art tool use it)
    decision?: { action?: string; reason?: string }
  }[]
  buildProgress?: { done?: number; total?: number; text?: string } // ④ Build, as it goes
  plan?: { rows: PlanRow[]; totals: { buildings: number; unit?: string; zones: number; area: unknown }; rationale: string }
  sketch?: { side: number; rects: { id: string; module: string; name: string; x: number; y: number; w: number; h: number }[] }
  problems?: Problem[]
  image?: string // places/<id>/layout.png: the solved layout as a map, with its legend
  solved?: { stats: Record<string, number>; checks: { id: string; predicate: string; ok: boolean; detail?: unknown }[] }
  verify?: VerifyRow[] // asked, planned, built
  brushModules?: { id: string; name: string; level: string }[] // what the module brush paints (7.8)
  sites?: { id: string; node: string; name: string; buildType: string; scale: string; bounds: number[] }[] // a region's (7.10)
  locks?: string[]
  result?: BuildResult // ⑤ Result: the scene build
  origin?: [number, number]
  revisions?: Revision[]
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
  { id: 'confirm', label: '① Confirm' },
  { id: 'layout', label: '② Layout' },
  { id: 'plan', label: '③ Plan' },
  { id: 'assets', label: '④ Assets' },
  { id: 'result', label: '⑤ Result' },
]

interface VerifyRow {
  id: string
  what: string
  asked?: unknown
  planned?: number
  built?: number
  status: 'ok' | 'short' | 'missing'
  note?: string
  zone?: boolean
  parts?: VerifyRow[]
}
interface SceneCheck {
  id: string
  what: string
  ok: boolean
  warning?: boolean
  detail: string
  items?: string[]
}
interface AssetNeed {
  key?: string
  role: string
  concepts?: string[]
  layer?: string
  uses?: number
  prefab?: string
  name?: string
  source: string
  licence?: string
  why?: string
  decision?: { step: string; action: string; reason: string; warn?: boolean }
}
interface BuildResult {
  assets: { needs: AssetNeed[]; terrains: Record<string, AssetNeed & { terrain?: string }>; pieces: Record<string, AssetNeed> }
  report: { cells: Record<string, number>; objects: Record<string, number>; missing: string[]; pieces: number }
  verify: SceneCheck[]
  repairs: { what: string; id: string; how: string }[]
  after: SceneCheck[]
  origin?: [number, number]
  image?: string // places/<id>/scene.png: the built place, as rendered
  critique?: { looking?: boolean; error?: string; summary?: string; issues?: string[] } // the AI's look at it
}
interface Revision {
  by: 'you' | 'review' | 'edit'
  time?: number // seconds since 1970
  redo?: boolean
  text: string
  summary: string
  now?: string
  checks?: { node: string; type: string; to: string; ok: boolean; detail: string }[]
  issues?: string[]
  notes?: string[]
  undo?: number
  undone?: boolean
}
interface PartField {
  name: string
  group: string
  type: string
  value?: unknown
  default?: unknown
  values?: string[]
  unit?: string
}
interface PartInfo {
  id: string
  base: string
  module: string
  name: string
  level: string
  path: string[]
  fields: PartField[]
  parts: { id: string; module: string; repeat: number }[]
  rationale: string
  locked: boolean
  repeat: number
}
interface BuildEntry {
  id: string
  query: string
  name: string
  stage: string
  updated: number // seconds since the epoch
}

const state = ref<BuildState | null>(null)
// Asset sourcing (7.9): the project's settings.
const sourcingSettings = ref({ webSearch: false, shareAlike: false, defaultSource: 'search' })
function setSourcing(change: { webSearch?: boolean; shareAlike?: boolean; defaultSource?: string }): void {
  sendToHost({ type: 'build:sourcing', ...change })
}
sendToHost({ type: 'build:sourcingRequest' })
// Editing after the build (7.8): the part selected in the viewport, and the module brush.
const selectedPart = ref<PartInfo | null>(null)
const brush = ref('')
function setBrush(module: string): void {
  brush.value = module
  sendToHost({ type: 'build:brush', module })
}
function editField(f: PartField, raw: string | boolean): void {
  if (!selectedPart.value) return
  let value: unknown = raw
  if (typeof raw === 'string') {
    if (raw === '') value = null
    else if (f.type === 'count' || f.type === 'number' || f.type === 'area' || f.type === 'length' || f.type === 'share') {
      value = raw.includes('-') ? raw.split('-').map((x) => Number(x.trim())) : Number(raw)
    } else if (f.type === 'size' || f.type === 'mix' || f.type === 'enumSet') {
      try {
        value = JSON.parse(raw)
      } catch {
        return
      }
    }
  }
  sendToHost({ type: 'build:editNode', node: selectedPart.value.id, field: f.name, value })
}
function fieldText(f: PartField): string {
  const v = f.value ?? null
  if (v === null) return ''
  if (Array.isArray(v) && v.length === 2 && typeof v[0] === 'number') return `${v[0]}-${v[1]}`
  return typeof v === 'object' ? JSON.stringify(v) : String(v)
}
const builds = ref<BuildEntry[]>([])
// The start screen shown over an open build (the header's Builds).
const browsing = ref(false)
const query = ref('')
const showAll = ref(true)

// The choices: the build type, its size, its main settings, features and parts.
// What's set here is sent with the words and kept as set; the rest the words decide.
const choiceTypes = ref<ChoiceType[]>([])
const choicesOpen = ref(false)
const chosenType = ref('')
const chosen = ref<Record<string, unknown>>({})
const partsChosen = ref<Record<string, boolean>>({})
const featuresChosen = ref<string[]>([])
const countLow = ref('')
const countHigh = ref('')
const choiceType = computed(() => choiceTypes.value.find((t) => t.id === chosenType.value))
function setType(id: string): void {
  chosenType.value = id
  chosen.value = {}
  partsChosen.value = {}
  featuresChosen.value = []
  countLow.value = countHigh.value = ''
}
function setChoice(path: string, value: unknown): void {
  const next = { ...chosen.value }
  if (value === '' || value === null || value === undefined) delete next[path]
  else next[path] = value
  chosen.value = next
}
// A part: in, then out, then the words decide.
function cyclePart(id: string): void {
  const next = { ...partsChosen.value }
  if (!(id in next)) next[id] = true
  else if (next[id]) next[id] = false
  else delete next[id]
  partsChosen.value = next
}
function toggleChosenFeature(id: string): void {
  featuresChosen.value = featuresChosen.value.includes(id) ? featuresChosen.value.filter((f) => f !== id) : [...featuresChosen.value, id]
}
function choices(): Record<string, unknown> {
  const out: Record<string, unknown> = {}
  if (chosenType.value) out.buildType = chosenType.value
  for (const [path, value] of Object.entries(chosen.value)) out[path] = value
  const lo = Number(countLow.value), hi = Number(countHigh.value || countLow.value)
  if (countLow.value !== '' && lo > 0) out['scale.buildings'] = { count: [lo, Math.max(lo, hi)] }
  for (const [id, on] of Object.entries(partsChosen.value)) out[`components.${id}.enabled`] = on
  if (featuresChosen.value.length) out.features = [...featuresChosen.value]
  return out
}
const hasChoices = computed(() => Object.keys(choices()).length > 0)
function countOf(v: unknown): string {
  if (typeof v === 'number') return String(v)
  if (v && typeof v === 'object' && 'count' in v) return String((v as { count: unknown }).count)
  return ''
}
sendToHost({ type: 'build:choicesRequest' })

onHostMessage((message) => {
  if (message.type === 'build:state') {
    if (typeof message.directions === 'string' && message.directions !== directions.value && document.activeElement?.id !== 'directions') {
      directions.value = message.directions
    }
    state.value = message.stage ? (message as unknown as BuildState) : null
    if (!state.value || ['cancelled', 'failed'].includes(state.value.stage)) sendToHost({ type: 'build:list' })
  } else if (message.type === 'build:part') {
    selectedPart.value = (message.part as PartInfo | null) ?? null
    brush.value = (message.brush as string) ?? ''
  } else if (message.type === 'build:advanced') {
    advancedDoc.value = message as unknown as { what?: string; file?: string; text?: string; cut?: boolean }
  } else if (message.type === 'build:sourcing') {
    sourcingSettings.value = {
      webSearch: Boolean(message.webSearch),
      shareAlike: Boolean(message.shareAlike),
      defaultSource: (message.defaultSource as string) || 'search',
    }
  } else if (message.type === 'build:choices') {
    choiceTypes.value = (message.types as ChoiceType[]) ?? []
  } else if (message.type === 'build:list') {
    builds.value = (message.builds as BuildEntry[]) ?? []
  } else if (message.type === 'build:open') {
    browsing.value = false
    setPanelVisible('build', true)
    activatePanel('build')
  }
})
sendToHost({ type: 'build:request' })
sendToHost({ type: 'build:list' })

const win = computed(() => state.value?.window ?? null)
const stage = computed(() => state.value?.stage ?? '')
const errors = computed(() => (win.value?.problems ?? []).filter((p) => !p.warning))
const warnings = computed(() => (win.value?.problems ?? []).filter((p) => p.warning))
const shownComponents = computed(() => {
  const all = (win.value?.components ?? []).filter((c) => showAll.value || c.inBrief)
  // What the place has first; then what it can have.
  return [...all].sort((x, y) => Number(y.enabled) - Number(x.enabled))
})
// The seconds the active step has run (the host's clock, ticking here between its messages).
const tick = ref(0)
setInterval(() => (tick.value += 1), 1000)
let progressAnchor = { host: 0, local: 0 }
const elapsed = computed(() => {
  const p = state.value?.confirmProgress
  if (!p) return 0
  void tick.value
  if (progressAnchor.host !== p.now) progressAnchor = { host: p.now, local: Date.now() / 1000 }
  return Math.max(0, Math.round(p.now - p.since + (Date.now() / 1000 - progressAnchor.local)))
})
// ① Confirm's "To decide": what control a decision's field takes.
type Decision = NonNullable<BuildWindow['decisions']>[number]
function decisionControl(d: Decision): 'enabled' | 'options' | 'text' | 'words' {
  const p = d.path ?? ''
  if (!p) return 'words'
  if (/^components\.[^.]+\.enabled$/.test(p)) return 'enabled'
  if (p === 'name') return 'text'
  if (decisionOptions(d).length) return 'options'
  return 'words'
}
function decisionOptions(d: Decision): string[] {
  const p = d.path ?? '', w = win.value
  if (!w) return []
  if (p === 'walled') return w.walled.options
  if (p === 'scale.class') return w.size.classes.map((c) => c.id)
  if (p.startsWith('style.')) return w.style[p.slice(6)]?.options ?? []
  const f = (w.settings ?? []).find((x) => x.path === p)
  return f?.values ?? []
}
// The assumptions no decision already says.
const otherAssumptions = computed(() => {
  const said = (win.value?.decisions ?? []).map((d) => d.assumed.toLowerCase())
  return (win.value?.assumptions ?? []).filter((a) => !said.some((s) => s && (a.toLowerCase().includes(s) || s.includes(a.toLowerCase()))))
})
// ① Confirm: the layout first; its tabs.
// A step looked at again without going back to it (the plan from ③ Assets, ① Confirm from the plan): '' is the stage's own.
const viewing = ref('')
const planStands = computed(() => ['plan', 'planned', 'sourced', 'built'].includes(stage.value) && !!state.value?.layout?.view)
const atConfirm = computed(
  () => stage.value === 'layout' || (stage.value === 'revising' && state.value?.revisingFrom === 'layout') || (viewing.value === 'confirm' && planStands.value),
)
const atLayout = atConfirm
// ① Confirm looked at while the plan stands: changing anything discards the plan (asked first).
const confirmLocked = computed(() => atConfirm.value && planStands.value)
const discardAsk = ref(false)
function guardLocked(e: Event): void {
  if (!confirmLocked.value) return
  const target = e.target as HTMLElement | null
  if (target?.closest('[data-view-ok]')) return
  e.preventDefault()
  e.stopPropagation()
  if (e.type === 'click' || e.type === 'keydown') discardAsk.value = true
}
function discardPlan(): void {
  discardAsk.value = false
  viewing.value = ''
  sendToHost({ type: 'build:step', step: 'layout' })
}
function backToPlan(): void {
  viewing.value = stage.value === 'plan' ? '' : 'plan'
}
const confirmTab = ref<'layout' | 'contents'>('contents')
const showAssets = computed(
  () =>
    (['planned', 'sourcing', 'sourced'].includes(stage.value) && viewing.value !== 'plan' && viewing.value !== 'confirm') ||
    (['built', 'building'].includes(stage.value) && viewing.value === 'assets'),
)
// ③ Assets' tabs: how each part's art will be found, the background (painted first), and the art found and made.
const assetsTab = ref<'how' | 'background' | 'found'>('how')
const assetsFound = computed(() => ['sourcing', 'sourced', 'building', 'built'].includes(stage.value))
const artStarted = computed(() => assetsFound.value || !!state.value?.afterBackground || state.value?.matched?.state === 'making')
// To the art when work on it starts (finding or making it, building): choosing how a part is found re-previews the
// plan (which can make it "sourced") and must leave the user where they are.
watch(stage, (s) => {
  if (s === 'sourcing' || s === 'building') {
    if (assetsTab.value !== 'background') assetsTab.value = 'found'
  } else if (s === 'planned' && !state.value?.backgroundMaking && state.value?.matched?.state !== 'making') assetsTab.value = 'how'
})
// The background painted: on to the art (still there to look at in its tab).
watch(
  () => state.value?.backgroundMaking,
  (making, was) => {
    if (was && !making && assetsTab.value === 'background') assetsTab.value = 'found'
  },
)
// One button for the art: made in one style when the build generates it, else found part by part. The background
// first, when there's none yet.
const makesArt = computed(() => sourcingSettings.value.defaultSource === 'generate')
function startArt(): void {
  sendToHost({ type: 'build:findAssets', then: makesArt.value ? 'matched' : 'find' })
  assetsTab.value = state.value?.background ? 'found' : 'background'
}
// ④ Result: the built place alone (the plan is ② Plan's).
const showResult = computed(() => stage.value === 'built' && viewing.value !== 'plan' && viewing.value !== 'assets')
// What each part's art will be, in words.
const ACTIONS: Record<string, string> = {
  use: "the project's art",
  import: 'from the library',
  generate: 'made by the Art tool',
  web: 'searched on the web',
  placeholder: 'a placeholder',
  drop: 'left out',
}
function willGet(n: { source: string; decision?: { action?: string } }): string {
  const action = n.decision?.action ? ACTIONS[n.decision.action] : undefined
  if (n.source === 'placeholder' && action) return action
  if (n.source === 'generated') return 'made (already)'
  return SOURCE_NAMES[n.source] ?? n.source
}
const SOURCE_NAMES: Record<string, string> = { project: 'the project', library: 'the library', generated: 'made', web: 'the web', placeholder: 'placeholder', dropped: 'left out' }
const neededBySource = computed(() => {
  const counts: Record<string, number> = {}
  for (const n of state.value?.assetNeeds ?? []) counts[n.source || 'placeholder'] = (counts[n.source || 'placeholder'] ?? 0) + 1
  return counts
})
const activeStep = computed(() => {
  const s = stage.value
  if (s === 'interpreting' || (s === 'planning' && state.value?.planPhase === 'root')) return 'confirm'
  if (atConfirm.value) return confirmTab.value === 'layout' ? 'layout' : 'confirm'
  if (showAssets.value) return 'assets'
  if (s === 'building') return 'result'
  if (s === 'built') return viewing.value === 'plan' ? 'plan' : 'result'
  if (['planning', 'plan', 'planned', 'sourcing', 'sourced', 'revising'].includes(s)) return 'plan'
  return 'confirm'
})
const REACHED: Record<string, string[]> = {
  confirm: ['layout', 'planning', 'plan', 'planned', 'revising', 'sourcing', 'sourced', 'building', 'built'],
  layout: ['layout', 'plan', 'planned', 'revising', 'sourcing', 'sourced', 'building', 'built'],
  plan: ['plan', 'planned', 'revising', 'sourcing', 'sourced', 'building', 'built'],
  assets: ['planned', 'sourcing', 'sourced', 'building', 'built'],
  result: ['building', 'built'],
}
function stepDisabled(id: string): boolean {
  // While revising, as the step it's revising: a layout revised at ① Confirm doesn't open ③ Plan.
  const at = stage.value === 'revising' && state.value?.revisingFrom ? state.value.revisingFrom : stage.value
  if (REACHED[id]) return !REACHED[id].includes(at)
  return !win.value
}
function step(id: string): void {
  // ① Confirm (the contents) and ② Layout are the same stage, two pages.
  if (id === 'confirm' || id === 'layout') confirmTab.value = id === 'layout' ? 'layout' : 'contents'
  if ((id === 'confirm' || id === 'layout') && planStands.value) {
    // ① Confirm as the plan was made from it; the plan stays (changing it there asks first).
    viewing.value = 'confirm'
  } else if ((id === 'confirm' || id === 'layout') && ['plan', 'planned', 'built'].includes(stage.value)) {
    sendToHost({ type: 'build:step', step: 'layout' })
  } else if (id === 'plan' && stage.value === 'plan') {
    viewing.value = ''
  } else if (id === 'assets' && ['planned', 'sourcing', 'sourced'].includes(stage.value)) {
    viewing.value = ''
  } else if (id === 'build') {
    viewing.value = ''
  } else if (id === 'plan' || id === 'assets') {
    viewing.value = id
  } else if (id === 'result') {
    viewing.value = ''
  }
}
// The main layout pattern (a mix shows its largest share).
const mainPattern = computed(() => {
  const m = state.value?.layout?.main as unknown
  if (typeof m === 'string') return m
  if (m && typeof m === 'object') {
    const shares = ('mix' in m ? (m as { mix: Record<string, number> }).mix : m) as Record<string, number>
    let best = 'organic', top = -1
    for (const [k, v] of Object.entries(shares)) if (typeof v === 'number' && v > top) (best = k), (top = v)
    return best
  }
  return 'organic'
})
function areaOf(v: unknown): string {
  if (typeof v === 'number') return String(v)
  if (v && typeof v === 'object' && 'area' in v) return String((v as { area: unknown }).area)
  return ''
}
function zoneName(id: string): string | undefined {
  return state.value?.layout?.zones.find((z) => z.id === id)?.name
}

// The drawing tool: the zones as laid out; a zone picked on the right is dragged where it goes.
const DRAW_SIZE = 560
const drawing = ref(false)
const drawZone = ref('')
const drawCanvas = ref<HTMLCanvasElement | null>(null)
const drawWarnings = computed(() =>
  (state.value?.layout?.zones ?? [])
    .filter((z) => z.space && !z.space.ok)
    .map((z) => `${z.name}: ${z.space?.cells} cells, about ${z.space?.needed} needed`),
)
function openDrawing(): void {
  drawing.value = true
  setTimeout(redraw, 0)
}
function cellAt(e: MouseEvent): [number, number] {
  const side = state.value?.layout?.side ?? 64
  const rect = (e.target as HTMLCanvasElement).getBoundingClientRect()
  const k = side / rect.width
  return [Math.max(0, Math.min(side - 1, Math.floor((e.clientX - rect.left) * k))), Math.max(0, Math.min(side - 1, Math.floor((e.clientY - rect.top) * k)))]
}
// The brush: a round stroke of cells, sent when the button comes up.
const brushSize = ref(3)
const erasing = ref(false)
const stroke = ref(new Set<number>())
const painting = ref(false)
function dab(e: MouseEvent): void {
  const side = state.value?.layout?.side ?? 64
  const [cx, cy] = cellAt(e)
  const r = (brushSize.value - 1) / 2
  for (let y = Math.floor(cy - r); y <= Math.ceil(cy + r); ++y) {
    for (let x = Math.floor(cx - r); x <= Math.ceil(cx + r); ++x) {
      if (x < 0 || y < 0 || x >= side || y >= side) continue
      if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= (r + 0.5) * (r + 0.5)) stroke.value.add(y * side + x)
    }
  }
}
function drawStart(e: MouseEvent): void {
  if (!drawZone.value) return
  painting.value = true
  stroke.value = new Set<number>()
  dab(e)
  redraw()
}
function drawMove(e: MouseEvent): void {
  if (!painting.value) return
  dab(e)
  redraw()
}
function drawEnd(): void {
  if (!painting.value) return
  painting.value = false
  const side = state.value?.layout?.side ?? 64
  const cells = [...stroke.value].map((c) => [c % side, Math.floor(c / side)])
  stroke.value = new Set<number>()
  if (cells.length) sendToHost({ type: 'build:paintZone', zone: drawZone.value, cells, erase: erasing.value })
  redraw()
}
function redraw(): void {
  const canvas = drawCanvas.value
  const layout = state.value?.layout
  if (!canvas || !layout) return
  const g = canvas.getContext('2d')
  if (!g) return
  const side = layout.side ?? 64
  const k = DRAW_SIZE / side
  g.fillStyle = '#86b25f'
  g.fillRect(0, 0, DRAW_SIZE, DRAW_SIZE)
  layout.regions?.forEach((r) => {
    const i = layout.zones.findIndex((z) => z.id === r.id)
    g.fillStyle = ZONE_COLOURS[(i < 0 ? 0 : i) % ZONE_COLOURS.length] ?? '#a6e3a1'
    g.globalAlpha = r.id === drawZone.value ? 1 : 0.75
    for (const [y, x0, x1] of r.cells) g.fillRect(x0 * k, y * k, (x1 - x0) * k, k)
  })
  g.globalAlpha = 1
  layout.paths?.forEach((p) => {
    g.fillStyle = p.class === 'river' ? '#4f8fc4' : '#c9a874'
    for (const [x, y] of p.cells) g.fillRect(x * k, y * k, Math.max(1, k), Math.max(1, k))
  })
  // Drawn zones: their rectangles.
  g.lineWidth = 2
  layout.zones.forEach((z) => {
    const d = z.drawn as { x: number; y: number; w: number; h: number } | undefined
    if (!d) return
    g.strokeStyle = z.id === drawZone.value ? '#ffffff' : '#333333'
    g.strokeRect(d.x * k, d.y * k, d.w * k, d.h * k)
  })
  if (stroke.value.size) {
    const i = layout.zones.findIndex((z) => z.id === drawZone.value)
    g.fillStyle = erasing.value ? 'rgba(134, 178, 95, 0.9)' : ZONE_COLOURS[(i < 0 ? 0 : i) % ZONE_COLOURS.length] ?? '#fff'
    for (const c of stroke.value) g.fillRect((c % side) * k, Math.floor(c / side) * k, k, k)
  }
  // Names on the zones.
  g.fillStyle = '#111'
  g.font = '11px sans-serif'
  layout.regions?.forEach((r) => {
    if (!r.cells.length) return
    let sx = 0, sy = 0, n = 0
    for (const [y, x0, x1] of r.cells) (sx += ((x0 + x1) / 2) * (x1 - x0)), (sy += y * (x1 - x0)), (n += x1 - x0)
    if (n) g.fillText(zoneName(r.id) ?? r.id, (sx / n) * k - 20, (sy / n) * k)
  })
}
watch(() => state.value?.layout, () => {
  if (drawing.value) setTimeout(redraw, 0)
})
watch(drawZone, () => redraw())
watch(stage, () => {
  viewing.value = ''
})
// ② Assets' answers to the questions, in the user's words: sent as they're written (on leaving the box).
const directions = ref('')
function saveDirections(): void {
  sendToHost({ type: 'build:directions', text: directions.value })
}
const ZONE_COLOURS = ['#a6e3a1', '#fab387', '#cba6f7', '#f9e2af', '#94e2d5', '#f38ba8']
const selectedZone = ref<string | null>(null)
const selectedRow = computed(() => state.value?.plan?.rows.find((r) => r.id === selectedZone.value) ?? null)
const MARKS: Record<string, string> = { ok: '✓', short: '!', missing: '✗' }
// ④ Build into the scene (7.7).
const SCENE_BUILD = true
const sceneFailing = computed(() => (state.value?.result?.after ?? []).filter((c) => !c.ok && !c.warning))
const builtCount = computed(() => {
  const r = state.value?.result?.report
  return r ? Object.values(r.objects).reduce((a, b) => a + b, 0) + (r.pieces ?? 0) : 0
})
const groundCells = computed(() => Object.values(state.value?.result?.report.cells ?? {}).reduce((a, b) => a + b, 0))
// Every asset the build used, placeholders first.
const assetRows = computed(() => {
  const a = state.value?.result?.assets
  if (!a) return []
  const rows: (AssetNeed & { key: string; label: string })[] = []
  for (const n of a.needs) {
    const what = n.concepts?.length ? `${n.concepts.join(', ')} (${n.role})` : n.role
    rows.push({ ...n, key: n.key ?? what, label: what })
  }
  for (const [slot, p] of Object.entries(a.pieces ?? {})) rows.push({ ...p, key: 'pieces:' + slot, label: `${slot} pieces` })
  for (const [t, row] of Object.entries(a.terrains ?? {})) {
    rows.push({ ...row, role: 'ground', key: 'ground:' + t, label: `${t} ground`, name: row.terrain })
  }
  return rows.sort((x, y) => Number(y.source === 'placeholder') - Number(x.source === 'placeholder'))
})
const placeholderKinds = computed(() => assetRows.value.filter((r) => r.source === 'placeholder').length)
const revision = ref('')
const verifyIssues = computed(() => {
  const out: VerifyRow[] = []
  for (const row of state.value?.verify ?? []) {
    if (row.status !== 'ok' && !(row.parts ?? []).some((p) => p.status !== 'ok')) out.push(row)
    for (const p of row.parts ?? []) if (p.status !== 'ok') out.push(p)
  }
  return out
})
// What Verify found, as a request to the AI.
const fixText = computed(
  () =>
    'Fix what the layout is missing: ' +
    verifyIssues.value
      .map((r) => `${r.what} (${r.id})${r.note ? `: ${r.note}` : ''}, planned ${r.planned ?? '?'}, built ${r.built ?? 0}`)
      .join('; '),
)
const canUndo = computed(() => (state.value?.revisions ?? []).some((r) => r.undo !== undefined && !r.undone))
const canRedo = computed(() => (state.value?.revisions ?? []).some((r) => r.undone && r.redo))
// What Undo and Redo would take back or bring back (for their tooltips).
const undoText = computed(() => {
  const list = (state.value?.revisions ?? []).filter((r) => r.undo !== undefined && !r.undone)
  const last = list[list.length - 1]
  return last ? 'Undo: ' + last.text : 'Nothing to undo'
})
const redoText = computed(() => {
  const next = (state.value?.revisions ?? []).find((r) => r.undone && r.redo)
  return next ? 'Redo: ' + next.text : 'Nothing to redo'
})
// The build's history, shown open or folded.
const historyOpen = ref(true)
// Advanced (8.2): the build's own documents.
const advanced = ref(false)
const advancedWhat = ref('pll')
const advancedDoc = ref<{ what?: string; file?: string; text?: string; cut?: boolean } | null>(null)
const ADVANCED = [
  { id: 'pll', label: 'PLL' },
  { id: 'solved', label: 'Solved layout' },
  { id: 'spec', label: 'Spec' },
  { id: 'brief', label: 'Brief' },
  { id: 'assets', label: 'Assets' },
  { id: 'log', label: 'Log' },
]
function showAdvanced(what: string): void {
  advancedWhat.value = what
  advancedDoc.value = null
  sendToHost({ type: 'build:advanced', what })
}
// Removing older builds, asked first; one build (its row's ✕), asked first too.
const removeAsk = ref(false)
const removeOne = ref<BuildEntry | null>(null)
// ③ Find the assets: the background painted first, its steps as the service reports them (the whole, then each window).
const backgroundCall = computed(() => aiCalls.value.find((c) => c.path === '/places/background' && c.phase !== 'done' && c.phase !== 'failed'))
// The next step working (off the UI thread, or a revision being written by the AI): an overlay over the panel with a
// spinner, what's being done, how long, and the AI call it waits on.
const working = computed(() => !!state.value?.busy || stage.value === 'revising')
const workingSince = ref(0)
const workingNow = ref(0)
let workingTimer: number | undefined
watch(working, (on) => {
  window.clearInterval(workingTimer)
  if (!on) return
  workingSince.value = workingNow.value = Date.now()
  workingTimer = window.setInterval(() => (workingNow.value = Date.now()), 500)
}, { immediate: true })
const workingSeconds = computed(() => Math.round((workingNow.value - workingSince.value) / 1000))
const workingCall = computed(() => aiCalls.value.find((c) => c.path.startsWith('/places/') && c.phase !== 'done' && c.phase !== 'failed'))
// Each step's AI calls, tokens and seconds (as in the Planner): every call of the build seen to end on the progress
// strip is added to the step the build was at when it began (build:usage; kept with the build).
type StepUsage = { calls: number; inputTokens: number; outputTokens: number; seconds: number }
function stepOfStage(): string {
  const s = stage.value === 'revising' && state.value?.revisingFrom ? state.value.revisingFrom : stage.value
  if (s === 'interpreting' || s === 'questions' || (s === 'planning' && state.value?.planPhase === 'root')) return 'confirm'
  if (s === 'layout') return 'layout'
  if (s === 'planning' || s === 'plan') return 'plan'
  if (s === 'planned' || s === 'sourcing' || s === 'sourced') return 'assets'
  if (s === 'building' || s === 'built') return 'result'
  return ''
}
const callStep = new Map<number, string>() // a call in flight: the step it began in
const counted = new Set<number>()
watch(aiCalls, (calls) => {
  for (const c of calls) {
    if (!c.path.startsWith('/places/') && !c.path.startsWith('/assets/')) continue
    if (!callStep.has(c.id)) callStep.set(c.id, stepOfStage())
    if ((c.phase === 'done' || c.phase === 'failed') && !counted.has(c.id)) {
      counted.add(c.id)
      const step = callStep.get(c.id) ?? ''
      if (step && state.value) sendToHost({ type: 'build:usage', step, inputTokens: c.inputTokens, outputTokens: c.outputTokens, seconds: c.seconds })
    }
  }
})
function usageOf(id: string): StepUsage | undefined {
  return (state.value as { usage?: Record<string, StepUsage> } | null)?.usage?.[id]
}
function shortTokens(n: number): string {
  return n >= 1000 ? (n / 1000).toFixed(n >= 10000 ? 0 : 1) + 'k' : String(n)
}
function shortTime(s: number): string {
  return s >= 60 ? `${Math.floor(s / 60)}m${String(Math.round(s % 60)).padStart(2, '0')}` : `${Math.round(s)}s`
}
function stepUsage(id: string): string {
  const u = usageOf(id)
  return u && u.calls ? `${shortTokens(u.outputTokens)} · ${shortTime(u.seconds)}` : ''
}
function usageTitle(id: string): string {
  const u = usageOf(id)
  return u && u.calls
    ? `${u.calls} AI call(s): read ${u.inputTokens.toLocaleString()} tokens, wrote ${u.outputTokens.toLocaleString()}, ${shortTime(u.seconds)} of AI time`
    : ''
}
// ③ Making the art: its steps, known beforehand (each sheet and the pieces on it, the check, the fixes when decided).
const matchedCall = computed(() => aiCalls.value.find((c) => c.path === '/assets/matched' && c.phase !== 'done' && c.phase !== 'failed'))
// ④ Assets: each need's own source; every need's at once.
const NEED_SOURCES = [
  { id: 'auto', label: 'auto' },
  { id: 'project', label: 'the project' },
  { id: 'library', label: 'the library' },
  { id: 'web', label: 'the web' },
  { id: 'search', label: 'search (project, library, web)' },
  { id: 'generate', label: 'generate' },
  { id: 'placeholder', label: 'a placeholder' },
]
function setNeedSource(key: string, source: string): void {
  sendToHost({ type: 'build:needSource', key, source })
}
function setAllSources(source: string): void {
  if (source) sendToHost({ type: 'build:allSources', source })
}
// What's still a placeholder once the art was looked for: another way, at once.
function retryNeed(key: string, source: string): void {
  sendToHost({ type: 'build:needSource', key, source })
  sendToHost({ type: 'build:findAssets' })
}
const unfound = computed(() =>
  (state.value?.assetNeeds ?? []).filter((n) => n.source === 'placeholder' && n.decision?.action !== 'placeholder'),
)
// Picking a piece of art for a need by hand: from the project's images, or the user's library.
const pickFor = ref('')
const pickTab = ref<'project' | 'library'>('library')
const pickFilter = ref('')
type PickMeta = { roles?: string; tags?: string; description?: string; kind?: string; name?: string }
const projectImages = ref<{ path: string; name?: string; analysis?: PickMeta }[]>([])
const libraryItems = ref<{ id: string; name: string; path: string; source: string; meta?: PickMeta }[]>([])
onHostMessage((message) => {
  if (message.type === 'assets:list') {
    const list = (message.assets as { path: string; type?: string; name?: string; analysis?: PickMeta }[]) ?? []
    projectImages.value = list.filter((a) => /\.(png|jpg|jpeg)$/i.test(a.path) && !a.path.includes('/placeholders/'))
  } else if (message.type === 'library:state') {
    libraryItems.value = (message.items as typeof libraryItems.value) ?? []
  }
})
function openPicker(key: string): void {
  pickFor.value = key
  // The need in plain words ("light prop" for light-prop): what it's for, not its id.
  const need = (state.value?.assetNeeds ?? []).find((n) => n.key === key)
  pickFilter.value = (need?.role.split('|')[0] ?? '').replace(/-/g, ' ')
  sendToHost({ type: 'library:get' })
}
// A search over what each piece is for: its name, its roles, tags, kind and description, every
// word of the search in one of them ("wall torch" finds a torch whose role is wall-torch).
function hay(name: string, path: string, m?: PickMeta): string {
  return [name, path, m?.name, m?.roles, m?.tags, m?.kind, m?.description].filter(Boolean).join(' ').toLowerCase().replace(/[-_]/g, ' ')
}
function rolesOf(c: unknown): string {
  const m = (c as { meta?: PickMeta; analysis?: PickMeta }).meta ?? (c as { analysis?: PickMeta }).analysis
  return m?.roles ?? ''
}
const pickChoices = computed(() => {
  const words = pickFilter.value.trim().toLowerCase().replace(/[-_]/g, ' ').split(/\s+/).filter(Boolean)
  const match = (h: string) => words.every((w) => h.includes(w))
  return pickTab.value === 'project'
    ? projectImages.value.filter((a) => match(hay(a.name ?? '', a.path, a.analysis))).slice(0, 200)
    : libraryItems.value.filter((i) => match(hay(i.name, i.path, i.meta))).slice(0, 200)
})
function pick(choice: { path: string; id?: string }): void {
  if (pickTab.value === 'library' && choice.id) sendToHost({ type: 'build:pickArt', key: pickFor.value, libraryItem: choice.id })
  else sendToHost({ type: 'build:pickArt', key: pickFor.value, path: choice.path })
  pickFor.value = ''
}
function revise(text?: string): void {
  const request = (text ?? revision.value).trim()
  if (!request) return
  sendToHost({ type: 'build:revise', text: request })
  if (text === undefined) revision.value = ''
}
const bigMap = ref(false)
// The panel's context menu (right-click its header): the ontology it builds from, and the builds.
function headerMenu(e: MouseEvent): void {
  openContextMenu(e.clientX, e.clientY, [
    { label: 'Ontology…', action: () => (editorState.showOntology = true) },
    { label: 'Your builds', action: () => browse() },
  ])
}
// How many of one kind of building a zone holds (empty: the plan decides).
function setKind(module: string, kind: string, value: string): void {
  sendToHost({ type: 'build:zoneKind', module, kind, value: value.trim() === '' ? null : Number(value) })
}

// The background's description: edited here, sent when the field is left (or with Generate).
const backgroundText = ref('')
watch(
  () => state.value?.backgroundDescription,
  (d) => {
    if (typeof d === 'string') backgroundText.value = d
  },
  { immediate: true },
)

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
// A piece of art seen large (click a thumbnail; click or Esc to close).
const zoomed = ref<{ path: string; caption: string } | null>(null)
function zoom(path: string, caption: string): void {
  zoomed.value = { path, caption }
}
window.addEventListener('keydown', (e) => {
  if (e.key === 'Escape') zoomed.value = null
})
// A made piece shown in its card: the last that passed the check, else the last made (the user can keep it anyway).
type MadeArt = { path: string; ok?: boolean; seen?: string }
function shownArt(n: { made: MadeArt[] }): MadeArt[] {
  const good = n.made.filter((x) => x.ok !== false)
  return good.length ? good.slice(-1) : n.made.slice(-1)
}
// A need still missing whose art was made but rejected by the check: that art (the latest), to look at and Keep.
function rejectedOf(key: string): MadeArt | undefined {
  const made = [...(state.value?.sourced ?? []), ...(state.value?.sourcing ?? [])].find((s) => s.key === key)?.made ?? []
  return made.filter((m) => m.ok === false).slice(-1)[0]
}
// Keep: the shown piece is the need's art, by the user's word (a piece the check rejected too).
function keepArt(n: { key: string; made: MadeArt[] }): void {
  const shown = shownArt(n)[0]
  if (shown && shown.ok === false) sendToHost({ type: 'build:pickArt', key: n.key, path: shown.path })
  sendToHost({ type: 'build:artVerdict', key: n.key, verdict: 'kept' })
}

// Refine one piece of the build's art: the user's words, then it's made again.
const refining = ref('')
const refineText = ref('')
function openRefine(key: string): void {
  refining.value = refining.value === key ? '' : key
  refineText.value = ''
}
function refine(key: string): void {
  if (!refineText.value.trim()) return
  sendToHost({ type: 'build:refineArt', key, text: refineText.value.trim() })
  refining.value = ''
  refineText.value = ''
}
function urlOf(path: string): string {
  return `${PROJECT_HOST}${path.split('/').map(encodeURIComponent).join('/')}`
}
function start(): void {
  // The words and the choices together; either may be empty, not both.
  if (query.value.trim() || hasChoices.value) sendToHost({ type: 'build:start', query: query.value.trim(), choices: choices() })
}
function browse(): void {
  browsing.value = true
  sendToHost({ type: 'build:list' })
}
function resume(id: string): void {
  browsing.value = false
  if (id !== state.value?.id || ['cancelled', 'failed'].includes(stage.value)) sendToHost({ type: 'build:resume', id })
}
const STAGE_NAMES: Record<string, string> = {
  interpreting: 'reading the request',
  request: '① Request',
  assets: '② Assets',
  planning: 'planning',
  layout: '① Confirm · ② Layout',
  plan: '③ Plan',
  planned: 'laid out',
  sourcing: '④ Assets: finding the art',
  sourced: '④ Assets: the art is ready',
  building: 'building',
  built: '⑤ Result',
  cancelled: 'stopped',
  failed: 'stopped',
}
function when(seconds: number): string {
  const d = new Date(seconds * 1000)
  const today = new Date()
  return d.toDateString() === today.toDateString()
    ? d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })
    : d.toLocaleDateString([], { day: 'numeric', month: 'short', year: d.getFullYear() === today.getFullYear() ? undefined : 'numeric' })
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
    <AiProgress :paths="['/places/', '/assets/']" />
    <div v-if="zoomed" class="lightbox" @click="zoomed = null">
      <img :src="urlOf(zoomed.path)" alt="" />
      <div class="caption">{{ zoomed.caption }} <span class="dim">{{ zoomed.path }}</span></div>
    </div>
    <!-- Working off the UI thread (7.10): the editor stays usable; the build's own buttons wait. -->
    <div v-if="working" class="working-overlay">
      <div class="working-box">
        <span class="spinner" />
        <div>
          <strong>{{ state?.status || 'Working…' }}</strong>
          <div class="dim">{{ workingSeconds }}s<template v-if="workingCall"> · {{ callName(workingCall) }}: {{ workingCall.phase
            }}{{ workingCall.outputTokens ? ', ' + workingCall.outputTokens.toLocaleString() + ' tokens' : '' }}</template></div>
          <div class="dim small">The rest of the editor stays usable meanwhile.</div>
        </div>
      </div>
    </div>
    <div class="header" @contextmenu.prevent="headerMenu($event)">
      <div class="steps">
        <button
          v-for="s in STEPS"
          :key="s.id"
          :class="{ active: activeStep === s.id }"
          :disabled="stepDisabled(s.id)"
          :title="usageTitle(s.id)"
          @click="step(s.id)"
        >
          {{ s.label }}
          <span v-if="stepUsage(s.id)" class="step-usage">{{ stepUsage(s.id) }}</span>
        </button>
      </div>
      <span v-if="state && !browsing" class="title">{{ state.name || state.query }}</span>
      <button v-if="state && !browsing && !['cancelled', 'failed'].includes(stage)" class="browse" title="Start another build or reopen an earlier one" @click="browse">Builds</button>
      <button v-if="browsing" class="browse" @click="browsing = false">Back to {{ state?.name || 'the build' }}</button>
      <button v-if="state && !browsing && !['cancelled', 'failed', 'interpreting'].includes(stage)" class="browse" :class="{ active: advanced }"
              title="The build's own documents: its PLL, the solved layout, the Spec, the Brief, the asset plan, the log"
              @click="(advanced = !advanced), advanced && showAdvanced(advancedWhat)">Advanced</button>
    </div>

    <section v-if="state?.notices?.length && !browsing" class="note notices">
      <div v-for="n in state.notices" :key="n">{{ n }}</div>
    </section>

    <!-- No build yet, or the last one is over: ask for one. -->
    <!-- The drawing tool: pick a zone on the right, drag where it goes on the left. -->
    <div v-if="drawing && state?.layout" class="drawing" @click.self="drawing = false">
      <div class="drawing-box">
        <div class="canvas-wrap">
          <canvas
            ref="drawCanvas"
            :width="DRAW_SIZE"
            :height="DRAW_SIZE"
            @mousedown="drawStart"
            @mousemove="drawMove"
            @mouseup="drawEnd"
            @mouseleave="drawEnd"
          />
          <p class="dim small">
            {{ drawZone ? (erasing ? 'Paint to take land away from the ' : 'Paint where the ') + (zoneName(drawZone) ?? drawZone) + (erasing ? '.' : ' goes.') : 'Pick a zone on the right.' }}
          </p>
        </div>
        <div class="draw-list">
          <h4>Zones</h4>
          <button
            v-for="(z, i) in state.layout.zones.filter((x) => x.level === 'zone')"
            :key="z.id"
            :class="['draw-zone', { active: drawZone === z.id }]"
            @click="drawZone = z.id"
          >
            <span class="swatch" :style="{ background: ZONE_COLOURS[state.layout.zones.indexOf(z) % ZONE_COLOURS.length] }" />
            {{ z.name }}
            <span v-if="z.space && !z.space.ok" class="warning small"> ⚠ needs ~{{ z.space.needed }}, has {{ z.space.cells }}</span>
            <span v-if="z.drawn" class="dim small"> · drawn</span>
          </button>
          <div class="brush">
            <label>Brush {{ brushSize }} cell{{ brushSize > 1 ? 's' : '' }}</label>
            <input v-model.number="brushSize" type="range" min="1" max="16" />
            <label class="row"><input v-model="erasing" type="checkbox" /> Erase</label>
          </div>
          <div v-if="drawWarnings.length" class="warning small">
            <strong>Short of room:</strong>
            <div v-for="w in drawWarnings" :key="w">{{ w }}</div>
          </div>
          <div class="actions">
            <button v-if="drawZone && (state.layout.zones.find((x) => x.id === drawZone)?.drawn || state.layout.zones.find((x) => x.id === drawZone)?.painted)" @click="sendToHost({ type: 'build:drawZone', zone: drawZone, rect: null })">
              Let it be placed
            </button>
            <button :disabled="!canUndo" :title="undoText" @click="sendToHost({ type: 'build:undo' })">↶ Undo</button>
            <button :disabled="!canRedo" :title="redoText" @click="sendToHost({ type: 'build:redo' })">↷ Redo</button>
            <button class="primary" @click="drawing = false">Done</button>
          </div>
        </div>
      </div>
    </div>

    <div v-if="pickFor" class="lightbox ask" @click.self="pickFor = ''">
      <div class="ask-box picker">
        <strong>Pick the art for {{ (state?.assetNeeds ?? []).find((n) => n.key === pickFor)?.title || pickFor }}</strong>
        <div class="row">
          <button :class="{ primary: pickTab === 'project' }" @click="pickTab = 'project'">The project</button>
          <button :class="{ primary: pickTab === 'library' }" @click="pickTab = 'library'">My library</button>
          <input v-model="pickFilter" placeholder="Search: a name, what it's for (wall torch), a tag" />
        </div>
        <div class="pick-grid">
          <button v-for="c in pickChoices" :key="c.path" class="pick" :title="c.path" @click="pick(c)">
            <img :src="pickTab === 'project' ? urlOf(c.path) : 'https://aige-library.local/' + c.path" alt="" loading="lazy" />
            <span>{{ ('name' in c && c.name) || c.path.split('/').pop() }}</span>
            <span v-if="rolesOf(c)" class="dim roles">{{ rolesOf(c) }}</span>
          </button>
          <p v-if="!pickChoices.length" class="dim">Nothing matches. Search the web (Assets panel ▸ Web Search) or generate it (Generate ▸ Art).</p>
        </div>
        <div class="actions"><button @click="pickFor = ''">Cancel</button></div>
      </div>
    </div>
    <section v-if="advanced && state && !browsing" class="advanced">
      <div class="confirm-tabs">
        <button v-for="a in ADVANCED" :key="a.id" :class="{ active: advancedWhat === a.id }" @click="showAdvanced(a.id)">{{ a.label }}</button>
        <span class="dim">{{ advancedDoc?.file ?? '' }}{{ advancedDoc?.cut ? ' (cut: too long to show whole)' : '' }}</span>
        <button class="small" @click="showAdvanced(advancedWhat)">Reload</button>
        <button class="small" @click="advanced = false">Close</button>
      </div>
      <pre>{{ advancedDoc ? advancedDoc.text : 'Loading…' }}</pre>
    </section>
    <section v-if="stage === 'revising' && !browsing" class="note busy-note">
      <span>{{ state?.status }}</span>
      <button @click="sendToHost({ type: 'build:cancel' })">Stop</button>
    </section>
    <section v-if="confirmLocked && !browsing" class="note view-note">
      <span>The layout and contents the plan was made from. Changing anything here discards the plan, and the zones are planned again.</span>
      <button class="primary" @click="backToPlan()">← Back to the plan</button>
    </section>
    <div v-if="discardAsk" class="lightbox ask" @click.self="discardAsk = false">
      <div class="ask-box">
        <strong>Discard the plan?</strong>
        <p>Changing the layout or contents discards the plan made from them{{ stage === 'built' ? ' (and the build)' : '' }}. You'll need to plan the zones again.</p>
        <div class="actions">
          <button @click="discardAsk = false">Keep the plan</button>
          <button class="primary" @click="discardPlan()">Discard it and make changes</button>
        </div>
      </div>
    </div>
    <div v-if="browsing || !state || stage === 'cancelled' || stage === 'failed'" class="body start">
      <p v-if="stage === 'failed'" class="error">{{ state?.errors.join(' · ') }}</p>
      <p v-else-if="stage === 'cancelled'" class="dim">The build was cancelled.</p>
      <p class="dim">Describe a place to build. You'll confirm the details before anything is made.</p>
      <div class="ask">
        <input v-model="query" placeholder="e.g. a large village by a river, with gardens and a market" @keydown.enter="start" />
        <button class="primary" :disabled="!query.trim() && !hasChoices" @click="start">Start</button>
      </div>
      <section class="choices">
        <button class="toggle" @click="choicesOpen = !choicesOpen">
          {{ choicesOpen ? '▾' : '▸' }} Choices
          <span class="dim">{{ hasChoices ? `${Object.keys(choices()).length} set: kept as you set them` : 'optional: set what matters, the words decide the rest' }}</span>
        </button>
        <div v-if="choicesOpen" class="grid">
          <label>Build</label>
          <select :value="chosenType" @change="setType(($event.target as HTMLSelectElement).value)">
            <option value="">from the words</option>
            <option v-for="t in choiceTypes" :key="t.id" :value="t.id">{{ t.name }}</option>
          </select>
          <template v-if="choiceType">
            <label>Size</label>
            <div>
              <select :value="(chosen['scale.class'] as string) ?? ''" @change="setChoice('scale.class', ($event.target as HTMLSelectElement).value)">
                <option value="">from the words</option>
                <option v-for="c in choiceType.classes" :key="c.id" :value="c.id">{{ c.label }} ({{ c.count[0] }}–{{ c.count[1] }} {{ choiceType.unit }})</option>
              </select>
              <span class="dim"> or </span>
              <input v-model="countLow" class="num" type="number" min="1" placeholder="from" />
              –
              <input v-model="countHigh" class="num" type="number" min="1" placeholder="to" />
              <span class="dim"> {{ choiceType.unit }}</span>
            </div>
            <template v-for="f in choiceType.fields" :key="f.path">
              <label :title="f.hint">{{ f.label }}</label>
              <div>
                <select v-if="f.values" :value="(chosen[f.path] as string) ?? ''" @change="setChoice(f.path, ($event.target as HTMLSelectElement).value)">
                  <option value="">from the words</option>
                  <option v-for="v in f.values" :key="v" :value="v">{{ v.replace(/-/g, ' ') }}</option>
                </select>
                <input v-else-if="f.type === 'count'" class="num" type="number" :min="f.min" :max="f.max" placeholder="from the words"
                       :value="(chosen[f.path] as number) ?? ''"
                       @change="setChoice(f.path, ($event.target as HTMLInputElement).value === '' ? '' : Number(($event.target as HTMLInputElement).value))" />
                <select v-else-if="f.type === 'bool'" :value="chosen[f.path] === undefined ? '' : String(chosen[f.path])"
                        @change="setChoice(f.path, ($event.target as HTMLSelectElement).value === '' ? '' : ($event.target as HTMLSelectElement).value === 'true')">
                  <option value="">from the words</option>
                  <option value="true">yes</option>
                  <option value="false">no</option>
                </select>
              </div>
            </template>
            <template v-if="choiceType.features.length">
              <label>Features</label>
              <div class="chips">
                <button v-for="f in choiceType.features" :key="f.id" :class="{ active: featuresChosen.includes(f.id) }" @click="toggleChosenFeature(f.id)">{{ f.label }}</button>
              </div>
            </template>
            <template v-if="choiceType.parts.length">
              <label>Parts</label>
              <div>
                <div class="chips">
                  <button
                    v-for="part in choiceType.parts"
                    :key="part.id"
                    :class="{ active: partsChosen[part.id] === true, out: partsChosen[part.id] === false }"
                    :title="part.description"
                    @click="cyclePart(part.id)"
                  >
                    {{ partsChosen[part.id] === true ? '✓ ' : partsChosen[part.id] === false ? '✗ ' : '' }}{{ part.label }}
                  </button>
                </div>
                <p class="dim">Click a part once to have it, again to leave it out, again to let the words decide.</p>
              </div>
            </template>
          </template>
        </div>
      </section>
      <section v-if="builds.length" class="builds">
        <h4>
          Your builds
          <button v-if="builds.length > 1" class="small" title="Deletes every build but the open one (asks first)" @click="removeAsk = true">Remove older builds</button>
        </h4>
        <div v-if="removeAsk" class="lightbox ask" @click.self="removeAsk = false">
          <div class="ask-box">
            <strong>Remove older builds?</strong>
            <p>
              Deletes {{ builds.filter((b) => b.id !== state?.id).length }} build(s) and their files (their plans, maps and history) from the
              project's places folder. {{ state?.id ? 'The open build is kept. ' : '' }}What they put in a scene stays there. This can't be undone.
            </p>
            <div class="actions">
              <button @click="removeAsk = false">Keep them</button>
              <button class="primary" @click="(removeAsk = false), sendToHost({ type: 'build:removeOlder' })">Remove them</button>
            </div>
          </div>
        </div>
        <div v-if="removeOne" class="lightbox ask" @click.self="removeOne = null">
          <div class="ask-box">
            <strong>Remove “{{ removeOne.name || removeOne.query }}”?</strong>
            <p>
              Deletes the build and its files (its plan, map and history) from the project's places folder.
              {{ removeOne.id === state?.id ? 'It is open: it will be closed. ' : '' }}What it put in a scene stays there. This can't be undone.
            </p>
            <div class="actions">
              <button @click="removeOne = null">Keep it</button>
              <button class="primary" @click="sendToHost({ type: 'build:remove', id: removeOne.id }), (removeOne = null)">Remove it</button>
            </div>
          </div>
        </div>
        <div v-for="b in builds" :key="b.id" class="build-row-wrap">
          <button class="build-row" :class="{ current: b.id === state?.id && !['cancelled', 'failed'].includes(stage) }" :title="b.query" @click="resume(b.id)">
            <span class="name">{{ b.name || b.query }}</span>
            <span class="dim">{{ STAGE_NAMES[b.stage] ?? b.stage }}</span>
            <span class="dim when">{{ when(b.updated) }}</span>
          </button>
          <button class="remove" title="Remove this build (asks first)" @click="removeOne = b">✕</button>
        </div>
      </section>
    </div>

    <div v-else-if="(stage === 'interpreting' || (stage === 'planning' && state?.planPhase === 'root')) && state?.confirmProgress" class="body start progress-view">
      <p class="query">“{{ state.query }}”</p>
      <div class="bar"><i :style="{ width: `${Math.round(100 * state.confirmProgress.fraction)}%` }" /></div>
      <ol class="progress-steps">
        <li v-for="s in state.confirmProgress.steps" :key="s.id" :class="s.state">
          <span class="mark">{{ s.state === 'done' ? '✓' : s.state === 'active' ? '●' : '○' }}</span>
          <span class="label">{{ s.label }}</span>
          <span v-if="s.state === 'active'" class="dim"> · {{ elapsed }}s</span>
          <ul v-if="s.detail.length && s.state !== 'waiting'" class="detail">
            <li v-for="(d, i) in s.detail" :key="i">{{ d }}</li>
          </ul>
        </li>
      </ol>
      <p class="dim">Then ① Confirm opens: what the place holds, and its layout to adjust.</p>
      <button @click="sendToHost({ type: 'build:cancel' })">Stop</button>
    </div>
    <div v-else-if="stage === 'interpreting'" class="body start">
      <p>{{ state?.status || 'Reading your request…' }}</p>
      <button @click="sendToHost({ type: 'build:cancel' })">Stop</button>
    </div>

    <div v-else-if="stage === 'planning'" class="body start">
      <p>{{ state?.status }}</p>
      <div v-if="state?.progress?.zones" class="bar"><i :style="{ width: `${(100 * state.progress.done) / state.progress.zones}%` }" /></div>
      <p class="dim">The zones are planned first, then each zone's parcels; gardens, yards and the like follow from the module rules.</p>
      <button @click="sendToHost({ type: 'build:cancel' })">Stop</button>
    </div>

    <!-- ① Confirm: the layout first (the zones and where they go), and what the place holds. -->
    <div
      v-else-if="atConfirm && confirmTab === 'layout' && state?.layout"
      class="body layout-view"
      :class="{ locked: confirmLocked }"
      @click.capture="guardLocked"
      @mousedown.capture="guardLocked"
      @keydown.capture="guardLocked"
    >
      <section v-if="state.layoutForm?.length" class="layout-form">
        <h4>Layout form</h4>
        <p class="dim small">How the place is laid out: the shape of its zones, its streets, its blocks and lots. A change lays it out again.</p>
        <div class="form-grid">
          <template v-for="f in state.layoutForm" :key="f.field">
            <label :title="f.description">{{ f.field.replace(/-/g, ' ') }}</label>
            <div>
              <select v-if="f.values" :value="f.value" @change="sendToHost({ type: 'build:rootField', field: f.field, value: ($event.target as HTMLSelectElement).value })">
                <option v-for="v in f.values" :key="v" :value="v">{{ v }}{{ v === f.default ? ' (default)' : '' }}</option>
              </select>
              <span v-else-if="f.type === 'size'" class="size-pair">
                <input type="number" min="8" max="60" :value="Array.isArray(f.value?.w) ? f.value.w[0] : f.value?.w"
                       @change="sendToHost({ type: 'build:rootField', field: f.field, value: { w: Number(($event.target as HTMLInputElement).value), h: Array.isArray(f.value?.h) ? f.value.h[0] : f.value?.h } })" />
                ×
                <input type="number" min="8" max="60" :value="Array.isArray(f.value?.h) ? f.value.h[0] : f.value?.h"
                       @change="sendToHost({ type: 'build:rootField', field: f.field, value: { w: Array.isArray(f.value?.w) ? f.value.w[0] : f.value?.w, h: Number(($event.target as HTMLInputElement).value) } })" />
                cells
              </span>
              <div v-if="f.meaning?.[f.value]" class="dim small">{{ f.meaning[f.value] }}</div>
            </div>
          </template>
        </div>
      </section>
      <section v-if="state.layout.stale" class="note warning">
        The Contents changed since these zones were planned.
        <button class="primary" @click="sendToHost({ type: 'build:replanLayout' })">Lay out the zones again</button>
      </section>
      <div class="layout-top">
        <div class="map-box">
          <img v-if="state.layout.image || state.image" :src="PROJECT_HOST + (state.layout.image ?? state.image)" alt="The zones" class="zones-map" title="Click to paint the zones" @click="openDrawing()" />
          <div class="map-actions">
            <button class="small" @click="openDrawing()">Paint the zones…</button>
            <button class="small" :disabled="!canUndo || stage === 'revising'" :title="undoText" @click="sendToHost({ type: 'build:undo' })">↶ Undo</button>
            <button class="small" :disabled="!canRedo || stage === 'revising'" :title="redoText" @click="sendToHost({ type: 'build:redo' })">↷ Redo</button>
          </div>
        </div>
        <table class="zones">
          <thead><tr><th>Zone</th><th>What it is</th><th>Where it goes</th><th>Size</th></tr></thead>
          <tbody>
            <tr v-for="(z, i) in state.layout.zones" :key="z.id">
              <td>
                <span class="swatch" :style="{ background: ZONE_COLOURS[i % ZONE_COLOURS.length] }" />
                <strong>{{ z.name }}</strong><br />
                <span class="dim">{{ z.id }}</span>
                <div v-if="z.space && !z.space.ok" class="warning small">⚠ painted smaller than its {{ show(z.count) }} {{ state.layout.unit }} need (about {{ z.space.needed }} cells)</div>
                <div v-if="z.drawn || z.painted" class="dim small">painted <button class="link" @click="sendToHost({ type: 'build:drawZone', zone: z.id, rect: null })">let it be placed</button></div>
              </td>
              <td class="small">{{ z.description }}<span v-if="z.rationale" class="dim"><br />{{ z.rationale }}</span></td>
              <td class="small">
                <span v-if="z.placement">{{ z.placement }}</span>
                <span v-if="z.rel?.length" class="dim"><br />{{ z.rel.map((r) => r.type + ' ' + r.to).join(' · ') }}</span>
                <span v-if="z.where" :class="z.placement ? 'dim' : ''"><br />Laid out: {{ z.where }}</span>
              </td>
              <td class="small">
                <template v-if="z.level === 'zone'">
                  <input class="num" type="number" min="1" :value="areaOf(z.area)" title="Its land, in cells²"
                         @change="sendToHost({ type: 'build:zoneField', zone: z.id, field: 'area', value: Number(($event.target as HTMLInputElement).value) })" />
                  <span class="dim">cells²</span>
                  <template v-if="z.countField">
                    <br />
                    <input class="num" type="number" min="0" :value="countOf(z.count)" :title="'Its ' + state.layout.unit"
                           @change="sendToHost({ type: 'build:zoneField', zone: z.id, field: z.countField, value: Number(($event.target as HTMLInputElement).value) })" />
                    <span class="dim">{{ state.layout.unit }}</span>
                  </template>
                  <div v-if="z.kinds?.length" class="kinds">
                    <label v-for="k in z.kinds" :key="k.module" :title="'How many ' + k.name + ' (empty: the plan decides)'">
                      <input class="num small" type="number" min="0" :value="k.count ?? ''" :placeholder="k.estimate !== undefined ? '~' + k.estimate : 'any'"
                             @change="setKind(z.module, k.module, ($event.target as HTMLInputElement).value)" />
                      <span class="dim">{{ k.name }}</span>
                    </label>
                  </div>
                </template>
              </td>
            </tr>
          </tbody>
        </table>
      </div>
      <section v-if="win" class="layouts-section">
        <h4>Layout</h4>
        <div class="layout-row">
          <span class="layout-name">The place</span>
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
        </div>
        <input
          class="wide"
          :value="win.layout.description ?? ''"
          placeholder="Where each pattern goes, if it matters: e.g. a grid core inside the walls, organic lanes outside"
          @change="edit('shape.description', ($event.target as HTMLInputElement).value)"
        />
        <div v-for="z in state.layout.zones.filter((x) => x.hasStyle)" :key="z.id" class="layout-row">
          <span class="layout-name">{{ z.name }}</span>
          <div class="layouts small-tiles">
            <div class="layout" :class="{ active: !z.style }">
              <button title="The place's layout" @click="sendToHost({ type: 'build:zoneField', zone: z.id, field: 'street-style', value: '' })">
                <svg viewBox="0 0 40 28" width="30" height="21" v-html="layoutSketch(mainPattern)" />
                <span>as the place</span>
              </button>
            </div>
            <div v-for="l in win.layout.options" :key="l" class="layout" :class="{ active: z.style === l }">
              <button @click="sendToHost({ type: 'build:zoneField', zone: z.id, field: 'street-style', value: l })">
                <svg viewBox="0 0 40 28" width="30" height="21" v-html="layoutSketch(l)" />
                <span>{{ l.replace(/-/g, ' ') }}</span>
              </button>
            </div>
          </div>
        </div>
      </section>
      <section v-if="state.directions" class="note small"><strong>Your answers:</strong> {{ state.directions }}</section>
      <section class="revise">
        <h4>
          Revise the layout
          <button v-if="state.revisions?.length" class="small link" @click="historyOpen = !historyOpen">
            {{ historyOpen ? 'hide' : 'show' }} the history ({{ state.revisions.length }})
          </button>
        </h4>
        <div v-for="(r, i) in historyOpen ? state.revisions ?? [] : []" :key="i" :class="['revision', { undone: r.undone, edit: r.by === 'edit' }]">
          <div v-if="r.by === 'you'"><span class="who">You:</span> {{ r.text }} <span v-if="r.time" class="dim when">{{ when(r.time) }}</span></div>
          <div v-if="r.by === 'edit'"><span class="who">You did:</span> {{ r.text }} <span v-if="r.time" class="dim when">{{ when(r.time) }}</span><span v-if="r.undone" class="dim"> (undone)</span></div>
          <div v-if="r.by !== 'edit'"><span class="who">AI:</span> {{ r.summary }}<span v-if="r.undone" class="dim"> (undone)</span></div>
          <div v-for="(c, k) in r.checks ?? []" :key="'c' + k" :class="['small', c.ok ? 'ok' : 'error']">
            {{ c.ok ? '✓' : '✗' }} {{ c.node }} {{ c.type }} {{ c.to }} <span class="dim">· {{ c.detail }}</span>
          </div>
          <div v-for="n in r.notes ?? []" :key="n" class="dim small">{{ n }}</div>
        </div>
        <p v-if="stage === 'revising'">{{ state.status }} <button @click="sendToHost({ type: 'build:cancel' })">Stop</button></p>
        <template v-else>
          <textarea
            v-model="revision"
            rows="2"
            placeholder="e.g. move the market to the crossroads; the residential ward along the river, south of the docks"
            @keydown.enter.exact.prevent="revise()"
          />
          <div class="actions left">
            <button class="primary" :disabled="!revision.trim()" @click="revise()">Revise</button>
            <button :disabled="!canUndo" :title="undoText" @click="sendToHost({ type: 'build:undo' })">↶ Undo</button>
            <button :disabled="!canRedo" :title="redoText" @click="sendToHost({ type: 'build:redo' })">↷ Redo</button>
          </div>
        </template>
      </section>
      <div class="actions">
        <button v-if="confirmLocked" class="primary" data-view-ok @click="backToPlan()">Back to the plan →</button>
        <template v-else>
        <button @click="sendToHost({ type: 'build:cancel' })">Cancel</button>
        <button
          class="primary"
          :disabled="stage === 'revising' || state.layout.stale || errors.length > 0"
          :title="
            state.layout.stale
              ? 'Lay the zones out again first'
              : errors.length
                ? 'Fix this in ① Confirm first: ' + (errors[0]?.where ?? '') + ' ' + (errors[0]?.message ?? '')
                : 'Plans what is inside each zone, the zones kept where they are'
          "
          @click="sendToHost({ type: 'build:planFromLayout' })"
        >
          Plan the zones →
        </button>
        </template>
      </div>
    </div>

    <!-- ⑤ Result, while it's built: each step of putting the place into the scene. -->
    <div v-else-if="stage === 'building' && viewing !== 'assets' && viewing !== 'plan'" class="body window">
      <section class="build-progress">
        <h4>Building {{ state?.name || 'the place' }}: step {{ Math.min(state?.buildProgress?.total ?? 5, (state?.buildProgress?.done ?? 0) + 1) }} of {{ state?.buildProgress?.total ?? 5 }}</h4>
        <div class="bar"><i :style="{ width: `${(100 * (state?.buildProgress?.done ?? 0)) / Math.max(1, state?.buildProgress?.total ?? 5)}%` }" /></div>
        <p>{{ state?.buildProgress?.text || state?.status }}</p>
        <p class="dim">The art for each part, then the ground and everything on it put into the scene, checked, repaired where it can be, and saved as one undo step.</p>
      </section>
    </div>

    <!-- ③ Assets (after the plan): where the art comes from, then the build. -->
    <div v-else-if="showAssets && (win || state?.sourcing || state?.sourced)" class="body window assets-view">
      <div class="confirm-tabs">
        <button :class="{ active: assetsTab === 'how' }" @click="assetsTab = 'how'">How each is found</button>
        <button :class="{ active: assetsTab === 'background' }" :disabled="!state?.background && !state?.backgroundMaking" @click="assetsTab = 'background'">Background</button>
        <button :class="{ active: assetsTab === 'found' }" :disabled="!artStarted" @click="assetsTab = 'found'">The assets</button>
        <span class="dim">{{ assetsTab === 'how' ? 'the project, the library, the web or the Art tool, for each part'
          : assetsTab === 'background' ? 'the whole ground in one picture, painted first' : 'what was found and made, each refinable' }}</span>
      </div>
      <template v-if="assetsTab === 'how' && win">
      <section class="note">
        <label class="row">
          <input type="checkbox" :checked="sourcingSettings.webSearch" @change="setSourcing({ webSearch: ($event.target as HTMLInputElement).checked })" />
          Search the web for free art (OpenGameArt; only CC0, CC-BY and OGA-BY, credited in CREDITS.md)
        </label>
        <label class="row">
          <input type="checkbox" :checked="sourcingSettings.shareAlike" @change="setSourcing({ shareAlike: ($event.target as HTMLInputElement).checked })" />
          Allow share-alike art (CC-BY-SA: the game's art must then be shared alike)
        </label>
        </section>
        <section v-if="state?.assetNeeds?.length" class="plan">
        <h4>
          Each part's art:
          <span class="dim">{{ state.assetNeeds.filter((n) => n.decision?.action === 'generate' && n.source === 'placeholder').length }} to make,
            {{ state.assetNeeds.filter((n) => n.decision?.action === 'web' && n.source === 'placeholder').length }} to search for,
            {{ state.assetNeeds.filter((n) => n.source !== 'placeholder').length }} found</span>
        </h4>
        <div v-if="state.fromPlanner" class="row set-all">
          <span class="dim">Art strategy: set in the AI Planner's Game Brief (① Concept ▸ Plan ▸ Art for the places)</span>
        </div>
        <div v-else class="row set-all">
          <span class="dim" title="How a piece set to auto is found: Search looks in the project, your library and the web first (found art is the most reliable), and makes it only when nothing found fits">Default for this build</span>
          <select :value="sourcingSettings.defaultSource" @change="setSourcing({ defaultSource: ($event.target as HTMLSelectElement).value })">
            <option value="search">Search (project, library, web, then make)</option>
            <option value="generate">Generate (the Art tool)</option>
            <option value="library">Library (else made by the Art tool)</option>
            <option value="project">The project (else made by the Art tool)</option>
            <option value="auto">Auto (by each piece's importance)</option>
          </select>
        </div>
        <div class="row set-all">
          <span class="dim">Every piece from</span>
          <select value="" @change="setAllSources(($event.target as HTMLSelectElement).value); ($event.target as HTMLSelectElement).value = ''">
            <option value="">choose…</option>
            <option v-for="s in NEED_SOURCES" :key="s.id" :value="s.id">{{ s.label }}</option>
          </select>
          <span class="dim">(each piece can still be set on its own below)</span>
        </div>
        <table class="components needs">
          <thead><tr><th></th><th>Needed</th><th>For</th><th>Gets</th><th>From</th><th>Why</th><th></th></tr></thead>
          <tbody>
            <tr class="background-row" title="The place's ground in one picture, matched to the grid: each cell shows its own square of it">
              <td class="thumb">
                <img v-if="state.background" :src="urlOf(state.background.path)" alt="" class="zoomable" @click="zoom(state.background.path, 'Background')" />
              </td>
              <td>Background</td>
              <td class="dim">the whole ground</td>
              <td>{{ state.background ? state.background.path.split('/').pop() : '—' }}</td>
              <td>{{ state.backgroundMaking ? 'painting…' : state.background ? (state.background.picked ? 'imported' : state.background.provider || 'painted') : 'not yet' }}</td>
              <td class="dim">roads, paths, floors and grass painted as one picture, from the description below</td>
              <td></td>
            </tr>
            <tr class="background-row">
              <td></td>
              <td colspan="6">
                <textarea
                  v-model="backgroundText"
                  class="background-description"
                  rows="4"
                  spellcheck="false"
                  title="What the background is painted from: the place and its look, then a line per kind of ground ('asphalt: road with white lane markings')"
                  @blur="edit('background.description', backgroundText)"
                />
              </td>
            </tr>
            <tr v-for="n in state.assetNeeds" :key="n.key" :title="n.description">
              <td class="thumb">
                <img v-if="n.entry && n.source !== 'placeholder' && /\.(png|jpg|jpeg)$/i.test(n.entry)" :src="urlOf(n.entry)" alt="" />
              </td>
              <td>
                {{ n.title || (n.concepts?.length ? n.concepts.join(', ') + ' ' : '') + n.role }}
                <span v-if="(n.uses ?? 1) > 1" class="dim"> ×{{ n.uses }}</span>
                <div v-if="n.description" class="dim small desc">{{ n.description }}</div>
              </td>
              <td class="dim">{{ n.componentName || n.component }}</td>
              <td :class="{ warning: n.source === 'placeholder' && !['generate', 'web'].includes(n.decision?.action ?? '') }">{{ willGet(n) }}
                <span v-if="n.name" class="dim">· {{ n.name }}</span></td>
              <td>
                <select v-if="!n.key.startsWith('terrain|')" :value="n.override || 'auto'" @change="setNeedSource(n.key, ($event.target as HTMLSelectElement).value)">
                  <option v-for="s in NEED_SOURCES" :key="s.id" :value="s.id">{{ s.id === 'auto' && n.policy && n.policy !== 'auto' ? 'as its part (' + n.policy + ')' : s.label }}</option>
                </select>
              </td>
              <td class="dim">{{ n.decision?.reason || n.why }}</td>
              <td><button v-if="!n.key.startsWith('terrain|')" class="small" title="Choose the art yourself: from the project or your library" @click="openPicker(n.key)">Pick…</button></td>
            </tr>
          </tbody>
        </table>
      </section>
      <section v-else-if="win.assetPlan" class="plan">
        <h4>Assets: {{ win.assetPlan.summary.project }} from the project, {{ win.assetPlan.summary.generate }} to generate</h4>
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
        <div class="actions">
          <button @click="viewing = 'plan'">← Plan</button>
          <span class="dim">{{ state?.status }}</span>
          <span v-if="state?.matched" class="dim" :title="(state.matched.report?.skipped ?? []).map((s) => `${s.key}: ${s.why}`).join('; ')">
            {{ state.matched.state === 'making' ? 'Making the art…'
              : state.matched.state === 'made' ? 'Cutting the pieces…'
              : state.matched.state === 'failed' ? `The art failed: ${state.matched.error ?? ''}`
              : `Matched art: ${state.matched.report?.pieces ?? 0} pieces, ${state.matched.report?.grounds ?? 0} grounds` +
                (state.matched.report?.flagged ? `, ${state.matched.report.flagged} off the style` : '') }}
          </span>
          <button v-if="SCENE_BUILD" class="primary"
                  :disabled="stage === 'sourcing' || stage === 'building' || state?.backgroundMaking || state?.matched?.state === 'making'"
                  :title="(state?.background ? '' : 'The background first (the whole ground, painted), then: ') + (makesArt
                    ? 'every part\'s art made by the art provider (Preferences ▸ AI ▸ Art) in one style: a style sheet first, then every piece in its style, drawn to the grid'
                    : 'each part\'s art found in the project or the library, and what\'s missing searched for or made')"
                  @click="startArt">
            {{ makesArt ? (assetsFound ? 'Make the art again' : 'Make the art →') : (assetsFound ? 'Find the assets again' : 'Find the assets →') }}
          </button>
        </div>
      </template>
      <!-- The background: painted first (the whole place, then each detail window), then on to the art. -->
      <template v-else-if="assetsTab === 'background'">
        <section v-if="state?.backgroundMaking" class="build-progress">
          <h4>
            Painting the background<span v-if="backgroundCall?.total" class="dim">: step {{ Math.min(backgroundCall.total, (backgroundCall.done ?? 0) + 1) }} of {{ backgroundCall.total }}</span>
          </h4>
          <div class="bar"><i :style="{ width: `${(100 * (backgroundCall?.done ?? 0)) / Math.max(1, backgroundCall?.total ?? 1)}%` }" /></div>
          <p>{{ backgroundCall?.step || 'Starting…' }}<span v-if="backgroundCall" class="dim"> · {{ Math.round(backgroundCall.seconds) }}s</span></p>
          <p class="dim">
            The whole place is painted in one picture, then the parts that need more detail are painted again as windows and blended.
            {{ state.afterBackground === 'matched' ? 'Then every part\'s art is made in its style.'
              : state.afterBackground === 'find' ? 'Then each part\'s art is found and made.' : '' }}
          </p>
        </section>
        <section v-else-if="state?.background" class="background-view">
          <img :src="urlOf(state.background.path)" alt="The background" class="zoomable" title="Click to zoom" @click="zoom(state.background.path, 'Background')" />
          <p class="dim">
            {{ state.background.picked ? 'Imported' : 'Painted' }}{{ state.background.provider ? ' by ' + state.background.provider : '' }}{{
              state.background.calls ? ' in ' + state.background.calls + ' calls' : '' }} · {{ state.background.path.split('/').pop() }}
          </p>
        </section>
        <div class="actions">
          <button @click="assetsTab = 'how'">← How each is found</button>
          <span class="dim">{{ state?.status }}</span>
          <button :disabled="!artStarted" @click="assetsTab = 'found'">The assets →</button>
        </div>
      </template>
      <template v-else>
        <section v-if="state?.matched?.state === 'making' || state?.matched?.state === 'made'" class="build-progress">
          <h4>
            {{ state.matched.state === 'making' ? 'Making the art in one style' : 'Cutting the pieces' }}<span
              v-if="state.matched.state === 'making' && matchedCall?.total" class="dim">: {{ matchedCall.done ?? 0 }} of {{ matchedCall.total }} steps done</span>
          </h4>
          <template v-if="state.matched.state === 'making'">
            <div class="bar"><i :style="{ width: `${(100 * (matchedCall?.done ?? 0)) / Math.max(1, matchedCall?.total ?? 1)}%` }" /></div>
            <p>{{ matchedCall?.step || 'Starting…' }}<span v-if="matchedCall" class="dim"> · {{ Math.round(matchedCall.seconds) }}s</span></p>
            <ol v-if="matchedCall?.plan?.length" class="art-steps">
              <li v-for="s in matchedCall.plan" :key="s.id" :class="s.state">
                <span class="mark">{{ s.state === 'done' ? '✓' : s.state === 'failed' ? '✗' : s.state === 'working' ? '●' : '○' }}</span>
                <span class="label">{{ s.label }}</span>
                <div v-if="s.pieces.length" class="pieces">
                  <span v-for="p in s.pieces" :key="p.slug" :class="['piece', { made: p.made, missing: s.state === 'done' && !p.made }]">
                    <img v-if="p.path" :src="urlOf(p.path)" alt="" class="zoomable" @click="zoom(p.path, p.name)" />
                    {{ p.name }}
                  </span>
                </div>
              </li>
            </ol>
          </template>
          <p v-else class="dim">Each piece cut from its sheet and added to the project's assets.</p>
        </section>
        <section v-else-if="state?.afterBackground" class="build-progress">
          <h4>Waiting for the background</h4>
          <p class="dim">The art starts as soon as it's painted (see the Background tab).</p>
        </section>
        <!-- Being found and made: each piece as it comes. -->
        <section v-if="stage === 'sourcing' && state?.sourcing" class="sourcing">
          <h4>Finding and making the art: {{ state.progress?.done ?? 0 }} of {{ state.progress?.total ?? state.sourcing.length }} done</h4>
          <div class="bar"><i :style="{ width: `${(100 * (state.progress?.done ?? 0)) / Math.max(1, state.progress?.total ?? state.sourcing.length)}%` }" /></div>
          <p class="dim">
            Each piece the project has no art for is {{ state.sourcing.some((n) => n.action === 'web') ? 'searched for on the web or ' : '' }}made by
            the Art tool, then checked by the vision model (up to 3 tries). When they're all in, Build puts the place into the scene.
          </p>
        <div class="art-grid">
          <div v-for="n in state.sourcing" :key="n.key" :class="['art-need', { done: n.done, passed: n.passed, open: refining === n.key }]">
            <div class="thumbs">
              <template v-if="n.made.length">
                <figure v-for="m in n.made" :key="m.path" :class="{ bad: m.ok === false }" :title="m.seen ? 'Seen as: ' + m.seen : 'Being checked…'">
                  <img :src="urlOf(m.path)" alt="" class="zoomable" @click="zoom(m.path, n.role + (m.seen ? ' · seen as ' + m.seen : ''))" />
                  <figcaption>{{ m.ok === undefined ? 'checking' : m.ok ? '✓' : '✗ ' + (m.seen ?? '') }}</figcaption>
                </figure>
              </template>
              <span v-else class="waiting">{{ n.done ? 'none came' : n.action === 'web' ? 'searching…' : 'making…' }}</span>
            </div>
            <div class="what">
              <strong>{{ n.role }}</strong>
              <span v-if="n.alternatives" class="dim">or any of: {{ n.alternatives.replace(/,/g, ', ') }}</span>
              <span class="dim">{{ n.description }}</span>
              <span class="dim">try {{ Math.max(1, n.attempts) }}{{ n.done ? (n.passed ? ' · done' : ' · placeholder') : '' }}</span>
              <span v-if="n.request" class="dim">asked: {{ n.request }}</span>
              <div v-if="n.done" class="row">
                <button v-if="!n.passed && n.made.length" class="small" title="It's right: keep it (the check rejected it)" @click="keepArt(n)">Keep</button>
                <button class="small" title="Art from the project or your library instead" @click="openPicker(n.key)">Pick…</button>
                <button class="small" @click="openRefine(n.key)">Refine</button>
              </div>
            </div>
            <div v-if="refining === n.key" class="refine">
              <textarea v-model="refineText" rows="2" placeholder="What to change, e.g. a thatched roof, warmer colours, smaller" @keydown.enter.ctrl="refine(n.key)" />
              <div class="row">
                <button class="primary" :disabled="!refineText.trim()" @click="refine(n.key)">Make it again</button>
                <button @click="refining = ''">Cancel</button>
              </div>
            </div>
          </div>
        </div>
        </section>
        <!-- Found: what each part got, and the art made (refinable). -->
        <section v-if="stage !== 'sourcing' && state?.assetNeeds?.length" class="plan">
          <h4>
            The assets:
            <span v-for="(n, src) in neededBySource" :key="src" class="dim">{{ n }} {{ SOURCE_NAMES[src] ?? src }} · </span>
          </h4>
          <table class="components">
            <thead><tr><th></th><th>Needed</th><th>Gets</th><th>From</th></tr></thead>
            <tbody>
              <tr v-for="n in state.assetNeeds" :key="n.key">
                <td class="thumb art">
                  <img v-if="n.entry && n.source !== 'placeholder' && /\.(png|jpg|jpeg)$/i.test(n.entry)" :src="urlOf(n.entry)" alt="" class="zoomable"
                       @click="zoom(n.entry, n.title || n.role)" />
                  <img v-else-if="rejectedOf(n.key)" :src="urlOf(rejectedOf(n.key)!.path)" alt="" class="zoomable rejected-thumb"
                       title="Made, but the check rejected it: Keep it below, or find it another way" @click="zoom(rejectedOf(n.key)!.path, (n.title || n.role) + ' · rejected')" />
                  <span v-else class="placeholder-box" title="A placeholder: no art for it yet" />
                </td>
                <td>{{ n.concepts?.length ? n.concepts.join(', ') + ' ' : '' }}{{ n.role }}<span v-if="(n.uses ?? 1) > 1" class="dim"> ×{{ n.uses }}</span></td>
                <td>{{ n.name || (n.entry ? n.entry.split('/').pop() : '') }}<span v-if="n.why" class="dim"> {{ n.why }}</span></td>
                <td :class="{ warning: n.source === 'placeholder' }">{{ SOURCE_NAMES[n.source] ?? n.source }}</td>
              </tr>
            </tbody>
          </table>
        </section>
        <section v-if="stage !== 'sourcing' && unfound.length" class="note warning unfound">
          <strong>{{ unfound.length }} still missing</strong>
          <span class="dim"> · nothing found or made yet; another way for each:</span>
          <div v-for="n in unfound" :key="n.key" class="unfound-row">
            <img v-if="rejectedOf(n.key)" :src="urlOf(rejectedOf(n.key)!.path)" alt="" class="zoomable rejected-thumb"
                 :title="'Made, but the check rejected it' + (rejectedOf(n.key)!.seen ? ': seen as ' + rejectedOf(n.key)!.seen : '')"
                 @click="zoom(rejectedOf(n.key)!.path, (n.title || n.role) + ' · rejected')" />
            <span>{{ n.title || n.role }}</span>
            <span class="dim small">{{ n.decision?.reason || n.why }}</span>
            <span class="spacer"></span>
            <button v-if="rejectedOf(n.key)" class="small" title="It's right: keep the art the check rejected"
                    @click="keepArt({ key: n.key, made: [rejectedOf(n.key)!] })">Keep</button>
            <button class="small" title="Made by the Art tool" @click="retryNeed(n.key, 'generate')">Generate</button>
            <button class="small" title="Free art from approved sites" @click="retryNeed(n.key, 'web')">Search the web</button>
            <button class="small" title="From the project or your library" @click="openPicker(n.key)">Pick…</button>
            <button class="small" title="Keep the placeholder" @click="setNeedSource(n.key, 'placeholder')">Placeholder</button>
          </div>
        </section>
        <section v-if="stage !== 'sourcing' && state?.sourced?.length" class="sourcing">
          <h4>Art made for this build</h4>
          <div class="art-grid">
            <div v-for="n in state.sourced" :key="n.key" :class="['art-need', { done: true, passed: n.passed, open: refining === n.key }]">
              <div class="thumbs">
                <figure v-for="m in shownArt(n)" :key="m.path" :class="{ bad: m.ok === false }" :title="m.seen ? 'Seen as: ' + m.seen : ''">
                  <img :src="urlOf(m.path)" alt="" class="zoomable" @click="zoom(m.path, n.role + (m.seen ? ' · seen as ' + m.seen : ''))" />
                  <figcaption v-if="m.ok === false">✗ the check: {{ m.seen || 'not right' }}</figcaption>
                </figure>
                <span v-if="!n.made.length" class="waiting">placeholder</span>
              </div>
              <div class="what">
                <strong>{{ n.role }}</strong>
                <span v-if="n.alternatives" class="dim">or any of: {{ n.alternatives.replace(/,/g, ', ') }}</span>
                <span class="dim">{{ n.description }}</span>
                <span v-if="n.request" class="dim">asked: {{ n.request }}</span>
                <span v-if="n.verdict === 'kept'" class="verdict kept">✓ kept by you</span>
                <span v-else-if="n.verdict === 'rejected'" class="verdict rejected">✗ rejected: found another way</span>
                <div class="row">
                  <button class="small" title="It's right: keep it (even if the check rejected it)" :disabled="n.verdict === 'kept'" @click="keepArt(n)">Keep</button>
                  <button class="small" title="Art from the project or your library instead" @click="openPicker(n.key)">Pick…</button>
                  <button class="small" title="Describe what to change, and it's made again" @click="openRefine(n.key)">Refine</button>
                  <button class="small" title="It's wrong: not used, the need is found another way (your library, the web, a placeholder)" :disabled="n.verdict === 'rejected'" @click="sendToHost({ type: 'build:artVerdict', key: n.key, verdict: 'rejected' })">Reject</button>
                </div>
              </div>
              <div v-if="refining === n.key" class="refine">
                <textarea v-model="refineText" rows="2" placeholder="What to change, e.g. a thatched roof, warmer colours, smaller" @keydown.enter.ctrl="refine(n.key)" />
                <div class="row">
                  <button class="primary" :disabled="!refineText.trim()" @click="refine(n.key)">Make it again</button>
                  <button @click="refining = ''">Cancel</button>
                </div>
              </div>
            </div>
          </div>
        </section>
        <section v-if="stage === 'building'" class="build-progress">
          <h4>Building {{ state?.name || 'the place' }}</h4>
          <div class="bar"><i :style="{ width: `${(100 * (state?.buildProgress?.done ?? 0)) / Math.max(1, state?.buildProgress?.total ?? 5)}%` }" /></div>
          <p>{{ state?.buildProgress?.text || state?.status }}</p>
        </section>
        <div class="actions">
          <button @click="assetsTab = 'how'">← How each is found</button>
          <span class="dim">{{ state?.status }}</span>
          <button v-if="stage === 'sourcing'" title="Stop finding and making art: what's missing is a placeholder" @click="sendToHost({ type: 'build:skipSourcing' })">
            Stop: use placeholders
          </button>
          <button v-if="stage === 'sourced' || stage === 'built'" class="primary" title="Puts the place into the scene, verifies it and repairs what it can"
                  @click="sendToHost({ type: 'build:build' })">
            {{ stage === 'built' ? 'Build again' : 'Build →' }}
          </button>
        </div>
      </template>
    </div>

    <!-- ③ Plan: what will be built, before anything is. -->
    <div v-else-if="['plan', 'planned', 'sourcing', 'sourced', 'revising', 'building', 'built'].includes(stage) && state?.plan && !atConfirm" class="body plan-view">
      <!-- ⑤ Result: what the scene got, checked. -->
      <section v-if="showResult && state.result" :class="['result', { failing: sceneFailing.length }]">
        <p class="headline">
          {{ sceneFailing.length ? `Built, ${sceneFailing.length} check(s) to look at` : 'Built and verified' }}
          <span class="dim">
            · {{ builtCount }} things, {{ groundCells }} ground cells
            <template v-if="state.result.origin">at {{ state.result.origin[0] }}, {{ state.result.origin[1] }}</template>
          </span>
        </p>
        <img
          v-if="state.result.image"
          :src="PROJECT_HOST + state.result.image"
          :class="['map', { large: bigMap }]"
          title="The place in the scene, drawn as the game draws it. Click to enlarge."
          @click="bigMap = !bigMap"
        />
        <h4>In the scene</h4>
        <ul class="checks">
          <li v-for="c in state.result.after" :key="c.id" :class="c.ok ? 'ok' : c.warning ? 'warning' : 'error'">
            {{ c.ok ? '✓' : c.warning ? '!' : '✗' }} {{ c.what }} <span class="dim">· {{ c.detail }}</span>
            <span v-if="!c.ok && c.items?.length" class="dim small"> ({{ c.items.slice(0, 5).join(', ') }}{{ c.items.length > 5 ? '…' : '' }})</span>
          </li>
        </ul>
        <h4>
          The AI's look at it
          <button class="small" :disabled="state.result.critique?.looking"
                  title="The vision model looks at the built place as the game draws it, in windows, and says what's wrong (a few minutes for a big place)"
                  @click="sendToHost({ type: 'build:inspectBuilt' })">
            {{ state.result.critique && !state.result.critique.looking ? 'Look again' : 'Have the AI look at it' }}
          </button>
        </h4>
        <template v-if="state.result.critique">
          <p v-if="state.result.critique.looking" class="dim">Looking at the built place… (a few minutes for a big place: the vision model answers all at once)</p>
          <p v-else-if="state.result.critique.error" class="dim">Couldn't look ({{ state.result.critique.error }}).</p>
          <template v-else>
            <p>{{ state.result.critique.summary }}</p>
            <ul v-if="state.result.critique.issues?.length">
              <li v-for="i in state.result.critique.issues" :key="i">{{ i }}</li>
            </ul>
            <button
              v-if="state.result.critique.issues?.length"
              @click="revise('Fix what the review of the built place found: ' + state.result.critique.issues.join('; '))"
            >
              Revise with these
            </button>
          </template>
        </template>
        <template v-if="state.result.repairs?.length">
          <h4>Repaired</h4>
          <ul class="dim">
            <li v-for="(r, i) in state.result.repairs" :key="i">{{ r.id }}: {{ r.what }} ({{ r.how }})</li>
          </ul>
        </template>
        <h4>Assets</h4>
        <table>
          <thead><tr><th>Source</th><th>For</th><th>Uses</th><th>Asset</th><th>Licence</th></tr></thead>
          <tbody>
            <tr v-for="a in assetRows" :key="a.key" :class="{ placeholder: a.source === 'placeholder' }">
              <td>{{ a.source }}</td>
              <td>{{ a.label }}</td>
              <td>{{ a.uses ?? '' }}</td>
              <td class="small">
                {{ a.source === 'placeholder' || a.source === 'dropped' ? a.why : a.name || a.prefab }}
                <span v-if="a.decision" class="dim"> · {{ a.decision.step }}: {{ a.decision.reason }}</span>
              </td>
              <td class="small">{{ a.licence || (a.source === 'project' ? 'the project’s' : '') }}</td>
            </tr>
          </tbody>
        </table>
        <h4>Next</h4>
        <ul class="dim">
          <li>The place is in the scene now. Finish saves the scene with it and closes the build (reopen it from Your builds).</li>
          <li v-if="placeholderKinds">
            {{ placeholderKinds }} kind(s) still show placeholders: Refine their art below, import art for them, or Build again
            to have the Art tool make what's missing.
          </li>
          <li>Revise below and Build again: the place is replaced, nothing else in the scene is touched.</li>
          <li>Undo (Ctrl+Z) takes the whole build out of the scene in one step.</li>
        </ul>
        <div class="row">
          <button class="primary" title="Save the scene with the place in it, close the build and show it in the Viewport" @click="sendToHost({ type: 'build:finish' }); setPanelVisible('viewport', true)">Finish</button>
        </div>
      </section>
      <template v-if="!showResult">
      <p class="headline">
        {{ state.plan.totals.buildings }} {{ state.plan.totals.unit ?? 'buildings' }} in {{ state.plan.totals.zones }} zones
        <span class="dim">· {{ amountText(state.plan.totals.area) }} cells²</span>
      </p>
      <p v-if="state.plan.rationale" class="dim">{{ state.plan.rationale }}</p>
      <div class="plan-split">
        <img
          v-if="state.image"
          :src="PROJECT_HOST + state.image"
          :class="['map', { large: bigMap }]"
          title="The solved layout, with what each colour means. Click to enlarge."
          @click="bigMap = !bigMap"
        />
        <svg v-else-if="state.sketch" class="sketch" :viewBox="`0 0 ${state.sketch.side} ${state.sketch.side}`">
          <rect x="0" y="0" :width="state.sketch.side" :height="state.sketch.side" class="site" />
          <g v-for="(r, i) in state.sketch.rects" :key="r.id" @click="selectedZone = r.id">
            <rect :x="r.x" :y="r.y" :width="r.w" :height="r.h" :fill="ZONE_COLOURS[i % 6]" :class="{ picked: selectedZone === r.id }" />
            <text :x="r.x + r.w / 2" :y="r.y + r.h / 2" :font-size="state.sketch.side / 28">{{ r.name }}</text>
          </g>
        </svg>
        <table class="components">
          <thead><tr><th>Part</th><th>{{ state.plan.totals.unit ?? 'buildings' }}</th><th>Area</th><th>Holds</th><th>Character</th></tr></thead>
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
      <div v-if="selectedRow" class="why">
        <strong>Why {{ selectedRow.name }}:</strong>
        <span class="dim"> {{ selectedRow.rationale || 'from the module rules' }}</span>
        <div v-if="selectedRow.rel?.length" class="dim small">
          Placed: {{ selectedRow.rel.map((r) => `${r.type} ${r.to}${r.weight ? ' (asked for)' : ''}`).join(' · ') }}
        </div>
        <div v-for="n in selectedRow.notes ?? []" :key="n" class="dim small">{{ n }}</div>
      </div>
      <p v-if="!state.solved" class="dim">The sketch shows sizes only; where each part goes is worked out when it's built. The site is outlined in the viewport.</p>
      <!-- Verify: does the layout hold what the Brief asked for and the plan has? -->
      <section v-if="state.verify?.length" class="verify">
        <h4>
          Verify
          <span :class="verifyIssues.length ? 'warning' : 'ok'">
            {{ verifyIssues.length ? `${verifyIssues.length} to look at` : 'everything asked for is there' }}
          </span>
        </h4>
        <table>
          <thead><tr><th></th><th>What</th><th>Asked</th><th>Planned</th><th>Built</th></tr></thead>
          <tbody>
            <template v-for="row in state.verify" :key="row.id">
              <tr :class="row.status">
                <td class="mark">{{ MARKS[row.status] }}</td>
                <td>{{ row.what }} <span v-if="row.zone" class="dim">{{ row.id }}</span><span v-if="row.note" class="dim"> · {{ row.note }}</span></td>
                <td>{{ row.asked === undefined ? '' : show(row.asked) }}</td>
                <td>{{ row.planned ?? '' }}</td>
                <td>{{ row.built ?? '' }}</td>
              </tr>
              <tr v-for="part in (row.parts ?? []).filter((p) => p.status !== 'ok')" :key="part.id" :class="['part', part.status]">
                <td class="mark">{{ MARKS[part.status] }}</td>
                <td>{{ part.what }} <span class="dim">{{ part.id }}</span></td>
                <td></td>
                <td>{{ part.planned }}</td>
                <td>{{ part.built }}</td>
              </tr>
            </template>
          </tbody>
        </table>
        <details class="dim">
          <summary>Layout checks and counts</summary>
          {{ state.solved?.stats.regions }} areas, {{ state.solved?.stats.objects }} objects, {{ state.solved?.stats.paths }} roads,
          {{ state.solved?.stats.lines }} fences and walls.
          <ul>
            <li v-for="c in state.solved?.checks ?? []" :key="c.id" :class="c.ok ? 'ok' : 'error'">
              {{ c.ok ? '✓' : '✗' }} {{ c.predicate }} <span class="dim">{{ c.id }}</span>
              <span v-if="c.detail !== undefined" class="dim">· {{ show(c.detail) }}</span>
            </li>
          </ul>
        </details>
      </section>

      <!-- Edit after the build (7.8): the part clicked in the viewport, and the module brush. -->
      <section v-if="selectedPart" class="part">
        <h4>
          {{ selectedPart.name }} <span class="dim">{{ selectedPart.id }}</span>
          <span v-if="selectedPart.locked" class="warning"> · locked</span>
        </h4>
        <div class="dim small">
          {{ selectedPart.path.join(' › ') }}<template v-if="selectedPart.path.length"> › </template>{{ selectedPart.base }}
          <template v-if="selectedPart.repeat > 1"> · one of {{ selectedPart.repeat }} (a change applies to all of them)</template>
        </div>
        <div class="fields">
          <template v-for="f in selectedPart.fields" :key="f.name">
            <label :title="f.unit ?? ''">{{ f.name }}</label>
            <select v-if="f.values?.length && f.type === 'enum'" :value="f.value ?? ''" @change="editField(f, ($event.target as HTMLSelectElement).value)">
              <option value="">(default)</option>
              <option v-for="v in f.values" :key="v" :value="v">{{ v }}</option>
            </select>
            <input v-else-if="f.type === 'bool'" type="checkbox" :checked="Boolean(f.value ?? f.default)" @change="editField(f, ($event.target as HTMLInputElement).checked)" />
            <input
              v-else
              :value="fieldText(f)"
              :placeholder="JSON.stringify(f.default ?? '')"
              @change="editField(f, ($event.target as HTMLInputElement).value)"
            />
          </template>
        </div>
        <div class="actions left">
          <button title="Lays this part out again; the rest stays as it is" @click="sendToHost({ type: 'build:regenerate', node: selectedPart.id })">
            Lay out again
          </button>
          <label class="row">
            <input type="checkbox" :checked="selectedPart.locked" @change="sendToHost({ type: 'build:lock', node: selectedPart.id, locked: ($event.target as HTMLInputElement).checked })" />
            Locked (kept as it is when what holds it is laid out again)
          </label>
        </div>
      </section>
      <section v-if="state.sites?.length && (stage === 'planned' || stage === 'built')" class="plan">
        <h4>Sites</h4>
        <p class="dim">Each site of the region is built as a place of its own, where the region put it.</p>
        <table>
          <thead><tr><th>Site</th><th>Kind</th><th>Where</th><th></th></tr></thead>
          <tbody>
            <tr v-for="s in state.sites" :key="s.id">
              <td>{{ s.name || s.id }}</td>
              <td>{{ s.scale.replace('-', ' ') }} {{ s.buildType }}</td>
              <td class="dim">{{ s.bounds[0] }}, {{ s.bounds[1] }} · {{ s.bounds[2] }}×{{ s.bounds[3] }}</td>
              <td><button @click="sendToHost({ type: 'build:nested', id: s.id })">Build this site</button></td>
            </tr>
          </tbody>
        </table>
      </section>
      <div v-if="state.brushModules?.length && (stage === 'plan' || stage === 'planned' || stage === 'built')" class="brush">
        <span>Module brush</span>
        <select :value="brush" @change="setBrush(($event.target as HTMLSelectElement).value)">
          <option value="">off</option>
          <option v-for="m in state.brushModules" :key="m.id" :value="m.id">{{ m.name }}</option>
        </select>
        <span v-if="brush" class="dim">drag an area in the viewport to paint a {{ state.brushModules.find((m) => m.id === brush)?.name }}</span>
      </div>
      <!-- Revise: say what to change; the AI changes the plan and it's laid out again. -->
      <section class="revise">
        <h4>
          Revise
          <button v-if="state.revisions?.length" class="small link" @click="historyOpen = !historyOpen">
            {{ historyOpen ? 'hide' : 'show' }} the history ({{ state.revisions.length }})
          </button>
        </h4>
        <div v-for="(r, i) in historyOpen ? state.revisions ?? [] : []" :key="i" :class="['revision', { undone: r.undone, edit: r.by === 'edit' }]">
          <div v-if="r.by === 'you'"><span class="who">You:</span> {{ r.text }} <span v-if="r.time" class="dim when">{{ when(r.time) }}</span></div>
          <div v-else-if="r.by === 'edit'"><span class="who">You did:</span> {{ r.text }} <span v-if="r.time" class="dim when">{{ when(r.time) }}</span></div>
          <div v-else><span class="who">AI review of the map:</span> <span v-if="r.time" class="dim when">{{ when(r.time) }}</span></div>
          <ul v-if="r.issues?.length" class="dim">
            <li v-for="issue in r.issues" :key="issue">{{ issue }}</li>
          </ul>
          <div v-if="r.summary"><span class="who">{{ r.by === 'edit' ? '' : 'AI:' }}</span> {{ r.summary }}<span v-if="r.undone" class="dim"> (undone)</span></div>
          <div v-for="(c, k) in r.checks ?? []" :key="'c' + k" :class="['small', c.ok ? 'ok' : 'error']">
            {{ c.ok ? '✓' : '✗' }} {{ c.node }} {{ c.type }} {{ c.to }} <span class="dim">· {{ c.detail }}</span>
          </div>
          <div v-for="n in r.notes ?? []" :key="n" class="dim small">{{ n }}</div>
        </div>
        <p v-if="stage === 'revising'">{{ state.status }} <button @click="sendToHost({ type: 'build:cancel' })">Stop</button></p>
        <template v-else>
          <textarea
            v-model="revision"
            rows="2"
            placeholder="e.g. put houses around the commons; the merchant ward needs townhouses; a bigger market"
            @keydown.enter.exact.prevent="revise()"
          />
          <div class="actions left">
            <button class="primary" :disabled="!revision.trim()" @click="revise()">Revise</button>
            <button v-if="verifyIssues.length" :title="fixText" @click="revise(fixText)">Fix what Verify found</button>
            <button v-if="stage === 'planned'" title="The AI looks at the map and fixes what looks wrong" @click="sendToHost({ type: 'build:review' })">
              Check the map
            </button>
            <button :disabled="!canUndo" :title="undoText" @click="sendToHost({ type: 'build:undo' })">↶ Undo</button>
            <button :disabled="!canRedo" :title="redoText" @click="sendToHost({ type: 'build:redo' })">↷ Redo</button>
            <button title="Lays the plan out again with the latest layout rules (no AI)" @click="sendToHost({ type: 'build:relayout' })">
              Lay out again
            </button>
          </div>
        </template>
      </section>
      <section v-if="state.problems?.length" class="problems">
        <div v-for="p in state.problems" :key="p.where + p.message" :class="p.warning ? 'warning' : 'error'">{{ p.where }}: {{ p.message }}</div>
      </section>
      </template>
      <div v-if="!showResult" class="actions">
        <button @click="sendToHost({ type: 'build:cancel' })">Cancel</button>
        <button v-if="stage === 'plan'" @click="sendToHost({ type: 'build:step', step: 'assets' })">Back</button>
        <button v-if="stage === 'plan'" class="primary" @click="sendToHost({ type: 'build:approve' })">
          Approve plan
        </button>
        <template v-else-if="['planned', 'sourcing', 'sourced', 'built'].includes(stage)">
          <span class="dim">{{ stage === 'planned' ? state.status : '' }}</span>
          <button class="primary" title="Where the art comes from, then the build" @click="viewing = 'assets'">Assets →</button>
        </template>
        <span v-else-if="stage === 'building'" class="dim">{{ state.status }}</span>
      </div>
    </div>

    <!-- ① Request and ② Assets: the confirmation window. -->
    <div
      v-else-if="win"
      class="body window"
      :class="{ locked: confirmLocked }"
      @click.capture="guardLocked"
      @mousedown.capture="guardLocked"
      @keydown.capture="guardLocked"
    >
      <div v-if="atConfirm && win.decisions?.length" class="confirm-row">
        <section class="note decide">
          <strong>To decide</strong>
          <span class="dim"> · each with what was assumed; change it if you want another</span>
          <table class="decisions">
            <thead><tr><th>Question</th><th>Assumed</th><th>Your answer</th></tr></thead>
            <tbody>
              <tr v-for="d in win.decisions" :key="d.question">
                <td>{{ d.question }}</td>
                <td class="dim">{{ d.assumed }}<span v-if="d.path" class="small"><br />({{ d.path }}: {{ show(d.current) }})</span></td>
                <td>
                  <template v-if="decisionControl(d) === 'enabled'">
                    <label class="row"><input type="checkbox" :checked="Boolean(d.current)" @change="edit(d.path ?? '', ($event.target as HTMLInputElement).checked)" /> yes</label>
                  </template>
                  <select v-else-if="decisionControl(d) === 'options'" :value="(d.current as string) ?? ''" @change="edit(d.path ?? '', ($event.target as HTMLSelectElement).value)">
                    <option v-for="o in decisionOptions(d)" :key="o" :value="o">{{ o.replace(/-/g, ' ') }}</option>
                  </select>
                  <input v-else-if="decisionControl(d) === 'text'" :value="(d.current as string) ?? ''" @change="edit(d.path ?? '', ($event.target as HTMLInputElement).value)" />
                  <template v-else>
                    <input
                      :value="state?.decisionAnswers?.[d.question] ?? ''"
                      :placeholder="d.options?.length ? 'e.g. ' + d.options.join(' / ') : 'your answer'"
                      :list="'opts-' + d.question.length"
                      @change="sendToHost({ type: 'build:decision', question: d.question, answer: ($event.target as HTMLInputElement).value })"
                    />
                    <datalist :id="'opts-' + d.question.length"><option v-for="o in d.options ?? []" :key="o" :value="o" /></datalist>
                  </template>
                </td>
              </tr>
            </tbody>
          </table>
        </section>
        <section v-if="otherAssumptions.length" class="note assumed">
          <strong>Also assumed:</strong>
          <ul><li v-for="a in otherAssumptions" :key="a">{{ a }}</li></ul>
        </section>
      </div>
      <div v-else :class="{ 'confirm-row': atConfirm }">
        <section v-if="win.questions?.length || Object.values(win.marks).some((m) => m.disputed)" class="note">
          <strong>To decide:</strong> fields marked
          <span class="tag disputed">differ</span> were read differently;
          <span class="tag low">guessed</span> ones are the AI's guesses.
          <ul v-if="win.questions?.length">
            <li v-for="q in win.questions" :key="q">{{ q }}</li>
          </ul>
        </section>
        <section v-if="atConfirm && win.assumptions?.length" class="note assumed">
          <strong>Assumed:</strong>
          <ul><li v-for="a in win.assumptions" :key="a">{{ a }}</li></ul>
        </section>
      </div>
      <section v-if="win.round === 2 && !atConfirm" class="questions">
        <h4>Before the layout</h4>
        <ul v-if="state?.questions?.length">
          <li v-for="(q, i) in state.questions" :key="i">{{ q.text }}</li>
        </ul>
        <p v-else class="dim">Asking the AI what to confirm…</p>
        <textarea
          id="directions"
          v-model="directions"
          rows="4"
          placeholder="Your answers, and anything else about where things go, e.g. the market at the crossroads; houses along the river on its west bank; farms to the south"
          @blur="saveDirections"
        />
        <p class="dim">The layout follows these: where each zone goes, in your words.</p>
      </section>

      <template v-if="win.round === 1 || atConfirm">
        <div class="grid">
          <label>Name</label>
          <div><input :value="win.name" @change="edit('name', ($event.target as HTMLInputElement).value)" /> <span class="dim">{{ win.buildTypeName }}</span></div>

          <label>Size</label>
          <div :class="markClass('scale.class')">
            <select :value="win.size.class" @change="edit('scale.class', ($event.target as HTMLSelectElement).value)">
              <option v-for="c in win.size.classes" :key="c.id" :value="c.id">
                {{ c.id.replace('-', ' ') }} ({{ c.buildings[0] }}–{{ c.buildings[1] }} {{ win.size.unit ?? 'buildings' }})
              </option>
            </select>
            {{ win.size.unit ?? 'buildings' }}
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
                {{ mark('scale.buildings.count')?.readings[i] }}: {{ show(alt) }} {{ win.size.unit ?? 'buildings' }}
              </button>
            </div>
          </div>

          <label v-if="!atConfirm" title="One street pattern or several; the plan gives each ward one of them">Layout</label>
          <div v-if="!atConfirm" :class="markClass('shape.layout')">
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

          <template v-for="f in win.settings ?? []" :key="f.path">
            <label>{{ f.label }}</label>
            <div :class="markClass(f.path)">
              <select v-if="f.values" :value="f.value ?? ''" @change="edit(f.path, ($event.target as HTMLSelectElement).value || null)">
                <option value="">default</option>
                <option v-for="v in f.values" :key="v" :value="v">{{ v.replace(/-/g, ' ') }}</option>
              </select>
              <input v-else-if="f.type === 'count'" class="num" type="number" :min="f.min" :max="f.max" :value="countOf(f.value)" placeholder="default"
                     @change="edit(f.path, ($event.target as HTMLInputElement).value === '' ? null : Number(($event.target as HTMLInputElement).value))" />
              <input v-else-if="f.type === 'bool'" type="checkbox" :checked="Boolean(f.value)" @change="edit(f.path, ($event.target as HTMLInputElement).checked)" />
              <span v-if="f.hint" class="dim"> {{ f.hint }}</span>
            </div>
          </template>

          <label v-if="win.walled.options.length">Walls</label>
          <div v-if="win.walled.options.length" :class="markClass('walled')">
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
            <tr><th>Component</th><th>On</th><th>How many</th><th>Art</th><th>Source</th></tr>
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
                  <span class="dim"> {{ c.unit }}</span>
                  <span v-if="c.holds" class="dim"> · {{ show(c.holds.value) }} {{ c.holds.unit }} each</span>
                  <div v-if="c.kinds?.length && c.level === 'zone'" class="kinds">
                    <label v-for="k in c.kinds" :key="k.module" :title="'How many ' + k.name + ' in each (empty: the plan decides)'">
                      <input class="num small" type="number" min="0" :value="k.count ?? ''" :placeholder="k.estimate !== undefined ? '~' + k.estimate : 'any'"
                             @change="setKind(c.id, k.module, ($event.target as HTMLInputElement).value)" />
                      <span class="dim">{{ k.name }}</span>
                    </label>
                  </div>
                </template>
                <template v-else-if="c.enabled && c.amount === 'density'">
                  <span class="dim">how common </span>
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

      <template v-if="win.round !== 1 || atConfirm">
        <h4 v-if="atConfirm" class="contents-head">What each part holds</h4>
        <div :class="{ 'params-grid': atConfirm }">
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

        </div>
        <section v-if="win.round === 2 && !atConfirm" class="note">
          <label class="row">
            <input type="checkbox" :checked="sourcingSettings.webSearch" @change="setSourcing({ webSearch: ($event.target as HTMLInputElement).checked })" />
            Search the web for free art (OpenGameArt; only CC0, CC-BY and OGA-BY, credited in CREDITS.md)
          </label>
          <label class="row">
            <input type="checkbox" :checked="sourcingSettings.shareAlike" @change="setSourcing({ shareAlike: ($event.target as HTMLInputElement).checked })" />
            Allow share-alike art (CC-BY-SA: the game's art must then be shared alike)
          </label>
        </section>
        <section v-if="win.assetPlan && !atConfirm" class="plan">
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

      <section v-if="win.assumptions?.length && !atConfirm" class="note">
        <strong>Assumed:</strong>
        <ul><li v-for="a in win.assumptions" :key="a">{{ a }}</li></ul>
      </section>
      <section v-if="errors.length || warnings.length" class="problems">
        <div v-for="p in errors" :key="p.where + p.message" class="error">{{ p.where }}: {{ p.message }}</div>
        <div v-for="p in warnings" :key="p.where + p.message" class="warning">{{ p.where }}: {{ p.message }}</div>
      </section>

      <div v-if="confirmLocked" class="actions">
        <button class="primary" data-view-ok @click="backToPlan()">Back to the plan →</button>
      </div>
      <div v-else-if="atConfirm" class="actions">
        <button @click="sendToHost({ type: 'build:cancel' })">Cancel</button>
        <span v-if="state?.layout?.stale" class="dim">These changed what the zones were planned from:</span>
        <button v-if="state?.layout?.stale" class="primary" :disabled="errors.length > 0" @click="sendToHost({ type: 'build:replanLayout' }); confirmTab = 'layout'">
          Lay out the zones again →
        </button>
        <button v-else class="primary" :disabled="errors.length > 0" title="② Layout: the zones and where they go" @click="confirmTab = 'layout'">
          Layout →
        </button>
      </div>
      <div v-else class="actions">
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
        <button v-else class="primary" :disabled="errors.length > 0" @click="saveDirections(); sendToHost({ type: 'build:confirm' })">
          Lay out the zones →
        </button>
      </div>
    </div>
  </div>
</template>

<style scoped>
.build {
  display: flex;
  flex-direction: column;
  height: 100%;
  position: relative;
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
.kinds {
  display: flex;
  flex-wrap: wrap;
  gap: 2px 10px;
  margin-top: 3px;
}
.kinds label {
  display: inline-flex;
  align-items: center;
  gap: 3px;
  white-space: nowrap;
}
.kinds input.num.small {
  width: 46px;
}
.note.quality {
  display: flex;
  gap: 10px;
  align-items: flex-start;
  justify-content: space-between;
}
.note.quality ul {
  margin: 4px 0 0;
  padding-left: 18px;
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
.result {
  border-left: 3px solid #a6e3a1;
  padding: 6px 10px;
  margin: 8px 0 12px;
  background: #1e1e2e;
}
.result.failing {
  border-left-color: #f9e2af;
}
.result h4 {
  margin: 10px 0 4px;
  font-weight: 600;
}
.result table {
  border-collapse: collapse;
  width: 100%;
}
.result td,
.result th {
  text-align: left;
  padding: 2px 8px 2px 0;
  border-bottom: 1px solid #313244;
}
.result th {
  color: #7f849c;
  font-weight: normal;
}
.result .placeholder td:first-child {
  color: #f9e2af;
}
.part {
  border-left: 3px solid #fab387;
  padding: 6px 10px;
  margin: 8px 0;
  background: #1e1e2e;
}
.part h4 {
  margin: 0 0 4px;
  font-weight: 600;
}
.part .fields {
  display: grid;
  grid-template-columns: max-content 1fr;
  gap: 3px 10px;
  align-items: center;
  margin: 6px 0;
}
.brush {
  display: flex;
  gap: 6px;
  align-items: center;
  margin: 8px 0;
}
.verify h4,
.revise h4 {
  margin: 14px 0 6px;
  font-weight: 600;
}
.verify h4 span {
  font-weight: normal;
  margin-left: 8px;
}
.verify table {
  border-collapse: collapse;
  width: 100%;
}
.verify th,
.verify td {
  text-align: left;
  padding: 2px 8px 2px 0;
  border-bottom: 1px solid #313244;
}
.verify th {
  color: #7f849c;
  font-weight: normal;
}
.verify .mark {
  width: 14px;
}
.verify tr.ok .mark,
.ok {
  color: #a6e3a1;
}
.verify tr.short .mark {
  color: #f9e2af;
}
.verify tr.missing .mark {
  color: #f38ba8;
}
.verify tr.part td:nth-child(2) {
  padding-left: 16px;
}
.verify details {
  margin-top: 6px;
}
.revise textarea {
  width: 100%;
  box-sizing: border-box;
  background: #1e1e2e;
  color: #cdd6f4;
  border: 1px solid #45475a;
  border-radius: 0;
  font: inherit;
  padding: 4px 6px;
  resize: vertical;
}
.revision {
  border-left: 3px solid #45475a;
  padding: 4px 8px;
  margin: 6px 0;
}
.revision.undone {
  opacity: 0.55;
}
.revision ul {
  margin: 2px 0 2px 16px;
  padding: 0;
}
.who {
  color: #89b4fa;
}
.actions.left {
  justify-content: flex-start;
  margin-top: 6px;
}
.notices {
  margin: 8px 12px 0;
  border-left-color: #f9e2af;
}
.builds {
  margin-top: 16px;
}
.builds h4 {
  margin: 0 0 6px;
  font-weight: 600;
}
.build-row {
  display: flex;
  width: 100%;
  gap: 12px;
  text-align: left;
  border: none;
  border-bottom: 1px solid #313244;
  padding: 5px 6px;
}
.build-row .name {
  flex: 1;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.build-row .when {
  width: 70px;
  text-align: right;
}
.build-row:hover {
  background: #313244;
}
.build-row.current .name {
  color: #89b4fa;
}
.art-steps {  list-style: none;  margin: 8px 0 0;  padding: 0;  max-height: 50vh;  overflow-y: auto;}.art-steps > li {  padding: 4px 0;  border-bottom: 1px solid #313244;}.art-steps > li.waiting .label {  color: #6c7086;}.art-steps > li.working .mark,.art-steps > li.working .label {  color: #89b4fa;}.art-steps > li.done .mark {  color: #a6e3a1;}.art-steps > li.failed .mark {  color: #f38ba8;}.art-steps .mark {  display: inline-block;  width: 16px;}.art-steps .pieces {  display: flex;  flex-wrap: wrap;  gap: 4px 10px;  margin: 3px 0 0 16px;  font-size: 11px;  color: #a6adc8;}.art-steps .piece {  display: inline-flex;  align-items: center;  gap: 4px;}.art-steps .piece.missing {  color: #f38ba8;}.art-steps .piece img {  width: 28px;  height: 28px;  object-fit: contain;  cursor: zoom-in;}
.step-usage {  display: block;  font-size: 10px;  opacity: 0.6;  font-weight: normal;}
.background-view img {
  display: block;
  max-width: 100%;
  max-height: 60vh;
  margin: 8px auto;
  image-rendering: auto;
  cursor: zoom-in;
  border: 1px solid #313244;
}
.background-view p {
  text-align: center;
}
.build-row-wrap {
  display: flex;
  border-bottom: 1px solid #313244;
}
.build-row-wrap .build-row {
  border-bottom: none;
}
.build-row-wrap .remove {
  border: none;
  background: none;
  color: #6c7086;
  padding: 0 8px;
}
.build-row-wrap .remove:hover {
  color: #f38ba8;
  background: #313244;
}
.browse {
  margin-left: auto;
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
.map {
  width: 420px;
  flex-shrink: 0;
  cursor: zoom-in;
  image-rendering: auto;
}
.map.large {
  width: 100%;
  max-width: none;
  cursor: zoom-out;
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
.sketch text {
  fill: #cdd6f4;
  text-anchor: middle;
  dominant-baseline: middle;
  pointer-events: none;
}
.sketch rect.picked {
  fill-opacity: 0.7;
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
.working-overlay {
  position: absolute;
  inset: 0;
  z-index: 20;
  background: rgba(17, 17, 27, 0.6);
  display: flex;
  align-items: center;
  justify-content: center;
}
.working-box {
  display: flex;
  gap: 12px;
  align-items: center;
  max-width: min(520px, 90%);
  padding: 14px 18px;
  background: #1e1e2e;
  border: 1px solid #3b82f6;
}
.spinner {
  width: 22px;
  height: 22px;
  flex: none;
  border: 3px solid #313244;
  border-top-color: #89b4fa;
  border-radius: 50%;
  animation: spin 0.9s linear infinite;
}
@keyframes spin {
  to {
    transform: rotate(360deg);
  }
}
.choices {
  margin: 8px 0;
}
.choices .toggle {
  background: none;
  border: none;
  padding: 4px 0;
  cursor: pointer;
  color: inherit;
}
.choices .grid {
  display: grid;
  grid-template-columns: max-content 1fr;
  gap: 6px 10px;
  align-items: center;
  padding: 6px 0;
}
.chips button.out {
  text-decoration: line-through;
  opacity: 0.6;
}
.build-progress {
  max-width: 560px;
  margin: 40px auto;
}
.build-progress .bar,
.sourcing .bar {
  height: 6px;
  background: var(--border, #333);
  margin: 4px 0 8px;
}
.build-progress .bar i,
.sourcing .bar i {
  display: block;
  height: 100%;
  background: var(--accent, #3b82f6);
}
.art-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(220px, 1fr));
  gap: 8px;
}
.art-need {
  display: flex;
  gap: 8px;
  padding: 6px;
  border: 1px solid var(--border, #333);
}
.art-need.done.passed {
  border-color: #6c9;
}
.art-need .thumbs {
  display: flex;
  gap: 4px;
  min-width: 64px;
  align-items: center;
}
.art-need figure {
  margin: 0;
  text-align: center;
  font-size: 10px;
}
.art-need figure.bad img {
  opacity: 0.45;
}
.art-need img {
  width: 56px;
  height: 56px;
  object-fit: contain;
  image-rendering: pixelated;
  background: rgba(127, 127, 127, 0.15);
}
.art-need .waiting {
  font-size: 11px;
  opacity: 0.7;
}
.art-need .what {
  display: flex;
  flex-direction: column;
  font-size: 11px;
  min-width: 0;
}
.art-need {
  flex-wrap: wrap;
}
.art-need.open {
  grid-column: 1 / -1;
}
.art-need .refine {
  flex-basis: 100%;
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.art-need .refine textarea {
  width: 100%;
  box-sizing: border-box;
}
.art-need .what button.small {
  align-self: flex-start;
  margin-top: 4px;
  font-size: 11px;
  padding: 1px 8px;
}
.art-view .actions {
  display: flex;
  gap: 8px;
  align-items: center;
  justify-content: flex-end;
  padding: 8px 0;
}
.zoomable {
  cursor: zoom-in;
}
.advanced {
  border-bottom: 1px solid #333;
  padding: 4px 8px 8px;
}
.advanced pre {
  max-height: 40vh;
  overflow: auto;
  margin: 6px 0 0;
  padding: 8px;
  background: #11111b;
  border: 1px solid #2a2a3a;
  font-size: 11px;
  white-space: pre-wrap;
  word-break: break-all;
}
.busy-note {
  display: flex;
  gap: 10px;
  align-items: center;
  justify-content: space-between;
  border-left: 3px solid #3b82f6;
}
.map-actions {
  display: flex;
  gap: 4px;
}
.revision.edit {
  opacity: 0.85;
}
.revision .when {
  font-size: 0.8em;
}
button.link {
  border: none;
  background: transparent;
  color: #89b4fa;
  padding: 0 4px;
  font-weight: normal;
}
.set-all {
  display: flex;
  gap: 6px;
  align-items: center;
  margin: 4px 0 6px;
}
.needs .thumb img {
  width: 28px;
  height: 28px;
  object-fit: contain;
  image-rendering: pixelated;
}
.thumb.art {
  width: 52px;
}
.thumb.art img {
  width: 48px;
  height: 48px;
  object-fit: contain;
  cursor: zoom-in;
  display: block;
}
.placeholder-box {
  display: block;
  width: 40px;
  height: 28px;
  margin: 10px 4px;
  border: 1px dashed #6c7086;
}
.needs .desc {
  max-width: 340px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.unfound-row {
  display: flex;
  gap: 6px;
  align-items: center;
  padding: 3px 0;
}
.unfound-row .spacer {
  flex: 1;
}
.rejected-thumb {
  width: 40px;
  height: 40px;
  object-fit: contain;
  outline: 1px solid #f38ba8;
  cursor: zoom-in;
}
.picker {
  max-width: 760px !important;
  width: min(760px, 92vw);
}
.pick-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(96px, 1fr));
  gap: 6px;
  max-height: 55vh;
  overflow: auto;
  margin: 8px 0;
}
.pick {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
  padding: 4px;
}
.pick img {
  width: 80px;
  height: 80px;
  object-fit: contain;
  image-rendering: pixelated;
}
.pick span {
  font-size: 0.75em;
  max-width: 100%;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.view-note {
  display: flex;
  gap: 10px;
  align-items: center;
  justify-content: space-between;
}
.body.locked input,
.body.locked select,
.body.locked textarea,
.body.locked button:not([data-view-ok]) {
  opacity: 0.75;
}
.lightbox.ask {
  cursor: default;
}
.ask-box {
  background: #1e1e2e;
  border: 1px solid #45475a;
  padding: 16px 18px;
  max-width: 420px;
}
.ask-box p {
  margin: 8px 0 12px;
}
.lightbox {
  position: fixed;
  inset: 0;
  z-index: 50;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 8px;
  background: rgba(0, 0, 0, 0.8);
  cursor: zoom-out;
}
.lightbox img {
  max-width: 90%;
  max-height: 80%;
  min-width: 256px;
  object-fit: contain;
  image-rendering: pixelated;
  background: rgba(127, 127, 127, 0.2);
}
.lightbox .caption {
  color: #fff;
  font-size: 12px;
}
.layout-view .layout-top {
  display: flex;
  gap: 10px;
  align-items: flex-start;
  flex-wrap: wrap;
}
.layout-view .zones-map {
  width: 320px;
  max-width: 100%;
  image-rendering: pixelated;
}
.layout-view table.zones {
  flex: 1;
  min-width: 280px;
  border-collapse: collapse;
}
.layout-view table.zones td,
.layout-view table.zones th {
  text-align: left;
  vertical-align: top;
  padding: 3px 6px;
  border-bottom: 1px solid var(--border, #333);
}
.questions textarea {
  width: 100%;
  box-sizing: border-box;
}
.confirm-tabs {
  display: flex;
  gap: 4px;
  align-items: center;
  padding: 6px 10px 0;
}
.confirm-tabs button {
  border: 1px solid var(--border, #444);
  background: none;
  color: inherit;
  padding: 4px 14px;
  cursor: pointer;
}
.confirm-tabs button.active {
  background: var(--accent, #3b82f6);
  color: #fff;
  border-color: var(--accent, #3b82f6);
}
.layout-view .zones-map {
  width: 520px;
  max-width: 100%;
  cursor: crosshair;
}
.layout-view .map-box {
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.layout-view .main-layout {
  display: flex;
  gap: 6px;
  align-items: center;
  flex-wrap: wrap;
}
.layout-view table.zones {
  width: 100%;
  margin-top: 8px;
}
.swatch {
  display: inline-block;
  width: 10px;
  height: 10px;
  margin-right: 4px;
  vertical-align: middle;
}
.warning {
  color: #e5a50a;
}
.drawing {
  position: fixed;
  inset: 0;
  z-index: 40;
  background: rgba(0, 0, 0, 0.7);
  display: flex;
  align-items: center;
  justify-content: center;
}
.drawing-box {
  display: flex;
  gap: 12px;
  background: var(--panel-bg, #1e1e1e);
  padding: 12px;
  max-width: 95vw;
  max-height: 92vh;
  overflow: auto;
}
.drawing canvas {
  width: 560px;
  height: 560px;
  max-width: 70vw;
  max-height: 70vw;
  cursor: crosshair;
  image-rendering: pixelated;
}
.draw-list {
  display: flex;
  flex-direction: column;
  gap: 4px;
  min-width: 220px;
}
.draw-zone {
  text-align: left;
  background: none;
  border: 1px solid var(--border, #444);
  color: inherit;
  padding: 4px 6px;
  cursor: pointer;
}
.draw-zone.active {
  border-color: var(--accent, #3b82f6);
  background: rgba(59, 130, 246, 0.15);
}
.contents-head {
  margin-top: 12px;
}
.layout-view .layout-top table.zones {
  flex: 1;
  min-width: 320px;
  margin-top: 0;
}
.layouts-section .layout-row {
  display: flex;
  gap: 8px;
  align-items: center;
  margin: 4px 0;
}
.layouts-section .layout-name {
  min-width: 130px;
  font-size: 12px;
}
.small-tiles .layout button span {
  font-size: 10px;
}
.confirm-row {
  display: flex;
  gap: 12px;
  align-items: flex-start;
}
.confirm-row > section {
  flex: 1;
  min-width: 0;
}
.params-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(300px, 1fr));
  gap: 8px;
}
.params-grid .params {
  margin: 0;
}
.drawing .brush {
  display: flex;
  flex-direction: column;
  gap: 4px;
  margin: 8px 0;
}
.progress-view .query {
  font-style: italic;
}
.progress-steps {
  list-style: none;
  padding: 0;
  margin: 10px 0;
}
.progress-steps > li {
  margin: 6px 0;
}
.progress-steps > li.waiting {
  opacity: 0.5;
}
.progress-steps > li.active .label {
  font-weight: 600;
}
.progress-steps .mark {
  display: inline-block;
  width: 18px;
}
.progress-steps .detail {
  margin: 2px 0 0 18px;
  padding-left: 12px;
  font-size: 11px;
  opacity: 0.8;
}
table.decisions {
  width: 100%;
  border-collapse: collapse;
  margin-top: 4px;
}
table.decisions td,
table.decisions th {
  text-align: left;
  vertical-align: top;
  padding: 3px 6px;
  border-bottom: 1px solid var(--border, #333);
  font-size: 12px;
}
table.decisions input {
  width: 100%;
  box-sizing: border-box;
}
.confirm-row > .decide {
  flex: 2;
}
.verdict {
  font-size: 0.85em;
}
.verdict.kept {
  color: #7fd07f;
}
.verdict.rejected {
  color: #e08080;
}

.layout-form {
  border: 1px solid var(--border, #3a3a40);
  padding: 8px 10px;
  margin-bottom: 10px;
}
.layout-form h4 {
  margin: 0 0 4px;
}
.layout-form .form-grid {
  display: grid;
  grid-template-columns: max-content 1fr;
  gap: 6px 12px;
  align-items: start;
}
.layout-form label {
  text-transform: capitalize;
  padding-top: 3px;
}
.layout-form .size-pair input {
  width: 4em;
}
.background-description {
  width: 100%;
  box-sizing: border-box;
  font: inherit;
  font-size: 12px;
  resize: vertical;
}
</style>
