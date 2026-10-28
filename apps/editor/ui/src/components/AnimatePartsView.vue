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
  status: 'pending' | 'finding' | 'animating' | 'done' | 'failed' | 'skipped'
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
const left = computed(() => shown.value.filter((t) => t.status === 'pending' || t.status === 'failed').length)
const credits = computed(() => 5 * shown.value.filter((t) => t.kind === 'thing' && (t.status === 'pending' || t.status === 'failed')).length)
const doors = computed(() => shown.value.filter((t) => t.kind === 'door').length)
const things = computed(() => shown.value.length - doors.value)
const current = computed(() => targets.value.find((t) => t.status === 'finding' || t.status === 'animating'))

const STATUS: Record<Target['status'], string> = {
  pending: 'to animate',
  finding: 'finding its door…',
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

function runAll(): void {
  sendToHost({ type: 'build:animateRun' })
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
        What moves in this place: <b>{{ doors }}</b> {{ doors === 1 ? 'door' : 'doors' }} of buildings (found in their
        pictures; they open as someone comes near, close as they go) and <b>{{ things }}</b>
        {{ things === 1 ? 'thing' : 'things' }} that are used (a chest, a gate, a lever: they play when used). A door is
        opened from its own pixels (free); a thing is animated by AutoSprite, about 5 credits each. More can be added later
        (right-click ▸ Animate a part…).
      </p>
      <div class="bar"><i :style="{ width: `${(100 * done) / Math.max(1, total)}%` }" /></div>
      <div class="row">
        <span class="dim">{{ done }} of {{ total }} animated<template v-if="failed"> · {{ failed }} failed</template>
          <template v-if="current"> · now: {{ current.label }} ({{ STATUS[current.status] }})</template></span>
        <span class="spacer"></span>
        <button v-if="running" @click="stop">Stop after this one</button>
        <button v-else class="primary" :disabled="left === 0" :title="credits ? `≈ ${credits} AutoSprite credits (the things; doors are free)` : 'Doors are opened from their own pixels: no credits'" @click="runAll">
          Animate all ({{ left }})
        </button>
      </div>
    </section>
    <p v-if="!shown.length" class="dim">Nothing that moves yet: buildings and used things show here once their art is made (or the place is painted).</p>
    <div v-for="t in shown" :key="t.key" class="target" :class="t.status">
      <div class="thumb" :style="{ width: `${t.region[2] * scaleOf(t)}px`, height: `${t.region[3] * scaleOf(t)}px` }"
           :title="t.status === 'done' ? 'Animated: click to see it big' : 'Click to see it big'" @click="big = t">
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
        <span class="status">{{ STATUS[t.status] }}</span>
        <button v-if="t.status === 'pending' || t.status === 'failed' || t.status === 'done'" :disabled="running" @click="runOne(t)">
          {{ t.status === 'done' ? 'Again' : 'Animate' }}
        </button>
        <button v-if="t.status !== 'finding' && t.status !== 'animating'" @click="skip(t)">{{ t.status === 'skipped' ? 'Include' : 'Skip' }}</button>
      </div>
    </div>
    <div v-if="big" class="big-backdrop" tabindex="-1" @mousedown.self="big = null" @keydown.esc="big = null">
      <div class="big-sheet">
        <div class="big-head">
          <b>{{ big.label }}</b>
          <span class="dim">{{ big.status === 'done' ? (big.kind === 'door' ? 'its door opening and closing' : 'its animation') : STATUS[big.status] }}</span>
          <span class="spacer"></span>
          <button @click="big = null">Close</button>
        </div>
        <div class="thumb big-thumb" :style="{ width: `${big.region[2] * bigScale(big)}px`, height: `${big.region[3] * bigScale(big)}px` }">
          <div class="pic" :style="picStyle(big, bigScale(big))"></div>
          <div v-if="big.preview && big.status === 'done' && big.kind === 'door' && big.box" class="play" :style="doorPlay(big, bigScale(big))"></div>
          <div v-else-if="big.preview && big.status === 'done' && big.kind === 'thing'" class="play thing" :style="thingPlay(big, bigScale(big))"></div>
          <div v-else-if="big.box" class="box" :style="boxStyle(big, bigScale(big))"></div>
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
