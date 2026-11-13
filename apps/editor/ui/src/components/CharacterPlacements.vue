<script setup lang="ts">
// AI Planner ▸ Build ▸ Placements (between Cast and Implement): where every NPC and every story prop / quest item lives, and where they are over the story
// (their whereabouts). Read from the host's placements (build:placements), edited a field at a time
// (build:setPlacement: the edit locks it against Replace Characters), placed again by Replace Characters.
// docs: data/help/ai/build-panel.md (Placements tab) and data/help/ai/planner.md.
import { computed, onBeforeUnmount, ref, watch } from 'vue'
import { onHostMessage, sendToHost } from '../ipc'

interface When {
  quest?: string | null
  stage?: string | number | null
  on?: string | null
  event?: string | null
  start?: boolean | null
}
interface Whereabout {
  id?: string | null
  from?: When | null
  until?: When | null
  scene?: string | null
  x?: number | null
  y?: number | null
  marker?: string | null
  building?: string | null
  spot?: Spot | null
  why?: string | null
  req?: string | null
  status?: string | null
}
interface Spot {
  scene?: string | null
  x?: number | null
  y?: number | null
  marker?: string | null
  building?: string | null
  near?: string | null
  inside?: boolean | null
}
interface Building {
  id?: string | null
  scene?: string | null
  kind?: string | null
  name?: string | null
  door?: (number | null)[] | null
  box?: (number | null)[] | null
  interior?: string | null
  owner?: string | null
  use?: string | null
  building?: boolean | null
  area?: boolean | null
  anchor?: boolean | null
  painted?: boolean | null
  marker?: string | null
  module?: string | null
}
interface Placed {
  id?: string | null
  name?: string | null
  scene?: string | null
  home?: string | null
  work?: string | null
  prefab?: string | null
  item?: string | null
  building?: string | null
  spot?: Spot | null
  why?: string | null
  locked?: boolean | null
  whereabouts?: Whereabout[] | null
  faces?: string | null // a prop's front: what it looks to
  facing?: string | null // how it was turned for it
}
interface Road {
  scene?: string | null
  name?: string | null
  class?: string | null
  points?: (number[] | null)[] | null
  cells?: number | null
  exits?: string[] | null
}
interface Exit {
  scene?: string | null
  marker?: string | null
  side?: string | null
  x?: number | null
  y?: number | null
  road?: string | null
}
interface WayOut {
  scene?: string | null
  to?: string | null
  exit?: string | null
  x?: number | null
  y?: number | null
  arrive?: (number | null)[] | null
  was?: (number | null)[] | null
  why?: string | null
}
interface Placements {
  updated?: number | null
  buildings?: Building[] | null
  characters?: Placed[] | null
  props?: Placed[] | null
  roads?: Road[] | null
  exits?: Exit[] | null
  gateways?: WayOut[] | null
}
type Kind = 'character' | 'prop'
type View = 'characters' | 'stages' | 'props' | 'buildings' | 'other' | 'paths'
interface QuestStage {
  index?: number | string | null
  journal?: string | null
  scene?: string | null
}
interface Quest {
  id?: string | null
  name?: string | null
  type?: string | null
  stages?: QuestStage[] | null
}

const props = defineProps<{ active?: boolean }>()

const placements = ref<Placements | null>(null)
const loaded = ref(false)
const error = ref('')
const running = ref(false)
const progress = ref('')
const filter = ref('')
const sceneFilter = ref('')
const view = ref<View>('characters')
const expanded = ref(new Set<string>())
const quests = ref<Quest[]>([]) // ⑥b's quests and stages, for By stage

let alive = true
onBeforeUnmount(() => (alive = false))
onHostMessage((message) => {
  if (!alive) return
  if (message.type === 'build:placements') {
    placements.value = (message.placements as Placements | null | undefined) ?? null
    error.value = typeof message.error === 'string' ? message.error : ''
    loaded.value = true
    running.value = false
    progress.value = ''
  } else if (message.type === 'build:questStages') {
    quests.value = ((message.quests as Quest[] | null | undefined) ?? []).filter((q): q is Quest => !!q && !!q.id)
  } else if (message.type === 'build:placementsProgress') {
    running.value = true
    progress.value = typeof message.text === 'string' ? message.text : ''
  }
})
function request(): void {
  sendToHost({ type: 'build:placements' })
  sendToHost({ type: 'build:questStages' })
}
watch(
  () => props.active,
  (on) => {
    if (on !== false) request()
  },
  { immediate: true },
)

function replaceCharacters(): void {
  running.value = true
  progress.value = 'Starting…'
  sendToHost({ type: 'build:replaceCharacters' })
}

// ---- lookups ----
const buildings = computed(() => (placements.value?.buildings ?? []).filter((b): b is Building => !!b))
const characters = computed(() => (placements.value?.characters ?? []).filter((c): c is Placed => !!c))
const propList = computed(() => (placements.value?.props ?? []).filter((p): p is Placed => !!p))
const buildingById = computed(() => {
  const m = new Map<string, Building>()
  for (const b of buildings.value) if (b.id) m.set(b.id, b)
  return m
})
const characterName = computed(() => {
  const m = new Map<string, string>()
  for (const c of characters.value) if (c.id) m.set(c.id, c.name || c.id)
  return m
})
const scenes = computed(() => {
  const s = new Set<string>()
  const add = (x?: string | null) => {
    if (x) s.add(x)
  }
  for (const b of buildings.value) add(b.scene)
  for (const p of [...characters.value, ...propList.value]) {
    add(p.scene)
    add(p.spot?.scene)
    for (const w of p.whereabouts ?? []) add(w?.scene)
  }
  return [...s].sort()
})
const isEmpty = computed(() => !buildings.value.length && !characters.value.length && !propList.value.length)

