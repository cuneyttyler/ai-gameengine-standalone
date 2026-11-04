<script setup>
defineProps({ data: Object })
</script>

<template>
  <section id="ontology" class="alt">
    <div class="wrap">
      <div class="section-head reveal">
        <span class="eyebrow">GenBuilding Ontology</span>
        <h2>{{ data.title }}</h2>
        <p>{{ data.lead }}</p>
      </div>

      <ol class="pipeline reveal" aria-label="How a request becomes a place">
        <li v-for="(stage, i) in data.pipeline" :key="stage.name">
          <span class="num">{{ String(i + 1).padStart(2, '0') }}</span>
          <b>{{ stage.name }}</b>
          <span>{{ stage.text }}</span>
        </li>
      </ol>

      <div class="grid four points">
        <div v-for="p in data.points" :key="p.title" class="card reveal">
          <h3>{{ p.title }}</h3>
          <p>{{ p.text }}</p>
        </div>
      </div>

      <div class="extend reveal">
        <div class="extend-head">
          <h3>Extendable, in layers</h3>
          <p>Each layer adds to the one below it; every change is checked before it's used.</p>
        </div>
        <ol class="layers">
          <li v-for="(layer, i) in data.extend" :key="layer.name" :style="{ '--i': i }">
            <b>{{ layer.name }}</b><span>{{ layer.text }}</span>
          </li>
        </ol>
      </div>
      <p class="story reveal">{{ data.story }}</p>
    </div>
  </section>
</template>

<style scoped>
.pipeline { list-style: none; padding: 0; margin: 0 0 28px; display: grid; grid-template-columns: repeat(5, minmax(0, 1fr)); border: 1px solid var(--line); }
.pipeline li { position: relative; padding: 22px 22px 22px; background: var(--panel); border-right: 1px solid var(--line); display: flex; flex-direction: column; gap: 8px; }
.pipeline li:last-child { border-right: 0; }
.pipeline li:not(:last-child)::after {
  content: ''; position: absolute; right: -7px; top: 50%; width: 12px; height: 12px; z-index: 1;
  background: var(--bg-2); border-top: 1px solid var(--cyan); border-right: 1px solid var(--cyan); transform: translateY(-50%) rotate(45deg);
}
.pipeline b { font-size: 17px; }
.pipeline span:last-child { color: var(--muted); font-size: 14px; }
@media (max-width: 900px) {
  .pipeline { grid-template-columns: minmax(0, 1fr); }
  .pipeline li { border-right: 0; border-bottom: 1px solid var(--line); }
  .pipeline li:not(:last-child)::after { right: auto; left: 28px; top: auto; bottom: -7px; transform: rotate(135deg); }
}
.points .card h3 { font-size: 17px; }

.extend { margin-top: 56px; display: grid; grid-template-columns: minmax(0, 0.8fr) minmax(0, 1.2fr); gap: 40px; align-items: center; }
@media (max-width: 860px) { .extend { grid-template-columns: minmax(0, 1fr); } }
.extend-head h3 { font-size: 28px; font-weight: 800; margin-bottom: 12px; }
.extend-head p { color: var(--muted); }
.layers { list-style: none; padding: 0; margin: 0; display: flex; flex-direction: column-reverse; gap: 6px; }
.layers li {
  display: flex; gap: 16px; align-items: baseline; padding: 14px 18px;
  margin-left: calc(var(--i) * 18px);
  background: linear-gradient(90deg, rgba(47, 124, 246, calc(0.26 - var(--i) * 0.04)), rgba(34, 211, 238, 0.04));
  border: 1px solid var(--line-2);
}
@media (max-width: 640px) { .layers li { margin-left: 0; flex-direction: column; gap: 2px; } }
.layers b { min-width: 170px; }
.layers span { color: var(--muted); font-size: 14px; }
.story { margin-top: 40px; color: var(--muted); max-width: 760px; border-left: 3px solid var(--cyan); padding-left: 18px; }
</style>
