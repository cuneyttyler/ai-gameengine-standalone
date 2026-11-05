<script setup lang="ts">
// A package template's (or procedure's) steps as an indented outline, in words: what the character does, in order.
// "@data:radius" shows as the package's own value when it has one.
import { computed } from 'vue'

interface Step { type?: string; children?: Step[]; [key: string]: unknown }
const props = defineProps<{ step: Step; data?: Record<string, unknown>; depth?: number }>()

const WORDS: Record<string, string> = {
  loop: 'again and again', sequence: 'in order', selector: 'the first that works', stack: 'the first that works',
  random: 'one at random', parallel: 'together', condition: 'only if', cooldown: 'not more often than', invert: 'the opposite of',
  'move-to': 'walk to', wander: 'wander', wait: 'wait', idle: 'stand', face: 'turn to', say: 'say', bark: 'say',
  'use-object': 'use a smart object', 'use-part': 'use the part', act: 'use', play: 'play the clip', 'talk-to-player': 'talk to the player',
  follow: 'follow', flee: 'run from', patrol: 'patrol', 'follow-path': 'walk the path', ambush: 'wait in ambush', escort: 'escort',
  set: 'set', emit: 'send the event', script: 'run the script',
}
// A value in words: a list of {action, weight} as "cook (weight 2), sweep", other objects as their fields.
function words(v: unknown): string {
  if (Array.isArray(v)) return v.map(words).join(', ')
  if (v && typeof v === 'object') {
    const o = v as Record<string, unknown>
    if (typeof o.action === 'string') return o.weight !== undefined && o.weight !== 1 ? `${o.action} (weight ${String(o.weight)})` : o.action
    return Object.entries(o).map(([k, x]) => `${k} ${words(x)}`).join(' ')
  }
  return String(v)
}
// "@data:waitMin=2": the package's value by that name, else the template's default (2).
function dataRef(v: string): { key: string; fallback?: string } {
  const [key, fallback] = v.slice(6).split('=', 2) as [string, string | undefined]
  return { key, fallback }
}
// Empty in this package: "@data:action" when it has none and no default (left out of the line).
function unset(v: unknown): boolean {
  if (typeof v !== 'string' || !v.startsWith('@data:') || !props.data) return false
  const { key, fallback } = dataRef(v)
  if (fallback !== undefined) return false
  const own = props.data[key]
  return own === undefined || own === '' || (Array.isArray(own) && !own.length)
}
function shown(v: unknown): string {
  if (typeof v === 'string' && v.startsWith('@data:')) {
    const { key, fallback } = dataRef(v)
    const own = props.data?.[key]
    if (own !== undefined && own !== '' && own !== null && (own !== 0 || fallback !== undefined)) return `${words(own)} (data.${key})`
    return fallback !== undefined ? `${fallback} (data.${key}, the template's)` : `data.${key}`
  }
  if (v === '@location') return 'the place'
  if (v === '@target') return 'the target'
  if (typeof v === 'string' && v.startsWith('@')) return v.slice(1)
  return words(v)
}
const params = computed(() =>
  Object.entries(props.step)
    .filter(([k, v]) => k !== 'type' && k !== 'children' && !unset(v))
    .map(([k, v]) => `${k === 'type_' ? 'type' : k} ${shown(v)}`),
)
const weights = computed(() => (Array.isArray(props.step.weights) ? (props.step.weights as unknown[]) : null))
// A weight in words: a number, or "@actionsWeight" (live: its actions' weights together).
function weightText(w: unknown): string {
  if (w !== '@actionsWeight') return shown(w)
  const list = Array.isArray(props.data?.actions) ? (props.data!.actions as Record<string, unknown>[]) : []
  const sum = list.reduce((t, a) => t + (typeof a?.weight === 'number' ? a.weight : 1), 0) || (props.data?.action ? 1 : 0)
  return `${sum} (the actions' weights together)`
}
</script>

<template>
  <div class="step" :style="{ paddingLeft: `${(depth ?? 0) * 14}px` }">
    <span class="kind" :class="{ group: !!step.children?.length }">{{ WORDS[step.type ?? ''] ?? step.type }}</span>
    <span class="params">{{ params.filter((p) => !p.startsWith('weights')).join(' · ') }}</span>
  </div>
  <template v-for="(child, i) in step.children ?? []" :key="i">
    <div v-if="weights" class="weight" :style="{ paddingLeft: `${((depth ?? 0) + 1) * 14}px` }">weight {{ weightText(weights[i]) }}:</div>
    <StepOutline :step="child" :data="data" :depth="(depth ?? 0) + (weights ? 2 : 1)" />
  </template>
</template>

<style scoped>
.step {
  display: flex;
  gap: 6px;
  font-size: 11.5px;
  line-height: 18px;
}
.kind {
  color: #93c5fd;
}
.kind.group {
  color: #c4b5fd;
  font-weight: 600;
}
.params {
  opacity: 0.75;
}
.weight {
  font-size: 10.5px;
  opacity: 0.55;
  line-height: 16px;
}
</style>
