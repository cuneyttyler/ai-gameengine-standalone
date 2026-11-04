<script setup lang="ts">
// Animate the interior (docs/places/interior-objects.md): a building's floors looked at by the vision model for the
// interior objects the ontology says such a building holds (a smithy's forge, a house's fireplace, a lab's tesla coil),
// how each moves written by the LLM from the ontology's animations; the user checks them (the motion, what plays it,
// the box, which ones), draws any it missed, then each is animated and put on its floor (AnimatedPart, floor k).
import { computed, nextTick, onBeforeUnmount, ref, watch } from 'vue'
import { editorState } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'
import { trackOverlay, untrackOverlay } from '../overlay'

const PROJECT_HOST = 'https://aige-project.local/'

interface Floor { floor: number; texture: string; size: [number, number]; region: [number, number, number, number] }
interface Part {
  key: string; floor: number; kind: string; label: string; box: [number, number, number, number]
  motion: string; clip: string; opens: string; prompt: string; duration: number; animate: boolean
  status: string; error?: string; source?: string; had?: boolean; confidence?: number
  preview?: { sheet: string; count: number; fps: number }
}
interface Candidate { id: string; label: string }
interface State {
  id: number | null; name?: string; building?: string; floors?: Floor[]; parts?: Part[]; candidates?: Candidate[]
  running?: boolean; status?: string; error?: string; warnings?: string[]; done?: number; failed?: number; chosen?: number
}

const backdrop = ref<HTMLElement | null>(null)
const state = ref<State | null>(null)
const floor = ref(1)
const selected = ref('')
const drawing = ref<[number, number, number, number] | null>(null)
const drawKind = ref('')
const clock = ref(0)
let dragFrom: [number, number] | null = null
const timer = window.setInterval(() => (clock.value += 1 / 20), 50)
onBeforeUnmount(() => window.clearInterval(timer))

watch(
  () => editorState.interiorPartsDialog,
  async (id) => {
    state.value = null
    selected.value = ''
    drawing.value = null
    floor.value = 1
    if (id !== null) sendToHost({ type: 'interiorParts:open', id })
    await nextTick()
    if (id !== null && backdrop.value) {
      trackOverlay(backdrop.value)
      backdrop.value.focus()
    }
  },
)

onHostMessage((message) => {
  // Opened by the AI panel (its editor action), or moved to the building being worked on.
  if (message.type === 'interiorParts:show' && typeof message.id === 'number') {
    editorState.interiorPartsDialog = message.id
    return
  }
  if (message.type !== 'interiorParts:state' || editorState.interiorPartsDialog === null) return
  const next = message as unknown as State
  if (next.id === null) {
    close()
    return
  }
  if (next.id !== editorState.interiorPartsDialog) return
  state.value = next
  if (!drawKind.value && next.candidates?.length) drawKind.value = next.candidates[0]!.id
})

const floors = computed(() => state.value?.floors ?? [])
const current = computed(() => floors.value.find((f) => f.floor === floor.value) ?? floors.value[0])
const parts = computed(() => (state.value?.parts ?? []).filter((p) => p.floor === (current.value?.floor ?? 1)))
const allParts = computed(() => state.value?.parts ?? [])
const working = computed(() => !!state.value?.running)
const kindLabel = (id: string) => state.value?.candidates?.find((c) => c.id === id)?.label ?? id

