<script setup lang="ts">
// A select box with a search field at the top of its list: for long lists (items, prefabs, pictures, entities).
import { computed, nextTick, onBeforeUnmount, ref } from 'vue'

export interface Option {
  value: string
  label?: string
  hint?: string // shown dim beside it (a path, a kind)
}
const props = defineProps<{ modelValue: string; options: Option[]; disabled?: boolean; placeholder?: string; empty?: string; custom?: boolean }>()
const emit = defineEmits<{ (e: 'update:modelValue', value: string): void }>()

const open = ref(false)
const query = ref('')
const highlighted = ref(0)
const root = ref<HTMLElement | null>(null)
const search = ref<HTMLInputElement | null>(null)

const current = computed(() => props.options.find((o) => o.value === props.modelValue))
const shown = computed(() => {
  const words = query.value.toLowerCase().split(/\s+/).filter(Boolean)
  const list = props.options.filter((o) => words.every((w) => `${o.label ?? ''} ${o.value} ${o.hint ?? ''}`.toLowerCase().includes(w)))
  return list.slice(0, 300)
})

async function toggle(): Promise<void> {
  if (props.disabled) return
  open.value = !open.value
  if (!open.value) return
  query.value = ''
  highlighted.value = Math.max(0, shown.value.findIndex((o) => o.value === props.modelValue))
  await nextTick()
  search.value?.focus()
  document.addEventListener('mousedown', outside, true)
}
function pick(value: string): void {
  emit('update:modelValue', value)
  close()
}
function close(): void {
  open.value = false
  document.removeEventListener('mousedown', outside, true)
}
function outside(event: MouseEvent): void {
  if (root.value && !root.value.contains(event.target as Node)) close()
}
function onKey(event: KeyboardEvent): void {
  if (event.key === 'ArrowDown') (highlighted.value = Math.min(shown.value.length - 1, highlighted.value + 1)), event.preventDefault()
  else if (event.key === 'ArrowUp') (highlighted.value = Math.max(0, highlighted.value - 1)), event.preventDefault()
  else if (event.key === 'Enter') {
    event.preventDefault()
    const o = shown.value[highlighted.value]
    if (o) pick(o.value)
    else if (props.custom && query.value.trim()) pick(query.value.trim())
  } else if (event.key === 'Escape') (close(), event.stopPropagation())
}
onBeforeUnmount(() => document.removeEventListener('mousedown', outside, true))
</script>

<template>
  <span ref="root" class="search-select" :class="{ off: disabled }">
    <button type="button" class="current" :disabled="disabled" :title="modelValue" @click="toggle">
      <span class="text">{{ current ? current.label ?? current.value : modelValue || (empty ?? placeholder ?? 'none') }}</span>
      <span class="caret">▾</span>
    </button>
    <div v-if="open" class="drop">
      <input ref="search" v-model="query" class="find" spellcheck="false" :placeholder="custom ? 'Search, or type a new one' : 'Search'"
             @input="highlighted = 0" @keydown="onKey" />
      <div class="list">
        <div v-if="empty !== undefined" class="option" :class="{ on: !modelValue }" @mousedown.prevent="pick('')">{{ empty }}</div>
        <div v-for="(o, i) in shown" :key="o.value" class="option" :class="{ on: o.value === modelValue, lit: i === highlighted }"
             @mousedown.prevent="pick(o.value)" @mousemove="highlighted = i">
          {{ o.label ?? o.value }} <span v-if="o.hint" class="hint">{{ o.hint }}</span>
        </div>
        <div v-if="custom && query.trim() && !shown.some((o) => o.value === query.trim())" class="option new" @mousedown.prevent="pick(query.trim())">
          Use “{{ query.trim() }}”
        </div>
        <div v-if="!shown.length && !(custom && query.trim())" class="empty">Nothing matches</div>
      </div>
    </div>
  </span>
</template>

<style scoped>
.search-select { position: relative; display: inline-block; min-width: 120px; flex: 1; }
.search-select.off { opacity: 0.6; }
.current { width: 100%; display: flex; justify-content: space-between; align-items: center; gap: 6px; text-align: left; }
.text { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.caret { opacity: 0.6; }
.drop { position: absolute; z-index: 3000; left: 0; right: 0; top: 100%; min-width: 220px; background: #1e1e28; border: 1px solid #3a3a4c; box-shadow: 0 6px 18px rgba(0, 0, 0, 0.45); }
.find { width: 100%; box-sizing: border-box; }
.list { max-height: 260px; overflow-y: auto; }
.option { padding: 3px 8px; cursor: pointer; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.option.lit { background: #2b3550; }
.option.on { color: #8ab4ff; }
.option.new { font-style: italic; }
.hint { opacity: 0.55; font-size: 0.85em; margin-left: 6px; }
.empty { padding: 4px 8px; opacity: 0.6; }
</style>
