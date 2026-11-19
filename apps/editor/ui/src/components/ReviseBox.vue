<script setup lang="ts">
// Revise, at the bottom of each AI Planner step and each Build tab: a change in your words, made by the AI as an agent
// would: it first finds which of the step's documents (one character, one quest, one placement) and which of their
// fields the words are about, then edits only those. Each step keeps its own history. docs: data/help/ai/planner.md.
import { computed, ref } from 'vue'

interface Edit {
  doc?: string
  pointer?: string
  reason?: string
}
interface Target {
  doc?: string
  fields?: string[]
  why?: string
}
export interface Revision {
  id?: string
  at?: number
  text?: string
  status?: string
  targets?: Target[]
  edits?: Edit[]
  rationale?: string
  error?: string
  made?: number
  // For chained revise: decomposed tasks
  tasks?: string[]
  task_results?: { task: string; ok: boolean; edits?: Edit[]; error?: string | null }[]
  // UI state (not from the host)
  _show_tasks?: boolean
  _show_task_result?: number
}
const props = defineProps<{ step: string; label: string; entries: Revision[] }>()
const emit = defineEmits<{ (e: 'revise', text: string): void; (e: 'revert', id: string): void; (e: 'remove', id: string): void }>()

const text = ref('')
const open = ref(true)
const busy = computed(() => props.entries.some((e) => e.status === 'working' || e.status === 'queued' || e.status === 'running'))
const shown = computed(() => [...props.entries].reverse())
function send(): void {
  const t = text.value.trim()
  if (!t) return
  emit('revise', t)
  text.value = ''
}
function when(at?: number): string {
  return at ? new Date(at * 1000).toLocaleString() : ''
}
// The one decomposed task's result the user opened, if any.
function selectedResult(r: Revision): { task: string; ok: boolean; edits?: Edit[]; error?: string | null } | undefined {
  const i = r._show_task_result
  return i != null && i >= 0 && r.task_results ? r.task_results[i] : undefined
}
function toggleResult(r: Revision, i: number): void {
  r._show_task_result = r._show_task_result === i ? -1 : i
}
function toggleTasks(r: Revision): void {
  r._show_tasks = !r._show_tasks
}
</script>

<template>
  <div class="revise">
    <div class="head" @click="open = !open">
      <span class="caret">{{ open ? '▾' : '▸' }}</span> Revise {{ label }}
      <span v-if="entries.length" class="count">{{ entries.length }}</span>
      <span v-if="busy" class="busy">working…</span>
    </div>
    <template v-if="open">
      <div class="ask">
        <textarea v-model="text" rows="2" :placeholder="`What to change in ${label}, in your words (only what you name is changed)`"
                  @keydown.ctrl.enter.prevent="send" />
        <button class="primary" :disabled="!text.trim()" title="Ctrl+Enter" @click="send">Revise</button>
      </div>
      <div v-for="r in shown" :key="r.id" class="entry">
        <div class="line">
          <span class="status" :class="r.status">{{ r.status }}</span>
          <span class="words">{{ r.text }}</span>
          <span class="dim small">{{ when(r.at) }}</span>
          <button v-if="r.status === 'done' && (r.made ?? 0) > 0 && r.id" class="mini" title="Take back every edit it made (a value put back as it was, an item it added taken out)"
                  @click="emit('revert', r.id!)">Revert</button>
          <button v-if="r.id" class="mini danger" title="Remove this revision from history" @click="emit('remove', r.id!)">Remove</button>
        </div>
        <div v-if="r.targets?.length" class="dim small">
          About: <span v-for="(t, i) in r.targets" :key="i" :title="t.why ?? ''">{{ t.doc }}<template v-if="t.fields?.length"> ({{ t.fields.join(', ') }})</template>{{ i + 1 < r.targets.length ? '; ' : '' }}</span>
        </div>
        <ul v-if="r.edits?.length" class="edits">
          <li v-for="(e, i) in r.edits" :key="i"><span class="mono">{{ e.doc }}{{ e.pointer }}</span><span v-if="e.reason" class="dim"> — {{ e.reason }}</span></li>
        </ul>
        <div v-else-if="r.status === 'done'" class="dim small">Nothing to change: {{ r.rationale }}</div>
        <div v-if="r.error" class="err small">{{ r.error }}</div>
        <!-- Decomposed tasks display -->
        <div v-if="r.tasks && r.tasks.length" class="decomposed">
          <div class="decomposed-header" @click="toggleTasks(r)">
            <span class="caret">{{ r._show_tasks ? '▾' : '▸' }}</span>
            <span>Decomposed into {{ r.tasks.length }} tasks</span>
            <span class="dim small">Click to expand</span>
          </div>
          <div v-if="r._show_tasks" class="decomposed-tasks">
            <div v-for="(task, i) in r.tasks" :key="i" class="decomposed-task">
              <span class="task-index">{{ i + 1 }}.</span>
              <span class="task-text">{{ task }}</span>
              <button v-if="r.task_results && r.task_results[i]" class="mini" @click="toggleResult(r, i)">
                {{ r._show_task_result === i ? 'Hide' : 'Show' }} result
              </button>
            </div>
            <div v-if="selectedResult(r)" class="task-result">
              <div class="result-header">
                <span class="status" :class="selectedResult(r)!.ok ? 'done' : 'failed'">
                  {{ selectedResult(r)!.ok ? '✓' : '✗' }}
                </span>
                <span>{{ selectedResult(r)!.task }}</span>
              </div>
              <ul v-if="selectedResult(r)!.edits && selectedResult(r)!.edits!.length" class="result-edits">
                <li v-for="(e, i) in selectedResult(r)!.edits" :key="i" class="mono small">
                  {{ e.doc }}{{ e.pointer }} <span v-if="e.reason" class="dim">— {{ e.reason }}</span>
                </li>
              </ul>
              <div v-if="selectedResult(r)!.error" class="err small">{{ selectedResult(r)!.error }}</div>
            </div>
          </div>
        </div>
      </div>
    </template>
  </div>
