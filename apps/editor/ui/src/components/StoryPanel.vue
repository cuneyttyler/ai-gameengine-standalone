<script setup lang="ts">
// Dialogue and quests (development_plan.md 3.2c, format in docs/systems/story.md).
// The host lists dialogue/*.json and quests/*.json with problems and what
// can be referenced (World Bible entries, prefabs); this panel edits one file
// at a time as a draft and saves it back through the engine's own reader and
// writer.
import { sceneList } from '../scenes'
import { computed, onMounted, reactive, ref } from 'vue'
import { onHostMessage, sendToHost } from '../ipc'
import { confirmDialog } from '../dialogs'
import ConditionList, { type Condition } from './story/ConditionList.vue'
import EffectList, { type Effect } from './story/EffectList.vue'

interface Choice {
  text: string
  next: string
  if?: Condition[]
  do?: Effect[]
  check?: { attribute: string; difficulty: number } // a skill check (15.3)
  fail?: string // where a failed check goes
}
interface Node {
  id: string
  speaker?: string
  text: string
  choices?: Choice[]
  next?: string
  do?: Effect[]
  portrait?: string
  voice?: string
}
interface DialogueContent {
  speaker: string
  start: string
  starts?: { if: Condition[]; node: string }[]
  nodes: Node[]
}
interface Objective {
  id: string
  text: string
  type: string
  target: string
  count?: number
  scene?: string // only counts in this scene (6.9)
  optional?: boolean
  item?: string // deliver
  to?: string // escort: the marker
  seconds?: number // survive, defend
}
interface Stage {
  index: number
  journal?: string
  objectives: Objective[]
  onEnter?: Effect[]
  branches?: { if: Condition[]; stage: number }[]
  next?: number
  ends?: string
}
interface Alias {
  id: string
  kind: string
  fill: string
}
interface Rewards {
  experience?: number
  gold?: number
  items?: string
  faction?: string
  reputation?: number
}
// Quests v2 (15.1). The panel always edits stages; a quest with one plain
// objective per stage is saved in the older "objectives" shape.
interface QuestContent {
  name: string
  description: string
  giver: string
  objectives?: Objective[]
  stages: Stage[]
  aliases: Alias[]
  rewards: Rewards
  onComplete?: Effect[]
  onFail?: Effect[]
}
interface ScheduleEntry {
  from: string
  activity: string
  place: string
  radius?: number
  scene?: string // where this happens (6.9)
}
interface ScheduleContent {
  name: string
  entries: ScheduleEntry[]
}
interface FileEntry<T> {
  id: string
  file: string
  content: T
}

const dialogues = ref<FileEntry<DialogueContent>[]>([])
const quests = ref<FileEntry<QuestContent>[]>([])
const schedules = ref<FileEntry<ScheduleContent>[]>([])
const markers = ref<string[]>([])
const problems = ref<{ file: string; message: string }[]>([])
const world = ref<{ key: string; name: string; kind: string }[]>([])
const prefabs = ref<string[]>([])
const usedBy = ref<Record<string, string[]>>({})

// The draft being edited.
const kind = ref<'dialogue' | 'quest' | 'schedule' | null>(null)
const draftId = ref('')
const originalId = ref('')
const dialogue = reactive<DialogueContent>({ speaker: '', start: '', starts: [], nodes: [] })
const quest = reactive<QuestContent>({
  name: '',
  description: '',
  giver: '',
  stages: [],
  aliases: [],
  rewards: {},
  onComplete: [],
  onFail: [],
})
const dialogueView = ref<'list' | 'graph'>('list')
const selectedNode = ref('')
const schedule = reactive<ScheduleContent>({ name: '', entries: [] })
const saved = ref('')
const saveError = ref('')
let reselect: string | null = null

onHostMessage((message) => {
  if (message.type === 'story:list') {
    dialogues.value = message.dialogues as FileEntry<DialogueContent>[]
    quests.value = message.quests as FileEntry<QuestContent>[]
    schedules.value = (message.schedules as FileEntry<ScheduleContent>[]) ?? []
    markers.value = (message.markers as string[]) ?? []
    problems.value = message.problems as { file: string; message: string }[]
    world.value = message.world as { key: string; name: string; kind: string }[]
    prefabs.value = message.prefabs as string[]
    usedBy.value = message.usedBy as Record<string, string[]>
    if (reselect) {
      const file = reselect
      reselect = null
      open(file, true)
    }
  } else if (message.type === 'story:saved') {
    saveError.value = (message.error as string | null) ?? ''
    if (!saveError.value) reselect = message.file as string
  }
})

onMounted(() => sendToHost({ type: 'story:request' }))

const characters = computed(() => world.value.filter((e) => e.kind === 'character'))
const questIds = computed(() => quests.value.map((q) => q.id))
const folderOf = { dialogue: 'dialogue', quest: 'quests', schedule: 'schedules' } as const
const currentFile = computed(() => `${folderOf[kind.value ?? 'dialogue']}/${originalId.value || draftId.value}.json`)

function stagesIn(content: QuestContent | undefined): Stage[] {
  if (!content) return []
  if (content.stages?.length) return content.stages
  return (content.objectives ?? []).map((o, i) => ({ index: (i + 1) * 10, objectives: [o] }))
}

function objectivesOf(id: string): string[] {
  const content = id === originalId.value && kind.value === 'quest' ? quest : quests.value.find((q) => q.id === id)?.content
  return stagesIn(content).flatMap((st) => st.objectives.map((o) => o.id))
}

