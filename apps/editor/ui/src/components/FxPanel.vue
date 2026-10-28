<script setup lang="ts">
// Animation and effects (development_plan.md 18.7, docs/systems/effects-animation.md):
// sheets and their clips (frames previewed, imported from Aseprite or a grid),
// particles and effects to try in the viewport, and cutscenes on a timeline. Records
// of particles, effects and animators are edited in Records; Customise copies an
// engine preset there.
import { computed, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue'
import { editorState } from '../editorState'
import SheetEditor from './SheetEditor.vue'
import { onHostMessage, sendToHost } from '../ipc'

const PROJECT_HOST = 'https://aige-project.local/'

interface Frame {
  x: number
  y: number
  w: number
  h: number
  duration: number
}
interface Clip {
  name: string
  loop: string
  directions: Record<string, Frame[]>
  mirrors: Record<string, string>
  events: number
}
interface Sheet {
  path: string
  texture: string
  size: [number, number]
  clips: Clip[]
}
interface Listed {
  id: string
  source: string
}
interface CutsceneInfo {
  id: string
  name: string
  seconds: number
  problems: string[]
}
interface Key {
  at: number
  seconds?: number
  op?: string
  [k: string]: unknown
}
interface Track {
  type: string
  actor?: string
  keys: Key[]
}
interface CutsceneDoc {
  name: string
  letterbox?: boolean
  length?: number
  tracks: Track[]
}

const section = ref<'sheets' | 'effects' | 'cutscenes'>('sheets')
const sheets = ref<Sheet[]>([])
const problems = ref<string[]>([])
const particles = ref<Listed[]>([])
const effects = ref<Listed[]>([])
const cutscenes = ref<CutsceneInfo[]>([])
const selectedSheet = ref('')
const editing = ref(false) // the selected sheet open in the sheet editor
// Assets ▸ Animations ▸ Show in Effects: that sheet, on the Sheets page.
watch(
  () => editorState.fxSheet,
  (path) => {
    if (!path) return
    section.value = 'sheets'
    selectedSheet.value = path
    editing.value = editorState.fxSheetEdit
    editorState.fxSheet = ''
    editorState.fxSheetEdit = false
  },
  { immediate: true },
)
const playing = reactive<Record<string, number>>({}) // clip key -> frame index
let timer: number | undefined

const grid = reactive({ texture: '', width: 32, height: 32, columns: 4, rows: 'idle, walk_s, walk_e, walk_n', fps: 8 })
const asepritePath = ref('')

const cutsceneId = ref('')
const cutscene = ref<CutsceneDoc | null>(null)
const selectedKey = ref<{ track: number; key: number } | null>(null)
const keyText = ref('')
const keyError = ref('')
const newCutsceneId = ref('')

onHostMessage((message) => {
  if (message.type === 'fx:model') {
    sheets.value = message.sheets as Sheet[]
    problems.value = message.problems as string[]
    particles.value = message.particles as Listed[]
    effects.value = message.effects as Listed[]
    cutscenes.value = message.cutscenes as CutsceneInfo[]
  } else if (message.type === 'fx:cutscene') {
    cutscene.value = (message.cutscene as CutsceneDoc | null) ?? { name: '', tracks: [] }
    cutsceneId.value = message.id as string
    selectedKey.value = null
  }
})

onMounted(() => {
  sendToHost({ type: 'fx:request' })
  // Every clip on screen plays, frame by frame.
  timer = window.setInterval(() => {
    for (const key of Object.keys(playing)) playing[key] = (playing[key] ?? 0) + 1
  }, 110)
})
onBeforeUnmount(() => window.clearInterval(timer))

const sheet = computed(() => sheets.value.find((s) => s.path === selectedSheet.value))

function frameStyle(s: Sheet, f: Frame, scale = 2): Record<string, string> {
  return {
    width: `${f.w * scale}px`,
    height: `${f.h * scale}px`,
    backgroundImage: `url("${PROJECT_HOST}${s.texture}")`,
    backgroundPosition: `-${f.x * scale}px -${f.y * scale}px`,
    backgroundSize: `${s.size[0] * scale}px ${s.size[1] * scale}px`,
    imageRendering: 'pixelated',
  }
}
function current(clipKey: string, frames: Frame[]): Frame | undefined {
  if (!(clipKey in playing)) playing[clipKey] = 0
  return frames.length ? frames[(playing[clipKey] ?? 0) % frames.length] : undefined
}
function importGrid(): void {
  sendToHost({
    type: 'fx:import-grid',
    texture: grid.texture,
    width: grid.width,
    height: grid.height,
    columns: grid.columns,
    rows: grid.rows.split(',').map((r) => r.trim()),
    fps: grid.fps,
  })
}
function importAseprite(): void {
  sendToHost({ type: 'fx:import-aseprite', path: asepritePath.value })
}
function tryIt(kind: 'effect' | 'particles', id: string): void {
  sendToHost({ type: 'fx:try', kind, id })
}
function customise(kind: 'effect' | 'particles', id: string): void {
  sendToHost({ type: 'fx:customise', kind, id })
}

// --- The timeline ---------------------------------------------------------------

const scale = 80 // pixels per second
const length = computed(() => {
  if (!cutscene.value) return 0
  let end = cutscene.value.length ?? 0
  for (const t of cutscene.value.tracks) for (const k of t.keys) end = Math.max(end, k.at + (k.seconds ?? 0.25))
  return Math.max(4, end + 1)
})
const opsByType: Record<string, string[]> = {
  actor: ['move', 'place', 'face', 'show', 'hide', 'tween', 'animate'],
  dialogue: ['line'],
  camera: ['follow', 'move', 'zoom', 'shake'],
  audio: ['music', 'sound'],
  screen: ['fade_in', 'fade_out', 'title'],
  logic: ['do', 'emit', 'wait_input'],
  effect: ['play'],
}
function openCutscene(id: string): void {
  sendToHost({ type: 'fx:cutscene', id })
}
function newCutscene(): void {
  const id = newCutsceneId.value.trim().toLowerCase().replace(/[^a-z0-9_-]+/g, '-')
  if (!id) return
  cutsceneId.value = id
  cutscene.value = { name: id, letterbox: true, tracks: [{ type: 'dialogue', keys: [{ at: 0, speaker: '', text: '…', seconds: 2 }] }] }
  newCutsceneId.value = ''
}
function addTrack(type: string): void {
  cutscene.value?.tracks.push({ type, ...(type === 'actor' ? { actor: 'player' } : {}), keys: [] })
}
function addKey(t: number): void {
  const track = cutscene.value?.tracks[t]
  if (!track) return
  const op = opsByType[track.type]?.[0] ?? ''
  const at = track.keys.length ? Math.max(...track.keys.map((k) => k.at)) + 1 : 0
  track.keys.push(track.type === 'dialogue' ? { at, speaker: '', text: '…', seconds: 2 } : { at, op })
  pickKey(t, track.keys.length - 1)
}
function pickKey(t: number, k: number): void {
  selectedKey.value = { track: t, key: k }
  keyText.value = JSON.stringify(cutscene.value!.tracks[t]!.keys[k], null, 2)
  keyError.value = ''
}
function applyKey(): void {
  if (!selectedKey.value || !cutscene.value) return
  try {
    const parsed = JSON.parse(keyText.value) as Key
    if (typeof parsed.at !== 'number') throw new Error('a key needs "at" (seconds)')
    cutscene.value.tracks[selectedKey.value.track]!.keys[selectedKey.value.key] = parsed
    keyError.value = ''
  } catch (e) {
    keyError.value = String((e as Error).message ?? e)
  }
}
function removeKey(): void {
  if (!selectedKey.value || !cutscene.value) return
  cutscene.value.tracks[selectedKey.value.track]!.keys.splice(selectedKey.value.key, 1)
  selectedKey.value = null
}
function saveCutscene(): void {
  if (!cutscene.value || !cutsceneId.value) return
  sendToHost({ type: 'fx:cutscene-save', id: cutsceneId.value, cutscene: JSON.parse(JSON.stringify(cutscene.value)) })
}
function playCutscene(): void {
  saveCutscene()
  sendToHost({ type: 'fx:cutscene-play', id: cutsceneId.value })
}
function keyLabel(track: Track, key: Key): string {
  if (track.type === 'dialogue') return String(key.text ?? '').slice(0, 18)
  return String(key.op ?? (key.do ? 'do' : '?'))
}
const cutsceneProblems = computed(() => cutscenes.value.find((c) => c.id === cutsceneId.value)?.problems ?? [])
</script>

<template>
  <div class="fx ed-form">
    <div class="topbar">
      <div class="view-switch">
        <button :class="{ on: section === 'sheets' }" @click="section = 'sheets'">Sheets</button>
        <button :class="{ on: section === 'effects' }" @click="section = 'effects'">Particles &amp; effects</button>
        <button :class="{ on: section === 'cutscenes' }" @click="section = 'cutscenes'">Cutscenes</button>
      </div>
      <span class="spacer"></span>
      <button @click="sendToHost({ type: 'fx:request' })">Refresh</button>
    </div>
    <ul v-if="problems.length && section === 'sheets'" class="problems">
      <li v-for="(p, i) in problems" :key="i">{{ p }}</li>
    </ul>

    <!-- Sheets ----------------------------------------------------------------->
    <div v-if="section === 'sheets'" class="split">
      <aside class="list">
        <button v-for="s in sheets" :key="s.path" class="row" :class="{ active: selectedSheet === s.path }" @click="selectedSheet = s.path">
          <span class="name">{{ s.path.replace('assets/', '') }}</span>
          <span class="muted small">{{ s.clips.length }}</span>
        </button>
        <p v-if="!sheets.length" class="muted small pad">No sheets yet (assets/*.anim.json). Make one below.</p>
      </aside>
      <section class="editor">
        <SheetEditor v-if="sheet && editing" :path="sheet.path" @close="editing = false" />
        <template v-else-if="sheet">
          <div class="edit-bar"><button class="primary" title="Its clips, directions and frames, by hand" @click="editing = true">Edit</button></div>
          <p class="muted small">{{ sheet.texture }} · {{ sheet.size[0] }}×{{ sheet.size[1] }}. Give an entity an
            <b>Animator</b> with this sheet (and an animator: humanoid, quadruped, flyer, prop or a record).</p>
          <div v-for="clip in sheet.clips" :key="clip.name" class="clip">
            <div class="clip-head"><b>{{ clip.name }}</b> <span class="muted small">{{ clip.loop }}{{ clip.events ? `, ${clip.events} events` : '' }}</span></div>
            <div class="dirs">
              <div v-for="(frames, dir) in clip.directions" :key="dir" class="dir">
                <span class="muted small">{{ dir === '-' ? '' : dir }}</span>
                <div v-if="current(sheet.path + clip.name + dir, frames)" class="frame"
                     :style="frameStyle(sheet, current(sheet.path + clip.name + dir, frames)!)"></div>
                <div class="strip">
                  <div v-for="(f, i) in frames" :key="i" class="frame small-frame" :style="frameStyle(sheet, f, 1)"></div>
                </div>
              </div>
              <div v-for="(of, dir) in clip.mirrors" :key="'m' + dir" class="dir muted small">{{ dir }} = {{ of }} mirrored</div>
            </div>
          </div>
        </template>
        <div class="section-label">New sheet from a grid</div>
        <div class="form-row">
          <input v-model="grid.texture" placeholder="assets/hero.png" spellcheck="false" class="grow" />
          <span class="muted small">frame</span>
          <input v-model.number="grid.width" type="number" class="num" />×<input v-model.number="grid.height" type="number" class="num" />
          <span class="muted small">columns</span><input v-model.number="grid.columns" type="number" class="num" />
          <span class="muted small">fps</span><input v-model.number="grid.fps" type="number" class="num" />
        </div>
        <div class="form-row">
          <span class="muted small">rows</span>
          <input v-model="grid.rows" class="grow" spellcheck="false" title="Each row's clip: walk_s, walk_e… (directions s, e, n, w…)" />
          <button :disabled="!grid.texture" @click="importGrid">Make sheet</button>
        </div>
        <div class="section-label">From Aseprite</div>
        <div class="form-row">
          <input v-model="asepritePath" class="grow" placeholder="assets/hero.json (Export Sprite Sheet ▸ JSON)" spellcheck="false" />
          <button :disabled="!asepritePath" @click="importAseprite">Import</button>
        </div>
        <p class="muted small">Tags become clips; a tag named walk_e is the east direction of walk.</p>
      </section>
    </div>

    <!-- Particles and effects --------------------------------------------------->
    <div v-else-if="section === 'effects'" class="two">
      <section class="editor">
        <div class="section-label">Particles</div>
        <div v-for="p in particles" :key="p.id" class="item">
          <span class="name">{{ p.id }}</span>
          <span class="badge" :class="p.source">{{ p.source }}</span>
          <button @click="tryIt('particles', p.id)">Try</button>
          <button v-if="p.source === 'engine'" title="Copy into the project's records to change it" @click="customise('particles', p.id)">Customise</button>
        </div>
      </section>
      <section class="editor">
        <div class="section-label">Effects</div>
        <div v-for="e in effects" :key="e.id" class="item">
          <span class="name">{{ e.id }}</span>
          <span class="badge" :class="e.source">{{ e.source }}</span>
          <button @click="tryIt('effect', e.id)">Try</button>
          <button v-if="e.source === 'engine'" @click="customise('effect', e.id)">Customise</button>
        </div>
        <p class="muted small">Try plays it in the middle of the viewport. Change the project's in Records (particles,
          effect). EffectCues on an entity plays effects when it's hit, dies or attacks.</p>
      </section>
    </div>

    <!-- Cutscenes --------------------------------------------------------------->
    <div v-else class="split">
      <aside class="list">
        <button v-for="c in cutscenes" :key="c.id" class="row" :class="{ active: cutsceneId === c.id }" @click="openCutscene(c.id)">
          <span class="name">{{ c.name || c.id }}</span>
          <span v-if="c.problems.length" class="warn">⚠</span>
          <span v-else class="muted small">{{ c.seconds.toFixed(1) }}s</span>
        </button>
        <div class="new-row">
          <input v-model="newCutsceneId" placeholder="new cutscene id" spellcheck="false" @keydown.enter="newCutscene" />
          <button :disabled="!newCutsceneId.trim()" @click="newCutscene">+</button>
        </div>
      </aside>
      <section v-if="cutscene" class="editor">
        <div class="form-row">
          <b>{{ cutsceneId }}</b>
          <input v-model="cutscene.name" placeholder="name" class="grow" />
          <label class="toggle"><input v-model="cutscene.letterbox" type="checkbox" /> letterbox</label>
          <button class="primary" @click="saveCutscene">Save</button>
          <button title="Save, then play the game with it" @click="playCutscene">▶ Play</button>
        </div>
        <ul v-if="cutsceneProblems.length" class="problems"><li v-for="p in cutsceneProblems" :key="p">{{ p }}</li></ul>
        <div class="timeline">
          <div class="ruler" :style="{ width: length * scale + 'px' }">
            <span v-for="s in Math.ceil(length)" :key="s" class="tick" :style="{ left: (s - 1) * scale + 'px' }">{{ s - 1 }}s</span>
          </div>
          <div v-for="(track, t) in cutscene.tracks" :key="t" class="track-row">
            <div class="track-head">
              <span>{{ track.type }}</span>
              <input v-if="track.type === 'actor'" v-model="track.actor" class="actor" spellcheck="false" />
              <button title="Add a key" @click="addKey(t)">+</button>
              <button title="Remove the track" @click="cutscene.tracks.splice(t, 1)">×</button>
            </div>
            <div class="lane" :style="{ width: length * scale + 'px' }">
              <div
                v-for="(key, k) in track.keys"
                :key="k"
                class="key"
                :class="{ on: selectedKey?.track === t && selectedKey?.key === k }"
                :style="{ left: key.at * scale + 'px', width: Math.max(18, (key.seconds ?? 0) * scale) + 'px' }"
                :title="JSON.stringify(key)"
                @click="pickKey(t, k)"
              >
                {{ keyLabel(track, key) }}
              </div>
            </div>
          </div>
        </div>
        <div class="form-row">
          <span class="muted small">Add a track</span>
          <button v-for="type in Object.keys(opsByType)" :key="type" @click="addTrack(type)">{{ type }}</button>
        </div>
        <template v-if="selectedKey">
          <div class="section-label">
            Key ({{ cutscene.tracks[selectedKey.track]!.type }}; ops: {{ (opsByType[cutscene.tracks[selectedKey.track]!.type] ?? []).join(', ') }})
          </div>
          <textarea v-model="keyText" class="code" spellcheck="false" @change="applyKey"></textarea>
          <div class="form-row">
            <button @click="applyKey">Apply</button>
            <button @click="removeKey">Remove key</button>
            <span v-if="keyError" class="warn small">{{ keyError }}</span>
          </div>
        </template>
        <p class="muted small">The format is in docs/systems/story.md ("Cutscenes"). Keys are placed by "at" (seconds); "seconds" is how long one lasts.</p>
      </section>
      <section v-else class="editor muted">Pick or make a cutscene.</section>
    </div>
  </div>
</template>

<style scoped>
.fx {
  display: flex;
  flex-direction: column;
  height: 100%;
  min-height: 0;
}
.topbar {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 6px 8px;
  border-bottom: 1px solid #2a2a3a;
}
.view-switch {
  display: flex;
}
.view-switch button {
  padding: 2px 10px;
}
.view-switch button.on {
  background: #313148;
  border-color: #3b82f6;
}
.split,
.two {
  display: flex;
  flex: 1;
  min-height: 0;
}
.list {
  width: 200px;
  flex-shrink: 0;
  overflow-y: auto;
  border-right: 1px solid #2a2a3a;
  display: flex;
  flex-direction: column;
}
.row {
  display: flex;
  gap: 6px;
  padding: 3px 8px !important;
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
  text-overflow: ellipsis;
  white-space: nowrap;
}
.editor {
  flex: 1;
  min-width: 0;
  overflow: auto;
  padding: 8px 10px;
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.form-row,
.item {
  display: flex;
  align-items: center;
  gap: 6px;
  flex-wrap: wrap;
}
.grow {
  flex: 1;
  min-width: 140px;
}
.num {
  width: 56px;
}
.clip {
  border: 1px solid #2a2a3a;
  padding: 6px;
}
.dirs {
  display: flex;
  flex-wrap: wrap;
  gap: 12px;
}
.dir {
  display: flex;
  flex-direction: column;
  gap: 3px;
}
.strip {
  display: flex;
  gap: 2px;
}
.frame {
  background-repeat: no-repeat;
  border: 1px solid #2a2a3a;
}
.badge {
  font-size: 0.75em;
  padding: 0 5px;
  border: 1px solid #3a3a50;
}
.badge.project {
  border-color: #fbbf24;
  color: #fbbf24;
}
.timeline {
  overflow-x: auto;
  border: 1px solid #2a2a3a;
  background: #11111a;
}
.ruler {
  position: relative;
  height: 18px;
  margin-left: 150px;
  border-bottom: 1px solid #2a2a3a;
}
.tick {
  position: absolute;
  font-size: 10px;
  opacity: 0.6;
  border-left: 1px solid #3a3a50;
  padding-left: 2px;
}
.track-row {
  display: flex;
  border-bottom: 1px solid #1f1f2c;
}
.track-head {
  width: 150px;
  flex-shrink: 0;
  display: flex;
  align-items: center;
  gap: 4px;
  padding: 2px 4px;
  font-size: 0.85em;
  position: sticky;
  left: 0;
  background: #15151f;
}
.track-head .actor {
  width: 60px;
}
.lane {
  position: relative;
  height: 26px;
}
.key {
  position: absolute;
  top: 3px;
  height: 20px;
  background: #2b3b5c;
  border: 1px solid #3b82f6;
  font-size: 10px;
  padding: 1px 3px;
  overflow: hidden;
  white-space: nowrap;
  cursor: pointer;
}
.key.on {
  border-color: #fbbf24;
  background: #4a3e1c;
}
.code {
  min-height: 120px;
  font-family: Consolas, monospace;
  font-size: 12px;
}
.new-row {
  display: flex;
  gap: 4px;
  padding: 6px;
}
.new-row input {
  flex: 1;
  min-width: 0;
}
.problems {
  margin: 4px 8px;
  padding-left: 18px;
  color: #f59e0b;
  font-size: 0.9em;
}
.warn {
  color: #f59e0b;
}
.section-label {
  margin-top: 6px;
  font-size: 0.8em;
  text-transform: uppercase;
  letter-spacing: 0.04em;
  opacity: 0.7;
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
.pad {
  padding: 6px 8px;
}
</style>