</template>

<style scoped>
.revise {
  margin-top: 18px;
  border-top: 1px solid #2a2a3a;
  padding-top: 8px;
}
.head {
  cursor: pointer;
  user-select: none;
  font-weight: 600;
  display: flex;
  align-items: center;
  gap: 6px;
}
.caret {
  width: 12px;
  color: #8a8aa0;
}
.count {
  font-size: 0.75em;
  color: #8a8aa0;
  border: 1px solid #3a3a50;
  padding: 0 5px;
}
.busy {
  color: #f9e2af;
  font-size: 0.85em;
}
.ask {
  display: flex;
  gap: 6px;
  margin: 8px 0;
  align-items: stretch;
}
.ask textarea {
  flex: 1;
  resize: vertical;
}
.entry {
  border-left: 2px solid #313244;
  padding: 4px 8px;
  margin: 6px 0;
}
.line {
  display: flex;
  gap: 8px;
  align-items: baseline;
}
.words {
  flex: 1;
}
.status {
  font-size: 0.8em;
  border: 1px solid #45475a;
  padding: 0 5px;
}
.status.reverted {
  color: #8a8aa0;
  text-decoration: line-through;
}
.status.done {
  color: #a6e3a1;
  border-color: #a6e3a1;
}
.status.failed {
  color: #f38ba8;
  border-color: #f38ba8;
}
.status.working,
.status.queued,
.status.running {
  color: #f9e2af;
  border-color: #f9e2af;
}
.edits {
  margin: 4px 0 0 16px;
  padding: 0;
}
.mono {
  font-family: monospace;
}
.dim {
  color: #8a8aa0;
}
.small {
  font-size: 0.85em;
}
.err {
  color: #f38ba8;
}
/* Decomposed tasks styles */
.decomposed {
  margin-top: 8px;
  padding: 8px;
  background: #181825;
  border: 1px solid #313244;
  border-radius: 4px;
}
.decomposed-header {
  cursor: pointer;
  display: flex;
  align-items: center;
  gap: 6px;
  font-weight: 500;
}
.decomposed-header .caret {
  width: 12px;
  color: #8a8aa0;
}
.decomposed-tasks {
  margin-top: 8px;
  padding-left: 16px;
  border-left: 2px solid #313244;
}
.decomposed-task {
  display: flex;
  align-items: center;
  gap: 8px;
  margin: 4px 0;
}
.task-index {
  color: #8a8aa0;
  font-size: 0.85em;
  min-width: 1.5em;
}
.task-text {
  flex: 1;
}
.task-result {
  margin-top: 4px;
  padding: 4px 8px;
  background: #1e1e2e;
  border-radius: 4px;
}
.result-header {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 4px;
}
.result-edits {
  margin-left: 16px;
}
.result-edits li {
  margin: 2px 0;
}
</style>