function stagesOf(id: string): number[] {
  const content = id === originalId.value && kind.value === 'quest' ? quest : quests.value.find((q) => q.id === id)?.content
  return stagesIn(content).map((st) => st.index)
}

// Everything the editors bind to, present (the engine drops what's empty).
function normalizeQuest(content: QuestContent): QuestContent {
  content.stages = stagesIn(content).map((st) => ({
    ...st,
    journal: st.journal ?? (content.stages?.length ? '' : st.objectives[0]?.text ?? ''),
    onEnter: st.onEnter ?? [],
    branches: st.branches ?? [],
  }))
  delete content.objectives
  content.aliases ??= []
  content.rewards ??= {}
  content.onComplete ??= []
  content.onFail ??= []
  for (const st of content.stages) for (const b of st.branches ?? []) b.if ??= []
  return content
}

// The file to save: v2 with stages, or the older shape when that says it all.
function questFile(content: QuestContent): Record<string, unknown> {
  const copy = JSON.parse(JSON.stringify(content)) as QuestContent
  const plain =
    copy.aliases.length === 0 &&
    !copy.onFail?.length &&
    Object.values(copy.rewards).every((v) => !v) &&
    copy.stages.every(
      (st, i) =>
        st.index === (i + 1) * 10 &&
        st.objectives.length === 1 &&
        !st.objectives[0]!.optional &&
        (!st.journal || st.journal === st.objectives[0]!.text) &&
        !st.onEnter?.length &&
        !st.branches?.length &&
        !st.next &&
        !st.ends,
    )
  const out: Record<string, unknown> = { name: copy.name, description: copy.description, giver: copy.giver }
  if (plain) out.objectives = copy.stages.map((st) => st.objectives[0])
  else {
    out.stages = copy.stages
    if (copy.aliases.length) out.aliases = copy.aliases
    out.rewards = copy.rewards
  }
  out.onComplete = copy.onComplete
  if (copy.onFail?.length) out.onFail = copy.onFail
  return out
}

function problemsFor(file: string): string[] {
  return problems.value.filter((p) => p.file === file).map((p) => p.message)
}

function nameOf(key: string): string {
  return world.value.find((e) => e.key === key)?.name ?? key
}

// Every choice and node gets its condition/effect lists up front, so the
// editors can bind to them (the engine drops empty ones when saving).
function normalize(content: DialogueContent): DialogueContent {
  for (const node of content.nodes) {
    node.do ??= []
    for (const choice of node.choices ?? []) {
      choice.if ??= []
      choice.do ??= []
    }
    node.portrait ??= ''
    node.voice ??= ''
  }
  content.starts ??= []
  return content
}

function draftContent(): DialogueContent | QuestContent | ScheduleContent {
  return kind.value === 'dialogue' ? dialogue : kind.value === 'quest' ? quest : schedule
}

function snapshot(): string {
  return JSON.stringify({ id: draftId.value, d: draftContent() })
}
const dirty = computed(() => kind.value !== null && snapshot() !== saved.value)

async function confirmDiscard(): Promise<boolean> {
  return !dirty.value || (await confirmDialog({ message: 'Discard your unsaved changes?', confirmLabel: 'Discard', danger: true }))
}

async function open(file: string, force = false): Promise<void> {
  if (!force && !(await confirmDiscard())) return
  const d = dialogues.value.find((x) => x.file === file)
  const q = quests.value.find((x) => x.file === file)
  const sc = schedules.value.find((x) => x.file === file)
  saveError.value = ''
  if (sc) {
    kind.value = 'schedule'
    draftId.value = originalId.value = sc.id
    const copy = JSON.parse(JSON.stringify(sc.content)) as ScheduleContent
    Object.assign(schedule, { name: copy.name, entries: copy.entries })
    saved.value = snapshot()
    return
  }
  if (d) {
    kind.value = 'dialogue'
    draftId.value = originalId.value = d.id
    const copy = normalize(JSON.parse(JSON.stringify(d.content)) as DialogueContent)
    Object.assign(dialogue, { speaker: copy.speaker, start: copy.start, starts: copy.starts ?? [], nodes: copy.nodes })
  } else if (q) {
    kind.value = 'quest'
    draftId.value = originalId.value = q.id
    const copy = normalizeQuest(JSON.parse(JSON.stringify(q.content)) as QuestContent)
    Object.assign(quest, copy)
  } else {
    return
  }
  saved.value = snapshot()
}

async function newDialogue(): Promise<void> {
  if (!(await confirmDiscard())) return
  kind.value = 'dialogue'
  draftId.value = ''
  originalId.value = ''
  Object.assign(dialogue, {
    speaker: characters.value[0]?.key ?? '',
    start: 'start',
    starts: [],
    nodes: [{ id: 'start', text: '', next: '', do: [] }],
  })
  saved.value = ''
  saveError.value = ''
}

async function newQuest(): Promise<void> {
  if (!(await confirmDiscard())) return
  kind.value = 'quest'
  draftId.value = ''
  originalId.value = ''
  Object.assign(quest, {
    name: '',
    description: '',
    giver: characters.value[0]?.key ?? '',
    stages: [
      { index: 10, journal: '', objectives: [{ id: 'first', text: '', type: 'defeat', target: 'Enemy', count: 1 }], onEnter: [], branches: [] },
    ],
    aliases: [],
    rewards: {},
    onComplete: [],
    onFail: [],
  })
  saved.value = ''
  saveError.value = ''
}

