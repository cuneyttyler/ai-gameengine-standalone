<script setup lang="ts">
// The AI Planner (development_plan.md 22, docs/ai/ai-planner.md §6): one view per stage — the
// Game Brief's confirmation window, the outline (story, cast, places, stacked), the story's acts and scene
// board, the cast, quests and places, systems, visuals and the validation report — and on
// every item Why?, Regenerate, Edit and Lock. Documents whose inputs changed show as stale;
// "Regenerate stale" redoes exactly those. A place's draft Brief opens in the Build panel.
import { computed, ref, watch } from 'vue'
import CastGrid, { type CastRow } from './CastGrid.vue'
import CharacterPlacements from './CharacterPlacements.vue'
import AiProgress from './AiProgress.vue'
import { editorState } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'
import { BUILD_METHODS, normalBuildMethod } from '../buildMethods'
import DocView from './DocView.vue'
import SystemsView from './SystemsView.vue'
import ReviseBox, { type Revision } from './ReviseBox.vue'
import VisualsView from './VisualsView.vue'
import ObjectsView from './ObjectsView.vue'
import SmallScenesView from './SmallScenesView.vue'
import CutscenesView from './CutscenesView.vue'
import LibraryPicker from './LibraryPicker.vue'
import DataTable from './DataTable.vue'
import { activatePanel, setPanelVisible } from '../layout'
import Splitter from './Splitter.vue'
import { confirmDialog } from '../dialogs'

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
// ⑬'s tabs: Result, Validate (the build against the plan, fixed), Story (each scene and what was built for it).
// The build page's tabs (34.5): what each step built, then what it's checked against.
type BuildTab = 'places' | 'characters' | 'quests' | 'requirements' | 'cast' | 'placements' | 'objects' | 'smallscenes' | 'gameplay' | 'visuals' | 'validate' | 'story' | 'result'
const buildTab = ref<BuildTab>('places')
const buildTabs = computed(() => {
  const b = state.value.build ?? {}
  const reqItems = (b.requirements?.items ?? []) as any[]
  return [
    // The Build page in its order (each tab builds its part, to the requirements made first, from the plan alone).
    { id: 'requirements', label: 'Requirements', count: reqItems.length ? String(reqItems.filter((r) => r.kind !== 'character-state').length) : '' },
    { id: 'places', label: 'Places', count: `${(b.rows ?? []).filter((r: any) => r.status === 'built' || r.status === 'placeholder').length}/${(b.rows ?? []).length}` },
    { id: 'characters', label: 'Characters', count: characterArt.value.length ? String(characterArt.value.length) : '' },
    { id: 'objects', label: 'Props & objects', count: (() => {
      const all = ((state.value.objects?.objects ?? []) as any[]).filter((o) => o?.id)
      return all.length ? `${all.filter((o) => b.world?.objects?.[o.id]?.status === 'built').length}/${all.length}` : ''
    })() },
    { id: 'quests', label: 'Quests', count: (b.bindings?.aliases ?? []).length ? String(b.bindings.aliases.length) : '' },
    { id: 'placements', label: 'Placements', count: (() => {
      const w = reqItems.filter((r) => r.kind === 'whereabouts')
      const bad = w.filter((r) => r.status === 'failed').length
      return w.length ? (bad ? `${bad} ✗` : `${w.filter((r) => r.status === 'verified').length}/${w.length}`) : ''
    })() },
    { id: 'cast', label: 'Behaviour', count: (() => {
      const cs = reqItems.filter((r) => r.kind === 'character-state')
      const bad = cs.filter((r) => r.status === 'failed').length
      return cs.length ? (bad ? `${bad} ✗` : `${cs.filter((r) => r.status === 'verified').length}/${cs.length}`) : ''
    })() },
    { id: 'smallscenes', label: 'Scenes', count: (() => {
      const all = ((state.value.smallscenes?.scenes ?? []) as any[]).filter((o) => o?.id)
      return all.length ? `${all.filter((o) => b.world?.scenes?.[o.id]?.status === 'built').length}/${all.length}` : ''
    })() },
    { id: 'gameplay', label: 'Systems', count: (() => {
      const mine = itemsOfTab('gameplay')
      return mine.length ? `${mine.filter((x) => x.status === 'built').length}/${mine.length}`
        : (b.features ?? []).length ? `${(b.features ?? []).filter((f: any) => f.status === 'done').length}/${b.features.length}` : ''
    })() },
    { id: 'visuals', label: 'Visuals', count: (() => {
      const panels = ((state.value.ui?.panels ?? []) as any[]).filter((p) => p?.id)
      const built = panels.filter((p) => b.visuals?.panels?.[p.id]?.status === 'built').length
      return panels.length ? `${built}/${panels.length}` : ''
    })() },
    { id: 'validate', label: 'Validate', count: checkOpen.value.length ? String(checkOpen.value.length) : '' },
    { id: 'story', label: 'Story & Test', count: '' },
    { id: 'result', label: 'Result', count: '' },
  ] as { id: BuildTab; label: string; count: string }[]
})
// Where the build is: its stage as the Build tab it's on (the steps before it done, those after it to come).
const STAGE_TAB: Record<string, BuildTab> = {
  requirements: 'requirements', build: 'places', characters: 'characters', objects: 'objects', quests: 'quests',
  placements: 'placements', behaviour: 'cast', scenes: 'smallscenes', systems: 'gameplay', features: 'gameplay', visuals: 'visuals',
  finish: 'visuals', playtest: 'validate', result: 'result', done: 'result',
}
const STEP_TABS: BuildTab[] = ['requirements', 'places', 'characters', 'objects', 'quests', 'placements', 'cast', 'smallscenes', 'gameplay', 'visuals']
const buildStageTab = computed<BuildTab | ''>(() => STAGE_TAB[state.value.build?.stage ?? ''] ?? '')
function tabState(id: BuildTab): 'done' | 'now' | 'next' | '' {
  const b = state.value.build ?? {}
  const stage = b.stage ?? ''
  // The furthest stage reached marks the steps done; the current stage is the one the build is at. A single place
  // rebuilt from the start (a step before the last) doesn't turn the later steps back into "to come".
  const reached = (b.reached as string) || stage
  if (reached === 'done' || reached === 'result' || reached === 'playtest') return STEP_TABS.includes(id) ? 'done' : ''
  const reachedIdx = STEP_TABS.indexOf((STAGE_TAB[reached] ?? '') as BuildTab)
  const nowIdx = STEP_TABS.indexOf(buildStageTab.value as BuildTab)
  const at = STEP_TABS.indexOf(id)
  if (at < 0) return ''
  if (reachedIdx >= 0 && at <= reachedIdx) return at === nowIdx && stage !== 'finish' ? 'now' : 'done'
  return at === nowIdx ? 'now' : 'next'
}
const stepLine = computed(() => {
  const b = state.value.build ?? {}
  const stage = b.stage ?? ''
  const tab = buildTabs.value.find((t) => t.id === buildStageTab.value)
  const n = STEP_TABS.indexOf(buildStageTab.value as BuildTab)
  const how = b.status === 'working' ? 'working' : b.status === 'waiting' ? 'waiting for you' : b.status === 'stopped' ? 'stopped: Continue building goes on from here' : b.status ?? ''
  if (stage === 'finish') return `Building what's left from the steps (after Visuals) — ${how}`
  if (stage === 'playtest') return `Playtesting — ${how}`
  if (stage === 'result' || stage === 'done') return 'Built: see Validate, Story & Test and Result'
  return tab ? `Step ${n + 1} of ${STEP_TABS.length}: ${tab.label} — ${how}` : ''
})
// Requirements (34.1): what the plan says must be true in the game, by scene, filtered by kind and by who made it.
const reqs = computed(() => (state.value.build?.requirements ?? {}) as Record<string, any>)
// The build's art: the characters (and the enemies' kinds) on the Characters tab; the rest (the items' icons, the
// interface's panel, the places' doors and ways through) on Props & objects.
const isCharacterArt = (a: any) => /^(character\/|class:)/.test(String(a?.subject ?? ''))
const characterArt = computed(() => ((state.value.build?.art ?? []) as any[]).filter(isCharacterArt))
const isInterfaceArt = (a: any) => String(a?.subject ?? '').startsWith('ui:')
const objectArt = computed(() => ((state.value.build?.art ?? []) as any[]).filter((a) => !isCharacterArt(a) && !isInterfaceArt(a)))
const interfaceArt = computed(() => ((state.value.build?.art ?? []) as any[]).filter(isInterfaceArt))
const reqKind = ref('')
const reqBy = ref('')
const reqFolded = ref<Record<string, boolean>>({})
const reqKinds = computed(() => [...new Set(((reqs.value.items ?? []) as any[]).filter((r) => r.kind !== 'asset' && r.kind !== 'animation' && r.kind !== 'character-state').map((r) => r.kind as string))].sort())
// The cast timeline (⑥b): a tab per quest, characters by its stages, and its cutscenes; from the plan (what's designed)
// or the build's requirements (how it checked).
function castKey(when: any): string {
  if (!when || when.start) return 'start'
  return `${when.quest}:${when.stage}:${when.on ?? 'enter'}`
}
const placeShort = (p: string) => placeNames.value.get(p) ?? String(p ?? '').split('/').pop() ?? ''
const castNames = computed(() => new Map<string, string>(((state.value.characters ?? []) as any[]).map((c) => [c.id, c.name || c.id])))
const whoName = (w: string) => (w === 'player' ? 'the player' : castNames.value.get(w) ?? String(w).split('/').pop() ?? w)
// A state in short (the grid) and in full (the detail).
function stateShort(st: any, isStart: boolean): string {
  const parts: string[] = []
  if (st?.place) parts.push((isStart ? '' : '→ ') + placeShort(st.place))
  if (st?.behaviour && (!isStart || st.behaviour !== 'friendly')) parts.push(st.behaviour)
  if (!isStart && st?.packages?.length) parts.push('runs ' + st.packages.map((p: any) => p.template ?? p).join('+'))
  if (!isStart && st?.dialogue) parts.push('talks')
  return parts.join(' · ') || (st?.does ? String(st.does) : '…')
}
function stateLines(st: any, why?: string): { label: string; value: string }[] {
  const lines: { label: string; value: string }[] = []
  if (st?.place) lines.push({ label: 'Where', value: placeShort(st.place) + (st.spot ? ` — ${st.spot}` : '') })
  if (st?.does) lines.push({ label: 'Does', value: st.does })
  for (const p of st?.packages ?? []) {
    lines.push({ label: 'AI package', value: `${p.template ?? p}${p.at ? ' — ' + p.at : p.place ? ' at ' + placeShort(p.place) : ''}${p.schedule?.from ? ` ${p.schedule.from}–${p.schedule.to ?? ''}` : ''}` })
  }
  if (st?.behaviour) lines.push({ label: 'Behaviour', value: st.behaviour })
  if (st?.dialogue) lines.push({ label: 'Talks about', value: st.dialogue })
  if (why) lines.push({ label: 'Why', value: why })
  return lines
}
function cutsceneLines(c: any): { label: string; value: string }[] {
  const t = c?.trigger ?? {}
  const trig = t.kind === 'leave' ? `coming out of ${placeShort(t.from)}` : t.kind === 'reach' ? `reaching ${t.spot}` : t.kind === 'talk-end' ? `the conversation with ${whoName(t.with)} ends`
    : t.kind === 'event' ? `the event ${t.name}` : 'as the stage begins'
  const lines = [{ label: 'Cutscene', value: `${c.name} (${c.id})${c.scene ? ' — from ' + c.scene : ''}` }, { label: 'Where', value: placeShort(c.place) },
    { label: 'Plays', value: trig + ` (${c.quest} stage ${c.stage}${c.on === 'complete' ? ' done' : ''})` }]
  for (const m of c.cast ?? []) lines.push({ label: whoName(m.who), value: [m.role, m.does].filter(Boolean).join(': ') })
  ;(c.beats ?? []).forEach((b: string, k: number) => b && lines.push({ label: k === 0 ? 'Beats' : '', value: `${k + 1}. ${b}` }))
  if (c.after) lines.push({ label: 'After', value: c.after })
  return lines
}
// The quests (a tab each): the timeline's snapshot of them, else what its cells name.
const castQuests = computed(() => {
  const qs = ((state.value.timeline?.quests ?? []) as any[]).map((q) => ({ id: q.id, name: q.name, type: q.type, stages: q.stages ?? [] }))
  if (qs.length) return qs
  const found = new Map<string, Set<number>>()
  for (const t of (state.value.timeline?.characters ?? []) as any[]) {
    for (const c of t.changes ?? []) {
      if (!found.has(c.quest)) found.set(c.quest, new Set())
      found.get(c.quest)!.add(Number(c.stage))
    }
  }
  return [...found.entries()].sort((a, b) => (a[0] === 'q-main' ? -1 : b[0] === 'q-main' ? 1 : a[0].localeCompare(b[0])))
    .map(([id, st]) => ({ id, name: id, type: id === 'q-main' ? 'main' : 'side', stages: [...st].map((index) => ({ index })) }))
})
// The cutscenes row: each stage's cutscenes; a character's cell marks the ones they're in.
function cutRow(cuts: { c: any; status?: string; checks?: any[]; id?: string; failed?: string }[]): CastRow {
  const cells: CastRow['cells'] = {}
  for (const x of cuts) {
    const k = castKey(x.c)
    const was = cells[k]
    cells[k] = {
      short: was ? `${was.short}, ${x.c.name}` : x.c.name,
      lines: [...(was?.lines ?? []), ...cutsceneLines(x.c)],
      status: was?.status === 'failed' ? 'failed' : x.status ?? was?.status,
      checks: [...(was?.checks ?? []), ...(x.checks ?? [])],
      id: was?.id ?? x.id,
      failed: was?.failed ?? x.failed,
    }
  }
  return { who: '#cutscenes', name: 'Cutscenes', cells }
}
function markCuts(rows: CastRow[], cuts: any[]) {
  for (const c of cuts) {
    const k = castKey(c)
    for (const m of c.cast ?? []) {
      const row = rows.find((r) => r.who === m.who)
      if (!row) continue
      const cell = row.cells[k] ?? (row.cells[k] = { short: '', lines: [] })
      cell.cuts = [...(cell.cuts ?? []), `${c.name}${m.does ? ' (' + m.does + ')' : ''}`]
      if (!cell.short) cell.short = 'in a cutscene'
    }
  }
}
// When quests overlap (two hold one character at once): rank by the change's priority (story, normal, ambient), then the
// main quest, then the side quests in order. Packages, dialogue and cutscenes are all kept (packages stacked by rank);
// where they are and how they behave is the highest-ranked quest's.
function castRank(quest: string, priority?: string): number {
  const prio = priority === 'story' ? 0 : priority === 'ambient' ? 2 : 1
  const at = castQuests.value.findIndex((q: any) => q.id === quest)
  const order = quest === 'q-main' ? 0 : at < 0 ? 50 : at + 1
  return prio * 100 + order
}
function topQuest(changes: any[], part: string): string {
  let best = '', rank = Infinity
  for (const c of changes) {
    if (c[part] === undefined) continue
    const r = castRank(c.quest, c.priority)
    if (r < rank) (rank = r), (best = c.quest)
  }
  return best
}
function overlapLines(changes: any[], c: any): { lines: { label: string; value: string }[]; yields: boolean; stacked: boolean } {
  const lines: { label: string; value: string }[] = []
  let yields = false, stacked = false
  for (const part of ['place', 'behaviour']) {
    if (c[part] === undefined) continue
    const top = topQuest(changes, part)
    const others = [...new Set(changes.filter((x) => x[part] !== undefined && x.quest !== c.quest).map((x) => x.quest))]
    if (!others.length) continue
    if (top !== c.quest) {
      yields = true
      lines.push({ label: 'Overlap', value: `${part === 'place' ? 'Where they are' : 'How they behave'}: yields to ${top} (ranked higher) while it holds them` })
    } else {
      lines.push({ label: 'Overlap', value: `${part === 'place' ? 'Where they are' : 'How they behave'}: this quest's, over ${others.join(', ')}` })
    }
  }
  if (c.packages !== undefined && changes.some((x) => x.packages !== undefined && x.quest !== c.quest)) {
    stacked = true
    lines.push({ label: 'Overlap', value: 'Packages: kept with the other quests\' and stacked by rank; this one runs while its quest is at these stages and no higher-ranked one holds' })
  }
  if (c.dialogue !== undefined && changes.some((x) => x.dialogue !== undefined && x.quest !== c.quest)) {
    lines.push({ label: 'Overlap', value: 'Dialogue: kept; each quest\'s talk is offered while its stage is on' })
  }
  return { lines, yields, stacked }
}
const planCast = computed(() => {
  const list = ((state.value.timeline?.characters ?? []) as any[])
  const cuts = ((state.value.timeline?.cutscenes ?? []) as any[])
  const rows: CastRow[] = list.map((t) => {
    const cells: CastRow['cells'] = { start: { short: stateShort(t.start, true), lines: stateLines(t.start, t.start?.why) } }
    const changes = (t.changes ?? []) as any[]
    for (const c of changes) {
      const o = overlapLines(changes, c)
      const lines = stateLines(c, c.why)
      if (c.priority) lines.push({ label: 'Priority', value: c.priority })
      cells[castKey(c)] = { short: (o.yields || o.stacked ? '⇅ ' : '') + stateShort(c, false), lines: [...lines, ...o.lines] }
    }
    return { who: t.who, name: castNames.value.get(t.who) ?? t.who, cells }
  })
  markCuts(rows, cuts)
  return { rows, cutscenes: cutRow(cuts.map((c) => ({ c }))) }
})
const buildCast = computed(() => {
  const items = (reqs.value.items ?? []) as any[]
  const byWho = new Map<string, CastRow>()
  for (const r of items.filter((x) => x.kind === 'character-state')) {
    const who = r.who?.[0] ?? ''
    if (!byWho.has(who)) byWho.set(who, { who, name: castNames.value.get(who) ?? who, cells: {} })
    const why = String(r.text ?? '').split(' — ')[1]
    const isStart = !!r.when?.start
    const lines = stateLines(r.state, why)
    if (r.yields) lines.push({ label: 'Overlap', value: `yields to the higher-ranked quest for ${r.yields} (not checked for that)` })
    byWho.get(who)!.cells[castKey(r.when)] = { short: (r.yields ? '⇅ ' : '') + stateShort(r.state, isStart), lines, status: r.status, checks: r.checks, id: r.id, failed: r.why }
  }
  const rows = [...byWho.values()]
  const cutReqs = items.filter((x) => x.kind === 'cutscene' && x.cutscene)
  markCuts(rows, cutReqs.map((x) => x.cutscene))
  return { rows, cutscenes: cutRow(cutReqs.map((x) => ({ c: x.cutscene, status: x.status, checks: x.checks, id: x.id, failed: x.why }))) }
})
const reqStatus = ref('')
// An asset's animations (a character's by its key, an asset's by "asset:<id>").
function animationsOf(r: any): any[] {
  const id = r.asset?.type === 'character' ? r.asset?.id : 'asset:' + (r.asset?.id ?? '')
  return ((reqs.value.items ?? []) as any[]).filter((x) => x.kind === 'animation' && x.animation?.who === id)
}
function checkMark(c: any): string {
  return c.ok === true ? '✓' : c.ok === false ? '✗' : '?'
}
// ⑫ Implement: the items by how they're built, in the order they're built.
// The Build tab an implementation item belongs to (as GameBuild::TabOf has it), by this page's tab ids: Systems only the
// Systems page's features; else by how it's built, and a script or stage effect by what it's about.
function tabOfItem(i: any, r: any): BuildTab {
  const kind = String(r?.kind ?? ''), how = String(i?.how ?? '')
  if (kind === 'system') return 'gameplay' // Systems: the Systems page's features only
  const aboutCharacter = () => {
    const an = r?.animation
    if (an && typeof an === 'object') return /^(character\/|class:)/.test(String(an.who ?? ''))
    return r?.asset?.type === 'character'
  }
  if (how === 'art' || how === 'animation') return r?.asset?.type === 'ui' ? 'visuals' : aboutCharacter() ? 'characters' : 'objects'
  if (['prefab', 'marker', 'spawn-point'].includes(how)) return 'placements'
  if (['objective', 'guidance'].includes(how)) return 'quests'
  if (['package', 'dialogue'].includes(how)) return 'cast'
  if (['cutscene', 'trigger'].includes(how)) return 'smallscenes'
  const people = ['presence', 'whereabouts', 'arrival', 'movement'].includes(kind)
  const things = ['prop', 'item', 'gateway'].includes(kind)
  const moment = ['effect', 'cutscene', 'scene'].includes(kind)
  const doings = ['character-state', 'dialogue', 'combat'].includes(kind)
  if (how === 'quest-effect') return ['quest', 'guidance'].includes(kind) ? 'quests' : doings ? 'cast' : moment ? 'smallscenes' : 'placements'
  if (kind === 'asset' || kind === 'animation') return aboutCharacter() ? 'characters' : 'objects'
  if (['quest', 'guidance'].includes(kind)) return 'quests'
  if (['script', 'stats'].includes(how)) return things ? 'objects' : moment ? 'smallscenes' : 'cast'
  if (people || things) return 'placements'
  if (doings) return 'cast'
  if (moment) return 'smallscenes'
  return 'gameplay'
}
function itemsOfTab(tab: BuildTab): any[] {
  const r = state.value.build?.requirements ?? {}
  const byId = new Map<string, any>(((r.items ?? []) as any[]).map((x) => [x.id, x]))
  return ((r.implementations ?? []) as any[]).filter((i) => tabOfItem(i, byId.get(i.req)) === tab)
}
const tabItems = computed(() => itemsOfTab(buildTab.value))
// The AI panel's tasks of a tab (each says its tab; a gameplay feature from a build before the requirements is Systems').
const TASK_TAB: Record<string, BuildTab> = {
  characters: 'characters', objects: 'objects', quests: 'quests', placements: 'placements', behaviour: 'cast', scenes: 'smallscenes',
  systems: 'gameplay', visuals: 'visuals',
}
// The tab of the item a task builds when it builds one (so a task made before tabs, or under an older mapping, shows
// where its item is); else the tab it says, else Systems.
const taskTab = computed(() => {
  const r = state.value.build?.requirements ?? {}
  const reqs = new Map<string, any>(((r.items ?? []) as any[]).map((x) => [x.id, x]))
  const items = new Map<string, any>(((r.implementations ?? []) as any[]).filter((i) => i.id).map((i) => [i.id, i]))
  return (f: any): BuildTab => {
    const item = f.item ? items.get(f.item) : undefined
    return item ? tabOfItem(item, reqs.get(item.req)) : TASK_TAB[f.tab ?? 'systems'] ?? 'gameplay'
  }
})
const tabTasks = computed(() => ((state.value.build?.features ?? []) as any[]).filter((f) => taskTab.value(f) === buildTab.value))
// A drawing's art from the library (a character with its animations, a prop's picture) instead of drawn.
const libraryFor = ref<{ subject: string; kind: 'character' | 'prop'; name: string } | null>(null)
function fromLibrary(a: any): void {
  libraryFor.value = { subject: a.subject, kind: isCharacterArt(a) ? 'character' : 'prop', name: a.name || a.subject }
}
function useLibrary(item: string): void {
  if (!libraryFor.value) return
  sendToHost({ type: 'planner:buildRow', place: libraryFor.value.subject, item, action: 'useLibrary' })
  libraryFor.value = null
}
// Drawings a library picture can stand in for: characters, the items' icons and the props' assets (not the doors
// found in a painting).
const canUseLibrary = (a: any) => isCharacterArt(a) || a.kind === 'item' || String(a.subject ?? '').startsWith('asset:')
// The cutscenes' items (each its tracks and the trigger that plays it): Build ▸ Scenes builds them.
const cutsceneItems = computed(() => {
  const r = state.value.build?.requirements ?? {}
  const kinds = new Map<string, string>(((r.items ?? []) as any[]).map((x) => [x.id, x.kind]))
  return itemsOfTab('smallscenes').filter((i) => kinds.get(i.req) === 'cutscene')
})
// Each planned cutscene as built: its tracks' item (its status, how it starts, or why not).
const cutscenesBuilt = computed(() => {
  const out: Record<string, { status?: string; starts?: string; error?: string }> = {}
  for (const i of cutsceneItems.value) {
    if (i.how !== 'cutscene' || !String(i.req ?? '').startsWith('cs-')) continue
    out[String(i.req).slice(3)] = { status: i.status, starts: i.starts, error: i.error }
  }
  return out
})
const implByHow = computed(() => {
  const order = ['art', 'animation', 'prefab', 'marker', 'spawn-point', 'stats', 'quest-effect', 'objective', 'guidance', 'cutscene', 'trigger', 'script', 'dialogue']
  const groups = new Map<string, any[]>()
  for (const i of tabItems.value) {
    if (!groups.has(i.how)) groups.set(i.how, [])
    groups.get(i.how)!.push(i)
  }
  const rank = (h: string) => (order.indexOf(h) < 0 ? 99 : order.indexOf(h))
  return [...groups.entries()].sort((a, b) => rank(a[0]) - rank(b[0])).map(([how, items]) => ({
    how, items, built: items.filter((i) => i.status === 'built').length, failed: items.filter((i) => i.status === 'failed'),
    building: items.filter((i) => i.status === 'building').length,
  }))
})
const implProgress = computed(() => {
  const list = (reqs.value.implementations ?? []) as any[]
  const by = (st: string) => list.filter((i) => i.status === st).length
  return { total: list.length, built: by('built'), building: by('building'), failed: by('failed'), todo: by('todo'), skipped: by('skipped') }
})
// By scene, or the visual requirements: the game's assets and how each looks.
const reqView = ref<'scenes' | 'assets'>('scenes')
const reqAssets = computed(() => ((reqs.value.items ?? []) as any[]).filter((r) => r.kind === 'asset')
  .sort((a, b) => (a.asset?.type === 'character' ? 1 : 0) - (b.asset?.type === 'character' ? 1 : 0) || (b.asset?.separate ? 1 : 0) - (a.asset?.separate ? 1 : 0)))
