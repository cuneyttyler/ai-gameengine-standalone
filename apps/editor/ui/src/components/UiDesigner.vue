<script setup lang="ts">
// The UI designer (docs/systems/game-ui.md §12), from Project ▸ UI ▸ Design…: a screen, a window or the HUD made by
// placing elements, with pictures imported for panels and buttons, actions on clicks and the game's data bound in.
// Saved as ui/designs/<id>.json; the host writes ui/screens/<id>.rml from it, which the game runs.
import { computed, nextTick, onBeforeUnmount, reactive, ref, watch } from 'vue'
import { editorState } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'
import { trackOverlay, untrackOverlay } from '../overlay'
import { plainCopy } from '../plainCopy'
import RulesEditor from './RulesEditor.vue'
import UiDesignNode from './UiDesignNode.vue'
import { ACTIONS, ANCHORS, MODEL_DATA, allIds, make, move, resize, type UiDesign, type UiNode } from '../uiDesign'

const PROJECT_HOST = 'https://aige-project.local/'
const PRESETS = [
  { id: '16:9', w: 1920, h: 1080 }, { id: '16:10', w: 1920, h: 1200 }, { id: '21:9', w: 2560, h: 1080 },
  { id: '4:3', w: 1600, h: 1200 }, { id: 'Steam Deck', w: 1280, h: 800 },
]
const PALETTE: { type: UiNode['type']; label: string; title: string }[] = [
  { type: 'panel', label: 'Panel', title: 'A box that holds other elements: a background picture or colour' },
  { type: 'image', label: 'Image', title: 'A picture' },
  { type: 'button', label: 'Button', title: 'Clicked to do something: open a screen, send an event, press an action…' },
  { type: 'text', label: 'Text', title: 'Words, with the game\'s data in {{braces}}' },
  { type: 'bar', label: 'Bar', title: 'A bar that fills with a value: health, mana, stamina, experience' },
  { type: 'list', label: 'List', title: 'One row per entry: a menu\'s lines, the dialogue\'s choices' },
  { type: 'slots', label: 'Slot grid', title: 'What the player carries, one slot per item: click to use it' },
  { type: 'input', label: 'Checkbox / slider', title: 'A checkbox, a slider or a text box that sends an event when changed' },
  { type: 'notes', label: 'Notifications', title: 'Toasts, the feed and banners' },
  { type: 'rml', label: 'Custom RML', title: 'RML written by hand, for what the designer has no element for' },
]

const backdrop = ref<HTMLElement | null>(null)
const canvasBox = ref<HTMLElement | null>(null)
const design = ref<UiDesign | null>(null)
const from = ref('')
const selectedId = ref('')
const preset = ref('16:9')
const tokens = ref<Record<string, string>>({})
const rml = ref('')
const problems = ref<string[]>([])
const showCode = ref(false)
const saved = ref('')
const history: string[] = []
const future: string[] = []
const view = reactive({ width: 900 })

watch(
  () => editorState.uiDesigner,
  async (id) => {
    design.value = null
    if (!id) return
    sendToHost({ type: 'ui:design:load', id })
    sendToHost({ type: 'ui:request' })
    await nextTick()
    if (backdrop.value) {
      trackOverlay(backdrop.value)
      backdrop.value.focus()
    }
  },
)
onHostMessage((m) => {
  if (m.type === 'ui:design' && m.id === editorState.uiDesigner) {
    design.value = m.design as UiDesign
    from.value = m.from as string
    rml.value = m.rml as string
    problems.value = (m.problems as string[]) ?? []
    history.length = future.length = 0
    saved.value = JSON.stringify(design.value)
    selectedId.value = ''
  } else if (m.type === 'ui:list' || m.tokens) {
    if (m.tokens) tokens.value = m.tokens as Record<string, string>
  } else if (m.type === 'ui:designSaved' && m.id === editorState.uiDesigner) {
    rml.value = m.rml as string
    problems.value = (m.problems as string[]) ?? []
  } else if (m.type === 'ui:designImage' && design.value) {
    const n = find(design.value.root, m.for as string)
    if (!n) return
    remember()
    const slot = m.slot as string
    if (slot === 'image') Object.assign(n, { image: m.path as string, imgW: m.w as number, imgH: m.h as number })
    else {
      n.states ??= {}
      ;(n.states as Record<string, { image?: string }>)[slot] = { ...(n.states as Record<string, object>)[slot], image: m.path as string }
      if (!n.imgW) Object.assign(n, { imgW: m.w as number, imgH: m.h as number })
    }
  }
})
onBeforeUnmount(() => backdrop.value && untrackOverlay(backdrop.value))

