<script setup lang="ts">
// Project ▸ Effects ▸ Sheets ▸ Edit (docs/systems/effects-animation.md): an animation sheet edited by hand. Its picture
// cut into its grid's slots (click one: it's added to the chosen direction's frames), its clips (loop, speed), each
// clip's directions (frames in order, or the mirror of another direction), a preview playing, and pictures added as new
// slot rows (a strip of frames split into them). Saved as the aige.anim/1 file it is.
import { computed, onMounted, onUnmounted, reactive, ref, watch } from 'vue'
import { promptDialog } from '../dialogs'
import { onHostMessage, sendToHost } from '../ipc'

const props = defineProps<{ path: string }>()
const emit = defineEmits<{ close: [] }>()
const PROJECT_HOST = 'https://aige-project.local/'
const DIRECTIONS = ['s', 'se', 'e', 'ne', 'n', 'nw', 'w', 'sw']
const LOOPS = ['loop', 'once', 'pingpong', 'hold']

type FrameList = (number | { region: number[]; duration?: number })[]
interface ClipJson {
  loop?: string
  fps?: number
  frames?: FrameList
  directions?: Record<string, FrameList | string>
  events?: unknown[]
}
interface SheetJson {
  schema?: string
  texture: string
  grid?: { width?: number; height?: number }
  size?: [number, number]
  fps?: number
  clips: Record<string, ClipJson>
}

const sheet = ref<SheetJson | null>(null)
const saved = ref('')
const version = ref(0) // the picture's, after pictures are added
const clipName = ref('')
const row = ref('') // the direction frames are added to ('-': a clip without directions)
const newClip = ref('')
const tick = ref(0)
const adding = ref(false)
const images = ref<string[]>([])
const imageFilter = ref('')
const picked = reactive(new Set<string>())
let timer = 0

onHostMessage((message) => {
  if (message.type === 'fx:sheet' && message.path === props.path) {
    sheet.value = message.json as SheetJson
    saved.value = JSON.stringify(message.json)
    version.value++
    if (!clipName.value || !sheet.value.clips[clipName.value]) clipName.value = Object.keys(sheet.value.clips)[0] ?? ''
    row.value = rowsOf(clip.value)[0] ?? '-'
  } else if (message.type === 'assets:list') {
    images.value = ((message.assets as { path: string }[]) ?? []).map((a) => a.path).filter((p) => /\.png$/i.test(p) && !p.includes('/placeholders/'))
  }
})
onMounted(() => {
  sendToHost({ type: 'fx:sheet-read', path: props.path })
  timer = window.setInterval(() => tick.value++, 50)
})
onUnmounted(() => window.clearInterval(timer))
watch(() => props.path, () => sendToHost({ type: 'fx:sheet-read', path: props.path }))

const changed = computed(() => !!sheet.value && JSON.stringify(sheet.value) !== saved.value)
const gw = computed(() => sheet.value?.grid?.width ?? 0)
const gh = computed(() => sheet.value?.grid?.height ?? 0)
const size = computed<[number, number]>(() => sheet.value?.size ?? [gw.value, gh.value])
const columns = computed(() => (gw.value > 0 ? Math.max(1, Math.floor(size.value[0] / gw.value)) : 1))
const slots = computed(() => (gw.value > 0 && gh.value > 0 ? columns.value * Math.max(1, Math.floor(size.value[1] / gh.value)) : 0))
const clip = computed<ClipJson | undefined>(() => sheet.value?.clips[clipName.value])
const scale = computed(() => (gw.value > 0 ? Math.min(3, Math.max(0.5, 56 / Math.max(gw.value, gh.value))) : 1))

function rowsOf(c?: ClipJson): string[] {
  if (!c) return []
  return c.directions ? Object.keys(c.directions) : ['-']
}
function framesOf(c: ClipJson | undefined, dir: string): FrameList {
  if (!c) return []
  if (dir === '-') return c.frames ?? []
  const list = c.directions?.[dir]
  return Array.isArray(list) ? list : []
}
function mirrorOf(c: ClipJson | undefined, dir: string): string {
  const list = c?.directions?.[dir]
  return typeof list === 'string' && list.startsWith('mirror:') ? list.slice(7) : ''
}

