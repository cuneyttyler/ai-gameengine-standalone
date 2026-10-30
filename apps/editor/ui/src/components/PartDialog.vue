<script setup lang="ts">
// Animate a part (docs/assets/animations.md §4): a box dragged over a thing's picture (or found: Find the door), and
// the part animated as an overlay on the thing. A door is opened from its own pixels (free, true to the art) and opens
// as its building says (the project's way, by itself, or with the door shortcuts), locked if it is; anything else (a
// window, a sign) animated by PixelLab from the motion said. The dialog waits for it and shows what it made.
import { computed, nextTick, ref, watch } from 'vue'
import { editorState } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'
import { trackOverlay, untrackOverlay } from '../overlay'
import ActionPicker from './ActionPicker.vue'
import PartActiveFields from './PartActiveFields.vue'

const PROJECT_HOST = 'https://aige-project.local/'
const backdrop = ref<HTMLElement | null>(null)
const info = ref<{ id: number; name: string; texture: string; region: number[]; size: number[] } | null>(null)
const box = ref<number[] | null>(null) // x, y, w, h in the picture's pixels (its region's)
const action = ref('the door swings open')
const kind = ref<'door' | 'opening' | 'other' | 'character'>('door')
const opens = ref<'project' | 'auto' | 'use' | 'action' | 'event' | 'always'>('project')
const trigger = ref('') // action / event: its name
const fires = ref('') // the game event it sends each time the player activates it
// While it's active: how long it plays, the player's clip (again and again), the player held still.
const active = ref({ duration: 0, playerClip: '', playerLoop: true, holdsPlayer: false })
const prompt = ref('') // what the player is shown near it ("Pull the lever")
const locked = ref(false)
const key = ref('')
const finding = ref(false)
const findError = ref('')
// Animating: waiting for it (its progress), then what it made (played) or why not.
const working = ref('')
const done = ref<{ ok: boolean; error?: string; sheet?: string; count?: number; fps?: number; warnings?: string[] } | null>(null)
const clock = ref(0)
let timer = 0
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
    working.value = ''
    done.value = null
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
  if (message.type === 'part:progress' && editorState.partDialog !== null) working.value = String(message.text ?? 'Animating…')
  if (message.type === 'part:done' && editorState.partDialog !== null) {
    working.value = ''
    done.value = message as unknown as NonNullable<typeof done.value>
    window.clearInterval(timer)
    timer = window.setInterval(() => (clock.value += 1 / 20), 50)
  }
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

// What it made, playing: open, held, shut, held.
const playStyle = computed(() => {
  const d = done.value
  if (!d?.ok || !d.sheet || !d.count || !box.value) return { display: 'none' }
  const n = Math.max(1, d.count), fps = Math.max(1, d.fps ?? 8), run = n / fps, hold = 0.8
  const t = clock.value % (2 * (run + hold))
  const f = t < run ? Math.floor(t * fps) : t < run + hold ? n - 1 : t < 2 * run + hold ? n - 1 - Math.floor((t - run - hold) * fps) : 0
  const [x, y, w, h] = box.value as [number, number, number, number]
  const s = scale.value
  return {
    left: `${x * s}px`, top: `${y * s}px`, width: `${w * s}px`, height: `${h * s}px`,
    backgroundImage: `url("${PROJECT_HOST}${d.sheet}")`, backgroundSize: `${n * 100}% 100%`,
    backgroundPosition: `${n > 1 ? (Math.min(n - 1, Math.max(0, f)) / (n - 1)) * 100 : 0}% 0`,
  }
})

