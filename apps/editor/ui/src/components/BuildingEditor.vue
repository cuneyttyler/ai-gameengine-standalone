<script setup lang="ts">
// The building editor (docs/places/interiors.md §5c), from Inspector ▸ Interior ▸ Edit building…: the building's
// exterior and each floor's interior, its footprint's cells drawn over them (as the scene draws them), and with a brush
// its stairs (on a floor: the stairs up from it) and its ways in (cells of its edge) painted on, erased, cleared.
// Saved on this building (interior:setArt): each floor change's stairs cells, its ways in.
import { computed, nextTick, reactive, ref, watch } from 'vue'
import { editorState } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'
import { trackOverlay, untrackOverlay } from '../overlay'

const PROJECT_HOST = 'https://aige-project.local/'
interface Cell { x: number; y: number; edge: boolean; poly: [number, number][] }
interface Door {
  box: [number, number, number, number]; opens: string; locked: boolean; key: string; consumeKey: boolean; animated: boolean; index?: number
  trigger: string; prompt: string; door: boolean
}
interface Info {
  id: number; name: string; pictures: string[]; size: [number, number]; cells: Cell[]; stairs: [number, number][][]; entries: [number, number][]
  doors: Door[]; projectOpens: string
  plan?: boolean // it has a Building Plan (floors, cells); else only its picture and its parts (Edit parts…)
  building?: boolean // the ontology says it's a building
}

const backdrop = ref<HTMLElement | null>(null)
const info = ref<Info | null>(null)
const tab = ref(0) // 0: the exterior; k: floor k's interior
const layer = ref<'stairs' | 'entries' | 'doors'>('stairs')
const doors = ref<Door[]>([]) // its doors (AnimatedPart), as edited here
const picked = ref<number | null>(null) // the door clicked
const tool = ref<'draw' | 'erase'>('draw')
const brush = ref(14) // its radius, in the picture's pixels
const stairs = reactive<Record<number, Set<string>>>({}) // floor change k (floor k up to k + 1) -> its cells "x,y"
const entries = ref(new Set<string>())
const dirty = ref(false)
const finding = ref(false)
let painting = false

const floors = computed(() => (info.value ? info.value.pictures.length - 1 : 0))
const scale = computed(() => {
  if (!info.value) return 1
  const [w, h] = info.value.size
  return Math.min(760 / Math.max(1, w), 520 / Math.max(1, h), 3)
})
// The stairs drawn on this tab: floor k's are its stairs up (the top floor has none of its own).
const change = computed(() => (tab.value >= 1 && tab.value < floors.value ? tab.value : 0))

watch(
  () => editorState.buildingEditor,
  async (id) => {
    info.value = null
    if (id === null) return
    sendToHost({ type: 'interior:art', id })
    await nextTick()
    if (backdrop.value) {
      trackOverlay(backdrop.value)
      backdrop.value.focus()
    }
  },
)

onHostMessage((message) => {
  if (message.type === 'interior:artInfo' && editorState.buildingEditor !== null) {
    const i = message as unknown as Info
    info.value = i
    for (const k of Object.keys(stairs)) delete stairs[Number(k)]
    i.stairs.forEach((cells, n) => (stairs[n + 1] = new Set(cells.map(([x, y]) => `${x},${y}`))))
    entries.value = new Set(i.entries.map(([x, y]) => `${x},${y}`))
    doors.value = (i.doors ?? []).map((d, n) => ({ ...d, index: n }))
    picked.value = null
    dirty.value = false
    finding.value = false
    if (tab.value > i.pictures.length - 1) tab.value = 0
    layer.value = i.plan === false ? 'doors' : tab.value >= 1 && tab.value < i.pictures.length - 1 ? 'stairs' : 'entries'
  }
  if (message.type === 'interior:stairsFound' && editorState.buildingEditor !== null) sendToHost({ type: 'interior:art', id: editorState.buildingEditor })
})

function close(): void {
  if (backdrop.value) untrackOverlay(backdrop.value)
  editorState.buildingEditor = null
}
function save(): void {
  if (!info.value) return
  const list: [number, number][][] = []
  for (let k = 1; k < floors.value; ++k) list.push([...(stairs[k] ?? [])].map((c) => c.split(',').map(Number) as [number, number]))
  sendToHost({
    type: 'interior:setArt',
    id: info.value.id,
    stairs: list,
    entries: [...entries.value].map((c) => c.split(',').map(Number)),
    doors: doors.value.map((d) => ({
      index: d.index, opens: d.opens, locked: d.locked, key: d.key, consumeKey: d.consumeKey, trigger: d.trigger, prompt: d.prompt, door: d.door,
    })),
  })
  dirty.value = false
}
function findStairs(): void {
  if (!info.value) return
  finding.value = true
  sendToHost({ type: 'interior:findStairs', id: info.value.id })
}