async function newSchedule(): Promise<void> {
  if (!(await confirmDiscard())) return
  kind.value = 'schedule'
  draftId.value = ''
  originalId.value = ''
  const place = markers.value[0] ?? ''
  Object.assign(schedule, {
    name: '',
    entries: [
      { from: '07:00', activity: 'wander', place, radius: 2 },
      { from: '21:00', activity: 'sleep', place },
    ],
  })
  saved.value = ''
  saveError.value = ''
}

function save(): void {
  saveError.value = ''
  sendToHost({
    type: 'story:save',
    kind: kind.value,
    id: draftId.value.trim(),
    originalId: originalId.value,
    content: kind.value === 'quest' ? questFile(quest) : JSON.parse(JSON.stringify(draftContent())),
  })
}

async function remove(): Promise<void> {
  const file = currentFile.value
  const users = usedBy.value[file]
  const warning = users?.length ? `\n\nUsed by ${users.join(', ')} in the open scene.` : ''
  if (!originalId.value) return
  if (!(await confirmDialog({ message: `Delete ${file}?${warning}`, confirmLabel: 'Delete', danger: true }))) return
  sendToHost({ type: 'story:delete', file })
  kind.value = null
}

function onKeydown(event: KeyboardEvent): void {
  if (event.ctrlKey && event.key.toLowerCase() === 's') {
    event.preventDefault()
    event.stopPropagation()
    save()
  }
}

// --- Dialogue helpers ------------------------------------------------------

const nodeIds = computed(() => dialogue.nodes.map((n) => n.id))

function addNode(): void {
  let n = dialogue.nodes.length + 1
  while (nodeIds.value.includes(`node-${n}`)) n++
  dialogue.nodes.push({ id: `node-${n}`, text: '', next: '', do: [] })
}

// Renaming a node updates every link to it.
function renameNode(node: Node, id: string): void {
  const old = node.id
  node.id = id
  if (!old) return
  if (dialogue.start === old) dialogue.start = id
  for (const s of dialogue.starts ?? []) if (s.node === old) s.node = id
  for (const n of dialogue.nodes) {
    if (n.next === old) n.next = id
    for (const c of n.choices ?? []) {
      if (c.next === old) c.next = id
      if (c.fail === old) c.fail = id
    }
  }
  if (selectedNode.value === old) selectedNode.value = id
}

function useCheck(choice: Choice, on: boolean): void {
  if (on) choice.check = { attribute: 'speech', difficulty: 5 }
  else {
    delete choice.check
    delete choice.fail
  }
}

// --- The dialogue graph (15.3): nodes in columns by distance from the start --

const graph = computed(() => {
  const depth = new Map<string, number>()
  const queue = [dialogue.start, ...(dialogue.starts ?? []).map((s) => s.node)]
  for (const id of queue) if (!depth.has(id)) depth.set(id, 0)
  while (queue.length) {
    const id = queue.shift()!
    const node = dialogue.nodes.find((n) => n.id === id)
    if (!node) continue
    const links = node.choices ? node.choices.flatMap((c) => [c.next, c.fail ?? '']) : [node.next ?? '']
    for (const to of links) {
      if (to && !depth.has(to)) {
        depth.set(to, depth.get(id)! + 1)
        queue.push(to)
      }
    }
  }
  const columns: string[][] = []
  for (const node of dialogue.nodes) {
    const d = depth.get(node.id) ?? columns.length // unreachable: a column of its own at the end
    ;(columns[d] ??= []).push(node.id)
  }
  const width = 170
  const height = 54
  const pos = new Map<string, { x: number; y: number }>()
  columns.forEach((col, x) => col?.forEach((id, y) => pos.set(id, { x: 12 + x * (width + 46), y: 12 + y * (height + 18) })))
  const edges: { from: string; to: string; label: string; fail: boolean }[] = []
  for (const node of dialogue.nodes) {
    if (node.choices) {
      node.choices.forEach((c, i) => {
        if (c.next) edges.push({ from: node.id, to: c.next, label: String(i + 1), fail: false })
        if (c.fail) edges.push({ from: node.id, to: c.fail, label: `${i + 1}✗`, fail: true })
      })
    } else if (node.next) edges.push({ from: node.id, to: node.next, label: '', fail: false })
  }
  let w = 0
  let h = 0
  for (const p of pos.values()) {
    w = Math.max(w, p.x + width + 12)
    h = Math.max(h, p.y + height + 12)
  }
  return { pos, edges, width, height, w, h }
})

function edgePath(from: string, to: string): string {
  const a = graph.value.pos.get(from)
  const b = graph.value.pos.get(to)
  if (!a || !b) return ''
  const { width, height } = graph.value
  const x1 = a.x + width
  const y1 = a.y + height / 2
  const x2 = b.x
  const y2 = b.y + height / 2
  if (x2 > x1) return `M${x1},${y1} C${x1 + 30},${y1} ${x2 - 30},${y2} ${x2},${y2}`
  // Back or sideways: around below.
  const below = Math.max(a.y, b.y) + height + 10
  return `M${a.x + width / 2},${a.y + height} C${a.x + width / 2},${below} ${b.x + width / 2},${below} ${b.x + width / 2},${b.y + height}`
}

function edgeLabelAt(from: string, to: string): { x: number; y: number } {
  const a = graph.value.pos.get(from)
  const b = graph.value.pos.get(to)
  if (!a || !b) return { x: 0, y: 0 }
  return { x: (a.x + graph.value.width + b.x) / 2, y: (a.y + b.y + graph.value.height) / 2 - 3 }
}

