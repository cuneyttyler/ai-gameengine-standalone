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

export function requestRecords(): void {
  if (asked) return
  asked = true
  sendToHost({ type: 'records:request' })
}