function sceneName(path?: string | null): string {
  if (!path) return '—'
  const last = path.split(/[\\/]/).pop() ?? path
  return last.replace(/\.(aige|scene|json)$/i, '')
}
function buildingLabel(id?: string | null): string {
  if (!id) return '—'
  const b = buildingById.value.get(id)
  if (!b) return id
  const owner = b.owner ? ` · ${characterName.value.get(b.owner) ?? b.owner}` : ''
  return `${b.name || b.kind || 'building'} (${id})${owner}`
}
function buildingOption(b: Building): string {
  const owner = b.owner ? ` — ${characterName.value.get(b.owner) ?? b.owner}` : ''
  return `${b.kind || 'building'} · ${b.id}${b.name ? ' ' + b.name : ''}${owner}`
}
function buildingsIn(scene?: string | null): Building[] {
  return buildings.value.filter((b) => !scene || b.scene === scene)
}
function num(v: unknown): number | null {
  return typeof v === 'number' && Number.isFinite(v) ? v : null
}
function xyText(x?: number | null, y?: number | null): string {
  const a = num(x)
  const b = num(y)
  return a !== null && b !== null ? `${Math.round(a)}, ${Math.round(b)}` : ''
}
function placeText(w?: Spot | Whereabout | null): string {
  if (!w) return '—'
  const parts: string[] = [sceneName(w.scene)]
  if (w.building) parts.push(buildingLabel(w.building))
  if (w.marker) parts.push(`marker ${w.marker}`)
  const xy = xyText(w.x, w.y)
  if (xy) parts.push(`(${xy})`)
  return parts.join(' · ')
}
// A building's box in cells: x0,y0–x1,y1.
function boxText(b?: Building | null): string {
  const box = b?.box
  const x0 = num(box?.[0]), y0 = num(box?.[1]), x1 = num(box?.[2]), y1 = num(box?.[3])
  return x0 !== null && y0 !== null && x1 !== null && y1 !== null ? `${x0}, ${y0} – ${x1}, ${y1}` : ''
}
// A span of the whereabouts: where they stand (the spot: a building's door or inside it, a marker, near something), the
// cell, then the marker made for it (their own: home-…, where/…) dimmed.
function whereText(w?: Whereabout | null): string {
  if (!w) return '—'
  const s = w.spot
  if (!s) return placeText(w)
  const parts: string[] = [sceneName(s.scene || w.scene)]
  if (s.building) parts.push(`${s.inside ? 'inside' : 'at the door of'} ${buildingLabel(s.building)}`)
  else if (s.marker) parts.push(`at ${s.marker}`)
  else if (s.near) parts.push(`near ${s.near}`)
  const xy = xyText(s.x, s.y)
  if (xy) parts.push(`(${xy})`)
  return parts.join(' · ')
}
function spotText(s?: Spot | null): string {
  if (!s) return '—'
  const where = placeText(s)
  if (!s.building) return where
  return `${where} · ${s.inside ? 'inside' : 'at door'}`
}
function whenText(w?: When | null, isUntil = false): string {
  if (!w) return isUntil ? 'the end' : '—'
  if (w.start) return 'start'
  if (w.event) return `event ${w.event}`
  if (w.quest) return `${w.quest}${w.stage !== undefined && w.stage !== null && w.stage !== '' ? ' · stage ' + w.stage : ''}`
  return '—'
}
// Where to frame it in the viewport: its x,y, else its building's door, else the middle of the building's box. A
// whereabouts span frames its spot.
function focusOf(w0?: Spot | Whereabout | null): { scene: string; x: number; y: number } | null {
  const w: Spot | Whereabout | null | undefined = w0 && 'spot' in w0 && w0.spot ? { ...w0.spot, scene: w0.spot.scene || w0.scene } : w0
  if (!w?.scene) return null
  const x = num(w.x)
  const y = num(w.y)
  if (x !== null && y !== null) return { scene: w.scene, x, y }
  const b = w.building ? buildingById.value.get(w.building) : undefined
  const dx = num(b?.door?.[0])
  const dy = num(b?.door?.[1])
  if (dx !== null && dy !== null) return { scene: w.scene, x: dx, y: dy }
  const box = b?.box
  const x0 = num(box?.[0]), y0 = num(box?.[1]), x1 = num(box?.[2]), y1 = num(box?.[3])
  if (x0 !== null && y0 !== null && x1 !== null && y1 !== null) return { scene: w.scene, x: (x0 + x1) / 2, y: (y0 + y1) / 2 }
  return null
}
// Framed and painted in the viewport (red: the cell they stand on; its building's box and door too). A click in the
// viewport clears the paint.
function focus(w?: Spot | Whereabout | null): void {
  const f = focusOf(w)
  if (!f) return
  const s: Spot | Whereabout | null | undefined = w && 'spot' in w && w.spot ? w.spot : w
  const b = s?.building ? buildingById.value.get(s.building) : undefined
  const msg: { type: string; [key: string]: unknown } = { type: 'build:focusPlacement', scene: f.scene, x: f.x, y: f.y }
  if (b && b.scene === f.scene) {
    if (boxText(b)) msg.box = b.box
    if (num(b.door?.[0]) !== null && num(b.door?.[1]) !== null) msg.door = b.door
  }
  sendToHost(msg)
}
function focusBuilding(b: Building): void {
  focus({ scene: b.scene, building: b.id })
}

// ---- the tables ----
function matches(p: Placed): boolean {
  const f = filter.value.trim().toLowerCase()
  if (f && !`${p.name ?? ''} ${p.id ?? ''} ${p.prefab ?? ''} ${p.item ?? ''}`.toLowerCase().includes(f)) return false
  if (sceneFilter.value) {
    const inScene = p.scene === sceneFilter.value || p.spot?.scene === sceneFilter.value ||
      (p.whereabouts ?? []).some((w) => w?.scene === sceneFilter.value)
    if (!inScene) return false
  }
  return true
}
const rows = computed(() => (view.value === 'props' ? propList.value : characters.value).filter(matches))
const kind = computed<Kind>(() => (view.value === 'props' ? 'prop' : 'character'))
// Buildings (houses, shops...), and the rest: the land areas (squares, graveyards, fields), the story's anchors, wells,
// stalls, graves, crops, gates.
const realBuildings = computed(() => buildings.value.filter((b) => b.building !== false))
const otherPlaces = computed(() => buildings.value.filter((b) => b.building === false))
function shown(list: Building[]): Building[] {
  const f = filter.value.trim().toLowerCase()
  return list.filter((b) => (!sceneFilter.value || b.scene === sceneFilter.value) &&
    (!f || `${b.id ?? ''} ${b.name ?? ''} ${b.kind ?? ''} ${b.marker ?? ''} ${b.owner ? characterName.value.get(b.owner) ?? b.owner : ''}`.toLowerCase().includes(f)))
}
const shownBuildings = computed(() => shown(realBuildings.value))
const OTHER_ORDER = ['square', 'graveyard', 'field', 'anchor', 'centrepiece', 'stall', 'gateway-gate', 'grave', 'crop']
const shownOther = computed(() => shown(otherPlaces.value).slice().sort((a, b) => {
  const i = OTHER_ORDER.indexOf(a.kind ?? ''), j = OTHER_ORDER.indexOf(b.kind ?? '')
  return (i < 0 ? 99 : i) - (j < 0 ? 99 : j)
}))
function otherKind(b: Building): string {
  if (b.anchor) return 'story anchor'
  if (b.kind === 'centrepiece') return 'centrepiece'
  if (b.kind === 'gateway-gate') return 'gate'
  return b.kind || '—'
}
function keyOf(p: Placed): string {
  return `${kind.value}:${p.id ?? p.name ?? ''}`
}
function toggle(p: Placed): void {
  const k = keyOf(p)
  const s = new Set(expanded.value)
  if (s.has(k)) s.delete(k)
  else s.add(k)
  expanded.value = s
}
const STATUS = ['todo', 'built', 'failed', 'verified']

