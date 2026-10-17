<script setup lang="ts">
// Dungeon generation by hand (development_plan.md 3.4): the same
// level.generate command the AI proposes. Preview shows the layout as a map;
// Generate builds it as one undo step. Village (6.5) does the same with
// level.village, from the pack's analyzed prefabs.
import { onMounted, reactive, ref, watch } from 'vue'
import { onHostMessage, sendToHost } from '../ipc'

const args = reactive({
  x0: 40,
  y0: 0,
  x1: 89,
  y1: 39,
  seed: 1,
  rooms: 8,
  roomMin: 4,
  roomMax: 9,
  corridorWidth: 1,
  loops: 0.2,
  floor: '',
  wall: '',
  enemy: '',
  enemies: 8,
  pickup: '',
  pickups: 2,
  movePlayer: true,
})
const tiles = ref<string[]>([])
const things = ref<string[]>([])
const preview = ref('')
const error = ref('')

const mode = ref<'dungeon' | 'village'>('dungeon')
const village = reactive({
  x0: 40,
  y0: 0,
  houses: 12,
  seed: 1,
  roadWidth: 1,
  houseCells: 0,
  nature: 0.5,
  fences: true,
  square: true,
  ground: '',
  road: '',
  movePlayer: true,
})
const villagePreview = ref('')
const villageError = ref('')

onHostMessage((message) => {
  if (message.type === 'level:defaults') {
    tiles.value = message.tiles as string[]
    things.value = message.things as string[]
    const width = args.x1 - args.x0
    const height = args.y1 - args.y0
    args.x0 = message.x0 as number
    args.y0 = message.y0 as number
    args.x1 = args.x0 + width
    args.y1 = args.y0 + height
    const pick = (list: string[], ...hints: string[]) => list.find((p) => hints.some((h) => p.includes(h))) ?? list[0] ?? ''
    args.floor ||= pick(tiles.value, 'floor', 'stone', 'dirt')
    args.wall ||= pick(tiles.value, 'wall')
    args.enemy ||= pick(things.value, 'skeleton', 'enemy')
    args.pickup ||= pick(things.value, 'potion')
    village.x0 = message.x0 as number
    village.y0 = message.y0 as number
    requestPreview()
    requestVillagePreview()
  } else if (message.type === 'village:preview') {
    villagePreview.value = message.text as string
    villageError.value = message.error as string
  } else if (message.type === 'level:preview') {
    preview.value = message.text as string
    error.value = message.error as string
  }
})

onMounted(() => sendToHost({ type: 'level:defaults' }))

let timer: number | undefined
function requestPreview(): void {
  window.clearTimeout(timer)
  timer = window.setTimeout(() => sendToHost({ type: 'level:preview', args: { ...args } }), 150)
}
watch(args, requestPreview, { deep: true })

let villageTimer: number | undefined
function requestVillagePreview(): void {
  window.clearTimeout(villageTimer)
  villageTimer = window.setTimeout(() => sendToHost({ type: 'village:preview', args: { ...village } }), 150)
}
watch(village, requestVillagePreview, { deep: true })
watch(mode, (m) => (m === 'village' ? requestVillagePreview() : requestPreview()))

function reroll(): void {
  if (mode.value === 'village') village.seed = Math.floor(Math.random() * 1_000_000)
  else args.seed = Math.floor(Math.random() * 1_000_000)
}

function buildVillage(): void {
  sendToHost({ type: 'village:generate', args: { ...village } })
}

function generate(): void {
  sendToHost({ type: 'level:generate', args: { ...args } })
}
</script>

