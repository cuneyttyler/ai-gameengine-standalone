<script setup lang="ts">
// Build ⑤ Mark (docs/places/painted-places.md §3): over the place's painting, what can be walked on (cells,
// walkable or blocked) and the occluders (the parts drawn again over a character behind them: a region and the base
// line where it meets the ground). Pre-filled from the plan by the host (PrefillMarks); the user trims and fixes.
// Masks are kept as canvases the size of their box, grown as they're painted past it; only the changed ones are sent.
import { computed, nextTick, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue'
import { onHostMessage, sendToHost } from '../ipc'

const PROJECT_HOST = 'https://aige-project.local/'

interface View { x0: number; y0: number; x1: number; y1: number; tw: number; th: number; top: number }
interface Paint { file?: string; size?: [number, number]; view?: View; paintedAt?: string }
interface Occluder {
  id: string
  name: string
  kind?: string
  object?: string
  line?: string
  mask?: string
  box: [number, number, number, number]
  base: [number, number][]
  prefilled?: boolean
  edited?: boolean
  hidden?: boolean
}
interface Marks { walk?: { x0: number; y0: number; w: number; h: number; rows: string[] }; occluders?: Occluder[]; savedAt?: string; paintedAt?: string }

const props = defineProps<{ place: string; paint: Paint }>()
const emit = defineEmits<{ saved: [] }>()

// --- The painting's view: cells to pixels and back (PaintView) ---
const view = computed<View>(() => props.paint.view ?? { x0: 0, y0: 0, x1: 1, y1: 1, tw: 64, th: 32, top: 0 })
function pixelOf(x: number, y: number): [number, number] {
  const v = view.value
  const ch = v.y1 - v.y0
  return [(x - v.x0 - (y - v.y0)) * v.tw / 2 + ch * v.tw / 2, (x - v.x0 + (y - v.y0)) * v.th / 2 + v.top]
}
function cellOf(px: number, py: number): [number, number] {
  const v = view.value
  const ch = v.y1 - v.y0
  const a = (px - ch * v.tw / 2) / (v.tw / 2)
  const b = (py - v.top) / (v.th / 2)
  return [Math.floor(v.x0 + (a + b) / 2), Math.floor(v.y0 + (b - a) / 2)]
}

// --- State ---
const loaded = ref(false)
const layer = ref<'walk' | 'occluders'>('occluders')
const tool = ref<'select' | 'sam' | 'brush' | 'erase' | 'base' | 'block' | 'rect' | 'fill'>('select')
// Click-to-select: waiting for the AI service's mask (the click, and whether it takes away).
const segmenting = ref<{ subtract: boolean } | null>(null)
const segmentError = ref('')
const brush = ref(16) // painting pixels
const cellBrush = ref(1) // cells
const opacity = reactive({ walk: 0.45, occluders: 0.45 })
const walk = ref<string[][]>([]) // [y][x]: '.' walkable, '#' blocked
const occluders = ref<Occluder[]>([])
const selectedId = ref('')
const selected = computed(() => occluders.value.find((o) => o.id === selectedId.value))
const dirty = new Set<string>() // masks to send
const removed = new Set<string>()
const changed = ref(false)
const masks = new Map<string, HTMLCanvasElement>()
const painting = new Image()
const paintingSize = computed<[number, number]>(() => props.paint.size ?? [painting.naturalWidth, painting.naturalHeight])
// A cell's mark ('.' walkable, '#' blocked), or '' off the grid; and set where it's on the grid.
function cellAt(x: number, y: number): string {
  return walk.value[y]?.[x] ?? ''
}
function setCell(x: number, y: number, value: '#' | '.'): void {
  const row = walk.value[y]
  if (row && x >= 0 && x < row.length) row[x] = value
}

function colourOf(id: string): string {
  let h = 0
  for (const c of id) h = (h * 31 + c.charCodeAt(0)) % 360
  return `hsl(${(h * 47) % 360} 80% 55%)`
}

// --- Loading ---
function load(marks: Marks): void {
  const w = marks.walk
  walk.value = w ? w.rows.map((r) => r.split('')) : []
  occluders.value = (marks.occluders ?? []).map((o) => ({ ...o, base: o.base.map((p) => [p[0], p[1]] as [number, number]) }))
  masks.clear()
  dirty.clear()
  removed.clear()
  changed.value = false
  undoStack.length = 0
  redoStack.length = 0
  const stamp = encodeURIComponent(marks.savedAt ?? marks.paintedAt ?? '')
  for (const o of occluders.value) {
    const canvas = document.createElement('canvas')
    canvas.width = Math.max(1, o.box[2])
    canvas.height = Math.max(1, o.box[3])
    masks.set(o.id, canvas)
    if (!o.mask) continue
    const img = new Image()
    img.onload = () => {
      const ctx = canvas.getContext('2d')!
      ctx.drawImage(img, 0, 0)
      // Its own colour, wherever it's marked (the engine reads only the alpha).
      ctx.globalCompositeOperation = 'source-in'
      ctx.fillStyle = colourOf(o.id)
      ctx.fillRect(0, 0, canvas.width, canvas.height)
      ctx.globalCompositeOperation = 'source-over'
      draw()
    }
    img.src = `${PROJECT_HOST}places/${props.place}/${o.mask}?v=${stamp}`
  }
  if (!occluders.value.some((o) => o.id === selectedId.value)) selectedId.value = occluders.value[0]?.id ?? ''
  loaded.value = true
  draw()
}
onHostMessage((m) => {
  if (m.type === 'build:marks' && m.place === props.place) load((m.marks as Marks) ?? {})
  if (m.type === 'build:segmented') applySegment(m as unknown as { ok: boolean; png?: string; box?: number[]; errors?: string[] })
})
// The mask the service found (white where the thing is) added to the selected occluder, or taken from it.
function applySegment(answer: { ok: boolean; png?: string; box?: number[]; errors?: string[] }): void {
  const asked = segmenting.value
  segmenting.value = null
  const o = selected.value
  if (!answer.ok || !answer.png || !answer.box || !o || !asked) {
    segmentError.value = answer.errors?.[0] ?? (o ? '' : 'Select an occluder first')
    return
  }
  segmentError.value = ''
  const [x, y, w, h] = answer.box as [number, number, number, number]
  if (w <= 0 || h <= 0) return
  const img = new Image()
  img.onload = () => {
    // Its white as alpha, in the occluder's colour.
    const piece = document.createElement('canvas')
    piece.width = w
    piece.height = h
    const pc = piece.getContext('2d')!
    pc.drawImage(img, -x, -y)
    const data = pc.getImageData(0, 0, w, h)
    for (let i = 0; i < data.data.length; i += 4) {
      const on = (data.data[i] ?? 0) > 127
      data.data[i + 3] = on ? 255 : 0
    }
    pc.putImageData(data, 0, 0)
    pc.globalCompositeOperation = 'source-in'
    pc.fillStyle = colourOf(o.id)
    pc.fillRect(0, 0, w, h)
    remember(snapshot('mask', o.id))
    if (!asked.subtract) grow(o, x, y, x + w, y + h)
    const ctx = masks.get(o.id)!.getContext('2d')!
    ctx.globalCompositeOperation = asked.subtract ? 'destination-out' : 'source-over'
    ctx.drawImage(piece, x - o.box[0], y - o.box[1])
    ctx.globalCompositeOperation = 'source-over'
    o.edited = true
    dirty.add(o.id)
    draw()
  }
  img.src = `data:image/png;base64,${answer.png}`
}

// --- The canvas: pan and zoom ---
const host = ref<HTMLDivElement | null>(null)
const canvas = ref<HTMLCanvasElement | null>(null)
const cam = reactive({ s: 0.5, x: 0, y: 0 })
function fit(): void {
  const el = host.value
  const [w, h] = paintingSize.value
  if (!el || !w || !h) return
  cam.s = Math.min(el.clientWidth / w, el.clientHeight / h) * 0.95
  cam.x = (el.clientWidth - w * cam.s) / 2
  cam.y = (el.clientHeight - h * cam.s) / 2
  draw()
}
function toImage(e: { clientX: number; clientY: number }): [number, number] {
  const r = canvas.value!.getBoundingClientRect()
  return [(e.clientX - r.left - cam.x) / cam.s, (e.clientY - r.top - cam.y) / cam.s]
}
let raf = 0
function draw(): void {
  if (raf) return
  raf = requestAnimationFrame(() => {
    raf = 0
    paint()
  })
}
function paint(): void {
  const c = canvas.value
  const el = host.value
  if (!c || !el) return
  if (c.width !== el.clientWidth || c.height !== el.clientHeight) {
    c.width = el.clientWidth
    c.height = el.clientHeight
  }
  const ctx = c.getContext('2d')!
  ctx.setTransform(1, 0, 0, 1, 0, 0)
  ctx.fillStyle = '#151515'
  ctx.fillRect(0, 0, c.width, c.height)
  ctx.setTransform(cam.s, 0, 0, cam.s, cam.x, cam.y)
  ctx.imageSmoothingEnabled = cam.s < 1
  if (painting.complete && painting.naturalWidth) ctx.drawImage(painting, 0, 0, paintingSize.value[0], paintingSize.value[1])
  if (layer.value === 'walk') {
    // Blocked cells red, walkable faintly outlined.
    ctx.globalAlpha = opacity.walk
    for (let y = 0; y < walk.value.length; y++) {
      const row = walk.value[y] ?? []
      for (let x = 0; x < row.length; x++) {
        const blocked = row[x] === '#'
        const a = pixelOf(x, y), b = pixelOf(x + 1, y), d = pixelOf(x + 1, y + 1), e = pixelOf(x, y + 1)
        ctx.beginPath()
        ctx.moveTo(a[0], a[1]); ctx.lineTo(b[0], b[1]); ctx.lineTo(d[0], d[1]); ctx.lineTo(e[0], e[1]); ctx.closePath()
        if (blocked) {
          ctx.fillStyle = '#e03030'
          ctx.fill()
        } else {
          ctx.strokeStyle = 'rgba(120, 220, 120, 0.5)'
          ctx.lineWidth = 1 / cam.s
          ctx.stroke()
        }
      }
    }
    ctx.globalAlpha = 1
    if (rectCells.value) {
      const [ax, ay, bx, by] = rectCells.value
      const x0 = Math.min(ax, bx), y0 = Math.min(ay, by), x1 = Math.max(ax, bx) + 1, y1 = Math.max(ay, by) + 1
      const a = pixelOf(x0, y0), b = pixelOf(x1, y0), d = pixelOf(x1, y1), e = pixelOf(x0, y1)
      ctx.strokeStyle = '#fff'
      ctx.lineWidth = 2 / cam.s
      ctx.beginPath()
      ctx.moveTo(a[0], a[1]); ctx.lineTo(b[0], b[1]); ctx.lineTo(d[0], d[1]); ctx.lineTo(e[0], e[1]); ctx.closePath()
      ctx.stroke()
    }
  } else {
    for (const o of occluders.value) {
      if (o.hidden) continue
      const m = masks.get(o.id)
      if (!m) continue
      ctx.globalAlpha = o.id === selectedId.value ? Math.min(1, opacity.occluders + 0.25) : opacity.occluders
      ctx.drawImage(m, o.box[0], o.box[1])
    }
    ctx.globalAlpha = 1
    const s = selected.value
    if (s) {
      ctx.strokeStyle = '#fff'
      ctx.setLineDash([4 / cam.s, 4 / cam.s])
      ctx.lineWidth = 1 / cam.s
      ctx.strokeRect(s.box[0], s.box[1], s.box[2], s.box[3])
      ctx.setLineDash([])
      // Its base line: where it meets the ground (a character behind it goes under the region).
      ctx.strokeStyle = '#ffd400'
      ctx.lineWidth = 2.5 / cam.s
      ctx.beginPath()
      s.base.forEach((p, i) => (i ? ctx.lineTo(p[0], p[1]) : ctx.moveTo(p[0], p[1])))
      ctx.stroke()
      for (const p of s.base) {
        ctx.fillStyle = '#ffd400'
        ctx.fillRect(p[0] - 4 / cam.s, p[1] - 4 / cam.s, 8 / cam.s, 8 / cam.s)
      }
    }
  }
  // The brush's outline.
  if (hover.value && (tool.value === 'brush' || tool.value === 'erase')) {
    ctx.strokeStyle = tool.value === 'erase' ? '#ff8080' : '#fff'
    ctx.lineWidth = 1 / cam.s
    ctx.beginPath()
    ctx.arc(hover.value[0], hover.value[1], brush.value / 2, 0, Math.PI * 2)
    ctx.stroke()
  }
}
const hover = ref<[number, number] | null>(null)

// --- Undo ---
type Step = { kind: 'mask'; id: string; box: [number, number, number, number]; data: HTMLCanvasElement; base: [number, number][]; edited?: boolean }
  | { kind: 'walk'; rows: string[][] }
  | { kind: 'list'; list: Occluder[]; masks: Map<string, HTMLCanvasElement> }
const undoStack: Step[] = []
const redoStack: Step[] = []
function cloneCanvas(c: HTMLCanvasElement): HTMLCanvasElement {
  const n = document.createElement('canvas')
  n.width = c.width
  n.height = c.height
  n.getContext('2d')!.drawImage(c, 0, 0)
  return n
}
function snapshot(kind: 'mask' | 'walk' | 'list', id = ''): Step {
  if (kind === 'walk') return { kind, rows: walk.value.map((r) => [...r]) }
  if (kind === 'list') return { kind, list: occluders.value.map((o) => ({ ...o, base: o.base.map((p) => [...p] as [number, number]) })), masks: new Map(masks) }
  const o = occluders.value.find((x) => x.id === id)!
  return { kind, id, box: [...o.box] as [number, number, number, number], data: cloneCanvas(masks.get(id)!), base: o.base.map((p) => [...p] as [number, number]), edited: o.edited }
}
function remember(step: Step): void {
  undoStack.push(step)
  if (undoStack.length > 60) undoStack.shift()
  redoStack.length = 0
  changed.value = true
}
function restore(step: Step): Step {
  if (step.kind === 'walk') {
    const now = snapshot('walk')
    walk.value = step.rows
    return now
  }
  if (step.kind === 'list') {
    const now = snapshot('list')
    occluders.value = step.list
    masks.clear()
    step.masks.forEach((v, k) => masks.set(k, v))
    if (!occluders.value.some((o) => o.id === selectedId.value)) selectedId.value = occluders.value[0]?.id ?? ''
    return now
  }
  const o = occluders.value.find((x) => x.id === step.id)
  if (!o) return step
  const now = snapshot('mask', step.id)
  o.box = step.box
  o.base = step.base
  o.edited = step.edited
  masks.set(step.id, step.data)
  dirty.add(step.id)
  return now
}
function undo(): void {
  const s = undoStack.pop()
  if (!s) return
  redoStack.push(restore(s))
  changed.value = true
  draw()
}
function redo(): void {
  const s = redoStack.pop()
  if (!s) return
  undoStack.push(restore(s))
  changed.value = true
  draw()
}

// --- Occluders: brush and eraser ---
function grow(o: Occluder, x0: number, y0: number, x1: number, y1: number): void {
  // The box widened to hold [x0, x1) x [y0, y1), with room to spare, within the painting.
  const [W, H] = paintingSize.value
  const [bx, by, bw, bh] = o.box
  if (x0 >= bx && y0 >= by && x1 <= bx + bw && y1 <= by + bh) return
  const nx0 = Math.max(0, Math.floor(Math.min(bx, x0 - 32))), ny0 = Math.max(0, Math.floor(Math.min(by, y0 - 32)))
  const nx1 = Math.min(W, Math.ceil(Math.max(bx + bw, x1 + 32))), ny1 = Math.min(H, Math.ceil(Math.max(by + bh, y1 + 32)))
  const n = document.createElement('canvas')
  n.width = Math.max(1, nx1 - nx0)
  n.height = Math.max(1, ny1 - ny0)
  const old = masks.get(o.id)
  if (old) n.getContext('2d')!.drawImage(old, bx - nx0, by - ny0)
  masks.set(o.id, n)
  o.box = [nx0, ny0, n.width, n.height]
}
function stroke(o: Occluder, from: [number, number], to: [number, number], erase: boolean): void {
  const r = brush.value / 2
  if (!erase) grow(o, Math.min(from[0], to[0]) - r, Math.min(from[1], to[1]) - r, Math.max(from[0], to[0]) + r, Math.max(from[1], to[1]) + r)
  const ctx = masks.get(o.id)!.getContext('2d')!
  ctx.globalCompositeOperation = erase ? 'destination-out' : 'source-over'
  ctx.strokeStyle = ctx.fillStyle = colourOf(o.id)
  ctx.lineWidth = brush.value
  ctx.lineCap = 'round'
  ctx.beginPath()
  ctx.moveTo(from[0] - o.box[0], from[1] - o.box[1])
  ctx.lineTo(to[0] - o.box[0], to[1] - o.box[1])
  ctx.stroke()
  ctx.globalCompositeOperation = 'source-over'
  o.edited = true
  dirty.add(o.id)
}
function occluderAt(p: [number, number]): Occluder | undefined {
  for (let i = occluders.value.length - 1; i >= 0; i--) {
    const o = occluders.value[i]
    if (!o || o.hidden) continue
    const [bx, by, bw, bh] = o.box
    if (p[0] < bx || p[1] < by || p[0] >= bx + bw || p[1] >= by + bh) continue
    const a = masks.get(o.id)?.getContext('2d')?.getImageData(Math.floor(p[0] - bx), Math.floor(p[1] - by), 1, 1).data[3] ?? 0
    if (a > 127) return o
  }
  return undefined
}
function newOccluder(): void {
  remember(snapshot('list'))
  let n = occluders.value.length + 1
  while (occluders.value.some((o) => o.id === `u${n}`)) n++
  const id = `u${n}`
  const v = view.value
  const [W, H] = paintingSize.value
  const c = document.createElement('canvas')
  c.width = c.height = 1
  masks.set(id, c)
  occluders.value.push({ id, name: `occluder ${n}`, box: [Math.floor(W / 2), Math.floor(H / 2), 1, 1], base: [[W / 2 - v.tw / 2, H / 2], [W / 2 + v.tw / 2, H / 2]], prefilled: false, edited: true })
  dirty.add(id)
  selectedId.value = id
  layer.value = 'occluders'
  tool.value = 'brush'
  draw()
}
function removeOccluder(id: string): void {
  remember(snapshot('list'))
  occluders.value = occluders.value.filter((o) => o.id !== id)
  masks.delete(id)
  dirty.delete(id)
  removed.add(id)
  if (selectedId.value === id) selectedId.value = occluders.value[0]?.id ?? ''
  draw()
}

// --- Base line ---
function handleAt(o: Occluder, p: [number, number]): number {
  const r = 8 / cam.s
  return o.base.findIndex((q) => Math.abs(q[0] - p[0]) <= r && Math.abs(q[1] - p[1]) <= r)
}
function sortBase(o: Occluder): void {
  o.base.sort((a, b) => a[0] - b[0])
}

// --- Walkable cells ---
function setCells(cx: number, cy: number, value: '#' | '.'): void {
  const k = cellBrush.value
  const h = Math.floor((k - 1) / 2)
  for (let y = cy - h; y < cy - h + k; y++) {
    for (let x = cx - h; x < cx - h + k; x++) {
      setCell(x, y, value)
    }
  }
}
function fillCells(cx: number, cy: number, value: '#' | '.'): void {
  const from = cellAt(cx, cy)
  if (!from || from === value) return
  const todo: [number, number][] = [[cx, cy]]
  while (todo.length) {
    const [x, y] = todo.pop()!
    if (cellAt(x, y) !== from) continue
    setCell(x, y, value)
    todo.push([x + 1, y], [x - 1, y], [x, y + 1], [x, y - 1])
  }
}

// --- Pointer ---
let drag: null | { mode: 'pan' | 'paint' | 'handle' | 'rect'; last: [number, number]; screen: [number, number]; erase?: boolean; handle?: number; start?: [number, number] } = null
const rectCells = ref<[number, number, number, number] | null>(null)
let space = false
function onDown(e: PointerEvent): void {
  canvas.value!.setPointerCapture(e.pointerId)
  const p = toImage(e)
  if (e.button === 1 || space) {
    drag = { mode: 'pan', last: p, screen: [e.clientX, e.clientY] }
    return
  }
  const alt = e.altKey || e.button === 2
  if (layer.value === 'walk') {
    const [cx, cy] = cellOf(p[0], p[1])
    remember(snapshot('walk'))
    if (tool.value === 'fill') {
      fillCells(cx, cy, alt ? '.' : '#')
      draw()
      return
    }
    if (tool.value === 'rect') {
      drag = { mode: 'rect', last: p, screen: [e.clientX, e.clientY], erase: alt, start: [cx, cy] }
      rectCells.value = [cx, cy, cx, cy]
      return
    }
    setCells(cx, cy, alt ? '.' : '#')
    drag = { mode: 'paint', last: p, screen: [e.clientX, e.clientY], erase: alt }
    draw()
    return
  }
  // Occluders.
  const o = selected.value
  if (tool.value === 'select' || (!o && tool.value !== 'base')) {
    const hit = occluderAt(p)
    selectedId.value = hit?.id ?? ''
    draw()
    return
  }
  if (!o) return
  if (tool.value === 'sam') {
    if (segmenting.value) return
    segmenting.value = { subtract: alt }
    sendToHost({ type: 'build:segment', points: [[Math.round(p[0]), Math.round(p[1]), 1]] })
    return
  }
  if (tool.value === 'base') {
    const i = handleAt(o, p)
    if (e.button === 2) {
      if (i >= 0 && o.base.length > 2) {
        remember(snapshot('mask', o.id))
        o.base.splice(i, 1)
        o.edited = true
      }
      draw()
      return
    }
    remember(snapshot('mask', o.id))
    o.edited = true
    if (i >= 0) {
      drag = { mode: 'handle', last: p, screen: [e.clientX, e.clientY], handle: i }
    } else {
      o.base.push([p[0], p[1]])
      sortBase(o)
      drag = { mode: 'handle', last: p, screen: [e.clientX, e.clientY], handle: o.base.findIndex((q) => q[0] === p[0] && q[1] === p[1]) }
    }
    draw()
    return
  }
  remember(snapshot('mask', o.id))
  const erase = tool.value === 'erase' || alt
  stroke(o, p, p, erase)
  drag = { mode: 'paint', last: p, screen: [e.clientX, e.clientY], erase }
  draw()
}
function onMove(e: PointerEvent): void {
  const p = toImage(e)
  hover.value = p
  if (!drag) {
    if (tool.value === 'brush' || tool.value === 'erase') draw()
    return
  }
  if (drag.mode === 'pan') {
    cam.x += e.clientX - drag.screen[0]
    cam.y += e.clientY - drag.screen[1]
    drag.screen = [e.clientX, e.clientY]
  } else if (drag.mode === 'paint') {
    if (layer.value === 'walk') {
      const [cx, cy] = cellOf(p[0], p[1])
      setCells(cx, cy, drag.erase ? '.' : '#')
    } else if (selected.value) {
      stroke(selected.value, drag.last, p, !!drag.erase)
    }
  } else if (drag.mode === 'handle' && selected.value && drag.handle !== undefined && drag.handle >= 0) {
    const pt = selected.value.base[drag.handle]
    if (pt) {
      pt[0] = p[0]
      pt[1] = p[1]
    }
  } else if (drag.mode === 'rect' && drag.start) {
    const [cx, cy] = cellOf(p[0], p[1])
    rectCells.value = [drag.start[0], drag.start[1], cx, cy]
  }
  drag.last = p
  draw()
}
function onUp(): void {
  if (drag?.mode === 'rect' && rectCells.value) {
    const [ax, ay, bx, by] = rectCells.value
    for (let y = Math.min(ay, by); y <= Math.max(ay, by); y++) {
      for (let x = Math.min(ax, bx); x <= Math.max(ax, bx); x++) {
        setCell(x, y, drag.erase ? '.' : '#')
      }
    }
    rectCells.value = null
  }
  if (drag?.mode === 'handle' && selected.value) sortBase(selected.value)
  drag = null
  draw()
}
function onWheel(e: WheelEvent): void {
  e.preventDefault()
  if (e.ctrlKey) {
    brush.value = Math.max(2, Math.min(256, Math.round(brush.value * (e.deltaY < 0 ? 1.15 : 1 / 1.15))))
    draw()
    return
  }
  const r = canvas.value!.getBoundingClientRect()
  const mx = e.clientX - r.left, my = e.clientY - r.top
  const k = e.deltaY < 0 ? 1.15 : 1 / 1.15
  const s = Math.max(0.05, Math.min(8, cam.s * k))
  cam.x = mx - ((mx - cam.x) * s) / cam.s
  cam.y = my - ((my - cam.y) * s) / cam.s
  cam.s = s
  draw()
}
function onKey(e: KeyboardEvent): void {
  const target = e.target as HTMLElement | null
  if (target && (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA')) return
  if (!host.value?.isConnected) return
  if (e.type === 'keyup') {
    if (e.code === 'Space') space = false
    return
  }
  if (e.code === 'Space') space = true
  else if (e.ctrlKey && e.key.toLowerCase() === 'z') undo()
  else if (e.ctrlKey && e.key.toLowerCase() === 'y') redo()
  else if (e.ctrlKey && e.key.toLowerCase() === 's') save(true)
  else if (e.key === '[') brush.value = Math.max(2, Math.round(brush.value / 1.25))
  else if (e.key === ']') brush.value = Math.min(256, Math.round(brush.value * 1.25))
  else if (e.key === 'v') tool.value = 'select'
  else if (e.key === 's') tool.value = 'sam'
  else if (e.key === 'b') tool.value = layer.value === 'walk' ? 'block' : 'brush'
  else if (e.key === 'e') tool.value = 'erase'
  else if (e.key === 'l') tool.value = 'base'
  else if (e.key === 'f') fit()
  else return
  e.preventDefault()
  draw()
}

// --- Saving ---
function save(say = false): void {
  if (!loaded.value || (!changed.value && !dirty.size && !removed.size)) return
  const list = occluders.value.map((o) => {
    const out: Record<string, unknown> = { ...o, box: [...o.box], base: o.base.map((p) => [Math.round(p[0] * 10) / 10, Math.round(p[1] * 10) / 10]) }
    if (dirty.has(o.id)) out.png = masks.get(o.id)!.toDataURL('image/png')
    return out
  })
  const w = walk.value
  sendToHost({
    type: 'build:saveMarks',
    say,
    marks: { walk: { x0: 0, y0: 0, w: w[0]?.length ?? 0, h: w.length, rows: w.map((r) => r.join('')) }, occluders: list, removed: [...removed] },
  })
  dirty.clear()
  removed.clear()
  changed.value = false
  emit('saved')
}
function prefill(replaceEdited: boolean): void {
  save()
  sendToHost({ type: 'build:prefillMarks', replaceEdited })
}
defineExpose({ save, prefill })

watch(layer, (l) => {
  if (l === 'walk' && !['block', 'rect', 'fill'].includes(tool.value)) tool.value = 'block'
  if (l === 'occluders' && ['block', 'rect', 'fill'].includes(tool.value)) tool.value = 'select'
  draw()
})
watch([selectedId, () => opacity.walk, () => opacity.occluders], draw)
watch(
  () => props.paint.file,
  (file) => {
    if (!file) return
    painting.onload = () => nextTick(fit)
    painting.src = `${PROJECT_HOST}${file}?v=${encodeURIComponent(props.paint.paintedAt ?? '')}`
  },
  { immediate: true },
)
let resize: ResizeObserver | undefined
let autosave: number | undefined
onMounted(() => {
  sendToHost({ type: 'build:marksRequest' })
  window.addEventListener('keydown', onKey)
  window.addEventListener('keyup', onKey)
  resize = new ResizeObserver(() => draw())
  if (host.value) resize.observe(host.value)
  autosave = window.setInterval(() => save(), 60000)
})
onBeforeUnmount(() => {
  save()
  window.removeEventListener('keydown', onKey)
  window.removeEventListener('keyup', onKey)
  resize?.disconnect()
  window.clearInterval(autosave)
})
const counts = computed(() => {
  let blocked = 0
  for (const r of walk.value) for (const c of r) blocked += c === '#' ? 1 : 0
  return { blocked, occluders: occluders.value.length, edited: occluders.value.filter((o) => o.edited || !o.prefilled).length }
})
</script>

<template>
  <div class="mark-editor">
    <div class="toolbar">
      <div class="group">
        <button :class="{ active: layer === 'occluders' }" title="What's drawn again over a character behind it" @click="layer = 'occluders'">Occluders</button>
        <button :class="{ active: layer === 'walk' }" title="Where one can walk" @click="layer = 'walk'">Walkable</button>
      </div>
      <div v-if="layer === 'occluders'" class="group">
        <button :class="{ active: tool === 'select' }" title="Pick an occluder (V)" @click="tool = 'select'">Select</button>
        <button :class="{ active: tool === 'sam' }" :disabled="!selected" title="Click a thing in the painting to add its outline to the selected occluder (S; Alt+click takes it away). The first click on a painting takes a few seconds." @click="tool = 'sam'">Click-select</button>
        <button :class="{ active: tool === 'brush' }" :disabled="!selected" title="Add to the selected occluder (B; Alt or right button erases)" @click="tool = 'brush'">Brush</button>
        <button :class="{ active: tool === 'erase' }" :disabled="!selected" title="Take away from it (E)" @click="tool = 'erase'">Eraser</button>
        <button :class="{ active: tool === 'base' }" :disabled="!selected" title="Its base line, where it meets the ground (L): drag a handle, click to add one, right-click to remove one" @click="tool = 'base'">Base line</button>
        <label class="size" title="Brush size ([ and ], or Ctrl+wheel)">⌀ <input v-model.number="brush" type="range" min="2" max="256" /> {{ brush }}</label>
        <button title="A new occluder, drawn by hand" @click="newOccluder">+ New</button>
      </div>
      <div v-else class="group">
        <button :class="{ active: tool === 'block' }" title="Paint cells blocked (Alt or right button: walkable)" @click="tool = 'block'">Brush</button>
        <button :class="{ active: tool === 'rect' }" title="A rectangle of cells" @click="tool = 'rect'">Rectangle</button>
        <button :class="{ active: tool === 'fill' }" title="Fill an area of the same kind" @click="tool = 'fill'">Fill</button>
        <label class="size">cells
          <select v-model.number="cellBrush"><option :value="1">1</option><option :value="2">2</option><option :value="4">4</option></select>
        </label>
      </div>
      <div class="group">
        <label class="size" title="How strongly the layer shows">opacity
          <input v-if="layer === 'walk'" v-model.number="opacity.walk" type="range" min="0.1" max="1" step="0.05" />
          <input v-else v-model.number="opacity.occluders" type="range" min="0.1" max="1" step="0.05" />
        </label>
        <button title="Undo (Ctrl+Z)" :disabled="!undoStack.length && !changed" @click="undo">↶</button>
        <button title="Redo (Ctrl+Y)" @click="redo">↷</button>
        <button title="Fit the painting (F)" @click="fit">Fit</button>
        <button title="Save (Ctrl+S; saved by itself every minute and when you leave)" :disabled="!changed" @click="save(true)">Save</button>
      </div>
    </div>
    <div class="work">
      <div ref="host" class="canvas-host">
        <canvas
          ref="canvas"
          @pointerdown="onDown"
          @pointermove="onMove"
          @pointerup="onUp"
          @pointerleave="hover = null; draw()"
          @wheel="onWheel"
          @contextmenu.prevent
        />
        <p v-if="!loaded" class="loading">Loading the marks…</p>
        <p v-if="segmenting" class="status">Finding its outline…</p>
        <p v-else-if="segmentError" class="status error">{{ segmentError }}</p>
      </div>
      <aside v-if="layer === 'occluders'" class="list">
        <h5>{{ counts.occluders }} occluders <span class="dim">· {{ counts.edited }} edited</span></h5>
        <ul>
          <li v-for="o in occluders" :key="o.id" :class="{ active: o.id === selectedId, hidden: o.hidden }" @click="selectedId = o.id">
            <i :style="{ background: colourOf(o.id) }" />
            <span class="name">{{ o.name }}</span>
            <span v-if="o.edited || !o.prefilled" class="dim" title="Changed by you">✎</span>
            <button :title="o.hidden ? 'Show' : 'Hide'" @click.stop="o.hidden = !o.hidden; changed = true; draw()">{{ o.hidden ? '◌' : '●' }}</button>
            <button title="Delete" @click.stop="removeOccluder(o.id)">✕</button>
          </li>
        </ul>
      </aside>
      <aside v-else class="list">
        <h5>{{ counts.blocked }} cells blocked</h5>
        <p class="dim">Red cells can't be walked on. Paint over walls, water, houses and tree trunks; clear paths, bridges and doorways.</p>
      </aside>
    </div>
  </div>
</template>

<style scoped>
.mark-editor {
  display: flex;
  flex-direction: column;
  height: 100%;
  min-height: 420px;
}
.toolbar {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  padding: 4px 0 6px;
}
.group {
  display: flex;
  gap: 2px;
  align-items: center;
}
.group button {
  border-radius: 0;
}
.group button.active {
  background: var(--accent, #3b82f6);
  color: #fff;
}
.size {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 11px;
  margin-left: 4px;
}
.size input[type='range'] {
  width: 80px;
}
.work {
  flex: 1;
  display: flex;
  min-height: 0;
  gap: 6px;
}
.canvas-host {
  position: relative;
  flex: 1;
  min-width: 0;
  background: #151515;
  border: 1px solid #333;
}
.canvas-host canvas {
  position: absolute;
  inset: 0;
  cursor: crosshair;
}
.status {
  position: absolute;
  left: 8px;
  bottom: 4px;
  margin: 0;
  padding: 2px 6px;
  background: rgba(0, 0, 0, 0.7);
  font-size: 11px;
}
.status.error {
  color: #e08080;
}
.loading {
  position: absolute;
  inset: 40% 0 auto;
  text-align: center;
  color: #999;
}
.list {
  width: 200px;
  overflow: auto;
  border: 1px solid #333;
  padding: 4px;
}
.list h5 {
  margin: 2px 0 6px;
}
.list ul {
  list-style: none;
  margin: 0;
  padding: 0;
}
.list li {
  display: flex;
  align-items: center;
  gap: 4px;
  padding: 2px 3px;
  cursor: pointer;
}
.list li.active {
  background: rgba(59, 130, 246, 0.25);
}
.list li.hidden .name {
  opacity: 0.5;
}
.list li i {
  width: 10px;
  height: 10px;
  flex: none;
}
.list li .name {
  flex: 1;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.list li button {
  border: none;
  background: none;
  padding: 0 2px;
  border-radius: 0;
  color: inherit;
}
.dim {
  opacity: 0.6;
}
</style>
