<script setup lang="ts">
// Dungeon generation by hand (development_plan.md 3.4): the same
// level.generate command the AI proposes. Preview shows the layout as a map;
// Generate builds it as one undo step.
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
    requestPreview()
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

function reroll(): void {
  args.seed = Math.floor(Math.random() * 1_000_000)
}

function generate(): void {
  sendToHost({ type: 'level:generate', args: { ...args } })
}
</script>

<template>
  <div class="level ed-form">
    <section class="form">
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
    </section>
    <section class="preview">
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
