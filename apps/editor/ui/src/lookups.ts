// What the editor's pickers choose from (searchable selects): the project's scripts, dialogues and packages, and the
// open scene's markers, paths and named things. The host sends them (EditorLists.cpp) when asked; panels ask as they
// open, and after changes that add to them.
import { reactive } from 'vue'
import { onHostMessage, sendToHost } from './ipc'
import type { Option } from './components/SearchSelect.vue'

export const lookups = reactive({
  scripts: [] as string[],
  scriptUsers: {} as Record<string, string[]>,
  markers: [] as string[],
  paths: [] as string[],
  entities: [] as string[],
  characters: [] as string[],
  dialogues: [] as string[],
  packages: [] as { id: string; template: string; description: string }[],
})

export function requestLookups(): void {
  sendToHost({ type: 'lookups:request' })
}

onHostMessage((message) => {
  if (message.type !== 'lookups') return
  for (const key of Object.keys(lookups) as (keyof typeof lookups)[]) {
    if (message[key] !== undefined) (lookups as Record<string, unknown>)[key] = message[key]
  }
})

const fileName = (path: string): string => path.split('/').pop() ?? path

// A record field's picker options by its `choices` (RecordField::choices), or a script field's.
export function optionsFor(choices: string | undefined, type?: string): Option[] | null {
  if (type === 'script') return lookups.scripts.map((s) => ({ value: s, label: fileName(s), hint: s }))
  switch (choices) {
    case 'paths':
      return lookups.paths.map((p) => ({ value: p, hint: 'path' }))
    case 'markers':
      return lookups.markers.map((m) => ({ value: m, hint: 'marker' }))
    case 'entities':
      return lookups.entities.map((e) => ({ value: e }))
    case 'characters':
      return lookups.characters.map((c) => ({ value: c, hint: 'character' }))
    case 'dialogues':
      return lookups.dialogues.map((d) => ({ value: d, label: fileName(d), hint: d }))
    case 'targets':
      return [
        { value: 'player', hint: 'the player' },
        ...lookups.characters.map((c) => ({ value: c, hint: 'character' })),
        ...lookups.markers.map((m) => ({ value: m, hint: 'marker' })),
      ]
    default:
      return null
  }
}
