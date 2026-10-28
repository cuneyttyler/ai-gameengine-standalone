<script setup lang="ts">
// Animate a part (docs/assets/animations.md §4): a box dragged over a thing's picture (or found: Find the door), and
// the part animated as an overlay on the thing. A door is opened from its own pixels (free, true to the art) and opens
// as its building says (the project's way, by itself, or with the door shortcuts), locked if it is; anything else (a
// window, a sign) is animated by AutoSprite from the motion said.
import { computed, nextTick, ref, watch } from 'vue'
import { editorState } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'
import { trackOverlay, untrackOverlay } from '../overlay'

const PROJECT_HOST = 'https://aige-project.local/'
const backdrop = ref<HTMLElement | null>(null)
const info = ref<{ id: number; name: string; texture: string; region: number[]; size: number[] } | null>(null)
const box = ref<number[] | null>(null) // x, y, w, h in the picture's pixels (its region's)
const action = ref('the door swings open')
const kind = ref<'door' | 'other'>('door')
const opens = ref<'project' | 'auto' | 'use'>('project')
const locked = ref(false)
const key = ref('')
const finding = ref(false)
const findError = ref('')
let dragFrom: [number, number] | null = null

const scale = computed(() => {
  if (!info.value) return 1
  const [, , w, h] = info.value.region
  return Math.min(640 / Math.max(1, w ?? 1), 440 / Math.max(1, h ?? 1), 4)
})

watch(
  () => editorState.partDialog,
  async (id) => {
    info.value = null
    box.value = null
    if (id !== null) sendToHost({ type: 'part:open', id })
    await nextTick()
    if (id !== null && backdrop.value) {
      trackOverlay(backdrop.value)
      backdrop.value.focus()
    }
  },
)

onHostMessage((message) => {
  if (message.type === 'part:info') info.value = message as unknown as typeof info.value
  if (message.type === 'part:found') {
    finding.value = false
    findError.value = message.ok ? '' : String(message.error ?? 'no door found')
    if (message.ok) box.value = message.box as number[]
  }
})

function findDoor(): void {
  if (!info.value) return
  finding.value = true
  findError.value = ''
  sendToHost({ type: 'part:find', id: info.value.id, region: info.value.region })
}

function close(): void {
  if (backdrop.value) untrackOverlay(backdrop.value)
  editorState.partDialog = null
}

function at(event: MouseEvent): [number, number] {
  const el = event.currentTarget as HTMLElement
  const r = el.getBoundingClientRect()
  return [Math.round((event.clientX - r.left) / scale.value), Math.round((event.clientY - r.top) / scale.value)]
}
function down(event: MouseEvent): void {
  dragFrom = at(event)
  box.value = [dragFrom[0], dragFrom[1], 1, 1]
}
function move(event: MouseEvent): void {
  if (!dragFrom) return
  const [x, y] = at(event)
  box.value = [Math.min(x, dragFrom[0]), Math.min(y, dragFrom[1]), Math.abs(x - dragFrom[0]) + 1, Math.abs(y - dragFrom[1]) + 1]
}
function up(): void {
  dragFrom = null
}

function animate(): void {
  if (!info.value || !box.value || box.value[2]! < 4 || box.value[3]! < 4) return
  sendToHost({
    type: 'part:animate',
    id: info.value.id,
    box: box.value,
    kind: kind.value,
    action: action.value.trim() || 'the door swings open',
    opens: opens.value,
    locked: locked.value,
    key: key.value.trim(),
  })
  close()
}

const pictureStyle = computed(() => {
  if (!info.value) return {}
  const [x, y, w, h] = info.value.region as [number, number, number, number]
  const [tw, th] = info.value.size as [number, number]
  const s = scale.value
  return {
    width: `${w * s}px`,
    height: `${h * s}px`,
    backgroundImage: `url("${PROJECT_HOST}${info.value.texture}")`,
    backgroundPosition: `-${x * s}px -${y * s}px`,
    backgroundSize: `${tw * s}px ${th * s}px`,
  }
})
const boxStyle = computed(() => {
  if (!box.value) return { display: 'none' }
  const [x, y, w, h] = box.value as [number, number, number, number]
  const s = scale.value
  return { left: `${x * s}px`, top: `${y * s}px`, width: `${w * s}px`, height: `${h * s}px` }
})
</script>

<template>
  <div v-if="editorState.partDialog !== null" ref="backdrop" class="backdrop ed-form" tabindex="-1" @mousedown.self="close" @keydown.esc.stop="close">
    <div class="sheet">
      <h3>Animate a part<template v-if="info"> of {{ info.name }}</template></h3>
      <p class="muted">
        Drag a box round the part that moves, or let the vision model find the door. A door is opened from its own pixels
        (free, the art kept as it is); anything else is animated by AutoSprite from the motion you give.
      </p>
      <div class="row">
        <label>It's</label>
        <select v-model="kind">
          <option value="door">a door (opened from its own pixels)</option>
          <option value="other">something else (AutoSprite)</option>
        </select>
        <button v-if="kind === 'door'" class="small" :disabled="!info || finding" title="The vision model looks for the door in the picture" @click="findDoor">
          {{ finding ? 'Finding…' : 'Find the door' }}
        </button>
        <span v-if="findError" class="muted">{{ findError }}</span>
      </div>
      <div v-if="info" class="picture" :style="pictureStyle" @mousedown.prevent="down" @mousemove="move" @mouseup="up" @mouseleave="up">
        <div class="box" :style="boxStyle"></div>
      </div>
      <p v-else class="muted">Loading the picture…</p>
      <div v-if="kind === 'other'" class="row">
        <label>Motion</label>
        <input v-model="action" spellcheck="false" placeholder="the sign swings in the wind" />
      </div>
      <template v-else>
        <div class="row">
          <label>Opens</label>
          <select v-model="opens">
            <option value="project">as the project says (Project Settings ▸ Game ▸ doorsOpen)</option>
            <option value="use">with the door shortcuts or a double click</option>
            <option value="auto">by itself, as someone comes near</option>
          </select>
        </div>
        <div class="row">
          <label>Locked</label>
          <input v-model="locked" type="checkbox" title="Shut, its doorway blocked, until the player uses it carrying the key" />
          <input v-if="locked" v-model="key" spellcheck="false" placeholder="the key's item id (empty: it can't be opened)" />
        </div>
      </template>
      <div class="buttons">
        <button @click="close">Cancel</button>
        <button class="primary" :disabled="!box || (box[2] ?? 0) < 4 || (box[3] ?? 0) < 4" @click="animate">Animate</button>
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
  max-width: 720px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.picture {
  position: relative;
  cursor: crosshair;
  image-rendering: pixelated;
  outline: 1px solid var(--border, #444);
  background-repeat: no-repeat;
}
.box {
  position: absolute;
  border: 2px solid #3c3;
  background: rgba(60, 200, 60, 0.15);
  pointer-events: none;
}
.row {
  display: flex;
  gap: 8px;
  align-items: center;
}
.row input {
  flex: 1;
}
.buttons {
  display: flex;
  justify-content: flex-end;
  gap: 6px;
}
</style>
