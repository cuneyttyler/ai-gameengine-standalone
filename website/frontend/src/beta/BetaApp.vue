<script setup>
// /beta: what the beta is, how getting it works, and registering (website/backend/beta.py).
import { onMounted, ref } from 'vue'
import BetaForm from './BetaForm.vue'

const data = ref(null)
const failed = ref(false)

onMounted(async () => {
  document.title = 'Beta · AIGE'
  try {
    const response = await fetch('/api/beta')
    if (!response.ok) throw new Error(String(response.status))
    data.value = await response.json()
  } catch {
    failed.value = true
  }
})
</script>

<template>
  <header class="top">
    <div class="wrap top-inner">
      <a href="/" class="brand" aria-label="AIGE home"><img src="/img/aige-mark.svg" alt="" class="mark" /><span class="word">AIGE</span></a>
      <nav class="links">
        <a href="/">Product</a>
        <a href="/docs">Docs</a>
        <a href="#register" class="btn primary small">Register for Beta</a>
      </nav>
    </div>
  </header>

  <main v-if="data">
    <section class="hero">
      <div class="wrap hero-grid">
        <div>
          <span class="eyebrow">{{ data.eyebrow }}</span>
          <h1>{{ data.title }}</h1>
          <p class="lead">{{ data.lead }}</p>
          <div class="chips">
            <span class="chip">{{ data.platform }}</span>
            <span class="chip">Personal link, valid {{ data.linkDays }} days</span>
          </div>
          <a href="#register" class="btn primary">Register for Beta</a>
        </div>
        <img src="/img/village.webp" alt="A river village made in AIGE" class="shot" />
      </div>
    </section>

    <section class="alt">
      <div class="wrap">
        <div class="section-head">
          <span class="eyebrow">What's in it</span>
          <h2>The whole editor, AI included</h2>
        </div>
        <div class="grid four">
          <div v-for="item in data.included" :key="item.title" class="card">
            <h3>{{ item.title }}</h3>
            <p>{{ item.text }}</p>
          </div>
        </div>
      </div>
    </section>

    <section>
      <div class="wrap how">
        <div>
          <div class="section-head">
            <span class="eyebrow">How it works</span>
            <h2>Three steps to your copy</h2>
          </div>
          <ol class="steps">
            <li v-for="(step, i) in data.steps" :key="step.title">
              <span class="num">{{ i + 1 }}</span>
              <div><h3>{{ step.title }}</h3><p>{{ step.text }}</p></div>
            </li>
          </ol>
        </div>
        <div class="reqs card">
          <h3>What you need</h3>
          <ul><li v-for="r in data.requirements" :key="r"><span class="cube"></span>{{ r }}</li></ul>
          <p class="note">{{ data.note }}</p>
        </div>
      </div>
    </section>

    <section id="register" class="register">
      <div class="wrap register-inner">
        <img src="/img/aige-mark.svg" alt="" class="big-mark" />
        <h2>Register for the beta</h2>
        <p class="lead">We'll email you a personal download link. It works for {{ data.linkDays }} days.</p>
        <BetaForm detailed />
      </div>
    </section>
  </main>
  <p v-else class="loading">{{ failed ? "The beta page couldn't be loaded. Is the server running?" : 'Loading…' }}</p>

  <footer class="footer">
    <div class="wrap footer-inner">
      <img src="/img/aige-logo-dark.svg" alt="AIGE" class="footer-logo" />
      <p>AIGE · AI Game Engine. A game engine and editor for 2D games, with AI built in.</p>
      <p class="dim">© 2026 AIGE</p>
    </div>
  </footer>
</template>

