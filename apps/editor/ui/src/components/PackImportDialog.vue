<script setup lang="ts">
// Assets ▸ Import as prefabs (development_plan.md 6.3): the plan for turning
// images into prefabs — ground tiles flat and a cell big, everything else sized
// by its pack's one scale — to review and adjust before anything is made.
import { computed, nextTick, ref, watch } from 'vue'
import { editorState } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'
import { packImport } from '../packImport'
import { trackOverlay, untrackOverlay } from '../overlay'

interface PlanRow {
  asset: string
  path: string
  name: string
  kind: string
  tile: boolean
  solid: boolean
  cells: number
  aspect: number
  include: boolean
}

interface Scale {
  folder: string
  pixelsPerCell: number
  source: string
}

const PROJECT_HOST = 'https://aige-project.local/'
const backdrop = ref<HTMLElement | null>(null)
const rows = ref<PlanRow[]>([])
const scales = ref<Scale[]>([])
const waiting = ref(false)


onHostMessage((message) => {
  if (message.type !== 'pack:plan') return
  waiting.value = false
  // Keep what the user changed when the plan is recomputed (a new scale).
  const before = new Map(rows.value.map((r) => [r.asset, r]))
  rows.value = (message.plans as PlanRow[]).map((plan) => {
    const old = before.get(plan.asset)
    return old
      ? { ...plan, include: old.include, name: old.name, tile: old.tile, solid: old.solid, cells: old.tile === plan.tile && !plan.tile ? plan.cells : old.cells }
      : { ...plan, include: true }
  })
  scales.value = message.scales as Scale[]
})

watch(
  () => packImport.open,
  async (open) => {
    if (open) {
      rows.value = []
      waiting.value = true
      sendToHost({ type: 'pack:plan', paths: packImport.paths })
    }
    await nextTick()
    if (open && backdrop.value) {
      trackOverlay(backdrop.value)
      backdrop.value.focus()
    }
  },
)

function close(): void {
  if (backdrop.value) untrackOverlay(backdrop.value)
  packImport.open = false
}

function setScale(folder: string, value: number): void {
  if (Number.isFinite(value) && value > 0) sendToHost({ type: 'pack:setScale', folder, pixelsPerCell: value })
}

const chosen = computed(() => rows.value.filter((r) => r.include))

function create(): void {
  sendToHost({ type: 'pack:import', plans: chosen.value })
  close()
}

function thumb(path: string): string {
  return `${PROJECT_HOST}${path.split('/').map(encodeURIComponent).join('/')}`
}
</script>

<template>
  <div v-if="packImport.open" ref="backdrop" class="backdrop" tabindex="-1" @mousedown.self="close" @keydown.esc.stop="close">
    <div class="sheet">
      <header>
        <h3>Import as prefabs</h3>
        <span class="spacer"></span>
        <button title="Close (Esc)" @click="close">×</button>
      </header>
      <p class="muted">
        Ground tiles lie flat, one cell each. Everything else is sized by its pack's scale, so a pack keeps its proportions.
        Analyze the images first for better names, kinds and sizes.
      </p>
      <div v-for="scale in scales" :key="scale.folder" class="scale">
        <span class="folder">{{ scale.folder }}</span>
        <label>
          <input
            type="number"
            min="1"
            step="1"
            :value="Math.round(scale.pixelsPerCell)"
            @change="setScale(scale.folder, Number(($event.target as HTMLInputElement).value))"
          />
          image pixels per cell
        </label>
        <span class="muted">({{ scale.source }})</span>
      </div>
      <p v-if="waiting" class="muted">Working out the plan…</p>
      <div class="rows">
        <div v-for="row in rows" :key="row.asset" class="row" :class="{ off: !row.include }">
          <input v-model="row.include" type="checkbox" :title="row.asset" />
          <span class="thumb"><img :src="thumb(row.asset)" alt="" /></span>
          <input v-model="row.name" class="name" spellcheck="false" :title="row.path" />
          <span class="kind">{{ row.kind }}</span>
          <select v-model="row.tile">
            <option :value="true">Tile</option>
            <option :value="false">Prop</option>
          </select>
          <label v-if="!row.tile" class="cells" title="Width in grid cells">
            <input v-model.number="row.cells" type="number" min="0.1" max="40" step="0.1" /> cells
          </label>
          <span v-else class="cells muted">1 cell</span>
          <label class="solid"><input v-model="row.solid" type="checkbox" /> solid</label>
        </div>
      </div>
      <footer>
        <span class="muted">{{ chosen.length }} of {{ rows.length }}</span>
        <span class="spacer"></span>
        <button @click="close">Cancel</button>
        <button class="primary" :disabled="chosen.length === 0 || editorState.playing" @click="create">
          Create {{ chosen.length }} prefab(s)
        </button>
      </footer>
    </div>
  </div>
</template>

<style scoped>
.backdrop {
  position: fixed;
  inset: 0;
  z-index: 1000;
  background: rgba(5, 5, 10, 0.55);
  display: flex;
  align-items: center;
  justify-content: center;
  outline: none;
}
.sheet {
  width: min(860px, 94vw);
  max-height: 88vh;
  display: flex;
  flex-direction: column;
  background: #181825;
  border: 1px solid #333;
  border-top: 2px solid #3b82f6;
  padding: 14px 18px;
}
header,
footer {
  display: flex;
  align-items: center;
  gap: 8px;
}
footer {
  margin-top: 10px;
}
h3 {
  margin: 0;
}
.spacer {
  flex: 1;
}
.muted {
  opacity: 0.6;
  font-size: 0.9em;
}
.scale {
  display: flex;
  align-items: center;
  gap: 10px;
  margin: 4px 0;
}
.folder {
  font-family: ui-monospace, Consolas, monospace;
  font-size: 0.85em;
}
.rows {
  overflow: auto;
  margin-top: 8px;
  border-top: 1px solid #2a2a3a;
}
.row {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 3px 0;
  border-bottom: 1px solid #22222e;
}
.row.off {
  opacity: 0.45;
}
.thumb {
  width: 36px;
  height: 36px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: #2a2a35;
}
.thumb img {
  max-width: 34px;
  max-height: 34px;
}
.name {
  flex: 1;
  min-width: 120px;
}
.kind {
  width: 80px;
  font-size: 0.85em;
  opacity: 0.7;
}
.cells {
  width: 90px;
}
.cells input {
  width: 56px;
}
button,
input:not([type='checkbox']),
select {
  height: 24px;
}
button {
  cursor: pointer;
}
button.primary {
  border-color: #3b82f6;
}
button:disabled {
  opacity: 0.4;
}
</style>
