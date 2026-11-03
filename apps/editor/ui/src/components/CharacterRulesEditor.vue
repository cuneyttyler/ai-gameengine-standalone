<script setup lang="ts">
import { plainCopy } from '../plainCopy'
// A character's rules (Responses): when something happens, what they do. Saved as the JSON text the engine reads.
import { computed } from 'vue'
import RulesEditor from './RulesEditor.vue'

interface Rule {
  event?: string
  if?: unknown[]
  do?: unknown[]
  says?: string
  clip?: string
  range?: number
  once?: boolean
}
const props = defineProps<{ value: unknown; disabled?: boolean }>()
const emit = defineEmits<{ (e: 'update', text: string): void }>()

const rules = computed<Rule[] | null>(() => {
  if (!props.value) return []
  try {
    const list = JSON.parse(String(props.value))
    return Array.isArray(list) ? list : null
  } catch {
    return null
  }
})
function write(next: Rule[]): void {
  emit('update', next.length ? JSON.stringify(next) : '')
}
function set(i: number, patch: Partial<Rule>): void {
  const next = plainCopy(rules.value ?? [])
  next[i] = { ...next[i], ...patch }
  write(next)
}
function setList(i: number, key: 'if' | 'do', text: string): void {
  const next = plainCopy(rules.value ?? [])
  const rule = next[i]!
  if (text) rule[key] = JSON.parse(text)
  else delete rule[key]
  write(next)
}
</script>

<template>
  <div class="character-rules">
    <p v-if="!rules" class="warn">These rules aren't a list, so they do nothing.
      <button class="small" :disabled="disabled" @click="write([])">Start again</button></p>
    <div v-for="(r, i) in rules ?? []" :key="i" class="rule">
      <label title="An event's name, quest:<id>:<stage> for a quest stage, or action:<name> for the player's own action">When
        <input :value="r.event ?? ''" spellcheck="false" :disabled="disabled" placeholder="alarm, quest:q-main:20, action:cook"
               @change="set(i, { event: ($event.target as HTMLInputElement).value })" /></label>
      <label>Says <input :value="r.says ?? ''" :disabled="disabled" @change="set(i, { says: ($event.target as HTMLInputElement).value })" /></label>
      <label>Clip <input :value="r.clip ?? ''" spellcheck="false" :disabled="disabled" @change="set(i, { clip: ($event.target as HTMLInputElement).value })" /></label>
      <label title="Only with the player this near, in cells (0: anywhere)">Range
        <input type="number" min="0" step="0.5" :value="r.range ?? 0" :disabled="disabled" @change="set(i, { range: Number(($event.target as HTMLInputElement).value) })" /></label>
      <label class="inline"><input type="checkbox" :checked="!!r.once" :disabled="disabled" @change="set(i, { once: ($event.target as HTMLInputElement).checked })" /> Only once</label>
      <div class="muted">Only if</div>
      <RulesEditor mode="if" :value="r.if ?? ''" :disabled="disabled" @update="setList(i, 'if', $event)" />
      <div class="muted">Then</div>
      <RulesEditor mode="do" :value="r.do ?? ''" :disabled="disabled" @update="setList(i, 'do', $event)" />
      <button class="small" :disabled="disabled" @click="write((rules ?? []).filter((_, k) => k !== i))">Remove this rule</button>
    </div>
    <button class="small" :disabled="disabled || !rules" @click="write([...(rules ?? []), { event: '' }])">Add a rule</button>
  </div>
</template>

<style scoped>
.character-rules { display: flex; flex-direction: column; gap: 6px; width: 100%; }
.rule { display: flex; flex-direction: column; gap: 3px; padding: 6px; border: 1px solid #2f2f3f; }
.rule label { display: flex; justify-content: space-between; align-items: center; gap: 8px; }
.rule label.inline { justify-content: flex-start; }
.muted { opacity: 0.6; font-size: 0.85em; }
.warn { color: #e6a23c; font-size: 0.85em; }
</style>
