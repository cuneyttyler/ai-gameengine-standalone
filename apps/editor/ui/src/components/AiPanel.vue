<script setup lang="ts">
// AI chat + Diff Review (technical.md §7.6). A request becomes a proposal;
// every proposed change is listed with its before/after, can be unticked or
// edited, and nothing touches the project until "Apply". The host validates
// again on apply and runs everything through the same code as manual edits.
import { computed, nextTick, reactive, ref, watch } from 'vue'
import { editorState } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'
import FieldEditor, { type FieldData } from './FieldEditor.vue'

interface ChangeItem {
  index: number
  kind: string
  target: string
  before: unknown
  after: unknown
  field: Partial<FieldData> | null
  detail: string
  touchesFiles: boolean
  error: string
}

interface ProposalEntry {
  kind: 'proposal'
  id: number
  summary: string
  notes: string
  items: ChangeItem[]
  usage: { inputTokens?: number; outputTokens?: number }
  status: 'pending' | 'applied' | 'partial' | 'rejected'
  applyErrors: string[]
  scriptCheck: { scripts: string[]; problems: { path: string; message: string; count: number }[] } | null
  // Review state, per item.
  accepted: Record<number, boolean>
  edited: Record<number, unknown>
  editedSource: Record<number, string>
  expanded: Record<number, boolean>
}

interface MessageEntry {
  kind: 'message'
  role: 'user' | 'error'
  text: string
}

type Entry = ProposalEntry | MessageEntry

const entries = ref<Entry[]>([])
const input = ref('')
const focus = ref('') // "Skeleton.Health.max" when asked from an Inspector field
const listEl = ref<HTMLElement | null>(null)
const inputEl = ref<HTMLTextAreaElement | null>(null)

const canSend = computed(() => editorState.ai.state === 'ready' && !editorState.ai.busy && !editorState.playing)

onHostMessage((message) => {
  if (message.type === 'ai:message') {
    entries.value.push({ kind: 'message', role: message.role as 'user' | 'error', text: message.text as string })
  } else if (message.type === 'ai:proposal') {
    const items = message.items as ChangeItem[]
    const entry: ProposalEntry = reactive({
      kind: 'proposal',
      id: message.id as number,
      summary: message.summary as string,
      notes: message.notes as string,
      items,
      usage: (message.usage as ProposalEntry['usage']) ?? {},
      status: 'pending',
      applyErrors: [],
      scriptCheck: null,
      accepted: Object.fromEntries(items.map((item) => [item.index, !item.error])),
      edited: {},
      editedSource: {},
      expanded: Object.fromEntries(items.map((item) => [item.index, item.kind === 'command'])),
    })
    entries.value.push(entry)
  } else if (message.type === 'ai:decided') {
    const entry = entries.value.find((e) => e.kind === 'proposal' && e.id === message.id) as ProposalEntry | undefined
    if (entry) {
      entry.status = message.status as ProposalEntry['status']
      entry.applyErrors = (message.errors as string[]) ?? []
    }
  } else if (message.type === 'ai:scriptCheck') {
    const entry = entries.value.find((e) => e.kind === 'proposal' && e.id === message.id) as ProposalEntry | undefined
    if (entry) {
      entry.scriptCheck = {
        scripts: message.scripts as string[],
        problems: message.problems as { path: string; message: string; count: number }[],
      }
    }
  } else if (message.type === 'ai:ask') {
    // From an Inspector field's "Ask AI".
    focus.value = message.focus as string
    nextTick(() => inputEl.value?.focus())
  }
})

// "Ask AI" from an Inspector field: focus the request on that field.
watch(
  () => editorState.aiAsk.n,
  () => {
    focus.value = editorState.aiAsk.focus
    nextTick(() => inputEl.value?.focus())
  },
)

watch(
  () => entries.value.length,
  () => nextTick(() => listEl.value?.scrollTo({ top: listEl.value.scrollHeight })),
)

function send(): void {
  const text = input.value.trim()
  if (!text || !canSend.value) {
    return
  }
  sendToHost({ type: 'ai:send', text, focus: focus.value })
  input.value = ''
  focus.value = ''
}

function onKeydown(event: KeyboardEvent): void {
  if (event.key === 'Enter' && !event.shiftKey) {
    event.preventDefault()
    send()
  }
}

function acceptedCount(entry: ProposalEntry): number {
  return entry.items.filter((item) => entry.accepted[item.index] && !item.error).length
}

