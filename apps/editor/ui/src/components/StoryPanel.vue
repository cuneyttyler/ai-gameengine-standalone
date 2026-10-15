<script setup lang="ts">
// Dialogue and quests (development_plan.md 3.2c, format in docs/story.md).
// The host lists dialogue/*.json and quests/*.json with problems and what
// can be referenced (World Bible entries, prefabs); this panel edits one file
// at a time as a draft and saves it back through the engine's own reader and
// writer.
import { computed, onMounted, reactive, ref } from 'vue'
import { onHostMessage, sendToHost } from '../ipc'
import ConditionList, { type Condition } from './story/ConditionList.vue'
import EffectList, { type Effect } from './story/EffectList.vue'

interface Choice {
  text: string
  next: string
  if?: Condition[]
  do?: Effect[]
}
interface Node {
  id: string
  speaker?: string
  text: string
  choices?: Choice[]
  next?: string
  do?: Effect[]
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
}
interface QuestContent {
  name: string
  description: string
  giver: string
  objectives: Objective[]
  onComplete?: Effect[]
}
interface ScheduleEntry {
  from: string
  activity: string
  place: string
  radius?: number
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
const quest = reactive<QuestContent>({ name: '', description: '', giver: '', objectives: [], onComplete: [] })
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

function objectivesOf(id: string): string[] {
  return quests.value.find((q) => q.id === id)?.content.objectives.map((o) => o.id) ?? []
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

function confirmDiscard(): boolean {
  return !dirty.value || window.confirm('Discard your unsaved changes?')
}

function open(file: string, force = false): void {
  if (!force && !confirmDiscard()) return
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
    const copy = JSON.parse(JSON.stringify(q.content)) as QuestContent
    Object.assign(quest, { ...copy, onComplete: copy.onComplete ?? [] })
  } else {
    return
  }
  saved.value = snapshot()
}

function newDialogue(): void {
  if (!confirmDiscard()) return
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

function newQuest(): void {
  if (!confirmDiscard()) return
  kind.value = 'quest'
  draftId.value = ''
  originalId.value = ''
  Object.assign(quest, {
    name: '',
    description: '',
    giver: characters.value[0]?.key ?? '',
    objectives: [{ id: 'first', text: '', type: 'defeat', target: 'Enemy', count: 1 }],
    onComplete: [],
  })
  saved.value = ''
  saveError.value = ''
}

function newSchedule(): void {
  if (!confirmDiscard()) return
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
    content: JSON.parse(JSON.stringify(draftContent())),
  })
}

function remove(): void {
  const file = currentFile.value
  const users = usedBy.value[file]
  const warning = users?.length ? `\n\nUsed by ${users.join(', ')} in the open scene.` : ''
  if (!originalId.value || !window.confirm(`Delete ${file}?${warning}`)) return
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
    for (const c of n.choices ?? []) if (c.next === old) c.next = id
  }
}

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
  document.getElementById(`story-node-${id}`)?.scrollIntoView({ behavior: 'smooth', block: 'center' })
}

// --- Quest helpers -----------------------------------------------------------

function targetsFor(type: string): { value: string; label: string }[] {
  if (type === 'talk') return world.value.filter((e) => e.kind === 'character').map((e) => ({ value: e.key, label: e.name }))
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
      <p class="muted">Files live in <code>dialogue/</code> and <code>quests/</code>; the format is in docs/story.md.</p>
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
        </div>
        <div class="starts">
          <div v-for="(s, i) in dialogue.starts" :key="i" class="start-row">
            <span class="muted">Instead start at</span>
            <select v-model="s.node">
              <option v-for="id in nodeIds" :key="id" :value="id">{{ id }}</option>
            </select>
            <ConditionList :conditions="s.if" :quests="questIds" />
            <button title="Remove" @click="dialogue.starts!.splice(i, 1)">×</button>
          </div>
          <button class="small-button" @click="dialogue.starts!.push({ if: [], node: dialogue.start })">
            + Different start when…
          </button>
        </div>

        <div v-for="(node, n) in dialogue.nodes" :id="`story-node-${node.id}`" :key="n" class="node">
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
            <button title="Delete this node" @click="dialogue.nodes.splice(n, 1)">×</button>
          </div>
          <textarea v-model="node.text" rows="2" placeholder="What they say"></textarea>
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
                <ConditionList :conditions="c.if!" :quests="questIds" />
                <EffectList :effects="c.do!" :quests="questIds" :objectives-of="objectivesOf" />
              </div>
            </div>
            <button class="small-button" :disabled="node.choices.length >= 4" @click="node.choices.push({ text: '', next: '', if: [], do: [] })">
              + Choice
            </button>
          </div>
          <EffectList :effects="node.do!" :quests="questIds" :objectives-of="objectivesOf" />
        </div>
        <button class="small-button" @click="addNode">+ Node</button>
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
          <select v-model="e.place">
            <option v-for="m in markers" :key="m" :value="m">{{ m }}</option>
            <option v-if="e.place && !markers.includes(e.place)" :value="e.place">{{ e.place }} (missing)</option>
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
        <div class="section-label">Objectives, in order</div>
        <div v-for="(o, i) in quest.objectives" :key="i" class="objective">
          <span class="num">{{ i + 1 }}.</span>
          <input v-model="o.id" class="id" spellcheck="false" title="Objective id" />
          <input v-model="o.text" class="grow" placeholder="Shown on screen" />
          <select v-model="o.type">
            <option value="defeat">defeat</option>
            <option value="collect">collect</option>
            <option value="talk">talk to</option>
          </select>
          <select v-model="o.target">
            <option v-for="t in targetsFor(o.type)" :key="t.value" :value="t.value">{{ t.label }}</option>
            <option v-if="o.target && !targetsFor(o.type).some((t) => t.value === o.target)" :value="o.target">
              {{ o.target }} (missing)
            </option>
          </select>
          <input v-if="o.type !== 'talk'" v-model.number="o.count" type="number" min="1" class="count" title="How many" />
          <button title="Remove" @click="quest.objectives.splice(i, 1)">×</button>
        </div>
        <button
          class="small-button"
          @click="quest.objectives.push({ id: `step-${quest.objectives.length + 1}`, text: '', type: 'defeat', target: 'Enemy', count: 1 })"
        >
          + Objective
        </button>
        <div class="section-label">When completed</div>
        <EffectList :effects="quest.onComplete!" :quests="questIds" :objectives-of="objectivesOf" />
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
.muted {
  opacity: 0.6;
}
.small {
  font-size: 0.8em;
}
</style>