// A slot's (or a region's) look, at `s` times its size.
function slotStyle(index: number, s = scale.value): Record<string, string> {
  return frameStyle({ region: [(index % columns.value) * gw.value, Math.floor(index / columns.value) * gh.value, gw.value, gh.value] }, s)
}
function frameStyle(f: number | { region: number[] }, s = scale.value): Record<string, string> {
  if (!sheet.value) return {}
  const r = typeof f === 'number' ? [(f % columns.value) * gw.value, Math.floor(f / columns.value) * gh.value, gw.value, gh.value] : f.region
  const [x, y, w, h] = [r[0] ?? 0, r[1] ?? 0, r[2] ?? 0, r[3] ?? 0]
  return {
    width: `${w * s}px`,
    height: `${h * s}px`,
    backgroundImage: `url("${PROJECT_HOST}${sheet.value.texture}?v=${version.value}")`,
    backgroundPosition: `-${x * s}px -${y * s}px`,
    backgroundSize: `${size.value[0] * s}px ${size.value[1] * s}px`,
    imageRendering: w <= 128 ? 'pixelated' : 'auto',
  }
}
// The chosen row, playing at its clip's speed.
const playing = computed(() => {
  const c = clip.value
  let list = framesOf(c, row.value)
  const mirror = mirrorOf(c, row.value)
  if (mirror) list = framesOf(c, mirror)
  if (!list.length) return null
  const fps = c?.fps ?? sheet.value?.fps ?? 8
  return { frame: list[Math.floor((tick.value * 0.05 * fps)) % list.length]!, flip: !!mirror }
})

function addSlot(index: number): void {
  const c = clip.value
  if (!c) return
  if (row.value === '-') (c.frames ??= []).push(index)
  else {
    c.directions ??= {}
    const list = c.directions[row.value]
    if (Array.isArray(list)) list.push(index)
    else c.directions[row.value] = [index] // a mirror becomes its own frames
  }
}
function removeFrame(dir: string, k: number): void {
  framesOf(clip.value, dir).splice(k, 1)
}
function moveFrame(dir: string, k: number, by: number): void {
  const list = framesOf(clip.value, dir)
  const to = k + by
  if (to < 0 || to >= list.length) return
  ;[list[k], list[to]] = [list[to]!, list[k]!]
}
function addDirection(dir: string): void {
  const c = clip.value
  if (!c || !dir) return
  if (!c.directions) {
    // From a clip without directions: its frames become this direction's.
    c.directions = { [dir]: c.frames ?? [] }
    delete c.frames
  } else if (!(dir in c.directions)) c.directions[dir] = []
  row.value = dir
}
function removeDirection(dir: string): void {
  const c = clip.value
  if (!c?.directions) return
  delete c.directions[dir]
  if (!Object.keys(c.directions).length) {
    delete c.directions
    c.frames = []
  }
  row.value = rowsOf(c)[0] ?? '-'
}
function setMirror(dir: string, of: string): void {
  const c = clip.value
  if (!c?.directions) return
  c.directions[dir] = of ? `mirror:${of}` : []
}
function addClip(): void {
  const name = newClip.value.trim().replace(/\s+/g, '_')
  if (!sheet.value || !name || sheet.value.clips[name]) return
  sheet.value.clips[name] = { loop: name === 'walk' || name === 'idle' ? 'loop' : 'once', frames: [] }
  clipName.value = name
  row.value = '-'
  newClip.value = ''
}
async function renameClip(): Promise<void> {
  if (!sheet.value || !clip.value) return
  const name = (await promptDialog({ title: 'Rename clip', message: 'Idle, walk, attack, cast, hurt and die are what the animators play.', value: clipName.value }))?.trim()
  if (!sheet.value || !clip.value) return
  if (!name || name === clipName.value || sheet.value.clips[name]) return
  sheet.value.clips[name] = clip.value
  delete sheet.value.clips[clipName.value]
  clipName.value = name
}
function removeClip(): void {
  if (!sheet.value || !clip.value) return
  delete sheet.value.clips[clipName.value]
  clipName.value = Object.keys(sheet.value.clips)[0] ?? ''
  row.value = rowsOf(clip.value)[0] ?? '-'
}
function save(): void {
  if (sheet.value) sendToHost({ type: 'fx:sheet-save', path: props.path, json: JSON.parse(JSON.stringify(sheet.value)) })
}
function revert(): void {
  sendToHost({ type: 'fx:sheet-read', path: props.path })
}
function openAdd(): void {
  adding.value = true
  picked.clear()
  sendToHost({ type: 'assets:request' })
}
function addPictures(): void {
  if (changed.value) save() // the file first, then the pictures onto it
  sendToHost({ type: 'fx:sheet-addPictures', path: props.path, pictures: [...picked] })
  adding.value = false
}
const shownImages = computed(() => {
  const words = imageFilter.value.toLowerCase().split(/\s+/).filter(Boolean)
  return images.value.filter((p) => p !== sheet.value?.texture && words.every((w) => p.toLowerCase().includes(w))).slice(0, 150)
})
</script>

