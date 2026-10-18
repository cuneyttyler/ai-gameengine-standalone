// Dragging panels between tab groups (development_plan.md 6.8), and keeping
// track of where each group is on screen so the panels (one layer, never
// re-parented) can be laid over their group's body.
import { reactive } from 'vue'
import { isNoOp } from './docktree'
import { activatePanel, applyDrop, dockView, layout, saveLayout, type DropTarget, type PanelId, type Rect, type Side } from './layout'

export const TAB_HEIGHT = 26 // the tab strip on top of every group
const EDGE = 18 // px from the shell's border that dock along the whole edge
const SIDE_ZONE = 0.25 // a group's outer quarter on each side splits it
const START_DRAG = 6 // px of movement before a press on a tab becomes a drag

export const dragView = reactive({
  target: null as DropTarget | null,
  marker: null as Rect | null, // where the panel will go, in shell coordinates
  x: 0, // the pointer, in shell coordinates (for the label under it)
  y: 0,
})

// --- where the groups are ---------------------------------------------------

const groupEls = new Map<string, HTMLElement>()
let shellEl: HTMLElement | null = null
let pending = false
const observer = new ResizeObserver(() => scheduleMeasure())

export function setShell(el: HTMLElement | null): void {
  if (shellEl) observer.unobserve(shellEl)
  shellEl = el
  if (el) observer.observe(el)
  scheduleMeasure()
}

export function registerGroup(id: string, el: HTMLElement): void {
  const old = groupEls.get(id)
  if (old && old !== el) observer.unobserve(old)
  groupEls.set(id, el)
  observer.observe(el)
  scheduleMeasure()
}

export function unregisterGroup(id: string, el: HTMLElement): void {
  if (groupEls.get(id) === el) {
    groupEls.delete(id)
    observer.unobserve(el)
    scheduleMeasure()
  }
}

function measure(): void {
  pending = false
  if (!shellEl) return
  const shell = shellEl.getBoundingClientRect()
  const rects: Record<string, Rect> = {}
  for (const [id, el] of groupEls) {
    const r = el.getBoundingClientRect()
    rects[id] = { x: r.left - shell.left, y: r.top - shell.top, width: r.width, height: r.height }
  }
  dockView.groupRects = rects
  // Things positioned over the page (the native viewport window) follow even
  // when their size didn't change.
  window.dispatchEvent(new Event('aige:layout'))
}

export function scheduleMeasure(): void {
  if (!pending) {
    pending = true
    requestAnimationFrame(measure)
  }
}
window.addEventListener('resize', scheduleMeasure)

// The body of a group (below its tabs), where its front panel is drawn.
export function bodyRect(groupId: string): Rect | null {
  const r = dockView.groupRects[groupId]
  if (!r) return null
  return { x: r.x, y: r.y + TAB_HEIGHT, width: r.width, height: Math.max(0, r.height - TAB_HEIGHT) }
}

// --- hit testing ----------------------------------------------------------------

function sideMarker(area: Rect, side: Side, share: number): Rect {
  switch (side) {
    case 'left':
      return { x: area.x, y: area.y, width: area.width * share, height: area.height }
    case 'right':
      return { x: area.x + area.width * (1 - share), y: area.y, width: area.width * share, height: area.height }
    case 'top':
      return { x: area.x, y: area.y, width: area.width, height: area.height * share }
    default:
      return { x: area.x, y: area.y + area.height * (1 - share), width: area.width, height: area.height * share }
  }
}