const shownNodes = computed(() =>
  dialogueView.value === 'graph' ? dialogue.nodes.filter((n) => n.id === selectedNode.value) : dialogue.nodes,
)

function selectNode(id: string): void {
  selectedNode.value = id
}

function addNodeInGraph(): void {
  addNode()
  selectedNode.value = dialogue.nodes[dialogue.nodes.length - 1]!.id
}

// --- Quest stages ----------------------------------------------------------

function addStage(): void {
  const last = quest.stages[quest.stages.length - 1]?.index ?? 0
  quest.stages.push({
    index: Math.floor(last / 10) * 10 + 10,
    journal: '',
    objectives: [{ id: `step-${objectivesOf(originalId.value).length + quest.stages.length + 1}`, text: '', type: 'talk', target: quest.giver }],
    onEnter: [],
    branches: [],
  })
}

const stageIndices = computed(() => quest.stages.map((st) => st.index))
const allObjectiveIds = computed(() => quest.stages.flatMap((st) => st.objectives.map((o) => o.id)))

function useChoices(node: Node, on: boolean): void {
  if (on) {
    node.choices = [{ text: '', next: node.next ?? '', if: [], do: [] }]
    delete node.next
  } else {
    node.next = node.choices?.[0]?.next ?? ''
    delete node.choices
  }
}

function goTo(id: string): void {
  if (dialogueView.value === 'graph') {
    selectedNode.value = id
    return
  }
  document.getElementById(`story-node-${id}`)?.scrollIntoView({ behavior: 'smooth', block: 'center' })
}

// --- Quest helpers -----------------------------------------------------------

// A scene's markers: the open scene's as they are now, others' as saved.
function markersOf(scene: string | undefined): string[] {
  if (!scene) return markers.value
  const entry = sceneList.scenes.find((s) => s.path === scene)
  if (entry?.open) return markers.value
  return entry?.markers ?? []
}

// Objective types whose target is typed in (an item id, a Name) rather than picked.
const freeTargets = ['use', 'examine', 'have']

function aliasTargets(): { value: string; label: string }[] {
  return quest.aliases.filter((a) => a.id).map((a) => ({ value: `@${a.id}`, label: `@${a.id} (${a.fill || '?'})` }))
}

function targetsFor(type: string, scene?: string): { value: string; label: string }[] {
  if (['talk', 'deliver', 'escort', 'defend'].includes(type)) {
    return [...aliasTargets(), ...world.value.filter((e) => e.kind === 'character').map((e) => ({ value: e.key, label: e.name }))]
  }
  if (type === 'reach') {
    const names = scene ? markersOf(scene) : [...new Set(sceneList.scenes.flatMap((s) => (s.open ? markers.value : s.markers)))]
    return [...(scene ? [{ value: '', label: '(arrive in the scene)' }] : []), ...names.map((m) => ({ value: m, label: m }))]
  }
  const list = prefabs.value.map((p) => ({ value: p, label: p }))
  return type === 'defeat' ? [{ value: 'Enemy', label: 'Any enemy' }, ...list] : list
}
</script>