// Is a picture point inside a cell's diamond?
function inside(px: number, py: number, poly: [number, number][]): boolean {
  let hit = false
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const [xi, yi] = poly[i]!, [xj, yj] = poly[j]!
    if (yi > py !== yj > py && px < ((xj - xi) * (py - yi)) / (yj - yi || 1e-6) + xi) hit = !hit
  }
  return hit
}
// The brush at a picture point: every cell it touches (its middle, and round its edge) marked or cleared.
function stroke(event: MouseEvent): void {
  if (!info.value) return
  const r = (event.currentTarget as HTMLElement).getBoundingClientRect()
  const px = (event.clientX - r.left) / scale.value, py = (event.clientY - r.top) / scale.value
  const set = layer.value === 'entries' ? entries.value : change.value ? (stairs[change.value] ??= new Set()) : null
  if (!set) return
  const probes: [number, number][] = [[px, py]]
  for (let a = 0; a < 8; ++a) probes.push([px + Math.cos((a * Math.PI) / 4) * brush.value, py + Math.sin((a * Math.PI) / 4) * brush.value])
  for (const cell of info.value.cells) {
    if (layer.value === 'entries' && !cell.edge) continue // a way in is on its edge
    if (!probes.some(([x, y]) => inside(x, y, cell.poly))) continue
    const key = `${cell.x},${cell.y}`
    if (tool.value === 'draw' && !set.has(key)) set.add(key), (dirty.value = true)
    if (tool.value === 'erase' && set.has(key)) set.delete(key), (dirty.value = true)
  }
  if (layer.value === 'entries') entries.value = new Set(entries.value) // seen as changed
}
function down(event: MouseEvent): void {
  if (layer.value === 'doors') {
    // A door picked: the one under the click.
    const r = (event.currentTarget as HTMLElement).getBoundingClientRect()
    const px = (event.clientX - r.left) / scale.value, py = (event.clientY - r.top) / scale.value
    const hit = doors.value.findIndex((d) => px >= d.box[0] && px <= d.box[0] + d.box[2] && py >= d.box[1] && py <= d.box[1] + d.box[3])
    picked.value = hit >= 0 ? hit : null
    return
  }
  painting = true
  stroke(event)
}
// What activates a part, in words (its own, or the project's).
function opensText(d: Door): string {
  const way = d.opens === 'project' ? info.value?.projectOpens ?? 'use' : d.opens
  if (d.locked) return `locked${d.key ? ': ' + d.key : ''}`
  return way === 'auto' ? 'opens as someone comes near' : way === 'open' ? 'an open doorway' : way === 'always' ? 'plays on and on'
    : way === 'action' ? `the ${d.trigger || '?'} action` : way === 'event' ? `the event ${d.trigger || '?'}` : 'on use'
}
function doorColour(d: Door): string {
  const way = d.opens === 'project' ? info.value?.projectOpens ?? 'use' : d.opens
  return d.locked ? '#e65a5a' : way === 'auto' ? '#4ec9b0' : way === 'open' ? '#9cdc6c' : way === 'always' ? '#c586c0'
    : way === 'action' || way === 'event' ? '#569cd6' : '#f5c542'
}
// A part's name: a door (a way in of a building) or another part.
const partName = (d: Door, i: number) => `${d.door && info.value?.building !== false ? 'Door' : 'Part'} ${i + 1}`
function editDoor(): void {
  dirty.value = true
}
function removeDoor(i: number): void {
  doors.value.splice(i, 1)
  picked.value = null
  dirty.value = true
}
function move(event: MouseEvent): void {
  if (painting) stroke(event)
}
function up(): void {
  painting = false
}
function clearLayer(): void {
  if (layer.value === 'entries') entries.value = new Set()
  else if (change.value) stairs[change.value] = new Set()
  dirty.value = true
}

