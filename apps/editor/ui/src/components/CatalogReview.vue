<script setup lang="ts">
// The catalog review (development_plan.md 7.3): what the builder knows about
// each piece of art — its role, footprint, anchor, perspective, layer,
// facings, piece set — measured from the pixels and described by the vision
// model, with what looks wrong. Any field can be set here; what's set by hand
// wins over the analysis.
import { computed, onMounted, ref, watch } from 'vue'
import { onHostMessage, sendToHost } from '../ipc'
import CatalogThumb from './CatalogThumb.vue'

interface Entry {
  id: string
  path: string
  rect: [number, number, number, number]
  base: [number, number, number, number]
  opaque: [number, number, number, number]
  analysed: boolean
  name: string
  kind: string
  roles: string[]
  concepts: string[]
  affordance: string
  perspective: string
  flat: boolean
  tileType: string
  layer: string
  footprint: { w: number; h: number; cellsW: number; cellsH: number }
  anchor: [number, number]
  sprite: [number, number]
  pixelsPerCell: number
  facings: string
  variantOf: string
  anchors: Record<string, [number, number]>
  passable: boolean
  accessible: boolean
  pieceSet: string
  piece: string
  pieceGrid: string
  palette: string[]
  shadow: boolean
  source: string
  problems: string[]
}

interface PieceSet {
  id: string
  role: string
  grid: string
  pieces: Record<string, string>
  missing: string[]
  usable: boolean
}

const props = defineProps<{ versions: Record<string, number>; filter: string }>()

const PROJECT_HOST = 'https://aige-project.local/'
const PERSPECTIVES = ['top-down', 'three-quarter', 'isometric']
const LAYERS = ['ground', 'decal', 'prop', 'building', 'overhead']
const FACINGS = ['any', 'two', 'one']
const PIECES = ['straight-h', 'straight-v', 'straight-x', 'straight-y', 'corner-ne', 'corner-se', 'corner-sw', 'corner-nw',
  't-n', 't-e', 't-s', 't-w', 'cross', 'end-n', 'end-e', 'end-s', 'end-w', 'post', 'gate']

const entries = ref<Entry[]>([])
const pieceSets = ref<PieceSet[]>([])
const roles = ref<string[]>([])
const grid = ref('iso')
const show = ref<'all' | 'problems' | 'pieces' | 'unanalysed'>('all')
const selectedId = ref<string | null>(null)

onHostMessage((message) => {
  if (message.type === 'catalog:state') {
    entries.value = message.entries as Entry[]
    pieceSets.value = message.pieceSets as PieceSet[]
    roles.value = message.roles as string[]
    grid.value = message.grid as string
  } else if (message.type === 'assets:list') {
    request()
  }
})

let pending = 0
function request(): void {
  // Analysis results arrive in bursts: ask once they settle.
  window.clearTimeout(pending)
  pending = window.setTimeout(() => sendToHost({ type: 'catalog:request' }), 150)
}
onMounted(() => sendToHost({ type: 'catalog:request' }))

const shown = computed(() => {
  const words = props.filter.trim().toLowerCase()
  return entries.value.filter((e) => {
    if (show.value === 'problems' && e.problems.length === 0) return false
    if (show.value === 'pieces' && !e.piece) return false
    if (show.value === 'unanalysed' && e.analysed) return false
    if (!words) return true
    return [e.id, e.name, e.kind, ...e.roles, ...e.concepts].some((t) => t.toLowerCase().includes(words))
  })
})
const problemCount = computed(() => entries.value.filter((e) => e.problems.length > 0).length)
const selected = computed(() => entries.value.find((e) => e.id === selectedId.value) ?? null)
const selectedSet = computed(() => pieceSets.value.find((s) => s.id === selected.value?.pieceSet) ?? null)
watch(shown, (list) => {
  if (selectedId.value && !list.some((e) => e.id === selectedId.value)) selectedId.value = null
})

function urlOf(path: string): string {
  return `${PROJECT_HOST}${path.split('/').map(encodeURIComponent).join('/')}?v=${props.versions[path] ?? 0}`
}

function set(fields: Record<string, string>): void {
  if (!selected.value) return
  sendToHost({ type: 'catalog:set', id: selected.value.id, fields })
}

// Editable copies of the selected entry's fields.
const rolesText = ref('')
const footprintText = ref('')
watch(selected, (e) => {
  rolesText.value = e ? e.roles.join(', ') : ''
  footprintText.value = e ? `${e.footprint.cellsW}x${e.footprint.cellsH}` : ''
}, { immediate: true })

function saveRoles(): void {
  const list = rolesText.value.split(',').map((r) => r.trim()).filter(Boolean)
  set({ roles: list.join(', ') })
}
function saveFootprint(): void {
  const text = footprintText.value.trim()
  if (text === '' || /^\d+x\d+$/.test(text)) set({ footprint: text })
}
function analyse(): void {
  const paths = [...new Set(shown.value.map((e) => e.path))]
  if (paths.length) sendToHost({ type: 'assets:analyze', paths })
}
const fixed = (n: number, d = 1) => (Math.round(n * 10 ** d) / 10 ** d).toString()
</script>

