<script setup lang="ts">
// Build ▸ Animate (docs/assets/animations.md §4.1): what moves in the open build — each building's door (found in its
// art, or in its part of the painting), each thing that's used (a chest, a gate, a lever) — animated one at a time
// (Generate ▸ Animate's provider), each with what triggers it. Progress, where each is, its door's box.
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { onHostMessage, sendToHost } from '../ipc'

const PROJECT_HOST = 'https://aige-project.local/'

interface Target {
  key: string
  kind: 'door' | 'thing'
  role: string
  label: string
  texture: string
  region: [number, number, number, number]
  box?: [number, number, number, number]
  trigger: string
  where: string
  status: 'pending' | 'finding' | 'identified' | 'animating' | 'done' | 'failed' | 'skipped'
  error?: string
  preview?: { sheet: string; count: number; fps: number } // animated: its frames in a row, played here
}

const props = defineProps<{ only?: 'door' | 'thing' }>()
const targets = ref<Target[]>([])
const running = ref(false)
const done = ref(0)
const failed = ref(0)
const total = ref(0)

onHostMessage((message) => {
  if (message.type !== 'build:animate') return
  targets.value = (message.targets as Target[]) ?? []
  running.value = Boolean(message.running)
  done.value = (message.done as number) ?? 0
  failed.value = (message.failed as number) ?? 0
  total.value = (message.total as number) ?? 0
})
onMounted(() => sendToHost({ type: 'build:animateScan' }))

// The animated ones play: opening, held open, closing, held shut (a door), on one clock.
const clock = ref(0)
const timer = window.setInterval(() => (clock.value += 1 / 20), 50)
onUnmounted(() => window.clearInterval(timer))
function frameOf(p: NonNullable<Target['preview']>): number {
  const n = Math.max(1, p.count)
  const run = n / Math.max(1, p.fps)
  const hold = 0.8
  const t = clock.value % (2 * (run + hold))
  if (t < run) return Math.min(n - 1, Math.floor(t * p.fps))
  if (t < run + hold) return n - 1
  if (t < 2 * run + hold) return Math.max(0, n - 1 - Math.floor((t - run - hold) * p.fps))
  return 0
}
// Its frame: the spritesheet scaled so one frame fills the box, moved to the frame (percentages: no pixel sizes needed).
function playStyle(t: Target, w: number, h: number): Record<string, string> {
  const p = t.preview!
  const n = Math.max(1, p.count)
  return {
    width: `${w}px`,
    height: `${h}px`,
    backgroundImage: `url('${PROJECT_HOST}${p.sheet}')`,
    backgroundSize: `${n * 100}% 100%`,
    backgroundPosition: `${n > 1 ? (frameOf(p) / (n - 1)) * 100 : 0}% 0`,
  }
}
// A door plays over its box on the building; a thing plays over its whole picture.
function doorPlay(t: Target, s = scaleOf(t)): Record<string, string> {
  const [x, y] = t.region
  const [bx, by, bw, bh] = t.box!
  return { ...playStyle(t, bw * s, bh * s), left: `${(bx - x) * s}px`, top: `${(by - y) * s}px` }
}
function thingPlay(t: Target, s = scaleOf(t)): Record<string, string> {
  return { ...playStyle(t, t.region[2] * s, t.region[3] * s), left: '0', top: '0' }
}

const shown = computed(() => targets.value.filter((t) => !props.only || t.kind === props.only))
// ① Doors still to identify; ② what's ready to animate (doors identified, things that are used).
const toIdentify = computed(() => shown.value.filter((t) => t.kind === 'door' && (t.status === 'pending' || (t.status === 'failed' && !t.box))).length)
const toAnimate = computed(() => shown.value.filter((t) => (t.kind === 'door' ? t.status === 'identified' : t.status === 'pending' || t.status === 'failed')).length)
const identified = computed(() => shown.value.filter((t) => t.kind === 'door' && t.status === 'identified').length)
const doors = computed(() => new Set(shown.value.filter((t) => t.kind === 'door').map((t) => t.key.split('#')[0])).size) // buildings
const things = computed(() => shown.value.length - doors.value)
const current = computed(() => targets.value.find((t) => t.status === 'finding' || t.status === 'animating'))

