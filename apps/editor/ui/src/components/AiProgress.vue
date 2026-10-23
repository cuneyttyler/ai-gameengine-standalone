<script setup lang="ts">
// The AI calls in flight that belong to this panel (by the service path they call): what each is
// doing (thinking, answering, fixing problems), the tokens it has written so far and how long it
// has taken. Nothing shows when nothing runs.
import { computed, onBeforeUnmount, ref } from 'vue'
import { aiCalls, aiCallsAt, callName, type AiCall } from '../aiProgress'

const props = withDefaults(defineProps<{ paths?: string[] }>(), { paths: () => [] })

const shown = computed(() =>
  aiCalls.value.filter((c) => props.paths.length === 0 || props.paths.some((p) => c.path === p || c.path.startsWith(p))),
)

// The seconds tick between reports.
const now = ref(performance.now())
const timer = window.setInterval(() => (now.value = performance.now()), 250)
onBeforeUnmount(() => window.clearInterval(timer))

function seconds(call: AiCall): string {
  const running = call.phase !== 'done' && call.phase !== 'failed'
  const s = call.seconds + (running ? Math.max(0, (now.value - aiCallsAt.value) / 1000) : 0)
  return s < 60 ? `${Math.floor(s)} s` : `${Math.floor(s / 60)} min ${Math.floor(s % 60)} s`
}
function phase(call: AiCall): string {
  return (
    { waiting: 'waiting for the model', thinking: 'thinking', answering: 'writing', repairing: 'finishing', done: 'done', failed: 'failed' }[
      call.phase
    ] ?? call.phase
  )
}
function tokens(n: number): string {
  return n.toLocaleString()
}
</script>

<template>
  <div v-if="shown.length" class="ai-progress">
    <div v-for="c in shown" :key="c.id" :class="['call', c.phase]">
      <div class="line">
        <span class="name">{{ callName(c) }}</span>
        <span class="phase">{{ phase(c) }}</span>
        <span class="spacer"></span>
        <span class="tokens" :title="`Thinking ${tokens(c.thinkingTokens)} · answer ${tokens(c.answerTokens)} · read ${tokens(c.inputTokens)}`">
          {{ tokens(c.outputTokens) }} tokens
        </span>
        <span class="time">{{ seconds(c) }}</span>
      </div>
      <div class="track"><div class="run"></div></div>
    </div>
  </div>
</template>

<style scoped>
.ai-progress {
  border-bottom: 1px solid #2a2a3a;
  background: #1c1c28;
  font-size: 0.85em;
}
.call {
  padding: 3px 8px 0;
}
.line {
  display: flex;
  gap: 8px;
  align-items: baseline;
  white-space: nowrap;
}
.name {
  font-weight: 600;
  overflow: hidden;
  text-overflow: ellipsis;
  min-width: 0;
}
.phase {
  color: #93c5fd;
}
.call.thinking .phase {
  color: #c4b5fd;
}
.call.repairing .phase {
  color: #93c5fd;
}
.call.done .phase {
  color: #4ade80;
}
.call.failed .phase {
  color: #f87171;
}
.spacer {
  flex: 1;
}
.tokens,
.time {
  font-variant-numeric: tabular-nums;
  color: #c8c8d8;
}
.time {
  min-width: 52px;
  text-align: right;
}
.muted {
  color: #8a8aa0;
}
/* No percentage (the model doesn't know how long its answer will be): a bar that runs while it works. */
.track {
  height: 2px;
  margin-top: 3px;
  background: #2a2a3a;
  overflow: hidden;
  position: relative;
}
.run {
  position: absolute;
  top: 0;
  bottom: 0;
  width: 30%;
  background: #3b82f6;
  animation: run 1.4s linear infinite;
}
.call.thinking .run {
  background: #8b5cf6;
}
.call.repairing .run {
  background: #3b82f6;
}
.call.done .run,
.call.failed .run {
  animation: none;
  width: 100%;
  left: 0;
}
.call.done .run {
  background: #4ade80;
}
.call.failed .run {
  background: #f87171;
}
@keyframes run {
  from {
    left: -30%;
  }
  to {
    left: 100%;
  }
}
</style>
