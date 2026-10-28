<script setup lang="ts">
// Generate ▸ Animate: an image of the project (a character, a torch…) made to move by
// PixelLab: 4 frames of the action asked for, each a texture, plus a spritesheet of
// them in a row, in assets/textures/generated/animations/<name>/.
import { computed, onMounted, onUnmounted, reactive, ref, watch } from 'vue'
import AiProgress from './AiProgress.vue'
import { confirmDialog } from '../dialogs'
import { editorState } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'

const PROJECT_HOST = 'https://aige-project.local/'
const DIRECTIONS = ['south', 'south-east', 'east', 'north-east', 'north', 'north-west', 'west', 'south-west']
const ACTIONS = ['walking', 'running', 'idle breathing', 'attacking with a sword', 'casting a spell', 'jumping', 'flickering flame']

const images = ref<string[]>([])
const filter = ref('')
const form = reactive({
  path: '',
  action: 'walking',
  description: '',
  direction: 'south',
  view: 'low top-down' as 'low top-down' | 'high top-down' | 'side',
  name: '',
})
const busy = ref(false)
const error = ref('')
// Every request made here and what came back, kept by the editor with the project (it stays when the tab changes).
interface HistoryEntry {
  id: number
  created: string
  request: Record<string, unknown>
  state: 'working' | 'done' | 'failed'
  errors: string[]
  results: { path: string; discarded: boolean }[]
}
const history = ref<HistoryEntry[]>([])
const frame = ref(0)
let timer = 0

onHostMessage((message) => {
  if (message.type === 'assets:list') {
    const list = (message.assets as { path: string }[]) ?? []
    images.value = list.map((a) => a.path).filter((p) => /\.png$/i.test(p) && !p.includes('/placeholders/') && !p.endsWith('-sheet.png'))
  } else if (message.type === 'animation:list') {
    history.value = (message.history as HistoryEntry[]) ?? []
    busy.value = message.busy as boolean
  } else if (message.type === 'animation:result') {
    busy.value = false
    const result = message.result as { ok: boolean; errors?: string[] }
    error.value = result.ok ? '' : (result.errors?.[0] ?? 'The animation couldn’t be made')
  }
})

onMounted(() => {
  sendToHost({ type: 'assets:request' })
  sendToHost({ type: 'animation:request' })
  timer = window.setInterval(() => (frame.value = (frame.value + 1) % 4), 160)
})
onUnmounted(() => window.clearInterval(timer))

// The Character tab's "Animate" picks the image.
watch(
  () => editorState.animateFrom,
  (path) => {
    if (path) {
      form.path = path
      const m = /-(south|south-east|east|north-east|north|north-west|west|south-west)\.png$/.exec(path)
      if (m?.[1]) form.direction = m[1]
      editorState.animateFrom = ''
    }
  },
  { immediate: true },
)

const shown = computed(() => {
  const words = filter.value.toLowerCase().split(/\s+/).filter(Boolean)
  return images.value.filter((p) => words.every((w) => p.toLowerCase().includes(w))).slice(0, 120)
})
const aiReady = computed(() => editorState.ai.state === 'ready')
const noKey = computed(() => editorState.ai.available.pixellab === false)

function generate(): void {
  error.value = ''
  busy.value = true
  sendToHost({ type: 'animation:generate', request: { ...form, action: form.action.trim(), description: form.description.trim() } })
}

function url(path: string, at: number | string = 0): string {
  return `${PROJECT_HOST}${path}?v=${encodeURIComponent(String(at))}`
}