const placeNames = computed(() => new Map<string, string>(((state.value.outline?.places ?? []) as any[]).map((p) => [p.id, p.name])))
function placesText(places: any[]): string {
  return (places ?? []).map((p: string) => placeNames.value.get(p) ?? p).join(', ')
}
// 34.2: each requirement's implementation items.
const implOf = computed(() => {
  const m = new Map<string, any[]>()
  for (const i of (reqs.value.implementations ?? []) as any[]) {
    if (!m.has(i.req)) m.set(i.req, [])
    m.get(i.req)!.push(i)
  }
  return m
})
const reqShowImpl = ref(true)
const reqGroups = computed(() => {
  const items = ((reqs.value.items ?? []) as any[]).filter((r) => r.kind !== 'asset' && r.kind !== 'animation' && r.kind !== 'character-state' && (!reqKind.value || r.kind === reqKind.value) &&
    (!reqBy.value || r.by === reqBy.value) && (!reqStatus.value || r.status === reqStatus.value))
  const order = new Map<string, number>()
  const goal = new Map<string, string>()
  for (const t of check.value?.trace ?? []) {
    order.set(t.scene, t.order ?? 0)
    goal.set(t.scene, t.goal ?? '')
  }
  const groups = new Map<string, any[]>()
  for (const r of items) {
    const key = r.scene ?? ''
    if (!groups.has(key)) groups.set(key, [])
    groups.get(key)!.push(r)
  }
  return [...groups.entries()]
    .sort((a, b) => (a[0] === '' ? 1 : b[0] === '' ? -1 : (order.get(a[0]) ?? 0) - (order.get(b[0]) ?? 0)))
    .map(([scene, list]) => ({ scene, goal: goal.get(scene) ?? '', items: list, act: list[0]?.act ?? '' }))
})
const reqScenesDone = computed(() => ((reqs.value.scenes ?? []) as any[]).filter((s) => s.status === 'done' || s.status === 'failed').length)
function whenText(when: any): string {
  if (!when || typeof when !== 'object') return ''
  if (when.quest) return `${when.quest} stage ${when.stage}`
  if (when.after) return `after: ${when.after}`
  if (when.event) return `on ${when.event}`
  if (when.reach) return `on reaching ${when.reach}`
  if (when.leave) return `on leaving ${when.leave}`
  return when.scene ? `in ${when.scene}` : ''
}
function checksText(accept: any[]): string {
  return (accept ?? []).map((a: any) => [a.check, a.who, a.target, a.name, a.id, a.to, a.place, a.quest && `${a.quest}${a.stage !== undefined ? ' ' + a.stage : ''}${a.from !== undefined ? ' from ' + a.from : ''}`]
    .filter((x) => x !== undefined && x !== null && x !== '').join(' ')).join(' · ')
}
const openScene = ref('')
const check = computed(() => state.value.build?.check as Record<string, any> | null | undefined)
const checkFindings = computed<any[]>(() => check.value?.findings ?? [])
const checkOpen = computed(() => checkFindings.value.filter((f) => f.status === 'open'))
const fixKinds = ['plan', 'game', 'markers', 'file']
const checkFixable = computed(() => checkOpen.value.filter((f) => fixKinds.includes(f.fix?.how)))
const findingsOf = (scene: string) => checkFindings.value.filter((f) => f.scene === scene && f.status !== 'fixed')
const findingsByScene = computed(() => {
  const goal = (scene: string) => (scene ? (check.value?.trace ?? []).find((t: any) => t.scene === scene)?.goal ?? '' : "beyond the story's scenes")
  const groups: { scene: string; goal: string; findings: any[] }[] = []
  for (const f of checkFindings.value) {
    let g = groups.find((x) => x.scene === f.scene)
    if (!g) groups.push((g = { scene: f.scene, goal: goal(f.scene), findings: [] }))
    g.findings.push(f)
  }
  return groups
})
const traceByAct = computed(() => {
  const acts: { act: string; scenes: any[] }[] = []
  for (const t of check.value?.trace ?? []) {
    let a = acts.find((x) => x.act === t.act)
    if (!a) acts.push((a = { act: t.act, scenes: [] }))
    a.scenes.push(t)
  }
  return acts
})
const firstLine = (text: unknown) => String(text ?? '').split(/\r?\n/)[0]
// A scene's test (⑬ Story ▸ Test): queued, running, judging, done (passed or not), failed.
const testOf = (scene: string) => (check.value?.tests ?? {})[scene] as Record<string, any> | undefined
function testLabel(scene: string): string {
  const t = testOf(scene)
  if (!t) return ''
  if (t.status === 'queued' || t.status === 'running') return 'testing…'
  if (t.status === 'watching') return 'testing in Play…'
  if (t.status === 'judging') return 'judging…'
  if (t.status === 'failed') return 'test failed'
  return t.passed ? '✓ passed' : '✗ not as the story has it'
}
function testClass(scene: string): string {
  const t = testOf(scene)
  if (!t || t.status !== 'done') return t?.status === 'failed' ? 'err' : 'busy'
  return t.passed ? 'ok' : 'err'
}
// Play from here and Test play in the Viewport: shown, in front.
function toViewport(): void {
  setPanelVisible('viewport', true)
  activatePanel('viewport')
}
function testEveryScene() {
  sendToHost({ type: 'planner:buildRow', place: '*', action: 'testScene' }) // every written scene, in turn
}
function fixFindings(ids: string[]) {
  sendToHost({ type: 'planner:buildRow', place: '', action: 'fixBuild', ids })
}
// How a finding is fixed, in a line.
function fixText(f: any): string {
  const fix = f.fix ?? {}
  if (fix.how === 'plan') {
    const edits = (fix.edits ?? []).map((e: any) => e.reason || `${e.doc} ${e.pointer}`)
    const merges = (fix.merge ?? []).map((m: any) => `added to the game's ${String(m.doc).replace('quests/', '')}`)
    return 'Fix: ' + [...edits, ...merges].join('; ')
  }
  if (fix.how === 'game') return 'Fix (AI panel): ' + fix.request
  if (fix.how === 'markers') return `Fix: ${(fix.ids ?? []).join(', ')} placed again in ${fix.scene}, in its room, where one can walk to it`
  if (fix.how === 'file') return `Fix: ${fix.path} written`
  return 'For you to decide.'
}
// What a quest role was bound to in the game, in a few words.
function bindingText(b: any): string {
  if (!b) return 'not bound'
  if (b.how === 'failed') return 'not found' + (b.reason ? ` (${b.reason})` : '')
  const what = b.marker || b.entity || ''
  return `${what}${b.scene ? ' in ' + b.scene : ''}${b.reason ? ' — ' + b.reason : ''}`
}
const request = ref('')
// The AI panel handed the Planner a request (its route card): in the box, ready to start a plan.
watch(
  () => editorState.prefill.panel,
  (panel) => {
    if (panel === 'planner') {
      request.value = editorState.prefill.text
      editorState.prefill = { panel: '', text: '' }
    }
  },
  { immediate: true },
)
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
// The pages, grouped (each a step of the stepper, its pages its subtabs): the description of the game, as the build
// makes it. A page's stage: its own, but World's and Life's two new pages are one stage, "world".
const PAGE_GROUPS: { id: string; label: string; pages: string[] }[] = [
  { id: 'concept', label: '① Concept', pages: ['concept'] },
  { id: 'outline', label: '② Outline', pages: ['outline'] },
  { id: 'story', label: '③ Story', pages: ['story'] },
  { id: 'world', label: '④ World', pages: ['characters', 'places', 'objects'] },
  { id: 'quests', label: '⑤ Quests', pages: ['quests'] },
  { id: 'life', label: '⑥ Life', pages: ['timeline', 'cutscenes', 'smallscenes'] },
  { id: 'systems', label: '⑦ Systems', pages: ['systems'] },
  { id: 'visuals', label: '⑧ Visuals', pages: ['visuals'] },
  { id: 'validate', label: '⑨ Validate', pages: ['validate'] },
]
const PAGE_LABELS: Record<string, string> = {
  outline: 'Outline', story: 'Story', characters: 'Characters', places: 'Places', objects: 'Props & objects',
  timeline: 'Cast timeline', cutscenes: 'Cutscenes', smallscenes: 'Small scenes',
}
const stageOfPage = (page: string) => (page === 'objects' || page === 'smallscenes' ? 'world' : page === 'cutscenes' ? 'timeline' : page)
// The story's watched scenes (mode cutscene): each should have its cutscene.
const watchedScenes = computed(() => acts.value.flatMap((a) => a.chapters ?? []).flatMap((c) => c.scenes ?? []).filter((s) => s.mode === 'cutscene').map((s) => s.id))
const stageOf = (page: string) => stages.value.find((s) => s.id === stageOfPage(page))
const groupOf = (page: string) => PAGE_GROUPS.find((g) => g.pages.includes(page))
const pageGroups = computed(() =>
  PAGE_GROUPS.map((g) => {
    const of = g.pages.map(stageOf).filter((s): s is Stage => !!s)
    const first = of.find((s) => s.status !== 'done') ?? of[0]
    return {
      ...g,
      stages: of,
      status: first?.status ?? 'pending',
      reachable: of.some((s) => reachable(s)),
      failed: of.reduce((n, s) => n + (s.failed ?? 0), 0),
      stale: of.reduce((n, s) => n + (s.stale ?? 0), 0),
      review: of.some((s) => s.review),
      done: of.reduce((n, s) => n + s.done, 0),
      total: of.reduce((n, s) => n + s.total, 0),
    }
  }).filter((g) => g.stages.length),
)
const currentGroup = computed(() => groupOf(view.value))
// The stage "world" is two pages: its first shown when the stage itself is asked for.
watch(view, (v) => { if (v === 'world') view.value = 'objects' })
function openGroup(g: { pages: string[] }): void {
  if (g.pages.includes(view.value)) return
  view.value = g.pages.find((p) => { const s = stageOf(p); return s && reachable(s) }) ?? g.pages[0]!
}
// The step on view, when it's one of the plan's (not ⑩–⑬ Build): what Restart from starts again.
const viewedStage = computed(() => stages.value.find((s) => s.id === stageOfPage(view.value) && s.id !== 'done'))
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
const buildRows = computed(() => (state.value.build?.rows ?? []) as { place: string; status: string; step?: string; planPhase?: string;
  method?: string; layoutMade?: boolean; painted?: boolean;
  paint?: { tiles: number; calls: number; cellPx: number; painted: boolean } }[])
