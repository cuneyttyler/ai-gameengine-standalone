// The project's records by type (items, packages, effects…), with their names: for pickers outside the Records tab.
import { ref } from 'vue'
import { onHostMessage, sendToHost } from './ipc'

export interface RecordOption {
  value: string
  label: string
}
export const recordsByType = ref<Record<string, RecordOption[]>>({})
let asked = false

onHostMessage((message) => {
  if (message.type !== 'records:list') return
  const out: Record<string, RecordOption[]> = {}
  for (const r of (message.records as { type: string; id: string; resolved?: { name?: unknown } }[]) ?? []) {
    const name = typeof r.resolved?.name === 'string' && r.resolved.name ? r.resolved.name : r.id
    ;(out[r.type] ??= []).push({ value: r.id, label: name === r.id ? r.id : `${name} (${r.id})` })
  }
  recordsByType.value = out
})

// The World Bible's entries of a kind (World panel: factions, characters…), by id: a faction can be one of these too.
export const worldByKind = ref<Record<string, RecordOption[]>>({})
onHostMessage((message) => {
  if (message.type !== 'world:list') return
  const out: Record<string, RecordOption[]> = {}
  for (const e of (message.entries as { kind: string; id: string; name?: string }[]) ?? []) {
    ;(out[e.kind] ??= []).push({ value: e.id, label: e.name && e.name !== e.id ? `${e.name} (${e.id})` : e.id })
  }
  worldByKind.value = out
})

// A record type's ids, and the World Bible's entries of the same kind not yet records (marked so).
export function recordsAndWorld(type: string): (RecordOption & { hint?: string })[] {
  const records = recordsByType.value[type] ?? []
  const ids = new Set(records.map((r) => r.value))
  return [...records, ...(worldByKind.value[type] ?? []).filter((w) => !ids.has(w.value)).map((w) => ({ ...w, hint: 'World Bible' }))]
}

export function requestRecords(): void {
  if (asked) return
  asked = true
  sendToHost({ type: 'records:request' })
  sendToHost({ type: 'world:request' })
}