// ---- Paths: the roads (as paths characters walk), where they leave each scene, and the gateways on them ----
const roads = computed(() => (placements.value?.roads ?? []).filter((r): r is Road => !!r && (!sceneFilter.value || r.scene === sceneFilter.value)))
const exits = computed(() => (placements.value?.exits ?? []).filter((x): x is Exit => !!x && (!sceneFilter.value || x.scene === sceneFilter.value)))
const waysOut = computed(() => (placements.value?.gateways ?? []).filter((g): g is WayOut => !!g && (!sceneFilter.value || g.scene === sceneFilter.value)))
function focusCell(scene?: string | null, x?: number | null, y?: number | null): void {
  if (!scene || num(x) === null || num(y) === null) return
  sendToHost({ type: 'build:focusPlacement', scene, x, y })
}
function roadEnds(r: Road): string {
  const p = r.points ?? []
  const a = p[0], b = p[p.length - 1]
  return a && b ? `${a[0]}, ${a[1]} → ${b[0]}, ${b[1]}` : '—'
}

// ---- By stage: where everyone is while a quest stage plays, and where they go once it completes ----
// A moment of a quest: the start (before anything), or a stage's enter or complete. A span of the whereabouts holds at it
// when it began at or before it (the start, or a moment of the same quest) and hasn't ended by then; a span bound to
// another quest's moments can't be ordered against it and is passed over (unless it runs from the start).
interface Moment {
  quest: string
  stage: number | null // null: the start
}
const moment = ref<Moment | null>(null)
function stageNum(v: unknown): number | null {
  const n = typeof v === 'number' ? v : typeof v === 'string' && /^-?\d+$/.test(v.trim()) ? Number(v) : NaN
  return Number.isFinite(n) ? n : null
}
// Its order within quest: the start -1, a stage's enter 2*stage, its complete 2*stage+1; undefined: another quest's,
// an event's (not ordered).
function orderOf(w: When | null | undefined, quest: string): number | undefined {
  if (!w) return undefined
  if (w.start) return -1
  if (w.quest !== quest) return undefined
  const n = stageNum(w.stage)
  if (n === null) return undefined
  return 2 * n + (w.on === 'complete' ? 1 : 0)
}
function spanAt(c: Placed, quest: string, at: number): Whereabout | null {
  let found: Whereabout | null = null
  for (const w of c.whereabouts ?? []) {
    if (!w) continue
    const from = orderOf(w.from, quest)
    if (from === undefined || from > at) continue
    const until = w.until ? orderOf(w.until, quest) : undefined
    if (until !== undefined && until <= at) continue
    if (w.until && until === undefined && !w.until.start && w.until.quest === quest) continue
    found = w
  }
  return found
}
// The quests to pick from: ⑥b's (with each stage's journal), else the ones the whereabouts name.
const questList = computed<Quest[]>(() => {
  if (quests.value.length) return quests.value
  const m = new Map<string, Set<number>>()
  for (const c of characters.value) {
    for (const w of c.whereabouts ?? []) {
      for (const t of [w?.from, w?.until]) {
        const n = stageNum(t?.stage)
        if (t?.quest && n !== null) (m.get(t.quest) ?? m.set(t.quest, new Set()).get(t.quest)!).add(n)
      }
    }
  }
  return [...m].map(([id, st]) => ({ id, name: id, stages: [...st].sort((a, b) => a - b).map((index) => ({ index })) }))
})
function pickStage(quest: string, stage: number | null): void {
  moment.value = { quest, stage }
}
function isPicked(quest: string, stage: number | null): boolean {
  return moment.value?.quest === quest && moment.value.stage === stage
}
const pickedStage = computed<QuestStage | null>(() => {
  const m = moment.value
  if (!m || m.stage === null) return null
  return questList.value.find((q) => q.id === m.quest)?.stages?.find((s) => stageNum(s.index) === m.stage) ?? null
})
interface StageRow {
  c: Placed
  during: Whereabout | null
  after: Whereabout | null // where they are once the stage completes, when that's another span
}
const stageRows = computed<StageRow[]>(() => {
  const m = moment.value
  if (!m) return []
  const at = m.stage === null ? -1 : 2 * m.stage
  return characters.value.filter(matches).map((c) => {
    const during = spanAt(c, m.quest, at)
    const next = m.stage === null ? null : spanAt(c, m.quest, at + 1)
    return { c, during, after: next && next !== during ? next : null }
  }).sort((a, b) => sceneName(a.during?.spot?.scene || a.during?.scene).localeCompare(sceneName(b.during?.spot?.scene || b.during?.scene)) ||
    (a.c.name ?? '').localeCompare(b.c.name ?? ''))
})
function sinceText(w: Whereabout | null): string {
  if (!w) return ''
  if (w.from?.start) return 'from the start'
  const n = stageNum(w.from?.stage)
  return w.from?.quest ? `since ${w.from.quest} · ${w.from.on === 'complete' ? 'completed' : 'entered'} ${n ?? w.from.stage ?? ''}` : whenText(w.from)
}

// ---- editing: every edit locks the entry against Replace Characters ----
function setPlacement(p: Placed, patch: Record<string, unknown>): void {
  if (!p.id) return
  const lock = 'locked' in patch ? {} : { locked: true }
  sendToHost({ type: 'build:setPlacement', kind: kind.value, id: p.id, patch: { ...patch, ...lock } })
}
function onSelect(p: Placed, field: 'home' | 'work', e: Event): void {
  const v = (e.target as HTMLSelectElement).value
  setPlacement(p, { [field]: v || null })
}
function onLocked(p: Placed, e: Event): void {
  setPlacement(p, { locked: (e.target as HTMLInputElement).checked })
}

