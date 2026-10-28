<script setup lang="ts">
// Animate a part (docs/assets/animations.md §4): a box dragged over a thing's picture (a building's door, a window, a
// sign; a part of a painted scene), the motion said, and the part animated (AutoSprite) as an overlay on the thing:
// attached to it, playing its clip when someone comes to it.
import { computed, nextTick, ref, watch } from 'vue'
import { editorState } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'
import { trackOverlay, untrackOverlay } from '../overlay'

const PROJECT_HOST = 'https://aige-project.local/'
const backdrop = ref<HTMLElement | null>(null)
const info = ref<{ id: number; name: string; texture: string; region: number[]; size: number[] } | null>(null)
const box = ref<number[] | null>(null) // x, y, w, h in the picture's pixels (its region's)
const action = ref('the door swings open')
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
})

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
  sendToHost({ type: 'part:animate', id: info.value.id, box: box.value, action: action.value.trim() || 'the door swings open' })
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
        Drag a box round the part that moves (a door, a window, a sign) and say how it moves. It's animated (Generate ▸
        Animate's provider) and put over the picture as a part of its own, attached to it, playing when someone comes to
        it (its Action, in the Inspector).
      </p>
      <div v-if="info" class="picture" :style="pictureStyle" @mousedown.prevent="down" @mousemove="move" @mouseup="up" @mouseleave="up">
        <div class="box" :style="boxStyle"></div>
      </div>
      <p v-else class="muted">Loading the picture…</p>
      <div class="row">
        <label>Motion</label>
        <input v-model="action" spellcheck="false" placeholder="the door swings open" />
      </div>
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
