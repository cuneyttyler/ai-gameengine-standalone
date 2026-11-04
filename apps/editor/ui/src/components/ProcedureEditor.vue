<script setup lang="ts">
// A package's own steps (template custom): its behaviour tree as JSON, edited with line numbers, Tab and Format, checked
// as it's typed (JSON, known steps, children where they go), and shown in words beside it as it's typed.
import { computed, nextTick, ref, watch } from 'vue'
import StepOutline from './StepOutline.vue'

const props = defineProps<{ value: unknown; data?: Record<string, unknown>; disabled?: boolean }>()
const emit = defineEmits<{ update: [value: string] }>()

const COMPOSITES = ['sequence', 'selector', 'stack', 'random', 'parallel', 'loop']
const DECORATORS = ['invert', 'condition', 'cooldown']
const LEAVES = ['move-to', 'wander', 'wait', 'idle', 'face', 'say', 'bark', 'use-object', 'use-part', 'act', 'play', 'talk-to-player',
  'follow', 'flee', 'patrol', 'follow-path', 'ambush', 'escort', 'set', 'emit', 'script']

const area = ref<HTMLTextAreaElement | null>(null)
const gutter = ref<HTMLElement | null>(null)
const text = ref(typeof props.value === 'string' ? props.value : props.value ? JSON.stringify(props.value, null, 2) : '')
watch(() => props.value, (v) => {
  const next = typeof v === 'string' ? v : v ? JSON.stringify(v, null, 2) : ''
  if (next !== text.value) text.value = next
})
const lines = computed(() => Math.max(1, text.value.split('\n').length))

interface Step { type?: string; children?: Step[]; [k: string]: unknown }
const parsed = computed<{ tree: Step | null; error: string }>(() => {
  if (!text.value.trim()) return { tree: null, error: 'Empty: write its steps, e.g. {"type": "loop", "children": [...]}' }
  try {
    const tree = JSON.parse(text.value)
    if (!tree || typeof tree !== 'object' || Array.isArray(tree)) return { tree: null, error: 'It must be one step: {"type": ...}' }
    return { tree, error: check(tree as Step, 'root') }
  } catch (e) {
    return { tree: null, error: `Not JSON yet: ${(e as Error).message}` }
  }
})
function check(step: Step, path: string): string {
  const kind = step.type ?? ''
  if (![...COMPOSITES, ...DECORATORS, ...LEAVES].includes(kind)) return `${path}: "${kind}" isn't a step (steps: ${[...COMPOSITES, ...DECORATORS, ...LEAVES].join(', ')})`
  const children = step.children ?? []
  if (COMPOSITES.includes(kind) && !children.length) return `${path} (${kind}) needs children`
  if (DECORATORS.includes(kind) && children.length !== 1) return `${path} (${kind}) takes exactly one child`
  if (LEAVES.includes(kind) && children.length) return `${path} (${kind}) has no children`
  if (kind === 'random' && Array.isArray(step.weights) && step.weights.length !== children.length) return `${path} (random): one weight per child`
  for (let i = 0; i < children.length; i++) {
    const problem = check(children[i]!, `${path}.${i}`)
    if (problem) return problem
  }
  return ''
}

function changed(): void {
  emit('update', text.value)
}
function format(): void {
  if (!parsed.value.tree) return
  text.value = JSON.stringify(parsed.value.tree, null, 2)
  changed()
}
function onKey(event: KeyboardEvent): void {
  if (event.key !== 'Tab') return
  event.preventDefault()
  const el = area.value!
  const start = el.selectionStart, end = el.selectionEnd
  text.value = text.value.slice(0, start) + '  ' + text.value.slice(end)
  nextTick(() => (el.selectionStart = el.selectionEnd = start + 2))
  changed()
}
function syncScroll(): void {
  if (gutter.value && area.value) gutter.value.scrollTop = area.value.scrollTop
}
</script>

<template>
  <div class="procedure">
    <div class="code">
      <div ref="gutter" class="gutter"><div v-for="n in lines" :key="n">{{ n }}</div></div>
      <textarea ref="area" v-model="text" spellcheck="false" wrap="off" :disabled="disabled" @input="changed" @keydown="onKey" @scroll="syncScroll"></textarea>
    </div>
    <div class="bar">
      <span v-if="parsed.error" class="error">{{ parsed.error }}</span>
      <span v-else class="ok">✓ Its steps are sound</span>
      <span class="grow"></span>
      <button class="small" :disabled="disabled || !parsed.tree" title="Lay the JSON out tidily" @click="format">Format</button>
    </div>
    <div v-if="parsed.tree" class="outline"><StepOutline :step="parsed.tree" :data="data ?? {}" /></div>
  </div>
</template>

<style scoped>
.procedure {
  display: flex;
  flex-direction: column;
  gap: 4px;
  flex: 1;
  min-width: 0;
}
.code {
  display: flex;
  height: 300px;
  border: 1px solid #2f2f40;
  background: #15151c;
  font-family: Consolas, 'Cascadia Mono', monospace;
  font-size: 12px;
  line-height: 17px;
}
.gutter {
  overflow: hidden;
  padding: 4px 6px;
  text-align: right;
  color: #6b7280;
  user-select: none;
  min-width: 30px;
  border-right: 1px solid #2a2a3a;
}
textarea {
  flex: 1;
  resize: none;
  border: none;
  outline: none;
  background: transparent;
  color: #e5e7eb;
  padding: 4px 6px;
  font: inherit;
  line-height: inherit;
  white-space: pre;
}
.bar {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 11px;
}
.grow {
  flex: 1;
}
.error {
  color: #f87171;
}
.ok {
  color: #4ade80;
}
.outline {
  border: 1px solid #2f2f40;
  background: #17171f;
  padding: 6px 8px;
}
</style>
