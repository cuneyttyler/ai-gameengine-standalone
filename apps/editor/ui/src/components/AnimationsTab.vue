<script setup lang="ts">
// Assets ▸ Animations: every animation sheet of the project (assets/**/*.anim.json: generated with Generate ▸ Animate,
// imported in Project ▸ Effects), each playing its first clip. Right-click: a prefab that plays it, the selection
// playing it, or the sheet in the Effects panel.
import { computed, onMounted, onUnmounted, reactive, ref } from 'vue'
import { openContextMenu, separator, type MenuItem } from '../contextMenu'
import { editorState } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'
import { panelTab } from '../paneltabs'

// `characters`: Assets ▸ Characters, the character sheets alone (each turning round), with their own menu.
const props = defineProps<{ filter: string; characters?: boolean }>()
const PROJECT_HOST = 'https://aige-project.local/'

interface Frame {
  x: number
  y: number
  w: number
  h: number
}
interface Clip {
  name: string
  directions: Record<string, Frame[]>
}
interface Sheet {
  path: string
  texture: string
  size: [number, number]
  clips: Clip[]
  kind?: string
  character?: { name?: string; description?: string }
}

const sheets = ref<Sheet[]>([])
const tick = ref(0)
const picked = ref('')
// The clip each card plays (by sheet): its idle (a character turning round) until another is clicked.
const clipShown = reactive<Record<string, string>>({})
let timer = 0

onHostMessage((message) => {
  if (message.type === 'animations:list' || message.type === 'fx:model') sheets.value = (message.sheets as Sheet[]) ?? sheets.value
})
onMounted(() => {
  sendToHost({ type: 'animations:request' })
  timer = window.setInterval(() => tick.value++, 125)
})
onUnmounted(() => window.clearInterval(timer))

const shown = computed(() => {
  const words = props.filter.toLowerCase().split(/\s+/).filter(Boolean)
  return sheets.value.filter(
    (s) =>
      (s.kind === 'character') === !!props.characters &&
      words.every((w) => (s.path + ' ' + (s.character?.description ?? '') + ' ' + s.clips.map((c) => c.name).join(' ')).toLowerCase().includes(w)),
  )
})

// Its first clip's frames (idle first), facing the first way it has.
function framesOf(sheet: Sheet): Frame[] {
  const clip = sheet.clips.find((c) => c.name === 'idle') ?? sheet.clips[0]
  return clip ? (Object.values(clip.directions)[0] ?? []) : []
}
const sizes = reactive<Record<string, [number, number]>>({})
function sizeOf(sheet: Sheet): [number, number] {
  if (sheet.size[0] > 0 && sheet.size[1] > 0) return sheet.size
  if (!sizes[sheet.path]) {
    let w = 1, h = 1 // without a size: the frames' extent
    for (const c of sheet.clips) for (const list of Object.values(c.directions)) for (const f of list) (w = Math.max(w, f.x + f.w)), (h = Math.max(h, f.y + f.h))
    sizes[sheet.path] = [w, h]
  }
  return sizes[sheet.path]!
}
// A character turning round: its idle pose each way, in turn.
function turningOf(sheet: Sheet): Frame[] {
  const clip = sheet.clips.find((c) => c.name === 'idle') ?? sheet.clips[0]
  const order = ['s', 'se', 'e', 'ne', 'n', 'nw', 'w', 'sw']
  return clip ? order.map((d) => clip.directions[d]?.[0]).filter((f): f is Frame => !!f) : []
}
// A clip played: a character's every direction in turn (twice round each), another sheet's first direction.
function clipFrames(sheet: Sheet, name: string): Frame[] {
  const clip = sheet.clips.find((c) => c.name === name)
  if (!clip) return []
  if (sheet.kind !== 'character') return Object.values(clip.directions)[0] ?? []
  const order = ['s', 'se', 'e', 'ne', 'n', 'nw', 'w', 'sw']
  return order.flatMap((d) => {
    const list = clip.directions[d] ?? []
    return list.length > 1 ? [...list, ...list] : list.length ? [list[0]!, list[0]!, list[0]!, list[0]!] : []
  })
}
function frameStyle(sheet: Sheet): Record<string, string> {
  const chosen = clipShown[sheet.path]
  const frames = chosen ? clipFrames(sheet, chosen) : sheet.kind === 'character' ? turningOf(sheet) : framesOf(sheet)
  const step = !chosen && sheet.kind === 'character' ? Math.floor(tick.value / 5) : tick.value
  const f = frames.length ? frames[step % frames.length]! : null
  if (!f || f.w <= 0 || f.h <= 0) return {}
  const scale = Math.min(76 / f.w, 76 / f.h)
  const [tw, th] = sizeOf(sheet)
  return {
    width: `${f.w * scale}px`,
    height: `${f.h * scale}px`,
    backgroundImage: `url("${PROJECT_HOST}${sheet.texture}")`,
    backgroundPosition: `-${f.x * scale}px -${f.y * scale}px`,
    backgroundSize: `${tw * scale}px ${th * scale}px`,
    imageRendering: f.w <= 128 ? 'pixelated' : 'auto',
  }
}
function name(sheet: Sheet): string {
  return sheet.character?.name || sheet.path.split('/').pop()!.replace(/(\.character)?\.anim\.json$/, '')
}