const scale = computed(() => {
  const f = current.value
  if (!f) return 1
  return Math.min(560 / Math.max(1, f.region[2]), 420 / Math.max(1, f.region[3]), 4)
})
const pictureStyle = computed(() => {
  const f = current.value
  if (!f) return {}
  const [x, y, w, h] = f.region
  const s = scale.value
  return {
    width: `${w * s}px`, height: `${h * s}px`,
    backgroundImage: `url("${PROJECT_HOST}${f.texture}")`,
    backgroundPosition: `-${x * s}px -${y * s}px`,
    backgroundSize: `${f.size[0] * s}px ${f.size[1] * s}px`,
  }
})
// A box in the floor picture's pixels, on the shown region.
function boxStyle(box: number[]): Record<string, string> {
  const f = current.value
  if (!f) return { display: 'none' }
  const s = scale.value
  return { left: `${(box[0]! - f.region[0]) * s}px`, top: `${(box[1]! - f.region[1]) * s}px`, width: `${box[2]! * s}px`, height: `${box[3]! * s}px` }
}
// A part done: its animation playing over its box (on and on; a use part opens and shuts).
function playStyle(p: Part): Record<string, string> {
  const d = p.preview
  if (!d?.sheet || !d.count) return { display: 'none' }
  const n = Math.max(1, d.count), fps = Math.max(1, d.fps || 8)
  const f = Math.floor(clock.value * fps) % n
  return {
    ...boxStyle(p.box),
    backgroundImage: `url("${PROJECT_HOST}${d.sheet}")`, backgroundSize: `${n * 100}% 100%`,
    backgroundPosition: `${n > 1 ? (f / (n - 1)) * 100 : 0}% 0`,
  }
}

function at(event: MouseEvent): [number, number] {
  const el = event.currentTarget as HTMLElement
  const r = el.getBoundingClientRect()
  const f = current.value!
  return [Math.round((event.clientX - r.left) / scale.value) + f.region[0], Math.round((event.clientY - r.top) / scale.value) + f.region[1]]
}
function down(event: MouseEvent): void {
  if (working.value) return
  dragFrom = at(event)
  drawing.value = [dragFrom[0], dragFrom[1], 1, 1]
}
function move(event: MouseEvent): void {
  if (!dragFrom) return
  const [x, y] = at(event)
  drawing.value = [Math.min(x, dragFrom[0]), Math.min(y, dragFrom[1]), Math.abs(x - dragFrom[0]) + 1, Math.abs(y - dragFrom[1]) + 1]
}
function up(): void {
  dragFrom = null
  // A click (no box): picks the part under it instead.
  if (drawing.value && drawing.value[2] < 4 && drawing.value[3] < 4) {
    const [x, y] = drawing.value
    const hit = parts.value.find((p) => x >= p.box[0] && y >= p.box[1] && x < p.box[0] + p.box[2] && y < p.box[1] + p.box[3])
    selected.value = hit?.key ?? ''
    drawing.value = null
  }
}
function addDrawn(): void {
  if (!drawing.value || !drawKind.value) return
  sendToHost({ type: 'interiorParts:add', floor: current.value?.floor ?? 1, box: drawing.value, kind: drawKind.value, label: kindLabel(drawKind.value) })
  drawing.value = null
}

function set(p: Part, field: string, value: unknown): void {
  sendToHost({ type: 'interiorParts:set', key: p.key, [field]: value })
}
function find(): void {
  sendToHost({ type: 'interiorParts:find' })
}
function run(key?: string): void {
  sendToHost({ type: 'interiorParts:run', ...(key ? { key } : {}) })
}
function stop(): void {
  sendToHost({ type: 'interiorParts:stop' })
}
function remove(p: Part): void {
  sendToHost({ type: 'interiorParts:remove', key: p.key })
}
function close(): void {
  if (backdrop.value) untrackOverlay(backdrop.value)
  editorState.interiorPartsDialog = null
}

const statusText = computed(() => {
  const s = state.value
  if (!s) return 'Loading its floors…'
  if (s.status === 'finding') return 'Looking at each floor (vision model), then writing how each thing moves (LLM)…'
  if (s.status === 'failed') return `Nothing could be found: ${s.error ?? 'no answer'}`
  if (s.status === 'none') return 'No moving things were found: draw a box round one on its floor to add it.'
  const animating = allParts.value.find((p) => p.status === 'animating')
  if (animating) return `Animating ${kindLabel(animating.kind)} on floor ${animating.floor} (a few minutes each)…`
  if (s.status === 'found' || s.status === 'animated') {
    return `${allParts.value.filter((p) => !p.had).length} found · ${s.done ?? 0} animated${s.failed ? ` · ${s.failed} failed` : ''}`
  }
  return `It holds ${s.building ?? 'a building'}'s things: the ontology's ${s.candidates?.length ?? 0} that move are looked for.`
})
const statusOf = (p: Part) => ({ found: p.animate ? 'to animate' : 'left still', queued: 'waiting', animating: 'animating…', done: 'animated', failed: 'failed' }[p.status] ?? p.status)
</script>