// An entry's spritesheet and frames (what's still there).
function sheetOf(entry: HistoryEntry): string {
  return entry.results.find((r) => !r.discarded && r.path.endsWith('-sheet.png'))?.path ?? ''
}
function framesOf(entry: HistoryEntry): string[] {
  return entry.results.filter((r) => !r.discarded && !r.path.endsWith('-sheet.png')).map((r) => r.path)
}
// The clip its action is, for the animators: walking walks, an attack attacks, a spell casts; anything else loops.
function clipFor(action: string): string {
  const a = action.toLowerCase()
  return /walk|run/.test(a) ? 'walk' : /attack|strik|swing/.test(a) ? 'attack' : /cast|spell/.test(a) ? 'cast' : /hurt|hit/.test(a) ? 'hurt' : /die|death/.test(a) ? 'die' : 'idle'
}
function make(entry: HistoryEntry, prefab: boolean): void {
  const r = entry.request as { action?: string; direction?: string; name?: string }
  const sheet = sheetOf(entry)
  const name = (r.name || sheet.split('/').pop()!.replace(/-sheet\.png$/, '')).replace(/[^A-Za-z0-9_-]/g, '-')
  sendToHost({ type: 'animation:make', sheet, frames: framesOf(entry), clip: clipFor(r.action ?? ''), direction: r.direction ?? '', name, prefab })
}
function useAgain(entry: HistoryEntry): void {
  const r = entry.request as Partial<typeof form>
  form.path = r.path ?? ''
  form.action = r.action ?? 'walking'
  form.description = r.description ?? ''
  form.direction = r.direction ?? 'south'
  form.view = r.view ?? 'low top-down'
  form.name = r.name ?? ''
}
async function clearHistory(): Promise<void> {
  if (await confirmDialog({ message: 'Forget the Animate history? The animations stay.', confirmLabel: 'Clear', danger: true })) {
    sendToHost({ type: 'animation:clearHistory' })
  }
}
function when(iso: string): string {
  const date = new Date(iso)
  return Number.isNaN(date.getTime()) ? iso : date.toLocaleString()
}
</script>

<template>
  <div class="animate ed-form">
    <AiProgress :paths="['/animations/']" />
    <section class="pick">
      <input v-model="filter" placeholder="Find an image: knight, torch, characters…" spellcheck="false" />
      <div class="grid">
        <button
          v-for="p in shown"
          :key="p"
          class="cell"
          :class="{ active: form.path === p }"
          :title="p"
          @click="form.path = p"
        >
          <img :src="url(p)" alt="" loading="lazy" />
        </button>
        <p v-if="shown.length === 0" class="muted small">No images{{ filter ? ' match' : ' in the project yet' }}.</p>
      </div>
    </section>
    <section class="form">
      <div class="row">
        <label>Image</label>
        <span class="path" :title="form.path">{{ form.path || 'pick one on the left' }}</span>
      </div>
      <div class="row">
        <label>Action</label>
        <input v-model="form.action" list="animate-actions" placeholder="walking" />
        <datalist id="animate-actions">
          <option v-for="a in ACTIONS" :key="a" :value="a" />
        </datalist>
      </div>
      <div class="row">
        <label>What it is</label>
        <input v-model="form.description" placeholder="optional: a knight in plate armour" />
      </div>
      <div class="row">
        <label>Facing</label>
        <select v-model="form.direction">
          <option v-for="d in DIRECTIONS" :key="d" :value="d">{{ d }}</option>
        </select>
      </div>
      <div class="row">
        <label>View</label>
        <select v-model="form.view">
          <option value="low top-down">low top-down</option>
          <option value="high top-down">high top-down</option>
          <option value="side">side</option>
        </select>
      </div>
      <div class="row">
        <label>Name</label>
        <input v-model="form.name" placeholder="optional: knight-walk" spellcheck="false" />
      </div>
      <div class="row">
        <label></label>
        <button class="primary" :disabled="busy || !aiReady || noKey || !form.path || !form.action.trim()" @click="generate">
          Animate
        </button>
        <span v-if="busy" class="muted">Drawing 4 frames (about a minute)…</span>
        <span v-else-if="!aiReady" class="muted">Needs AI to be on.</span>
        <span v-else-if="noKey" class="muted">Needs PIXELLAB_API_KEY in ai_service/.env.</span>
      </div>
      <div v-if="error" class="error">{{ error }}</div>
      <p class="muted small">
        PixelLab draws 4 frames at 64 × 64 from the image (it’s fitted into that square). The frames and a spritesheet
        of them go into assets/textures/generated/animations/.
      </p>
      <div class="history-head">
        <span class="muted small">History ({{ history.length }})</span>
        <span class="spacer"></span>
        <button v-if="history.length" title="Forget the history (the animations stay)" @click="clearHistory">Clear history</button>
      </div>
      <div v-for="entry in history" :key="entry.id" class="card">
        <div class="title">{{ entry.request.name || entry.request.action }} <span class="muted small">{{ entry.request.direction }}</span></div>
        <div class="muted small">{{ when(entry.created) }} · {{ String(entry.request.path ?? '').split('/').pop() }}</div>
        <div v-if="entry.state === 'working'" class="muted small">Drawing…</div>
        <div v-else-if="entry.state === 'failed'" class="error small">{{ entry.errors[0] ?? 'Failed' }}</div>
        <template v-else-if="framesOf(entry).length">
          <div class="frames">
            <div class="thumb play"><img :src="url(framesOf(entry)[frame % framesOf(entry).length] ?? '', entry.created)" alt="playing" /></div>
            <div v-for="f in framesOf(entry)" :key="f" class="thumb small-thumb"><img :src="url(f, entry.created)" alt="" /></div>
          </div>
          <div class="muted small">{{ sheetOf(entry) }}</div>
        </template>
        <div class="actions">
          <button v-if="sheetOf(entry)" title="An animation sheet of it (Project ▸ Effects ▸ Sheets), for an Animator" @click="make(entry, false)">Make animation</button>
          <button v-if="sheetOf(entry)" title="A prefab that plays it (a character's action through the humanoid animator, else looping)" @click="make(entry, true)">Make prefab</button>
          <button title="Put this request back in the form" @click="useAgain(entry)">Use again</button>
          <button :disabled="busy || !aiReady || noKey" title="Send the same request again" @click="useAgain(entry), generate()">Generate again</button>
        </div>
      </div>
    </section>
  </div>