// Which drop a pointer at (x, y) in shell coordinates means, and where to show it.
export function hitTest(x: number, y: number, shellWidth: number, shellHeight: number): { target: DropTarget; marker: Rect } | null {
  const whole: Rect = { x: 0, y: 0, width: shellWidth, height: shellHeight }
  if (x < EDGE) return { target: { kind: 'edge', side: 'left' }, marker: sideMarker(whole, 'left', 0.22) }
  if (x > shellWidth - EDGE) return { target: { kind: 'edge', side: 'right' }, marker: sideMarker(whole, 'right', 0.22) }
  if (y < EDGE) return { target: { kind: 'edge', side: 'top' }, marker: sideMarker(whole, 'top', 0.22) }
  if (y > shellHeight - EDGE) return { target: { kind: 'edge', side: 'bottom' }, marker: sideMarker(whole, 'bottom', 0.22) }

  for (const [id, r] of Object.entries(dockView.groupRects)) {
    if (x < r.x || x > r.x + r.width || y < r.y || y > r.y + r.height) continue
    if (y < r.y + TAB_HEIGHT) {
      // On the tab strip: between the tabs, at the pointer.
      const el = groupEls.get(id)
      const tabs = el ? Array.from(el.querySelectorAll<HTMLElement>('[data-tab]')) : []
      const shell = shellEl?.getBoundingClientRect()
      let index = 0
      let at = r.x + 2
      for (const tab of tabs) {
        const t = tab.getBoundingClientRect()
        const left = t.left - (shell?.left ?? 0)
        if (x > left + t.width / 2) {
          index++
          at = left + t.width
        }
      }
      return { target: { kind: 'tab', group: id, index }, marker: { x: at - 1, y: r.y + 3, width: 3, height: TAB_HEIGHT - 6 } }
    }
    const body = bodyRect(id)!
    const fx = (x - body.x) / Math.max(1, body.width)
    const fy = (y - body.y) / Math.max(1, body.height)
    const distances: [Side, number][] = [
      ['left', fx],
      ['right', 1 - fx],
      ['top', fy],
      ['bottom', 1 - fy],
    ]
    const [side, d] = distances.reduce((a, b) => (b[1] < a[1] ? b : a))
    if (d < SIDE_ZONE) return { target: { kind: 'side', group: id, side }, marker: sideMarker(body, side, 0.5) }
    return { target: { kind: 'tab', group: id }, marker: { x: body.x + 4, y: body.y + 4, width: body.width - 8, height: body.height - 8 } }
  }
  return null
}

// --- the drag ---------------------------------------------------------------

// A press on a tab: a click activates it; a drag moves the panel.
export function pressTab(panel: PanelId, event: PointerEvent): void {
  if (event.button !== 0 || !shellEl) return
  const handle = event.currentTarget as HTMLElement
  const startX = event.clientX
  const startY = event.clientY
  let dragging = false
  handle.setPointerCapture(event.pointerId)
  event.preventDefault()

  const update = (e: PointerEvent): void => {
    const shell = shellEl!.getBoundingClientRect()
    dragView.x = e.clientX - shell.left
    dragView.y = e.clientY - shell.top
    const hit = hitTest(dragView.x, dragView.y, shell.width, shell.height)
    if (hit && !isNoOp(layout.root, panel, hit.target)) {
      dragView.target = hit.target
      dragView.marker = hit.marker
    } else {
      dragView.target = null
      dragView.marker = null
    }
  }
  const finish = (drop: boolean): void => {
    handle.removeEventListener('pointermove', move)
    handle.removeEventListener('pointerup', up)
    handle.removeEventListener('pointercancel', cancel)
    window.removeEventListener('keydown', key, true)
    if (handle.hasPointerCapture(event.pointerId)) handle.releasePointerCapture(event.pointerId)
    const target = dragView.target
    dragView.target = null
    dragView.marker = null
    dockView.dragging = null
    if (dragging && drop && target) applyDrop(panel, target)
    else if (!dragging && drop) activatePanel(panel)
    saveLayout()
  }
  const move = (e: PointerEvent): void => {
    if (!dragging) {
      if (Math.hypot(e.clientX - startX, e.clientY - startY) < START_DRAG) return
      dragging = true
      dockView.dragging = panel
    }
    update(e)
  }
  const up = (): void => finish(true)
  const cancel = (): void => finish(false)
  const key = (e: KeyboardEvent): void => {
    if (e.key === 'Escape') {
      e.preventDefault()
      e.stopPropagation()
      finish(false)
    }
  }
  handle.addEventListener('pointermove', move)
  handle.addEventListener('pointerup', up)
  handle.addEventListener('pointercancel', cancel)
  window.addEventListener('keydown', key, true)
}
