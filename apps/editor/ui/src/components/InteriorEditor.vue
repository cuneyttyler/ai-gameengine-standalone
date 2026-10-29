<script setup lang="ts">
// The Interior component edited as what it is (docs/places/interiors.md): its floors' pictures (one a storey: added,
// removed), each floor change's stairs cell, and its ways in. Written back as its fields (floors, stairs, entries: the
// text lists the engine reads), through the Inspector's own setField.
import { computed, ref } from 'vue'
import FieldEditor, { type Choices, type FieldData } from './FieldEditor.vue'

const props = defineProps<{ fields: FieldData[]; choices?: Choices; disabled?: boolean }>()
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

    <template v-if="floorCount > 1">
      <h5>Stairs <span class="dim">a cell each floor change (x,y); empty: its corner. Right-click inside it ▸ Stairs here.</span></h5>
      <div v-for="change in floorCount - 1" :key="change" class="row">
        <span class="label">Floor {{ change }} ↔ {{ change + 1 }}</span>
        <input :value="stairs[change - 1] ?? ''" :disabled="disabled" placeholder="its corner" spellcheck="false"
               @change="setStairs(change, ($event.target as HTMLInputElement).value)" />
        <button class="small" :disabled="disabled || !stairs[change - 1]" title="Back to its corner" @click="setStairs(change, '')">Corner</button>
      </div>
    </template>

    <h5>Ways in <span class="dim">cells of its edge (dx,dy from its top-left). Right-click on its edge ▸ Entry point here.</span></h5>
    <p v-if="!entries.length" class="dim small">None set: where its doors are (else its door side's middle).</p>
    <div v-for="(cell, i) in entries" :key="cell" class="row">
      <span class="label">{{ cell }}</span>
      <button class="small" :disabled="disabled" title="No longer a way in" @click="removeEntry(i)">✕</button>
    </div>
    <button v-if="entries.length > 1" class="small" :disabled="disabled" @click="emit('set', 'entries', '')">Clear all</button>
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
