// An animation sheet's picture and its first frame (from the Assets list): a thumbnail shows that one pose.
import { ref } from 'vue'
import { onHostMessage, sendToHost } from './ipc'

export interface SheetFrame {
  frame: [number, number, number, number] // x, y, w, h in the picture
  width: number
  height: number
}
export const sheetFrames = ref<Record<string, SheetFrame>>({})
let asked = false

onHostMessage((message) => {
  if (message.type !== 'assets:list') return
  const out: Record<string, SheetFrame> = {}
  for (const a of (message.assets as { path: string; frame?: [number, number, number, number]; width?: number; height?: number }[]) ?? []) {
    if (a.frame && a.width && a.height) out[a.path] = { frame: a.frame, width: a.width, height: a.height }
  }
  sheetFrames.value = out
})

export function requestAssetFrames(): void {
  if (asked) return
  asked = true
  sendToHost({ type: 'assets:request' })
}

// A sheet's picture whose frame the Assets list didn't give (a character the AI panel just made, a list sent before
// its sheet was read): its sheet file next to it (x.character.png → x.character.anim.json, x-sheet.png → x.anim.json)
// read over the project's file host, and its idle frame facing the camera (else its first clip's first) taken.
const PROJECT_HOST = 'https://aige-project.local/'
export const derivedFrames = ref<Record<string, SheetFrame | null>>({})
const asking = new Set<string>()

interface SheetFile {
  grid?: { width?: number; height?: number }
  size?: [number, number]
  clips?: Record<string, { directions?: Record<string, (number | { x: number; y: number; w: number; h: number })[]> }>
}

function sheetFileOf(path: string): string | null {
  if (path.endsWith('.character.png')) return path.replace(/\.png$/, '.anim.json')
  if (path.endsWith('-sheet.png')) return path.replace(/-sheet\.png$/, '.anim.json')
  return null
}

export function ensureFrame(path: string): void {
  const file = sheetFileOf(path)
  if (!file || path in derivedFrames.value || asking.has(path)) return
  asking.add(path)
  fetch(PROJECT_HOST + file)
    .then((r) => (r.ok ? r.json() : null))
    .then((sheet: SheetFile | null) => {
      let found: SheetFrame | null = null
      const gw = sheet?.grid?.width ?? 0, gh = sheet?.grid?.height ?? 0
      const [tw, th] = sheet?.size ?? [0, 0]
      const clips = sheet?.clips ?? {}
      const clip = clips.idle ?? Object.values(clips)[0]
      const dirs = clip?.directions ?? {}
      const first = (dirs.s ?? dirs[''] ?? Object.values(dirs)[0] ?? [])[0]
      if (first !== undefined && tw > 0 && th > 0) {
        if (typeof first === 'number' && gw > 0 && gh > 0) {
          const cols = Math.max(1, Math.floor(tw / gw))
          found = { frame: [(first % cols) * gw, Math.floor(first / cols) * gh, gw, gh], width: tw, height: th }
        } else if (typeof first === 'object') {
          found = { frame: [first.x, first.y, first.w, first.h], width: tw, height: th }
        }
      }
      derivedFrames.value = { ...derivedFrames.value, [path]: found }
    })
    .catch(() => (derivedFrames.value = { ...derivedFrames.value, [path]: null }))
    .finally(() => asking.delete(path))
}

// A sheet changed (a walk added): its frame read again next time.
onHostMessage((message) => {
  if (message.type === 'assets:list' || message.type === 'animations:list') derivedFrames.value = {}
})