function close(): void {
  window.clearInterval(timer)
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
    opens: kind.value === 'other' && opens.value === 'project' ? 'use' : opens.value,
    trigger: trigger.value.trim(),
    fires: fires.value.trim(),
    ...active.value,
    prompt: prompt.value.trim(),
    locked: locked.value,
    key: key.value.trim(),
  })
  done.value = null
  working.value = kind.value === 'opening' ? 'Marking the doorway…' : kind.value === 'door' ? 'Opening the door (PixelLab: a few minutes)…'
    : kind.value === 'character' ? 'Animating the character (AutoSprite: a few minutes)…' : 'Animating it (PixelLab: a few minutes)…'
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
        (PixelLab, the art kept as it is); anything else is animated by PixelLab from the motion you give.
      </p>
      <div class="row">
        <label>It's</label>
        <select v-model="kind">
          <option value="door">a door (opened from its own pixels)</option>
          <option value="opening">an open doorway (its entry point, nothing to animate)</option>
          <option value="other">another object: a sign, a window, a flag (PixelLab)</option>
          <option value="character">a character (AutoSprite)</option>
        </select>
        <button v-if="kind !== 'other'" class="small" :disabled="!info || finding" title="The vision model looks for the door in the picture" @click="findDoor">
          {{ finding ? 'Finding…' : 'Find the door' }}
        </button>
        <span v-if="findError" class="muted">{{ findError }}</span>
      </div>
      <div v-if="info" class="picture" :style="pictureStyle" @mousedown.prevent="down" @mousemove="move" @mouseup="up" @mouseleave="up">
        <div class="box" :style="boxStyle"></div>
        <div class="played" :style="playStyle"></div>
      </div>
      <p v-else class="muted">Loading the picture…</p>
      <div v-if="kind === 'other' || kind === 'character'" class="row">
        <label>Describe the animation</label>
        <input v-model="action" spellcheck="false"
               :placeholder="kind === 'character' ? 'waves at the player' : 'the sign swings gently in the wind'" />
      </div>
      <template v-if="kind === 'door' || kind === 'other'">
        <div class="row">
          <label>Activation</label>
          <select v-model="opens">
            <option v-if="kind === 'door'" value="project">as the project's doors (Project Settings ▸ Game ▸ doorsOpen)</option>
            <option value="use">with the door shortcuts or a double click</option>
            <option value="auto">by itself, as someone comes near</option>
            <option value="action">an input action, near it</option>
            <option value="event">a game event (a script's, a trigger's)</option>
            <option value="always">always playing (a loop)</option>
          </select>
          <input v-if="opens === 'event'" v-model="trigger" spellcheck="false" placeholder="the event (bridge_down)" />
        </div>
        <div v-if="opens === 'action' || opens === 'always'" class="row">
          <label :title="opens === 'always' ? 'Optional: it plays anyway; pressing this near it sends its event' : 'Pressed near it: it plays'">
            Input action{{ opens === 'always' ? ' (optional)' : '' }}</label>
          <ActionPicker v-model="trigger" />
        </div>
        <div v-if="opens !== 'auto' && opens !== 'event'" class="row">
          <label title="Sent each time the player activates it: scripts, triggers and quests react to it">Sends event</label>
          <input v-model="fires" spellcheck="false" placeholder="optional (cooked)" />
        </div>
        <div v-if="!['auto', 'always'].includes(opens)" class="row">
          <label>While active</label>
          <PartActiveFields v-bind="active" @set="(k, v) => (active = { ...active, [k]: v })" />
        </div>
        <div v-if="['project', 'use', 'action'].includes(opens) || (opens === 'always' && trigger)" class="row">
          <label>Prompt</label>
          <input v-model="prompt" :placeholder="kind === 'door' ? 'Open the door|Close the door' : 'Pull the lever'"
                 title="Shown near it; 'shut|open' for each way" />
        </div>
      </template>
      <template v-if="kind === 'door'">
        <div class="row">
          <label>Locked</label>
          <input v-model="locked" type="checkbox" title="Shut, its doorway blocked, until the player uses it carrying the key" />
          <input v-if="locked" v-model="key" spellcheck="false" placeholder="the key's item id (empty: it can't be opened)" />
        </div>
      </template>
      <p v-if="working" class="status">{{ working }}</p>
      <p v-else-if="done && done.ok && (done as { opening?: boolean }).opening" class="status ok">Done: the doorway is the building's way in now
        (Inspector ▸ AnimatedPart, opens = open); characters walk through it.</p>
      <p v-else-if="done && done.ok" class="status ok">Done: it plays over the picture above{{ done.warnings?.length ? ` (${done.warnings[0]})` : '' }}. It's on the building now
        (Inspector ▸ AnimatedPart); every copy of its prefab has it.</p>
      <p v-else-if="done" class="status error">It wasn't animated: {{ done.error }}</p>
      <div class="buttons">
        <button @click="close">{{ done ? 'Close' : working ? 'Close (it goes on)' : 'Cancel' }}</button>
        <button class="primary" :disabled="!!working || !box || (box[2] ?? 0) < 4 || (box[3] ?? 0) < 4" @click="animate">
          {{ done ? 'Animate again' : 'Animate' }}
        </button>
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
.played {
  position: absolute;
  background-repeat: no-repeat;
  pointer-events: none;
}
.status {
  margin: 0;
}
.status.ok {
  color: var(--accent, #4a8);
}
.status.error {
  color: #e66;
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