const STATUS: Record<Target['status'], string> = {
  pending: 'to do',
  finding: 'identifying its doors…',
  identified: 'door found: check it',
  animating: 'animating…',
  done: 'animated',
  failed: 'failed',
  skipped: 'skipped',
}

// Its door's box over its picture (the thumbnail is its region, scaled to at most 120 x 90 px).
function boxStyle(t: Target, s = scaleOf(t)): Record<string, string> {
  if (!t.box) return { display: 'none' }
  const [x, y] = t.region
  return { left: `${(t.box[0] - x) * s}px`, top: `${(t.box[1] - y) * s}px`, width: `${t.box[2] * s}px`, height: `${t.box[3] * s}px` }
}
function scaleOf(t: Target): number {
  return Math.min(120 / Math.max(1, t.region[2]), 90 / Math.max(1, t.region[3]))
}
// The big view (a click on a picture): as large as fits the window, at most 4x.
const big = ref<Target | null>(null)
// In the big view: boxes drawn round its doors by hand (the picture's pixels), one door each, to redo them from.
const drawing = ref(false)
const drawn = ref<[number, number, number, number][]>([])
let drawFrom: [number, number] | null = null
function openBig(t: Target): void {
  big.value = t
  drawing.value = false
  drawn.value = []
}
// The building's doors as they are (its targets: "<key>" and "<key>#n"), shown in the big view.
function doorsOf(t: Target): Target[] {
  const base = t.key.split('#')[0]
  return targets.value.filter((o) => o.key.split('#')[0] === base)
}
const usable = computed(() => drawn.value.filter((b) => b[2] >= 4 && b[3] >= 6))
function pointAt(event: MouseEvent, t: Target): [number, number] {
  const r = (event.currentTarget as HTMLElement).getBoundingClientRect()
  const s = bigScale(t)
  return [Math.round(t.region[0] + (event.clientX - r.left) / s), Math.round(t.region[1] + (event.clientY - r.top) / s)]
}
function drawDown(event: MouseEvent): void {
  if (!drawing.value || !big.value) return
  drawFrom = pointAt(event, big.value)
  drawn.value.push([drawFrom[0], drawFrom[1], 1, 1]) // another door
}
function drawMove(event: MouseEvent): void {
  if (!drawFrom || !big.value) return
  const [x, y] = pointAt(event, big.value)
  drawn.value[drawn.value.length - 1] = [Math.min(x, drawFrom[0]), Math.min(y, drawFrom[1]), Math.abs(x - drawFrom[0]) + 1, Math.abs(y - drawFrom[1]) + 1]
}
function drawUp(): void {
  // A click without a drag leaves no box.
  if (drawFrom && drawn.value.length) {
    const last = drawn.value[drawn.value.length - 1]!
    if (last[2] < 4 || last[3] < 6) drawn.value.pop()
  }
  drawFrom = null
}
function clearDrawn(): void {
  drawn.value = []
}
function rectStyle(t: Target, b: number[]): Record<string, string> {
  const s = bigScale(t)
  const [x, y, w, h] = b as [number, number, number, number]
  return { left: `${(x - t.region[0]) * s}px`, top: `${(y - t.region[1]) * s}px`, width: `${w * s}px`, height: `${h * s}px` }
}
// The boxes drawn: the building's doors are those (identified, ready for ② Animate).
function useDrawn(t: Target): void {
  if (!usable.value.length) return
  sendToHost({ type: 'build:animateSetDoors', key: t.key, boxes: usable.value })
  drawing.value = false
  drawn.value = []
  big.value = null
}
function identifyOne(t: Target): void {
  sendToHost({ type: 'build:animateIdentify', key: t.key })
  big.value = null
}
function animateOne(t: Target): void {
  sendToHost({ type: 'build:animateRun', key: t.key })
  big.value = null
}
const pendingLabel = (t: Target) => (t.kind === 'door' ? 'to identify' : 'to animate')
function bigScale(t: Target): number {
  return Math.min((window.innerWidth * 0.8) / Math.max(1, t.region[2]), (window.innerHeight * 0.75) / Math.max(1, t.region[3]), 4)
}
function picStyle(t: Target, s: number): Record<string, string> {
  return {
    width: `${t.region[2]}px`,
    height: `${t.region[3]}px`,
    backgroundImage: `url('${PROJECT_HOST}${t.texture}')`,
    backgroundPosition: `-${t.region[0]}px -${t.region[1]}px`,
    transform: `scale(${s})`,
  }
}