<template>
  <div class="level ed-form">
    <section class="form">
      <div class="modes">
        <button :class="{ on: mode === 'dungeon' }" @click="mode = 'dungeon'">Dungeon</button>
        <button :class="{ on: mode === 'village' }" @click="mode = 'village'">Village</button>
      </div>
      <template v-if="mode === 'village'">
        <div class="row">
          <label>Top-left</label>
          <input v-model.number="village.x0" type="number" title="Column" />
          <input v-model.number="village.y0" type="number" title="Row" />
          <span class="muted">grows right and down</span>
        </div>
        <div class="row">
          <label>Seed</label>
          <input v-model.number="village.seed" type="number" min="0" />
          <button title="Another layout" @click="reroll">🎲</button>
        </div>
        <div class="row">
          <label>Houses</label>
          <input v-model.number="village.houses" type="number" min="1" max="60" />
          <span class="muted">size</span>
          <input
            v-model.number="village.houseCells"
            type="number"
            min="0"
            max="6"
            title="A house's footprint in cells; 0 = from the house art"
          />
          <span class="muted">cells (0 = from the art)</span>
        </div>
        <div class="row">
          <label>Streets</label>
          <input v-model.number="village.roadWidth" type="number" min="1" max="3" title="Width in cells" />
          <span class="muted">wide</span>
          <label class="check"><input v-model="village.square" type="checkbox" /> Square with a well</label>
        </div>
        <div class="row">
          <label>Nature</label>
          <input v-model.number="village.nature" type="range" min="0" max="1" step="0.05" class="slider" />
          <span class="muted">{{ village.nature.toFixed(2) }}</span>
        </div>
        <div class="row">
          <label>Terrain</label>
          <input v-model="village.road" placeholder="road (found)" title="Road terrain from the tiles' edges, e.g. dirt" />
          <span class="muted">on</span>
          <input v-model="village.ground" placeholder="ground (found)" title="Ground terrain, e.g. grass" />
        </div>
        <div class="row">
          <label></label>
          <label class="check"><input v-model="village.fences" type="checkbox" /> Fenced gardens</label>
          <label class="check"><input v-model="village.movePlayer" type="checkbox" /> Move the player</label>
        </div>
        <div class="row">
          <label></label>
          <button class="primary" :disabled="!!villageError" @click="buildVillage">Build village</button>
          <span class="muted">Replaces everything in the area (but the player). Ctrl+Z undoes it.</span>
        </div>
      </template>
      <template v-else>
      <div class="row">
        <label>Area</label>
        <input v-model.number="args.x0" type="number" title="First column" />
        <input v-model.number="args.y0" type="number" title="First row" />
        <span class="muted">to</span>
        <input v-model.number="args.x1" type="number" title="Last column" />
        <input v-model.number="args.y1" type="number" title="Last row" />
      </div>
      <div class="row">
        <label>Seed</label>
        <input v-model.number="args.seed" type="number" min="0" />
        <button title="Another layout" @click="reroll">🎲</button>
      </div>
      <div class="row">
        <label>Rooms</label>
        <input v-model.number="args.rooms" type="number" min="2" max="40" />
        <span class="muted">size</span>
        <input v-model.number="args.roomMin" type="number" min="2" max="30" />
        <span class="muted">–</span>
        <input v-model.number="args.roomMax" type="number" min="2" max="30" />
      </div>
      <div class="row">
        <label>Corridors</label>
        <input v-model.number="args.corridorWidth" type="number" min="1" max="3" title="Width" />
        <span class="muted">loops</span>
        <input v-model.number="args.loops" type="range" min="0" max="1" step="0.05" class="slider" />
        <span class="muted">{{ args.loops.toFixed(2) }}</span>
      </div>
      <div class="row">
        <label>Floor</label>
        <select v-model="args.floor"><option v-for="p in tiles" :key="p" :value="p">{{ p }}</option></select>
      </div>
      <div class="row">
        <label>Walls</label>
        <select v-model="args.wall"><option v-for="p in tiles" :key="p" :value="p">{{ p }}</option></select>
      </div>
      <div class="row">
        <label>Enemies</label>
        <input v-model.number="args.enemies" type="number" min="0" max="200" />
        <select v-model="args.enemy">
          <option value="">none</option>
          <option v-for="p in things" :key="p" :value="p">{{ p }}</option>
        </select>
      </div>
      <div class="row">
        <label>Pickups</label>
        <input v-model.number="args.pickups" type="number" min="0" max="100" />
        <select v-model="args.pickup">
          <option value="">none</option>
          <option v-for="p in things" :key="p" :value="p">{{ p }}</option>
        </select>
      </div>
      <div class="row">
        <label></label>
        <label class="check"><input v-model="args.movePlayer" type="checkbox" /> Move the player to the start</label>
      </div>
      <div class="row">
        <label></label>
        <button class="primary" :disabled="!!error || !args.floor || !args.wall" @click="generate">Generate</button>
        <span class="muted">Replaces everything in the area (but the player). Ctrl+Z undoes it.</span>
      </div>
      </template>
    </section>
    <section v-if="mode === 'village'" class="preview">
      <div v-if="villageError" class="error">{{ villageError }}</div>
      <pre v-else>{{ villagePreview }}</pre>
    </section>
    <section v-else class="preview">
      <div v-if="error" class="error">{{ error }}</div>
      <pre v-else>{{ preview }}</pre>
    </section>
  </div>
</template>

<style scoped>
.level {
  display: flex;
  height: 100%;
  min-height: 0;
}
.form {
  width: 420px;
  flex-shrink: 0;
  padding: 8px 10px;
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  gap: 5px;
  border-right: 1px solid #2a2a3a;
}
.modes {
  display: flex;
  gap: 2px;
  margin-bottom: 4px;
}
.modes button {
  flex: 1;
  border-radius: 0;
}
.modes button.on {
  background: #3b3b5c;
  color: #fff;
}
.row {
  display: flex;
  align-items: center;
  gap: 6px;
}
.row > label:first-child {
  width: 70px;
  flex-shrink: 0;
  font-size: 0.85em;
  opacity: 0.75;
}
.row input[type='number'] {
  width: 58px;
}
.row select {
  flex: 1;
}
.slider {
  flex: 1;
}
.check {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 0.9em;
}
.preview {
  flex: 1;
  min-width: 0;
  overflow: auto;
  padding: 8px 10px;
}
pre {
  margin: 0;
  font-family: Consolas, 'Cascadia Mono', monospace;
  font-size: 11px;
  line-height: 1.15;
  white-space: pre;
}
.error {
  color: #f59e0b;
}
.muted {
  opacity: 0.6;
  font-size: 0.85em;
}
</style>