function apply(entry: ProposalEntry): void {
  const items = entry.items.map((item) => {
    const decision: Record<string, unknown> = { index: item.index, accepted: !!entry.accepted[item.index] }
    if (item.index in entry.edited) decision.value = entry.edited[item.index]
    if (item.index in entry.editedSource) decision.source = entry.editedSource[item.index]
    return decision
  })
  sendToHost({ type: 'ai:apply', id: entry.id, items })
}

// Sends the runtime errors back as a follow-up; the conversation history
// gives the model the script it wrote.
function askToFix(entry: ProposalEntry): void {
  if (!entry.scriptCheck || !canSend.value) {
    return
  }
  const lines = entry.scriptCheck.problems.map((p) => `- ${p.path}: ${p.message}`)
  sendToHost({
    type: 'ai:send',
    text: `The script you wrote fails when the game runs:\n${lines.join('\n')}\nFix it (rewrite the whole script).`,
    focus: '',
  })
}

function reject(entry: ProposalEntry): void {
  sendToHost({ type: 'ai:reject', id: entry.id })
}

// The editable "after" of field-like changes, via the Inspector's editor.
function editableField(item: ChangeItem, entry: ProposalEntry): FieldData | null {
  if (!item.field || !['field', 'prefab_field', 'setting'].includes(item.kind)) {
    return null
  }
  return {
    name: item.field.name ?? '',
    type: (item.field.type as FieldData['type']) ?? 'string',
    description: '',
    options: item.field.options,
    min: item.field.min,
    max: item.field.max,
    value: (item.index in entry.edited ? entry.edited[item.index] : item.after) as FieldData['value'],
  }
}

function show(value: unknown): string {
  if (value === null || value === undefined || value === '') return '—'
  if (typeof value === 'number') return Number.isInteger(value) ? String(value) : value.toFixed(3).replace(/0+$/, '')
  if (typeof value === 'object') return JSON.stringify(value)
  return String(value)
}

const kindIcons: Record<string, string> = {
  field: '✎',
  prefab_field: '◈',
  setting: '⚙',
  add_component: '+',
  remove_component: '−',
  script_property: '✎',
  command: '▸',
}

const examples = [
  'Give the hero a sword that burns enemies for 3 seconds',
  'Make skeletons tougher but slower',
  'Add two more skeletons near the courtyard entrance',
  'Make the background a dark blue night sky',
]
</script>