const size = computed(() => PRESETS.find((p) => p.id === preset.value) ?? PRESETS[0]!)
const scale = computed(() => Math.min(view.width / size.value.w, 560 / size.value.h))
const dirty = computed(() => !!design.value && JSON.stringify(design.value) !== saved.value)
const picture = (path: string) => PROJECT_HOST + path.replace(/^\//, '')

function find(nodes: UiNode[], id: string): UiNode | null {
  for (const n of nodes) {
    if (n.id === id) return n
    const inner = find(n.children ?? [], id)
    if (inner) return inner
  }
  return null
}
// The list a node is in, and where.
function siblingsOf(nodes: UiNode[], id: string): { list: UiNode[]; index: number } | null {
  const index = nodes.findIndex((n) => n.id === id)
  if (index >= 0) return { list: nodes, index }
  for (const n of nodes) {
    const inner = siblingsOf(n.children ?? [], id)
    if (inner) return inner
  }
  return null
}
const selected = computed(() => (design.value && selectedId.value ? find(design.value.root, selectedId.value) : null))

// Undo: whole snapshots (a design is small).
function remember(): void {
  if (!design.value) return
  history.push(JSON.stringify(design.value))
  if (history.length > 100) history.shift()
  future.length = 0
}
function undo(): void {
  if (!design.value || !history.length) return
  future.push(JSON.stringify(design.value))
  design.value = JSON.parse(history.pop()!)
}
function redo(): void {
  if (!design.value || !future.length) return
  history.push(JSON.stringify(design.value))
  design.value = JSON.parse(future.pop()!)
}

function add(type: UiNode['type']): void {
  if (!design.value) return
  remember()
  const node = make(type, allIds(design.value.root))
  // Into the panel picked, else the screen.
  const into = selected.value && (selected.value.type === 'panel' || selected.value.type === 'button') ? selected.value : null
  if (into) (into.children ??= []).push(node)
  else design.value.root.push(node)
  selectedId.value = node.id
}
function remove(): void {
  if (!design.value || !selected.value) return
  const at = siblingsOf(design.value.root, selected.value.id)
  if (!at) return
  remember()
  at.list.splice(at.index, 1)
  selectedId.value = ''
}
function duplicate(): void {
  if (!design.value || !selected.value) return
  const at = siblingsOf(design.value.root, selected.value.id)
  if (!at) return
  remember()
  const copy = plainCopy(selected.value)
  const taken = allIds(design.value.root)
  const renumber = (n: UiNode) => {
    n.id = `${n.type}-${Math.random().toString(36).slice(2, 7)}`
    while (taken.has(n.id)) n.id += 'x'
    taken.add(n.id)
    ;(n.children ?? []).forEach(renumber)
  }
  renumber(copy)
  copy.x = (copy.x ?? 0) + 20
  copy.y = (copy.y ?? 0) + 20
  at.list.splice(at.index + 1, 0, copy)
  selectedId.value = copy.id
}
// Layers: up and down among its siblings; into the one above it (a panel); out to its parent's level.
function reorder(step: number): void {
  if (!design.value || !selected.value) return
  const at = siblingsOf(design.value.root, selected.value.id)
  if (!at || at.index + step < 0 || at.index + step >= at.list.length) return
  remember()
  const [n] = at.list.splice(at.index, 1)
  at.list.splice(at.index + step, 0, n!)
}
function nestInto(): void {
  if (!design.value || !selected.value) return
  const at = siblingsOf(design.value.root, selected.value.id)
  const above = at && at.index > 0 ? at.list[at.index - 1] : null
  if (!at || !above || !['panel', 'button'].includes(above.type)) return
  remember()
  const [n] = at.list.splice(at.index, 1)
  ;(above.children ??= []).push(n!)
}
function outOf(): void {
  if (!design.value || !selected.value) return
  const at = siblingsOf(design.value.root, selected.value.id)
  if (!at || at.list === design.value.root) return
  const parent = [...walk(design.value.root)].find((n) => n.children === at.list)
  const outer = parent ? siblingsOf(design.value.root, parent.id) : null
  if (!outer) return
  remember()
  const [n] = at.list.splice(at.index, 1)
  outer.list.splice(outer.index + 1, 0, n!)
}
function* walk(nodes: UiNode[]): Generator<UiNode> {
  for (const n of nodes) {
    yield n
    yield* walk(n.children ?? [])
  }
}
const layers = computed(() => {
  const out: { node: UiNode; depth: number }[] = []
  const visit = (nodes: UiNode[], depth: number) => nodes.forEach((n) => (out.push({ node: n, depth }), visit(n.children ?? [], depth + 1)))
  if (design.value) visit(design.value.root, 0)
  return out
})

// Dragging on the canvas: moved, or resized by its corner, in design pixels.
let grab: { id: string; mode: 'move' | 'resize'; x: number; y: number; started: boolean } | null = null
function onGrab(id: string, mode: 'move' | 'resize', event: MouseEvent): void {
  grab = { id, mode, x: event.clientX, y: event.clientY, started: false }
  window.addEventListener('mousemove', onDrag)
  window.addEventListener('mouseup', onDrop, { once: true })
}
function onDrag(event: MouseEvent): void {
  if (!grab || !design.value) return
  const n = find(design.value.root, grab.id)
  if (!n) return
  const dx = Math.round((event.clientX - grab.x) / scale.value), dy = Math.round((event.clientY - grab.y) / scale.value)
  if (!dx && !dy) return
  if (!grab.started) (remember(), (grab.started = true))
  if (grab.mode === 'move') move(n, dx, dy)
  else resize(n, dx, dy)
  grab.x += dx * scale.value
  grab.y += dy * scale.value
}
function onDrop(): void {
  grab = null
  window.removeEventListener('mousemove', onDrag)
}
function onKey(event: KeyboardEvent): void {
  if ((event.target as HTMLElement).closest('input, textarea, select')) return
  if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'z') return (event.preventDefault(), event.shiftKey ? redo() : undo())
  if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'y') return (event.preventDefault(), redo())
  if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 's') return (event.preventDefault(), save())
  if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'd') return (event.preventDefault(), duplicate())
  if (!selected.value) return
  if (event.key === 'Delete') return remove()
  const step = event.shiftKey ? 10 : 1
  const arrows: Record<string, [number, number]> = { ArrowLeft: [-step, 0], ArrowRight: [step, 0], ArrowUp: [0, -step], ArrowDown: [0, step] }
  if (arrows[event.key]) {
    event.preventDefault()
    remember()
    move(selected.value, ...arrows[event.key]!)
  }
}

