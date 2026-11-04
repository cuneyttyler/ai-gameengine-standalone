<script setup>
defineProps({ data: Object })
</script>

<template>
  <section id="library" class="alt">
    <div class="wrap layout">
      <div>
        <div class="section-head reveal">
          <span class="eyebrow">AIGE Library</span>
          <h2>{{ data.title }}</h2>
          <p>{{ data.lead }}</p>
        </div>
        <div class="grid two">
          <div v-for="f in data.features" :key="f.title" class="feature reveal">
            <h3>{{ f.title }}</h3>
            <p>{{ f.text }}</p>
          </div>
        </div>
      </div>

      <aside class="order card reveal">
        <h3>Where the Builder looks for art</h3>
        <ol>
          <li v-for="(o, i) in data.order" :key="o"><span class="num">{{ i + 1 }}</span>{{ o }}</li>
        </ol>
        <p class="note">{{ data.note }}</p>
      </aside>
    </div>
  </section>
</template>

<style scoped>
.layout { display: grid; grid-template-columns: minmax(0, 1.4fr) minmax(0, 0.8fr); gap: 56px; align-items: start; }
@media (max-width: 960px) { .layout { grid-template-columns: minmax(0, 1fr); } }
.feature { border-top: 2px solid var(--blue); padding-top: 16px; }
.feature h3 { font-size: 17px; margin-bottom: 8px; }
.feature p { color: var(--muted); font-size: 15px; }
.order h3 { font-size: 17px; margin-bottom: 18px; }
.order ol { list-style: none; margin: 0; padding: 0; display: grid; gap: 0; }
.order li { display: flex; gap: 14px; align-items: center; padding: 12px 0; border-bottom: 1px solid var(--line); color: var(--pale); font-size: 15px; }
.order .num { width: 26px; height: 26px; display: grid; place-items: center; border: 1px solid var(--line-2); }
.note { margin-top: 18px; font-size: 13px; color: var(--dim); }
</style>
