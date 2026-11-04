// What's been happening (the status bar's notifications): the AI's calls in flight and finished (art, characters,
// animations, backgrounds, a place's steps), a part being animated, an interior's things, and the editor's notices.
// Newest first; the panel lists them, and a few show as floating boxes while the panel is closed.
import { reactive, ref } from 'vue'
import { onHostMessage } from './ipc'
import { callName, type AiCall } from './aiProgress'
import { editorState } from './editorState'

export interface Activity {
  id: string
  title: string
  detail: string
  state: 'running' | 'done' | 'failed' | 'info'
  started: number // ms
  updated: number
  done?: number // of total
  total?: number
  float?: boolean // shown as a floating box while running (its window closed, the panel closed)
  hidden?: boolean // its floating box hidden
  window?: 'part' | 'interior' // the window it was started from: no box while that's open
}

export const activities = reactive<Activity[]>([])
export const panelOpen = ref(false)
export const unseen = ref(0)

const MAX = 120

export function track(id: string, patch: Partial<Activity> & { title?: string }): void {
  const now = Date.now()
  const at = activities.findIndex((a) => a.id === id)
  if (at >= 0) {
    const was = activities[at]!
    const next = { ...was, ...patch, updated: now }
    activities.splice(at, 1)
    activities.unshift(next)
    if (was.state === 'running' && next.state !== 'running' && !panelOpen.value) unseen.value++
    return
  }
  activities.unshift({ id, title: patch.title ?? id, detail: '', state: 'info', started: now, updated: now, ...patch })
  if (!panelOpen.value && patch.state !== 'running') unseen.value++
  if (activities.length > MAX) activities.splice(MAX)
}

export function openPanel(open = !panelOpen.value): void {
  panelOpen.value = open
  if (open) unseen.value = 0
}

export function clearFinished(): void {
  for (let i = activities.length - 1; i >= 0; i--) if (activities[i]!.state !== 'running') activities.splice(i, 1)
}

// The AI's calls: each one while it runs, finished when it's gone from the list.
const live = new Set<string>()
onHostMessage((message) => {
  if (message.type === 'ai:progress') {
    const calls = (message.calls ?? []) as AiCall[]
    const now = new Set<string>()
    for (const c of calls) {
      const id = `call-${c.id}`
      now.add(id)
      const failed = c.phase === 'failed'
      track(id, {
        title: callName(c),
        detail: c.step ? c.step : `${c.phase}${c.seconds ? ` · ${Math.round(c.seconds)} s` : ''}`,
        state: failed ? 'failed' : c.phase === 'done' ? 'done' : 'running',
        done: c.done,
        total: c.total,
      })
    }
    for (const id of live) {
      if (!now.has(id)) {
        const a = activities.find((x) => x.id === id)
        if (a && a.state === 'running') track(id, { state: 'done', detail: 'finished' })
      }
    }
    live.clear()
    for (const id of now) live.add(id)
    return
  }
  if (message.type === 'notify' && typeof message.text === 'string') {
    track(`note-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`, { title: message.text, state: 'info' })
    return
  }
  // A part being animated (Animate a part…): a floating box when its window is closed.
  if (message.type === 'part:progress') {
    track('part', { title: 'Animating a part', detail: String(message.text ?? 'Animating…'), state: 'running', float: true, hidden: false, window: 'part' })
    return
  }
  if (message.type === 'part:done') {
    track('part', {
      title: 'Animating a part',
      detail: message.ok ? 'Done: it plays on the thing now' : `Not animated: ${String(message.error ?? '')}`,
      state: message.ok ? 'done' : 'failed',
    })
    return
  }
  // An interior's things (Animate the interior…).
  if (message.type === 'interiorParts:state' && message.id !== null && message.id !== undefined) {
    const parts = (message.parts ?? []) as { status: string }[]
    const animating = parts.filter((p) => p.status === 'animating' || p.status === 'queued').length
    const running = !!message.running
    if (!running && !activities.some((a) => a.id === `interior-${message.id}`)) return
    track(`interior-${message.id}`, {
      title: `Animating the interior of ${String(message.name ?? 'a building')}`,
      detail: message.status === 'finding' ? 'Finding the moving things…' : running ? `${animating} to go` : `${message.done ?? 0} animated`,
      state: running ? 'running' : message.failed ? 'failed' : 'done',
      done: message.done as number | undefined,
      total: ((message.done as number) ?? 0) + animating || undefined,
      float: true,
      window: 'interior',
    })
  }
})

// Whether an activity's floating box shows now: running (or just finished), its window closed, the panel closed.
export function floats(a: Activity): boolean {
  if (!a.float || a.hidden || panelOpen.value) return false
  if (a.window === 'part' && editorState.partDialog !== null) return false
  if (a.window === 'interior' && editorState.interiorPartsDialog !== null) return false
  return a.state === 'running' || Date.now() - a.updated < 6000
}
