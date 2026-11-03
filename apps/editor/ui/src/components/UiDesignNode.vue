<script setup lang="ts">
// One element on the UI designer's canvas, and its children inside it: drawn as the game will show it (near enough:
// the game's renderer has the last word, Preview shows it), dragged to move, its corner dragged to resize.
import { computed } from 'vue'
import { OVERHEAD_POINT, boxOf, fontFamily, isOverhead, overheadSample, sampleText, type UiNode } from '../uiDesign'

defineOptions({ name: 'UiDesignNode' })
const props = defineProps<{
  node: UiNode
  pw: number // the parent's size, in design pixels
  ph: number
  scale: number // design pixel -> screen pixel
  selected: string
  tokens: Record<string, string>
  picture: (path: string) => string
}>()
const emit = defineEmits<{
  (e: 'pick', id: string): void
  (e: 'grab', id: string, mode: 'move' | 'resize', event: MouseEvent): void
}>()

const box = computed(() => boxOf(props.node, props.pw, props.ph))
const colour = (value?: string) => (!value ? undefined : value.startsWith('#') || value.startsWith('rgb') ? value : props.tokens[value] ?? value)

// A picture whole, or in nine pieces (its corners kept), as border-image draws it.
function pictureStyle(image?: string): Record<string, string> {
  if (!image) return {}
  const s = props.node.slice
  if (s && s.some((v) => v > 0)) {
    return {
      borderStyle: 'solid',
      borderWidth: s.map((v) => `${v * props.scale}px`).join(' ').replace(/(\S+) (\S+) (\S+) (\S+)/, '$2 $3 $4 $1'),
      borderImage: `url("${props.picture(image)}") ${s[1]} ${s[2]} ${s[3]} ${s[0]} fill / ${s[1] * props.scale}px ${s[2] * props.scale}px ${s[3] * props.scale}px ${s[0] * props.scale}px stretch`,
    }
  }
  return { backgroundImage: `url("${props.picture(image)}")`, backgroundSize: '100% 100%' }
}

const style = computed<Record<string, string>>(() => {
  const n = props.node, b = box.value, k = props.scale
  return {
    left: `${b.left * k}px`,
    top: `${b.top * k}px`,
    width: `${b.width * k}px`,
    height: `${b.height * k}px`,
    background: colour(n.bg) ?? '',
    color: colour(n.color) ?? colour('text') ?? '#eee',
    fontSize: `${(n.size ?? 16) * k}px`,
    fontWeight: n.bold ? 'bold' : 'normal',
    textAlign: (n.align as string) ?? 'left',
    padding: `${(n.padding ?? 0) * k}px`,
    opacity: String(n.opacity ?? 1),
    fontFamily: fontFamily(n.font, props.picture) ?? '',
    ...pictureStyle(n.image),
  }
})
// Over people's heads: drawn at a sample head's point, its bottom middle there (x, y move it), as wide as its text.
const overhead = computed(() => isOverhead(props.node.type))
const point = computed((): [number, number] => OVERHEAD_POINT[props.node.type] ?? [960, 500])
const overheadStyle = computed<Record<string, string>>(() => {
  const n = props.node, k = props.scale
  const kinds = n.kinds ?? {}
  return {
    ...style.value,
    left: `${(point.value[0] + (n.x ?? 0)) * k}px`,
    top: `${(point.value[1] - (n.y ?? 0)) * k}px`,
    width: n.wrap ? `${(n.w ?? 260) * k}px` : 'auto',
    maxWidth: `${(n.w ?? 260) * k}px`,
    height: 'auto',
    transform: 'translate(-50%, -100%)',
    whiteSpace: n.wrap ? 'normal' : 'nowrap',
    color: (n.type === 'numbers' ? colour(kinds.damage) : undefined) ?? style.value.color ?? '',
  }
})
const inner = computed(() => ({ w: box.value.width - (props.node.padding ?? 0) * 2, h: box.value.height - (props.node.padding ?? 0) * 2 }))
const rows = computed(() => Math.max(1, Math.min(6, Math.floor(box.value.height / (props.node.list?.rowH ?? 34)))))
const cells = computed(() => Math.max(1, Math.min(24, Math.floor(box.value.width / ((props.node.slots?.cell ?? 64) + (props.node.slots?.gap ?? 6))) * Math.floor(box.value.height / ((props.node.slots?.cell ?? 64) + (props.node.slots?.gap ?? 6))))))
// A list's sample row: its row text with sample data, numbered.
function rowText(i: number): string {
  return sampleText(props.node.list?.row ?? '{{item.label}}').replace('Potion', `Line ${i}`)
}
</script>