<template>
  <div class="ai-panel">
    <header class="panel-header">
      <h2>AI</h2>
      <span class="status" :class="editorState.ai.state" :title="editorState.ai.text">
        <span class="dot"></span>{{ editorState.ai.text }}
      </span>
      <button
        v-if="editorState.mode === 'assisted'"
        class="collapse"
        title="Collapse the AI panel"
        @click="editorState.aiOpen = false"
      >
        ›
      </button>
    </header>

    <div ref="listEl" class="entries">
      <div v-if="entries.length === 0" class="empty">
        <p>Describe a change in plain language. You'll review every edit before anything is applied.</p>
        <button v-for="example in examples" :key="example" class="example" @click="input = example">
          {{ example }}
        </button>
      </div>

      <template v-for="(entry, entryIndex) in entries" :key="entryIndex">
        <div v-if="entry.kind === 'message'" class="message" :class="entry.role">{{ entry.text }}</div>

        <div v-else class="proposal" :class="entry.status">
          <div class="summary">{{ entry.summary }}</div>
          <div v-if="entry.notes" class="notes">{{ entry.notes }}</div>
          <p v-if="entry.items.length === 0" class="no-changes">No changes proposed.</p>

          <ul class="items">
            <li v-for="item in entry.items" :key="item.index" :class="{ invalid: !!item.error }">
              <div class="item-row">
                <input
                  type="checkbox"
                  :checked="entry.accepted[item.index] && !item.error"
                  :disabled="!!item.error || entry.status !== 'pending'"
                  :title="item.error ? 'Can\'t be applied' : 'Include this change'"
                  @change="entry.accepted[item.index] = ($event.target as HTMLInputElement).checked"
                />
                <span class="kind" :title="item.kind">{{ kindIcons[item.kind] ?? '•' }}</span>
                <span class="target" :title="item.target">{{ item.target }}</span>
                <span v-if="item.touchesFiles" class="badge" title="Changes a file (prefab, script or settings); not covered by scene undo">
                  file
                </span>
                <button
                  v-if="item.detail"
                  class="expand"
                  :title="entry.expanded[item.index] ? 'Hide details' : 'Show details'"
                  @click="entry.expanded[item.index] = !entry.expanded[item.index]"
                >
                  {{ entry.expanded[item.index] ? '▾' : '▸' }}
                </button>
              </div>

              <div v-if="item.kind !== 'command' && item.kind !== 'remove_component'" class="change">
                <span class="before" :title="'Before: ' + show(item.before)">{{ show(item.before) }}</span>
                <span class="arrow">→</span>
                <FieldEditor
                  v-if="editableField(item, entry) && entry.status === 'pending' && !item.error"
                  class="after-editor"
                  :field="editableField(item, entry)!"
                  @change="entry.edited[item.index] = $event"
                />
                <span v-else class="after">{{ show(item.after) }}</span>
              </div>

              <div v-if="item.detail && entry.expanded[item.index]" class="detail">
                <textarea
                  v-if="item.kind === 'command' && item.target.startsWith('Write ')"
                  spellcheck="false"
                  :readonly="entry.status !== 'pending'"
                  :value="entry.editedSource[item.index] ?? item.detail"
                  @input="entry.editedSource[item.index] = ($event.target as HTMLTextAreaElement).value"
                ></textarea>
                <pre v-else>{{ item.detail }}</pre>
              </div>
              <div v-if="item.error" class="item-error">{{ item.error }}</div>
            </li>
          </ul>

          <div class="footer">
            <template v-if="entry.status === 'pending'">
              <button class="primary" :disabled="acceptedCount(entry) === 0 || editorState.playing" @click="apply(entry)">
                Apply {{ acceptedCount(entry) }}
              </button>
              <button @click="reject(entry)">Reject</button>
            </template>
            <span v-else class="decision">
              {{ entry.status === 'rejected' ? 'Rejected' : entry.status === 'applied' ? 'Applied ✓' : 'Partly applied' }}
            </span>
            <span class="usage" title="Tokens used (input / output)">
              {{ entry.usage.inputTokens ?? 0 }} / {{ entry.usage.outputTokens ?? 0 }} tok
            </span>
          </div>
          <ul v-if="entry.applyErrors.length" class="apply-errors">
            <li v-for="error in entry.applyErrors" :key="error">{{ error }}</li>
          </ul>
          <div v-if="entry.scriptCheck" class="script-check" :class="{ failing: entry.scriptCheck.problems.length }">
            <template v-if="entry.scriptCheck.problems.length === 0">
              ✓ Script check: ran {{ entry.scriptCheck.scripts.join(', ') }} for 2 seconds without errors
            </template>
            <template v-else>
              <div>⚠ Script check found errors when the game runs:</div>
              <ul>
                <li v-for="problem in entry.scriptCheck.problems" :key="problem.path + problem.message">
                  {{ problem.path }}: {{ problem.message }}
                </li>
              </ul>
              <button class="primary" :disabled="!canSend" @click="askToFix(entry)">Ask AI to fix</button>
            </template>
          </div>
        </div>
      </template>

      <div v-if="editorState.ai.busy" class="thinking">Thinking…</div>
    </div>

    <div class="composer">
      <div v-if="focus" class="focus-chip">
        About {{ focus }}
        <button title="Ask about the whole project instead" @click="focus = ''">×</button>
      </div>
      <textarea
        ref="inputEl"
        v-model="input"
        rows="3"
        :placeholder="
          editorState.ai.state === 'ready'
            ? 'Describe a change… (Enter to send, Shift+Enter for a new line)'
            : editorState.ai.text
        "
        :disabled="editorState.ai.state !== 'ready'"
        @keydown="onKeydown"
      ></textarea>
      <button class="send primary" :disabled="!canSend || !input.trim()" @click="send">Send</button>
    </div>
  </div>
</template>