<template>
  <div class="story ed-form">
    <aside class="list">
      <div class="group-head">
        <span>Dialogue</span><button class="add" title="New dialogue" @click="newDialogue">+</button>
      </div>
      <button
        v-for="d in dialogues"
        :key="d.file"
        class="row"
        :class="{ active: kind === 'dialogue' && originalId === d.id }"
        :title="usedBy[d.file]?.length ? 'Talk on ' + usedBy[d.file]!.join(', ') : 'Not used by anything in the open scene'"
        @click="open(d.file)"
      >
        <span class="name">{{ d.id }}</span>
        <span v-if="problemsFor(d.file).length" class="warn">⚠</span>
        <span v-else-if="usedBy[d.file]?.length" class="muted small">{{ usedBy[d.file]!.length }}×</span>
      </button>
      <div class="group-head">
        <span>Routines</span><button class="add" title="New daily routine" @click="newSchedule">+</button>
      </div>
      <button
        v-for="r in schedules"
        :key="r.file"
        class="row"
        :class="{ active: kind === 'schedule' && originalId === r.id }"
        :title="usedBy[r.file]?.length ? 'Routine on ' + usedBy[r.file]!.join(', ') : 'Nobody in the open scene follows it'"
        @click="open(r.file)"
      >
        <span class="name">{{ r.content.name || r.id }}</span>
        <span v-if="problemsFor(r.file).length" class="warn">⚠</span>
      </button>
      <div class="group-head">
        <span>Quests</span><button class="add" title="New quest" @click="newQuest">+</button>
      </div>
      <button
        v-for="q in quests"
        :key="q.file"
        class="row"
        :class="{ active: kind === 'quest' && originalId === q.id }"
        @click="open(q.file)"
      >
        <span class="name">{{ q.content.name || q.id }}</span>
        <span v-if="problemsFor(q.file).length" class="warn">⚠</span>
      </button>
    </aside>

    <section v-if="kind === null" class="editor empty">
      <p>
        Conversations and quests. Give an NPC a <b>Talk</b> component (in the Inspector) pointing at a dialogue, and
        players talk to them with <b>E</b>.
      </p>
      <p class="muted">Files live in <code>dialogue/</code> and <code>quests/</code>; the format is in docs/systems/story.md.</p>
    </section>

    <section v-else class="editor" @keydown="onKeydown">
      <ul v-if="problemsFor(currentFile).length || saveError" class="problems">
        <li v-for="m in problemsFor(currentFile)" :key="m">{{ m }}</li>
        <li v-if="saveError">{{ saveError }}</li>
      </ul>

      <!-- Dialogue ------------------------------------------------------------>
      <template v-if="kind === 'dialogue'">
        <div class="form-row">
          <label>File</label>
          <span class="muted">dialogue/</span><input v-model="draftId" class="id" spellcheck="false" /><span class="muted">.json</span>
          <label class="inline">Speaker</label>
          <select v-model="dialogue.speaker">
            <option v-for="c in characters" :key="c.key" :value="c.key">{{ c.name }}</option>
            <option v-if="dialogue.speaker && !characters.some((c) => c.key === dialogue.speaker)" :value="dialogue.speaker">
              {{ dialogue.speaker }}
            </option>
          </select>
          <label class="inline">Starts at</label>
          <select v-model="dialogue.start">
            <option v-for="id in nodeIds" :key="id" :value="id">{{ id }}</option>
          </select>
          <span class="spacer"></span>
          <div class="view-switch">
            <button :class="{ on: dialogueView === 'list' }" @click="dialogueView = 'list'">List</button>
            <button
              :class="{ on: dialogueView === 'graph' }"
              @click="dialogueView = 'graph'; selectedNode = selectedNode || dialogue.start"
            >
              Graph
            </button>
          </div>
        </div>
        <div v-if="dialogueView === 'graph'" class="graph">
          <svg :width="graph.w" :height="graph.h">
            <defs>
              <marker id="story-arrow" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto">
                <path d="M0,0 L8,4 L0,8 z" fill="#6b7280" />
              </marker>
            </defs>
            <g v-for="(e, i) in graph.edges" :key="'e' + i">
              <path :d="edgePath(e.from, e.to)" class="edge" :class="{ fail: e.fail }" marker-end="url(#story-arrow)" />
              <text v-if="e.label" :x="edgeLabelAt(e.from, e.to).x" :y="edgeLabelAt(e.from, e.to).y" class="edge-label">
                {{ e.label }}
              </text>
            </g>
            <g
              v-for="node in dialogue.nodes"
              :key="node.id"
              class="gnode"
              :class="{ on: node.id === selectedNode, start: node.id === dialogue.start }"
              @click="selectNode(node.id)"
            >
              <rect
                :x="graph.pos.get(node.id)?.x"
                :y="graph.pos.get(node.id)?.y"
                :width="graph.width"
                :height="graph.height"
              />
              <text :x="(graph.pos.get(node.id)?.x ?? 0) + 8" :y="(graph.pos.get(node.id)?.y ?? 0) + 17" class="gid">
                {{ node.id }}
              </text>
              <text :x="(graph.pos.get(node.id)?.x ?? 0) + 8" :y="(graph.pos.get(node.id)?.y ?? 0) + 36" class="gtext">
                {{ node.text.length > 24 ? node.text.slice(0, 23) + '…' : node.text || '(no text)' }}
              </text>
            </g>
          </svg>
          <button class="small-button" @click="addNodeInGraph">+ Node</button>
        </div>
        <div class="starts">
          <div v-for="(s, i) in dialogue.starts" :key="i" class="start-row">
            <span class="muted">Instead start at</span>
            <select v-model="s.node">
              <option v-for="id in nodeIds" :key="id" :value="id">{{ id }}</option>
            </select>
            <ConditionList :conditions="s.if" :quests="questIds" :stages-of="stagesOf" />
            <button title="Remove" @click="dialogue.starts!.splice(i, 1)">×</button>
          </div>
          <button class="small-button" @click="dialogue.starts!.push({ if: [], node: dialogue.start })">
            + Different start when…
          </button>
        </div>

        <div v-for="node in shownNodes" :id="`story-node-${node.id}`" :key="dialogue.nodes.indexOf(node)" class="node">
          <div class="node-head">
            <input
              :value="node.id"
              class="id"
              spellcheck="false"
              title="Node id (links update when you rename it)"
              @change="renameNode(node, ($event.target as HTMLInputElement).value.trim())"
            />
            <select v-model="node.speaker" title="Who says this line">
              <option :value="undefined">{{ nameOf(dialogue.speaker) }}</option>
              <option value="player">The player</option>
              <option v-for="c in characters.filter((c) => c.key !== dialogue.speaker)" :key="c.key" :value="c.key">
                {{ c.name }}
              </option>
            </select>
            <span v-if="dialogue.start === node.id" class="tag">start</span>
            <span class="spacer"></span>
            <label class="toggle">
              <input type="checkbox" :checked="!!node.choices" @change="useChoices(node, ($event.target as HTMLInputElement).checked)" />
              choices
            </label>
            <button title="Delete this node" @click="dialogue.nodes.splice(dialogue.nodes.indexOf(node), 1)">×</button>
          </div>
          <textarea v-model="node.text" rows="2" placeholder="What they say"></textarea>
          <div class="media-row">
            <span class="muted small">portrait</span>
            <input v-model="node.portrait" class="grow" placeholder="assets/… (optional)" spellcheck="false" />
            <span class="muted small">voice</span>
            <input v-model="node.voice" class="grow" placeholder="assets/….wav (optional)" spellcheck="false" />
          </div>
          <div v-if="!node.choices" class="next-row">
            <span class="muted">then</span>
            <select v-model="node.next">
              <option value="">end the conversation</option>
              <option v-for="id in nodeIds.filter((i) => i !== node.id)" :key="id" :value="id">go to {{ id }}</option>
            </select>
            <button v-if="node.next" title="Show that node" @click="goTo(node.next)">→</button>
          </div>
          <div v-else class="choices">
            <div v-for="(c, ci) in node.choices" :key="ci" class="choice">
              <div class="choice-row">
                <span class="num">{{ ci + 1 }}.</span>
                <input v-model="c.text" class="grow" placeholder="What the player can say" />
                <select v-model="c.next">
                  <option value="">ends</option>
                  <option v-for="id in nodeIds" :key="id" :value="id">→ {{ id }}</option>
                </select>
                <button v-if="c.next" title="Show that node" @click="goTo(c.next)">→</button>
                <button title="Remove this choice" @click="node.choices!.splice(ci, 1)">×</button>
              </div>
              <div class="choice-extra">
                <ConditionList :conditions="c.if!" :quests="questIds" :stages-of="stagesOf" />
                <div class="check-row">
                  <label class="toggle">
                    <input type="checkbox" :checked="!!c.check" @change="useCheck(c, ($event.target as HTMLInputElement).checked)" />
                    skill check
                  </label>
                  <template v-if="c.check">
                    <input v-model="c.check.attribute" class="short" spellcheck="false" title="The player's attribute" />
                    <span class="muted small">≥</span>
                    <input v-model.number="c.check.difficulty" type="number" class="count" title="Difficulty" />
                    <span class="muted small">else</span>
                    <select v-model="c.fail" title="Where a failed check goes">
                      <option :value="undefined">ends</option>
                      <option v-for="id in nodeIds" :key="id" :value="id">→ {{ id }}</option>
                    </select>
                  </template>
                </div>
                <EffectList :effects="c.do!" :quests="questIds" :objectives-of="objectivesOf" :stages-of="stagesOf" />
              </div>
            </div>
            <button class="small-button" :disabled="node.choices.length >= 9" @click="node.choices.push({ text: '', next: '', if: [], do: [] })">
              + Choice
            </button>
          </div>
          <EffectList :effects="node.do!" :quests="questIds" :objectives-of="objectivesOf" :stages-of="stagesOf" />
        </div>
        <button v-if="dialogueView === 'list'" class="small-button" @click="addNode">+ Node</button>
      </template>

      <!-- Routine ------------------------------------------------------------->
      <template v-else-if="kind === 'schedule'">
        <div class="form-row">
          <label>File</label>
          <span class="muted">schedules/</span><input v-model="draftId" class="id" spellcheck="false" /><span class="muted">.json</span>
          <label class="inline">Name</label>
          <input v-model="schedule.name" class="grow" />
        </div>
        <p class="muted small">
          Each part of the day lasts until the next one starts. Places are markers in the scene (place the Marker
          component, or ask the AI). Give someone a <b>Routine</b> component to follow it.
        </p>
        <div v-for="(e, i) in schedule.entries" :key="i" class="objective">
          <span class="muted">from</span>
          <input v-model="e.from" type="time" class="time" />
          <select v-model="e.activity">
            <option value="stay">stay at</option>
            <option value="wander">wander around</option>
            <option value="sleep">sleep at</option>
          </select>
          <select v-model="e.scene" class="scene-pick" title="Where this part of the day happens (empty: wherever they are)">
            <option :value="undefined">here</option>
            <option v-for="s in sceneList.scenes" :key="s.path" :value="s.path">in {{ s.name }}</option>
          </select>
          <select v-model="e.place">
            <option v-for="m in markersOf(e.scene)" :key="m" :value="m">{{ m }}</option>
            <option v-if="e.place && !markersOf(e.scene).includes(e.place)" :value="e.place">{{ e.place }} (missing)</option>
          </select>
          <template v-if="e.activity === 'wander'">
            <span class="muted">within</span>
            <input v-model.number="e.radius" type="number" min="1" max="20" class="count" />
            <span class="muted">cells</span>
          </template>
          <button title="Remove" @click="schedule.entries.splice(i, 1)">×</button>
        </div>
        <button
          class="small-button"
          @click="schedule.entries.push({ from: '12:00', activity: 'stay', place: markers[0] ?? '' })"
        >
          + Part of the day
        </button>
        <p v-if="markers.length === 0" class="problems">The open scene has no markers yet.</p>
      </template>

      <!-- Quest --------------------------------------------------------------->
      <template v-else>
        <div class="form-row">
          <label>File</label>
          <span class="muted">quests/</span><input v-model="draftId" class="id" spellcheck="false" /><span class="muted">.json</span>
        </div>
        <div class="form-row">
          <label>Name</label>
          <input v-model="quest.name" class="grow" />
          <label class="inline">Given by</label>
          <select v-model="quest.giver">
            <option v-for="c in characters" :key="c.key" :value="c.key">{{ c.name }}</option>
          </select>
        </div>
        <div class="form-row top">
          <label>Description</label>
          <textarea v-model="quest.description" rows="2" class="grow"></textarea>
        </div>
        <div class="section-label">Roles (aliases)</div>
        <p class="muted small">A role is written "@thief" in a target and filled here, so the quest works with whoever plays it.</p>
        <div v-for="(a, i) in quest.aliases" :key="'a' + i" class="objective">
          <span class="muted">@</span>
          <input v-model="a.id" class="id" spellcheck="false" placeholder="thief" />
          <select v-model="a.kind">
            <option value="character">character</option>
            <option value="item">item</option>
            <option value="location">location</option>
            <option value="entity">entity</option>
          </select>
          <select v-if="a.kind === 'character'" v-model="a.fill">
            <option v-for="c in characters" :key="c.key" :value="c.key">{{ c.name }}</option>
          </select>
          <input v-else v-model="a.fill" class="grow" spellcheck="false" placeholder="item id, marker or name" />
          <button title="Remove" @click="quest.aliases.splice(i, 1)">×</button>
        </div>
        <button class="small-button" @click="quest.aliases.push({ id: '', kind: 'character', fill: characters[0]?.key ?? '' })">
          + Role
        </button>

        <div class="section-label">Stages</div>
        <p class="muted small">
          A stage's objectives can be done in any order (optional ones don't hold it back). Then the first branch that
          holds picks the next stage, else "then", else the next one below.
        </p>
        <div v-for="(st, si) in quest.stages" :key="'s' + si" class="node stage">
          <div class="node-head">
            <span class="muted">Stage</span>
            <input v-model.number="st.index" type="number" class="count" title="Stage number (10, 20…)" />
            <input v-model="st.journal" class="grow" placeholder="The journal's line for this stage" />
            <select v-model="st.ends" title="The quest ends on reaching this stage">
              <option :value="undefined">goes on</option>
              <option value="complete">completes the quest</option>
              <option value="fail">fails the quest</option>
            </select>
            <button title="Remove this stage" @click="quest.stages.splice(si, 1)">×</button>
          </div>
          <template v-if="!st.ends">
            <div v-for="(o, i) in st.objectives" :key="i" class="objective">
              <input v-model="o.id" class="id" spellcheck="false" title="Objective id" />
              <input v-model="o.text" class="grow" placeholder="Shown on screen" />
              <select v-model="o.type">
                <option value="defeat">defeat</option>
                <option value="collect">collect</option>
                <option value="talk">talk to</option>
                <option value="reach">reach</option>
                <option value="deliver">deliver to</option>
                <option value="use">use</option>
                <option value="examine">examine</option>
                <option value="have">have item</option>
                <option value="survive">survive</option>
                <option value="defend">defend</option>
                <option value="escort">escort</option>
              </select>
              <input v-if="freeTargets.includes(o.type)" v-model="o.target" class="short" spellcheck="false"
                     :placeholder="o.type === 'have' ? 'item id' : 'its Name'" />
              <select v-else-if="o.type !== 'survive'" v-model="o.target">
                <option v-for="t in targetsFor(o.type, o.scene)" :key="t.value" :value="t.value">{{ t.label }}</option>
                <option v-if="o.target && !targetsFor(o.type, o.scene).some((t) => t.value === o.target)" :value="o.target">
                  {{ o.target }} (missing)
                </option>
              </select>
              <input v-if="o.type === 'deliver'" v-model="o.item" class="short" spellcheck="false" placeholder="item id" />
              <template v-if="o.type === 'escort'">
                <span class="muted small">to</span>
                <select v-model="o.to">
                  <option v-for="m in markersOf(o.scene)" :key="m" :value="m">{{ m }}</option>
                </select>
              </template>
              <template v-if="o.type === 'survive' || o.type === 'defend'">
                <input v-model.number="o.seconds" type="number" min="1" class="count" title="Seconds" />
                <span class="muted small">s</span>
              </template>
              <select v-model="o.scene" class="scene-pick" title="Only counts in this scene (empty: anywhere)">
                <option :value="undefined">anywhere</option>
                <option v-for="sc in sceneList.scenes" :key="sc.path" :value="sc.path">in {{ sc.name }}</option>
              </select>
              <input v-if="['defeat', 'collect', 'have', 'deliver'].includes(o.type)" v-model.number="o.count" type="number"
                     min="1" class="count" title="How many" />
              <label class="toggle" title="Doesn't hold the stage back">
                <input v-model="o.optional" type="checkbox" />optional
              </label>
              <button title="Remove" @click="st.objectives.splice(i, 1)">×</button>
            </div>
            <button
              class="small-button"
              @click="st.objectives.push({ id: `step-${allObjectiveIds.length + 1}`, text: '', type: 'defeat', target: 'Enemy', count: 1 })"
            >
              + Objective
            </button>
            <div v-for="(b, bi) in st.branches" :key="'b' + bi" class="start-row">
              <span class="muted">go to stage</span>
              <select v-model.number="b.stage">
                <option v-for="ix in stageIndices.filter((x) => x !== st.index)" :key="ix" :value="ix">{{ ix }}</option>
              </select>
              <ConditionList :conditions="b.if" :quests="questIds" :stages-of="stagesOf" />
              <button title="Remove" @click="st.branches!.splice(bi, 1)">×</button>
            </div>
            <div class="next-row">
              <button class="small-button" @click="st.branches!.push({ if: [], stage: stageIndices.find((x) => x > st.index) ?? st.index })">
                + Branch
              </button>
              <span class="muted small">then</span>
              <select v-model.number="st.next">
                <option :value="undefined">the next stage</option>
                <option v-for="ix in stageIndices.filter((x) => x !== st.index)" :key="ix" :value="ix">stage {{ ix }}</option>
              </select>
            </div>
          </template>
          <div class="muted small">On reaching it</div>
          <EffectList :effects="st.onEnter!" :quests="questIds" :objectives-of="objectivesOf" :stages-of="stagesOf" />
        </div>
        <button class="small-button" @click="addStage">+ Stage</button>

        <div class="section-label">Rewards</div>
        <div class="objective">
          <span class="muted small">experience</span>
          <input v-model.number="quest.rewards.experience" type="number" min="0" class="count" />
          <span class="muted small">gold</span>
          <input v-model.number="quest.rewards.gold" type="number" min="0" class="count" />
          <span class="muted small">items</span>
          <input v-model="quest.rewards.items" class="short" spellcheck="false" placeholder="potion*2, iron-sword" />
          <span class="muted small">reputation</span>
          <input v-model="quest.rewards.faction" class="short" spellcheck="false" placeholder="faction" />
          <input v-model.number="quest.rewards.reputation" type="number" class="count" />
        </div>
        <div class="section-label">When completed</div>
        <EffectList :effects="quest.onComplete!" :quests="questIds" :objectives-of="objectivesOf" :stages-of="stagesOf" />
        <div class="section-label">When failed</div>
        <EffectList :effects="quest.onFail!" :quests="questIds" :objectives-of="objectivesOf" :stages-of="stagesOf" />
      </template>

      <div class="actions">
        <button class="primary" :disabled="!draftId.trim()" title="Save (Ctrl+S)" @click="save">
          {{ originalId ? 'Save' : 'Create' }}
        </button>
        <button :disabled="!dirty" @click="originalId ? open(currentFile, true) : (kind = null)">
          {{ originalId ? 'Revert' : 'Cancel' }}
        </button>
        <span v-if="dirty" class="muted">Unsaved changes</span>
        <span class="spacer"></span>
        <button v-if="originalId" @click="remove">Delete…</button>
      </div>
    </section>
  </div>
