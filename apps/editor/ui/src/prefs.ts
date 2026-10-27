// The user's own preferences beyond the AI's and the projects' (File ▸ Preferences): kept by the host in prefs.json
// (prefs:set, prefs:values), applied where they act: the interface scale here, the rest in the host.
import { reactive } from 'vue'
import { onHostMessage, sendToHost } from './ipc'

export interface Prefs {
  uiScale: number // the whole interface, 0.8 to 1.5
  showGridAtStart: boolean // tile borders on empty ground when a scene opens
  zoomSpeed: number // how much one wheel step zooms the viewport (1.05 to 1.3)
  invertZoom: boolean // the wheel zooms the other way
  recoveryMinutes: number // how often unsaved changes are kept for a crash (0: never)
  paintModel: string // who paints a place whole when a project doesn't say (Project Settings ▸ AI ▸ paint)
  defaultBuildMethod: 'Assembled' | 'Paint' // what New Project starts with
}

export const prefs = reactive<Prefs>({
  uiScale: 1,
  showGridAtStart: true,
  zoomSpeed: 1.15,
  invertZoom: false,
  recoveryMinutes: 2,
  paintModel: 'flux-3-image',
  defaultBuildMethod: 'Paint',
})

export function setPref<K extends keyof Prefs>(key: K, value: Prefs[K]): void {
  prefs[key] = value
  if (key === 'uiScale') applyScale()
  sendToHost({ type: 'prefs:set', key, value })
}

function applyScale(): void {
  document.documentElement.style.zoom = String(prefs.uiScale)
}

onHostMessage((message) => {
  if (message.type !== 'prefs:values') return
  const values = (message.values ?? {}) as Partial<Prefs>
  for (const [k, v] of Object.entries(values)) {
    if (v !== null && v !== undefined && k in prefs) (prefs as Record<string, unknown>)[k] = v
  }
  applyScale()
})

export function requestPrefs(): void {
  sendToHost({ type: 'prefs:request' })
}
