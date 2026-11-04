<script setup>
import { computed, ref, watch } from 'vue'

const props = defineProps({ data: Object })
const method = ref('assembled')
const steps = computed(() => props.data.steps.filter((s) => s.methods.includes(method.value)))
const active = ref('confirm')
watch(method, () => { if (!steps.value.some((s) => s.key === active.value)) active.value = 'confirm' })
const current = computed(() => steps.value.find((s) => s.key === active.value) ?? steps.value[0])
const circled = (n) => String.fromCodePoint(0x2460 + n - 1)
</script>

<template>
  <section id="builder" class="alt">
    <div class="wrap">
      <div class="section-head reveal">
        <span class="eyebrow">Builder</span>
        <h2>{{ data.title }}</h2>
        <p>{{ data.lead }}</p>
      </div>

      <div class="toggle reveal" role="radiogroup" aria-label="Building method">
        <button role="radio" :aria-checked="method === 'assembled'" :class="{ on: method === 'assembled' }" @click="method = 'assembled'">Assembled</button>
        <button role="radio" :aria-checked="method === 'paint'" :class="{ on: method === 'paint' }" @click="method = 'paint'">Paint</button>
        <span class="hint">{{ steps.length }} steps</span>
      </div>

      <TransitionGroup name="step" tag="ol" class="steps reveal">
        <li v-for="(s, i) in steps" :key="s.key">
          <button :class="{ on: current.key === s.key, only: s.methods.length === 1 }" @click="active = s.key">
            <span class="circ">{{ circled(i + 1) }}</span>
            <span class="name">{{ s.name }}</span>
            <span v-if="s.methods.length === 1" class="tag">Paint only</span>
          </button>
        </li>
      </TransitionGroup>

      <div class="detail card reveal">
        <Transition name="swap" mode="out-in">
          <div :key="current.key + method">
            <h3>{{ current.name }}</h3>
            <p>{{ current.text }}</p>
          </div>
        </Transition>
      </div>

      <div class="after grid four">
        <div v-for="a in data.after" :key="a.name" class="reveal">
          <h4>{{ a.name }}</h4>
          <p>{{ a.text }}</p>
        </div>
      </div>

      <div class="types reveal">
        <span class="label">Build types</span>
        <span v-for="t in data.buildTypes" :key="t" class="chip">{{ t }}</span>
      </div>
    </div>
  </section>
</template>

<style scoped>
.toggle { display: inline-flex; align-items: center; border: 1px solid var(--line-2); margin-bottom: 24px; }
.toggle button { padding: 11px 22px; background: none; border: 0; color: var(--muted); font: 600 14px/1 Inter, sans-serif; cursor: pointer; }
.toggle button.on { background: var(--grad); color: #04101f; }
.hint { padding: 0 16px; font: 500 12px/1 var(--mono); color: var(--dim); }

.steps { list-style: none; padding: 0; margin: 0; display: flex; flex-wrap: wrap; gap: 0; border: 1px solid var(--line); }
.steps li { flex: 1 1 0; min-width: 120px; border-right: 1px solid var(--line); }
.steps li:last-child { border-right: 0; }
.steps button {
  width: 100%; height: 100%; display: flex; flex-direction: column; align-items: flex-start; gap: 6px;
  padding: 18px; background: var(--panel); border: 0; border-top: 2px solid transparent; color: var(--muted);
  cursor: pointer; text-align: left; font: 600 15px/1.2 Inter, sans-serif;
}
.steps button:hover { color: var(--text); }
.steps button.on { color: #fff; border-top-color: var(--cyan); background: var(--panel-2); }
.circ { color: var(--cyan); font-size: 20px; font-weight: 500; }
.tag { font: 500 10px/1 var(--mono); letter-spacing: 0.08em; text-transform: uppercase; color: var(--gold); }
@media (max-width: 760px) { .steps li { flex: 1 1 33%; border-bottom: 1px solid var(--line); } }
.step-enter-active, .step-leave-active { transition: opacity 0.25s; }
.step-enter-from, .step-leave-to { opacity: 0; }

.detail { margin-top: 20px; min-height: 140px; border-left: 3px solid var(--blue); }
.detail h3 { font-size: 24px; }
.detail p { font-size: 17px; max-width: 820px; }
.swap-enter-active, .swap-leave-active { transition: opacity 0.18s; }
.swap-enter-from, .swap-leave-to { opacity: 0; }

.after { margin-top: 48px; }
.after h4 { font-size: 16px; margin-bottom: 6px; }
.after p { color: var(--muted); font-size: 15px; }
.types { margin-top: 40px; display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
.label { font: 500 12px/1 var(--mono); text-transform: uppercase; letter-spacing: 0.12em; color: var(--dim); margin-right: 8px; }
</style>