</template>

<style scoped>
.story {
  display: flex;
  height: 100%;
  min-height: 0;
}
.list {
  width: 220px;
  flex-shrink: 0;
  overflow-y: auto;
  border-right: 1px solid #2a2a3a;
}
.group-head {
  display: flex;
  align-items: center;
  padding: 6px 6px 2px;
  font-size: 0.8em;
  text-transform: uppercase;
  letter-spacing: 0.04em;
  opacity: 0.75;
}
.group-head .add {
  margin-left: auto;
  padding: 0 6px;
}
.row {
  display: flex;
  width: 100%;
  gap: 6px;
  padding: 3px 6px 3px 14px !important;
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
  overflow: hidden;
  white-space: nowrap;
  text-overflow: ellipsis;
}
.warn {
  color: #f59e0b;
}
.editor {
  flex: 1;
  min-width: 0;
  overflow-y: auto;
  padding: 8px 10px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.editor.empty {
  max-width: 560px;
}
.form-row,
.next-row,
.choice-row,
.scene-pick {
  max-width: 130px;
}
.objective,
.node-head,
.start-row {
  display: flex;
  align-items: center;
  gap: 6px;
  flex-wrap: wrap;
}
.form-row.top {
  align-items: flex-start;
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
.id {
  width: 130px;
}
.grow {
  flex: 1;
  min-width: 160px;
}
.count {
  width: 56px;
}
.time {
  width: 96px;
}
.node {
  border: 1px solid #2a2a3a;
  background: #15151f;
  padding: 8px;
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.node textarea {
  width: 100%;
  box-sizing: border-box;
  resize: vertical;
  line-height: 1.4;
}
.tag {
  font-size: 0.75em;
  padding: 1px 6px;
  border: 1px solid #3b82f6;
  color: #93c5fd;
}
.toggle {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 0.85em;
  opacity: 0.8;
}
.choices {
  display: flex;
  flex-direction: column;
  gap: 6px;
  padding-left: 8px;
  border-left: 2px solid #2a2a3a;
}
.choice-extra {
  display: flex;
  flex-direction: column;
  gap: 3px;
  padding-left: 22px;
}
.num {
  width: 18px;
  opacity: 0.6;
}
.small-button {
  align-self: flex-start;
  font-size: 0.85em;
}
.section-label {
  margin-top: 6px;
  font-size: 0.8em;
  text-transform: uppercase;
  letter-spacing: 0.04em;
  opacity: 0.7;
}
.problems {
  margin: 0;
  padding-left: 18px;
  color: #f59e0b;
  font-size: 0.9em;
}
.actions {
  display: flex;
  align-items: center;
  gap: 6px;
  position: sticky;
  bottom: -8px;
  padding: 6px 0;
  background: #181825;
}
.spacer {
  flex: 1;
}
.view-switch {
  display: flex;
}
.view-switch button {
  padding: 2px 10px;
}
.view-switch button.on {
  background: #313148;
  border-color: #3b82f6;
}
.graph {
  overflow: auto;
  max-height: 340px;
  border: 1px solid #2a2a3a;
  background: #11111a;
  padding: 4px;
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.graph .edge {
  fill: none;
  stroke: #6b7280;
  stroke-width: 1.5;
}
.graph .edge.fail {
  stroke: #ef4444;
  stroke-dasharray: 4 3;
}
.graph .edge-label {
  fill: #9ca3af;
  font-size: 10px;
}
.gnode {
  cursor: pointer;
}
.gnode rect {
  fill: #1c1c2a;
  stroke: #3a3a50;
}
.gnode.start rect {
  stroke: #3b82f6;
}
.gnode.on rect {
  fill: #2a2a40;
  stroke: #fbbf24;
}
.gnode .gid {
  fill: #e5e7eb;
  font-size: 12px;
  font-weight: 600;
}
.gnode .gtext {
  fill: #9ca3af;
  font-size: 11px;
}
.media-row,
.check-row {
  display: flex;
  align-items: center;
  gap: 6px;
  flex-wrap: wrap;
}
.short {
  width: 110px;
}
.stage {
  gap: 4px;
}
.muted {
  opacity: 0.6;
}
.small {
  font-size: 0.8em;
}
</style>