<template>
  <div v-if="editorState.interiorPartsDialog !== null" ref="backdrop" class="backdrop ed-form" tabindex="-1" @mousedown.self="close" @keydown.esc.stop="close">
    <div class="sheet">
      <h3>Animate the interior<template v-if="state?.name"> of {{ state.name }}</template></h3>
      <p class="muted">
        The vision model looks at each floor for the things a {{ state?.building || 'building' }} holds that can move (the ontology's
        interior objects: a fireplace, a stove, a clock, a fan…), and the LLM writes how each moves. Check them, change any motion,
        draw a box round anything it missed, then animate the ones ticked: each plays on its floor (Inspector ▸ AnimatedPart).
      </p>
      <div class="row">
        <button class="primary" :disabled="!state || working" @click="find">{{ allParts.some((p) => !p.had) ? 'Find again' : 'Find the moving things' }}</button>
        <span class="muted grow">{{ statusText }}</span>
      </div>
      <p v-if="state?.warnings?.length" class="muted small">{{ state.warnings.join(' · ') }}</p>

      <div v-if="floors.length > 1" class="tabs">
        <button v-for="f in floors" :key="f.floor" :class="{ on: f.floor === current?.floor }" @click="floor = f.floor">
          Floor {{ f.floor }} ({{ allParts.filter((p) => p.floor === f.floor).length }})
        </button>
      </div>

      <div class="body">
        <div class="left">
          <div v-if="current" class="picture" :style="pictureStyle" @mousedown.prevent="down" @mousemove="move" @mouseup="up" @mouseleave="up">
            <div v-for="p in parts" :key="p.key" class="box" :class="[p.status, { sel: p.key === selected, off: !p.animate && p.status !== 'done' }]"
                 :style="boxStyle(p.box)" :title="`${kindLabel(p.kind)}: ${p.label}`"></div>
            <div v-for="p in parts" :key="`play-${p.key}`" class="played" :style="playStyle(p)"></div>
            <div v-if="drawing" class="box drawing" :style="boxStyle(drawing)"></div>
          </div>
          <p v-else class="muted">Loading its floors…</p>
          <div v-if="drawing && drawing[2] >= 4 && drawing[3] >= 4" class="row">
            <label>It's</label>
            <select v-model="drawKind">
              <option v-for="c in state?.candidates ?? []" :key="c.id" :value="c.id">{{ c.label }}</option>
            </select>
            <button class="small" @click="addDrawn">Add</button>
            <button class="small" @click="drawing = null">Cancel</button>
          </div>
          <p v-else class="muted small">Drag a box round a thing it missed to add it; click a box to pick its row.</p>
        </div>

        <div class="list">
          <p v-if="!parts.length" class="muted">Nothing on this floor yet.</p>
          <div v-for="p in parts" :key="p.key" class="part" :class="{ sel: p.key === selected }" @click="selected = p.key">
            <div class="head">
              <input type="checkbox" :checked="p.animate" :disabled="p.had || p.status === 'animating' || p.status === 'queued'"
                     title="Animate it" @change="set(p, 'animate', ($event.target as HTMLInputElement).checked)" />
              <strong>{{ p.had ? 'Animated before' : kindLabel(p.kind) }}</strong>
              <span class="muted small grow">{{ p.label }}</span>
              <span class="tag" :class="p.status">{{ statusOf(p) }}</span>
              <button v-if="!p.had" class="small" :disabled="p.status === 'animating' || p.status === 'queued'" title="Animate this one now"
                      @click.stop="run(p.key)">{{ p.status === 'done' ? 'Again' : 'Animate' }}</button>
              <button v-if="!p.had" class="small" title="Remove from the list (what's animated stays on the building)" @click.stop="remove(p)">✕</button>
            </div>
            <template v-if="!p.had">
              <textarea :value="p.motion" rows="2" spellcheck="false" title="What the animator is told: what moves and how"
                        @change="set(p, 'motion', ($event.target as HTMLTextAreaElement).value)"></textarea>
              <div class="row small">
                <label>Clip</label>
                <input class="clip" :value="p.clip" spellcheck="false" @change="set(p, 'clip', ($event.target as HTMLInputElement).value.trim())" />
                <label>Plays</label>
                <select :value="p.opens" @change="set(p, 'opens', ($event.target as HTMLSelectElement).value)">
                  <option value="always">always (a loop)</option>
                  <option value="use">when the player uses it</option>
                  <option value="auto">as someone comes near</option>
                </select>
                <template v-if="p.opens === 'use'">
                  <input class="prompt" :value="p.prompt" placeholder="Prompt" @change="set(p, 'prompt', ($event.target as HTMLInputElement).value)" />
                  <input class="secs" type="number" min="0" step="0.5" :value="p.duration" title="Seconds it plays (0: until used again)"
                         @change="set(p, 'duration', Number(($event.target as HTMLInputElement).value) || 0)" />
                </template>
              </div>
              <p v-if="p.error" class="error small">{{ p.error }}</p>
            </template>
          </div>
        </div>
      </div>

      <div class="buttons">
        <span class="muted small grow">{{ state?.chosen ?? 0 }} ticked · PixelLab (Preferences ▸ AI ▸ Non-humanoid animation) animates each</span>
        <button @click="close">{{ working ? 'Close (it goes on)' : 'Close' }}</button>
        <button v-if="working && allParts.some((p) => p.status === 'queued')" @click="stop">Stop</button>
        <button class="primary" :disabled="!state || working || !(state.chosen ?? 0)" @click="run()">Animate {{ state?.chosen ?? 0 }} ticked</button>
      </div>
    </div>
  </div>