// The spot: a building (inside or at its door), a marker, or x,y.
interface SpotDraft {
  key: string
  scene: string
  mode: 'building' | 'marker' | 'xy'
  building: string
  inside: boolean
  marker: string
  x: number | null
  y: number | null
}
const spotEdit = ref<SpotDraft | null>(null)
function editSpot(p: Placed): void {
  const s = p.spot ?? {}
  spotEdit.value = {
    key: keyOf(p),
    scene: s.scene || p.scene || sceneFilter.value || scenes.value[0] || '',
    mode: s.building ? 'building' : s.marker ? 'marker' : 'xy',
    building: s.building ?? '',
    inside: !!s.inside,
    marker: s.marker ?? '',
    x: num(s.x),
    y: num(s.y),
  }
}
function saveSpot(p: Placed): void {
  const d = spotEdit.value
  if (!d) return
  const spot: Spot = { scene: d.scene }
  if (d.mode === 'building' && d.building) {
    spot.building = d.building
    spot.inside = d.inside
  } else if (d.mode === 'marker' && d.marker.trim()) {
    spot.marker = d.marker.trim()
  } else {
    spot.x = d.x ?? 0
    spot.y = d.y ?? 0
  }
  setPlacement(p, { spot })
  spotEdit.value = null
}

// The whereabouts: the whole list edited, then sent whole.
type WhenKind = 'start' | 'quest' | 'event' | 'end'
interface WhenDraft {
  kind: WhenKind
  quest: string
  stage: string
  event: string
}
interface RowDraft {
  id: string
  from: WhenDraft
  until: WhenDraft
  scene: string
  building: string
  marker: string
  x: number | null
  y: number | null
  why: string
  req: string | null
  status: string
}
const wbEdit = ref<{ key: string; rows: RowDraft[] } | null>(null)
function whenDraft(w: When | null | undefined, isUntil: boolean): WhenDraft {
  const d: WhenDraft = { kind: isUntil ? 'end' : 'start', quest: '', stage: '', event: '' }
  if (!w) return d
  if (w.start) d.kind = 'start'
  else if (w.event) {
    d.kind = 'event'
    d.event = w.event
  } else if (w.quest) {
    d.kind = 'quest'
    d.quest = w.quest
    d.stage = w.stage === undefined || w.stage === null ? '' : String(w.stage)
  }
  return d
}
function whenOut(d: WhenDraft): When | null {
  if (d.kind === 'end') return null
  if (d.kind === 'start') return { start: true }
  if (d.kind === 'event') return { event: d.event.trim() }
  const stage = d.stage.trim()
  return { quest: d.quest.trim(), stage: /^-?\d+$/.test(stage) ? Number(stage) : stage || null }
}
function editWhereabouts(p: Placed): void {
  wbEdit.value = {
    key: keyOf(p),
    rows: (p.whereabouts ?? []).filter((w): w is Whereabout => !!w).map((w, i) => ({
      id: w.id || `w${i + 1}`,
      from: whenDraft(w.from, false),
      until: whenDraft(w.until, true),
      scene: w.scene ?? '',
      building: w.building ?? '',
      marker: w.marker ?? '',
      x: num(w.x),
      y: num(w.y),
      why: w.why ?? '',
      req: w.req ?? null,
      status: w.status ?? 'todo',
    })),
  }
}
function addRow(p: Placed): void {
  const d = wbEdit.value
  if (!d) return
  const used = new Set(d.rows.map((r) => r.id))
  let n = d.rows.length + 1
  while (used.has(`w${n}`)) n++
  const last = d.rows[d.rows.length - 1]
  d.rows.push({
    id: `w${n}`,
    from: { kind: 'quest', quest: '', stage: '', event: '' },
    until: { kind: 'end', quest: '', stage: '', event: '' },
    scene: last?.scene || p.spot?.scene || p.scene || '',
    building: '',
    marker: '',
    x: null,
    y: null,
    why: '',
    req: null,
    status: 'todo',
  })
}
function removeRow(i: number): void {
  wbEdit.value?.rows.splice(i, 1)
}
function moveRow(i: number, by: number): void {
  const r = wbEdit.value?.rows
  if (!r) return
  const j = i + by
  if (j < 0 || j >= r.length) return
  const [row] = r.splice(i, 1)
  if (row) r.splice(j, 0, row)
}
function saveWhereabouts(p: Placed): void {
  const d = wbEdit.value
  if (!d) return
  const whereabouts: Whereabout[] = d.rows.map((r) => {
    const w: Whereabout = { id: r.id, from: whenOut(r.from), until: whenOut(r.until), scene: r.scene, why: r.why, req: r.req, status: r.status }
    if (r.building) w.building = r.building
    if (r.marker.trim()) w.marker = r.marker.trim()
    if (r.x !== null && r.y !== null) {
      w.x = r.x
      w.y = r.y
    }
    return w
  })
  setPlacement(p, { whereabouts })
  wbEdit.value = null
}

const updatedText = computed(() => {
  const u = num(placements.value?.updated)
  return u ? new Date(u * 1000).toLocaleString() : ''
})
</script>

