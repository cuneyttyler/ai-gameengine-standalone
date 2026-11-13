// Edit ▸ Search in project (Ctrl+Shift+F) and "Show uses" (ProjectSearch.cpp on the host): text across the project's
// scenes, prefabs, packages and records, story, scripts and UI, art by its file name; or what refers to one thing.
// Results come grouped by file, each opened where it's edited: a scene at the thing (scene:reveal), a record in
// Project ▸ Records, a quest in Story, art and prefabs selected in the Assets panel.
import { reactive } from 'vue'
import { editorState } from './editorState'
import { onHostMessage, sendToHost } from './ipc'
import { registerAction } from './keys'
import { entityMenuExtensions } from './sceneMenus'
import { activatePanel, setPanelVisible } from './layout'
import { showTab } from './paneltabs'
import { projectState } from './projects'

export type FileKind =
  | 'scene' | 'prefab' | 'package' | 'record' | 'quest' | 'dialogue' | 'schedule' | 'cutscene' | 'script'
  | 'world' | 'planner' | 'ui' | 'art' | 'module' | 'loc' | 'settings' | 'other'

export interface SearchHit {
  line: number
  text: string
  entity?: { index: number; name: string } // a scene's thing the line belongs to
}

export interface SearchFile {
  path: string
  kind: FileKind
  id: string // its file name without the extension: a record's, a quest's, a cutscene's id
  recordType?: string
  byName: boolean // its file name matched
  hits: SearchHit[]
}

// What "Show uses" looks for: a file's path, or a thing known by its id (a record, a package).
export interface UsesTarget {
  kind: FileKind
  path?: string
  id?: string
  label?: string
}

// The search's filters: each a group of kinds.
export const SEARCH_GROUPS: { id: string; label: string; kinds: FileKind[] }[] = [
  { id: 'scenes', label: 'Scenes', kinds: ['scene'] },
  { id: 'prefabs', label: 'Prefabs', kinds: ['prefab'] },
  { id: 'packages', label: 'Packages', kinds: ['package'] },
  { id: 'records', label: 'Records', kinds: ['record'] },
  { id: 'story', label: 'Story', kinds: ['quest', 'dialogue', 'schedule', 'cutscene', 'world', 'planner'] },
  { id: 'scripts', label: 'Scripts', kinds: ['script'] },
  { id: 'ui', label: 'UI', kinds: ['ui', 'loc'] },
  { id: 'art', label: 'Art', kinds: ['art'] },
  { id: 'other', label: 'Other', kinds: ['module', 'settings', 'other'] },
]

export const KIND_LABELS: Record<FileKind, string> = {
  scene: 'Scene', prefab: 'Prefab', package: 'Package', record: 'Record', quest: 'Quest', dialogue: 'Dialogue',
  schedule: 'Routine', cutscene: 'Cutscene', script: 'Script', world: 'World', planner: 'Planner', ui: 'UI',
  art: 'Art', module: 'Module', loc: 'Text', settings: 'Settings', other: 'File',
}

export const searchState = reactive({
  open: false,
  mode: 'search' as 'search' | 'uses',
  query: '',
  exact: false, // case and the whole query as typed
  groups: [] as string[], // the filters on; none: everything
  uses: null as UsesTarget | null,
  busy: false,
  files: [] as SearchFile[],
  truncated: false,
  searched: '', // the query the results are for
  focusAt: 0, // asks the dialog to focus its box
})

let seq = 0

export function openSearch(query?: string): void {
  searchState.open = true
  searchState.focusAt = performance.now()
  if (query !== undefined) {
    searchState.query = query
    runSearch()
  } else if (searchState.mode === 'uses') {
    runSearch() // back to the search's own results
  }
}

export function runSearch(): void {
  const query = searchState.query.trim()
  searchState.mode = 'search'
  if (!query) {
    searchState.files = []
    searchState.searched = ''
    return
  }
  searchState.busy = true
  const kinds = SEARCH_GROUPS.filter((g) => searchState.groups.includes(g.id)).flatMap((g) => g.kinds)
  sendToHost({ type: 'project:search', query, exact: searchState.exact, kinds, seq: ++seq })
}