// A field changed in the properties: remembered for undo first.
function set<K extends keyof UiNode>(key: K, value: UiNode[K]): void {
  if (!selected.value) return
  remember()
  if (value === '' || value === undefined) delete selected.value[key]
  else selected.value[key] = value
}
function setIn(group: 'bar' | 'list' | 'slots' | 'input', key: string, value: unknown): void {
  if (!selected.value) return
  remember()
  const g = ((selected.value as Record<string, unknown>)[group] ??= {}) as Record<string, unknown>
  g[key] = value
}
function setState(state: 'hover' | 'pressed' | 'disabled', key: 'bg' | 'color', value: string): void {
  if (!selected.value) return
  remember()
  const states = (selected.value.states ??= {}) as Record<string, Record<string, string>>
  states[state] = { ...(states[state] ?? {}), [key]: value }
  if (!value) delete states[state]![key]
}
function setSlice(i: number, value: number): void {
  if (!selected.value) return
  remember()
  const s = selected.value.slice ?? [0, 0, 0, 0]
  s[i] = Math.max(0, value)
  selected.value.slice = [...s] as [number, number, number, number]
}
function importImage(slot: string): void {
  if (selected.value) sendToHost({ type: 'ui:design:image', for: selected.value.id, slot })
}
function insertBinding(name: string): void {
  if (!selected.value) return
  set('text', `${selected.value.text ?? ''}{{${name}}}`)
}
function addAction(): void {
  if (!selected.value) return
  remember()
  ;(selected.value.actions ??= []).push({ do: 'open', arg: '' })
}
const colourChoices = computed(() => Object.keys(tokens.value).filter((k) => !k.includes('size')))