</template>

<style scoped>
.backdrop {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.45);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 50;
}
.sheet {
  background: var(--panel, #222);
  border: 1px solid var(--border, #444);
  padding: 14px 16px;
  width: min(1060px, 96vw);
  max-height: 92vh;
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.row {
  display: flex;
  gap: 8px;
  align-items: center;
}
.grow {
  flex: 1;
}
.small {
  font-size: 11px;
}
.tabs {
  display: flex;
  gap: 2px;
}
.tabs button.on {
  border-color: var(--accent, #4a8);
  color: var(--accent, #4a8);
}
.body {
  display: flex;
  gap: 12px;
  min-height: 0;
  flex: 1;
}
.left {
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.picture {
  position: relative;
  cursor: crosshair;
  image-rendering: pixelated;
  outline: 1px solid var(--border, #444);
  background-repeat: no-repeat;
  flex: none;
}
.box {
  position: absolute;
  border: 2px solid #e8b030;
  background: rgba(232, 176, 48, 0.12);
  pointer-events: none;
}
.box.done {
  border-color: #3c3;
  background: none;
}
.box.failed {
  border-color: #e66;
}
.box.animating,
.box.queued {
  border-style: dashed;
}
.box.off {
  border-color: #888;
  background: none;
}
.box.sel {
  outline: 2px solid #fff;
}
.box.drawing {
  border-color: #3cf;
  background: rgba(60, 200, 255, 0.15);
}
.played {
  position: absolute;
  background-repeat: no-repeat;
  pointer-events: none;
}
.list {
  flex: 1;
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  gap: 6px;
  min-width: 340px;
}
.part {
  border: 1px solid var(--border, #444);
  padding: 6px 8px;
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.part.sel {
  border-color: var(--accent, #4a8);
}
.head {
  display: flex;
  gap: 6px;
  align-items: center;
}
.tag {
  font-size: 11px;
  padding: 0 4px;
  border: 1px solid var(--border, #444);
}
.tag.done {
  color: #3c3;
}
.tag.failed {
  color: #e66;
}
textarea {
  width: 100%;
  resize: vertical;
  box-sizing: border-box;
}
.clip {
  width: 70px;
}
.prompt {
  width: 110px;
}
.secs {
  width: 52px;
}
.error {
  color: #e66;
  margin: 0;
}
.buttons {
  display: flex;
  align-items: center;
  justify-content: flex-end;
  gap: 6px;
}
</style>