const points = (poly: [number, number][]) => poly.map(([x, y]) => `${x * scale.value},${y * scale.value}`).join(' ')
function fillOf(cell: Cell): string {
  const key = `${cell.x},${cell.y}`
  if (entries.value.has(key)) return 'rgba(80, 220, 120, 0.45)'
  if (change.value && stairs[change.value]?.has(key)) return 'rgba(80, 150, 255, 0.5)'
  if (tab.value >= 2 && stairs[tab.value - 1]?.has(key)) return 'rgba(80, 150, 255, 0.22)' // the stairs coming up to this floor
  return 'transparent'
}
const tabLabel = (k: number) => (k === 0 ? 'Exterior' : `Floor ${k}${k === floors.value ? ' (top)' : ''}`)
</script>

<template>
  <div v-if="editorState.buildingEditor !== null" ref="backdrop" class="backdrop ed-form" tabindex="-1" @mousedown.self="close" @keydown.esc.stop="close">
    <div class="sheet">
      <h3>{{ info?.plan === false ? 'Parts' : 'Building editor' }}<template v-if="info">: {{ info.name }}</template></h3>
      <div v-if="info && info.plan !== false" class="tabs">
        <button v-for="(_, k) in info.pictures" :key="k" :class="{ active: tab === k }" @click="tab = k; layer = k >= 1 && k < floors ? 'stairs' : 'entries'">
          {{ tabLabel(k) }}
        </button>
      </div>
      <div v-if="info && info.plan !== false" class="tools">
        <button :class="{ active: layer === 'stairs' }" :disabled="!change" :title="change ? `The stairs from floor ${change} up to ${change + 1}` : 'Stairs are drawn on a floor below the top one'"
                @click="layer = 'stairs'">Stairs{{ change ? ` (floor ${change} → ${change + 1})` : '' }}</button>
        <button :class="{ active: layer === 'entries' }" title="Its ways in: cells of its edge where it's entered" @click="layer = 'entries'">Ways in</button>
        <button :class="{ active: layer === 'doors' }" title="Its doors and other moving parts: click one to see what activates it and whether it's locked" @click="layer = 'doors'">
          Doors and parts ({{ doors.length }})</button>
        <span class="sep"></span>
        <template v-if="layer !== 'doors'">
          <button :class="{ active: tool === 'draw' }" @click="tool = 'draw'">Draw</button>
          <button :class="{ active: tool === 'erase' }" @click="tool = 'erase'">Erase</button>
          <label class="inline">Brush <input v-model.number="brush" type="range" min="4" max="60" /></label>
          <button title="Clear what's drawn on this layer" @click="clearLayer">Clear</button>
        </template>
        <span class="sep"></span>
        <button :disabled="finding || floors < 2 || editorState.ai.state !== 'ready'" title="The vision model says which corner each floor's stairs are in"
                @click="findStairs">{{ finding ? 'Finding…' : 'Find the stairs (AI)' }}</button>
      </div>
      <div v-if="info" class="work">
      <div class="picture" :style="{ width: `${info.size[0] * scale}px`, height: `${info.size[1] * scale}px` }"
           @mousedown.prevent="down" @mousemove="move" @mouseup="up" @mouseleave="up">
        <img :src="PROJECT_HOST + info.pictures[tab]" alt="" :style="{ width: `${info.size[0] * scale}px`, height: `${info.size[1] * scale}px` }" />
        <svg :width="info.size[0] * scale" :height="info.size[1] * scale">
          <polygon v-for="c in info.cells" :key="`${c.x},${c.y}`" :points="points(c.poly)" :fill="fillOf(c)"
                   :stroke="c.edge ? 'rgba(255,255,255,0.35)' : 'rgba(255,255,255,0.15)'" stroke-width="1" />
          <!-- Its doors: a box each, coloured by how it opens (red: locked); the picked one thicker. -->
          <g v-for="(d, i) in doors" :key="'door' + i" :opacity="layer === 'doors' ? 1 : 0.6">
            <rect :x="d.box[0] * scale" :y="d.box[1] * scale" :width="d.box[2] * scale" :height="d.box[3] * scale" fill="none"
                  :stroke="doorColour(d)" :stroke-width="picked === i ? 3 : 1.5" />
            <text :x="d.box[0] * scale + 3" :y="d.box[1] * scale + 13" :fill="doorColour(d)" font-size="12">{{ d.locked ? '🔒 ' : '' }}{{ i + 1 }}</text>
          </g>
        </svg>
      </div>
      <!-- The door picked: how it opens, its lock. -->
      <div v-if="layer === 'doors'" class="door-panel">
        <p v-if="!doors.length" class="muted small">No moving parts: animate one (Build ▸ Animate, or right-click ▸ Animate a part…).</p>
        <div v-for="(d, i) in doors" :key="'row' + i" class="door-row" :class="{ picked: picked === i }" @click="picked = i">
          <b :style="{ color: doorColour(d) }">{{ partName(d, i) }}</b> <span class="muted small">{{ opensText(d) }}{{ d.animated ? '' : ' · not animated' }}</span>
        </div>
        <template v-if="picked !== null && doors[picked]">
          <label v-if="info.building !== false" class="inline" title="A way in of the building: blocked while shut. A window, a sign or a lever on it isn't.">
            <input v-model="doors[picked]!.door" type="checkbox" @change="editDoor" /> A door (a way in)</label>
          <label>Activation
            <select v-model="doors[picked]!.opens" @change="editDoor">
              <option value="project">as the project's doors ({{ info.projectOpens }})</option>
              <option value="use">on use (the door shortcuts, a double click)</option>
              <option value="auto">by itself, as someone comes near</option>
              <option value="action">an input action, near it</option>
              <option value="event">a game event (a script's, a trigger's)</option>
              <option value="always">always playing (a loop)</option>
              <option v-if="doors[picked]!.door" value="open">always open (a doorway)</option>
            </select>
          </label>
          <label v-if="doors[picked]!.opens === 'action' || doors[picked]!.opens === 'event'">
            {{ doors[picked]!.opens === 'action' ? 'Input action (Project Settings ▸ Input)' : 'Event name' }}
            <input v-model="doors[picked]!.trigger" spellcheck="false" :placeholder="doors[picked]!.opens === 'action' ? 'interact' : 'bridge_down'" @change="editDoor" />
          </label>
          <label v-if="['project', 'use', 'action'].includes(doors[picked]!.opens)" title="Shown near it; 'shut|open' for each way">
            Prompt <input v-model="doors[picked]!.prompt" :placeholder="doors[picked]!.door ? 'Open the door|Close the door' : 'Pull the lever'" @change="editDoor" />
          </label>
          <label class="inline"><input v-model="doors[picked]!.locked" type="checkbox" @change="editDoor" /> Locked</label>
          <label v-if="doors[picked]!.locked">Key (an item id) <input v-model="doors[picked]!.key" spellcheck="false" placeholder="iron_key" @change="editDoor" /></label>
          <label v-if="doors[picked]!.locked" class="inline"><input v-model="doors[picked]!.consumeKey" type="checkbox" @change="editDoor" /> The key is used up</label>
          <button class="small" @click="removeDoor(picked)">Remove this part</button>
        </template>
      </div>
      </div>
      <p v-else class="muted">Loading the building…</p>
      <p class="muted small">
        Paint over the stairs on a floor (they take one up from it; down from the floor above) and along its edge where it's
        entered. Blue: stairs; green: ways in. Doors: yellow opens on use, teal by itself, red locked. Saved on this building.
      </p>
      <div class="buttons">
        <button @click="close">{{ dirty ? 'Cancel' : 'Close' }}</button>
        <button class="primary" :disabled="!dirty" @click="save">Save</button>
      </div>
    </div>
  </div>
