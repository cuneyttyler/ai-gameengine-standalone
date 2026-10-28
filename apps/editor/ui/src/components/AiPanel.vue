<script setup lang="ts">
// AI chat + Diff Review (technical.md §7.6). A request becomes a proposal;
// every proposed change is listed with its before/after, can be unticked or
// edited, and nothing touches the project until "Apply". The host validates
// again on apply and runs everything through the same code as manual edits.
import { computed, nextTick, onMounted, reactive, ref, watch } from 'vue'
import AiProgress from './AiProgress.vue'
import { confirmDialog } from '../dialogs'
import { editorState, setAiEnabled, setAiOpen } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'
import { setPanelVisible, activatePanel } from '../layout'
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
  autoApplied?: boolean // applied without review (review policy)
}

// A contradiction the checker found between this proposal and the World Bible
// (development_plan.md 3.3).
interface ConsistencyIssue {
  op: number | null // 1-based operation, i.e. item.index + 1
  quote: string
  conflictsWith: string
  canonSays: string
  explanation: string
  severity: 'contradiction' | 'possible'
}

interface ProposalEntry {
  kind: 'proposal'
  id: number
  summary: string
  notes: string
  items: ChangeItem[]
  usage: { inputTokens?: number; outputTokens?: number }
  status: 'pending' | 'applied' | 'partial' | 'rejected' | 'expired' // expired: from before a restart
  applyErrors: string[]
  scriptCheck: { scripts: string[]; problems: { path: string; message: string; count: number }[] } | null
  task: { plan: string[]; steps: { number: number; summary: string }[]; stopped: string } | null
  consistency: { state: 'none' | 'checking' | 'done' | 'failed'; issues: ConsistencyIssue[]; errors: string[] }
  autoApplied: number[]
  autoState: 'applied' | 'undone'
  autoCanToggle?: boolean // its undo step is the next to undo or redo
  // After Apply: whether the change is in effect (Ctrl+Z / Ctrl+Y follow it).
  applyState?: 'applied' | 'undone' | 'settled'
  canUndo?: boolean
  canApply?: boolean
  autoNote: string // targets "Undo" left alone because they'd changed since
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

// A multi-step task in progress (development_plan.md 2.9): its plan and steps
// as they happen; its result arrives later as an ordinary proposal.
interface TaskEntry {
  kind: 'task'
  id: number
  plan: string[]
  steps: { number: number; summary: string; problems: number; applyErrors: number }[]
  state: 'working' | 'finished' | 'stopped'
  reason: string
  paused?: { step: number; summary: string; operations: unknown[] } // Review each step: waiting for Continue
}

// Another panel's work (a whole place, a whole game): a card that opens it with the request filled in.
interface RouteEntry {
  kind: 'route'
  id: number
  panel: 'build' | 'planner'
  request: string
  reason: string
}

// Unclear: a question back, with answers to click.
interface AskEntry {
  kind: 'ask'
  id: number
  question: string
  choices: string[]
  answered: string
}

// What it read or looked at before answering (its tools), in a line.
interface ToolsEntry {
  kind: 'tools'
  id: number
  tools: string[]
}

// "Too big for one go": suggested smaller requests.
interface SplitEntry {
  kind: 'split'
  id: number
  summary: string
  notes: string
  parts: string[]
}

// A place build (7.4): its stage in a line or two; the build itself is in the Build panel.
interface BuildEntry {
  kind: 'build'
  id: string
  name: string
  stage: string
  text: string
  query: string
}

// The AI Planner's card (development_plan.md 22): one per plan, kept up to date.
interface PlanEntry {
  kind: 'plan'
  stageLabel: string
  status: string
  message: string
  headline: string
  done: number
  total: number
}

type Entry = ProposalEntry | MessageEntry | TaskEntry | SplitEntry | BuildEntry | PlanEntry | RouteEntry | AskEntry | ToolsEntry

const entries = ref<Entry[]>([])
const input = ref('')
const focus = ref('') // "Skeleton.Health.max" when asked from an Inspector field
const listEl = ref<HTMLElement | null>(null)
const inputEl = ref<HTMLTextAreaElement | null>(null)

const canSend = computed(() => editorState.ai.state === 'ready' && !editorState.ai.busy && !editorState.playing)

onHostMessage((message) => {
  if (message.type === 'ai:restore') {
    restore(message.events as Record<string, unknown>[])
  } else {
    handle(message)
  }
})

// The conversation is kept per project (development_plan.md 4.5): on open the
// host replays what the panel was sent. Earlier runs' entries have negative
// ids; their undecided proposals can't be applied any more.
function restore(events: Record<string, unknown>[]): void {
  entries.value = []
  for (const event of events) handle(event)
  for (const entry of entries.value) {
    if (entry.kind === 'proposal' && entry.id < 0) {
      if (entry.status === 'pending') entry.status = 'expired'
      if (entry.consistency.state === 'checking') entry.consistency.state = 'none'
    } else if (entry.kind === 'task' && entry.id < 0 && entry.state === 'working') {
      entry.state = 'stopped'
      entry.reason = 'The editor was closed while it was working.'
    }
  }
}

onMounted(() => sendToHost({ type: 'ai:history' }))

async function clearConversation(): Promise<void> {
  if (
    await confirmDialog({
      title: 'Clear conversation',
      message: 'Forget this conversation? The AI starts fresh. Changes already applied stay.',
      confirmLabel: 'Clear',
      danger: true,
    })
  ) {
    sendToHost({ type: 'ai:clear' })
  }
}

function handle(message: Record<string, unknown>): void {
  if (message.type === 'ai:message') {
    entries.value.push({ kind: 'message', role: message.role as 'user' | 'error', text: message.text as string })
  } else if (message.type === 'build:card') {
    if (!message.buildId) return
    const card = entries.value.find((e) => e.kind === 'build' && e.id === message.buildId) as BuildEntry | undefined
    const fields = { name: String(message.name ?? ''), stage: String(message.stage ?? ''), text: String(message.text ?? ''),
      query: String(message.query ?? '') }
    if (card) Object.assign(card, fields)
    else entries.value.push(reactive({ kind: 'build', id: String(message.buildId), ...fields }) as BuildEntry)
  } else if (message.type === 'planner:card') {
    const card = message.card as Record<string, any> | null
    if (!card || !card.stage) return
    const fields = { stageLabel: String(card.stageLabel ?? ''), status: String(card.status ?? ''), message: String(card.message ?? ''),
      headline: String(card.headline ?? ''), done: Number(card.progress?.done ?? 0), total: Number(card.progress?.total ?? 0) }
    const entry = entries.value.find((e) => e.kind === 'plan') as PlanEntry | undefined
    if (entry) Object.assign(entry, fields)
    else entries.value.push(reactive({ kind: 'plan', ...fields }) as PlanEntry)
  } else if (message.type === 'ai:proposal') {
    const items = message.items as ChangeItem[]
    const entry: ProposalEntry = reactive({
      kind: 'proposal',
      id: message.id as number,
      summary: message.summary as string,
      notes: message.notes as string,
      items,
      usage: (message.usage as ProposalEntry['usage']) ?? {},
      status: message.decided ? 'applied' : 'pending',
      applyErrors: (message.autoErrors as string[]) ?? [],
      scriptCheck: null,
      task: (message.task as ProposalEntry['task']) ?? null,
      autoApplied: (message.autoApplied as number[]) ?? [],
      consistency: { state: 'none', issues: [], errors: [] },
      autoState: 'applied',
      autoNote: '',
      accepted: Object.fromEntries(items.map((item) => [item.index, !item.error && !item.autoApplied])),
      edited: {},
      editedSource: {},
      expanded: Object.fromEntries(
        items.map((item) => [item.index, ['command', 'dialogue', 'quest'].includes(item.kind)]),
      ),
    })
    entries.value.push(entry)
  } else if (message.type === 'ai:decided') {
    const entry = entries.value.find((e) => e.kind === 'proposal' && e.id === message.id) as ProposalEntry | undefined
    if (entry) {
      entry.status = message.status as ProposalEntry['status']
      entry.applyErrors = (message.errors as string[]) ?? []
    }
  } else if (message.type === 'ai:applyState') {
    const entry = entries.value.find((e) => e.kind === 'proposal' && e.id === message.id) as ProposalEntry | undefined
    if (entry) {
      entry.applyState = message.state as ProposalEntry['applyState']
      entry.canUndo = message.canUndo as boolean
      entry.canApply = message.canApply as boolean
    }
  } else if (message.type === 'ai:taskProgress') {
    let entry = entries.value.find((e) => e.kind === 'task' && e.id === message.id) as TaskEntry | undefined
    if (!entry) {
      const created: TaskEntry = reactive({
        kind: 'task',
        id: message.id as number,
        plan: [],
        steps: [],
        state: 'working',
        reason: '',
      })
      entries.value.push(created)
      entry = created
    }
    entry.plan = (message.plan as string[]) ?? []
    entry.steps.push({
      number: message.step as number,
      summary: message.summary as string,
      problems: ((message.problems as unknown[]) ?? []).length,
      applyErrors: ((message.applyErrors as unknown[]) ?? []).length,
    })
    entry.state = message.state as TaskEntry['state']
  } else if (message.type === 'ai:taskPaused') {
    const entry = entries.value.find((e) => e.kind === 'task' && e.id === message.id) as TaskEntry | undefined
    if (entry) entry.paused = { step: message.step as number, summary: message.summary as string, operations: (message.operations as unknown[]) ?? [] }
  } else if (message.type === 'ai:taskStopped') {
    const entry = entries.value.find((e) => e.kind === 'task' && e.id === message.id) as TaskEntry | undefined
    if (entry) {
      entry.state = 'stopped'
      entry.reason = message.reason as string
    }
  } else if (message.type === 'panel:show') {
    // The agent's "openPanel" (docs/ai/agent.md).
    const id = message.panel as string
    if (['build', 'planner', 'generate', 'assets', 'project', 'scene'].includes(id)) {
      setPanelVisible(id as Parameters<typeof setPanelVisible>[0], true)
      activatePanel(id as Parameters<typeof activatePanel>[0])
    }
  } else if (message.type === 'ai:route') {
    entries.value.push({
      kind: 'route',
      id: message.id as number,
      panel: (message.panel as 'build' | 'planner') ?? 'build',
      request: (message.request as string) ?? '',
      reason: (message.reason as string) ?? '',
    })
  } else if (message.type === 'ai:ask') {
    entries.value.push({
      kind: 'ask',
      id: message.id as number,
      question: (message.question as string) ?? '',
      choices: (message.choices as string[]) ?? [],
      answered: '',
    })
  } else if (message.type === 'ai:tools') {
    entries.value.push({ kind: 'tools', id: message.id as number, tools: (message.tools as string[]) ?? [] })
  } else if (message.type === 'ai:split') {
    entries.value.push({
      kind: 'split',
      id: message.id as number,
      summary: message.summary as string,
      notes: message.notes as string,
      parts: (message.parts as string[]) ?? [],
    })
  } else if (message.type === 'ai:consistency') {
    const entry = entries.value.find((e) => e.kind === 'proposal' && e.id === message.id) as ProposalEntry | undefined
    const result = message.result as { checking?: boolean; ok?: boolean; checked?: boolean; issues?: ConsistencyIssue[]; errors?: string[] }
    if (entry) {
      if (result.checking) {
        entry.consistency = { state: 'checking', issues: [], errors: [] }
      } else if (result.ok && result.checked) {
        entry.consistency = { state: 'done', issues: result.issues ?? [], errors: [] }
      } else if (result.ok) {
        entry.consistency = { state: 'none', issues: [], errors: [] }
      } else {
        entry.consistency = { state: 'failed', issues: [], errors: result.errors ?? [] }
      }
    }
  } else if (message.type === 'ai:autoState') {
    // The auto-applied tweaks followed the undo history (Ctrl+Z, Ctrl+Y): Undo while applied, Apply once undone.
    const entry = entries.value.find((e) => e.kind === 'proposal' && e.id === message.id) as ProposalEntry | undefined
    if (entry && message.state !== 'settled') {
      entry.autoState = message.state as ProposalEntry['autoState']
      entry.autoCanToggle = !!message.canToggle
      if (message.state === 'applied') entry.autoNote = ''
    }
  } else if (message.type === 'ai:autoUndone') {
    const entry = entries.value.find((e) => e.kind === 'proposal' && e.id === message.id) as ProposalEntry | undefined
    if (entry) {
      entry.autoState = 'undone'
      entry.autoNote = ((message.skipped as string[]) ?? []).join(', ')
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
}

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

function issuesFor(entry: ProposalEntry, item: ChangeItem): ConsistencyIssue[] {
  return entry.consistency.issues.filter((issue) => issue.op === item.index + 1)
}

// Sends the contradictions back as a follow-up, like "Ask AI to fix" for scripts.
function fixContradictions(entry: ProposalEntry): void {
  if (!canSend.value) return
  const lines = entry.consistency.issues.map(
    (i) => `- "${i.quote}" contradicts ${i.conflictsWith}, which says "${i.canonSays}"`,
  )
  sendToHost({
    type: 'ai:send',
    text: `Your last proposal contradicts the World Bible:\n${lines.join('\n')}\nPropose it again, consistent with the canon.`,
    focus: '',
  })
}

function continueTask(entry: TaskEntry): void {
  entry.paused = undefined
  sendToHost({ type: 'ai:taskContinue', id: entry.id })
}

function stopTask(entry: TaskEntry): void {
  sendToHost({ type: 'ai:taskStop', id: entry.id })
}

function showPanel(id: 'build' | 'planner'): void {
  setPanelVisible(id, true)
  activatePanel(id)
}

function askAsChange(entry: BuildEntry): void {
  sendToHost({ type: 'build:cancel' })
  sendToHost({ type: 'ai:send', text: entry.query, asChange: true })
}

function openRoute(entry: RouteEntry): void {
  editorState.prefill = { panel: entry.panel, text: entry.request }
  showPanel(entry.panel)
}

function answerAsk(entry: AskEntry, choice: string): void {
  entry.answered = choice
  sendToHost({ type: 'ai:send', text: choice, asChange: true })
}

// A split carried out in one go: the parts as the steps of one task, each checked before the next.
function runSplit(entry: SplitEntry): void {
  const steps = entry.parts.map((p, i) => `${i + 1}. ${p}`).join('\n')
  sendToHost({ type: 'ai:send', text: `Do these in order, as one task, step by step:\n${steps}`, asChange: true })
}

function useSplitPart(part: string): void {
  input.value = part
  nextTick(() => inputEl.value?.focus())
}

function undoAuto(entry: ProposalEntry): void {
  sendToHost({ type: 'ai:undoAuto', id: entry.id })
}

const showSettings = ref(false)

function setReviewPolicy(policy: string): void {
  sendToHost({ type: 'ai:setReviewPolicy', policy })
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
  dialogue: '“',
  quest: '!',
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
        v-if="editorState.ai.enabled"
        class="header-button"
        :class="{ active: showSettings }"
        title="AI settings"
        @click="showSettings = !showSettings"
      >
        ⚙
      </button>
      <button
        v-if="entries.length"
        class="header-button"
        title="Clear the conversation (it's kept with the project until you do)"
        @click="clearConversation"
      >
        ⌫
      </button>
      <button class="header-button" title="Collapse the AI panel" @click="setAiOpen(false)">›</button>
    </header>
    <AiProgress :paths="['/propose', '/consistency', '/world/']" />

    <div v-if="showSettings && editorState.ai.enabled" class="settings">
      <div class="settings-title">Review</div>
      <label>
        <input
          type="radio"
          name="review-policy"
          :checked="editorState.ai.reviewPolicy === 'reviewAll'"
          @change="setReviewPolicy('reviewAll')"
        />
        Review every change before it's applied
      </label>
      <label>
        <input
          type="radio"
          name="review-policy"
          :checked="editorState.ai.reviewPolicy === 'autoApplyTweaks'"
          @change="setReviewPolicy('autoApplyTweaks')"
        />
        Apply small tweaks directly
      </label>
      <label>
        <input type="checkbox" :checked="editorState.ai.stepReview" @change="sendToHost({ type: 'ai:setStepReview', on: ($event.target as HTMLInputElement).checked })" />
        Review each step of a bigger task (it waits for Continue after each)
      </label>
      <p class="settings-hint">
        Small tweaks are new values for existing fields, settings and script properties. They apply right away and
        can be undone from the chat. Components, spawning, scripts, prefab edits and deletions are always reviewed.
      </p>
      <button class="turn-off" title="Stops the AI service; nothing is sent to the model" @click="setAiEnabled(false)">
        Turn AI off
      </button>
    </div>

    <div ref="listEl" class="entries">
      <div v-if="editorState.ai.choice === 'undecided'" class="empty first-run">
        <p><strong>Use AI in this editor?</strong></p>
        <p>
          You can describe changes in plain language and review each one before it's applied. Everything
          else in the editor works without AI.
        </p>
        <p>When AI is on, each request sends your message to the AI provider (Mistral), along with:</p>
        <ul>
          <li>the open scene's entities, their components and values, and tile counts</li>
          <li>your prefabs, project settings and asset file names</li>
          <li>the source of your Lua scripts</li>
          <li>the earlier messages in the conversation</li>
        </ul>
        <p>Nothing is sent until you turn it on, and you can turn it off at any time.</p>
        <div class="first-run-buttons">
          <button class="primary" @click="setAiEnabled(true)">Turn on AI</button>
          <button @click="setAiEnabled(false)">Not now</button>
        </div>
      </div>
      <div v-else-if="!editorState.ai.enabled" class="empty">
        <p>AI is turned off. The AI service isn't running and nothing is sent to the model.</p>
        <button class="primary turn-on" @click="setAiEnabled(true)">Turn on AI</button>
      </div>
      <div v-else-if="entries.length === 0" class="empty">
        <p>
          Describe a change in plain language.
          {{
            editorState.ai.reviewPolicy === 'reviewAll'
              ? "You'll review every edit before anything is applied."
              : 'Small tweaks apply right away (with Undo); everything else waits for your review.'
          }}
        </p>
        <button v-for="example in examples" :key="example" class="example" @click="input = example">
          {{ example }}
        </button>
      </div>

      <template v-for="(entry, entryIndex) in entries" :key="entryIndex">
        <div v-if="entry.kind === 'message'" class="message" :class="entry.role">{{ entry.text }}</div>

        <div v-else-if="entry.kind === 'task'" class="task" :class="entry.state">
          <div class="task-head">
            <span class="task-title">
              {{
                entry.state === 'working'
                  ? `Working — step ${entry.steps.length + 1}`
                  : entry.state === 'stopped'
                    ? 'Stopped'
                    : `Done in ${entry.steps.length} ${entry.steps.length === 1 ? 'step' : 'steps'}`
              }}
            </span>
            <button v-if="entry.paused" class="primary" title="This step looks right: on to the next" @click="continueTask(entry)">
              Continue
            </button>
            <button v-if="entry.state === 'working'" title="Stop; nothing is changed" @click="stopTask(entry)">
              Stop
            </button>
            <div v-if="entry.paused" class="paused-step">
              Step {{ entry.paused.step }} done on the working copy: {{ entry.paused.summary }}
              ({{ entry.paused.operations.length }} {{ entry.paused.operations.length === 1 ? 'change' : 'changes' }}). Nothing
              reaches the project until the whole is reviewed.
            </div>
          </div>
          <ol v-if="entry.plan.length" class="task-plan">
            <li v-for="(line, i) in entry.plan" :key="i">{{ line }}</li>
          </ol>
          <ul class="task-steps">
            <li v-for="step in entry.steps" :key="step.number">
              <span class="step-number">{{ step.number }}</span>
              <span class="step-summary">{{ step.summary }}</span>
              <span v-if="step.problems || step.applyErrors" class="step-problems">
                {{ step.problems + step.applyErrors }} to fix
              </span>
              <span v-else class="step-ok">✓</span>
            </li>
          </ul>
          <div v-if="entry.state === 'working'" class="task-note">
            Changes go to a working copy; nothing reaches your project until you review the result.
          </div>
          <div v-if="entry.reason" class="task-note">{{ entry.reason }}</div>
        </div>

        <div v-else-if="entry.kind === 'build'" class="build-card" :class="entry.stage">
          <div class="build-title">Building {{ entry.name || entry.query }}</div>
          <div class="build-text">{{ entry.text }}</div>
          <div class="build-buttons">
            <button class="primary" @click="showPanel('build')">Open build panel</button>
            <button
              v-if="entry.stage === 'interpreting' || entry.stage === 'request'"
              title="Not a place to build: treat it as a change to the scene instead"
              @click="askAsChange(entry)"
            >
              Ask as a change instead
            </button>
          </div>
        </div>

        <div v-else-if="entry.kind === 'plan'" class="build-card" :class="entry.status">
          <div class="build-title">Planning {{ entry.headline }}</div>
          <div class="build-text">
            {{ entry.stageLabel }}<span v-if="entry.total"> · {{ entry.done }}/{{ entry.total }}</span> — {{ entry.message }}
          </div>
          <div class="build-buttons">
            <button class="primary" @click="showPanel('planner')">Open planner panel</button>
          </div>
        </div>

        <div v-else-if="entry.kind === 'route'" class="split route">
          <div class="summary">This is {{ entry.panel === 'build' ? "the Build panel's" : "the Planner's" }} work</div>
          <div v-if="entry.reason" class="notes">{{ entry.reason }}</div>
          <div class="build-buttons">
            <button class="primary" :title="entry.request" @click="openRoute(entry)">
              {{ entry.panel === 'build' ? 'Open the Build panel with it' : 'Open the Planner with it' }}
            </button>
          </div>
        </div>

        <div v-else-if="entry.kind === 'ask'" class="split ask">
          <div class="summary">{{ entry.question }}</div>
          <div class="build-buttons">
            <button
              v-for="choice in entry.choices"
              :key="choice"
              class="example"
              :class="{ chosen: entry.answered === choice }"
              :disabled="!!entry.answered"
              @click="answerAsk(entry, choice)"
            >
              {{ choice }}
            </button>
          </div>
          <div v-if="!entry.answered" class="split-hint">Or answer in your own words below.</div>
        </div>

        <div v-else-if="entry.kind === 'tools'" class="tools-line">Looked at: {{ entry.tools.join(', ') }}</div>

        <div v-else-if="entry.kind === 'split'" class="split">
          <div class="summary">{{ entry.summary }}</div>
          <div v-if="entry.notes" class="notes">{{ entry.notes }}</div>
          <div class="build-buttons">
            <button class="primary" title="Carry them out as one task, step by step, each checked before the next" @click="runSplit(entry)">Do them all, step by step</button>
          </div>
          <div class="split-hint">Or one at a time:</div>
          <button
            v-for="part in entry.parts"
            :key="part"
            class="example"
            title="Put this request in the message box"
            @click="useSplitPart(part)"
          >
            {{ part }}
          </button>
        </div>

        <div v-else class="proposal" :class="entry.status">
          <div v-if="entry.task" class="task-result">
            Result of {{ entry.task.steps.length }} {{ entry.task.steps.length === 1 ? 'step' : 'steps' }} — review
            it all here; nothing has been applied yet.
          </div>
          <div v-if="entry.task?.stopped" class="item-error">{{ entry.task.stopped }}</div>
          <div class="summary">{{ entry.summary }}</div>
          <div v-if="entry.notes" class="notes">{{ entry.notes }}</div>
          <div v-if="entry.consistency.state !== 'none'" class="consistency" :class="entry.consistency.state">
            <template v-if="entry.consistency.state === 'checking'">Checking against the World Bible…</template>
            <template v-else-if="entry.consistency.state === 'failed'">
              Couldn't check against the World Bible{{ entry.consistency.errors.length ? ': ' + entry.consistency.errors[0] : '' }}
            </template>
            <template v-else-if="entry.consistency.issues.length === 0">✓ Consistent with the World Bible</template>
            <template v-else>
              <div class="consistency-head">
                ⚠ {{ entry.consistency.issues.length }} possible
                {{ entry.consistency.issues.length === 1 ? 'contradiction' : 'contradictions' }} with the World Bible
              </div>
              <ul>
                <li v-for="(issue, i) in entry.consistency.issues" :key="i">
                  <span class="severity" :class="issue.severity">{{ issue.severity }}</span>
                  “{{ issue.quote }}” — {{ issue.conflictsWith }} says “{{ issue.canonSays }}”.
                  <span class="muted">{{ issue.explanation }}</span>
                </li>
              </ul>
              <button v-if="entry.status === 'pending'" class="primary" :disabled="!canSend" @click="fixContradictions(entry)">
                Ask AI to fix
              </button>
            </template>
          </div>
          <p v-if="entry.items.length === 0" class="no-changes">No changes proposed.</p>

          <ul class="items">
            <li v-for="item in entry.items" :key="item.index" :class="{ invalid: !!item.error }">
              <div class="item-row">
                <span
                  v-if="item.autoApplied"
                  class="auto-tag"
                  :class="{ undone: entry.autoState === 'undone' }"
                  :title="entry.autoState === 'undone' ? 'Applied automatically, then undone' : 'Applied automatically (small tweak)'"
                >
                  {{ entry.autoState === 'undone' ? '↺' : '✓' }}
                </span>
                <input
                  v-else
                  type="checkbox"
                  :checked="entry.accepted[item.index] && !item.error"
                  :disabled="!!item.error || entry.status !== 'pending'"
                  :title="item.error ? 'Can\'t be applied' : 'Include this change'"
                  @change="entry.accepted[item.index] = ($event.target as HTMLInputElement).checked"
                />
                <span class="kind" :title="item.kind">{{ kindIcons[item.kind] ?? '•' }}</span>
                <span
                  v-if="issuesFor(entry, item).length"
                  class="issue-mark"
                  :title="issuesFor(entry, item).map((i) => i.explanation).join('\n')"
                >
                  ⚠
                </span>
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

              <div v-if="!['command', 'remove_component', 'dialogue', 'quest'].includes(item.kind)" class="change">
                <span class="before" :title="'Before: ' + show(item.before)">{{ show(item.before) }}</span>
                <span class="arrow">→</span>
                <FieldEditor
                  v-if="editableField(item, entry) && entry.status === 'pending' && !item.error && !item.autoApplied"
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

          <div v-if="entry.autoApplied.length" class="auto-line">
            <template v-if="entry.autoState === 'undone'">
              Undone{{ entry.autoNote ? ` — kept your newer value for ${entry.autoNote}` : '' }}
              <button
                v-if="entry.autoCanToggle"
                class="primary"
                :disabled="editorState.playing"
                title="Apply these tweaks again (Ctrl+Y)"
                @click="undoAuto(entry)"
              >
                Apply
              </button>
            </template>
            <template v-else>
              {{ entry.autoApplied.length }} small {{ entry.autoApplied.length === 1 ? 'tweak' : 'tweaks' }} applied
              automatically
              <button
                v-if="entry.id > 0"
                :disabled="editorState.playing"
                title="Put the previous values back"
                @click="undoAuto(entry)"
              >
                Undo
              </button>
            </template>
          </div>
          <div class="footer">
            <template v-if="entry.status === 'pending'">
              <button class="primary" :disabled="acceptedCount(entry) === 0 || editorState.playing" @click="apply(entry)">
                Apply {{ acceptedCount(entry) }}
              </button>
              <button @click="reject(entry)">{{ entry.autoApplied.length ? 'Reject the rest' : 'Reject' }}</button>
            </template>
            <template v-else-if="entry.applyState === 'undone'">
              <span class="decision">Undone</span>
              <button
                class="primary"
                :disabled="!entry.canApply || editorState.playing"
                :title="entry.canApply ? 'Apply this change again' : 'Can no longer be applied again'"
                @click="sendToHost({ type: 'ai:reapply', id: entry.id })"
              >
                Apply
              </button>
            </template>
            <template v-else-if="entry.applyState === 'applied'">
              <span class="decision">{{ entry.status === 'partial' ? 'Partly applied' : 'Applied ✓' }}</span>
              <button
                :disabled="!entry.canUndo || editorState.playing"
                :title="entry.canUndo ? 'Undo this change (Ctrl+Z)' : 'Undo the later changes first (Ctrl+Z)'"
                @click="sendToHost({ type: 'ai:undoApplied', id: entry.id })"
              >
                Undo
              </button>
            </template>
            <span v-else class="decision">
              {{
                entry.status === 'rejected'
                  ? entry.autoApplied.length ? 'Rest rejected' : 'Rejected'
                  : entry.status === 'applied'
                    ? 'Applied ✓'
                    : entry.status === 'expired'
                      ? 'Not applied — from before the editor restarted; ask again to redo it'
                      : 'Partly applied'
              }}
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
.header-button {
  flex-shrink: 0;
  min-width: 22px;
  height: 22px;
  padding: 0 6px;
  border: 1px solid #333;
  border-radius: 0;
  background: #11111b;
  color: #eee;
  font: inherit;
  font-size: 0.85em;
  cursor: pointer;
}
.header-button:hover {
  background: #262637;
}
.first-run ul {
  margin: 0 0 8px;
  padding-left: 18px;
  opacity: 0.8;
}
.first-run-buttons {
  display: flex;
  gap: 6px;
  margin-top: 8px;
}
.header-button.active {
  border-color: #3b82f6;
}
.settings {
  display: flex;
  flex-direction: column;
  gap: 6px;
  padding: 10px 12px;
  border-bottom: 1px solid #2a2a3a;
  background: #11111b;
}
.settings-title {
  font-size: 0.85em;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  opacity: 0.6;
}
.settings label {
  display: flex;
  align-items: center;
  gap: 6px;
  cursor: pointer;
}
.settings-hint {
  margin: 0;
  font-size: 0.9em;
  opacity: 0.6;
}
.turn-off {
  align-self: flex-start;
  margin-top: 4px;
}
.task,
.build-card {
  border: 1px solid #45475a;
  border-left: 3px solid #89b4fa;
  background: #1e1e2e;
  padding: 8px 10px;
  margin: 6px 0;
}
.build-card.failed,
.build-card.cancelled {
  border-left-color: #6c7086;
}
.build-title {
  font-weight: 600;
}
.build-text {
  color: #a6adc8;
  margin: 4px 0 8px;
}
.build-buttons {
  display: flex;
  gap: 6px;
  flex-wrap: wrap;
}
.split {
  margin: 8px 0;
  padding: 10px;
  border: 1px solid #2a2a3a;
  background: #11111b;
}
.task.working {
  border-color: #3b82f6;
}
.task-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
}
.task-title {
  font-weight: 600;
}
.task-plan {
  margin: 8px 0 0;
  padding-left: 20px;
  opacity: 0.75;
}
.task-steps {
  list-style: none;
  margin: 8px 0 0;
  padding: 0;
}
.task-steps li {
  display: flex;
  gap: 8px;
  padding: 3px 0;
  border-top: 1px solid #222232;
}
.step-number {
  width: 16px;
  opacity: 0.5;
}
.step-summary {
  flex: 1;
  min-width: 0;
}
.step-problems {
  color: #f59e0b;
  white-space: nowrap;
}
.step-ok {
  color: #22c55e;
}
.task-note,
.split-hint {
  margin-top: 8px;
  font-size: 0.9em;
  opacity: 0.65;
}
.task-result {
  margin-bottom: 6px;
  font-size: 0.9em;
  color: #93c5fd;
}
.consistency {
  margin: 6px 0;
  padding: 6px 8px;
  border-left: 2px solid #3b82f6;
  background: #11111b;
  font-size: 0.9em;
}
.consistency.done:not(:has(ul)) {
  border-left-color: #22c55e;
}
.consistency:has(ul) {
  border-left-color: #f59e0b;
}
.consistency ul {
  margin: 4px 0 6px;
  padding-left: 16px;
}
.consistency li {
  margin-bottom: 4px;
}
.consistency-head {
  color: #f59e0b;
}
.severity {
  font-size: 0.8em;
  padding: 0 4px;
  border: 1px solid #555;
  margin-right: 4px;
}
.severity.contradiction {
  border-color: #ef4444;
  color: #fca5a5;
}
.issue-mark {
  color: #f59e0b;
}
.muted {
  opacity: 0.65;
}
.auto-tag {
  width: 13px;
  text-align: center;
  color: #22c55e;
}
.auto-tag.undone {
  color: #888;
}
.auto-line {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-top: 8px;
  font-size: 0.9em;
  opacity: 0.85;
}
.turn-on {
  margin-top: 8px;
}
.status {
  display: flex;
  align-items: center;
  gap: 6px;
  min-width: 0;
  margin-right: auto; /* pushes the header buttons right */
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
.tools-line {
  font-size: 0.82em;
  opacity: 0.65;
  padding: 2px 8px;
}
.ask .chosen {
  outline: 1px solid var(--accent, #4a8);
}
</style>
