<script setup lang="ts">
// ⑧ Visuals (the design: the interface's one look, and each panel — HUD, inventory, journal, map... — its layout,
// components, look, images and RCSS) and Build ▸ Visuals (each panel made: its images drawn, its document written as
// designed). docs: data/help/ai/planner.md, docs/systems/game-ui.md §17.
import { computed, ref } from 'vue'

interface Component {
  id?: string
  kind?: string
  shows?: string
  does?: string
  look?: string
}
interface Image {
  id?: string
  use?: string
  prompt?: string
  width?: number
  height?: number
  nine?: boolean
}
interface Panel {
  id?: string
  name?: string
  layer?: string
  opens?: string
  purpose?: string
  layout?: string
  components?: Component[]
  look?: string
  images?: Image[]
  css?: string
}
interface Built {
  status?: string
  images?: Record<string, string>
  file?: string
  notes?: string
  error?: string
  doing?: string
}
const props = defineProps<{
  ui?: { theme?: string; font?: string; style?: Record<string, unknown> & { palette?: Record<string, string> }; panels?: Panel[] } | null
  built?: { panels?: Record<string, Built> } | null
  mode: 'design' | 'build'
}>()
const emit = defineEmits<{ (e: 'redesign'): void; (e: 'build', panel: string): void }>()

const panels = computed(() => (props.ui?.panels ?? []).filter((p): p is Panel => !!p && !!p.id))
const open = ref<Record<string, boolean>>({})
function toggle(id: string): void {
  open.value = { ...open.value, [id]: !open.value[id] }
}
const builtOf = (id?: string) => (id ? props.built?.panels?.[id] : undefined)
const builtCount = computed(() => panels.value.filter((p) => builtOf(p.id)?.status === 'built').length)
const styleRows = computed(() => Object.entries(props.ui?.style ?? {}).filter(([k, v]) => k !== 'palette' && typeof v === 'string' && v))
const busy = computed(() => panels.value.some((p) => ['images', 'writing'].includes(builtOf(p.id)?.status ?? '')))
// A project file as the editor serves it.
function imageUrl(file?: string): string {
  return file ? `https://aige-project.local/${file}` : ''
}
</script>

<template>
  <div class="visuals">
    <div class="bar">
      <template v-if="mode === 'design'">
        <span class="muted">{{ panels.length ? `${panels.length} panels designed` : 'Not designed yet.' }}</span>
        <button title="Design the interface again: its look and every panel" @click="emit('redesign')">Redesign Visuals</button>
      </template>
      <template v-else>
        <span class="muted">{{ panels.length ? `${builtCount}/${panels.length} panels built` : '⑧ Visuals has no panels yet: Redesign Visuals there.' }}</span>
        <span class="spacer" />
        <button title="Design the interface again (⑧)" @click="emit('redesign')">Redesign Visuals</button>
        <button class="primary" :disabled="!panels.length || busy" title="Draw every panel's images and write its document" @click="emit('build', 'all')">Build all</button>
      </template>
    </div>

    <div v-if="mode === 'design' && ui?.style" class="style">
      <div v-if="ui.style.palette" class="palette">
        <span v-for="(c, k) in ui.style.palette" :key="k" class="swatch" :title="`${k} ${c}`"><i :style="{ background: c }" />{{ k }}</span>
      </div>
      <div v-for="[k, v] in styleRows" :key="k" class="row"><b>{{ k }}</b> {{ v }}</div>
      <div class="row"><b>theme</b> {{ ui.theme }} <b>font</b> {{ ui.font }}</div>
    </div>

    <div v-for="p in panels" :key="p.id" class="panel">
      <div class="head" @click="toggle(p.id!)">
        <span class="caret">{{ open[p.id!] ? '▾' : '▸' }}</span>
        <b>{{ p.name || p.id }}</b> <span class="mono dim">{{ p.id }}</span>
        <span class="tag">{{ p.layer }}</span>
        <span v-if="p.opens" class="tag" title="The input action that opens it">{{ p.opens }}</span>
        <span class="dim small">{{ (p.components ?? []).length }} components · {{ (p.images ?? []).length }} images</span>
        <span class="spacer" />
        <template v-if="mode === 'build'">
          <span class="status" :class="builtOf(p.id)?.status ?? 'todo'" :title="builtOf(p.id)?.error ?? ''">
            {{ builtOf(p.id)?.status ?? 'to build' }}{{ builtOf(p.id)?.doing ? ' · ' + builtOf(p.id)?.doing : '' }}</span>
          <button class="mini" :disabled="['images', 'writing'].includes(builtOf(p.id)?.status ?? '')" @click.stop="emit('build', p.id!)">
            {{ builtOf(p.id)?.status === 'built' ? 'Build again' : 'Build' }}</button>
        </template>
      </div>
      <div v-if="open[p.id!]" class="body">
        <p v-if="p.purpose"><b>Purpose</b> {{ p.purpose }}</p>
        <p v-if="p.layout"><b>Layout</b> {{ p.layout }}</p>
        <p v-if="p.look"><b>Look</b> {{ p.look }}</p>
        <table v-if="(p.components ?? []).length" class="grid">
          <thead><tr><th>Component</th><th>Kind</th><th>Shows</th><th>Does</th><th>Look</th></tr></thead>
          <tbody>
            <tr v-for="c in p.components" :key="c.id"><td class="mono">{{ c.id }}</td><td>{{ c.kind }}</td><td>{{ c.shows }}</td><td>{{ c.does }}</td><td>{{ c.look }}</td></tr>
          </tbody>
        </table>
        <div v-if="(p.images ?? []).length" class="images">
          <div v-for="im in p.images" :key="im.id" class="image">
            <img v-if="mode === 'build' && builtOf(p.id)?.images?.[im.id!]" :src="imageUrl(builtOf(p.id)?.images?.[im.id!])" />
            <div><b class="mono">{{ im.id }}</b> <span class="tag">{{ im.use }}</span> <span class="dim small">{{ im.width }}×{{ im.height }}{{ im.nine ? ' · nine-piece' : '' }}</span></div>
            <div class="dim small">{{ im.prompt }}</div>
          </div>
        </div>
        <details v-if="p.css"><summary>RCSS</summary><pre>{{ p.css }}</pre></details>
        <template v-if="mode === 'build' && builtOf(p.id)">
          <p v-if="builtOf(p.id)?.file" class="small"><b>Document</b> <span class="mono">{{ builtOf(p.id)?.file }}</span></p>
          <p v-if="builtOf(p.id)?.notes" class="small dim">{{ builtOf(p.id)?.notes }}</p>
          <p v-if="builtOf(p.id)?.error" class="small err">{{ builtOf(p.id)?.error }}</p>
        </template>
      </div>
    </div>
  </div>
