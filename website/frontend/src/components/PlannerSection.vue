<script setup>
import { computed, ref } from 'vue'

const props = defineProps({ data: Object })
const active = ref(1)
const step = computed(() => props.data.steps.find((s) => s.n === active.value))
const circled = (n) => String.fromCodePoint(0x2460 + n - 1)
const move = (by) => {
  const n = active.value + by
  if (n >= 1 && n <= props.data.steps.length) active.value = n
}
</script>

<template>
  <section id="planner">
    <div class="wrap">
      <div class="section-head reveal">
        <span class="eyebrow">AI Planner</span>
        <h2>{{ data.title }}</h2>
        <p>{{ data.lead }}</p>
      </div>

      <div class="planner reveal">
        <ol class="rail" role="tablist" aria-label="Planner stages">
          <template v-for="group in data.groups" :key="group.name">
            <li class="group">{{ group.name }} <span>{{ group.range }}</span></li>
            <li v-for="s in data.steps.filter((x) => x.group === group.name)" :key="s.n">
              <button
                role="tab"
                :aria-selected="active === s.n"
                :class="{ on: active === s.n, done: s.n < active }"
                @click="active = s.n"
              >
                <span class="circ">{{ circled(s.n) }}</span>{{ s.name }}
              </button>
            </li>
          </template>
        </ol>

        <div class="detail" role="tabpanel">
          <Transition name="swap" mode="out-in">
            <div :key="step.n" class="detail-body">
              <span class="chip">{{ step.group }} · stage {{ step.n }} of {{ data.steps.length }}</span>
              <h3><span class="big">{{ circled(step.n) }}</span> {{ step.name }}</h3>
              <p>{{ step.text }}</p>
            </div>
          </Transition>
          <div class="progress" aria-hidden="true">
            <span :style="{ width: `${(active / data.steps.length) * 100}%` }"></span>
          </div>
          <div class="nav">
            <button class="btn ghost" :disabled="active === 1" @click="move(-1)">← Previous</button>
            <button class="btn ghost" :disabled="active === data.steps.length" @click="move(1)">Next →</button>
          </div>
        </div>
      </div>

      <div class="grid four controls">
        <div v-for="c in data.controls" :key="c.name" class="card reveal">
          <h3>{{ c.name }}</h3>
          <p>{{ c.text }}</p>
        </div>
      </div>
      <p class="genres reveal">
        Genre profiles in 1.0:
        <span v-for="g in data.genres" :key="g" class="chip">{{ g }}</span>
      </p>
    </div>
  </section>
</template>

<style scoped>
.planner { display: grid; grid-template-columns: 300px minmax(0, 1fr); border: 1px solid var(--line); background: var(--panel); }
@media (max-width: 860px) { .planner { grid-template-columns: minmax(0, 1fr); } }
.rail { list-style: none; margin: 0; padding: 12px 0; border-right: 1px solid var(--line); background: #0e1530; }
@media (max-width: 860px) {
  .rail { display: flex; overflow-x: auto; border-right: 0; border-bottom: 1px solid var(--line); padding: 8px; gap: 4px; }
  .rail .group { display: none; }
  .rail button { white-space: nowrap; }
}
.group { padding: 14px 20px 6px; font: 500 11px/1 var(--mono); letter-spacing: 0.12em; text-transform: uppercase; color: var(--dim); }
.group span { color: var(--line-2); margin-left: 6px; }
.rail button {
  width: 100%; display: flex; align-items: center; gap: 12px; text-align: left;
  padding: 10px 20px; background: none; border: 0; border-left: 2px solid transparent;
  color: var(--muted); font: 500 15px/1.2 Inter, sans-serif; cursor: pointer;
}
.rail button:hover { color: var(--text); background: rgba(255, 255, 255, 0.02); }
.rail button.done { color: var(--pale); }
.rail button.on { color: #fff; border-left-color: var(--cyan); background: rgba(47, 124, 246, 0.12); }
.circ { font-size: 17px; color: var(--cyan); width: 18px; }

.detail { padding: 44px; display: flex; flex-direction: column; min-height: 420px; }
@media (max-width: 640px) { .detail { padding: 24px; } }
.detail-body h3 { font-size: clamp(28px, 3.4vw, 40px); font-weight: 800; margin: 22px 0 18px; display: flex; align-items: center; gap: 14px; }
.big { color: var(--cyan); font-weight: 500; }
.detail-body p { color: var(--muted); font-size: 18px; max-width: 640px; }
.progress { margin-top: auto; height: 3px; background: var(--line); }
.progress span { display: block; height: 100%; background: var(--grad); transition: width 0.35s; }
.nav { display: flex; justify-content: space-between; margin-top: 20px; }
.nav .btn:disabled { opacity: 0.35; cursor: default; transform: none; }
.swap-enter-active, .swap-leave-active { transition: opacity 0.2s, transform 0.2s; }
.swap-enter-from { opacity: 0; transform: translateX(10px); }
.swap-leave-to { opacity: 0; transform: translateX(-10px); }

.controls { margin-top: 28px; }
.controls .card h3 { font-size: 16px; }
.genres { margin-top: 28px; color: var(--muted); display: flex; flex-wrap: wrap; align-items: center; gap: 10px; }
</style>