// Checks the editor can make itself: text too long for its box, a missing picture, a button without an action.
const localProblems = computed(() => {
  const out: string[] = []
  for (const n of walk(design.value?.root ?? [])) {
    const label = n.name || n.id
    if ((n.type === 'text' || n.type === 'button') && n.text && !n.text.includes('{{')) {
      const perLine = Math.max(1, Math.floor((n.w ?? 100) / ((n.size ?? 16) * 0.55)))
      const lines = Math.ceil(n.text.length / perLine)
      if (lines * (n.size ?? 16) * 1.25 > (n.h ?? 40) + 2 && !(n.anchor ?? '').startsWith('stretch')) out.push(`${label}: its text may not fit (make it bigger, or the words fewer)`)
    }
    if (n.type === 'button' && !n.actions?.length) out.push(`${label}: a button that does nothing`)
    if (n.type === 'image' && !n.image) out.push(`${label}: no picture yet`)
  }
  return out
})

function save(): void {
  if (!design.value || !editorState.uiDesigner) return
  sendToHost({ type: 'ui:design:save', id: editorState.uiDesigner, design: plainCopy(design.value) })
  saved.value = JSON.stringify(design.value)
  from.value = 'project'
}
function preview(): void {
  save()
  sendToHost({ type: 'ui:preview', id: editorState.uiDesigner })
}
function close(): void {
  if (backdrop.value) untrackOverlay(backdrop.value)
  sendToHost({ type: 'ui:preview', id: '' })
  editorState.uiDesigner = null
}
function measure(): void {
  if (canvasBox.value) view.width = Math.max(300, canvasBox.value.clientWidth - 24)
}
watch(design, () => nextTick(measure))
</script>

