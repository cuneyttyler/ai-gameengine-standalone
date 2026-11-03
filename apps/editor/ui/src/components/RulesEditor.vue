<script setup lang="ts">
import { plainCopy } from '../plainCopy'
// A thing's "if" or "do" (a Reaction's, a moving part's, a recipe's, a character rule's), picked from lists as the story
// editor's are, and saved as the JSON text the engine reads. Text that isn't a list stays editable as text, marked.
import { onMounted, ref, watch } from 'vue'
import { onHostMessage, sendToHost } from '../ipc'
import ConditionList, { type Condition } from './story/ConditionList.vue'
import EffectList, { type Effect } from './story/EffectList.vue'

const props = defineProps<{ mode: 'if' | 'do'; value: unknown; disabled?: boolean }>()
const emit = defineEmits<{ (e: 'update', text: string): void }>()

// The project's quests, for the lists to name.
const quests = ref<string[]>([])
const details = new Map<string, { stages?: { index: number }[]; objectives?: { id: string }[] }>()
onHostMessage((message) => {
  if (message.type !== 'story:list') return
  const list = (message.quests as { id: string; content?: { stages?: { index: number }[]; objectives?: { id: string }[] } }[]) ?? []
  quests.value = list.map((q) => q.id)
  details.clear()
  for (const q of list) details.set(q.id, q.content ?? {})
})
onMounted(() => {
  if (!quests.value.length) sendToHost({ type: 'story:request' })
})
const stagesOf = (id: string) => (details.get(id)?.stages ?? []).map((s) => s.index)
const objectivesOf = (id: string) => (details.get(id)?.objectives ?? []).map((o) => o.id)

function parse(value: unknown): unknown[] | null {
  if (Array.isArray(value)) return plainCopy(value)
  if (value === '' || value === null || value === undefined) return []
  try {
    const list = JSON.parse(String(value))
    return Array.isArray(list) ? list : null
  } catch {
    return null
  }
}
const list = ref<unknown[] | null>(parse(props.value))
const raw = ref(String(props.value ?? ''))
let last = JSON.stringify(list.value ?? [])
// From outside (another part picked, an undo): read again. Our own change isn't one.
watch(
  () => props.value,
  (value) => {
    const next = parse(value)
    if (JSON.stringify(next ?? []) === last) return
    list.value = next
    raw.value = String(value ?? '')
    last = JSON.stringify(next ?? [])
  },
)
watch(
  list,
  (now) => {
    if (!now) return
    const text = JSON.stringify(now)
    if (text === last) return
    last = text
    emit('update', now.length ? text : '')
  },
  { deep: true },
)
</script>

<template>
  <div class="rules" :class="{ off: disabled }">
    <template v-if="list">
      <ConditionList v-if="mode === 'if'" :conditions="list as Condition[]" :quests="quests" :stages-of="stagesOf" />
      <EffectList v-else :effects="list as Effect[]" :quests="quests" :objectives-of="objectivesOf" :stages-of="stagesOf" />
    </template>
    <template v-else>
      <input class="bad" :value="raw" spellcheck="false" :disabled="disabled" @change="emit('update', ($event.target as HTMLInputElement).value)" />
      <p class="warn">This isn't a list, so as written it never holds and does nothing.</p>
      <button class="small" :disabled="disabled" @click="list = []">Start again</button>
    </template>
  </div>
</template>

<style scoped>
.rules { width: 100%; }
.rules.off { opacity: 0.6; pointer-events: none; }
.bad { border-color: #ef4444; width: 100%; }
.warn { color: #e6a23c; font-size: 0.82em; margin: 2px 0; }
</style>