</template>

<style scoped>
.visuals { display: flex; flex-direction: column; gap: 8px; }
.bar { display: flex; align-items: center; gap: 8px; }
.spacer { flex: 1; }
.style { border: 1px solid #2a2a3a; padding: 8px; }
.palette { display: flex; flex-wrap: wrap; gap: 8px; margin-bottom: 6px; }
.swatch { display: inline-flex; align-items: center; gap: 4px; font-size: 0.85em; }
.swatch i { display: inline-block; width: 16px; height: 16px; border: 1px solid #45475a; }
.row { margin: 2px 0; }
.row b { color: #a6adc8; margin-right: 6px; }
.panel { border: 1px solid #2a2a3a; }
.head { display: flex; align-items: center; gap: 8px; padding: 6px 8px; cursor: pointer; user-select: none; }
.head:hover { background: #1e1e2e; }
.caret { width: 12px; color: #8a8aa0; }
.body { padding: 4px 12px 10px; }
.body p { margin: 4px 0; }
.tag { font-size: 0.8em; border: 1px solid #3a3a50; padding: 0 5px; color: #a6adc8; }
.status { font-size: 0.8em; border: 1px solid #45475a; padding: 0 5px; }
.status.built { color: #a6e3a1; border-color: #a6e3a1; }
.status.failed { color: #f38ba8; border-color: #f38ba8; }
.status.images, .status.writing { color: #f9e2af; border-color: #f9e2af; }
.grid { width: 100%; border-collapse: collapse; margin: 6px 0; }
.grid th, .grid td { text-align: left; padding: 3px 6px; border-top: 1px solid #2a2a3a; vertical-align: top; font-size: 0.9em; }
.images { display: flex; flex-direction: column; gap: 6px; margin: 6px 0; }
.image { display: flex; flex-direction: column; gap: 2px; }
.image img { max-width: 160px; max-height: 120px; object-fit: contain; background: #11111b; border: 1px solid #2a2a3a; }
pre { white-space: pre-wrap; font-size: 0.85em; background: #11111b; padding: 6px; }
.mono { font-family: monospace; }
.dim { color: #8a8aa0; }
.muted { color: #8a8aa0; }
.small { font-size: 0.85em; }
.err { color: #f38ba8; }
</style>