<style scoped>
.top { position: sticky; top: 0; z-index: 40; background: rgba(11, 16, 32, 0.9); backdrop-filter: blur(10px); border-bottom: 1px solid var(--line); }
.top-inner { height: 64px; display: flex; align-items: center; gap: 24px; }
.brand { display: flex; align-items: center; gap: 10px; color: var(--text); }
.mark { width: 24px; height: 26px; }
.word { font-weight: 800; letter-spacing: 0.06em; font-size: 17px; }
.links { margin-left: auto; display: flex; align-items: center; gap: 22px; }
.links a { color: var(--muted); font-size: 14px; font-weight: 500; }
.links a.btn { color: #04101f; }
.btn.small { padding: 9px 14px; font-size: 14px; }

.hero { padding: 88px 0 96px; background: radial-gradient(800px 400px at 80% 10%, rgba(34, 211, 238, 0.12), transparent 70%); }
.hero-grid { display: grid; grid-template-columns: 1.05fr 1fr; gap: 56px; align-items: center; }
.hero h1 { font-size: clamp(36px, 5vw, 60px); font-weight: 800; margin: 16px 0 18px; }
.hero .lead { color: var(--muted); font-size: 18px; margin-bottom: 22px; }
.chips { display: flex; flex-wrap: wrap; gap: 8px; margin-bottom: 28px; }
.chip { font: 500 12px/1 var(--mono); padding: 8px 10px; border: 1px solid var(--line-2); color: var(--ice); background: rgba(255, 255, 255, 0.02); }
.shot { width: 100%; border: 1px solid var(--line-2); box-shadow: 0 30px 80px rgba(0, 0, 0, 0.45); }

.grid.four { display: grid; grid-template-columns: repeat(4, 1fr); gap: 16px; }
.card h3 { font-size: 17px; margin-bottom: 10px; }
.card p { color: var(--muted); font-size: 14.5px; }

.how { display: grid; grid-template-columns: 1.1fr 1fr; gap: 48px; align-items: start; }
.steps { list-style: none; margin: 0; padding: 0; display: grid; gap: 18px; }
.steps li { display: flex; gap: 18px; align-items: flex-start; }
.num { flex: none; width: 40px; height: 40px; display: grid; place-items: center; background: var(--grad); color: #04101f; font-weight: 800; }
.steps h3 { font-size: 17px; margin-bottom: 4px; }
.steps p { color: var(--muted); font-size: 15px; }
.reqs h3 { font-size: 17px; padding-bottom: 12px; border-bottom: 1px solid var(--line); margin-bottom: 14px; }
.reqs ul { list-style: none; margin: 0 0 18px; padding: 0; display: grid; gap: 10px; }
.reqs li { display: flex; gap: 10px; color: var(--muted); font-size: 14.5px; line-height: 1.5; }
.reqs .cube { flex: none; width: 8px; height: 8px; margin-top: 7px; }
.note { font-size: 13.5px; color: var(--dim); border-top: 1px solid var(--line); padding-top: 14px; }

.register { background: radial-gradient(700px 300px at 50% 0%, rgba(47, 124, 246, 0.25), transparent 70%), var(--bg-2); border-top: 1px solid var(--line); text-align: center; }
.register-inner { display: flex; flex-direction: column; align-items: center; }
.big-mark { width: 56px; height: 60px; margin-bottom: 22px; }
.register h2 { font-size: clamp(30px, 4.5vw, 46px); font-weight: 800; }
.register .lead { color: var(--muted); font-size: 18px; margin: 14px 0 30px; }

.loading { padding: 200px 24px; color: var(--muted); text-align: center; }
.footer { border-top: 1px solid var(--line); padding: 40px 0; background: #080c18; }
.footer-inner { display: flex; align-items: center; gap: 24px; flex-wrap: wrap; color: var(--muted); font-size: 14px; }
.footer-logo { height: 28px; width: auto; }
.footer .dim { margin-left: auto; color: var(--dim); }

@media (max-width: 960px) {
  .hero-grid, .how { grid-template-columns: 1fr; }
  .grid.four { grid-template-columns: 1fr 1fr; }
}
@media (max-width: 600px) {
  .grid.four { grid-template-columns: 1fr; }
  .links a:not(.btn) { display: none; }
}
</style>
