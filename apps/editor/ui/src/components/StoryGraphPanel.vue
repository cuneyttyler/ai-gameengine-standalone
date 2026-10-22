<script setup lang="ts">
// The story knowledge graph (development_plan.md 21.4, docs/story-ontology.md): its nodes by
// class, a node's facts over time, what depends on it, why it exists, the world state of a
// scene, and the story rules' issues. planner/kg.json is written by the Planner and by the
// AI panel's story_graph changes; Sync reads hand-written World Bible rel: lines in and
// writes the graph's end-of-story relations out.
import { computed, onMounted, ref } from 'vue'
import { onHostMessage, sendToHost } from '../ipc'

interface NodeRow {
  id: string
  class: string
  label: string
  layer: string
}
interface Issue {
  rule: string
  severity: string
  message: string
  node: string
}
interface Fact {
  p: string
  s: string
  o: string
  from: string
  until: string
  branch?: string
  fields?: Record<string, unknown>
}
interface Detail {
  id: string
  class: string
  fields: Record<string, unknown>
  timeline: Fact[]
  dependents: string[]
  madeBy: string
  subgraph?: Record<string, unknown>
}

const exists = ref(false)
const revision = ref(0)
const nodes = ref<NodeRow[]>([])
const issues = ref<Issue[]>([])
const detail = ref<Detail | null>(null)
const section = ref<'nodes' | 'issues'>('nodes')
const filter = ref('')
const layer = ref('')
const notice = ref('')

onHostMessage((message) => {
  if (message.type === 'storygraph:model') {
    exists.value = message.exists as boolean
    revision.value = message.revision as number
    nodes.value = message.nodes as NodeRow[]
    issues.value = message.issues as Issue[]
    notice.value = (message.notice as string) || ''
    if (detail.value && !nodes.value.some((n) => n.id === detail.value?.id)) detail.value = null
  } else if (message.type === 'storygraph:detail') {
    detail.value = message as unknown as Detail
  }
})
onMounted(() => sendToHost({ type: 'storygraph:request' }))

const layers = computed(() => [...new Set(nodes.value.map((n) => n.layer))].sort())
const shown = computed(() =>
  nodes.value.filter(
    (n) =>
      (!layer.value || n.layer === layer.value) &&
      (!filter.value || `${n.id} ${n.label} ${n.class}`.toLowerCase().includes(filter.value.toLowerCase())),
  ),
)
const errors = computed(() => issues.value.filter((i) => i.severity === 'error').length)
const nodeIssues = computed(() => issues.value.filter((i) => detail.value && i.node === detail.value.id))
const fieldRows = computed(() =>
  Object.entries(detail.value?.fields ?? {}).map(([k, v]) => [k, typeof v === 'string' ? v : JSON.stringify(v)]),
)

function open(id: string) {
  sendToHost({ type: 'storygraph:node', id })
}
function factText(f: Fact): string {
  const extra = f.fields ? Object.entries(f.fields).map(([k, v]) => `${k}=${typeof v === 'string' ? v : JSON.stringify(v)}`).join(' ') : ''
  return `${f.s} —${f.p}→ ${f.o || ''} ${extra}`.trim()
}
function when(f: Fact): string {
  if (!f.from && !f.until) return 'always'
  return `${f.from || 'start'} … ${f.until ? 'before ' + f.until : 'end'}${f.branch ? ' (' + f.branch + ')' : ''}`
}
</script>