<template>
  <div v-if="editorState.uiDesigner" ref="backdrop" class="backdrop ed-form" tabindex="-1" @keydown="onKey" @keydown.esc.stop="close">
    <div class="sheet">
      <div class="top">
        <h3>UI designer: {{ editorState.uiDesigner }}</h3>
        <span v-if="from === 'template'" class="muted small">from the engine's starting design: saving makes it this project's</span>
        <span v-else-if="from === 'imported'" class="muted small">a hand-written screen, kept whole as a Custom RML element</span>
        <span class="spacer"></span>
        <select v-if="design" v-model="design.model" title="The data it shows: the HUD's (health, gold, what the player carries…), a menu's or window's (title, lines), or the dialogue's">
          <option value="hud">HUD data</option><option value="menu">Menu / window data</option><option value="dialogue">Dialogue data</option>
        </select>
        <select v-model="preset" title="The screen's shape: anchored elements follow it">
          <option v-for="p in PRESETS" :key="p.id" :value="p.id">{{ p.id }} ({{ p.w }}×{{ p.h }})</option>
        </select>
        <button title="Undo (Ctrl+Z)" @click="undo">Undo</button>
        <button title="Redo (Ctrl+Y)" @click="redo">Redo</button>
        <button :class="{ on: showCode }" title="The RML the game runs, written from the design" @click="showCode = !showCode">Code</button>
        <button title="Save, then show it in the game's renderer in the viewport" @click="preview">Preview</button>
        <button class="primary" :disabled="!dirty" title="Save (Ctrl+S)" @click="save">Save</button>
        <button @click="close">Close</button>
      </div>
      <div v-if="design" class="work">
        <aside class="left">
          <h4>Add</h4>
          <div class="palette">
            <button v-for="p in PALETTE" :key="p.type" :title="p.title" @click="add(p.type)">{{ p.label }}</button>
          </div>
          <h4>Layers</h4>
          <div class="layer-tools">
            <button title="Up" :disabled="!selected" @click="reorder(-1)">↑</button>
            <button title="Down" :disabled="!selected" @click="reorder(1)">↓</button>
            <button title="Into the panel above it" :disabled="!selected" @click="nestInto">→</button>
            <button title="Out of its panel" :disabled="!selected" @click="outOf">←</button>
            <button title="Duplicate (Ctrl+D)" :disabled="!selected" @click="duplicate">⧉</button>
            <button title="Delete (Del)" :disabled="!selected" @click="remove">×</button>
          </div>
          <div class="layers">
            <div v-for="l in layers" :key="l.node.id" class="layer" :class="{ on: l.node.id === selectedId }" :style="{ paddingLeft: `${6 + l.depth * 14}px` }"
                 @click="selectedId = l.node.id">
              <span class="grow">{{ l.node.name || l.node.id }} <span class="muted small">{{ l.node.type }}</span></span>
              <button class="icon" :title="l.node.hidden ? 'Show' : 'Hide'" @click.stop="remember(), (l.node.hidden = !l.node.hidden)">{{ l.node.hidden ? '◌' : '●' }}</button>
              <button class="icon" :title="l.node.locked ? 'Unlock' : 'Lock (not moved by dragging)'" @click.stop="remember(), (l.node.locked = !l.node.locked)">{{ l.node.locked ? '🔒' : '·' }}</button>
            </div>
            <p v-if="!layers.length" class="muted small">Empty. Add a panel or a button from the list above.</p>
          </div>
        </aside>
        <section ref="canvasBox" class="middle">
          <div class="canvas" :style="{ width: `${size.w * scale}px`, height: `${size.h * scale}px` }" @mousedown="selectedId = ''">
            <div class="safe" :style="{ inset: `${size.h * scale * 0.05}px ${size.w * scale * 0.05}px` }"></div>
            <UiDesignNode v-for="n in design.root" :key="n.id" :node="n" :pw="size.w" :ph="size.h" :scale="scale" :selected="selectedId"
                          :tokens="tokens" :picture="picture" @pick="(id) => (selectedId = id)" @grab="onGrab" />
          </div>
          <pre v-if="showCode" class="code">{{ rml || 'Save to see the RML written from the design.' }}</pre>
          <div v-if="problems.length || localProblems.length" class="checks">
            <b>Checks</b>
            <div v-for="p in [...problems, ...localProblems]" :key="p" class="warn">{{ p }}</div>
          </div>
          <p class="muted small">Drag to move, the blue corner to resize; arrows nudge (Shift: 10). The faint box is the safe area. Data shows sample values here; Preview shows the real renderer.</p>
        </section>
        <aside class="right">
          <template v-if="selected">
            <h4>{{ selected.type }}</h4>
            <label>Name <input :value="selected.name ?? ''" @change="set('name', ($event.target as HTMLInputElement).value)" /></label>
            <label title="Which point of its parent it keeps its place from: it stays there at any screen size">Anchor
              <select :value="selected.anchor ?? 'tl'" @change="set('anchor', ($event.target as HTMLSelectElement).value)">
                <option v-for="a in ANCHORS" :key="a.id" :value="a.id">{{ a.label }}</option>
              </select></label>
            <div class="grid4">
              <label>x <input type="number" :value="selected.x ?? 0" @change="set('x', Number(($event.target as HTMLInputElement).value))" /></label>
              <label>y <input type="number" :value="selected.y ?? 0" @change="set('y', Number(($event.target as HTMLInputElement).value))" /></label>
              <label :title="(selected.anchor ?? '').startsWith('stretch') ? 'For a stretched side: its margin from the right' : 'Width'">w <input type="number" :value="selected.w ?? 0" @change="set('w', Number(($event.target as HTMLInputElement).value))" /></label>
              <label :title="(selected.anchor ?? '').startsWith('stretch') ? 'For a stretched side: its margin from the bottom' : 'Height'">h <input type="number" :value="selected.h ?? 0" @change="set('h', Number(($event.target as HTMLInputElement).value))" /></label>
            </div>
            <template v-if="selected.type !== 'rml'">
              <h4>Look</h4>
              <label>Background
                <select :value="selected.bg ?? ''" @change="set('bg', ($event.target as HTMLSelectElement).value)">
                  <option value="">none</option><option v-for="c in colourChoices" :key="c" :value="c">{{ c }}</option>
                </select></label>
              <label>Picture
                <span class="row"><span class="muted small path">{{ selected.image || 'none' }}</span>
                  <button class="small" @click="importImage('image')">Import…</button>
                  <button v-if="selected.image" class="small" @click="set('image', '')">×</button></span></label>
              <label v-if="selected.image" title="Pixels of the picture kept as they are at each side (its corners and edges); the middle stretches">Nine-slice
                <span class="row"><input v-for="(side, i) in ['left', 'top', 'right', 'bottom']" :key="side" class="num" type="number" min="0" :title="side"
                         :value="(selected.slice ?? [0, 0, 0, 0])[i]" @change="setSlice(i, Number(($event.target as HTMLInputElement).value))" /></span></label>
              <label>Opacity <input type="range" min="0" max="1" step="0.05" :value="selected.opacity ?? 1" @change="set('opacity', Number(($event.target as HTMLInputElement).value))" /></label>
              <label>Padding <input type="number" min="0" :value="selected.padding ?? 0" @change="set('padding', Number(($event.target as HTMLInputElement).value))" /></label>
            </template>
            <template v-if="selected.type === 'text' || selected.type === 'button'">
              <h4>Text</h4>
              <label>Text <input :value="selected.text ?? ''" @change="set('text', ($event.target as HTMLInputElement).value)" /></label>
              <label title="The game's data, shown in the text">Insert data
                <select :value="''" @change="insertBinding(($event.target as HTMLSelectElement).value); ($event.target as HTMLSelectElement).value = ''">
                  <option value="">choose…</option><option v-for="v in MODEL_DATA[design.model]!.values" :key="v" :value="v">{{ v }}</option>
                </select></label>
              <div class="grid4">
                <label>Size <input type="number" min="6" :value="selected.size ?? 16" @change="set('size', Number(($event.target as HTMLInputElement).value))" /></label>
                <label>Align <select :value="selected.align ?? 'left'" @change="set('align', ($event.target as HTMLSelectElement).value)"><option>left</option><option>center</option><option>right</option></select></label>
              </div>
              <label>Colour <select :value="selected.color ?? ''" @change="set('color', ($event.target as HTMLSelectElement).value)">
                <option value="">the theme's text</option><option v-for="c in colourChoices" :key="c" :value="c">{{ c }}</option></select></label>
              <label class="inline"><input type="checkbox" :checked="!!selected.bold" @change="set('bold', ($event.target as HTMLInputElement).checked)" /> Bold</label>
            </template>
            <template v-if="selected.type === 'button'">
              <h4>States</h4>
              <div v-for="state in (['hover', 'pressed', 'disabled'] as const)" :key="state" class="state">
                <span class="muted small">{{ state }}</span>
                <select :value="selected.states?.[state]?.bg ?? ''" title="Background" @change="setState(state, 'bg', ($event.target as HTMLSelectElement).value)">
                  <option value="">as normal</option><option v-for="c in colourChoices" :key="c" :value="c">{{ c }}</option></select>
                <button class="small" title="A picture for this state" @click="importImage(state)">{{ selected.states?.[state]?.image ? 'Picture ✓' : 'Picture…' }}</button>
              </div>
            </template>
            <template v-if="selected.type === 'bar'">
              <h4>Bar</h4>
              <label>Value <select :value="selected.bar?.value ?? 'health'" @change="setIn('bar', 'value', ($event.target as HTMLSelectElement).value)"><option v-for="v in MODEL_DATA[design.model]!.values" :key="v">{{ v }}</option></select></label>
              <label>Out of <select :value="selected.bar?.max ?? 'health_max'" @change="setIn('bar', 'max', ($event.target as HTMLSelectElement).value)"><option v-for="v in MODEL_DATA[design.model]!.values" :key="v">{{ v }}</option></select></label>
              <label>Fill <select :value="selected.bar?.color ?? 'health'" @change="setIn('bar', 'color', ($event.target as HTMLSelectElement).value)"><option v-for="c in colourChoices" :key="c">{{ c }}</option></select></label>
            </template>
            <template v-if="selected.type === 'list'">
              <h4>List</h4>
              <label>Of <select :value="selected.list?.source ?? MODEL_DATA[design.model]!.lists[0]" @change="setIn('list', 'source', ($event.target as HTMLSelectElement).value)"><option v-for="v in MODEL_DATA[design.model]!.lists" :key="v">{{ v }}</option></select></label>
              <label title="What each row shows: {{item.label}}, or for the HUD's items {{item.name}} and {{item.count}}">Row <input :value="selected.list?.row ?? ''" @change="setIn('list', 'row', ($event.target as HTMLInputElement).value)" /></label>
              <label>Row height <input type="number" min="8" :value="selected.list?.rowH ?? 34" @change="setIn('list', 'rowH', Number(($event.target as HTMLInputElement).value))" /></label>
              <p class="muted small">Menus and the dialogue: a click picks the line, as the keys do.</p>
            </template>
            <template v-if="selected.type === 'slots'">
              <h4>Slot grid</h4>
              <p v-if="design.model !== 'hud'" class="warn">Slots show what the player carries: choose HUD data above.</p>
              <div class="grid4">
                <label>Cell <input type="number" min="16" :value="selected.slots?.cell ?? 64" @change="setIn('slots', 'cell', Number(($event.target as HTMLInputElement).value))" /></label>
                <label>Gap <input type="number" min="0" :value="selected.slots?.gap ?? 6" @change="setIn('slots', 'gap', Number(($event.target as HTMLInputElement).value))" /></label>
              </div>
              <p class="muted small">A click uses the item, wears it, or takes it in hand.</p>
            </template>
            <template v-if="selected.type === 'input'">
              <h4>Control</h4>
              <label>Kind <select :value="selected.input?.kind ?? 'checkbox'" @change="setIn('input', 'kind', ($event.target as HTMLSelectElement).value)"><option>checkbox</option><option value="slider">slider</option><option>text</option></select></label>
              <label>Sends event <input :value="selected.input?.event ?? ''" placeholder="music_toggled" @change="setIn('input', 'event', ($event.target as HTMLInputElement).value)" /></label>
              <p class="muted small">It sends its event when changed. Its value isn't passed on yet.</p>
            </template>
            <template v-if="selected.type === 'rml'">
              <h4>Custom RML</h4>
              <textarea rows="10" spellcheck="false" :value="selected.rml ?? ''" @change="set('rml', ($event.target as HTMLTextAreaElement).value)"></textarea>
            </template>
            <template v-if="selected.type !== 'notes' && selected.type !== 'rml'">
              <h4>On click</h4>
              <div v-for="(a, i) in selected.actions ?? []" :key="i" class="action">
                <select :value="a.do" @change="remember(), (a.do = ($event.target as HTMLSelectElement).value)">
                  <option v-for="o in ACTIONS" :key="o.id" :value="o.id">{{ o.label }}</option>
                </select>
                <RulesEditor v-if="a.do === 'effects'" mode="do" :value="a.arg ?? ''" @update="(t) => (remember(), (a.arg = t))" />
                <input v-else-if="ACTIONS.find((o) => o.id === a.do)?.arg" :value="String(a.arg ?? '')" :placeholder="ACTIONS.find((o) => o.id === a.do)?.arg"
                       @change="remember(), (a.arg = ($event.target as HTMLInputElement).value)" />
                <button class="small" @click="remember(), selected.actions!.splice(i, 1)">×</button>
              </div>
              <button class="small" @click="addAction">+ Action</button>
              <h4>Only show if</h4>
              <RulesEditor mode="if" :value="selected.show ?? ''" @update="(t) => set('show', t ? JSON.parse(t) : undefined)" />
            </template>
          </template>
          <p v-else class="muted small">Click an element to change it, or add one from the list on the left.</p>
        </aside>
      </div>
      <p v-else class="muted">Loading…</p>
    </div>
  </div>
