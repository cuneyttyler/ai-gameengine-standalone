// The project's input actions (Project Settings ▸ Input), by name: what a part's "an input action" can be set to, and
// a new one added (with its key) from where it's picked.
import { ref } from 'vue'
import { onHostMessage, sendToHost } from './ipc'

interface ActionInfo {
  name: string
  description: string
  context: string
  axis?: boolean
  keys: (string | { key: string })[]
  [more: string]: unknown
}

let map: { schema?: string; actions: ActionInfo[] } | null = null
export const inputActions = ref<{ name: string; keys: string; description: string }[]>([])

onHostMessage((message) => {
  if (message.type !== 'input:data') return
  map = message.map as typeof map
  const list = (map?.actions ?? []).filter((a) => !a.axis && (a.context === 'gameplay' || a.context === 'any'))
  inputActions.value = list.map((a) => ({
    name: a.name,
    description: a.description,
    keys: a.keys.map((k) => (typeof k === 'string' ? k : k.key)).join(', '),
  }))
})

export function requestInputActions(): void {
  sendToHost({ type: 'input:request' })
}

export const isInputAction = (name: string) => inputActions.value.some((a) => a.name === name)

// An action's name as the input map keeps it: lower case, words joined by "_".
export const actionName = (text: string) => text.trim().toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_|_$/g, '')

// A new gameplay action pressed with `key` (the engine's key name), saved to the project's input.json; false if the
// name is taken or the actions aren't known yet.
export function addInputAction(name: string, key: string, description: string): boolean {
  if (!map || !name || map.actions.some((a) => a.name === name)) return false
  const actions = [...map.actions, { name, description, context: 'gameplay', keys: key ? [key] : [], nearOnly: true }]
  sendToHost({ type: 'input:save', map: { schema: map.schema ?? 'aige.input/1', actions } })
  return true
}

// The browser's key codes, as the engine names them (platform/Input.h; Project Settings ▸ Input's).
export function engineKey(event: KeyboardEvent): string | null {
  const code = event.code
  if (/^Key[A-Z]$/.test(code)) return code.slice(3)
  if (/^Digit[0-9]$/.test(code)) return 'Num' + code.slice(5)
  if (/^F([1-9]|1[0-2])$/.test(code)) return code
  const named: Record<string, string> = {
    ArrowUp: 'Up', ArrowDown: 'Down', ArrowLeft: 'Left', ArrowRight: 'Right', Space: 'Space', Enter: 'Enter',
    NumpadEnter: 'Enter', Escape: 'Escape', Tab: 'Tab', Backspace: 'Backspace', ShiftLeft: 'LeftShift',
    ShiftRight: 'RightShift', ControlLeft: 'LeftCtrl', ControlRight: 'RightCtrl', AltLeft: 'LeftAlt', AltRight: 'RightAlt',
  }
  return named[code] ?? null
}

// The characters' clips (input:choices): what a part's player clip can be.
export const playerClips = ref<string[]>([])
onHostMessage((message) => {
  if (message.type === 'input:choices') playerClips.value = (message.clips as string[]) ?? []
})
export function requestPlayerClips(): void {
  sendToHost({ type: 'input:choices' })
}