<template>
  <div class="storygraph ed-form">
    <div class="topbar">
      <div class="view-switch">
        <button :class="{ on: section === 'nodes' }" @click="section = 'nodes'">Nodes</button>
        <button :class="{ on: section === 'issues' }" @click="section = 'issues'">
          Issues<span v-if="issues.length"> ({{ errors }} errors, {{ issues.length - errors }} warnings)</span>
        </button>
      </div>
      <input v-model="filter" placeholder="filter" spellcheck="false" class="filter" />
      <select v-model="layer">
        <option value="">all layers</option>
        <option v-for="l in layers" :key="l" :value="l">{{ l }}</option>
      </select>
      <span class="spacer"></span>
      <span class="muted">revision {{ revision }}</span>
      <button title="Hand-written rel: lines in, the graph's relations out" @click="sendToHost({ type: 'storygraph:sync' })">
        Sync World Bible
      </button>
      <button @click="sendToHost({ type: 'storygraph:request' })">Refresh</button>
    </div>
    <div v-if="notice" class="notice">{{ notice }}</div>

    <div v-if="!exists" class="empty">
      No story graph yet. The AI Planner writes <code>planner/kg.json</code> as it designs the story; you can also ask the
      AI panel ("add Tamsin and Aldric to the story graph, Tamsin trusts Aldric until scene 4").
    </div>

    <div v-else-if="section === 'nodes'" class="split">
      <div class="list">
        <table class="grid">
          <thead>
            <tr><th>node</th><th>class</th></tr>
          </thead>
          <tbody>
            <tr v-for="n in shown" :key="n.id" :class="{ on: detail?.id === n.id }" @click="open(n.id)">
              <td>{{ n.label }}<span v-if="n.label !== n.id" class="muted"> {{ n.id }}</span></td>
              <td>{{ n.class }}</td>
            </tr>
          </tbody>
        </table>
      </div>
      <div v-if="detail" class="detail">
        <h3>{{ detail.id }} <span class="badge">{{ detail.class }}</span></h3>
        <div v-if="detail.madeBy" class="muted">made by <a href="#" @click.prevent="open(detail.madeBy)">{{ detail.madeBy }}</a></div>
        <ul v-if="nodeIssues.length" class="problems">
          <li v-for="(i, k) in nodeIssues" :key="k">{{ i.rule }}: {{ i.message }}</li>
        </ul>
        <table v-if="fieldRows.length" class="grid">
          <tbody>
            <tr v-for="[k, v] in fieldRows" :key="k"><td class="muted">{{ k }}</td><td>{{ v }}</td></tr>
          </tbody>
        </table>
        <h4>Facts over time</h4>
        <table class="grid">
          <tbody>
            <tr v-for="(f, k) in detail.timeline" :key="k">
              <td class="when">{{ when(f) }}</td>
              <td>
                <a href="#" @click.prevent="open(f.s === detail.id ? f.o : f.s)">{{ factText(f) }}</a>
              </td>
            </tr>
          </tbody>
        </table>
        <h4>Depends on it ({{ detail.dependents.length }})</h4>
        <div class="chips">
          <a v-for="d in detail.dependents" :key="d" href="#" class="chip" @click.prevent="open(d)">{{ d }}</a>
        </div>
        <template v-if="detail.subgraph">
          <h4>World state here</h4>
          <pre class="state">{{ JSON.stringify(detail.subgraph, null, 2) }}</pre>
        </template>
      </div>
    </div>

    <div v-else class="editor">
      <div v-if="!issues.length" class="muted">Every rule holds.</div>
      <table v-else class="grid">
        <thead>
          <tr><th>rule</th><th></th><th>issue</th></tr>
        </thead>
        <tbody>
          <tr v-for="(i, k) in issues" :key="k" @click="i.node && (section = 'nodes', open(i.node))">
            <td>{{ i.rule }}</td>
            <td :class="i.severity === 'error' ? 'err' : 'warn'">{{ i.severity }}</td>
            <td>{{ i.message }}</td>
          </tr>
        </tbody>
      </table>
    </div>
  </div>
</template>

<style scoped>
.storygraph {
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
.spacer {
  flex: 1;
}
.muted {
  color: #8a8aa0;
}
.notice {
  padding: 4px 10px;
  color: #93c5fd;
  border-bottom: 1px solid #2a2a3a;
}
.empty {
  padding: 16px;
  color: #8a8aa0;
}
.split {
  flex: 1;
  min-height: 0;
  display: flex;
}
.list {
  width: 320px;
  overflow: auto;
  border-right: 1px solid #2a2a3a;
}
.detail,
.editor {
  flex: 1;
  min-width: 0;
  overflow: auto;
  padding: 8px 10px;
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.detail h3,
.detail h4 {
  margin: 4px 0 0;
}
.grid {
  border-collapse: collapse;
  font-size: 0.9em;
  width: 100%;
}
.grid th,
.grid td {
  border: 1px solid #2a2a3a;
  padding: 2px 6px;
  text-align: left;
}
.grid tr.on td {
  background: #262637;
}
.grid tbody tr {
  cursor: pointer;
}
.when {
  white-space: nowrap;
  color: #8a8aa0;
}
.badge {
  font-size: 0.75em;
  padding: 0 5px;
  border: 1px solid #3a3a50;
}
.chips {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}
.chip {
  border: 1px solid #3a3a50;
  padding: 0 6px;
  font-size: 0.85em;
}
.problems {
  margin: 0;
  padding-left: 18px;
  color: #f59e0b;
  font-size: 0.9em;
}
.state {
  margin: 0;
  padding: 6px;
  border: 1px solid #2a2a3a;
  font-size: 0.85em;
  max-height: 320px;
  overflow: auto;
}
.err {
  color: #f87171;
}
.warn {
  color: #f59e0b;
}
a {
  color: #93c5fd;
  text-decoration: none;
}
</style>
