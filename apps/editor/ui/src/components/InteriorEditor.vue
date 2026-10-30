<script setup lang="ts">
// The Interior component edited as what it is (docs/places/interiors.md): its floors' pictures (one a storey: added,
// removed), each floor change's stairs cell, and its ways in. Written back as its fields (floors, stairs, entries: the
// text lists the engine reads), through the Inspector's own setField.
import { computed, ref } from 'vue'
import FieldEditor, { type Choices, type FieldData } from './FieldEditor.vue'
import { editorState } from '../editorState'
import { sendToHost } from '../ipc'
import { promptDialog } from '../dialogs'
import { aiCalls } from '../aiProgress'
import { redesigns } from '../redesigns'

const props = defineProps<{ fields: FieldData[]; choices?: Choices; disabled?: boolean; id?: number }>()
const emit = defineEmits<{ (e: 'set', field: string, value: string | number): void }>()

const PROJECT_HOST = 'https://aige-project.local/'
const value = (name: string): string => String(props.fields.find((f) => f.name === name)?.value ?? '')
const list = (text: string): string[] => text.split(';').map((s) => s.trim()).filter(Boolean)

const ground = computed(() => value('texture'))
const upper = computed(() => list(value('floors')))
const floorCount = computed(() => (ground.value ? 1 + upper.value.length : 0))
const stairs = computed(() => list(value('stairs')))
const entries = computed(() => list(value('entries')))
const adding = ref('')

// A picture field for the Inspector's own editor (the project's textures to choose from).
function pictureField(name: string, picture: string): FieldData {
  return { name, type: 'texture', description: 'The floor\'s picture: the building cut away, its rooms on that storey.', value: picture }
}

function setFloor(index: number, picture: string): void {
  if (index === 0) {
    emit('set', 'texture', picture)
    return
  }
  const next = [...upper.value]
  next[index - 1] = picture
  emit('set', 'floors', next.filter(Boolean).join(';'))
}
function removeFloor(index: number): void {
  const next = upper.value.filter((_, i) => i !== index - 1)
  emit('set', 'floors', next.join(';'))
  // Its stairs: the floor changes above the last floor go.
  if (stairs.value.length > next.length) emit('set', 'stairs', stairs.value.slice(0, next.length).join(';'))
}
function addFloor(): void {
  if (!adding.value) return
  emit('set', 'floors', [...upper.value, adding.value].join(';'))
  adding.value = ''
}
function setStairs(change: number, cell: string): void {
  const next = [...stairs.value]
  while (next.length < change) next.push('')
  next[change - 1] = cell.trim()
  // An empty one in the middle would shift the rest: kept as its corner by leaving trailing empties out only.
  while (next.length && !next[next.length - 1]) next.pop()
  emit('set', 'stairs', next.join(';'))
}
// Its floors painted again from its exterior (as many as it has now): a new design, its stairs where they're painted.
async function redesign(): Promise<void> {
  const id = props.id
  if (id === undefined) return
  // Its design in the user's words (empty: as the building is, from what kind it is).
  const design = await promptDialog({
    title: 'Redesign interiors',
    message: `Paint its ${floorCount.value} floor(s) again from its exterior? Its pictures are replaced (every copy of its prefab). ` +
      'Describe the interior you want, or leave it empty for one that fits the building.',
    placeholder: "e.g. a cosy candle-lit family home, a cluttered alchemist's lab",
    confirmLabel: 'Redesign',
  })
  if (design === null) return
  redesigns[id] = { state: 'working', text: '' }
  sendToHost({ type: 'interior:redesign', id, design: design.trim() })
}
// This building's redesign: working (its bar: the floors painted of all, from the service's progress), done, failed.
const redesign_ = computed(() => (props.id === undefined ? undefined : redesigns[props.id]))
const paint = computed(() => aiCalls.value.find((c) => c.path === '/assets/interior' && c.phase !== 'done' && c.phase !== 'failed'))
const percent = computed(() => (paint.value?.total ? Math.round((100 * (paint.value.done ?? 0)) / paint.value.total) : null))

