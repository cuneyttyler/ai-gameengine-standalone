<script setup lang="ts">
// Asset Generator Studio (development_plan.md 3.6): describe a sprite, get it
// drawn (an image model, then cut out, cropped and scaled by the AI service),
// and keep, regenerate or discard what comes back. Everything generated lands
// in assets/textures/generated with a .gen.json saying how it was made.
import { computed, onMounted, reactive, ref } from 'vue'
import AiProgress from './AiProgress.vue'
import { editorState } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'
import { confirmDialog } from '../dialogs'

const PROJECT_HOST = 'https://aige-project.local/'

interface Meta {
  description?: string
  variant?: string | null
  family?: string
  kind?: string
  prompt?: string
  model?: string
  seed?: number | null
  created?: string
  seconds?: number
  processing?: { size?: number[]; style?: string }
}
interface Item {
  path: string
  version: number
  meta: Meta
}

const form = reactive({
  description: '',
  name: '',
  kind: 'object' as 'object' | 'character' | 'tile',
  height: 48,
  style: 'pixel' as 'pixel' | 'smooth',
  variants: '',
})
// Every request made here (development_plan.md 4.5), newest first.
interface HistoryEntry {
  id: number
  created: string
  request: { name?: string; description?: string; kind?: string; height?: number; style?: string; variants?: string[] }
  state: 'working' | 'done' | 'failed'
  errors: string[]
  results: { path: string; discarded: boolean }[]
}
const view = ref<'files' | 'history'>('files')
const history = ref<HistoryEntry[]>([])
const items = ref<Item[]>([])
const busy = ref(false)
const error = ref('')
const expected = ref(0)

onHostMessage((message) => {
  if (message.type === 'art:list') {
    items.value = message.items as Item[]
    busy.value = message.busy as boolean
    history.value = (message.history as HistoryEntry[]) ?? []
  } else if (message.type === 'art:result') {
    busy.value = false
    const result = message.result as { ok: boolean; errors?: string[] }
    error.value = result.ok ? '' : (result.errors?.[0] ?? 'Generation failed')
  }
})

onMounted(() => sendToHost({ type: 'art:request' }))

const variantList = computed(() =>
  form.variants
    .split(',')
    .map((v) => v.trim())
    .filter(Boolean),
)

// A name from the description until one is typed.
const suggestedName = computed(() =>
  form.description
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '')
    .split('-')
    .slice(0, 3)
    .join('-'),
)

function request(variants: string[], overrides: Partial<typeof form> = {}): void {
  const f = { ...form, ...overrides }
  error.value = ''
  busy.value = true
  expected.value = Math.max(1, variants.length)
  sendToHost({
    type: 'art:generate',
    request: {
      name: (f.name || suggestedName.value).trim(),
      description: f.description,
      kind: f.kind,
      height: f.height,
      style: f.style,
      variants,
    },
  })
}

function generate(): void {
  request(variantList.value)
}

function regenerate(item: Item): void {
  const m = item.meta
  request(m.variant ? [m.variant] : [], {
    name: m.family ?? '',
    description: m.description ?? '',
    kind: (m.kind as 'object' | 'character' | 'tile') ?? 'object',
    height: m.processing?.size?.[1] ?? form.height,
    style: (m.processing?.style as 'pixel' | 'smooth') ?? form.style,
  })
}

// "Use again": the form as it was for that request.
function useAgain(entry: HistoryEntry): void {
  const r = entry.request
  form.description = r.description ?? ''
  form.name = r.name ?? ''
  form.kind = (r.kind as 'object' | 'character' | 'tile') ?? 'object'
  form.height = r.height ?? 48
  form.style = (r.style as 'pixel' | 'smooth') ?? 'pixel'
  form.variants = (r.variants ?? []).join(', ')
}

function generateAgain(entry: HistoryEntry): void {
  useAgain(entry)
  generate()
}

// A later request can overwrite the same file name.
function replaced(entry: HistoryEntry, path: string): boolean {
  return history.value.some((other) => other.id > entry.id && other.results.some((r) => r.path === path && !r.discarded))
}

async function clearHistory(): Promise<void> {
  if (await confirmDialog({ message: 'Forget the Art history? Generated files stay.', confirmLabel: 'Clear', danger: true })) {
    sendToHost({ type: 'art:clearHistory' })
  }
}

