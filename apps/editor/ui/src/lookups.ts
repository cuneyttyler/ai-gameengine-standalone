// What the editor's pickers choose from (searchable selects): the project's scripts, dialogues and packages, and the
// open scene's markers, paths and named things. The host sends them (EditorLists.cpp) when asked; panels ask as they
// open, and after changes that add to them.
import { reactive } from 'vue'
import { onHostMessage, sendToHost } from './ipc'
import type { Option } from './components/SearchSelect.vue'
import { editorState } from './editorState'

export const lookups = reactive({
  scripts: [] as string[],
  scriptUsers: {} as Record<string, string[]>,
  markers: [] as string[],
  paths: [] as string[],
  entities: [] as string[],
  characters: [] as string[],
  dialogues: [] as string[],
  packages: [] as { id: string; template: string; description: string }[],
  templates: {} as Record<string, Record<string, unknown>>, // the built-in package templates' steps
  scenes: [] as string[],
  schedules: [] as string[],
  cutscenes: [] as string[],
  worldCharacters: [] as { key: string; name: string }[],
  records: {} as Record<string, string[]>, // record type -> ids
  actions: [] as string[], // the input actions
  uses: [] as string[], // what characters use things by: input actions, parts' and Reactions' names
  textures: [] as string[],
  audio: [] as string[],
})

// Asked for by every picker that shows: one request for a burst of them.
let asking = 0
export function requestLookups(): void {
  window.clearTimeout(asking)
  asking = window.setTimeout(() => sendToHost({ type: 'lookups:request' }), 60)
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
  if (type === 'texture') return lookups.textures.map((t) => ({ value: t, label: fileName(t), hint: t }))
  if (type === 'audio') return lookups.audio.map((t) => ({ value: t, label: fileName(t), hint: t }))
  if (type === 'prefab') return editorState.prefabs.map((p) => ({ value: p.path, label: p.path.replace(/^prefabs\//, '') }))
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
    case 'scenes':
      return lookups.scenes.map((x) => ({ value: x, label: fileName(x), hint: x }))
    case 'schedules':
      return lookups.schedules.map((x) => ({ value: x, label: fileName(x), hint: x }))
    case 'cutscenes':
      return lookups.cutscenes.map((x) => ({ value: x, hint: 'cutscene' }))
    case 'worldCharacters':
      return lookups.worldCharacters.map((c) => ({ value: c.key, label: c.name || c.key, hint: c.key }))
    case 'actions':
      return lookups.actions.map((a) => ({ value: a, hint: 'input action' }))
    case 'uses':
      return lookups.uses.map((a) => ({ value: a, hint: lookups.actions.includes(a) ? 'input action' : 'a part or Reaction' }))
    case 'targets':
      return [
        { value: 'player', hint: 'the player' },
        ...lookups.characters.map((c) => ({ value: c, hint: 'character' })),
        ...lookups.markers.map((m) => ({ value: m, hint: 'marker' })),
      ]
    default:
      if (choices?.startsWith('records:')) return (lookups.records[choices.slice(8)] ?? []).map((id) => ({ value: id, hint: choices.slice(8) }))
      return null
  }
}