<style scoped>
.ai-panel {
  display: flex;
  flex-direction: column;
  height: 100%;
  background: #181825;
  font-size: 0.85em;
}
.panel-header {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 10px 12px 8px;
  border-bottom: 1px solid #2a2a3a;
}
h2 {
  font-size: 0.95em;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  opacity: 0.7;
  margin: 0;
}
.collapse {
  margin-left: auto;
  flex-shrink: 0;
  width: 22px;
  height: 22px;
  padding: 0;
  border: 1px solid #333;
  border-radius: 0;
  background: #11111b;
  color: #eee;
  cursor: pointer;
}
.collapse:hover {
  background: #262637;
}
.status {
  display: flex;
  align-items: center;
  gap: 6px;
  min-width: 0;
  opacity: 0.75;
  font-size: 0.85em;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.dot {
  width: 7px;
  height: 7px;
  flex-shrink: 0;
  background: #666;
}
.status.ready .dot {
  background: #22c55e;
}
.status.starting .dot {
  background: #eab308;
}
.status.failed .dot {
  background: #ef4444;
}
.entries {
  flex: 1;
  overflow-y: auto;
  padding: 10px 12px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}
.empty p {
  opacity: 0.6;
  margin: 0 0 8px;
}
.example {
  display: block;
  width: 100%;
  margin-bottom: 4px;
  text-align: left;
  padding: 6px 8px;
  height: auto;
  white-space: normal;
}
.message {
  padding: 6px 8px;
  white-space: pre-wrap;
  word-break: break-word;
}
.message.user {
  align-self: flex-end;
  max-width: 90%;
  background: #313148;
}
.message.error {
  border-left: 2px solid #ef4444;
  background: #2a1b22;
}
.proposal {
  border: 1px solid #333;
  background: #11111b;
}
.proposal.pending {
  border-color: #3b82f6;
}
.summary {
  padding: 8px 8px 2px;
  font-weight: 600;
}
.notes {
  padding: 2px 8px 6px;
  opacity: 0.65;
}
.no-changes {
  padding: 0 8px;
  opacity: 0.6;
}
.items {
  list-style: none;
  margin: 0;
  padding: 0;
}
.items li {
  border-top: 1px solid #222232;
  padding: 6px 8px;
}
.items li.invalid {
  background: #1d1418;
}
.item-row {
  display: flex;
  align-items: center;
  gap: 6px;
}
.kind {
  width: 14px;
  text-align: center;
  opacity: 0.7;
}
.target {
  flex: 1;
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.badge {
  font-size: 0.75em;
  text-transform: uppercase;
  padding: 0 4px;
  border: 1px solid #555;
  opacity: 0.7;
}
.change {
  display: flex;
  align-items: center;
  gap: 6px;
  margin: 4px 0 0 34px;
}
.before {
  opacity: 0.55;
  text-decoration: line-through;
  max-width: 35%;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.arrow {
  opacity: 0.5;
}
.after {
  color: #86efac;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.detail {
  margin: 6px 0 0 34px;
}
.detail pre,
.detail textarea {
  width: 100%;
  box-sizing: border-box;
  margin: 0;
  padding: 6px;
  background: #0b0b12;
  border: 1px solid #2a2a3a;
  color: #d7dae0;
  font-family: Consolas, 'Cascadia Mono', monospace;
  font-size: 0.95em;
  white-space: pre;
  overflow-x: auto;
}
.detail textarea {
  min-height: 160px;
  resize: vertical;
}
.item-error {
  margin: 4px 0 0 34px;
  color: #f87171;
}
.footer {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 8px;
  border-top: 1px solid #222232;
}
.decision {
  opacity: 0.8;
}
.proposal.applied .decision {
  color: #86efac;
}
.proposal.partial .decision {
  color: #fcd34d;
}
.usage {
  margin-left: auto;
  opacity: 0.45;
  font-size: 0.85em;
}
.apply-errors {
  margin: 0;
  padding: 0 8px 8px 24px;
  color: #fcd34d;
}
.script-check {
  margin: 0 8px 8px;
  padding: 6px 8px;
  border-left: 2px solid #22c55e;
  background: #12201a;
}
.script-check.failing {
  border-left-color: #f59e0b;
  background: #221c10;
}
.script-check ul {
  margin: 4px 0 6px;
  padding-left: 16px;
  word-break: break-word;
}
.thinking {
  opacity: 0.6;
  font-style: italic;
}
.composer {
  border-top: 1px solid #2a2a3a;
  padding: 8px;
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.focus-chip {
  display: flex;
  align-items: center;
  gap: 6px;
  align-self: flex-start;
  padding: 2px 6px;
  border: 1px solid #3b82f6;
  background: #1f2440;
}
.focus-chip button {
  height: 18px;
  width: 18px;
  padding: 0;
  border: 0;
  background: none;
}
.composer textarea {
  resize: vertical;
  min-height: 52px;
  padding: 6px;
  background: #11111b;
  border: 1px solid #333;
  color: #eee;
  font: inherit;
}
.composer textarea:focus {
  outline: none;
  border-color: #3b82f6;
}
.send {
  align-self: flex-end;
}
button {
  height: 24px;
  padding: 0 10px;
  border: 1px solid #333;
  border-radius: 0;
  background: #11111b;
  color: #eee;
  font: inherit;
  cursor: pointer;
}
button:hover:not(:disabled) {
  background: #262637;
}
button:disabled {
  opacity: 0.4;
  cursor: default;
}
button.primary {
  border-color: #3b82f6;
}
button.primary:not(:disabled) {
  background: #1f3a6e;
}
.expand {
  width: 22px;
  padding: 0;
  height: 20px;
}
</style>