// Painted places: their tiles and calls (each once its plan is laid out), the not-yet-painted counted, before painting.
const paintCalls = computed(() => {
  let tiles = 0, calls = 0, places = 0, done = 0
  for (const r of buildRows.value) {
    if (!r.paint) continue
    places++
    if (r.paint.painted) { done++; continue }
    tiles += r.paint.tiles
    calls += r.paint.calls
  }
  return { tiles, calls, places, done }
})
const buildingRow = computed(() => buildRows.value.find((r) => r.status === 'building'))
const buildDone = computed(() => buildRows.value.filter((r) => r.status === 'built' || r.status === 'placeholder').length)
async function rebuildPlace(place: string) {
  if (await confirmDialog({ message: `Build ${place.replace(/^location\//, '')} again from the start? Its scene is replaced.`, confirmLabel: 'Rebuild' }))
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
async function restartBuilds() {
  if (await confirmDialog({ message: 'Build every place again from the start, with the art setting as it is now?', confirmLabel: 'Rebuild all' }))
    sendToHost({ type: 'planner:restartBuilds' })
}
function retryFailedPlaces() {
  for (const r of buildRetryable.value) sendToHost({ type: 'planner:buildRow', place: r.place, action: 'retry' })
}
// The place builder's step, named as the Build panel names it (its activeStep): the AI drafting
// the first plan is still ① Confirm there. A painted place has no ③ Plan: ③ is its Paint.
function stepName(step: string, phase = '', row?: { method?: string; layoutMade?: boolean; painted?: boolean }): string {
  // A build still taking its Brief in: the Build panel's first page, before ① Confirm.
  if (step === 'interpreting') return 'reading the request'
  // Before its zones are laid out, the Build panel is ① Confirm (its Layout tab lays them out).
  if (['request', 'assets'].includes(step) || (step === 'planning' && phase === 'root') ||
      (step === 'layout' && !row?.layoutMade)) return '① Confirm'
  if (step === 'layout') return '② Layout'
  if (row?.method === 'Paint') {
    if (['planning', 'plan', 'revising'].includes(step) || (step === 'planned' && !row.painted)) return '③ Paint'
    if (step === 'planned') return '④ Mark'
    if (['sourcing', 'sourced'].includes(step)) return '⑤ Assets'
    if (['building', 'built'].includes(step)) return '⑥ Result'
    return step
  }
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
// Revise (each step's and each Build tab's own): which one is showing, its name, its history.
const PLAN_REVISABLE: Record<string, string> = { concept: 'the concept', outline: 'the outline', story: 'the story', characters: 'the characters',
  quests: 'the quests', places: 'the places', timeline: 'the cast timeline', systems: 'the systems', visuals: 'the visuals',
  objects: 'the props & objects', smallscenes: 'the small scenes', cutscenes: 'the cutscenes' }
const BUILD_REVISABLE: Record<string, string> = { requirements: 'the requirements', quests: 'the quests (their game files)', placements: 'the placements',
  cast: 'the cast', gameplay: 'the implementations' }
const reviseKey = computed(() => (view.value === 'build' ? (BUILD_REVISABLE[buildTab.value] ? `build:${buildTab.value}` : '')
  : PLAN_REVISABLE[view.value] ? view.value : ''))
const reviseLabel = computed(() => (reviseKey.value.startsWith('build:') ? BUILD_REVISABLE[reviseKey.value.slice(6)] : PLAN_REVISABLE[reviseKey.value]) ?? '')
const reviseEntries = computed<Revision[]>(() => (reviseKey.value.startsWith('build:')
  ? (state.value.build?.revisions?.[reviseKey.value.slice(6)] ?? []) : (state.value.revisions?.[reviseKey.value] ?? [])) as Revision[])
function sendRevert(id: string): void {
  if (reviseKey.value.startsWith('build:')) sendToHost({ type: 'build:revertTabRevision', tab: reviseKey.value.slice(6), id })
  else sendToHost({ type: 'planner:revertRevision', step: reviseKey.value, id })
}
function sendRemove(id: string): void {
  if (reviseKey.value.startsWith('build:')) sendToHost({ type: 'build:removeTabRevision', tab: reviseKey.value.slice(6), id })
  else sendToHost({ type: 'planner:removeRevision', step: reviseKey.value, id })
}
function sendRevise(text: string): void {
  if (reviseKey.value.startsWith('build:')) sendToHost({ type: 'build:reviseTab', tab: reviseKey.value.slice(6), text, history: reviseEntries.value })
  else sendToHost({ type: 'planner:reviseStep', step: reviseKey.value, text, history: reviseEntries.value })
}
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
  const what = { gameplay: 'Gameplay', system: 'System', behaviour: 'Behaviour of', outline: 'The outline', act: 'Act', scene: 'Scene', critic: 'Review of act', character: 'Character', quests: 'The quests', place: 'Place', systems: 'The systems', visuals: 'The interface', review: 'The review', interpret: 'Reading' }[j.kind] ?? j.kind
  return j.subject && j.subject !== j.kind ? `${what} ${j.subject}` : what
}
const waiting = computed(() => state.value.status === 'waiting' || state.value.status === 'failed')
// The places are being built (⑩–⑬, not finished): Continue goes on with them, not back to ⑨.
const buildGoing = computed(() => !!state.value.build?.active && state.value.build?.stage !== 'done')
const outline = computed(() => (state.value.outline ?? {}) as Record<string, any>)
// ② Outline, edited (each change to the outline document; the story graph follows, later steps go stale).
const choices = computed(() => {
  const c = (state.value.outlineChoices ?? {}) as Record<string, string[]>
  return { kinds: c.kinds ?? [], importance: c.importance ?? [], sizes: c.sizes ?? [], landforms: c.landforms ?? [],
           connections: c.connections ?? [], gatewayKinds: c.gatewayKinds ?? [] }
})
function val(e: Event): string {
  return (e.target as HTMLInputElement | HTMLSelectElement | HTMLTextAreaElement).value
}
function list(e: Event): string[] {
  return val(e).split(',').map((x) => x.trim()).filter((x) => x)
}
// The cards open for editing (the rest read as before): 'story', 'world', 'cast:<id>', 'place:<id>', 'link:<index>'.
const editing = ref<string[]>([])
function isEditing(key: string): boolean {
  return editing.value.includes(key)
}
function toggleEdit(key: string) {
  editing.value = isEditing(key) ? editing.value.filter((k) => k !== key) : [...editing.value, key]
}
function editOutline(pointer: string, value: unknown) {
  sendToHost({ type: 'planner:edit', doc: 'outline', pointer, value }) // (an emptied text stays "": the engine reads texts with defaults)
}
function slug(text: string): string {
  return text.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '') || 'new'
}
function freeId(prefix: string, name: string, taken: string[]): string {
  let id = `${prefix}/${slug(name)}`
  for (let n = 2; taken.includes(id); n++) id = `${prefix}/${slug(name)}-${n}`
  return id
}
// A new id for a character or place, changed everywhere in the plan (its edit card follows it).
function renameId(from: string, kind: string, text: string) {
  const to = kind + slug(text)
  if (to === from) return
  if (isEditing('cast:' + from)) editing.value = [...editing.value.filter((k) => k !== 'cast:' + from), 'cast:' + to]
  if (isEditing('place:' + from)) editing.value = [...editing.value.filter((k) => k !== 'place:' + from), 'place:' + to]
  sendToHost({ type: 'planner:renameId', from, to })
}
function setPlayer(at: number | string, on: boolean) {
  const index = Number(at)
  // One player: ticking one unticks the others.
  const cast = (outline.value.cast ?? []).map((c: any, i: number) => ({ ...c, player: on ? i === index : i === index ? false : !!c.player }))
  editOutline('/cast', cast)
}
function removeFrom(list: 'cast' | 'places' | 'connections', at: number | string) {
  const index = Number(at)
  const items = [...(outline.value[list] ?? [])]
  const [gone] = items.splice(index, 1)
  editOutline(`/${list}`, items)
  // A place gone takes its connections with it.
  if (list === 'places' && gone?.id) {
    const kept = (outline.value.connections ?? []).filter((c: any) => c.from !== gone.id && c.to !== gone.id)
    if (kept.length !== (outline.value.connections ?? []).length) editOutline('/connections', kept)
  }
}
// Delete, on a card: asked first (what goes with it said), then removed as Remove does.
function deleteFrom(list: 'cast' | 'places' | 'connections', at: number | string, what: string) {
  const also = list === 'places' ? ' Its connections go with it.' : ''
  if (!window.confirm(`Delete ${what}?${also} The steps after the outline are made again from it.`)) return
  removeFrom(list, at)
}
function addCast() {
  const cast = outline.value.cast ?? []
  const id = freeId('character', 'new character', cast.map((c: any) => c.id))
  editOutline('/cast', [...cast, { id, name: 'New character', importance: 'supporting', player: false, narrativeRoles: [], gameplayRoles: [], line: '' }])
  toggleEdit('cast:' + id)
}
function addPlace() {
  const places = outline.value.places ?? []
  const id = freeId('location', 'new place', places.map((p: any) => p.id))
  editOutline('/places', [...places, { id, name: 'New place', kind: choices.value.kinds[0] ?? 'settlement', parent: '', line: '' }])
  toggleEdit('place:' + id)
}
function addConnection() {
  const places = outline.value.places ?? []
  editOutline('/connections', [...(outline.value.connections ?? []), { from: places[0]?.id, to: places[1]?.id, kind: 'road' }])
  toggleEdit('link:' + (outline.value.connections ?? []).length)
}
// Back to a step and on from there (its work and every later step's made again; what came before stays, with your edits).
// Every page's Regenerate: its documents made again (locked ones kept); Concept is the request read again.
const PAGE_DOCS: Record<string, string> = {
  outline: 'the outline', story: 'every act and its scenes', characters: 'every character', quests: 'every quest', places: 'every place',
  objects: 'the props & objects', timeline: 'the cast timeline', cutscenes: 'the cast timeline and its cutscenes', smallscenes: 'the small scenes',
  systems: 'the gameplay features, then each system design, the data, the controls and the behaviour plans', visuals: 'the interface design',
  validate: 'the review',
}
async function regeneratePage(page: string): Promise<void> {
  if (page === 'concept') {
    await restartFrom('concept', PAGE_GROUPS[0]!.label)
    return
  }
  const label = PAGE_LABELS[page] ?? PAGE_GROUPS.find((g) => g.pages.includes(page))?.label ?? page
  const message = `The AI makes ${PAGE_DOCS[page] ?? 'this page'} again.

Locked documents stay as they are. What was made from them ` +
    'later shows as stale (Regenerate stale makes it again).'
  if (await confirmDialog({ title: `Regenerate ${label}?`, message, confirmLabel: `Regenerate ${label}` })) {
    sendToHost({ type: 'planner:regeneratePage', page })
  }
}
async function restartFrom(stage: string, label: string) {
  // Exactly what goes: this step's work, and every later step that has been made; what stays.
  const all = stages.value.filter((s) => s.id !== 'done')
  const at = all.findIndex((s) => s.id === stage)
  const later = all.slice(at + 1).filter((s) => reachable(s)).map((s) => s.label)
  const before = all.slice(0, Math.max(0, at)).map((s) => s.label)
  const message = stage === 'concept'
    ? `This starts the whole plan again from your request.\n\nEverything it has made is removed and made again: the Game Brief${later.length ? ', ' + later.join(', ') : ''}.`
    : `This restarts from ${label}.\n\n${label} will be removed and made again by the AI` +
      (later.length ? `, and so will everything made after it: ${later.join(', ')}.` : '.') +
      `\n\nWhat comes before stays as it is, with your edits: ${before.join(', ')}.` +
      (stage === 'outline' ? '\n\nYour outline and your edits to it will be replaced by a new one.' : '')
  if (await confirmDialog({ title: `Restart from ${label}?`, message, confirmLabel: `Remove and regenerate ${label}`, danger: true })) {
    sendToHost({ type: 'planner:restartFrom', stage })
  }
}
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
// The main choices, as selects above the rest (Other choices): genre, perspective, tone and art style.
const MAIN_CHOICES = ['genre', 'perspective', 'tone', 'artStyle.style']
const mainChoices = computed<Choice[]>(() => ((state.value.choices ?? []) as Choice[]).filter((c) => MAIN_CHOICES.includes(c.path) && c.type !== 'enumSet'))
const otherChoices = computed<Choice[]>(() => ((state.value.choices ?? []) as Choice[]).filter((c) => !mainChoices.value.includes(c)))
function setMain(c: Choice, value: string): void {
  const next = { ...picked.value }
  if (value) next[c.path] = value
  else delete next[c.path]
  picked.value = next
}
const replaceChoices = ref<Record<string, unknown>>({})
// The home page (⌂ on the step bar): a new plan, and the project's plans (the current one, the earlier ones).
const home = ref(false)
// How the plan's places are built (Project Settings ▸ Places ▸ method): the plan's, from the project's by default.
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
        <select :value="normalBuildMethod(state.buildMethod || state.projectBuildMethod)" @change="setBuildMethod(($event.target as HTMLSelectElement).value)">
          <option v-for="m in BUILD_METHODS" :key="m.id" :value="m.id" :title="m.title">{{ m.label }}</option>
        </select>
        <span class="help" :title="BUILD_METHODS.find((m) => m.id === normalBuildMethod(state.buildMethod || state.projectBuildMethod))?.title">?</span>
        <span class="muted">{{ (state.buildMethod || state.projectBuildMethod) === state.projectBuildMethod ? "the project's" : 'this plan only' }}</span>
      </label>
      <textarea v-model="request" rows="4" placeholder="A short dark-fantasy action RPG about a disgraced warden recovering a stolen amulet, three towns and a dungeon, about four hours" />
      <div v-if="mainChoices.length" class="main-choices">
        <label v-for="c in mainChoices" :key="c.path" class="main-choice">
          <span>{{ c.label }}</span>
          <select :value="(picked[c.path] as string) ?? ''" @change="setMain(c, ($event.target as HTMLSelectElement).value)">
            <option value="">Decided by the AI</option>
            <option v-for="o in c.options" :key="o" :value="o">{{ o }}</option>
          </select>
        </label>
      </div>
      <section v-if="otherChoices.length" class="choices">
        <button class="toggle" @click="choicesOpen = !choicesOpen">
          {{ choicesOpen ? '▾' : '▸' }} Other choices <span class="muted">{{ Object.keys(picked).length ? Object.keys(picked).length + ' chosen: kept as you chose them' : 'from the story ontology, sent with the request' }}</span>
        </button>
        <div v-if="choicesOpen" class="choice-rows">
          <div v-for="c in otherChoices" :key="c.path" class="choice-row">
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
          v-for="g in pageGroups"
          :key="g.id"
          :class="['step', g.status, { on: currentGroup?.id === g.id }]"
          :title="g.reachable ? `${g.label}: ${g.status}${g.total ? ` (${g.done}/${g.total})` : ''}` : `${g.label}: not reached yet`"
          :disabled="!g.reachable"
          @click="openGroup(g)"
        >
          {{ g.label }} <span class="mark">{{ statusMark(g.status) }}</span>
          <span v-if="g.failed" class="failed-count" :title="`${g.failed} failed: Retry in the stage`">{{ g.failed }} ✗</span>
          <span v-if="g.stale" class="stale">{{ g.stale }}</span>
          <span v-if="g.review" class="badge">review</span>
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
      <div v-if="view !== 'build' && (currentGroup?.pages.length ?? 0) > 1" class="subtabs">
        <button v-for="p in currentGroup!.pages" :key="p" :class="{ on: view === p }" :disabled="!stageOf(p) || !reachable(stageOf(p)!)"
                @click="view = p">
          {{ PAGE_LABELS[p] ?? p }} <span class="mark">{{ statusMark(stageOf(p)?.status ?? '') }}</span>
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
        <button v-if="view !== 'build' && (view === 'concept' || PAGE_DOCS[view]) && viewedStage && reachable(viewedStage)"
          :title="view === 'concept' ? 'Read the request again: the plan made anew' : `Make ${PAGE_DOCS[view]} again (locked documents kept)`"
          @click="regeneratePage(view)">Regenerate {{ PAGE_LABELS[view] ?? groupOf(view)?.label ?? view }}</button>
        <button v-if="viewedStage && reachable(viewedStage)" :title="`Make ${viewedStage.label} and every step after it again`"
          @click="restartFrom(viewedStage.id, viewedStage.label)">Restart from {{ viewedStage.label }}</button>
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
                  <template v-if="!isEditing('story')">
                    <p><b>{{ outline.story?.logline }}</b> <button class="link edit" @click="toggleEdit('story')">Edit</button></p>
                    <p class="muted">{{ outline.story?.synopsis }}</p>
                    <div v-for="a in outline.story?.acts ?? []" :key="a.id" class="card"><b>{{ a.id }}</b> {{ a.summary }}</div>
                  </template>
                  <div v-else class="form">
                    <label>Logline<input :value="outline.story?.logline" @change="editOutline('/story/logline', val($event))" /></label>
                    <label>Synopsis<textarea rows="4" :value="outline.story?.synopsis" @change="editOutline('/story/synopsis', val($event))"></textarea></label>
                    <div v-for="(a, i) in outline.story?.acts ?? []" :key="a.id" class="card row">
                      <b>{{ a.id }}</b>
                      <input class="wide" :value="a.summary" placeholder="what happens in it" @change="editOutline(`/story/acts/${i}/summary`, val($event))" />
                    </div>
                    <div><button @click="toggleEdit('story')">Done</button></div>
                  </div>
                </template>
              </section>
              <section class="sect">
                <h4 class="fold" @click="toggleFold('cast')">
                  <span class="caret">{{ folded.cast ? '▸' : '▾' }}</span> Cast <span class="muted">{{ (outline.cast ?? []).length }}</span>
                </h4>
                <div v-if="!folded.cast" class="cards">
                  <div v-for="(c, i) in outline.cast ?? []" :key="c.id" class="card">
                    <template v-if="!isEditing('cast:' + c.id)">
                      <b>{{ c.name }}</b> <span class="badge">{{ c.importance }}</span><span v-if="c.player" class="badge">player</span>
                      <button class="link edit" @click="toggleEdit('cast:' + c.id)">Edit</button>
                      <button class="link edit" title="Leave them out of the story" @click="deleteFrom('cast', i, c.name || c.id)">Delete</button>
                      <div class="muted">{{ (c.narrativeRoles ?? []).join(', ') }} {{ (c.gameplayRoles ?? []).join(', ') }}</div>
                      <div>{{ c.line }}</div>
                    </template>
                    <div v-else class="form">
                    <div class="row">
                      <input :value="c.name" placeholder="name" @change="editOutline(`/cast/${i}/name`, val($event))" />
                      <select :value="c.importance" @change="editOutline(`/cast/${i}/importance`, val($event))">
                        <option v-for="o in choices.importance" :key="o" :value="o">{{ o }}</option>
                      </select>
                      <label class="check"><input type="checkbox" :checked="!!c.player" @change="setPlayer(i, ($event.target as HTMLInputElement).checked)" /> player</label>
                      <button class="link" title="Leave them out of the story" @click="removeFrom('cast', i)">Remove</button>
                    </div>
                    <input class="wide" :value="c.line" placeholder="who they are, in a line" @change="editOutline(`/cast/${i}/line`, val($event))" />
                    <label class="row" title="How the plan and the game know them: changed everywhere (the story, quests, dialogue, the World Bible)">id character/
                      <input :value="c.id.replace(/^character\//, '')" @change="renameId(c.id, 'character/', val($event))" /></label>
                    <div class="row">
                      <input :value="(c.narrativeRoles ?? []).join(', ')" placeholder="story roles: mentor, antagonist" title="Their parts in the story, comma-separated"
                        @change="editOutline(`/cast/${i}/narrativeRoles`, list($event))" />
                      <input :value="(c.gameplayRoles ?? []).join(', ')" placeholder="gameplay roles: boss, vendor" title="Their parts in play, comma-separated"
                        @change="editOutline(`/cast/${i}/gameplayRoles`, list($event))" />
                    </div>
                    <div><button @click="toggleEdit('cast:' + c.id)">Done</button></div>
                    </div>
                  </div>
                  <button class="link" @click="addCast()">Add a character</button>
                </div>
              </section>
              <!-- An open world: its name, land, spacing and its places' size (the world is built as one place of sections) -->
              <section v-if="outline.world && Object.keys(outline.world).length" class="sect">
                <h4>World <span class="muted">{{ (outline.places ?? []).filter((p: any) => p.section).length }} sections</span></h4>
                <div v-if="!isEditing('world')" class="card">
                  <b>{{ outline.world.name || 'The world' }}</b> <span class="badge">open world</span>
                  <button class="link edit" @click="toggleEdit('world')">Edit</button>
                  <div class="muted">
                    {{ outline.world.landform || 'inland' }} · spacing {{ outline.world.spacing ?? 3 }} of 10 ·
                    places mostly {{ outline.world.settlementSize || 'small' }}
                  </div>
                </div>
                <div v-else class="card form row">
                  <input :value="outline.world.name" placeholder="its name" @change="editOutline('/world/name', val($event))" />
                  <select :value="outline.world.landform || 'inland'" @change="editOutline('/world/landform', val($event))">
                    <option v-for="o in choices.landforms" :key="o" :value="o">{{ o }}</option>
                  </select>
                  <label title="How far apart its places are: 1 close neighbours, 9 far apart">spacing
                    <input class="num" type="number" min="1" max="10" :value="outline.world.spacing ?? 3" @change="editOutline('/world/spacing', Number(val($event)))" /></label>
                  <label>places mostly
                    <select :value="outline.world.settlementSize || 'small'" @change="editOutline('/world/settlementSize', val($event))">
                      <option v-for="o in choices.sizes" :key="o" :value="o">{{ o }}</option>
                    </select></label>
                  <button @click="toggleEdit('world')">Done</button>
                </div>
              </section>
              <section class="sect">
                <h4 class="fold" @click="toggleFold('places')">
                  <span class="caret">{{ folded.places ? '▸' : '▾' }}</span> Places <span class="muted">{{ (outline.places ?? []).length }}</span>
                </h4>
                <div v-if="!folded.places" class="cards">
                  <div v-for="(p, i) in outline.places ?? []" :key="p.id" class="card">
                    <template v-if="!isEditing('place:' + p.id)">
                      <b>{{ p.name || p.id }}</b> <span class="badge">{{ p.kind }}</span>
                      <span v-if="p.section" class="badge" title="A section of the open world">section{{ p.size ? ' · ' + p.size : '' }}</span>
                      <span v-if="p.where?.to" class="muted"> {{ p.where.type || 'near' }} {{ p.where.to }}</span>
                      <span v-if="p.parent" class="muted"> in {{ p.parent }}</span>
                      <button class="link edit" @click="toggleEdit('place:' + p.id)">Edit</button>
                      <button class="link edit" title="Leave it out of the game" @click="deleteFrom('places', i, p.name || p.id)">Delete</button>
                      <div>{{ p.line }}</div>
                    </template>
                    <div v-else class="form">
                    <div class="row">
                      <input :value="p.name" placeholder="name" @change="editOutline(`/places/${i}/name`, val($event))" />
                      <select :value="p.kind" title="What kind of place it is (its build type)" @change="editOutline(`/places/${i}/kind`, val($event))">
                        <option v-if="!choices.kinds.includes(p.kind)" :value="p.kind">{{ p.kind }}</option>
                        <option v-for="o in choices.kinds" :key="o" :value="o">{{ o }}</option>
                      </select>
                      <select :value="p.parent ?? ''" title="The place it's inside (an interior in a town)" @change="editOutline(`/places/${i}/parent`, val($event))">
                        <option value="">inside nothing</option>
                        <option v-for="o in (outline.places ?? []).filter((x: any) => x.id !== p.id)" :key="o.id" :value="o.id">in {{ o.name || o.id }}</option>
                      </select>
                      <button class="link" title="Leave it out of the game" @click="removeFrom('places', i)">Remove</button>
                    </div>
                    <input class="wide" :value="p.line" placeholder="what it is, in a line" @change="editOutline(`/places/${i}/line`, val($event))" />
                    <label class="row" title="How the plan and the game know it: changed everywhere (the story, quests, the World Bible)">id location/
                      <input :value="p.id.replace(/^location\//, '')" @change="renameId(p.id, 'location/', val($event))" /></label>
                    <div v-if="outline.world && Object.keys(outline.world).length" class="row">
                      <label class="check" title="A section of the open world (an outdoor place joined to the others by roads)">
                        <input type="checkbox" :checked="!!p.section" @change="editOutline(`/places/${i}/section`, ($event.target as HTMLInputElement).checked)" /> section</label>
                      <select v-if="p.section" :value="p.size ?? ''" @change="editOutline(`/places/${i}/size`, val($event))">
                        <option value="">the world's size</option>
                        <option v-for="o in choices.sizes" :key="o" :value="o">{{ o }}</option>
                      </select>
                      <select v-if="p.section" :value="p.where?.type ?? ''" title="Where the world puts it"
                        @change="editOutline(`/places/${i}/where`, val($event) ? { type: val($event), to: p.where?.to || 'sea' } : null)">
                        <option value="">anywhere</option>
                        <option value="near">near</option>
                        <option value="by">by</option>
                      </select>
                      <input v-if="p.section && p.where?.type" :value="p.where?.to" placeholder="sea, lake, river, forest, mountains, a place"
                        @change="editOutline(`/places/${i}/where`, { type: p.where.type, to: val($event) })" />
                    </div>
                    <div><button @click="toggleEdit('place:' + p.id)">Done</button></div>
                    </div>
                  </div>
                  <button class="link" @click="addPlace()">Add a place</button>
                </div>
              </section>
              <section class="sect">
                <h4 class="fold" @click="toggleFold('connections')">
                  <span class="caret">{{ folded.connections ? '▸' : '▾' }}</span> Connections
                  <span class="muted">{{ (outline.connections ?? []).length }}</span>
                </h4>
                <div v-if="!folded.connections" class="cards">
                  <div v-for="(c, i) in outline.connections ?? []" :key="i" :class="isEditing('link:' + i) ? 'card form row' : 'muted'">
                    <template v-if="!isEditing('link:' + i)">
                      {{ c.from }} → {{ c.to }} ({{ c.kind }}){{ c.gate ? ' · opens with ' + c.gate : '' }}{{ c.gateway?.kind ? ` · gateway: ${c.gateway.kind}${c.gateway.at ? ', ' + c.gateway.at : ''}` : '' }}
                      <button class="link edit" @click="toggleEdit('link:' + i)">Edit</button>
                      <button class="link edit" @click="deleteFrom('connections', i, `the connection ${c.from} → ${c.to}`)">Delete</button>
                    </template>
                    <template v-else>
                    <select :value="c.from" @change="editOutline(`/connections/${i}/from`, val($event))">
                      <option v-for="o in outline.places ?? []" :key="o.id" :value="o.id">{{ o.name || o.id }}</option>
                    </select>
                    →
                    <select :value="c.to" @change="editOutline(`/connections/${i}/to`, val($event))">
                      <option v-for="o in outline.places ?? []" :key="o.id" :value="o.id">{{ o.name || o.id }}</option>
                    </select>
                    <select :value="c.kind" @change="editOutline(`/connections/${i}/kind`, val($event))">
                      <option v-if="!choices.connections.includes(c.kind)" :value="c.kind">{{ c.kind }}</option>
                      <option v-for="o in choices.connections" :key="o" :value="o">{{ o }}</option>
                    </select>
                    <select :value="c.gateway?.kind ?? ''" title="A way in of its own (a cave mouth, crypt stairs): placed in the world"
                      @change="editOutline(`/connections/${i}/gateway`, val($event) ? { kind: val($event), at: c.gateway?.at ?? '' } : null)">
                      <option value="">no gateway</option>
                      <option v-for="o in choices.gatewayKinds" :key="o" :value="o">{{ o.replace(/^gateway-/, '') }}</option>
                    </select>
                    <input v-if="c.gateway?.kind" :value="c.gateway?.at" placeholder="where, in words"
                      @change="editOutline(`/connections/${i}/gateway`, { kind: c.gateway.kind, at: val($event) })" />
                    <input :value="c.gate ?? ''" placeholder="opens with (optional)" @change="editOutline(`/connections/${i}/gate`, val($event))" />
                    <button class="link" @click="removeFrom('connections', i)">Remove</button>
                    <button @click="toggleEdit('link:' + i)">Done</button>
                    </template>
                  </div>
                  <button class="link" :disabled="(outline.places ?? []).length < 2" @click="addConnection()">Add a connection</button>
                </div>
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
                <td v-if="view === 'places'">
                  {{ r.kind }}
                  <span v-if="r.section" class="badge" title="A section of the open world">section{{ r.size ? ' · ' + r.size : '' }}</span>
                  <span v-if="r.gateway" class="badge" title="Entered through a gateway">{{ r.gateway }}</span>
                </td>
                <td class="wide">
                  {{ view === 'places' ? r.purpose : view === 'characters' ? r.line : r.strategy }}
                  <div v-if="view === 'characters' && r.routine" class="muted">Routine: {{ r.routine }}</div>
                  <div v-else-if="view === 'characters' && r.detailed && !r.player" class="warn">No routine</div>
                </td>
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
              <SystemsView :systems="state.systems" :gameplay="state.gameplay" :designs="state.systemDesigns" :behaviours="state.behaviours" :input="state.input"
                           @open-gameplay="open('gameplay')" @regenerate-input="sendToHost({ type: 'planner:regenerate', doc: 'input' })" />
            </div>
            <div v-else class="muted">Not yet.</div>
          </template>

          <template v-else-if="view === 'objects'">
            <ObjectsView :objects="state.objects" mode="design" @redesign="sendToHost({ type: 'planner:regenerate', doc: 'objects' })" />
          </template>
          <template v-else-if="view === 'cutscenes'">
            <CutscenesView :cutscenes="state.timeline?.cutscenes" :watched="watchedScenes" @redesign="sendToHost({ type: 'planner:regenerate', doc: 'timeline' })" />
          </template>
          <template v-else-if="view === 'smallscenes'">
            <SmallScenesView :scenes="state.smallscenes" mode="design" @redesign="sendToHost({ type: 'planner:regenerate', doc: 'smallscenes' })" />
          </template>
          <template v-else-if="view === 'timeline'">
            <div class="section">
              <div class="row">
                <span class="muted">Where each character is, the AI packages they run, how they behave and what they talk about, before the first
                  quest stage and as each stage changes it (— not changed): the story, the characters and the places read together. The build
                  implements it and checks it (⑩–⑬ Build ▸ Cast).</span>
                <span class="spacer"></span>
                <a v-if="state.timeline" href="#" @click.prevent="open('timeline')">open the document</a>
                <button @click="sendToHost({ type: 'planner:regenerate', doc: 'timeline' })">{{ state.timeline ? 'Regenerate' : 'Make the timeline' }}</button>
              </div>
              <p v-if="planCast.rows.length" class="muted small">
                When quests overlap (two hold a character at once), the higher-ranked one decides: the change's priority (story, then normal,
                then ambient), then the main quest, then the side quests in order. Packages, dialogue and cutscenes are all kept (packages
                stacked by rank, each while its quest is at its stages); where they are and how they behave is the higher-ranked quest's.
                ⇅ marks a cell where that applies (its detail says how).</p>
              <CastGrid v-if="planCast.rows.length" :quests="castQuests" :rows="planCast.rows" :cutscenes="planCast.cutscenes" />
              <p v-else class="muted">Not yet: it's made after ⑥ Places.</p>
            </div>
          </template>

          <template v-else-if="view === 'visuals'">
            <div v-if="state.ui" class="section">
              <h4 class="fold">The interface <a href="#" @click.prevent="open('ui')">open</a></h4>
              <p class="muted">
                Its one look, and every panel designed in full (layout, components, look, images, RCSS); Build ▸ Visuals makes them.
              </p>
              <VisualsView :ui="state.ui" mode="design" @redesign="sendToHost({ type: 'planner:regenerate', doc: 'ui' })" />
              <!-- In a reading order (the document's own keys come alphabetically), then anything else it has -->
              <DataTable :value="uiInOrder" />
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
              <div class="build-now">
                <template v-if="buildingRow">
                  <span class="busy">Building <b>{{ buildingRow.place }}</b></span>
                  <span v-if="buildingRow.step" class="muted"> — {{ stepName(buildingRow.step, buildingRow.planPhase, buildingRow) }}</span>
                </template>
                <span v-else class="muted">{{ state.build.message }}</span>
                <span class="muted"> · {{ buildDone }} of {{ (state.build.rows ?? []).length }} places done</span>
                <span class="spacer"></span>
                <button v-if="buildRetryable.length" class="retry" @click="retryFailedPlaces">Retry failed ({{ buildRetryable.length }})</button>
                <button v-if="state.build.status === 'working'" class="stop" @click="sendToHost({ type: 'planner:stopBuilds' })">Stop building</button>
                <button v-else-if="state.build.stage === 'done' && (state.build.features ?? []).some((f: any) => f.status === 'failed')" class="primary"
                  title="The gameplay steps that failed, tried again; then the playtest and the result again"
                  @click="sendToHost({ type: 'planner:continueBuilds' })">Retry failed gameplay steps ({{ (state.build.features ?? []).filter((f: any) => f.status === 'failed').length }})</button>
                <button v-else-if="state.build.stage !== 'done'" class="primary" title="Go on from the next place that isn't built; those that are stay; failed gameplay steps are tried again"
                  @click="sendToHost({ type: 'planner:continueBuilds' })">{{ state.build.status === 'waiting' && state.build.stage === 'build' ? 'Continue: connect the places and make the requirements' : 'Continue building' }}</button>
                <button title="Build every place again from the start, with the art setting as it is now" @click="restartBuilds">Restart builds</button>
              </div>
              <LibraryPicker v-if="libraryFor" :kind="libraryFor.kind"
                :title="`${libraryFor.kind === 'character' ? 'A character' : 'A picture'} from your library for ${libraryFor.name}`"
                @pick="useLibrary" @close="libraryFor = null" />
              <div v-if="stepLine" class="build-step">
                <b>{{ stepLine }}</b>
                <button v-if="buildStageTab && buildTab !== buildStageTab" class="link" @click="buildTab = buildStageTab as BuildTab">show it</button>
              </div>
              <div class="tabs build-tabs">
                <button v-for="t in buildTabs" :key="t.id" :class="['step-' + tabState(t.id), { on: buildTab === t.id }]"
                  :title="tabState(t.id) === 'now' ? 'The build is at this step' : tabState(t.id) === 'done' ? 'Built' : tabState(t.id) === 'next' ? 'Comes later' : ''"
                  @click="buildTab = t.id">
                  <span v-if="tabState(t.id) === 'done'" class="step-mark">✓</span><span v-else-if="tabState(t.id) === 'now'" class="step-mark">▶</span>
                  {{ t.label }}<span v-if="t.count" class="tab-count">{{ t.count }}</span></button>
              </div>
              <template v-if="buildTab === 'places'">
              <p v-if="paintCalls.places" class="muted">
                Painting (the places planned so far): {{ paintCalls.tiles }} tiles, {{ paintCalls.calls }} calls{{ paintCalls.done ? `, ${paintCalls.done} already painted` : '' }}.
                A place's density (px a cell) is set in its build, on ③ Plan: fewer px, fewer calls.
              </p>
              <table class="grid">
                <tbody>
                  <tr v-for="r in state.build.rows ?? []" :key="r.place" :class="{ 'building-now': r.status === 'building' }">
                    <td>
                      <b>{{ r.place }}</b> <span class="muted">{{ r.scene }}</span>
                      <div v-if="(r.sections ?? []).length" class="muted" title="An open world: these places are built in it, as its sections">
                        sections: {{ r.sections.join(', ') }}</div>
                    </td>
                    <td>{{ r.buildType }}</td>
                    <td>{{ r.mode }}</td>
                    <td :class="{ err: r.status === 'failed', warn: r.status === 'placeholder' || r.status === 'needs-you', busy: r.status === 'building' }">
                      {{ r.status === 'building' ? `building… ${r.step ? '(' + stepName(r.step, r.planPhase, r) + ')' : ''}` : r.status }}
                    </td>
                    <td class="wide muted">{{ r.error }}</td>
                    <td class="muted" :title="r.paint ? `Painted at ${r.paint.cellPx} px a cell (change it in the place's build, on ③ Plan)` : ''">
                      <template v-if="r.paint">{{ r.paint.painted ? 'painted' : 'paint' }}: {{ r.paint.tiles }} tiles · {{ r.paint.calls }} calls</template>
                    </td>
                    <td>
                      <button v-if="r.build" @click="sendToHost({ type: 'planner:buildRow', place: r.place, action: 'open' })">Open build</button>
                      <button v-if="r.awaitingApproval" class="primary" :title="r.paint ? `${r.paint.tiles} tiles, ${r.paint.calls} calls` : 'Paint it'"
                        @click="sendToHost({ type: 'planner:buildRow', place: r.place, action: 'approvePaint' })">Approve painting</button>
                      <button v-if="r.status === 'needs-you'" @click="sendToHost({ type: 'planner:buildRow', place: r.place, action: 'done' })">Built</button>
                      <button v-if="r.status === 'failed' || r.status === 'placeholder'" @click="sendToHost({ type: 'planner:buildRow', place: r.place, action: 'retry' })">Retry</button>
                      <button v-if="['built','needs-you','waiting','building','failed','placeholder'].includes(r.status)" title="Build this place again from the start, with the art setting as it is now"
                        @click="rebuildPlace(r.place)">Rebuild</button>
                      <button v-if="r.status === 'failed'" @click="sendToHost({ type: 'planner:buildRow', place: r.place, action: 'placeholder' })">Placeholder</button>
                    </td>
                  </tr>
                </tbody>
              </table>
              <!-- How the places find their art: the project's setting, shared with the Build panel -->
              <div class="art-strategy">
                <h4>Art for the places</h4>
                <div>
                  <b>{{ artSourceName(state.sourcing?.defaultSource ?? 'search') }}</b>
                  <span class="muted"> · web search {{ state.sourcing?.webSearch ? 'on' : 'off' }} · share-alike {{ state.sourcing?.shareAlike ? 'allowed' : 'not allowed' }}</span>
                </div>
                <p class="muted">
                  How the places find the art for their things, set in the Game Brief: ① Concept ▸ Plan ▸ Art for the places
                  <a href="#" @click.prevent="view = 'concept'; tab = 'plan'">change it</a>.
                  A change is used by the builds started after it (Restart builds).
                </p>
              </div>
              </template>
              <template v-if="buildTab === 'characters'">
              <p v-if="!characterArt.length" class="muted">The characters are drawn and animated once the requirements are made (⑪).</p>
              <template v-if="characterArt.length">
                <h4>⑪ Characters' art
                  <span class="muted">{{ characterArt.reduce((n: number, a: any) => n + (a.calls ?? 0), 0) }} calls</span>
                  <button v-if="characterArt.some((a: any) => a.status === 'failed' || a.status === 'queued')" class="draw-others"
                    title="Every drawing here that failed or was left part-way (a character not drawn, a clip not made) drawn again; what's drawn stays"
                    @click="sendToHost({ type: 'planner:buildRow', place: 'characters', action: 'drawMissing' })">✎ Draw what's missing ({{ characterArt.filter((a: any) => a.status === 'failed' || a.status === 'queued').length }})</button>
                  <button class="draw-others" title="Draw and animate every character not drawn yet (the supporting and minor ones): each drawn once, then its walk, and its attack, hurt and death if it fights"
                    @click="sendToHost({ type: 'planner:buildRow', place: '', action: 'drawOthers' })">✎ Draw the others too</button>
                </h4>
                <p v-if="state.build.stage === 'characters' && characterArt.some((a: any) => a.status === 'queued') && !state.build.artApproved" class="muted">
                  Each character is drawn once, then animated for what it does (walk, its attack, hurt, die).
                  <button class="primary" @click="sendToHost({ type: 'planner:buildRow', place: '', action: 'approveArt' })">Draw them</button>
                  <button @click="sendToHost({ type: 'planner:buildRow', place: '', action: 'skipArt' })">Skip (keep placeholders)</button>
                </p>
                <!-- What it's doing now: the step's line, and the art calls under way (each one's progress). -->
                <p v-if="characterArt.some((a: any) => a.status === 'drawing')" class="muted art-now">{{ state.build.message }}</p>
                <AiProgress :paths="['/characters/']" />
                <table class="grid">
                  <tbody>
                    <tr v-for="a in characterArt" :key="a.subject">
                      <td><b>{{ a.name }}</b> <span class="muted">{{ a.subject }}</span><span v-if="a.fromLibrary" class="badge">library</span></td>
                      <td :class="{ err: a.status === 'failed', busy: a.status === 'drawing' }">{{ a.doing || a.status }}</td>
                      <td class="muted">{{ a.done }} / {{ a.calls }}</td>
                      <td class="wide muted">{{ a.error }}</td>
                      <td><button v-if="a.status !== 'drawing'" class="link" title="Use a character from your library (its sheet and animations) instead of drawing it"
                        @click="fromLibrary(a)">From library…</button></td>
                    </tr>
                  </tbody>
                </table>
              </template>
              </template>
              <template v-if="buildTab === 'quests'">
              <p v-if="!(state.build.bindings?.aliases ?? []).length" class="muted">The quests' roles are found once the characters are placed (⑪).</p>
              <template v-if="(state.build.bindings?.aliases ?? []).length">
                <!-- Each quest's roles (its giver, places, spots, things, foes) matched to what's in the built game -->
                <h4>⑪ Quest roles in the game
                  <span class="muted">{{ state.build.bindings.aliases.filter((a: any) => a.how !== 'failed').length }} of {{ state.build.bindings.aliases.length }} found</span>
                </h4>
                <table class="grid">
                  <thead><tr><th>Quest</th><th>Role</th><th>Found as</th><th>Where</th></tr></thead>
                  <tbody>
                    <tr v-for="(a, i) in state.build.bindings.aliases" :key="i">
                      <td>{{ a.quest }}</td><td>{{ a.alias }}</td>
                      <td :class="{ err: a.how === 'failed' }" :title="a.how">{{ ({ fixed: 'the one named', 'story-anchor': 'its spot in the place', made: 'placed for it', query: 'whoever fits', failed: 'not found' } as Record<string, string>)[a.how] ?? a.how }}</td>
                      <td class="wide muted">{{ a.scene }} {{ a.marker || a.entity }} {{ a.reason }}</td>
                    </tr>
                  </tbody>
                </table>
              </template>
              </template>
              <!-- Requirements (34.1): what the plan says must be true in the game, each with how to tell it's done -->
              <template v-if="buildTab === 'requirements'">
                <div class="row">
                  <button class="primary" :disabled="['', 'build'].includes(state.build.stage ?? '') || reqs.status === 'making'"
                    title="The plan's requirements: by rule from the scenes, quests, cast and systems; then the AI reads each scene's events and beats for the rest. Made while the build is stopped, only they are made: Continue building goes on after"
                    @click="sendToHost({ type: 'planner:buildRow', place: '', action: 'makeRequirements' })">
                    {{ (reqs.items ?? []).length ? 'Make them again' : 'Make the requirements' }}</button>
                  <button v-if="(reqs.items ?? []).length" :disabled="reqs.status === 'making'"
                    title="How each requirement is built (34.2): by rule from its kind; the AI plans the ones it read from the scenes. The requirements are kept."
                    @click="sendToHost({ type: 'planner:buildRow', place: '', action: 'planImplementations' })">Plan the implementations</button>
                  <button v-if="(reqs.implementations ?? []).length && ['done', 'finish', 'playtest', 'result', 'features'].includes(state.build.stage ?? '')"
                    :disabled="reqs.status === 'making' || (reqs.implementing && state.build.status === 'working')"
                    title="Build what isn't built yet (⑫ Implement): the items not started, those that failed, and those a stop left half-way; built ones stay"
                    @click="sendToHost({ type: 'planner:buildRow', place: '', action: 'implementNow' })">
                    {{ (reqs.implementations ?? []).some((i: any) => i.status === 'built') ? 'Build what’s left' : 'Build them' }}</button>
                  <button v-if="(reqs.items ?? []).length" :disabled="reqs.status === 'making'"
                    title="Check every requirement: against the files, and against what the scenes' tests recorded (Story & Test ▸ Test)"
                    @click="sendToHost({ type: 'planner:buildRow', place: '', action: 'checkRequirements' })">Check</button>
                  <button v-if="reqs.summary?.failed" :disabled="reqs.implementing"
                    title="Build each failed requirement's items again, told what failed"
                    @click="sendToHost({ type: 'planner:buildRow', place: '*', action: 'fixRequirement' })">Fix the failed ({{ reqs.summary.failed }})</button>
                  <span v-if="reqs.status === 'making'" class="busy">{{ state.build.message }}<template v-if="reqs.phase === 'scenes'"> ({{ reqScenesDone }} of {{ (reqs.scenes ?? []).length }} scenes)</template></span>
                  <span class="spacer"></span>
                  <div class="tabs">
                    <button :class="{ on: reqView === 'scenes' }" @click="reqView = 'scenes'">By scene</button>
                    <button :class="{ on: reqView === 'assets' }" @click="reqView = 'assets'">Assets<span v-if="reqAssets.length" class="tab-count">{{ reqAssets.length }}</span></button>
                  </div>
                </div>
                <div v-if="reqs.artWaiting" class="row warn">
                  <span>⑫ {{ reqs.artWaiting }} assets and animations wait to be drawn (paid AI calls).</span>
                  <button class="primary" @click="sendToHost({ type: 'planner:buildRow', place: '', action: 'approveAssetArt' })">Draw them</button>
                  <button @click="sendToHost({ type: 'planner:buildRow', place: '', action: 'skipAssetArt' })">Skip</button>
                </div>
                <p v-if="reqs.summary || implProgress.total" class="muted">
                  <template v-if="reqs.summary">{{ reqs.summary.verified ?? 0 }} verified · {{ reqs.summary.built ?? 0 }} built (to test) ·
                    {{ reqs.summary.failed ?? 0 }} failed · {{ reqs.summary.todo ?? 0 }} to do</template>
                  <template v-if="implProgress.total"> — items: {{ implProgress.built }} of {{ implProgress.total }} built<template v-if="implProgress.building">,
                    {{ implProgress.building }} being built</template><template v-if="implProgress.failed">, {{ implProgress.failed }} failed</template></template>
                  <span v-if="reqs.implementing" class="busy"> {{ state.build.message }}</span>
                </p>
                <div v-if="reqView === 'scenes' && (reqs.items ?? []).length" class="row">
                  <label title="Each requirement's implementation items: how it's built, by whom"><input v-model="reqShowImpl" type="checkbox" /> implementations</label>
                  <span class="spacer"></span>
                  <select v-model="reqStatus" title="Only those">
                    <option value="">every status</option>
                    <option v-for="st in ['todo', 'built', 'verified', 'failed']" :key="st" :value="st">{{ st }}</option>
                  </select>
                  <select v-model="reqKind" title="Only this kind">
                    <option value="">every kind</option>
                    <option v-for="k in reqKinds" :key="k" :value="k">{{ k }}</option>
                  </select>
                  <select v-model="reqBy" title="Made by rule from the plan, or read from the scenes by the AI">
                    <option value="">by rule and from the scenes</option>
                    <option value="rule">by rule</option>
                    <option value="ai">from the scenes (AI)</option>
                  </select>
                </div>
                <p v-if="!(reqs.items ?? []).length" class="muted">
                  The plan (the story's scenes, the quests, the cast, the places, the systems) is the game's requirements. Each becomes a
                  sentence that must be true in the game, with when, where, who and how to tell it's done. They're built in ⑫ and checked in Validate.
                </p>
                <div v-for="(s, i) in (reqs.scenes ?? []).filter((s: any) => s.status === 'failed')" :key="'rf' + i" class="err">
                  ✗ {{ s.scene }}: the AI couldn't read it ({{ s.error }})</div>
                <div v-if="reqs.assetsError" class="err">✗ The assets weren't merged ({{ reqs.assetsError }}): they're as the plan named them</div>
                <div v-for="(s, i) in (reqs.implScenes ?? []).filter((s: any) => s.status === 'failed')" :key="'if' + i" class="err">
                  ✗ {{ s.scene }}: the AI couldn't plan its implementations ({{ s.error }})</div>
                <!-- The visual requirements: each separate asset the game needs, how it looks; what's painted with its place -->
                <table v-if="reqView === 'assets'" class="grid reqs">
                  <thead><tr><th>Asset</th><th>Look</th><th>Where</th><th></th></tr></thead>
                  <tbody>
                    <tr v-for="r in reqAssets" :key="r.id">
                      <td><b>{{ r.asset?.name }}</b>
                        <div class="muted">{{ r.asset?.type }}{{ r.asset?.size ? ' · ' + r.asset.size : '' }}{{ r.asset?.separate === false ? ' · painted with its place' : '' }}</div></td>
                      <td class="wide">{{ r.asset?.look || '—' }}
                        <div v-if="(r.asset?.states ?? []).filter((x: string) => x).length" class="muted">states: {{ r.asset.states.filter((x: string) => x).join(', ') }}</div>
                        <div v-for="an in animationsOf(r)" :key="an.id" class="impl">
                          ▶ animated {{ an.animation?.action }}
                          <span :class="{ ok: an.status === 'verified' || an.status === 'built', err: an.status === 'failed', muted: an.status === 'todo' }">· {{ an.status }}</span>
                          <span v-if="an.why" class="err"> {{ an.why }}</span></div>
                        <div v-if="r.why" class="err">✗ {{ r.why }}</div></td>
                      <td class="muted">{{ placesText(r.asset?.places) }}<div>{{ (r.scenes ?? [r.scene]).join(', ') }}</div></td>
                      <td :class="{ ok: r.status === 'verified' || r.status === 'built', err: r.status === 'failed', muted: r.status === 'todo' }">{{ r.status }}</td>
                    </tr>
                  </tbody>
                </table>
                <template v-for="g in reqView === 'scenes' ? reqGroups : []" :key="g.scene">
                  <h4 class="fold" @click="reqFolded[g.scene] = !reqFolded[g.scene]">
                    {{ reqFolded[g.scene] ? '▸' : '▾' }} {{ g.scene || 'The systems (⑦)' }}
                    <span class="muted">{{ g.act ? g.act + ' · ' : '' }}{{ g.goal }} · {{ g.items.length }}</span></h4>
                  <table v-if="!reqFolded[g.scene]" class="grid reqs">
                    <tbody>
                      <tr v-for="r in g.items" :key="r.id">
                        <td><span class="badge">{{ r.kind }}</span></td>
                        <td class="wide">{{ r.text }}
                          <div class="muted">{{ whenText(r.when) }}{{ r.where?.marker ? ' · at ' + r.where.marker : '' }}{{ r.where?.from ? ' · from ' + r.where.from : '' }}</div>
                          <div v-if="!(r.checks ?? []).length" class="muted checks" title="How to tell it's done">✓ {{ checksText(r.accept) }}</div>
                          <div v-else class="muted checks" title="How to tell it's done: ✓ holds, ✗ doesn't, ? a played test tells">
                            <span v-for="(c, k) in r.checks" :key="k" :class="{ ok: c.ok === true, err: c.ok === false }">{{ checkMark(c) }}
                              {{ checksText([r.accept?.[k] ?? c]) }}{{ c.played ? ' (played)' : '' }} · </span></div>
                          <div v-if="r.why" class="err">✗ {{ r.why }}</div>
                          <template v-if="reqShowImpl">
                            <div v-for="i in implOf.get(r.id) ?? []" :key="i.id" class="impl" :title="'by ' + i.by + (i.after?.length ? '; after ' + i.after.join(', ') : '')">
                              ↳ <span class="badge">{{ i.how }}</span> {{ i.does }} <span class="muted">· {{ i.by }} ·</span>
                              <span :class="{ ok: i.status === 'built', err: i.status === 'failed', busy: i.status === 'building' }">{{ i.status }}</span>
                              <span v-if="i.error" class="err"> {{ i.error }}</span><span v-else-if="i.note" class="muted"> ({{ i.note }})</span></div>
                          </template></td>
                        <td class="muted" :title="r.by === 'ai' ? 'Read from the scene by the AI' : 'Made by rule from the plan'">{{ r.by === 'ai' ? 'AI' : 'rule' }}</td>
                        <td :class="{ ok: r.status === 'verified' || r.status === 'built', err: r.status === 'failed', muted: r.status === 'todo' }">{{ r.status }}
                          <button v-if="r.status === 'failed'" :disabled="reqs.implementing" title="Build its items again, told what failed"
                            @click="sendToHost({ type: 'planner:buildRow', place: r.id, action: 'fixRequirement' })">Fix</button></td>
                      </tr>
                    </tbody>
                  </table>
                </template>
              </template>
              <!-- Cast (character validation): each character's state at the start and as each stage changes it, checked -->
              <template v-if="buildTab === 'cast'">
                <div class="row">
                  <span class="muted">The cast timeline (⑥b) as built: each cell is checked against the files (the quests' stage effects, the
                    packages, the dialogue) and against what the scenes' tests saw as each stage began (Story &amp; Test ▸ Test). A failed cell's
                    Fix has the AI decide how to make it so: a stage effect, an AI package, dialogue, or a script.</span>
                  <span class="spacer"></span>
                  <button :disabled="!(reqs.items ?? []).length" @click="sendToHost({ type: 'planner:buildRow', place: '', action: 'checkRequirements' })">Check</button>
                  <button v-if="buildCast.rows.some((r) => Object.values(r.cells).some((c) => c.status === 'failed'))" :disabled="reqs.implementing"
                    @click="sendToHost({ type: 'planner:buildRow', place: '*', action: 'fixRequirement' })">Fix the failed</button>
                </div>
                <CastGrid v-if="buildCast.rows.length" :quests="castQuests" :rows="buildCast.rows" :cutscenes="buildCast.cutscenes" :fixing="reqs.implementing"
                  @fix="(id) => sendToHost({ type: 'planner:buildRow', place: id, action: 'fixRequirement' })" />
                <p v-else class="muted">
                  {{ state.timeline ? 'Make the requirements (Requirements tab): the timeline\'s states become checks here.'
                    : 'The plan has no cast timeline yet: make it in ⑥b Cast timeline, then make the requirements.' }}</p>
              </template>
              <!-- Placements (34.11): who and what lives where, and where each is over the story; their whereabouts
                   requirements are what Implement makes true -->
              <CharacterPlacements v-show="buildTab === 'placements'" :active="buildTab === 'placements'" />
              <!-- ⑫ The gameplay set up: each feature, its steps (the AI panel's tasks), how far along -->
              <template v-if="buildTab === 'objects'">
                <ObjectsView :objects="state.objects" :built="state.build.world" mode="build"
                  @redesign="sendToHost({ type: 'planner:regenerate', doc: 'objects' })" @build="(id) => sendToHost({ type: 'build:objects', id })" />
                <!-- The art that isn't a character's: the items' icons, the interface's panel, the places' doors and ways through -->
                <p v-if="['objects', 'visuals'].includes(state.build.stage ?? '') && (state.build.message ?? '').includes('approve, or skip')" class="row warn">
                  <span>{{ state.build.message }}</span>
                  <button class="primary" @click="sendToHost({ type: 'planner:buildRow', place: '', action: 'approveArt' })">Draw them</button>
                  <button @click="sendToHost({ type: 'planner:buildRow', place: '', action: 'skipArt' })">Skip (keep placeholders)</button>
                </p>
                <template v-if="objectArt.length">
                  <h4>Items and ways through: their art
                    <button v-if="objectArt.some((a: any) => a.status === 'failed' || a.status === 'queued')" class="draw-others"
                      title="Every drawing here that failed or was left part-way drawn again; what's drawn stays"
                      @click="sendToHost({ type: 'planner:buildRow', place: 'objects', action: 'drawMissing' })">✎ Draw what's missing ({{ objectArt.filter((a: any) => a.status === 'failed' || a.status === 'queued').length }})</button>
                  </h4>
                  <p v-if="objectArt.some((a: any) => a.status === 'drawing')" class="muted art-now">{{ state.build.message }}</p>
                  <AiProgress :paths="['/assets/generate', '/places/find-entrances', '/places/find-way-out']" />
                  <table class="grid">
                    <tbody>
                      <tr v-for="a in objectArt" :key="a.subject">
                        <td><b>{{ a.name }}</b> <span class="muted">{{ a.subject }}</span><span v-if="a.fromLibrary" class="badge">library</span></td>
                        <td :class="{ err: a.status === 'failed', busy: a.status === 'drawing' }">{{ a.doing || a.status }}</td>
                        <td class="wide muted">{{ a.error }}</td>
                        <td><button v-if="a.status !== 'drawing' && canUseLibrary(a)" class="link" title="Use a picture from your library instead of drawing it"
                          @click="fromLibrary(a)">From library…</button></td>
                      </tr>
                    </tbody>
                  </table>
                </template>
              </template>
              <template v-if="buildTab === 'smallscenes'">
                <h4>Cutscenes
                  <span v-if="cutsceneItems.length" class="muted">{{ cutsceneItems.filter((i: any) => i.status === 'built').length }} of {{ cutsceneItems.length }} items built</span>
                  <button v-if="cutsceneItems.length" class="draw-others" :disabled="reqs.implementing && state.build.status === 'working'"
                    title="Every cutscene built now: its tracks written from its beats, its cast at their marks, and the trigger that plays it (only these; a stopped build stops again after)"
                    @click="sendToHost({ type: 'planner:buildRow', place: 'scenes', kind: 'cutscene', action: 'buildItems' })">Build all</button>
                </h4>
                <p v-if="!cutsceneItems.length" class="muted">Make the requirements first: each cutscene's items (its tracks, its trigger) come from them.</p>
                <CutscenesView :cutscenes="state.timeline?.cutscenes" :watched="watchedScenes" :built="cutscenesBuilt" @redesign="sendToHost({ type: 'planner:regenerate', doc: 'timeline' })" />
                <h4>Small scenes</h4>
                <SmallScenesView :scenes="state.smallscenes" :built="state.build.world" mode="build"
                  @redesign="sendToHost({ type: 'planner:regenerate', doc: 'smallscenes' })" @build="(id) => sendToHost({ type: 'build:smallscenes', id })" />
              </template>
              <template v-if="buildTab === 'visuals'">
                <VisualsView :ui="state.ui" :built="state.build.visuals" mode="build"
                  @redesign="sendToHost({ type: 'planner:regenerate', doc: 'ui' })" @build="(panel) => sendToHost({ type: 'build:visuals', panel })" />
                <template v-if="interfaceArt.length">
                  <h4>The interface's art</h4>
                  <table class="grid">
                    <tbody>
                      <tr v-for="a in interfaceArt" :key="a.subject">
                        <td><b>{{ a.name }}</b> <span class="muted">{{ a.subject }}</span></td>
                        <td :class="{ err: a.status === 'failed', busy: a.status === 'drawing' }">{{ a.doing || a.status }}</td>
                        <td class="wide muted">{{ a.error }}</td>
                      </tr>
                    </tbody>
                  </table>
                </template>
              </template>
              <template v-if="buildTab === 'gameplay'">
              <p v-if="!(state.build.features ?? []).length && !state.build.playtest?.quests && !tabItems.length" class="muted">
                The gameplay systems' scripts (⑦'s steps) are built here, by the AI panel, once the scenes are.</p>
              </template>
              <template v-if="['characters', 'objects', 'quests', 'placements', 'cast', 'smallscenes', 'gameplay', 'visuals'].includes(buildTab)">
              <template v-if="tabTasks.length">
                <h4>The AI panel's tasks
                  <span class="muted">{{ tabTasks.filter((f: any) => f.status === 'done').length }} of {{ tabTasks.length }} done</span>
                </h4>
                <p v-if="tabTasks.some((f: any) => f.status === 'running')" class="muted">{{ state.build.message }} Each is a task for the AI panel; its work shows there.</p>
                <table class="grid">
                  <tbody>
                    <tr v-for="(f, i) in tabTasks" :key="i" :class="{ 'building-now': f.status === 'running' }">
                      <td><b>{{ f.name }}</b> <span v-if="f.place" class="muted">{{ f.place }}</span></td>
                      <td :class="{ err: f.status === 'failed', busy: f.status === 'running', ok: f.status === 'done' }">{{ f.status }}</td>
                      <td class="wide muted" :title="f.request">{{ f.error || f.request }}</td>
                    </tr>
                  </tbody>
                </table>
              </template>
              <template v-if="buildTab === 'gameplay' && state.build.playtest?.quests">
                <h4>⑫ Playtest — {{ state.build.playtest.summary?.passed }}/{{ state.build.playtest.summary?.total }} checks</h4>
                <div v-for="q in state.build.playtest.quests" :key="q.id">
                  <b>{{ q.id }}</b>: <span :class="{ err: q.result !== 'completed' }">{{ q.result }}</span> <span class="muted">{{ q.reason }}</span>
                </div>
                <div v-for="(c, i) in (state.build.playtest.checks ?? []).filter((c: any) => !c.ok)" :key="'c' + i" class="err">✗ {{ c.check }} {{ c.detail }}</div>
              </template>
              </template>
              <!-- Each tab's own implementation items (what its requirements need built), how far along -->
              <template v-if="tabItems.length && ['characters', 'objects', 'quests', 'placements', 'cast', 'smallscenes', 'gameplay', 'visuals'].includes(buildTab)">
                <h4>Built here, to the requirements <span class="muted">{{ tabItems.filter((i: any) => i.status === 'built').length }} of {{ tabItems.length }} items built</span></h4>
                <p v-if="reqs.implementing && (reqs.tab === buildTab || !reqs.tab)" class="muted">{{ state.build.message }}</p>
                <div v-if="reqs.artWaiting" class="row warn">
                  <span>{{ reqs.artWaiting }} assets and animations wait to be drawn (paid AI calls).</span>
                  <button class="primary" @click="sendToHost({ type: 'planner:buildRow', place: '', action: 'approveAssetArt' })">Draw them</button>
                  <button @click="sendToHost({ type: 'planner:buildRow', place: '', action: 'skipAssetArt' })">Skip</button>
                </div>
                <table class="grid">
                  <tbody>
                    <tr v-for="g in implByHow" :key="g.how">
                      <td><span class="badge">{{ g.how }}</span></td>
                      <td :class="{ ok: g.built === g.items.length, busy: g.building > 0 }">{{ g.built }}/{{ g.items.length }}<template v-if="g.building"> ({{ g.building }} building)</template></td>
                      <td class="wide muted">
                        <div v-for="f in g.failed.slice(0, 4)" :key="f.id" class="err">✗ {{ f.does }}: {{ f.error }}</div>
                        <template v-if="g.failed.length > 4">and {{ g.failed.length - 4 }} more (Requirements ▸ failed)</template></td>
                    </tr>
                  </tbody>
                </table>
              </template>
              <p v-if="!state.build.result && ['validate', 'story', 'result'].includes(buildTab)" class="muted">Once the game is built (⑬).</p>
              <template v-if="state.build.result && buildTab === 'validate'">
                <div class="row">
                  <button class="primary" :disabled="state.build.stage !== 'done' || check?.status === 'checking'"
                    title="Each story scene against what was built for it: rules, then the AI, scene by scene"
                    @click="sendToHost({ type: 'planner:buildRow', place: '', action: 'checkBuild' })">
                    {{ checkFindings.length ? 'Validate again' : 'Validate the build' }}</button>
                  <button v-if="checkFixable.length" :disabled="state.build.stage !== 'done' || check?.status === 'checking'"
                    title="Every finding with a fix: the plan's edits into the game's quests; the rest as tasks for the AI panel; then the playtest again"
                    @click="fixFindings(checkFixable.map((f: any) => f.id))">Fix all ({{ checkFixable.length }})</button>
                  <span v-if="check?.status === 'checking'" class="busy">{{ state.build.message }}
                    ({{ (check.scenes ?? []).filter((s: any) => s.status === 'done' || s.status === 'failed').length }} of {{ (check.scenes ?? []).length }} scenes)</span>
                  <span v-else-if="check?.status === 'fixed'" class="muted">Fixed: Validate again to see what's left.</span>
                </div>
                <p v-if="!check?.status" class="muted">
                  Compares what was built (⑩–⑫) with the plan's documents: each story scene's cast, quest stages, roles, scripts and cutscene.
                  What's missing or wrong is listed; most can be fixed here.
                </p>
                <div v-for="(s, i) in (check?.scenes ?? []).filter((s: any) => s.status === 'failed')" :key="'f' + i" class="err">
                  ✗ {{ s.scene }}: the AI couldn't look at it ({{ s.error }})</div>
                <template v-for="g in findingsByScene" :key="g.scene">
                  <h4>{{ g.scene || 'The whole game' }} <span class="muted">{{ g.goal }}</span></h4>
                  <table class="grid">
                    <tbody>
                      <tr v-for="f in g.findings" :key="f.id">
                        <td :class="{ err: f.severity === 'error', warn: f.severity !== 'error' }">{{ f.severity }}</td>
                        <td class="wide"><b>{{ f.what }}</b><div class="muted">{{ f.detail }}</div>
                          <div class="muted fix-how">{{ fixText(f) }}</div></td>
                        <td class="muted">{{ f.source === 'ai' ? 'AI' : 'rule' }}</td>
                        <td :class="{ ok: f.status === 'fixed', err: f.status === 'failed', busy: f.status === 'fixing' }">
                          <button v-if="f.status === 'open' && fixKinds.includes(f.fix?.how)"
                            :disabled="state.build.stage !== 'done' || check?.status === 'checking'" @click="fixFindings([f.id])">Fix</button>
                          <template v-else>{{ f.status }}<template v-if="f.error">: {{ f.error }}</template></template>
                        </td>
                      </tr>
                    </tbody>
                  </table>
                </template>
                <p v-if="check?.status === 'done' && !checkFindings.length" class="ok">Every scene is built as the story has it.</p>
              </template>
              <template v-if="state.build.result && buildTab === 'story'">
                <div class="row">
                  <span class="muted">Each scene of the story (③) and what was built for it: its quest stages and the roles they need, its cast, scripts and packages.
                    ▶ plays the game from a scene; Test has an autopilot play it and the AI judge it.</span>
                  <span class="spacer"></span>
                  <button :disabled="state.build.stage !== 'done' || !(check?.trace ?? []).length"
                    title="Each written scene tested in turn: played by the autopilot, judged by the AI; what didn't happen goes to Validate"
                    @click="testEveryScene">Test every scene</button>
                </div>
                <div v-if="!(check?.trace ?? []).length" class="muted">Validate the build to trace its scenes.</div>
                <template v-for="act in traceByAct" :key="act.act">
                  <h4>{{ act.act }}</h4>
                  <div v-for="t in act.scenes" :key="t.scene" class="trace-scene">
                    <div class="row trace-head" @click="openScene = openScene === t.scene ? '' : t.scene">
                      <span>{{ openScene === t.scene ? '▾' : '▸' }}</span>
                      <b>{{ t.scene }}</b> <span>{{ t.goal }}</span>
                      <span class="muted">{{ t.mode }} · {{ t.locationScene || t.location }}</span>
                      <span class="spacer"></span>
                      <span v-if="findingsOf(t.scene).length" :class="findingsOf(t.scene).some((f: any) => f.severity === 'error') ? 'err' : 'warn'">
                        {{ findingsOf(t.scene).length }} to fix</span>
                      <span v-if="testOf(t.scene)" :class="testClass(t.scene)">{{ testLabel(t.scene) }}</span>
                      <button :disabled="state.build.stage !== 'done'" title="Play the game from here: you start where this stage begins (beside whoever set it), each quest at its stage and its journal on"
                        @click.stop="sendToHost({ type: 'planner:buildRow', place: t.scene, action: 'playScene' }); toViewport()">▶ Play from here</button>
                      <button :disabled="state.build.stage !== 'done' || ['queued', 'running', 'judging', 'watching'].includes(testOf(t.scene)?.status)"
                        title="An autopilot plays it from where this stage begins, in Play, where you watch it (walking through the gateways to each objective, doing it); then the AI judges what happened against the scene. Stop Play to stop it."
                        @click.stop="openScene = t.scene; sendToHost({ type: 'planner:buildRow', place: t.scene, action: 'watchScene' }); toViewport()">Test</button>
                    </div>
                    <div v-if="openScene === t.scene" class="trace-body">
                      <template v-if="testOf(t.scene)?.status === 'done' || testOf(t.scene)?.status === 'failed'">
                        <h5>Test</h5>
                        <div :class="testClass(t.scene)"><b>{{ testLabel(t.scene) }}</b> {{ testOf(t.scene)?.summary || testOf(t.scene)?.error }}</div>
                        <div v-for="(s, i) in testOf(t.scene)?.log?.steps ?? []" :key="'s' + i" class="muted">· {{ s }}</div>
                        <div v-for="(l, i) in (testOf(t.scene)?.log?.dialogue ?? []).slice(0, 12)" :key="'d' + i" class="muted">“{{ l.speaker }}: {{ l.text }}”</div>
                        <div v-for="(e, i) in testOf(t.scene)?.log?.errors ?? []" :key="'e' + i" class="err">✗ {{ e.script }}: {{ firstLine(e.message) }}</div>
                        <div v-if="findingsOf(t.scene).some((f: any) => f.source === 'test')" class="muted">What didn't happen is in Validate, with its fixes.</div>
                      </template>
                      <div v-for="(b, i) in t.beats" :key="'b' + i"><span class="muted">{{ b.function }}</span> {{ b.text }}</div>
                      <h5>Quest stages</h5>
                      <div v-if="!(t.stages ?? []).length" class="muted">None advances it.</div>
                      <div v-for="st in t.stages" :key="st.quest + st.index">
                        <b>{{ st.quest }} stage {{ st.index }}</b> {{ st.journal }}
                        <div v-for="(o, i) in st.objectives" :key="i" class="muted">
                          · {{ o.action }} {{ o.target }} → <span :class="{ err: !o.binding || o.binding.how === 'failed' }">{{ bindingText(o.binding) }}</span></div>
                        <div v-for="(op, i) in st.onEnter" :key="'op' + i" class="muted">· as it begins: {{ op.op }} {{ op.who || op.class || op.item }} {{ op.to || op.at || '' }}</div>
                        <div v-if="st.game" class="muted">In the game: {{ (st.game.objectives ?? []).map((g: any) => g.text || g.type + ' ' + g.target).join('; ') || 'no objectives' }}</div>
                      </div>
                      <h5>Cast</h5>
                      <div v-for="c in t.cast" :key="c.who">
                        <b>{{ c.name }}</b> <span class="muted">{{ c.role }}{{ c.player ? ' · the player' : '' }}{{ c.hostile ? ' · fights' : '' }}</span>
                        <span v-if="!c.player" :class="{ err: !c.prefab }"> {{ c.prefab || 'no prefab' }}</span>
                        <span v-if="!c.player" class="muted"> · lives in {{ c.homeScene || c.home }}{{ c.inScene ? ', a copy here' : '' }}</span>
                      </div>
                      <h5>Scripts</h5>
                      <div v-if="!(t.scripts ?? []).length" class="muted">None.</div>
                      <div v-for="s in t.scripts" :key="s.path"><span :class="{ err: !s.written }">{{ s.path }}</span>
                        <span class="muted"> {{ s.attached || ('asked by ' + s.feature) }}{{ s.written ? '' : ' · missing' }}</span></div>
                      <h5>Packages, designs, cutscene</h5>
                      <div class="muted">{{ (t.packages ?? []).map((p: string) => p.replace('records/package/', '').replace('.json', '')).join(', ') || 'no packages' }}</div>
                      <div class="muted">{{ (t.features ?? []).map((f: any) => f.id).join(', ') || 'no gameplay design names it' }}</div>
                      <div v-if="t.cutscene" class="muted">{{ t.cutscene }}</div>
                    </div>
                  </div>
                </template>
              </template>
              <template v-if="state.build.result && buildTab === 'result'">
                <p><b>{{ state.build.result.headline }}</b></p>
                <table class="grid">
                  <tbody>
                    <tr v-for="(a, i) in state.build.result.askedVsMade" :key="i"><td>{{ a.what }}</td><td>asked {{ a.asked }}</td><td>made {{ a.made }}</td></tr>
                  </tbody>
                </table>
                <template v-if="state.build.result.features?.length">
                  <h4>Gameplay set up</h4>
                  <table class="grid">
                    <tbody>
                      <tr v-for="(f, i) in state.build.result.features" :key="i">
                        <td>{{ f.name }}</td><td>{{ f.place || 'the whole project' }}</td>
                        <td :class="{ err: f.status === 'failed' }">{{ f.status }}<template v-if="f.error">: {{ f.error }}</template></td>
                      </tr>
                    </tbody>
                  </table>
                </template>
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
          <ReviseBox v-if="reviseKey" :key="reviseKey" :step="reviseKey" :label="reviseLabel" :entries="reviseEntries" @revise="sendRevise" @revert="sendRevert" @remove="sendRemove" />
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
.build-tabs {
  flex-wrap: wrap;
}
.build-step {
  display: flex;
  align-items: center;
  gap: 8px;
  margin: 6px 0 2px;
}
.build-step .link {
  background: none;
  border: none;
  color: #89b4fa;
  cursor: pointer;
  padding: 0;
}
.build-tabs .step-mark {
  margin-right: 4px;
  font-size: 0.85em;
}
.build-tabs .step-done .step-mark {
  color: #a6e3a1;
}
.build-tabs .step-now {
  border-bottom: 2px solid #f9e2af;
}
.build-tabs .step-now .step-mark {
  color: #f9e2af;
}
.build-tabs .step-next {
  opacity: 0.7;
}
.tab-count {
  margin-left: 4px;
  opacity: 0.65;
  font-size: 0.9em;
}
.reqs td.wide .checks {
  font-size: 0.9em;
}
.reqs .impl {
  margin-top: 2px;
  font-size: 0.9em;
}
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
.main-choices {
  display: flex;
  flex-wrap: wrap;
  gap: 6px 14px;
  margin: 6px 0;
}
.main-choice {
  display: flex;
  flex-direction: column;
  gap: 2px;
  font-size: 11px;
}
.main-choice select {
  min-width: 140px;
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
.subtabs {
  display: flex;
  gap: 0;
  padding: 4px 8px 0;
  border-bottom: 1px solid #2a2a3a;
  background: #161622;
}
.subtabs button {
  border: 1px solid transparent;
  border-bottom: none;
  background: transparent;
  padding: 3px 10px;
}
.subtabs button.on {
  border-color: #3a3a50;
  background: #1e1e2e;
  color: #cdd6f4;
}
/* ⑬ Story: a scene a row, opened to what was built for it */
.trace-scene {
  border-bottom: 1px solid #2a2a3a;
}
.trace-head {
  cursor: pointer;
  gap: 6px;
  padding: 3px 0;
}
.trace-body {
  padding: 4px 0 8px 18px;
}
.trace-body h5 {
  margin: 8px 0 2px;
  font-size: 11px;
  text-transform: uppercase;
  color: #9a9ab0;
}
.fix-how {
  font-style: italic;
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

/* ② Outline, edited */
.form { display: flex; flex-direction: column; gap: 4px; }
.form .row, .card.row { display: flex; flex-wrap: wrap; align-items: center; gap: 6px; }
.form input, .form select, .form textarea { min-width: 0; }
.form input.wide, .card .wide { flex: 1; width: 100%; }
.form label { display: flex; flex-direction: column; gap: 2px; }
.form label.check, .form .row label { flex-direction: row; align-items: center; gap: 4px; }
.form input.num { width: 52px; }
.card .edit, p .edit { margin-left: 6px; font-size: 11px; }

/* ⑪ Draw the others too: a real button beside the art table's heading */
.draw-others { margin-left: 10px; padding: 3px 10px; font-size: 12px; font-weight: 600; color: #fff; background: #3a6ea5; border: 1px solid #4f86c2; border-radius: 0; cursor: pointer; }
.draw-others:hover { background: #4f86c2; }
</style>