<template>
  <div class="catalog">
    <div class="bar">
      <select v-model="show" title="Which entries to list">
        <option value="all">All ({{ entries.length }})</option>
        <option value="problems">Problems ({{ problemCount }})</option>
        <option value="pieces">Fence, wall and hedge pieces</option>
        <option value="unanalysed">Not analysed yet</option>
      </select>
      <span class="grid" :title="'The project\'s grid: art that lies flat must be drawn for it'">
        Grid: {{ grid === 'iso' ? 'isometric' : 'square' }}
      </span>
      <button :disabled="shown.length === 0" title="Measure the pixels again and (with AI on) describe the art" @click="analyse">
        Analyze shown
      </button>
    </div>
    <div class="split">
      <div class="list">
        <button
          v-for="e in shown"
          :key="e.id"
          :class="['row', { active: e.id === selectedId }]"
          @click="selectedId = e.id"
        >
          <CatalogThumb :src="urlOf(e.path)" :rect="e.rect" :size="36" />
          <span class="text">
            <span class="name">{{ e.name }}<span v-if="e.id.includes('#')" class="dim"> · object {{ e.id.split('#')[1] }}</span></span>
            <span class="meta">
              {{ e.roles.length ? e.roles.join(', ') : 'no role' }} · {{ e.footprint.cellsW }}×{{ e.footprint.cellsH }}
              · {{ e.perspective || '?' }}<template v-if="e.piece"> · {{ e.piece }}</template>
            </span>
          </span>
          <span v-if="e.problems.length" class="badge" :title="e.problems.join('\n')">{{ e.problems.length }}</span>
        </button>
        <div v-if="shown.length === 0" class="empty">Nothing to show. Analyze the project's images to fill the catalog.</div>
      </div>

      <div v-if="selected" class="detail">
        <CatalogThumb
          :src="urlOf(selected.path)"
          :rect="selected.rect"
          :base="selected.base"
          :anchor="selected.anchor"
          :anchors="selected.anchors"
          :size="180"
          marks
        />
        <div class="legend">
          <span><i class="key base" /> base (footprint)</span>
          <span><i class="key anchor" /> stands here</span>
          <span v-if="Object.keys(selected.anchors).length"><i class="key point" /> {{ Object.keys(selected.anchors).join(', ') }}</span>
        </div>
        <ul v-if="selected.problems.length" class="problems">
          <li v-for="p in selected.problems" :key="p">{{ p }}</li>
        </ul>
        <dl>
          <dt>Roles</dt>
          <dd>
            <input v-model="rolesText" list="catalog-roles" spellcheck="false" @change="saveRoles" />
            <datalist id="catalog-roles"><option v-for="r in roles" :key="r" :value="r" /></datalist>
          </dd>
          <dt>Shows</dt>
          <dd>{{ selected.concepts.join(', ') || '—' }}</dd>
          <dt>Kind</dt>
          <dd>{{ selected.kind || '—' }} · {{ selected.affordance }}</dd>
          <dt>Perspective</dt>
          <dd>
            <select :value="selected.perspective" @change="set({ perspective: ($event.target as HTMLSelectElement).value })">
              <option v-for="p in PERSPECTIVES" :key="p" :value="p">{{ p }}</option>
              <option v-if="!PERSPECTIVES.includes(selected.perspective)" :value="selected.perspective">{{ selected.perspective || 'unknown' }}</option>
            </select>
            <label class="check" title="Lies on the ground (drawn as the cell) rather than standing up">
              <input type="checkbox" :checked="selected.flat" @change="set({ flat: ($event.target as HTMLInputElement).checked ? '1' : '0' })" />
              flat
            </label>
          </dd>
          <dt>Footprint</dt>
          <dd>
            <input v-model="footprintText" class="short" title="Cells on the ground, w x h" @change="saveFootprint" />
            <span class="dim">measured {{ fixed(selected.footprint.w) }} × {{ fixed(selected.footprint.h) }} at {{ fixed(selected.pixelsPerCell, 0) }} px/cell</span>
          </dd>
          <dt>On screen</dt>
          <dd>{{ fixed(selected.sprite[0]) }} × {{ fixed(selected.sprite[1]) }} cells · anchor {{ fixed(selected.anchor[0], 2) }}, {{ fixed(selected.anchor[1], 2) }}</dd>
          <dt>Layer</dt>
          <dd>
            <select :value="selected.layer" @change="set({ layer: ($event.target as HTMLSelectElement).value })">
              <option v-for="l in LAYERS" :key="l" :value="l">{{ l }}</option>
            </select>
            <span class="dim">{{ selected.tileType }}</span>
          </dd>
          <dt>Facings</dt>
          <dd>
            <select :value="selected.facings" @change="set({ facings: ($event.target as HTMLSelectElement).value })">
              <option v-for="f in FACINGS" :key="f" :value="f">{{ f === 'two' ? 'two (drawn + mirrored)' : f }}</option>
            </select>
          </dd>
          <dt>Movement</dt>
          <dd>{{ selected.passable ? 'can be walked through' : 'blocks the way' }}<template v-if="selected.accessible"> · can be entered</template></dd>
          <template v-if="selected.piece">
            <dt>Piece</dt>
            <dd>
              <select :value="selected.piece" @change="set({ piece: ($event.target as HTMLSelectElement).value })">
                <option v-for="p in PIECES" :key="p" :value="p">{{ p }}</option>
              </select>
              <span class="dim">joins on a {{ selected.pieceGrid || '?' }} grid</span>
            </dd>
          </template>
          <dt>Palette</dt>
          <dd class="palette">
            <i v-for="c in selected.palette" :key="c" :style="{ background: c }" :title="c" />
          </dd>
          <dt>Variants</dt>
          <dd class="dim">{{ selected.variantOf }}</dd>
        </dl>
        <div v-if="selectedSet" class="set">
          <div class="set-title">
            Piece set {{ selectedSet.id }} · {{ selectedSet.grid }} grid ·
            <span :class="selectedSet.usable ? 'ok' : 'bad'">{{ selectedSet.usable ? 'usable' : 'lacks ' + selectedSet.missing.join(', ') }}</span>
          </div>
          <div class="pieces">
            <button v-for="(id, kind) in selectedSet.pieces" :key="kind" :class="{ active: id === selected.id }" @click="selectedId = id">
              {{ kind }}
            </button>
          </div>
        </div>
      </div>
      <div v-else class="detail empty">Pick an entry to review what was understood about it.</div>
    </div>
  </div>
