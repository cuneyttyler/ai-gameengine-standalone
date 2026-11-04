<script setup lang="ts">
// A path characters follow, edited as what it is (docs/ai/ai-paths.md): its name (renamed with the packages that use
// it), its end, its points (what's done at each) and its links (forks: weight, conditions, priority, default). Written
// back as its fields (points, links: the JSON the engine reads) through the Inspector's own setField.
import { computed, ref, watch } from 'vue'
import type { FieldData } from './FieldEditor.vue'
import { editorState } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'

const props = defineProps<{ fields: FieldData[]; disabled?: boolean; id?: number }>()
const emit = defineEmits<{ (e: 'set', field: string, value: string | number | boolean): void }>()

interface Point { x: number; y: number; wait?: number; clip?: string; use?: string; do?: string; label?: string }
interface Link { from: number; to: number; weight?: number; if?: string; priority?: boolean; default?: boolean; do?: string; label?: string }

const value = (name: string): string => String(props.fields.find((f) => f.name === name)?.value ?? '')
const parse = <T,>(text: string): T[] => {
  try {
    const v = JSON.parse(text || '[]')
    return Array.isArray(v) ? (v as T[]) : []
  } catch {
    return []
  }
}
const points = computed(() => parse<Point>(value('points')))
const end = computed(() => value('end') || 'stop')
// Its links as listed, or point to point in order (and round, for a loop) when none are.
const links = computed<Link[]>(() => {
  const listed = parse<Link>(value('links'))
  if (listed.length) return listed
  const n = points.value.length
  const inOrder: Link[] = []
  for (let i = 0; i + 1 < n; i++) inOrder.push({ from: i, to: i + 1 })
  if (end.value === 'loop' && n > 2) inOrder.push({ from: n - 1, to: 0 })
  return inOrder
})
const forks = computed(() => {
  const out = new Map<number, number>()
  for (const l of links.value) out.set(l.from, (out.get(l.from) ?? 0) + 1)
  return out
})
const name = ref(value('name'))
watch(() => value('name'), (v) => (name.value = v))

const info = ref<{ packages: string[]; characters: { id: number; name: string }[]; problems: string[] } | null>(null)
function refreshInfo(): void {
  if (props.id !== undefined) sendToHost({ type: 'path:info', id: props.id })
}
watch(() => [props.id, value('points'), value('links'), value('name')], refreshInfo, { immediate: true })
onHostMessage((message) => {
  if (message.type === 'path:info' && message.id === props.id) info.value = message as unknown as typeof info.value
})

function rename(): void {
  const next = name.value.trim().toLowerCase().replace(/[^a-z0-9_-]+/g, '-')
  if (props.id !== undefined && next && next !== value('name')) sendToHost({ type: 'path:rename', id: props.id, name: next })
}
function setPoint(i: number, key: keyof Point, v: string): void {
  const next = points.value.map((p) => ({ ...p }))
  const p = next[i]!
  if (key === 'wait') p.wait = Number(v) || 0
  else if (key === 'x' || key === 'y') p[key] = Number(v) || 0
  else if (v) (p as Record<string, unknown>)[key] = v
  else delete (p as Record<string, unknown>)[key]
  emit('set', 'points', JSON.stringify(next))
}
function setLink(i: number, key: keyof Link, v: string | number | boolean): void {
  const next = links.value.map((l) => ({ ...l }))
  const l = next[i]! as Record<string, unknown>
  if (v === '' || v === false) delete l[key]
  else l[key] = v
  emit('set', 'links', JSON.stringify(next))
}
function removeLink(i: number): void {
  emit('set', 'links', JSON.stringify(links.value.filter((_, k) => k !== i)))
}
function conditionsOk(text?: string): boolean {
  if (!text) return true
  try {
    return Array.isArray(JSON.parse(text))
  } catch {
    return false
  }
}
</script>

