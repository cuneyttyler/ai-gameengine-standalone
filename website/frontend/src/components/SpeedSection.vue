<script setup>
defineProps({ data: Object })
</script>

<template>
  <section id="speed">
    <div class="wrap">
      <div class="section-head reveal">
        <span class="eyebrow">Faster</span>
        <h2>{{ data.title }}</h2>
        <p>{{ data.lead }}</p>
      </div>
      <div class="table reveal" role="table" aria-label="Time compared">
        <div class="row head" role="row">
          <span role="columnheader">The work</span>
          <span role="columnheader">The old way</span>
          <span role="columnheader">With AIGE</span>
        </div>
        <div v-for="r in data.rows" :key="r.task" class="row" role="row">
          <span role="cell" class="task">{{ r.task }}</span>
          <span role="cell" class="before">{{ r.before }}</span>
          <span role="cell" class="after">{{ r.after }}</span>
        </div>
      </div>
    </div>
  </section>
</template>

<style scoped>
.table { border: 1px solid var(--line); }
.row { display: grid; grid-template-columns: minmax(0, 1.2fr) minmax(0, 0.9fr) minmax(0, 1.1fr); border-bottom: 1px solid var(--line); }
.row:last-child { border-bottom: 0; }
.row span { padding: 18px 22px; }
.head { background: var(--panel); font: 500 12px/1 var(--mono); text-transform: uppercase; letter-spacing: 0.1em; color: var(--dim); }
.head span { padding: 16px 22px; }
.task { font-weight: 600; }
.before { color: var(--dim); text-decoration: line-through; text-decoration-color: rgba(255, 255, 255, 0.25); }
.after { color: var(--ice); background: rgba(47, 124, 246, 0.06); border-left: 1px solid var(--line); }
@media (max-width: 720px) {
  .head { display: none; }
  .row { grid-template-columns: minmax(0, 1fr); }
  .row span { padding: 6px 18px; }
  .task { padding-top: 16px !important; }
  .after { border-left: 0; background: none; padding-bottom: 16px !important; }
}
</style>
