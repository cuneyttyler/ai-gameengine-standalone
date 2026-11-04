<script setup>
defineProps({ data: Object })
</script>

<template>
  <section id="roadmap" class="alt">
    <div class="wrap">
      <div class="section-head reveal">
        <span class="eyebrow">Roadmap</span>
        <h2>{{ data.title }}</h2>
        <p>{{ data.lead }}</p>
      </div>

      <ol class="timeline">
        <li v-for="v in data.versions" :key="v.v" class="reveal" :class="v.status">
          <div class="marker"><span>v{{ v.v }}</span></div>
          <div class="body">
            <div class="top">
              <h3>{{ v.name }}</h3>
              <span class="era">{{ v.era }}</span>
              <span v-if="v.status === 'current'" class="chip now">In development</span>
              <span v-if="v.status === 'goal'" class="chip goal">The goal</span>
            </div>
            <p>{{ v.text }}</p>
          </div>
        </li>
      </ol>

      <div class="every reveal">
        <h3>Every version ships with</h3>
        <div class="chips"><span v-for="e in data.everyVersion" :key="e" class="chip">{{ e }}</span></div>
      </div>
    </div>
  </section>
</template>

<style scoped>
.timeline { list-style: none; padding: 0; margin: 0; position: relative; }
.timeline::before { content: ''; position: absolute; left: 47px; top: 8px; bottom: 8px; width: 2px; background: linear-gradient(var(--cyan), var(--blue) 30%, var(--line) 70%, var(--gold)); }
.timeline li { display: grid; grid-template-columns: 96px minmax(0, 1fr); gap: 24px; padding: 14px 0; position: relative; }
.marker { display: flex; justify-content: center; align-items: flex-start; }
.marker span {
  position: relative; z-index: 1; min-width: 72px; text-align: center; padding: 7px 8px;
  font: 600 13px/1 var(--mono); color: var(--pale); background: var(--bg-2); border: 1px solid var(--line-2);
}
.current .marker span { background: var(--grad); color: #04101f; border-color: transparent; }
.goal .marker span { border-color: var(--gold); color: var(--gold); }
.body { padding: 4px 0 14px; border-bottom: 1px solid var(--line); }
.top { display: flex; flex-wrap: wrap; align-items: center; gap: 10px 14px; margin-bottom: 6px; }
.top h3 { font-size: 19px; }
.era { font: 500 12px/1 var(--mono); color: var(--dim); }
.body p { color: var(--muted); font-size: 15px; }
.now { border-color: var(--cyan); color: var(--cyan); }
.goal .chip.goal { border-color: var(--gold); color: var(--gold); background: rgba(245, 199, 107, 0.06); }
.current .body h3 { font-size: 22px; }
@media (max-width: 640px) {
  .timeline::before { left: 35px; }
  .timeline li { grid-template-columns: 72px minmax(0, 1fr); gap: 14px; }
  .marker span { min-width: 60px; font-size: 12px; }
}
.every { margin-top: 48px; padding: 28px; border: 1px solid var(--line); background: var(--panel); }
.every h3 { font-size: 17px; margin-bottom: 14px; }
.chips { display: flex; flex-wrap: wrap; gap: 8px; }
</style>
