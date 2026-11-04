<script setup>
defineProps({ hero: Object, pillars: Array })
</script>

<template>
  <section id="top" class="hero">
    <div class="glow" aria-hidden="true"></div>
    <div class="wrap">
      <div class="copy">
        <span class="eyebrow">{{ hero.eyebrow }}</span>
        <h1>
          Make your game in <span class="grad-text">weeks</span>,<br />not years.
        </h1>
        <p class="lead">{{ hero.lead }}</p>
        <div class="actions">
          <a href="#signup" class="btn primary">Get early access</a>
          <a href="#planner" class="btn ghost">See the AI Planner</a>
        </div>
      </div>

      <figure class="shot">
        <div class="chrome">
          <span class="dot"></span><span class="dot"></span><span class="dot"></span>
          <span class="title">FirstGame — village · AIGE</span>
        </div>
        <img :src="hero.image" alt="An isometric river village with timber houses, a market, roads and woods, built in AIGE" />
        <figcaption>{{ hero.caption }}</figcaption>
      </figure>

      <dl class="stats">
        <div v-for="stat in hero.stats" :key="stat.label" class="stat">
          <dt class="grad-text">{{ stat.value }}</dt>
          <dd>{{ stat.label }}</dd>
        </div>
      </dl>

      <div class="pillars grid four">
        <a v-for="(pillar, i) in pillars" :key="pillar.title" :href="`#${pillar.anchor}`" class="card pillar reveal">
          <span class="num">0{{ i + 1 }}</span>
          <h3>{{ pillar.title }}</h3>
          <p>{{ pillar.text }}</p>
          <span class="more">Read more →</span>
        </a>
      </div>
    </div>
  </section>
</template>

<style scoped>
.hero { padding: 148px 0 96px; overflow: hidden; }
.glow {
  position: absolute; inset: -20% -10% auto -10%; height: 900px; pointer-events: none;
  background:
    radial-gradient(600px 360px at 18% 18%, rgba(47, 124, 246, 0.22), transparent 70%),
    radial-gradient(520px 320px at 85% 8%, rgba(34, 211, 238, 0.14), transparent 70%);
}
.copy { position: relative; max-width: 1120px; }
h1 { font-size: clamp(40px, 6.4vw, 80px); font-weight: 800; margin: 22px 0 24px; letter-spacing: -0.035em; }
.lead { color: var(--muted); font-size: clamp(17px, 2vw, 20px); max-width: 680px; }
.actions { display: flex; gap: 14px; margin-top: 34px; flex-wrap: wrap; }

.shot { position: relative; margin: 64px 0 0; border: 1px solid var(--line-2); background: #0a0f1d; box-shadow: 0 40px 120px -30px rgba(47, 124, 246, 0.45); }
.chrome { display: flex; align-items: center; gap: 7px; padding: 10px 14px; border-bottom: 1px solid var(--line); background: var(--panel); }
.dot { width: 9px; height: 9px; background: var(--line-2); }
.title { margin-left: 10px; font: 500 12px/1 var(--mono); color: var(--dim); }
.shot img { width: 100%; height: auto; }
figcaption { padding: 12px 16px; font-size: 13px; color: var(--dim); border-top: 1px solid var(--line); }

.stats { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); margin: 0; border: 1px solid var(--line); border-top: 0; }
.stat { padding: 24px; border-right: 1px solid var(--line); }
.stat:last-child { border-right: 0; }
.stat dt { font-size: 38px; font-weight: 800; letter-spacing: -0.03em; }
.stat dd { margin: 6px 0 0; color: var(--muted); font-size: 14px; }
@media (max-width: 760px) {
  .stats { grid-template-columns: repeat(2, minmax(0, 1fr)); }
  .stat:nth-child(2) { border-right: 0; }
  .stat:nth-child(-n + 2) { border-bottom: 1px solid var(--line); }
}

.pillars { margin-top: 72px; }
.pillar { display: flex; flex-direction: column; gap: 10px; color: inherit; }
.pillar h3 { margin: 4px 0 0; }
.pillar:hover { transform: translateY(-3px); border-color: var(--blue); color: inherit; }
.more { margin-top: auto; padding-top: 8px; font-size: 14px; font-weight: 600; color: var(--ice); }
</style>