function showInEffects(sheet: Sheet, edit = false): void {
  editorState.fxSheetEdit = edit
  editorState.fxSheet = sheet.path
  panelTab.project = 'fx'
}
function menu(sheet: Sheet, event: MouseEvent): void {
  picked.value = sheet.path
  if (sheet.kind === 'character') {
    openContextMenu(event.clientX, event.clientY, [
      { label: 'Make player', disabled: editorState.playing, action: () => sendToHost({ type: 'character:prefab', sheet: sheet.path, npc: false }) },
      { label: 'Make NPC', disabled: editorState.playing, action: () => sendToHost({ type: 'character:prefab', sheet: sheet.path, npc: true }) },
      { label: 'Animate…', action: () => ((editorState.animateSheet = sheet.path), (panelTab.generate = 'animate')) },
      separator,
      { label: 'Edit…', action: () => showInEffects(sheet, true) },
      { label: 'Show in Explorer', action: () => sendToHost({ type: 'asset:reveal', path: sheet.path }) },
    ])
    return
  }
  const items: MenuItem[] = [
    { label: 'Make prefab', disabled: editorState.playing, action: () => sendToHost({ type: 'animation:prefab', sheet: sheet.path }) },
    {
      label: 'Add to selected',
      disabled: editorState.playing || editorState.selectedId === null,
      action: () => sendToHost({ type: 'entity:addAnimator', sheet: sheet.path }),
    },
    separator,
    { label: 'Edit…', action: () => showInEffects(sheet, true) },
    { label: 'Show in Effects', action: () => showInEffects(sheet) },
    { label: 'Show in Explorer', action: () => sendToHost({ type: 'asset:reveal', path: sheet.path }) },
  ]
  openContextMenu(event.clientX, event.clientY, items)
}
</script>

<template>
  <div class="animations">
    <p v-if="!shown.length && characters" class="muted pad">
      No characters yet. Make one with Generate ▸ Character: one sheet, every direction, and its actions added in Generate ▸ Animate.
    </p>
    <p v-else-if="!shown.length" class="muted pad">
      No animations yet. Make one with Generate ▸ Animate (Make animation), or import a sprite sheet in Project ▸ Effects.
    </p>
    <div class="grid">
      <div
        v-for="s in shown"
        :key="s.path"
        class="card"
        :class="{ selected: picked === s.path }"
        tabindex="0"
        :title="`${s.path}\nClick a clip to play it; double-click one to open it in Effects\nRight-click: ${characters ? 'make the player or an NPC, animate' : 'make a prefab, add to the selection'}`"
        @click="picked = s.path"
        @contextmenu.prevent="menu(s, $event)"
      >
        <div class="preview"><div class="frame" :style="frameStyle(s)"></div></div>
        <div class="label">{{ name(s) }}</div>
        <div class="clips">
          <button
            v-for="c in s.clips"
            :key="c.name"
            class="clip"
            :class="{ on: (clipShown[s.path] ?? '') === c.name }"
            :title="`Play ${c.name}${s.kind === 'character' ? ', every direction in turn' : ''}`"
            @click.stop="clipShown[s.path] = clipShown[s.path] === c.name ? '' : c.name"
            @dblclick.stop="showInEffects(s)"
          >
            {{ c.name }}
          </button>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.animations {
  height: 100%;
  overflow-y: auto;
}
.pad {
  padding: 10px;
}
.muted {
  opacity: 0.6;
}
.grid {
  display: flex;
  flex-wrap: wrap;
  align-content: flex-start;
  gap: 6px;
  padding: 6px;
}
.card {
  width: 104px;
  padding: 4px;
  background: #22222e;
  border: 1px solid #2f2f3f;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
  text-align: center;
}
.card.selected {
  border-color: #3b82f6;
}
.preview {
  width: 80px;
  height: 80px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: #181822;
}
.label {
  font-size: 0.82em;
  width: 100%;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.clips {
  display: flex;
  flex-wrap: wrap;
  justify-content: center;
  gap: 2px;
  width: 100%;
}
.clip {
  font-size: 0.7em;
  padding: 0 4px;
  opacity: 0.75;
}
.clip.on {
  opacity: 1;
  border-color: #3b82f6;
  color: #fff;
}
</style>
