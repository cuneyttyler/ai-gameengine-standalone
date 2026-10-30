// What the AI's calls in flight are doing (the service's GET /progress, relayed by the editor as
// ai:progress about once a second while any run): one shared list for every panel's strip.
import { ref } from 'vue'
import { onHostMessage } from './ipc'

export interface AiCall {
  id: number
  path: string
  label: string
  phase: string // waiting, thinking, answering, repairing, done, failed
  seconds: number
  calls: number
  inputTokens: number
  outputTokens: number
  thinkingTokens: number
  answerTokens: number
  step?: string // a request of several steps: what it does now, and how far (done of total)
  done?: number
  total?: number
  plan?: AiStep[] // its steps known beforehand (matched art: each sheet and its pieces, the check, the fixes)
}

export interface AiStep {
  id: string
  label: string
  state: string // waiting, working, done, failed
  pieces: { name: string; slug: string; made?: boolean; path?: string }[]
}

export const aiCalls = ref<AiCall[]>([])
// When the last report came, so a strip can tick its seconds between reports.
export const aiCallsAt = ref(0)

onHostMessage((message) => {
  if (message.type !== 'ai:progress') return
  aiCalls.value = (message.calls ?? []) as AiCall[]
  aiCallsAt.value = performance.now()
})

// A call's name when it has no label of its own.
const names: Record<string, string> = {
  '/propose': 'AI panel request',
  '/consistency': 'Consistency check',
  '/world/search': 'World Bible search',
  '/places/interpret': 'Reading the place request',
  '/places/repair-brief': 'Fixing the place Brief',
  '/places/questions': 'Questions about the place',
  '/places/plan-root': 'Planning the place',
  '/places/plan-zone': 'Planning a zone',
  '/places/repair-node': 'Fixing a zone',
  '/places/revise-plan': 'Revising the plan',
  '/places/review-layout': 'Reviewing the layout',
  '/assets/describe': 'Describing assets',
  '/assets/generate': 'Generating art',
  '/assets/references': 'Finding references',
  '/assets/web-search': 'Searching the web',
  '/assets/web-preview': 'Previewing web results',
  '/assets/web-fetch': 'Downloading from the web',
  '/characters/generate': 'Generating a character',
  '/animations/generate': 'Generating animations',
  '/assets/interior': 'Painting the interiors',
  '/parts/stairs': 'Finding the stairs',
}

export function callName(call: AiCall): string {
  if (call.label) return call.label
  const named = names[call.path]
  if (named) return named
  if (call.path.startsWith('/planner/')) return 'Planner · ' + call.path.slice('/planner/'.length)
  return call.path
}
