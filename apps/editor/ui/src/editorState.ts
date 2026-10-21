// Mirror of the host's editor state. The C++ side (apps/editor/EditorApp)
// is the single source of truth; components read this and send requests,
// never mutate it directly.
import { reactive } from 'vue'
import { onHostMessage, sendToHost } from './ipc'
import { setPanelVisible } from './layout'

// Everything placeable is a prefab, so there are three tools: pick, erase,
// and the prefab brush (paints tile prefabs, places everything else); and the
// height brush, which raises, lowers or levels the ground.
export type ToolId = 'select' | 'erase' | 'prefab' | 'height'

export interface Prefab {
  path: string
  name: string
  error: string
  isTile: boolean
  isPlayer: boolean
  kind: 'tile' | 'character' | 'item' | 'other'
  flat: boolean // top-down art lying on the grid (5.2)
  terrain: string | null // an analyzed ground tile: which terrain it paints with Auto-fit (6.4)
  texture: string
  uv: [number, number, number, number]
  size: [number, number]
}

export const editorState = reactive({
  tool: 'select' as ToolId,
  brush: '', // prefab path for the 'prefab' tool
  heightMode: 'raise' as 'raise' | 'lower' | 'flatten', // the height brush
  heightRadius: 2,
  playing: false,
  dirty: false,
  canUndo: false,
  canRedo: false,
  sceneName: '',
  selectedId: null as number | null,
  notice: '',
  prefabs: [] as Prefab[],
  ai: {
    state: 'off' as 'off' | 'starting' | 'ready' | 'failed',
    text: 'AI is off',
    busy: false,
    enabled: false,
    // 'undecided' until the user answers the first-run question; until then nothing runs.
    choice: 'undecided' as 'undecided' | 'on' | 'off',
    reviewPolicy: 'reviewAll' as 'reviewAll' | 'autoApplyTweaks',
    provider: 'mistral' as string, // the model that writes: mistral or claude
    model: '',
    providerError: '',
    available: {} as Record<string, boolean>,
    artProvider: 'auto' as string, // what draws the art: auto (follows the LLM), mistral or pixellab
    art: '', // the generator in use
    artError: '',
    models: {} as Record<string, string[]>, // each provider's models (Preferences ▸ AI ▸ Model)
    modelsError: '',
  },
  // Set by an Inspector field's "Ask AI"; the AI panel picks it up (n makes repeats distinct).
  aiAsk: { focus: '', n: 0 },
  animateFrom: '', // Generate ▸ Character's "Animate": the image the Animate tab picks
  showShortcuts: false, // Help ▸ Keyboard Shortcuts
  showOntology: false, // File ▸ Ontology…, the Build panel's menu (development_plan.md 9)
  replacePrefab: null as string | null, // Edit ▸ Replace prefab…: the prefab to replace ('' : choose)
  grid: false, // tile borders on empty ground (G)
  brushScale: 1, // the prefab brush places copies at this size
  brushRotation: 0, // quarter turns for flat things the brush places (R)
  autotile: true, // Auto-fit: terrain tiles pick their pieces (6.4)
  hasSave: false, // a saved game exists (Play ▸ Continue from save, 6.9)
})

// The AI panel's ✕ and the AI rail (the panel itself lives in layout.ts).
export function setAiOpen(open: boolean): void {
  setPanelVisible('ai', open)
}

// Turning AI off stops the service, so nothing is sent to the model.
export function setAiEnabled(enabled: boolean): void {
  sendToHost({ type: 'ai:setEnabled', enabled })
}

export function askAiAbout(focus: string): void {
  setAiOpen(true)
  editorState.aiAsk = { focus, n: editorState.aiAsk.n + 1 }
}

export function armPrefab(path: string): void {
  sendToHost({ type: 'tool:set', tool: 'prefab', prefab: path })
}

let noticeTimer: number | undefined

onHostMessage((message) => {
  if (message.type === 'editor:state') {
    editorState.tool = message.tool as ToolId
    editorState.brush = message.brush as string
    editorState.heightMode = ((message.heightMode as string) ?? 'raise') as 'raise' | 'lower' | 'flatten'
    editorState.heightRadius = (message.heightRadius as number) ?? 2
    editorState.playing = message.playing as boolean
    editorState.dirty = message.dirty as boolean
    editorState.canUndo = message.canUndo as boolean
    editorState.canRedo = message.canRedo as boolean
    editorState.sceneName = message.sceneName as string
    editorState.grid = (message.grid as boolean) ?? false
    editorState.brushScale = (message.brushScale as number) ?? 1
    editorState.brushRotation = (message.brushRotation as number) ?? 0
    editorState.autotile = (message.autotile as boolean) ?? true
    editorState.hasSave = (message.hasSave as boolean) ?? false
  } else if (message.type === 'selection:changed') {
    editorState.selectedId = (message.id as number | null) ?? null
  } else if (message.type === 'ai:status') {
    editorState.ai.state = message.state as typeof editorState.ai.state
    editorState.ai.text = message.text as string
    editorState.ai.enabled = message.enabled as boolean
    editorState.ai.choice = message.choice as typeof editorState.ai.choice
    editorState.ai.reviewPolicy = message.reviewPolicy as typeof editorState.ai.reviewPolicy
    editorState.ai.provider = (message.provider as string) ?? 'mistral'
    editorState.ai.model = (message.model as string) ?? ''
    editorState.ai.providerError = (message.providerError as string) ?? ''
    editorState.ai.available = (message.available as Record<string, boolean>) ?? {}
    editorState.ai.artProvider = (message.artProvider as string) ?? 'auto'
    editorState.ai.art = (message.art as string) ?? ''
    editorState.ai.artError = (message.artError as string) ?? ''
  } else if (message.type === 'ai:models') {
    const provider = (message.provider as string) ?? editorState.ai.provider
    editorState.ai.models = { ...editorState.ai.models, [provider]: (message.models as string[]) ?? [] }
    editorState.ai.modelsError = message.ok === false ? ((message.errors as string[])?.[0] ?? '') : ''
  } else if (message.type === 'ai:busy') {
    editorState.ai.busy = message.busy as boolean
  } else if (message.type === 'prefabs:list') {
    editorState.prefabs = message.prefabs as Prefab[]
  } else if (message.type === 'notify') {
    editorState.notice = message.text as string
    window.clearTimeout(noticeTimer)
    noticeTimer = window.setTimeout(() => (editorState.notice = ''), 3500)
  }
})