</template>

<style scoped>
.backdrop {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.5);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 55;
}
.sheet {
  background: var(--panel, #222);
  border: 1px solid var(--border, #444);
  padding: 12px 14px;
  display: flex;
  flex-direction: column;
  gap: 8px;
  max-width: 96vw;
}
.tabs,
.tools {
  display: flex;
  gap: 4px;
  align-items: center;
  flex-wrap: wrap;
}
.tabs button.active,
.tools button.active {
  border-color: var(--accent, #4a8);
  background: rgba(68, 170, 136, 0.15);
}
.sep {
  width: 10px;
}
.work {
  display: flex;
  gap: 10px;
  align-items: flex-start;
}
.door-panel {
  display: flex;
  flex-direction: column;
  gap: 6px;
  min-width: 220px;
}
.door-panel label {
  display: flex;
  flex-direction: column;
  gap: 2px;
}
.door-panel label.inline {
  flex-direction: row;
  align-items: center;
}
.door-row {
  padding: 3px 5px;
  border: 1px solid transparent;
  cursor: pointer;
}
.door-row.picked {
  border-color: var(--accent, #4a8);
}
.picture {
  position: relative;
  cursor: crosshair;
  background: #1a1a1a;
  outline: 1px solid var(--border, #444);
}
.picture img,
.picture svg {
  position: absolute;
  left: 0;
  top: 0;
  pointer-events: none;
}
.buttons {
  display: flex;
  justify-content: flex-end;
  gap: 6px;
}
</style>
