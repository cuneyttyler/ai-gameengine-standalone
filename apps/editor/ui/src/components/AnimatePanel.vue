<script setup lang="ts">
// Generate ▸ Animate: an image of the project (a character, a torch…) made to move by
// PixelLab: 4 frames of the action asked for, each a texture, plus a spritesheet of
// them in a row, in assets/textures/generated/animations/<name>/.
import { computed, onMounted, onUnmounted, reactive, ref, watch } from 'vue'
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
const made = ref<{ name: string; frames: string[]; sheet: string; at: number }[]>([])
const frame = ref(0)
let timer = 0

onHostMessage((message) => {
  if (message.type === 'assets:list') {
    const list = (message.assets as { path: string }[]) ?? []
    images.value = list.map((a) => a.path).filter((p) => /\.png$/i.test(p) && !p.includes('/placeholders/') && !p.endsWith('-sheet.png'))
  } else if (message.type === 'animation:result') {
    busy.value = false
    const result = message.result as { ok: boolean; errors?: string[]; name?: string; frames?: string[]; sheet?: string }
    error.value = result.ok ? '' : (result.errors?.[0] ?? 'The animation couldn’t be made')
    if (result.ok && result.frames?.length) {
      made.value.unshift({ name: result.name ?? '', frames: result.frames, sheet: result.sheet ?? '', at: Date.now() })
    }
  }
})

onMounted(() => {
  sendToHost({ type: 'assets:request' })
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

function url(path: string, at = 0): string {
  return `${PROJECT_HOST}${path}?v=${at}`
}
</script>

<template>
  <div class="animate ed-form">
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
      <div v-for="m in made" :key="m.at" class="card">
        <div class="title">{{ m.name }}</div>
        <div class="frames">
          <div class="thumb play"><img :src="url(m.frames[frame % m.frames.length] ?? '', m.at)" alt="playing" /></div>
          <div v-for="f in m.frames" :key="f" class="thumb small-thumb"><img :src="url(f, m.at)" alt="" /></div>
        </div>
        <div class="muted small">{{ m.sheet }}</div>
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