<template>
  <div v-if="sheet" class="sheet-editor">
    <div class="bar">
      <b>{{ path.split('/').pop() }}</b>
      <span class="muted small">{{ sheet.texture.split('/').pop() }} · slots {{ gw }}×{{ gh }} · {{ slots }} slots</span>
      <label class="small">fps <input v-model.number="sheet.fps" type="number" min="1" max="60" class="num" /></label>
      <span class="spacer"></span>
      <button title="Images of the project as new slots (a strip of frames is split into them)" @click="openAdd">Add pictures…</button>
      <button :disabled="!changed" @click="revert">Revert</button>
      <button class="primary" :disabled="!changed" @click="save">Save</button>
      <button @click="emit('close')">Done</button>
    </div>
    <div v-if="adding" class="adding">
      <input v-model="imageFilter" placeholder="Find images: walk, knight-walk, east…" spellcheck="false" />
      <div class="images">
        <button v-for="p in shownImages" :key="p" class="image" :class="{ on: picked.has(p) }" :title="p" @click="picked.has(p) ? picked.delete(p) : picked.add(p)">
          <img :src="PROJECT_HOST + p" alt="" loading="lazy" />
        </button>
      </div>
      <div class="row">
        <span class="muted small">{{ picked.size }} picked, added in the order picked as new rows of slots</span>
        <span class="spacer"></span>
        <button @click="adding = false">Cancel</button>
        <button class="primary" :disabled="!picked.size" @click="addPictures">Add</button>
      </div>
    </div>
    <div class="split">
      <section class="slots">
        <p v-if="!slots" class="muted small">This sheet's frames are regions, not a grid: edit them in its file.</p>
        <div class="grid">
          <button v-for="i in slots" :key="i" class="slot" :title="`slot ${i - 1}: add to ${clipName} ${row === '-' ? '' : row}`" @click="addSlot(i - 1)">
            <div :style="slotStyle(i - 1)"></div>
            <span class="index">{{ i - 1 }}</span>
          </button>
        </div>
      </section>
      <section class="clips">
        <div class="clip-tabs">
          <button v-for="(c, name) in sheet.clips" :key="name" :class="{ on: name === clipName }" @click="(clipName = String(name)), (row = rowsOf(c)[0] ?? '-')">{{ name }}</button>
          <input v-model="newClip" placeholder="new clip" class="new" spellcheck="false" @keydown.enter="addClip" />
          <button :disabled="!newClip.trim()" @click="addClip">Add</button>
        </div>
        <template v-if="clip">
          <div class="row">
            <label class="small">loop</label>
            <select v-model="clip.loop">
              <option v-for="l in LOOPS" :key="l" :value="l">{{ l }}</option>
            </select>
            <label class="small">fps</label>
            <input v-model.number="clip.fps" type="number" min="1" max="60" class="num" :placeholder="String(sheet.fps ?? 8)" />
            <span class="spacer"></span>
            <button @click="renameClip">Rename</button>
            <button class="danger" @click="removeClip">Remove</button>
          </div>
          <div class="preview">
            <div v-if="playing" :style="{ ...frameStyle(playing.frame, scale * 1.6), transform: playing.flip ? 'scaleX(-1)' : '' }"></div>
            <span v-else class="muted small">no frames yet: click slots on the left</span>
          </div>
          <div v-for="dir in rowsOf(clip)" :key="dir" class="dir-row" :class="{ on: dir === row }" @click="row = dir">
            <span class="dir">{{ dir === '-' ? 'frames' : dir }}</span>
            <template v-if="mirrorOf(clip, dir)">
              <span class="muted small">mirror of {{ mirrorOf(clip, dir) }}</span>
            </template>
            <div v-else class="frames">
              <div v-for="(f, k) in framesOf(clip, dir)" :key="k" class="frame">
                <div :style="frameStyle(f, scale * 0.8)"></div>
                <div class="frame-tools">
                  <button title="Earlier" @click.stop="moveFrame(dir, k, -1)">‹</button>
                  <button title="Remove" @click.stop="removeFrame(dir, k)">×</button>
                  <button title="Later" @click.stop="moveFrame(dir, k, 1)">›</button>
                </div>
              </div>
              <span v-if="!framesOf(clip, dir).length" class="muted small">click slots to add frames</span>
            </div>
            <span class="spacer"></span>
            <select v-if="dir !== '-'" :value="mirrorOf(clip, dir)" title="Drawn as another direction's frames, flipped" @click.stop @change="setMirror(dir, ($event.target as HTMLSelectElement).value)">
              <option value="">own frames</option>
              <option v-for="d in DIRECTIONS.filter((x) => x !== dir)" :key="d" :value="d">mirror of {{ d }}</option>
            </select>
            <button v-if="dir !== '-'" title="Remove this direction" @click.stop="removeDirection(dir)">×</button>
          </div>
          <div class="row">
            <label class="small">add direction</label>
            <select :value="''" @change="addDirection(($event.target as HTMLSelectElement).value)">
              <option value="">…</option>
              <option v-for="d in DIRECTIONS.filter((x) => !(clip!.directions && x in clip!.directions))" :key="d" :value="d">{{ d }}</option>
            </select>
          </div>
        </template>
      </section>
    </div>
  </div>
  <p v-else class="muted small">Opening {{ path }}…</p>
