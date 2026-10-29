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
interface Info { id: number; name: string; pictures: string[]; size: [number, number]; cells: Cell[]; stairs: [number, number][][]; entries: [number, number][] }

const backdrop = ref<HTMLElement | null>(null)
const info = ref<Info | null>(null)
const tab = ref(0) // 0: the exterior; k: floor k's interior
const layer = ref<'stairs' | 'entries'>('stairs')
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
    dirty.value = false
    finding.value = false
    if (tab.value > i.pictures.length - 1) tab.value = 0
    layer.value = tab.value >= 1 && tab.value < i.pictures.length - 1 ? 'stairs' : 'entries'
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
  sendToHost({ type: 'interior:setArt', id: info.value.id, stairs: list, entries: [...entries.value].map((c) => c.split(',').map(Number)) })
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
  painting = true
  stroke(event)
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
      <h3>Building editor<template v-if="info">: {{ info.name }}</template></h3>
      <div v-if="info" class="tabs">
        <button v-for="(_, k) in info.pictures" :key="k" :class="{ active: tab === k }" @click="tab = k; layer = k >= 1 && k < floors ? 'stairs' : 'entries'">
          {{ tabLabel(k) }}
        </button>
      </div>
      <div v-if="info" class="tools">
        <button :class="{ active: layer === 'stairs' }" :disabled="!change" :title="change ? `The stairs from floor ${change} up to ${change + 1}` : 'Stairs are drawn on a floor below the top one'"
                @click="layer = 'stairs'">Stairs{{ change ? ` (floor ${change} → ${change + 1})` : '' }}</button>
        <button :class="{ active: layer === 'entries' }" title="Its ways in: cells of its edge where it's entered" @click="layer = 'entries'">Ways in</button>
        <span class="sep"></span>
        <button :class="{ active: tool === 'draw' }" @click="tool = 'draw'">Draw</button>
        <button :class="{ active: tool === 'erase' }" @click="tool = 'erase'">Erase</button>
        <label class="inline">Brush <input v-model.number="brush" type="range" min="4" max="60" /></label>
        <button title="Clear what's drawn on this layer" @click="clearLayer">Clear</button>
        <span class="sep"></span>
        <button :disabled="finding || floors < 2 || editorState.ai.state !== 'ready'" title="The vision model says which corner each floor's stairs are in"
                @click="findStairs">{{ finding ? 'Finding…' : 'Find the stairs (AI)' }}</button>
      </div>
      <div v-if="info" class="picture" :style="{ width: `${info.size[0] * scale}px`, height: `${info.size[1] * scale}px` }"
           @mousedown.prevent="down" @mousemove="move" @mouseup="up" @mouseleave="up">
        <img :src="PROJECT_HOST + info.pictures[tab]" alt="" :style="{ width: `${info.size[0] * scale}px`, height: `${info.size[1] * scale}px` }" />
        <svg :width="info.size[0] * scale" :height="info.size[1] * scale">
          <polygon v-for="c in info.cells" :key="`${c.x},${c.y}`" :points="points(c.poly)" :fill="fillOf(c)"
                   :stroke="c.edge ? 'rgba(255,255,255,0.35)' : 'rgba(255,255,255,0.15)'" stroke-width="1" />
        </svg>
      </div>
      <p v-else class="muted">Loading the building…</p>
      <p class="muted small">
        Paint over the stairs on a floor (they take one up from it; down from the floor above) and along its edge where it's
        entered. Blue: stairs; green: ways in. Saved on this building.
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
