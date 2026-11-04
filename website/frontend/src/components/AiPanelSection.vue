<script setup>
import { onMounted, onUnmounted, ref } from 'vue'

defineProps({ data: Object })

// A short, looping demo of a panel conversation.
const script = [
  { who: 'you', text: 'Populate this village: one or more people in each building, named, with gossip.' },
  { who: 'ai', text: 'Outline — waiting for you: Willowmere, a river village. Market: Nella Voss, provisions; Tovin Reed, ropes and nets…' },
  { who: 'you', text: 'Approve and make it' },
  { who: 'ai', text: 'Step 3 of about 6 · placing the Market residents on cells inside their buildings, each with a Talk and their dialogue.' },
  { who: 'ai', text: 'Done in 6 steps. 34 changes to review: World Bible, 30 people, 30 dialogues. Apply all?' },
]
const shown = ref(1)
let timer
onMounted(() => {
  timer = setInterval(() => { shown.value = shown.value >= script.length ? 1 : shown.value + 1 }, 2200)
})
onUnmounted(() => clearInterval(timer))
</script>

<template>
  <section id="ai-panel" class="alt">
    <div class="wrap layout">
      <div>
        <div class="section-head reveal">
          <span class="eyebrow">AI Panel</span>
          <h2>{{ data.title }}</h2>
          <p>{{ data.lead }}</p>
        </div>
        <ul class="kinds reveal">
          <li v-for="kind in data.kinds" :key="kind.name">
            <span class="cube"></span>
            <div><b>{{ kind.name }}</b><span>{{ kind.text }}</span></div>
          </li>
        </ul>
      </div>

      <div class="panel reveal" aria-label="An example AI panel conversation">
        <div class="panel-head"><span class="cube"></span>AI</div>
        <TransitionGroup name="msg" tag="div" class="messages">
          <div v-for="(m, i) in script.slice(0, shown)" :key="i" class="msg" :class="m.who">{{ m.text }}</div>
        </TransitionGroup>
        <div class="input"><span>Ask the AI to change something…</span><span class="send">↵</span></div>
      </div>
    </div>

    <div class="wrap">
      <div class="grid three caps">
        <div v-for="cap in data.capabilities" :key="cap.title" class="card reveal">
          <h3>{{ cap.title }}</h3>
          <p>{{ cap.text }}</p>
        </div>
      </div>
      <ul class="safety reveal">
        <li v-for="line in data.safety" :key="line">✓ {{ line }}</li>
      </ul>
    </div>
  </section>
</template>

<style scoped>
.layout { display: grid; grid-template-columns: minmax(0, 1.05fr) minmax(0, 1fr); gap: 56px; align-items: start; margin-bottom: 56px; }
@media (max-width: 960px) { .layout { grid-template-columns: minmax(0, 1fr); } }
.section-head { margin-bottom: 32px; }
.kinds { list-style: none; padding: 0; margin: 0; display: grid; gap: 14px; }
.kinds li { display: flex; gap: 14px; align-items: flex-start; }
.kinds .cube { margin-top: 6px; }
.kinds b { display: block; font-weight: 700; }
.kinds span { color: var(--muted); font-size: 15px; }

.panel { background: #0b1124; border: 1px solid var(--line-2); display: flex; flex-direction: column; min-height: 440px; }
.panel-head { display: flex; align-items: center; gap: 10px; padding: 12px 16px; border-bottom: 1px solid var(--line); font: 600 13px/1 var(--mono); color: var(--pale); background: var(--panel); }
.messages { flex: 1; padding: 18px; display: flex; flex-direction: column; gap: 12px; }
.msg { padding: 12px 14px; font-size: 14px; max-width: 90%; border: 1px solid var(--line); }
.msg.you { align-self: flex-end; background: rgba(47, 124, 246, 0.16); border-color: rgba(47, 124, 246, 0.45); }
.msg.ai { align-self: flex-start; background: var(--panel); color: var(--pale); }
.input { display: flex; justify-content: space-between; margin: 0 18px 18px; padding: 12px 14px; border: 1px solid var(--line-2); color: var(--dim); font-size: 14px; }
.send { color: var(--cyan); }
.msg-enter-active { transition: opacity 0.35s, transform 0.35s; }
.msg-enter-from { opacity: 0; transform: translateY(8px); }

.caps .card h3 { font-size: 17px; }
.safety { list-style: none; padding: 0; margin: 36px 0 0; display: flex; flex-wrap: wrap; gap: 12px 32px; color: var(--muted); font-size: 15px; }
</style>