<template>
  <div class="placements">
    <div class="toolbar">
      <button class="primary" :disabled="running"
              title="Place every NPC and story prop again from the story and the built places (locked entries are kept)"
              @click="replaceCharacters">Replace Characters</button>
      <span v-if="running" class="progress"><span class="spinner" />{{ progress || 'Working…' }}</span>
      <input v-model="filter" class="filter" placeholder="Filter by name" />
      <select v-model="sceneFilter" title="Only what is in this scene (at any time of the story)">
        <option value="">All scenes</option>
        <option v-for="s in scenes" :key="s" :value="s">{{ sceneName(s) }}</option>
      </select>
      <div class="seg">
        <button :class="{ active: view === 'characters' }" @click="view = 'characters'">Characters <span class="dim">{{ characters.length }}</span></button>
        <button :class="{ active: view === 'stages' }" title="Pick a quest stage: where every character is while it plays, and where they go once it completes"
                @click="view = 'stages'">By stage</button>
        <button :class="{ active: view === 'props' }" @click="view = 'props'">Props <span class="dim">{{ propList.length }}</span></button>
        <button :class="{ active: view === 'buildings' }" @click="view = 'buildings'">Buildings <span class="dim">{{ realBuildings.length }}</span></button>
        <button :class="{ active: view === 'other' }" title="Squares, graveyards, fields, the story's anchors, wells, stalls, graves, gates"
                @click="view = 'other'">Other <span class="dim">{{ otherPlaces.length }}</span></button>
        <button :class="{ active: view === 'paths' }" title="The roads as paths characters walk, where they leave each scene (to-the-west…), and the gateways between outdoor places on them"
                @click="view = 'paths'">Paths <span class="dim">{{ (placements?.roads ?? []).length }}</span></button>
      </div>
      <span class="spacer" />
      <span v-if="updatedText" class="dim small">Updated {{ updatedText }}</span>
      <button title="Read the placements again" @click="request">Reload</button>
    </div>

    <p v-if="error" class="error">{{ error }}</p>
    <div v-if="!loaded" class="empty dim">Loading placements…</div>
    <div v-else-if="isEmpty" class="empty">
      <p>No placements yet — press <strong>Replace Characters</strong>.</p>
      <p class="dim small">It places every NPC in a home and a workplace in the built places, puts the story's props and quest
        items where the story needs them, and works out where each one is at every quest stage.</p>
    </div>

    <!-- Buildings -->
    <div v-else-if="view === 'buildings'" class="table-wrap">
      <table class="grid-table">
        <thead>
          <tr>
            <th>Id</th><th>Kind</th><th>Scene</th>
            <th title="The cells the building covers: x0, y0 – x1, y1 (a painted one: its box as the painting shows it)">Box</th>
            <th title="The cell in front of its front door: where whoever lives or works there stands when they're 'at the door', and where its interior's gateway is">Door</th>
            <th>Interior</th><th>Owner</th><th>Use</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="b in shownBuildings" :key="(b.scene ?? '') + (b.id ?? '')">
            <td class="mono">{{ b.id || '—' }}<span v-if="b.name" class="dim"> {{ b.name }}</span></td>
            <td>{{ b.kind || '—' }}</td>
            <td :title="b.scene ?? ''">{{ sceneName(b.scene) }}</td>
            <td>
              <button v-if="boxText(b)" class="link" title="Show it in the viewport: its box and door painted" @click="focusBuilding(b)">{{ boxText(b) }}</button>
              <span v-else class="dim">—</span>
            </td>
            <td>
              <button v-if="focusOf({ scene: b.scene, building: b.id })" class="link" title="Show it in the viewport: its box and door painted" @click="focusBuilding(b)">
                {{ xyText(b.door?.[0], b.door?.[1]) || 'show' }}
              </button>
              <span v-else class="dim">—</span>
            </td>
            <td :title="b.interior ?? ''">{{ b.interior ? sceneName(b.interior) : '' }}<span v-if="!b.interior" class="dim">not drawn</span></td>
            <td>{{ b.owner ? characterName.get(b.owner) ?? b.owner : '—' }}</td>
            <td>{{ b.use || '—' }}</td>
          </tr>
          <tr v-if="!shownBuildings.length"><td colspan="8" class="dim">No buildings match.</td></tr>
        </tbody>
      </table>
    </div>

    <!-- Other: land areas, story anchors, landmarks -->
    <div v-else-if="view === 'other'" class="table-wrap">
      <table class="grid-table">
        <thead>
          <tr>
            <th>Kind</th><th>Name</th><th>Scene</th>
            <th title="The cells it covers: x0, y0 – x1, y1">Box</th>
            <th title="Where one stands at it: an area's middle, an anchor's marker, a thing's front">Cell</th>
            <th title="The marker quests and the story use for it">Marker</th>
            <th>Id</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="b in shownOther" :key="(b.scene ?? '') + (b.id ?? '')">
            <td>{{ otherKind(b) }}<span v-if="b.painted" class="dim" title="Where the painting shows it (not where the plan had it)"> · painted</span></td>
            <td :title="b.module ? 'Plan module: ' + b.module : ''">{{ b.name || '—' }}</td>
            <td :title="b.scene ?? ''">{{ sceneName(b.scene) }}</td>
            <td>
              <button v-if="boxText(b)" class="link" title="Show it in the viewport, painted" @click="focusBuilding(b)">{{ boxText(b) }}</button>
              <span v-else class="dim">—</span>
            </td>
            <td>
              <button v-if="xyText(b.door?.[0], b.door?.[1])" class="link" title="Show it in the viewport, painted" @click="focusBuilding(b)">
                {{ xyText(b.door?.[0], b.door?.[1]) }}
              </button>
              <span v-else class="dim">—</span>
            </td>
            <td class="mono">{{ b.marker || '' }}</td>
            <td class="mono dim">{{ b.id || '—' }}</td>
          </tr>
          <tr v-if="!shownOther.length"><td colspan="7" class="dim">Nothing matches.</td></tr>
        </tbody>
      </table>
    </div>

    <!-- Paths: roads, ways out, gateways -->
    <div v-else-if="view === 'paths'" class="table-wrap">
      <h4 class="sub">Roads <span class="dim small">paths characters' AI packages can walk (road-…); the corners shown</span></h4>
      <table class="grid-table">
        <thead><tr><th>Path</th><th>Kind</th><th>Scene</th><th>From → to</th><th>Corners</th><th>Ways out</th></tr></thead>
        <tbody>
          <tr v-for="r in roads" :key="(r.scene ?? '') + (r.name ?? '')">
            <td class="mono">{{ r.name }}</td>
            <td>{{ r.class || '—' }}</td>
            <td :title="r.scene ?? ''">{{ sceneName(r.scene) }}</td>
            <td>
              <button class="link" title="Show its start in the viewport" @click="focusCell(r.scene, r.points?.[0]?.[0], r.points?.[0]?.[1])">{{ roadEnds(r) }}</button>
            </td>
            <td>{{ (r.points ?? []).length }}</td>
            <td class="mono">{{ (r.exits ?? []).join(', ') }}</td>
          </tr>
          <tr v-if="!roads.length"><td colspan="6" class="dim">No roads yet: press Replace Characters.</td></tr>
        </tbody>
      </table>
      <h4 class="sub">Ways out <span class="dim small">where a road leaves the scene: x 0 is west, y 0 north</span></h4>
      <table class="grid-table">
        <thead><tr><th>Marker</th><th>Side</th><th>Scene</th><th>Cell</th><th>Road</th></tr></thead>
        <tbody>
          <tr v-for="x in exits" :key="(x.scene ?? '') + (x.marker ?? '')">
            <td class="mono">{{ x.marker }}</td>
            <td>{{ x.side }}</td>
            <td :title="x.scene ?? ''">{{ sceneName(x.scene) }}</td>
            <td><button class="link" title="Show it in the viewport, painted" @click="focusCell(x.scene, x.x, x.y)">{{ xyText(x.x, x.y) }}</button></td>
            <td class="mono dim">{{ x.road }}</td>
          </tr>
          <tr v-if="!exits.length"><td colspan="5" class="dim">No road leaves a scene.</td></tr>
        </tbody>
      </table>
      <h4 class="sub">Gateways between outdoor places <span class="dim small">put on a way out by the AI; the anchors by them moved with them</span></h4>
      <table class="grid-table">
        <thead><tr><th>Scene</th><th>To</th><th>On</th><th>Cell</th><th>Arrive</th><th>Was</th><th>Why</th></tr></thead>
        <tbody>
          <tr v-for="g in waysOut" :key="(g.scene ?? '') + (g.to ?? '')">
            <td :title="g.scene ?? ''">{{ sceneName(g.scene) }}</td>
            <td :title="g.to ?? ''">{{ sceneName(g.to) }}</td>
            <td class="mono">{{ g.exit }}</td>
            <td><button class="link" title="Show it in the viewport, painted" @click="focusCell(g.scene, g.x, g.y)">{{ xyText(g.x, g.y) }}</button></td>
            <td>{{ xyText(g.arrive?.[0], g.arrive?.[1]) }}</td>
            <td class="dim">{{ xyText(g.was?.[0], g.was?.[1]) }}</td>
            <td class="small">{{ g.why }}</td>
          </tr>
          <tr v-if="!waysOut.length"><td colspan="7" class="dim">None moved: they stand where the build put them.</td></tr>
        </tbody>
      </table>
    </div>

    <!-- By stage: a quest's stages, and where everyone is at the one picked -->
    <div v-else-if="view === 'stages'" class="stages">
      <div class="stage-list">
        <div v-if="!questList.length" class="dim small">No quests yet: ⑥b Cast timeline lays them out.</div>
        <div v-for="q in questList" :key="q.id ?? ''" class="quest">
          <div class="quest-name">{{ q.name || q.id }} <span class="dim mono small">{{ q.id }}</span></div>
          <button class="stage" :class="{ active: isPicked(q.id ?? '', null) }" title="Before the story begins"
                  @click="pickStage(q.id ?? '', null)">Start</button>
          <button v-for="st in (q.stages ?? [])" :key="String(st.index)" class="stage"
                  :class="{ active: isPicked(q.id ?? '', stageNum(st.index)) }" :title="st.journal || ''"
                  @click="pickStage(q.id ?? '', stageNum(st.index))">
            <span class="mono">{{ st.index }}</span> {{ st.journal || '' }}<span v-if="st.scene" class="dim small"> · {{ st.scene }}</span>
          </button>
        </div>
      </div>
      <div class="table-wrap">
        <div v-if="!moment" class="empty dim">Pick a stage on the left.</div>
        <template v-else>
          <div class="stage-head">
            <b>{{ moment.quest }} · {{ moment.stage === null ? 'start' : 'stage ' + moment.stage }}</b>
            <span v-if="pickedStage?.journal" class="dim"> — {{ pickedStage.journal }}</span>
          </div>
          <table class="grid-table">
            <thead>
              <tr>
                <th>Character</th>
                <th title="Where they are while this stage plays (click: show it in the viewport)">Where</th>
                <th>Since</th>
                <th>Why</th>
                <th v-if="moment.stage !== null" title="Where they go once this stage completes, when they move then">Once it completes</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="r in stageRows" :key="r.c.id ?? ''">
                <td>{{ r.c.name || r.c.id }}</td>
                <td>
                  <button v-if="r.during && focusOf(r.during)" class="link" title="Show it in the viewport, painted" @click="focus(r.during)">{{ whereText(r.during) }}</button>
                  <span v-else-if="r.during">{{ whereText(r.during) }}</span>
                  <span v-else class="dim">— not placed for this quest</span>
                </td>
                <td class="small dim">{{ sinceText(r.during) }}</td>
                <td class="small">{{ r.during?.why || '' }}</td>
                <td v-if="moment.stage !== null">
                  <button v-if="r.after && focusOf(r.after)" class="link" :title="r.after.why || ''" @click="focus(r.after)">→ {{ whereText(r.after) }}</button>
                  <span v-else class="dim">stays</span>
                </td>
              </tr>
              <tr v-if="!stageRows.length"><td :colspan="moment.stage === null ? 4 : 5" class="dim">Nothing matches.</td></tr>
            </tbody>
          </table>
        </template>
      </div>
    </div>

    <!-- Characters and props -->
    <div v-else class="table-wrap">
      <table class="grid-table">
        <thead>
          <tr>
            <th class="tw" />
            <th>Name</th>
            <th v-if="view === 'props'">Prefab / item</th>
            <th>Scene</th>
            <th v-if="view === 'characters'">Home</th>
            <th v-if="view === 'characters'">Work</th>
            <th v-if="view === 'props'">Building</th>
            <th>Spot now</th>
            <th>Why</th>
            <th title="Locked: kept as it is when Replace Characters runs">Locked</th>
          </tr>
        </thead>
        <tbody>
          <template v-for="p in rows" :key="keyOf(p)">
            <tr :class="{ open: expanded.has(keyOf(p)) }">
              <td class="tw"><button class="twisty" :title="expanded.has(keyOf(p)) ? 'Hide the whereabouts' : 'Show the whereabouts'" @click="toggle(p)">{{ expanded.has(keyOf(p)) ? '▾' : '▸' }}</button></td>
              <td :title="p.id ?? ''"><strong>{{ p.name || p.id || '—' }}</strong></td>
              <td v-if="view === 'props'" class="mono small">{{ [p.prefab, p.item].filter(Boolean).join(' / ') || '—' }}
                <div v-if="p.faces" class="dim" :title="'Turned for it: ' + (p.facing || 'not yet')">faces {{ p.faces }}{{ p.facing ? ' · ' + p.facing : '' }}</div></td>
              <td :title="p.scene ?? ''">{{ sceneName(p.scene) }}</td>
              <td v-if="view === 'characters'">
                <select :value="p.home ?? ''" @change="onSelect(p, 'home', $event)">
                  <option value="">— none —</option>
                  <option v-if="p.home && !buildingsIn(p.scene).some((b) => b.id === p.home)" :value="p.home">{{ buildingLabel(p.home) }}</option>
                  <option v-for="b in buildingsIn(p.scene)" :key="b.id ?? ''" :value="b.id ?? ''">{{ buildingOption(b) }}</option>
                </select>
              </td>
              <td v-if="view === 'characters'">
                <select :value="p.work ?? ''" @change="onSelect(p, 'work', $event)">
                  <option value="">— none —</option>
                  <option v-if="p.work && !buildingsIn(p.scene).some((b) => b.id === p.work)" :value="p.work">{{ buildingLabel(p.work) }}</option>
                  <option v-for="b in buildingsIn(p.scene)" :key="b.id ?? ''" :value="b.id ?? ''">{{ buildingOption(b) }}</option>
                </select>
              </td>
              <td v-if="view === 'props'">{{ buildingLabel(p.building) }}</td>
              <td>
                <button v-if="focusOf(p.spot)" class="link" title="Frame it in the viewport" @click="focus(p.spot)">{{ spotText(p.spot) }}</button>
                <span v-else>{{ spotText(p.spot) }}</span>
              </td>
              <td class="why" :title="p.why ?? ''" @click="toggle(p)">{{ p.why || '' }}</td>
              <td class="center"><input type="checkbox" :checked="!!p.locked" @change="onLocked(p, $event)" /></td>
            </tr>
            <tr v-if="expanded.has(keyOf(p))" class="detail">
              <td />
              <td :colspan="view === 'characters' ? 8 : 7">
                <div v-if="p.why" class="why-full"><span class="dim">Why: </span>{{ p.why }}</div>

                <!-- The spot -->
                <div class="spot-row">
                  <span class="dim">Spot now:</span>
                  <template v-if="spotEdit && spotEdit.key === keyOf(p)">
                    <select v-model="spotEdit.scene" title="Scene">
                      <option v-if="spotEdit.scene && !scenes.includes(spotEdit.scene)" :value="spotEdit.scene">{{ sceneName(spotEdit.scene) }}</option>
                      <option v-for="s in scenes" :key="s" :value="s">{{ sceneName(s) }}</option>
                    </select>
                    <select v-model="spotEdit.mode">
                      <option value="building">in a building</option>
                      <option value="marker">at a marker</option>
                      <option value="xy">at x, y</option>
                    </select>
                    <template v-if="spotEdit.mode === 'building'">
                      <select v-model="spotEdit.building">
                        <option value="">— pick —</option>
                        <option v-for="b in buildingsIn(spotEdit.scene)" :key="b.id ?? ''" :value="b.id ?? ''">{{ buildingOption(b) }}</option>
                      </select>
                      <label class="row"><input v-model="spotEdit.inside" type="checkbox" /> inside</label>
                    </template>
                    <input v-else-if="spotEdit.mode === 'marker'" v-model="spotEdit.marker" placeholder="marker" />
                    <template v-else>
                      <input v-model.number="spotEdit.x" type="number" class="num" placeholder="x" />
                      <input v-model.number="spotEdit.y" type="number" class="num" placeholder="y" />
                    </template>
                    <button class="primary" @click="saveSpot(p)">Set</button>
                    <button @click="spotEdit = null">Cancel</button>
                  </template>
                  <template v-else>
                    <button v-if="focusOf(p.spot)" class="link" @click="focus(p.spot)">{{ spotText(p.spot) }}</button>
                    <span v-else>{{ spotText(p.spot) }}</span>
                    <button class="small" @click="editSpot(p)">Change…</button>
                  </template>
                </div>

                <!-- The whereabouts over the story -->
                <div class="wb-head">
                  <strong>Whereabouts</strong>
                  <template v-if="!(wbEdit && wbEdit.key === keyOf(p))">
                    <button class="small" @click="editWhereabouts(p)">Edit…</button>
                  </template>
                  <template v-else>
                    <button class="small" @click="addRow(p)">+ Add</button>
                    <button class="small primary" @click="saveWhereabouts(p)">Save</button>
                    <button class="small" @click="wbEdit = null">Cancel</button>
                  </template>
                </div>

                <table v-if="wbEdit && wbEdit.key === keyOf(p)" class="wb editing">
                  <thead><tr><th>From</th><th>Until</th><th>Where</th><th>Why</th><th>Status</th><th /></tr></thead>
                  <tbody>
                    <tr v-for="(r, i) in wbEdit.rows" :key="r.id">
                      <td v-for="side in (['from', 'until'] as const)" :key="side" class="when-edit">
                        <select v-model="r[side].kind">
                          <option v-if="side === 'from'" value="start">start</option>
                          <option value="quest">quest stage</option>
                          <option value="event">event</option>
                          <option v-if="side === 'until'" value="end">the end</option>
                        </select>
                        <template v-if="r[side].kind === 'quest'">
                          <input v-model="r[side].quest" placeholder="quest" />
                          <input v-model="r[side].stage" class="num" placeholder="stage" />
                        </template>
                        <input v-else-if="r[side].kind === 'event'" v-model="r[side].event" placeholder="event" />
                      </td>
                      <td class="where-edit">
                        <select v-model="r.scene" title="Scene">
                          <option v-if="r.scene && !scenes.includes(r.scene)" :value="r.scene">{{ sceneName(r.scene) }}</option>
                          <option v-for="s in scenes" :key="s" :value="s">{{ sceneName(s) }}</option>
                        </select>
                        <select v-model="r.building" title="Building">
                          <option value="">— no building —</option>
                          <option v-for="b in buildingsIn(r.scene)" :key="b.id ?? ''" :value="b.id ?? ''">{{ buildingOption(b) }}</option>
                        </select>
                        <input v-model="r.marker" placeholder="marker" title="Marker" />
                        <input v-model.number="r.x" type="number" class="num" placeholder="x" />
                        <input v-model.number="r.y" type="number" class="num" placeholder="y" />
                      </td>
                      <td><input v-model="r.why" class="why-input" placeholder="why" /></td>
                      <td><span :class="['status', r.status]">{{ r.status }}</span></td>
                      <td class="row-tools">
                        <button class="small" title="Earlier" :disabled="i === 0" @click="moveRow(i, -1)">↑</button>
                        <button class="small" title="Later" :disabled="i === wbEdit.rows.length - 1" @click="moveRow(i, 1)">↓</button>
                        <button class="small" title="Remove" @click="removeRow(i)">✕</button>
                      </td>
                    </tr>
                    <tr v-if="!wbEdit.rows.length"><td colspan="6" class="dim">No whereabouts: press + Add.</td></tr>
                  </tbody>
                </table>
                <table v-else-if="(p.whereabouts ?? []).length" class="wb">
                  <thead><tr><th>From</th><th>Until</th><th>Where</th><th>Why</th><th>Status</th></tr></thead>
                  <tbody>
                    <tr v-for="(w, i) in (p.whereabouts ?? [])" :key="w?.id ?? i" :class="{ clickable: !!focusOf(w) }"
                        :title="focusOf(w) ? 'Frame it in the viewport' : ''" @click="focus(w)">
                      <td>{{ whenText(w?.from) }}</td>
                      <td>{{ whenText(w?.until, true) }}</td>
                      <td :title="w?.marker ? 'Its marker: ' + w.marker : ''">{{ whereText(w) }}<span v-if="w?.marker" class="dim"> · marker {{ w.marker }}</span></td>
                      <td class="why" :title="w?.why ?? ''">{{ w?.why || '' }}</td>
                      <td><span :class="['status', STATUS.includes(w?.status ?? '') ? w?.status : 'todo']"
                                :title="w?.req ? 'Requirement ' + w.req : ''">{{ w?.status || 'todo' }}</span></td>
                    </tr>
                  </tbody>
                </table>
                <p v-else class="dim small">No whereabouts: it stays at its spot all story long.</p>
              </td>
            </tr>
          </template>
          <tr v-if="!rows.length"><td :colspan="view === 'characters' ? 9 : 8" class="dim">Nothing matches.</td></tr>
        </tbody>
      </table>
    </div>
  </div>