<template>
  <template v-if="overhead && !node.hidden">
    <div class="head-mark" :style="{ left: `${point[0] * scale}px`, top: `${point[1] * scale}px` }" title="Someone's head: where it shows over them"></div>
    <div class="ui-node over" :class="{ selected: selected === node.id, locked: node.locked }" :style="overheadStyle" :title="node.name"
         @mousedown.stop="emit('pick', node.id); !node.locked && emit('grab', node.id, 'move', $event)">
      {{ overheadSample(node.type, node.text) }}
      <UiDesignNode v-for="child in node.children ?? []" :key="child.id" :node="child" :pw="node.w ?? 260" :ph="(node.size ?? 16) * 2"
                    :scale="scale" :selected="selected" :tokens="tokens" :picture="picture"
                    @pick="(id) => emit('pick', id)" @grab="(id, mode, e) => emit('grab', id, mode, e)" />
    </div>
  </template>
  <div
    v-else-if="!node.hidden"
    class="ui-node"
    :class="[node.type, { selected: selected === node.id, locked: node.locked }]"
    :style="style"
    :title="node.name"
    @mousedown.stop="emit('pick', node.id); !node.locked && emit('grab', node.id, 'move', $event)"
  >
    <template v-if="node.type === 'text' || node.type === 'button'">{{ sampleText(node.text) }}</template>
    <div v-else-if="node.type === 'bar'" class="fill" :style="{ width: '70%', background: colour(node.bar?.color) ?? '#d64545' }"></div>
    <template v-else-if="node.type === 'list'">
      <div v-for="i in rows" :key="i" class="row" :style="{ height: `${(node.list?.rowH ?? 34) * scale}px` }">{{ rowText(i) }}</div>
    </template>
    <template v-else-if="node.type === 'slots'">
      <div v-for="i in cells" :key="i" class="slot"
           :style="{ width: `${(node.slots?.cell ?? 64) * scale}px`, height: `${(node.slots?.cell ?? 64) * scale}px`, margin: `0 ${(node.slots?.gap ?? 6) * scale}px ${(node.slots?.gap ?? 6) * scale}px 0`, background: colour(node.slots?.look?.bg) ?? colour('panel') }"></div>
    </template>
    <template v-else-if="node.type === 'input'"><input :type="node.input?.kind === 'slider' ? 'range' : node.input?.kind ?? 'checkbox'" disabled /></template>
    <div v-else-if="node.type === 'notes'" class="note">A notification</div>
    <div v-else-if="node.type === 'rml'" class="rml">Custom RML: shown in Preview</div>
    <UiDesignNode
      v-for="child in node.children ?? []"
      :key="child.id"
      :node="child"
      :pw="inner.w"
      :ph="inner.h"
      :scale="scale"
      :selected="selected"
      :tokens="tokens"
      :picture="picture"
      @pick="(id) => emit('pick', id)"
      @grab="(id, mode, e) => emit('grab', id, mode, e)"
    />
    <div v-if="selected === node.id && !node.locked" class="handle" @mousedown.stop="emit('grab', node.id, 'resize', $event)"></div>
  </div>
</template>

<style scoped>
.ui-node { position: absolute; box-sizing: border-box; overflow: hidden; outline: 1px dashed rgba(255, 255, 255, 0.08); cursor: move; white-space: pre-wrap; }
.ui-node.selected { outline: 2px solid #3b82f6; z-index: 2; overflow: visible; }
.ui-node.locked { cursor: default; }
.ui-node.button { display: flex; align-items: center; justify-content: center; }
.fill { height: 100%; }
.row { border-bottom: 1px solid rgba(255, 255, 255, 0.08); display: flex; align-items: center; }
.slot { display: inline-block; vertical-align: top; }
.note { text-align: center; background: rgba(0, 0, 0, 0.5); margin: 0 auto; width: 40%; padding: 4px; }
.rml { border: 1px dashed #e6a23c; color: #e6a23c; height: 100%; display: flex; align-items: center; justify-content: center; }
.ui-node.over { overflow: visible; }
.head-mark { position: absolute; width: 8px; height: 8px; margin: -4px 0 0 -4px; background: rgba(255, 255, 255, 0.35); pointer-events: none; }
.handle { position: absolute; right: -6px; bottom: -6px; width: 10px; height: 10px; background: #3b82f6; cursor: nwse-resize; }
</style>