</template>

<style scoped>
.sheet-editor {
  display: flex;
  flex-direction: column;
  height: 100%;
  min-height: 0;
  gap: 6px;
}
.bar,
.row {
  display: flex;
  align-items: center;
  gap: 6px;
  flex-wrap: wrap;
}
.spacer {
  flex: 1;
}
.muted {
  opacity: 0.6;
}
.small {
  font-size: 0.82em;
}
.num {
  width: 52px;
}
.split {
  display: flex;
  gap: 8px;
  flex: 1;
  min-height: 0;
}
.slots {
  width: 42%;
  overflow: auto;
  border-right: 1px solid #2a2a3a;
  padding-right: 6px;
}
.grid {
  display: flex;
  flex-wrap: wrap;
  gap: 3px;
}
.slot {
  position: relative;
  padding: 2px;
  background: #1b1b26;
  border: 1px solid #2f2f3f;
}
.slot:hover {
  border-color: #3b82f6;
}
.index {
  position: absolute;
  right: 2px;
  bottom: 1px;
  font-size: 0.65em;
  opacity: 0.55;
}
.clips {
  flex: 1;
  overflow: auto;
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.clip-tabs {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
  align-items: center;
}
.clip-tabs button.on {
  border-color: #3b82f6;
  color: #fff;
}
.clip-tabs .new {
  width: 90px;
}
.preview {
  height: 120px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: #15151e;
}
.dir-row {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 4px;
  border: 1px solid #262635;
  cursor: pointer;
}
.dir-row.on {
  border-color: #3b82f6;
}
.dir {
  width: 44px;
  font-weight: 600;
}
.frames {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}
.frame {
  display: flex;
  flex-direction: column;
  align-items: center;
  background: #1b1b26;
  padding: 2px;
}
.frame-tools {
  display: flex;
}
.frame-tools button {
  padding: 0 4px;
  font-size: 0.75em;
}
.adding {
  border: 1px solid #333;
  padding: 6px;
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.images {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
  max-height: 160px;
  overflow-y: auto;
}
.image {
  width: 54px;
  height: 54px;
  padding: 2px;
  background: #22222e;
  border: 1px solid transparent;
}
.image.on {
  border-color: #3b82f6;
}
.image img {
  max-width: 48px;
  max-height: 48px;
}
.danger {
  color: #f87171;
}
</style>