function when(iso: string): string {
  const date = new Date(iso)
  return Number.isNaN(date.getTime()) ? iso : date.toLocaleString()
}

function makePrefab(item: Item): void {
  const name = item.path.split('/').pop()!.replace(/\.png$/, '')
  // A generated ground tile becomes a flat tile on the grid (5.2).
  const isTile = item.meta.kind === 'tile'
  sendToHost({ type: 'prefab:createFromAsset', asset: item.path, tile: 0, asTile: isTile, solid: false, name, flat: isTile })
}

async function discard(item: Item): Promise<void> {
  if (await confirmDialog({ message: `Delete ${item.path}?`, confirmLabel: 'Discard', danger: true })) {
    sendToHost({ type: 'art:discard', path: item.path })
  }
}

function thumb(item: Item): string {
  return `${PROJECT_HOST}${item.path}?v=${item.version}-${item.meta.created ?? ''}`
}

const aiReady = computed(() => editorState.ai.state === 'ready')
// Why Generate is off, in words (shown beside it): "" when it's on.
const whyOff = computed(() =>
  busy.value ? 'Still drawing the last one…'
  : !aiReady.value ? `Turn AI on (${editorState.ai.text || editorState.ai.state})`
  : !form.description.trim() ? 'Write a description'
  : !(form.name || suggestedName.value) ? 'Name it'
  : '',
)
</script>

<template>
  <div class="art ed-form">
    <AiProgress :paths="['/assets/generate', '/assets/describe']" />
    <div class="cols">
    <section class="form">
      <textarea
        v-model="form.description"
        rows="3"
        placeholder="Describe it: a wooden barrel with iron bands; an old herbalist with a basket…"
      ></textarea>
      <div class="row">
        <label>Name</label>
        <input v-model="form.name" :placeholder="suggestedName || 'barrel'" spellcheck="false" />
      </div>
      <div class="row">
        <label>Kind</label>
        <select v-model="form.kind">
          <option value="object">object</option>
          <option value="character">character</option>
          <option value="tile">ground tile (top-down)</option>
        </select>
        <label class="inline">Height</label>
        <input v-model.number="form.height" type="number" min="8" max="512" class="num" />
        <span class="muted">px</span>
      </div>
      <div class="row">
        <label>Style</label>
        <select v-model="form.style">
          <option value="pixel">pixel art</option>
          <option value="smooth">smooth</option>
        </select>
      </div>
      <div class="row">
        <label>Variants</label>
        <input v-model="form.variants" placeholder="optional: mossy, broken, painted red" />
      </div>
      <div class="row">
        <label></label>
        <button
          class="primary"
          :disabled="!!whyOff"
          :title="whyOff"
          @click="generate"
        >
          Generate {{ variantList.length > 1 ? variantList.length + ' variants' : '' }}
        </button>
        <span v-if="busy" class="muted">Drawing {{ expected }} {{ expected === 1 ? 'image' : 'images' }} (~10 s each)…</span>
        <span v-else-if="!aiReady" class="muted">Needs AI to be on.</span>
        <span v-else-if="whyOff" class="muted">{{ whyOff }}</span>
      </div>
      <div v-if="error" class="error">{{ error }}</div>
      <p class="muted small">
        Drawn by an image model, then cut out, cropped and scaled. Each file keeps its description, prompt and model in
        a .gen.json next to it. Sometimes a little ground is drawn under the subject — regenerate if so.
      </p>
    </section>
    <section class="right">
      <div class="view-tabs">
        <button :class="{ active: view === 'files' }" @click="view = 'files'">Files ({{ items.length }})</button>
        <button :class="{ active: view === 'history' }" @click="view = 'history'">
          History ({{ history.length }})
        </button>
        <span class="spacer"></span>
        <button v-if="view === 'history' && history.length" title="Forget the history (files stay)" @click="clearHistory">
          Clear history
        </button>
      </div>
      <section v-if="view === 'history'" class="gallery">
        <p v-if="history.length === 0" class="muted">No requests yet. Everything you ask for here is listed, kept or not.</p>
        <div v-for="entry in history" :key="entry.id" class="card history" :class="entry.state">
          <div class="info">
            <div class="title" :title="entry.request.description">
              {{ entry.request.description || entry.request.name }}
            </div>
            <div class="muted small">
              {{ when(entry.created) }} · {{ entry.request.kind }} · {{ entry.request.height }} px ·
              {{ entry.request.style }}{{ entry.request.variants?.length ? ' · ' + entry.request.variants.join(', ') : '' }}
            </div>
            <div v-if="entry.state === 'working'" class="muted small">Drawing…</div>
            <div v-else-if="entry.state === 'failed'" class="error small">{{ entry.errors[0] ?? 'Failed' }}</div>
            <div v-else class="results">
              <span
                v-for="result in entry.results"
                :key="result.path"
                class="mini"
                :title="result.path + (result.discarded ? ' (discarded)' : replaced(entry, result.path) ? ' (drawn again later)' : '')"
              >
                <img v-if="!result.discarded && !replaced(entry, result.path)" :src="thumb({ path: result.path, version: 0, meta: { created: entry.created } })" alt="" />
                <span v-else class="muted small">{{ result.discarded ? 'discarded' : 'redrawn' }}</span>
              </span>
            </div>
            <div class="actions">
              <button title="Put this request back in the form" @click="useAgain(entry)">Use again</button>
              <button :disabled="busy || !aiReady" title="Send the same request again" @click="generateAgain(entry)">
                Generate again
              </button>
            </div>
          </div>
        </div>
      </section>
      <section v-else class="gallery">
      <p v-if="items.length === 0" class="muted">Nothing generated yet.</p>
      <div v-for="item in items" :key="item.path" class="card">
        <div class="thumb"><img :src="thumb(item)" :alt="item.path" /></div>
        <div class="info">
          <div class="title" :title="item.meta.prompt">
            {{ item.path.split('/').pop() }}
          </div>
          <div class="muted small">
            {{ item.meta.description }}{{ item.meta.variant ? ' — ' + item.meta.variant : '' }}
          </div>
          <div class="muted small">
            {{ item.meta.model ?? 'unknown model' }} · seed {{ item.meta.seed ?? 'not exposed' }}
          </div>
          <div class="actions">
            <button :disabled="busy || !aiReady" title="Draw it again" @click="regenerate(item)">Regenerate</button>
            <button title="A placeable prefab with this sprite" @click="makePrefab(item)">Make prefab</button>
            <button title="Delete the file" @click="discard(item)">Discard</button>
          </div>
        </div>
      </div>
      </section>
    </section>
  </div>
  </div>
