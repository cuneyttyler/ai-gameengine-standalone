// The UI designer's model (docs/systems/game-ui.md §12; engine/ui/UiDesign.h): a screen as a tree of elements, each
// placed by an anchor in its parent. The editor draws it; the host writes the RML the game runs.

export interface UiState {
  image?: string
  bg?: string
  color?: string
}
export interface UiAction {
  do: string
  arg?: unknown
}
export interface UiNode {
  id: string
  type: 'panel' | 'image' | 'button' | 'text' | 'bar' | 'list' | 'slots' | 'input' | 'notes' | 'rml'
  name?: string
  hidden?: boolean
  locked?: boolean
  anchor?: string
  x?: number
  y?: number
  w?: number
  h?: number
  opacity?: number
  bg?: string
  image?: string
  slice?: [number, number, number, number]
  imgW?: number
  imgH?: number
  states?: { hover?: UiState; pressed?: UiState; disabled?: UiState }
  text?: string
  size?: number
  color?: string
  align?: string
  bold?: boolean
  padding?: number
  actions?: UiAction[]
  show?: unknown[]
  bar?: { value?: string; max?: string; color?: string }
  list?: { source?: string; row?: string; rowH?: number }
  slots?: { cell?: number; gap?: number; look?: UiState }
  input?: { kind?: string; event?: string }
  rml?: string
  children?: UiNode[]
}
export interface UiDesign {
  schema?: string
  id?: string
  model: 'hud' | 'menu' | 'dialogue'
  width: number
  height: number
  head?: string
  root: UiNode[]
}

export const ANCHORS: { id: string; label: string }[] = [
  { id: 'tl', label: 'top left' }, { id: 't', label: 'top' }, { id: 'tr', label: 'top right' },
  { id: 'l', label: 'left' }, { id: 'c', label: 'middle' }, { id: 'r', label: 'right' },
  { id: 'bl', label: 'bottom left' }, { id: 'b', label: 'bottom' }, { id: 'br', label: 'bottom right' },
  { id: 'stretch', label: 'fill the parent' }, { id: 'stretch-top', label: 'across, at the top' },
  { id: 'stretch-bottom', label: 'across, at the bottom' }, { id: 'stretch-x', label: 'across, in the middle' },
  { id: 'stretch-left', label: 'down, at the left' }, { id: 'stretch-right', label: 'down, at the right' },
  { id: 'stretch-y', label: 'down, in the middle' },
]

// Which way each side follows: l c r s across, t c b s down (as the engine's SidesOf).
export function sidesOf(anchor = 'tl'): [string, string] {
  const map: Record<string, [string, string]> = {
    stretch: ['s', 's'], 'stretch-x': ['s', 'c'], 'stretch-top': ['s', 't'], 'stretch-bottom': ['s', 'b'],
    'stretch-y': ['c', 's'], 'stretch-left': ['l', 's'], 'stretch-right': ['r', 's'],
  }
  if (map[anchor]) return map[anchor]!
  return [anchor.includes('l') ? 'l' : anchor.includes('r') ? 'r' : 'c', anchor.includes('t') ? 't' : anchor.includes('b') ? 'b' : 'c']
}

// Its box in its parent's (design) pixels.
export function boxOf(n: UiNode, pw: number, ph: number): { left: number; top: number; width: number; height: number } {
  const [hs, vs] = sidesOf(n.anchor)
  const x = n.x ?? 0, y = n.y ?? 0, w = n.w ?? 100, h = n.h ?? 40
  let left = 0, width = w, top = 0, height = h
  if (hs === 's') (left = x), (width = pw - x - w)
  else if (hs === 'l') left = x
  else if (hs === 'r') left = pw - x - w
  else left = pw / 2 + x - w / 2
  if (vs === 's') (top = y), (height = ph - y - h)
  else if (vs === 't') top = y
  else if (vs === 'b') top = ph - y - h
  else top = ph / 2 + y - h / 2
  return { left, top, width: Math.max(0, width), height: Math.max(0, height) }
}

// Moved by (dx, dy) design pixels, whatever it's anchored to.
export function move(n: UiNode, dx: number, dy: number): void {
  const [hs, vs] = sidesOf(n.anchor)
  if (hs === 'r') n.x = (n.x ?? 0) - dx
  else n.x = (n.x ?? 0) + dx
  if (hs === 's') n.w = (n.w ?? 0) - dx
  if (vs === 'b') n.y = (n.y ?? 0) - dy
  else n.y = (n.y ?? 0) + dy
  if (vs === 's') n.h = (n.h ?? 0) - dy
}