function identifyAll(): void {
  sendToHost({ type: 'build:animateIdentify' })
}
function runAll(): void {
  sendToHost({ type: 'build:animateRun' })
}
// Everything already animated, animated again (with Preferences ▸ AI's animators now: PixelLab for doors and things).
const doneCount = computed(() => shown.value.filter((t) => t.status === 'done').length)
function againAll(): void {
  sendToHost({ type: 'build:animateRun', again: true })
}
function runOne(t: Target): void {
  sendToHost({ type: 'build:animateRun', key: t.key })
}
function skip(t: Target): void {
  sendToHost({ type: t.status === 'skipped' ? 'build:animateUnskip' : 'build:animateSkip', key: t.key })
}
function stop(): void {
  sendToHost({ type: 'build:animateStop' })
}
</script>

<template>
  <div class="animate-parts">
    <section class="head">
      <p>
        What moves in this place: <b>{{ doors }}</b> {{ doors === 1 ? 'building' : 'buildings' }} with doors and <b>{{ things }}</b>
        {{ things === 1 ? 'thing' : 'things' }} that are used (a chest, a gate, a lever). Two steps:
      </p>
      <ol class="steps-info">
        <li><b>Identify doors.</b> Each building's doors are looked for in its picture (the vision model; a minute or so each)
          and shown with their boxes. Check them: click a picture to see it big, and where a box is off or a door is missing,
          <i>Draw the doors</i> (a box each; Clear to start again) and <i>Use these doors</i>.</li>
        <li><b>Animate.</b> Each door identified is opened by PixelLab from its own picture (a few minutes each, PixelLab's
          queue), kept to its doorway; each thing that's used is animated too (PixelLab; AutoSprite for people: Preferences ▸
          AI). They play here as they're done, and in the game as the project's doors open (Project Settings ▸ Game).</li>
      </ol>
      <div class="bar"><i :style="{ width: `${(100 * done) / Math.max(1, total)}%` }" /></div>
      <div class="row">
        <span class="dim">{{ done }} of {{ total }} animated<template v-if="failed"> · {{ failed }} failed</template>
          <template v-if="current"> · now: {{ current.label }} ({{ STATUS[current.status] }})</template></span>
        <span class="spacer"></span>
        <button v-if="running" @click="stop">Stop after this one</button>
        <template v-else>
          <button :class="{ primary: toIdentify > 0 }" :disabled="toIdentify === 0" title="① Look for each building's doors in its picture"
                  @click="identifyAll">① Identify doors ({{ toIdentify }})</button>
          <button :class="{ primary: toIdentify === 0 }" :disabled="toAnimate === 0"
                  :title="identified ? `${identified} doors identified (and the things) animated by PixelLab` : 'Identify the doors first'"
                  @click="runAll">② Animate ({{ toAnimate }})</button>
          <button v-if="doneCount" title="Everything already animated, animated again (Preferences ▸ AI ▸ Non-humanoid animation for doors and things)"
                  @click="againAll">Animate all again ({{ doneCount }})</button>
        </template>
      </div>
    </section>
    <p v-if="!shown.length" class="dim">Nothing that moves yet: buildings and used things show here once their art is made (or the place is painted).</p>
    <div v-for="t in shown" :key="t.key" class="target" :class="t.status">
      <div class="thumb" :style="{ width: `${t.region[2] * scaleOf(t)}px`, height: `${t.region[3] * scaleOf(t)}px` }"
           :title="t.status === 'done' ? 'Animated: click to see it big' : 'Click to see it big'" @click="openBig(t)">
        <div class="pic" :style="picStyle(t, scaleOf(t))"></div>
        <div v-if="t.preview && t.status === 'done' && t.kind === 'door' && t.box" class="play" :style="doorPlay(t)"></div>
        <div v-else-if="t.preview && t.status === 'done' && t.kind === 'thing'" class="play thing" :style="thingPlay(t)"></div>
        <div v-else-if="t.box" class="box" :style="boxStyle(t)"></div>
      </div>
      <div class="info">
        <div><b>{{ t.label }}</b> <span class="chip">{{ t.kind === 'door' ? 'door' : t.role }}</span></div>
        <div class="dim small">{{ t.where }}</div>
        <div class="dim small">Triggered: {{ t.trigger }}</div>
        <div v-if="t.box" class="dim small">Door at {{ t.box[0] }},{{ t.box[1] }} ({{ t.box[2] }}×{{ t.box[3] }} px)</div>
        <div v-if="t.error" class="error small">{{ t.error }}</div>
      </div>
      <div class="side">
        <span class="status">{{ t.status === 'pending' ? pendingLabel(t) : STATUS[t.status] }}</span>
        <button v-if="t.kind === 'door' && t.status !== 'finding' && t.status !== 'animating' && t.status !== 'skipped'" :disabled="running"
                title="Look for its doors again" @click="identifyOne(t)">Identify</button>
        <button v-if="(t.kind === 'door' ? t.box && ['identified', 'failed', 'done'].includes(t.status) : ['pending', 'failed', 'done'].includes(t.status))"
                :disabled="running" @click="runOne(t)">{{ t.status === 'done' ? 'Again' : 'Animate' }}</button>
        <button v-if="t.status !== 'finding' && t.status !== 'animating'" @click="skip(t)">{{ t.status === 'skipped' ? 'Include' : 'Skip' }}</button>
      </div>
    </div>
    <div v-if="big" class="big-backdrop" tabindex="-1" @mousedown.self="big = null" @keydown.esc="big = null">
      <div class="big-sheet">
        <div class="big-head">
          <b>{{ big.label }}</b>
          <span class="dim">{{ big.status === 'done' ? (big.kind === 'door' ? 'its door opening and closing' : 'its animation') : STATUS[big.status] }}</span>
          <span class="spacer"></span>
          <template v-if="big.kind === 'door'">
            <button :class="{ active: drawing }" :disabled="running"
                    title="Drag a box round each door (as many as it has), then Use these doors"
                    @click="drawing = !drawing; drawn = []">{{ drawing ? 'Drawing doors…' : 'Draw the doors' }}</button>
            <button v-if="drawing" :disabled="!drawn.length" title="Clear the boxes drawn" @click="clearDrawn">Clear</button>
            <button v-if="drawing" class="primary" :disabled="running || !usable.length"
                    title="Its doors are the ones you drew (identified: ② Animate opens them)" @click="useDrawn(big)">
              Use {{ usable.length === 1 ? 'this door' : `these ${usable.length} doors` }}
            </button>
            <template v-else>
              <button :disabled="running" title="Its doors looked for again" @click="identifyOne(big)">Identify again</button>
              <button class="primary" :disabled="running || !big.box" title="This door opened by PixelLab, from its box" @click="animateOne(big)">
                {{ big.status === 'done' ? 'Animate again' : 'Animate' }}
              </button>
            </template>
          </template>
          <button v-else class="primary" :disabled="running" title="Animated again (PixelLab, or AutoSprite for a person)" @click="animateOne(big)">
            {{ big.status === 'done' ? 'Animate again' : 'Animate' }}
          </button>
          <button @click="big = null">Close</button>
        </div>
        <p v-if="drawing" class="dim small">Drag round each door, threshold to top and frame to frame (a double door as one box); what's
          in front of it (a post, a step) is kept drawn. The building's doors become the ones you draw.</p>
        <p v-else-if="big.kind === 'door' && doorsOf(big).length > 1" class="dim small">{{ doorsOf(big).length }} doors: each has its own row in the list.</p>
        <div class="thumb big-thumb" :class="{ drawing }" :style="{ width: `${big.region[2] * bigScale(big)}px`, height: `${big.region[3] * bigScale(big)}px` }"
             @mousedown.prevent="drawDown" @mousemove="drawMove" @mouseup="drawUp" @mouseleave="drawUp">
          <div class="pic" :style="picStyle(big, bigScale(big))"></div>
          <div v-if="big.preview && big.status === 'done' && big.kind === 'door' && big.box" class="play" :style="doorPlay(big, bigScale(big))"></div>
          <div v-else-if="big.preview && big.status === 'done' && big.kind === 'thing'" class="play thing" :style="thingPlay(big, bigScale(big))"></div>
          <div v-else-if="big.box" class="box" :style="boxStyle(big, bigScale(big))"></div>
          <template v-if="drawing">
            <div v-for="(b, i) in drawn" :key="i" class="box drawn" :style="rectStyle(big, b)"><span class="num">{{ i + 1 }}</span></div>
          </template>
          <template v-else-if="big.kind === 'door'">
            <div v-for="o in doorsOf(big).filter((o) => o.key !== big!.key && o.box)" :key="o.key" class="box other" :style="rectStyle(big, o.box!)"></div>
          </template>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.animate-parts {
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.head p {
  margin: 0 0 6px;
}
.bar {
  height: 6px;
  background: var(--border, #444);
}
.bar i {
  display: block;
  height: 100%;
  background: var(--accent, #4a8);
}
.row {
  display: flex;
  gap: 8px;
  align-items: center;
  margin-top: 6px;
}
.spacer {
  flex: 1;
}
.target {
  display: flex;
  gap: 10px;
  align-items: flex-start;
  padding: 6px;
  border: 1px solid var(--border, #444);
}
.target.done {
  border-color: var(--accent, #4a8);
}
.target.failed {
  border-color: #c55;
}
.target.skipped {
  opacity: 0.5;
}
.thumb {
  position: relative;
  overflow: hidden;
  flex: none;
  background: #222;
}
.pic {
  transform-origin: 0 0;
  background-repeat: no-repeat;
  image-rendering: pixelated;
}
.thumb {
  cursor: zoom-in;
}
.big-backdrop {
  position: fixed;
  inset: 0;
  z-index: 60;
  background: rgba(0, 0, 0, 0.6);
  display: flex;
  align-items: center;
  justify-content: center;
}
.big-sheet {
  background: var(--panel, #222);
  border: 1px solid var(--border, #444);
  padding: 10px 12px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.big-head {
  display: flex;
  gap: 10px;
  align-items: center;
}
.big-thumb {
  cursor: default;
}
.big-thumb.drawing {
  cursor: crosshair;
}
.steps-info {
  margin: 0 0 8px;
  padding-left: 20px;
  line-height: 1.45;
}
.steps-info li {
  margin-bottom: 4px;
}
.box.drawn {
  border-color: #f5c542;
}
.box.other {
  border-style: dashed;
}
.box .num {
  position: absolute;
  left: 0;
  top: 0;
  padding: 0 3px;
  background: #f5c542;
  color: #111;
  font-size: 11px;
}
button.active {
  border-color: var(--accent, #4a8);
}
.play {
  position: absolute;
  background-repeat: no-repeat;
  pointer-events: none;
}
.play.thing {
  background-color: var(--panel, #222);
}
.box {
  position: absolute;
  border: 2px solid #3c3;
  pointer-events: none;
}
.info {
  flex: 1;
  display: flex;
  flex-direction: column;
  gap: 2px;
}
.side {
  display: flex;
  flex-direction: column;
  gap: 4px;
  align-items: flex-end;
}
.status {
  font-size: 0.85em;
}
.chip {
  font-size: 0.8em;
  padding: 0 4px;
  border: 1px solid var(--border, #444);
}
.small {
  font-size: 0.85em;
}
.error {
  color: #e66;
}
</style>