</template>

<style scoped>
.art {
  display: flex;
  flex-direction: column;
  height: 100%;
  min-height: 0;
}
/* The progress strip above (AiProgress), the columns below it. */
.cols {
  display: flex;
  flex: 1;
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
.row {
  display: flex;
  align-items: center;
  gap: 6px;
}
.row > label:first-child {
  width: 64px;
  flex-shrink: 0;
  font-size: 0.85em;
  opacity: 0.75;
}
.row input:not(.num),
.row select {
  flex: 1;
}
.inline {
  font-size: 0.85em;
  opacity: 0.75;
}
.num {
  width: 64px;
}
.right {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
}
.view-tabs {
  display: flex;
  gap: 2px;
  padding: 6px 8px 0;
}
.view-tabs button.active {
  border-color: #3b82f6;
  background: #313148;
}
.spacer {
  flex: 1;
}
.history.failed {
  border-color: #5a3a1a;
}
.results {
  display: flex;
  gap: 4px;
  flex-wrap: wrap;
}
.mini {
  width: 48px;
  height: 48px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: #2a2a35;
}
.mini img {
  max-width: 46px;
  max-height: 46px;
  image-rendering: pixelated;
}
.gallery {
  flex: 1;
  min-width: 0;
  overflow-y: auto;
  padding: 8px;
  display: flex;
  flex-wrap: wrap;
  align-content: flex-start;
  gap: 8px;
}
.card {
  display: flex;
  gap: 8px;
  width: 340px;
  padding: 6px;
  border: 1px solid #2a2a3a;
  background: #15151f;
}
.thumb {
  width: 96px;
  height: 96px;
  flex-shrink: 0;
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
  transform: scale(1.6);
}
.info {
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 2px;
}
.title {
  font-weight: 600;
  overflow: hidden;
  white-space: nowrap;
  text-overflow: ellipsis;
}
.actions {
  display: flex;
  gap: 4px;
  margin-top: auto;
  flex-wrap: wrap;
}
.actions button {
  font-size: 0.85em;
  padding: 2px 6px;
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
