<script setup lang="ts">
// Abilities and magic (development_plan.md 19.4, docs/systems/magic.md): every ability with
// its figures (the formula's cost against its own, output per cost and per second,
// outliers flagged) and problems, the magic effects, Try (play, learn it and cast it),
// and Customise (an engine preset as the project's record, to change in Records).
import { computed, onMounted, ref } from 'vue'
import { onHostMessage, sendToHost } from '../ipc'

interface AbilityRow {
  id: string
  name: string
  source: string
  kind: string
  school: string
  delivery: string
  activation: string
  cost: number
  costAttribute: string
  cooldown: number
  effects: string[]
  formulaCost: number
  output: number
  outputPerCost: number
  outputPerSecond: number
  outlier: boolean
  ai: string
  problems: string[]
}
interface EffectRow {
  id: string
  source: string
  archetype: string
  magnitude: string
  duration: number
  tags: string[]
}

const abilities = ref<AbilityRow[]>([])
const effects = ref<EffectRow[]>([])
const section = ref<'abilities' | 'effects'>('abilities')
const filter = ref('')
const selected = ref('')

onHostMessage((message) => {
  if (message.type === 'magic:model') {
    abilities.value = message.abilities as AbilityRow[]
    effects.value = message.effects as EffectRow[]
  }
})
onMounted(() => sendToHost({ type: 'magic:request' }))

const shown = computed(() =>
  abilities.value.filter((a) => !filter.value || `${a.id} ${a.name} ${a.school}`.toLowerCase().includes(filter.value.toLowerCase())),
)
const current = computed(() => abilities.value.find((a) => a.id === selected.value))

function fmt(n: number): string {
  return Number.isInteger(n) ? String(n) : n.toFixed(1)
}
</script>

<template>
  <div class="magic ed-form">
    <div class="topbar">
      <div class="view-switch">
        <button :class="{ on: section === 'abilities' }" @click="section = 'abilities'">Abilities</button>
        <button :class="{ on: section === 'effects' }" @click="section = 'effects'">Magic effects</button>
      </div>
      <input v-model="filter" placeholder="filter" spellcheck="false" class="filter" />
      <span class="spacer"></span>
      <button @click="sendToHost({ type: 'magic:request' })">Refresh</button>
    </div>

    <section v-if="section === 'abilities'" class="editor">
      <table class="grid">
        <thead>
          <tr>
            <th>ability</th><th>kind</th><th>delivery</th><th>cost</th><th title="What the formula suggests">formula</th>
            <th title="Damage or healing per point of cost">per cost</th><th title="Damage or healing per second">per s</th><th></th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="a in shown" :key="a.id" :class="{ on: selected === a.id }" @click="selected = a.id">
            <td>
              {{ a.name }} <span class="badge" :class="a.source">{{ a.source }}</span>
              <span v-if="a.problems.length" class="warn" :title="a.problems.join('\n')">⚠</span>
            </td>
            <td>{{ a.kind }}{{ a.school ? ` · ${a.school}` : '' }}</td>
            <td>{{ a.delivery }}</td>
            <td>{{ fmt(a.cost) }} {{ a.costAttribute }}</td>
            <td :class="{ warn: a.outlier }" :title="a.outlier ? 'Far from the formula: check it' : ''">{{ fmt(a.formulaCost) }}</td>
            <td>{{ fmt(a.outputPerCost) }}</td>
            <td>{{ fmt(a.outputPerSecond) }}</td>
            <td class="actions">
              <button title="Play, learn it and cast it at the nearest enemy (or ahead)" @click.stop="sendToHost({ type: 'magic:try', id: a.id })">Try</button>
              <button v-if="a.source === 'engine'" title="Copy into the project's records to change it" @click.stop="sendToHost({ type: 'magic:customise', kind: 'ability', id: a.id })">Customise</button>
            </td>
          </tr>
        </tbody>
      </table>
      <div v-if="current" class="detail">
        <div><b>{{ current.name }}</b> <span class="muted small">({{ current.id }}) {{ current.activation }}, cooldown {{ fmt(current.cooldown) }}s</span></div>
        <div class="muted small">Effects: {{ current.effects.join(', ') }}</div>
        <div v-if="current.ai" class="muted small">AI: {{ current.ai }}</div>
        <ul v-if="current.problems.length" class="problems"><li v-for="p in current.problems" :key="p">{{ p }}</li></ul>
        <p class="muted small">Give a character an <b>Abilities</b> component (known: "{{ current.id }}"), or teach it with a
          book: an item whose use is {"learn": "{{ current.id }}"}. Edit the project's in Records ▸ ability.</p>
      </div>
    </section>

    <section v-else class="editor">
      <table class="grid">
        <thead><tr><th>effect</th><th>archetype</th><th>magnitude</th><th>duration</th><th>tags</th><th></th></tr></thead>
        <tbody>
          <tr v-for="e in effects.filter((x) => !filter || x.id.includes(filter))" :key="e.id">
            <td>{{ e.id }} <span class="badge" :class="e.source">{{ e.source }}</span></td>
            <td>{{ e.archetype }}</td>
            <td><code>{{ e.magnitude }}</code></td>
            <td>{{ e.duration ? fmt(e.duration) + 's' : 'instant' }}</td>
            <td class="muted small">{{ e.tags.join(', ') }}</td>
            <td>
              <button v-if="e.source === 'engine'" @click="sendToHost({ type: 'magic:customise', kind: 'magic-effect', id: e.id })">Customise</button>
            </td>
          </tr>
        </tbody>
      </table>
    </section>
  </div>
</template>

<style scoped>
.magic {
  display: flex;
  flex-direction: column;
  height: 100%;
  min-height: 0;
}
.topbar {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 6px 8px;
  border-bottom: 1px solid #2a2a3a;
}
.view-switch {
  display: flex;
}
.view-switch button {
  padding: 2px 10px;
}
.view-switch button.on {
  background: #313148;
  border-color: #3b82f6;
}
.filter {
  width: 160px;
}
.editor {
  flex: 1;
  min-width: 0;
  overflow: auto;
  padding: 8px 10px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.grid {
  border-collapse: collapse;
  font-size: 0.9em;
}
.grid th,
.grid td {
  border: 1px solid #2a2a3a;
  padding: 2px 6px;
  text-align: left;
  white-space: nowrap;
}
.grid tr.on td {
  background: #262637;
}
.grid tbody tr {
  cursor: pointer;
}
.actions {
  display: flex;
  gap: 4px;
}
.badge {
  font-size: 0.75em;
  padding: 0 5px;
  border: 1px solid #3a3a50;
}
.badge.project {
  border-color: #fbbf24;
  color: #fbbf24;
}
.warn {
  color: #f59e0b;
}
.detail {
  border: 1px solid #2a2a3a;
  padding: 8px;
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.problems {
  margin: 0;
  padding-left: 18px;
  color: #f59e0b;
  font-size: 0.9em;
}
.spacer {
  flex: 1;
}
.muted {
  opacity: 0.6;
}
.small {
  font-size: 0.8em;
}
</style>
