// The native viewport window sits on top of the web page, so anything HTML
// drawn over the viewport's area would be hidden under it. Open menus and
// dialogs register their element here; their rectangles are sent to the host,
// which cuts them out of the viewport window (development_plan.md 4.1).
import { onBeforeUnmount, onMounted, type Ref } from 'vue'
import { sendToHost } from './ipc'

const tracked = new Set<HTMLElement>()
const observer = new ResizeObserver(() => schedule())
let pending = false

function send(): void {
  pending = false
  const rects: number[][] = []
  for (const el of tracked) {
    const r = el.getBoundingClientRect()
    if (r.width > 0 && r.height > 0) {
      rects.push([Math.floor(r.left), Math.floor(r.top), Math.ceil(r.width) + 1, Math.ceil(r.height) + 1])
    }
  }
  sendToHost({ type: 'viewport:occlude', rects })
}

function schedule(): void {
  if (!pending) {
    pending = true
    requestAnimationFrame(send)
  }
}

window.addEventListener('resize', schedule)

export function trackOverlay(el: HTMLElement): void {
  tracked.add(el)
  observer.observe(el)
  schedule()
}

export function untrackOverlay(el: HTMLElement): void {
  tracked.delete(el)
  observer.unobserve(el)
  schedule()
}

// For components that are mounted only while open (v-if): their root element
// is cut out of the viewport for as long as they exist.
export function useOverlay(el: Ref<HTMLElement | null>): void {
  onMounted(() => el.value && trackOverlay(el.value))
  onBeforeUnmount(() => el.value && untrackOverlay(el.value))
}
