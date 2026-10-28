<script setup lang="ts">
// Generate ▸ Character: a character drawn from 4 or 8 directions by PixelLab, each
// direction a texture in assets/textures/generated/characters/<name>/ (with a
// .gen.json saying how it was made). The Animate tab then makes it move.
import { computed, onMounted, reactive, ref } from 'vue'
import AiProgress from './AiProgress.vue'
import { confirmDialog } from '../dialogs'
import { editorState } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'
import { panelTab } from '../paneltabs'

const PROJECT_HOST = 'https://aige-project.local/'

const form = reactive({
  description: '',
  name: '',
  directions: 4 as 4 | 8,
  size: 64,
  view: 'low top-down' as 'low top-down' | 'high top-down' | 'side',
  isometric: false,
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

onHostMessage((message) => {
  if (message.type === 'character:list') {
    history.value = (message.history as HistoryEntry[]) ?? []
    busy.value = message.busy as boolean
  } else if (message.type === 'character:result') {
    busy.value = false
    const result = message.result as { ok: boolean; errors?: string[] }
    error.value = result.ok ? '' : (result.errors?.[0] ?? 'The character couldn’t be made')
  }
})
onMounted(() => sendToHost({ type: 'character:request' }))

// "Use again": the form as it was for that request.
function useAgain(entry: HistoryEntry): void {
  const r = entry.request as Partial<typeof form>
  form.description = r.description ?? ''
  form.name = r.name ?? ''
  form.directions = r.directions ?? 4
  form.size = r.size ?? 64
  form.view = r.view ?? 'low top-down'
  form.isometric = !!r.isometric
}
async function clearHistory(): Promise<void> {
  if (await confirmDialog({ message: 'Forget the Character history? The characters stay.', confirmLabel: 'Clear', danger: true })) {
    sendToHost({ type: 'character:clearHistory' })
  }
}
function when(iso: string): string {
  const date = new Date(iso)
  return Number.isNaN(date.getTime()) ? iso : date.toLocaleString()
}
function makePlayer(entry: HistoryEntry): void {
  const pictures = entry.results.filter((r) => !r.discarded).map((r) => r.path)
  const name = String(entry.request.name || pictures[0]?.split('/').slice(-2, -1)[0] || 'hero')
  sendToHost({ type: 'character:makePlayer', pictures, name })
}
function direction(path: string): string {
  return /-(south|south-east|east|north-east|north|north-west|west|south-west)\.png$/.exec(path)?.[1] ?? ''
}

const suggestedName = computed(() =>
  form.description
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '')
    .split('-')
    .slice(0, 3)
    .join('-'),
)
const aiReady = computed(() => editorState.ai.state === 'ready')
// Drawn by the project's art model when it's BFL's, else PixelLab.
const byBfl = computed(() => editorState.ai.artInUse.startsWith('bfl:'))
const noKey = computed(() => (byBfl.value ? editorState.ai.available.bfl === false : editorState.ai.available.pixellab === false))

function generate(): void {
  error.value = ''
  busy.value = true
  sendToHost({
    type: 'character:generate',
    request: { ...form, name: (form.name || suggestedName.value).trim(), description: form.description.trim() },
  })
}

function makePrefab(path: string): void {
  const name = path.split('/').pop()!.replace(/\.png$/, '')
  sendToHost({ type: 'prefab:createFromAsset', asset: path, tile: 0, asTile: false, solid: false, name, flat: false })
}

function animate(path: string): void {
  // The Animate tab, with this image picked.
  editorState.animateFrom = path
  panelTab.generate = 'animate'
}

function thumb(path: string, created: string): string {
  return `${PROJECT_HOST}${path}?v=${encodeURIComponent(created)}`
}
</script>

<template>
  <div class="character ed-form">
    <AiProgress :paths="['/characters/']" />
    <section class="form">
      <textarea
        v-model="form.description"
        rows="3"
        placeholder="Describe them: an old herbalist in a green hooded cloak, carrying a basket of herbs…"
      ></textarea>
      <div class="row">
        <label>Name</label>
        <input v-model="form.name" :placeholder="suggestedName || 'herbalist'" spellcheck="false" />
      </div>
      <div class="row">
        <label>Directions</label>
        <select v-model.number="form.directions">
          <option :value="4">4 (south, west, east, north)</option>
          <option :value="8">8 (and the diagonals)</option>
        </select>
      </div>
      <div class="row">
        <label>Size</label>
        <select v-model.number="form.size">
          <option :value="32">32 px</option>
          <option :value="48">48 px</option>
          <option :value="64">64 px</option>
          <option :value="96">96 px</option>
          <option :value="128">128 px</option>
        </select>
      </div>
      <div class="row">
        <label>View</label>
        <select v-model="form.view">
          <option value="low top-down">low top-down (most 2D RPGs)</option>
          <option value="high top-down">high top-down</option>
          <option value="side">side</option>
        </select>
      </div>
      <label class="row check">
        <input v-model="form.isometric" type="checkbox" />
        Isometric
      </label>
      <div class="row">
        <label></label>
        <button
          class="primary"
          :disabled="busy || !aiReady || noKey || !form.description.trim() || !(form.name || suggestedName)"
          @click="generate"
        >
          Generate character
        </button>
        <span v-if="busy" class="muted">Drawing {{ form.directions }} directions (a minute or two)…</span>
        <span v-else-if="!aiReady" class="muted">Needs AI to be on.</span>
        <span v-else-if="noKey" class="muted">Needs {{ byBfl ? 'BFL_API_KEY' : 'PIXELLAB_API_KEY' }} in ai_service/.env.</span>
      </div>
      <div v-if="error" class="error">{{ error }}</div>
      <p class="muted small">
        Drawn by {{ byBfl ? `the project's art model (${editorState.ai.artInUse.slice(4)}): facing you first, each other direction an edit of it, the west side mirrored` : 'PixelLab' }}, in the project’s colours. Each direction lands in
        assets/textures/generated/characters/{{ form.name || suggestedName || '<name>' }}/ and shows in the Assets panel.
      </p>
    </section>
    <section class="gallery">
      <div class="history-head">
        <span class="muted small">History ({{ history.length }})</span>
        <span class="spacer"></span>
        <button v-if="history.length" title="Forget the history (the characters stay)" @click="clearHistory">Clear history</button>
      </div>
      <p v-if="history.length === 0" class="muted">Characters made here show up here and in the Assets panel.</p>
      <div v-for="entry in history" :key="entry.id" class="card">
        <div class="title" :title="String(entry.request.description ?? '')">{{ entry.request.name || entry.request.description }}</div>
        <div class="muted small">{{ when(entry.created) }} · {{ entry.request.directions }} directions · {{ entry.request.size }} px · {{ entry.request.view }}</div>
        <div v-if="entry.state === 'working'" class="muted small">Drawing…</div>
        <div v-else-if="entry.state === 'failed'" class="error small">{{ entry.errors[0] ?? 'Failed' }}</div>
        <div v-else class="dirs">
          <div v-for="a in entry.results.filter((r) => !r.discarded)" :key="a.path" class="dir">
            <div class="thumb"><img :src="thumb(a.path, entry.created)" :alt="direction(a.path)" /></div>
            <div class="muted small">{{ direction(a.path) }}</div>
            <div class="actions">
              <button title="A placeable prefab with this sprite" @click="makePrefab(a.path)">Prefab</button>
              <button title="Animate this direction" @click="animate(a.path)">Animate</button>
            </div>
          </div>
        </div>
        <div class="actions">
          <button
            v-if="entry.state === 'done' && entry.results.some((r) => !r.discarded)"
            class="primary"
            :disabled="editorState.playing"
            title="The player: moves, collides, fights, facing the way it walks; placed in the scene in the player's stead"
            @click="makePlayer(entry)"
          >
            Make player
          </button>
          <button title="Put this request back in the form" @click="useAgain(entry)">Use again</button>
          <button :disabled="busy || !aiReady || noKey" title="Send the same request again" @click="useAgain(entry), generate()">Generate again</button>
        </div>
      </div>
    </section>
  </div>
</template>

<style scoped>
.character {
  display: flex;
  height: 100%;
  min-height: 0;
}
.form {
  width: 380px;
  flex-shrink: 0;
  display: flex;
  flex-direction: column;
  gap: 5px;
  padding: 8px 10px;
  overflow-y: auto;
  border-right: 1px solid #2a2a3a;
}
.form textarea {
  resize: vertical;
}
.history-head {
  display: flex;
  align-items: center;
  gap: 6px;
  margin-bottom: 6px;
}
.spacer {
  flex: 1;
}
.row {
  display: flex;
  align-items: center;
  gap: 6px;
}
.row > label:first-child {
  width: 72px;
  flex-shrink: 0;
  font-size: 0.85em;
  opacity: 0.75;
}
.row.check {
  padding-left: 78px;
  font-size: 0.9em;
}
.row input:not([type='checkbox']),
.row select {
  flex: 1;
}
.gallery {
  flex: 1;
  min-width: 0;
  overflow-y: auto;
  padding: 8px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.card {
  padding: 6px;
  border: 1px solid #2a2a3a;
  background: #15151f;
}
.title {
  font-weight: 600;
  margin-bottom: 4px;
}
.dirs {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}
.dir {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
}
.thumb {
  width: 96px;
  height: 96px;
  display: flex;
  align-items: center;
  justify-content: center;
  background-color: #2a2a35;
  background-image: linear-gradient(45deg, #34343f 25%, transparent 25%, transparent 75%, #34343f 75%),
    linear-gradient(45deg, #34343f 25%, transparent 25%, transparent 75%, #34343f 75%);
  background-size: 16px 16px;
  background-position:
    0 0,
    8px 8px;
}
.thumb img {
  max-width: 92px;
  max-height: 92px;
  image-rendering: pixelated;
}
.actions {
  display: flex;
  gap: 2px;
}
.actions button {
  font-size: 0.8em;
  padding: 1px 5px;
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