</template>

<style scoped>
.backdrop { position: fixed; inset: 0; background: rgba(0, 0, 0, 0.55); z-index: 2500; display: flex; align-items: center; justify-content: center; outline: none; }
.sheet { background: #1e1e28; border: 1px solid #343448; width: 96vw; height: 94vh; display: flex; flex-direction: column; padding: 10px; gap: 8px; }
.top { display: flex; align-items: center; gap: 6px; }
.top h3 { margin: 0 8px 0 0; }
.spacer { flex: 1; }
.work { flex: 1; display: grid; grid-template-columns: 230px 1fr 300px; gap: 10px; min-height: 0; }
.left, .right { overflow-y: auto; display: flex; flex-direction: column; gap: 5px; }
.middle { overflow: auto; display: flex; flex-direction: column; gap: 8px; }
.palette { display: grid; grid-template-columns: 1fr 1fr; gap: 4px; }
.layer-tools { display: flex; gap: 3px; }
.layers { display: flex; flex-direction: column; }
.layer { display: flex; align-items: center; gap: 4px; padding: 2px 6px; cursor: pointer; }
.layer.on { background: #2b3550; }
.layer .grow { flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.icon { padding: 0 4px; background: none; border: none; }
.canvas { position: relative; background: repeating-conic-gradient(#2a2a36 0% 25%, #24242e 0% 50%) 50% / 24px 24px; flex: none; overflow: hidden; }
.safe { position: absolute; border: 1px dashed rgba(255, 255, 255, 0.12); pointer-events: none; }
.right label { display: flex; justify-content: space-between; align-items: center; gap: 6px; }
.right label.inline { justify-content: flex-start; }
.grid4 { display: grid; grid-template-columns: 1fr 1fr; gap: 4px; }
.grid4 input { width: 64px; }
.row { display: flex; gap: 4px; align-items: center; }
.num { width: 46px; }
.path { max-width: 120px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.state, .action { display: flex; gap: 4px; align-items: center; flex-wrap: wrap; }
textarea { width: 100%; font-family: monospace; }
.code { max-height: 220px; overflow: auto; background: #15151c; padding: 8px; font-size: 12px; white-space: pre; }
.checks .warn, .warn { color: #e6a23c; font-size: 0.85em; }
h4 { margin: 8px 0 2px; }
.muted { opacity: 0.65; }
.small { font-size: 0.85em; }
button.on { border-color: #3b82f6; }
</style>