function removeEntry(index: number): void {
  emit('set', 'entries', entries.value.filter((_, i) => i !== index).join(';'))
}
</script>

<template>
  <div class="interior-editor">
    <h5>Floors <span class="dim">({{ floorCount }})</span></h5>
    <div v-for="(picture, i) in [ground, ...upper]" :key="i" class="floor">
      <img v-if="picture" :src="PROJECT_HOST + picture" alt="" class="thumb" />
      <span class="label">Floor {{ i + 1 }}</span>
      <FieldEditor :field="pictureField(`floor ${i + 1}`, picture)" :choices="choices" :disabled="disabled" @change="setFloor(i, String($event))" />
      <button v-if="i > 0" class="small" :disabled="disabled" title="This floor removed (two storeys counted as three)" @click="removeFloor(i)">✕</button>
    </div>
    <div v-if="ground" class="floor">
      <span class="label">Add a floor</span>
      <FieldEditor :field="pictureField('add', adding)" :choices="choices" :disabled="disabled" @change="adding = String($event)" />
      <button class="small" :disabled="disabled || !adding" @click="addFloor">Add</button>
    </div>

    <h5>Stairs and ways in</h5>
    <p class="dim small">
      {{ floorCount > 1 ? `Stairs: ${stairs.filter(Boolean).length ? stairs.filter(Boolean).length + ' of ' + (floorCount - 1) + ' drawn' : 'its corners'}` : 'One floor: no stairs' }} ·
      Ways in: {{ entries.length ? 'drawn' : 'where its doors are' }}
    </p>
    <button v-if="id !== undefined && ground" :disabled="disabled" title="Its exterior and each floor's interior: draw its stairs and its ways in"
            @click="editorState.buildingEditor = id">Edit building…</button>
    <button v-if="id !== undefined && ground" :disabled="disabled || editorState.ai.state !== 'ready'"
            title="Its floors painted again from its exterior (BFL: each floor's room from a blockout, laid on the exterior): a new design"
            @click="redesign">Redesign interiors…</button>
    <!-- Its redesign: a bar while its floors are painted, then that it's done (or why not). -->
    <div v-if="redesign_" class="redesign" :class="redesign_.state">
      <template v-if="redesign_.state === 'working'">
        <span class="small">Redesigning its interiors{{ paint?.step ? `: ${paint.step}` : '…' }}</span>
        <div class="bar"><div v-if="percent !== null" class="fill" :style="{ width: `${percent}%` }"></div><div v-else class="run"></div></div>
      </template>
      <span v-else class="small">{{ redesign_.text }}</span>
      <button v-if="redesign_.state !== 'working'" class="small" title="Dismiss" @click="delete redesigns[id!]">✕</button>
    </div>
  </div>
</template>

<style scoped>
.interior-editor {
  display: flex;
  flex-direction: column;
  gap: 4px;
  margin-bottom: 8px;
  padding-bottom: 8px;
  border-bottom: 1px solid var(--border, #333);
}
h5 {
  margin: 6px 0 2px;
  font-size: 0.95em;
}
.floor,
.row {
  display: flex;
  align-items: center;
  gap: 6px;
}
.redesign {
  display: flex;
  flex-direction: column;
  gap: 3px;
}
.redesign.done,
.redesign.failed {
  flex-direction: row;
  align-items: center;
  justify-content: space-between;
}
.redesign.done span {
  color: #4ade80;
}
.redesign.failed span {
  color: #f87171;
}
.bar {
  position: relative;
  height: 4px;
  background: #2a2a3a;
  overflow: hidden;
}
.fill {
  position: absolute;
  inset: 0 auto 0 0;
  background: #3b82f6;
  transition: width 0.4s;
}
.run {
  position: absolute;
  top: 0;
  bottom: 0;
  width: 30%;
  background: #3b82f6;
  animation: run 1.4s linear infinite;
}
@keyframes run {
  from {
    left: -30%;
  }
  to {
    left: 100%;
  }
}
.thumb {
  width: 32px;
  height: 32px;
  object-fit: contain;
  background: #222;
}
.label {
  min-width: 84px;
}
.row input {
  flex: 1;
  min-width: 0;
}
</style>