// "Show uses": the search panel opened on what refers to it.
export function showUses(target: UsesTarget): void {
  searchState.open = true
  searchState.mode = 'uses'
  searchState.uses = target
  searchState.busy = true
  searchState.files = []
  sendToHost({ type: 'project:uses', kind: target.kind, path: target.path ?? '', id: target.id ?? '', label: target.label ?? '' })
}

export function closeSearch(): void {
  searchState.open = false
}

// A thing's right-click (viewport, Scene tree): what else uses its prefab.
entityMenuExtensions.push((entity) =>
  entity.prefab
    ? [{ label: 'Show uses of its prefab', action: () => showUses({ kind: 'prefab', path: entity.prefab!, label: entity.prefab! }) }]
    : [],
)

// Ctrl+Shift+F: opens it (on the words selected in the page, if any), or takes it away when it's open with its box in use.
registerAction('edit.searchProject', () => {
  const picked = window.getSelection()?.toString().trim() ?? ''
  if (searchState.open && !picked && document.activeElement?.closest('.project-search')) closeSearch()
  else openSearch(picked && picked.length < 80 && !picked.includes('\n') ? picked : undefined)
})

onHostMessage((message) => {
  if (message.type === 'project:searchResults') {
    if ((message.seq as number) !== seq) return // an older search's
    searchState.busy = false
    searchState.files = (message.files as SearchFile[]) ?? []
    searchState.truncated = Boolean(message.truncated)
    searchState.searched = (message.query as string) ?? ''
  } else if (message.type === 'project:usesResults') {
    if (searchState.mode !== 'uses') return
    searchState.busy = false
    searchState.files = (message.files as SearchFile[]) ?? []
    searchState.truncated = Boolean(message.truncated)
  }
})

// Where other panels pick up "open this" (each watches its own).
export const revealState = reactive({
  story: '', // quests/, dialogue/, schedules/ file: the Story tab opens it
  cutscene: '', // a cutscene's id: Project ▸ Effects ▸ Cutscenes opens it
  asset: '', // assets/ file: selected on the Assets tab
  prefab: '', // prefabs/ file: selected on the Prefabs tab
  at: 0, // when asked: the same thing asked again shows it again
})

// A file (and one of its lines) opened where it's edited.
export function openResult(file: SearchFile, hit?: SearchHit): void {
  const at = performance.now()
  switch (file.kind) {
    case 'scene':
      setPanelVisible('scene', true)
      if (hit?.entity) sendToHost({ type: 'scene:reveal', path: file.path, index: hit.entity.index, name: hit.entity.name })
      else if (!editorState.playing) sendToHost({ type: 'scene:open', path: file.path })
      break
    case 'prefab':
      Object.assign(revealState, { prefab: file.path, at })
      setPanelVisible('assets', true)
      activatePanel('assets')
      break
    case 'art':
      Object.assign(revealState, { asset: file.path, at })
      setPanelVisible('assets', true)
      activatePanel('assets')
      break
    case 'package':
    case 'record':
      showTab('records')
      activatePanel('project')
      editorState.openRecord = { type: file.recordType ?? (file.kind === 'package' ? 'package' : ''), id: file.id }
      break
    case 'quest':
    case 'dialogue':
    case 'schedule':
      Object.assign(revealState, { story: file.path, at })
      showTab('story')
      activatePanel('project')
      break
    case 'cutscene':
      Object.assign(revealState, { cutscene: file.id, at })
      showTab('fx')
      activatePanel('project')
      break
    case 'script':
      editorState.scriptEditor = file.path
      break
    case 'world':
      showTab('world')
      activatePanel('project')
      break
    case 'ui':
      showTab('ui')
      activatePanel('project')
      break
    case 'planner':
      setPanelVisible('planner', true)
      activatePanel('planner')
      break
    case 'settings':
      projectState.showProjectSettings = true
      break
    default:
      sendToHost({ type: 'asset:reveal', path: file.path }) // in Explorer
  }
}