</template>

<style scoped>
.animate {
  display: flex;
  height: 100%;
  min-height: 0;
}
.history-head {
  display: flex;
  align-items: center;
  gap: 6px;
  margin: 8px 0 6px;
}
.spacer {
  flex: 1;
}
.actions {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
  margin-top: 4px;
}
.pick {
  width: 300px;
  flex-shrink: 0;
  display: flex;
  flex-direction: column;
  gap: 5px;
  padding: 8px;
  border-right: 1px solid #2a2a3a;
  min-height: 0;
}
.grid {
  flex: 1;
  overflow-y: auto;
  display: flex;
  flex-wrap: wrap;
  align-content: flex-start;
  gap: 4px;
}
.cell {
  width: 56px;
  height: 56px;
  padding: 2px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: #2a2a35;
  border: 1px solid transparent;
}
.cell.active {
  border-color: #3b82f6;
}
.cell img {
  max-width: 50px;
  max-height: 50px;
  image-rendering: pixelated;
}
.form {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 5px;
  padding: 8px 10px;
  overflow-y: auto;
}
.row {
  display: flex;
  align-items: center;
  gap: 6px;
  max-width: 460px;
}
.row > label:first-child {
  width: 72px;
  flex-shrink: 0;
  font-size: 0.85em;
  opacity: 0.75;
}
.row input,
.row select {
  flex: 1;
}
.path {
  overflow: hidden;
  white-space: nowrap;
  text-overflow: ellipsis;
  opacity: 0.85;
}
.card {
  padding: 6px;
  border: 1px solid #2a2a3a;
  background: #15151f;
  max-width: 460px;
}
.title {
  font-weight: 600;
  margin-bottom: 4px;
}
.frames {
  display: flex;
  gap: 4px;
  align-items: flex-end;
}
.thumb {
  display: flex;
  align-items: center;
  justify-content: center;
  background-color: #2a2a35;
}
.thumb.play {
  width: 128px;
  height: 128px;
}
.thumb.play img {
  width: 128px;
  height: 128px;
  image-rendering: pixelated;
}
.small-thumb {
  width: 48px;
  height: 48px;
}
.small-thumb img {
  width: 48px;
  height: 48px;
  image-rendering: pixelated;
}
.muted {
  opacity: 0.6;
}
.small {
  font-size: 0.85em;
}
.error {
  color: #f59e0b;
}
</style>