</template>

<style scoped>
.catalog {
  display: flex;
  flex-direction: column;
  min-height: 0;
  overflow: hidden;
}
.bar {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 6px 8px;
  border-bottom: 1px solid #333;
  flex-shrink: 0;
}
.bar .grid {
  color: #a6adc8;
}
select,
input,
button {
  background: #1e1e2e;
  color: #cdd6f4;
  border: 1px solid #45475a;
  border-radius: 0;
  font: inherit;
  padding: 2px 6px;
}
button:hover:not(:disabled) {
  border-color: #89b4fa;
}
button:disabled {
  opacity: 0.5;
}
.split {
  display: flex;
  flex: 1;
  min-height: 0;
}
.list {
  width: 50%;
  min-width: 200px;
  overflow-y: auto;
  border-right: 1px solid #333;
}
.row {
  display: flex;
  align-items: center;
  gap: 8px;
  width: 100%;
  padding: 4px 8px;
  border: 0;
  border-bottom: 1px solid #262637;
  background: transparent;
  text-align: left;
}
.row.active {
  background: #313244;
}
.text {
  display: flex;
  flex-direction: column;
  flex: 1;
  min-width: 0;
}
.name,
.meta {
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.meta,
.dim {
  color: #7f849c;
}
.badge {
  background: #f38ba8;
  color: #11111b;
  padding: 0 6px;
  font-weight: 600;
}
.detail {
  flex: 1;
  overflow-y: auto;
  padding: 8px;
}
.empty {
  color: #7f849c;
  padding: 12px;
}
.legend {
  display: flex;
  gap: 10px;
  margin: 6px 0;
  color: #a6adc8;
}
.key {
  display: inline-block;
  width: 9px;
  height: 9px;
  margin-right: 3px;
  vertical-align: middle;
}
.key.base {
  background: #89b4fa;
}
.key.anchor {
  background: #f38ba8;
}
.key.point {
  background: #a6e3a1;
}
.problems {
  margin: 6px 0;
  padding: 6px 8px 6px 20px;
  background: #3a1f2b;
  border-left: 3px solid #f38ba8;
}
dl {
  display: grid;
  grid-template-columns: auto 1fr;
  gap: 4px 10px;
  margin: 8px 0;
}
dt {
  color: #a6adc8;
}
dd {
  margin: 0;
  display: flex;
  align-items: center;
  gap: 6px;
  flex-wrap: wrap;
}
dd input:not([type='checkbox']) {
  flex: 1;
  min-width: 80px;
}
dd input.short {
  flex: 0 0 60px;
}
.check {
  display: flex;
  align-items: center;
  gap: 3px;
}
.palette i {
  display: inline-block;
  width: 16px;
  height: 16px;
  border: 1px solid #11111b;
}
.set {
  border-top: 1px solid #333;
  padding-top: 6px;
}
.set-title {
  margin-bottom: 4px;
}
.ok {
  color: #a6e3a1;
}
.bad {
  color: #f38ba8;
}
.pieces {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}
.pieces button.active {
  border-color: #89b4fa;
  background: #313244;
}
</style>