// Its bottom-right corner dragged by (dx, dy): it grows that way, the rest staying where it is.
export function resize(n: UiNode, dx: number, dy: number): void {
  const [hs, vs] = sidesOf(n.anchor)
  if (hs === 's') n.w = (n.w ?? 0) - dx
  else if (hs === 'r') (n.x = (n.x ?? 0) - dx), (n.w = Math.max(4, (n.w ?? 0) + dx))
  else if (hs === 'c') (n.x = (n.x ?? 0) + dx / 2), (n.w = Math.max(4, (n.w ?? 0) + dx))
  else n.w = Math.max(4, (n.w ?? 0) + dx)
  if (vs === 's') n.h = (n.h ?? 0) - dy
  else if (vs === 'b') (n.y = (n.y ?? 0) - dy), (n.h = Math.max(4, (n.h ?? 0) + dy))
  else if (vs === 'c') (n.y = (n.y ?? 0) + dy / 2), (n.h = Math.max(4, (n.h ?? 0) + dy))
  else n.h = Math.max(4, (n.h ?? 0) + dy)
}

// The data each model offers: values to show and lists to repeat.
export const MODEL_DATA: Record<string, { values: string[]; lists: string[] }> = {
  hud: {
    values: ['health', 'health_max', 'mana', 'mana_max', 'stamina', 'stamina_max', 'level', 'xp', 'xp_next', 'gold', 'quest',
      'objective', 'clock', 'prompt_key', 'prompt_text', 'subtitle', 'abilities'],
    lists: ['items'],
  },
  menu: { values: ['title', 'subtitle', 'body', 'footer'], lists: ['items'] },
  dialogue: { values: ['speaker', 'text'], lists: ['choices'] },
}
export const SAMPLE: Record<string, string | number> = {
  health: 70, health_max: 100, mana: 40, mana_max: 100, stamina: 85, stamina_max: 100, level: 3, xp: 40, xp_next: 100,
  gold: 120, quest: 'The thief', objective: 'Find Mira', clock: 'Day 2  14:30', prompt_key: 'E', prompt_text: 'Talk to Tamsin',
  subtitle: 'The last one came back in a sack.', abilities: '1 Firebolt   2 Heal', title: 'Inventory', subtitle_: '',
  body: 'Gold 120   Level 3', footer: 'Esc to close', speaker: 'Old Tamsin', text: 'Another Warden? The last one came back in a sack.',
}

export const ACTIONS: { id: string; label: string; arg?: string }[] = [
  { id: 'open', label: 'open a screen', arg: 'screen' },
  { id: 'close', label: 'close a screen', arg: 'screen' },
  { id: 'event', label: 'send an event', arg: 'event' },
  { id: 'press', label: 'press an input action', arg: 'action' },
  { id: 'flag', label: 'set a flag', arg: 'flag' },
  { id: 'clear', label: 'clear a flag', arg: 'flag' },
  { id: 'effects', label: 'run effects', arg: 'effects' },
  { id: 'save', label: 'quick save' },
  { id: 'load', label: 'quick load' },
  { id: 'quit', label: 'quit the game' },
]

let counter = 0
export function newId(type: string, taken: Set<string>): string {
  let id = ''
  do id = `${type}-${++counter}`
  while (taken.has(id))
  return id
}
export function allIds(nodes: UiNode[], out = new Set<string>()): Set<string> {
  for (const n of nodes) {
    out.add(n.id)
    allIds(n.children ?? [], out)
  }
  return out
}

export function make(type: UiNode['type'], taken: Set<string>): UiNode {
  const id = newId(type, taken)
  const base: UiNode = { id, type, name: type[0]!.toUpperCase() + type.slice(1), anchor: 'tl', x: 40, y: 40, w: 200, h: 48 }
  if (type === 'panel') Object.assign(base, { w: 400, h: 300, bg: 'panel', children: [] })
  if (type === 'button') Object.assign(base, { text: 'Button', bg: 'panel-border', align: 'center', states: { hover: { bg: 'accent' } }, actions: [] })
  if (type === 'text') Object.assign(base, { text: 'Text', size: 18 })
  if (type === 'image') Object.assign(base, { w: 128, h: 128 })
  if (type === 'bar') Object.assign(base, { w: 240, h: 14, bg: 'bar-back', bar: { value: 'health', max: 'health_max', color: 'health' } })
  if (type === 'list') Object.assign(base, { w: 400, h: 300, list: { rowH: 34 } })
  if (type === 'slots') Object.assign(base, { w: 360, h: 200, slots: { cell: 64, gap: 6 } })
  if (type === 'input') Object.assign(base, { w: 200, h: 30, input: { kind: 'checkbox', event: '' } })
  if (type === 'notes') Object.assign(base, { anchor: 'stretch-top', x: 0, y: 24, w: 0, h: 300 })
  if (type === 'rml') Object.assign(base, { anchor: 'stretch', x: 0, y: 0, w: 0, h: 0, rml: '<div>…</div>' })
  return base
}

// A design-time picture of a value: its sample, for text with {{bindings}}.
export function sampleText(text: string | undefined): string {
  return (text ?? '').replace(/\{\{\s*([^}]+?)\s*\}\}/g, (_, expr: string) => {
    const key = expr.trim()
    if (key in SAMPLE) return String(SAMPLE[key])
    if (key.startsWith('item.')) return key === 'item.count' ? '2' : 'Potion'
    if (key === 'i + 1') return '1'
    return `[${key}]`
  })
}
