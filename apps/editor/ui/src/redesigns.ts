// Redesign interiors (Inspector ▸ Building Plan): each building's in flight or just ended, by its entity id, kept
// while the building isn't selected (the Inspector shows it again when it is).
import { reactive } from 'vue'
import { onHostMessage } from './ipc'

export const redesigns = reactive<Record<number, { state: 'working' | 'done' | 'failed'; text: string }>>({})

// Its end, heard whichever building is selected then.
onHostMessage((message) => {
  if (message.type !== 'interior:redesigned') return
  redesigns[Number(message.id)] = message.ok
    ? { state: 'done', text: 'Done: its interiors are redesigned (its stairs are found again on the new floors).' }
    : { state: 'failed', text: `They weren't redesigned: ${String(message.error ?? '')}` }
})
