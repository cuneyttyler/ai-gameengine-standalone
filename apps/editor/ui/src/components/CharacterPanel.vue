<script setup lang="ts">
// Generate ▸ Character: a character drawn from 4 or 8 directions by PixelLab, each
// direction a texture in assets/textures/generated/characters/<name>/ (with a
// .gen.json saying how it was made). The Animate tab then makes it move.
import { computed, reactive, ref } from 'vue'
import AiProgress from './AiProgress.vue'
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
const made = ref<{ name: string; assets: { path: string; direction: string }[]; at: number }[]>([])

onHostMessage((message) => {
  if (message.type !== 'character:result') return
  busy.value = false
  const result = message.result as { ok: boolean; errors?: string[]; name?: string; assets?: { path: string; direction: string }[] }
  error.value = result.ok ? '' : (result.errors?.[0] ?? 'The character couldn’t be made')
  if (result.ok && result.assets?.length) made.value.unshift({ name: result.name ?? '', assets: result.assets, at: Date.now() })
})

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
const noKey = computed(() => editorState.ai.available.pixellab === false)

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

function thumb(path: string, at: number): string {
  return `${PROJECT_HOST}${path}?v=${at}`
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
        <span v-else-if="noKey" class="muted">Needs PIXELLAB_API_KEY in ai_service/.env.</span>
      </div>
      <div v-if="error" class="error">{{ error }}</div>
      <p class="muted small">
        Drawn by PixelLab in the project’s colours. Each direction lands in
        assets/textures/generated/characters/{{ form.name || suggestedName || '<name>' }}/ and shows in the Assets panel.
      </p>
    </section>
    <section class="gallery">
      <p v-if="made.length === 0" class="muted">Characters made here show up here and in the Assets panel.</p>
      <div v-for="c in made" :key="c.at" class="card">
        <div class="title">{{ c.name }}</div>
        <div class="dirs">
          <div v-for="a in c.assets" :key="a.path" class="dir">
            <div class="thumb"><img :src="thumb(a.path, c.at)" :alt="a.direction" /></div>
            <div class="muted small">{{ a.direction }}</div>
            <div class="actions">
              <button title="A placeable prefab with this sprite" @click="makePrefab(a.path)">Prefab</button>
              <button title="Animate this direction" @click="animate(a.path)">Animate</button>
            </div>
          </div>
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