<template>
  <div class="path-editor">
    <div class="row">
      <label>Name</label>
      <input v-model="name" :disabled="disabled" spellcheck="false" title="Packages use it by this name; renaming it renames it in them" @change="rename" />
    </div>
    <div class="row">
      <label>At an end</label>
      <select :value="end" :disabled="disabled" @change="emit('set', 'end', ($event.target as HTMLSelectElement).value)">
        <option value="stop">stop</option>
        <option value="loop">loop (back to the first point)</option>
        <option value="back">back (the way it came)</option>
      </select>
      <button class="small" :disabled="disabled || id === undefined" title="Draw on with the Path tool: click a point to branch from it"
              @click="sendToHost({ type: 'path:edit', id, point: points.length - 1 })">Draw on…</button>
    </div>
    <p v-if="info" class="dim small">
      Used by {{ info.packages.length ? info.packages.join(', ') : 'no package yet' }}{{ info.characters.length ? ` · ${info.characters.map((c) => c.name).join(', ')}` : '' }}
    </p>
    <p v-for="p in info?.problems ?? []" :key="p" class="error small">{{ p }}</p>

    <h5>Points</h5>
    <div v-for="(p, i) in points" :key="`p${i}`" class="point">
      <span class="num" :title="forks.get(i)! > 1 ? 'A fork: its links below choose the way' : ''">{{ i + 1 }}{{ (forks.get(i) ?? 0) > 1 ? '⑂' : '' }}</span>
      <input class="label" :value="p.label ?? ''" placeholder="label" :disabled="disabled" @change="setPoint(i, 'label', ($event.target as HTMLInputElement).value)" />
      <input class="secs" type="number" min="0" step="0.5" :value="p.wait ?? 0" title="Seconds a character waits here" :disabled="disabled"
             @change="setPoint(i, 'wait', ($event.target as HTMLInputElement).value)" />
      <input class="word" :value="p.clip ?? ''" placeholder="clip" title="A clip played while it waits here" :disabled="disabled"
             @change="setPoint(i, 'clip', ($event.target as HTMLInputElement).value)" />
      <input class="word" :value="p.use ?? ''" placeholder="use part" title="A part used here, by its name (cook)" :disabled="disabled"
             @change="setPoint(i, 'use', ($event.target as HTMLInputElement).value)" />
    </div>

    <h5>Links <span class="dim small">(more than one from a point: a fork)</span></h5>
    <div v-for="(l, i) in links" :key="`l${i}`" class="link" :class="{ fork: (forks.get(l.from) ?? 0) > 1 }">
      <span class="num">{{ l.from + 1 }} → {{ l.to + 1 }}</span>
      <template v-if="(forks.get(l.from) ?? 0) > 1">
        <input class="secs" type="number" min="0" step="0.5" :value="l.weight ?? 1" title="Its weight: how often it's chosen against the others"
               :disabled="disabled" @change="setLink(i, 'weight', Number(($event.target as HTMLInputElement).value) || 0)" />
        <label class="tick" title="Taken whenever its conditions hold"><input type="checkbox" :checked="!!l.priority" :disabled="disabled"
               @change="setLink(i, 'priority', ($event.target as HTMLInputElement).checked)" />first</label>
        <label class="tick" title="Taken when no other's conditions hold"><input type="checkbox" :checked="!!l.default" :disabled="disabled"
               @change="setLink(i, 'default', ($event.target as HTMLInputElement).checked)" />default</label>
        <input class="label" :value="l.label ?? ''" placeholder="label" title="A package can prefer it by this name (weights)" :disabled="disabled"
               @change="setLink(i, 'label', ($event.target as HTMLInputElement).value)" />
      </template>
      <button class="small" :disabled="disabled" title="Remove this link" @click="removeLink(i)">✕</button>
      <input v-if="(forks.get(l.from) ?? 0) > 1" class="cond" :class="{ bad: !conditionsOk(l.if) }" :value="l.if ?? ''" spellcheck="false"
             placeholder='conditions: [{"flag": "market_day"}, {"time": {"from": "18:00", "to": "23:00"}}, {"playerNear": 5, "not": true}, {"free": true}]'
             :disabled="disabled" @change="setLink(i, 'if', ($event.target as HTMLInputElement).value.trim())" />
    </div>
    <p class="dim small">{{ editorState.showPaths ? '' : 'Paths are hidden (View ▸ Paths): only this one shows. ' }}Orange links have conditions, red ones go first, grey ones are the default.</p>
  </div>
</template>

<style scoped>
.path-editor {
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.row,
.point,
.link {
  display: flex;
  gap: 4px;
  align-items: center;
  flex-wrap: wrap;
}
.row label {
  width: 70px;
}
.num {
  min-width: 34px;
  font-variant-numeric: tabular-nums;
}
.label {
  width: 80px;
}
.word {
  width: 70px;
}
.secs {
  width: 50px;
}
.cond {
  flex-basis: 100%;
}
.cond.bad {
  border-color: #e66;
}
.tick {
  display: flex;
  align-items: center;
  gap: 2px;
  font-size: 11px;
}
.link.fork {
  border-left: 2px solid #f9a040;
  padding-left: 4px;
}
.error {
  color: #e66;
  margin: 0;
}
h5 {
  margin: 6px 0 2px;
}
</style>
