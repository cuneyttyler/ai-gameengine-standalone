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