</template>

<style scoped>
.placements {
  display: flex;
  flex-direction: column;
  flex: 1;
  min-height: 0;
  overflow: hidden;
}
* {
  border-radius: 0;
}
.toolbar {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 6px;
  padding: 6px 12px;
  border-bottom: 1px solid #333;
  flex-shrink: 0;
}
.toolbar .filter {
  width: 160px;
}
.spacer {
  flex: 1;
}
.progress {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  color: #f9e2af;
  max-width: 320px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.spinner {
  width: 10px;
  height: 10px;
  border: 2px solid #45475a;
  border-top-color: #89b4fa;
  animation: spin 0.8s linear infinite;
  flex-shrink: 0;
}
@keyframes spin {
  to { transform: rotate(360deg); }
}
.seg {
  display: flex;
}
.stages {
  display: flex;
  flex: 1;
  min-height: 0;
  gap: 8px;
}
.stage-list {
  width: 280px;
  flex: none;
  overflow-y: auto;
  border-right: 1px solid #262637;
  padding: 0 6px 12px 12px;
}
.stages > .table-wrap {
  flex: 1;
}
.quest {
  margin-bottom: 10px;
}
.quest-name {
  font-weight: 600;
  margin: 2px 0 4px;
}
button.stage {
  display: block;
  width: 100%;
  text-align: left;
  margin: 0 0 2px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
button.stage.active {
  border-color: #89b4fa;
  background: #313244;
  color: #cdd6f4;
}
.stage-head {
  padding: 4px 2px 6px;
}
h4.sub {
  margin: 12px 0 4px;
  font-size: 12px;
  font-weight: 600;
}
.seg button {
  margin-left: -1px;
}
.seg button.active {
  border-color: #89b4fa;
  color: #cdd6f4;
  background: #313244;
  position: relative;
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
button {
  cursor: pointer;
}
button:hover:not(:disabled) {
  border-color: #89b4fa;
}
button:disabled {
  opacity: 0.5;
  cursor: default;
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
  padding: 0;
  text-align: left;
}
button.link:hover {
  text-decoration: underline;
}
button.small {
  font-size: 0.9em;
  padding: 1px 6px;
}
.dim {
  color: #7f849c;
}
.small {
  font-size: 0.9em;
}
.error {
  color: #f38ba8;
  margin: 6px 12px;
}
.mono {
  font-family: ui-monospace, Consolas, monospace;
}
.num {
  width: 64px;
}
.row {
  display: inline-flex;
  align-items: center;
  gap: 4px;
}
.empty {
  padding: 16px 12px;
}
.table-wrap {
  flex: 1;
  overflow: auto;
  padding: 0 12px 12px;
}
.grid-table {
  width: 100%;
  border-collapse: collapse;
}
.grid-table th {
  position: sticky;
  top: 0;
  z-index: 1;
  background: #181825;
  text-align: left;
  font-weight: 600;
  color: #a6adc8;
  padding: 6px 6px 4px;
  border-bottom: 1px solid #45475a;
  white-space: nowrap;
}
.grid-table td {
  padding: 3px 6px;
  border-bottom: 1px solid #262637;
  vertical-align: middle;
}
.grid-table > tbody > tr:hover:not(.detail) {
  background: #1e1e2e;
}
.grid-table > tbody > tr.open {
  background: #1e1e2e;
}
.grid-table select {
  max-width: 220px;
}
.tw {
  width: 18px;
  padding: 0 !important;
}
.twisty {
  border: 0;
  background: none;
  color: #a6adc8;
  padding: 0 4px;
}
td.why {
  max-width: 260px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  color: #a6adc8;
  cursor: pointer;
}
td.center {
  text-align: center;
}
tr.detail > td {
  background: #1b1b2b;
  padding: 8px 6px 10px;
  border-bottom: 1px solid #45475a;
}
.why-full {
  margin-bottom: 6px;
  white-space: pre-wrap;
}
.spot-row,
.wb-head {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 6px;
  margin: 4px 0;
}
.wb-head {
  margin-top: 10px;
}
table.wb {
  width: 100%;
  border-collapse: collapse;
  margin-top: 4px;
  border-left: 2px solid #45475a;
}
table.wb th {
  text-align: left;
  font-weight: 600;
  color: #7f849c;
  padding: 3px 6px;
  border-bottom: 1px solid #333;
}
table.wb td {
  padding: 3px 6px;
  border-bottom: 1px solid #262637;
  vertical-align: middle;
}
table.wb tr.clickable {
  cursor: pointer;
}
table.wb tr.clickable:hover {
  background: #313244;
}
.when-edit,
.where-edit {
  white-space: nowrap;
}
.when-edit input,
.when-edit select,
.where-edit input,
.where-edit select {
  margin-right: 3px;
}
.when-edit input:not(.num) {
  width: 110px;
}
.where-edit input:not(.num) {
  width: 80px;
}
.where-edit select {
  max-width: 180px;
}
.why-input {
  width: 100%;
  min-width: 140px;
}
.row-tools {
  white-space: nowrap;
}
.status {
  display: inline-block;
  padding: 0 6px;
  font-size: 0.9em;
  border: 1px solid currentColor;
  text-transform: uppercase;
  letter-spacing: 0.03em;
}
.status.todo {
  color: #9399b2;
}
.status.built {
  color: #89b4fa;
}
.status.failed {
  color: #f38ba8;
}
.status.verified {
  color: #a6e3a1;
}
</style>
