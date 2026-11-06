<script setup>
import { onMounted, onUnmounted, ref } from 'vue'

const links = [
  { href: '#ai-panel', label: 'AI Panel' },
  { href: '#planner', label: 'Planner' },
  { href: '#builder', label: 'Builder' },
  { href: '#ontology', label: 'GenBuilding' },
  { href: '#manual', label: 'By hand' },
  { href: '#library', label: 'Library' },
  { href: '#roadmap', label: 'Roadmap' },
  { href: '/docs', label: 'Docs' },
]
const scrolled = ref(false)
const open = ref(false)
const onScroll = () => { scrolled.value = window.scrollY > 12 }
onMounted(() => { window.addEventListener('scroll', onScroll, { passive: true }); onScroll() })
onUnmounted(() => window.removeEventListener('scroll', onScroll))
</script>

<template>
  <header class="nav" :class="{ scrolled, open }">
    <div class="wrap nav-inner">
      <a href="#top" class="brand" aria-label="AIGE home">
        <img src="/img/aige-mark.svg" alt="" class="mark" />
        <span class="word">AIGE</span>
      </a>
      <nav class="links" aria-label="Sections">
        <a v-for="link in links" :key="link.href" :href="link.href" @click="open = false">{{ link.label }}</a>
      </nav>
      <a href="/beta" class="btn primary cta">Register for Beta</a>
      <button class="burger" :aria-expanded="open" aria-label="Menu" @click="open = !open">
        <span></span><span></span><span></span>
      </button>
    </div>
  </header>
</template>

<style scoped>
.nav {
  position: fixed; inset: 0 0 auto 0; z-index: 50;
  transition: background 0.2s, border-color 0.2s;
  border-bottom: 1px solid transparent;
}
.nav.scrolled, .nav.open { background: rgba(11, 16, 32, 0.88); backdrop-filter: blur(10px); border-bottom-color: var(--line); }
.nav-inner { height: 68px; display: flex; align-items: center; gap: 28px; }
.brand { display: flex; align-items: center; gap: 10px; color: var(--text); }
.mark { width: 26px; height: 28px; }
.word { font-weight: 800; letter-spacing: 0.06em; font-size: 18px; }
.links { display: flex; gap: 22px; margin-left: auto; }
.links a { color: var(--muted); font-size: 14px; font-weight: 500; }
.links a:hover { color: var(--text); }
.cta { padding: 10px 16px; font-size: 14px; }
.burger { display: none; background: none; border: 1px solid var(--line-2); width: 40px; height: 40px; padding: 10px 9px; cursor: pointer; flex-direction: column; justify-content: space-between; }
.burger span { display: block; height: 2px; background: var(--text); }
@media (max-width: 1040px) {
  .links {
    display: none; position: absolute; top: 68px; left: 0; right: 0; flex-direction: column; gap: 0;
    background: rgba(11, 16, 32, 0.97); border-bottom: 1px solid var(--line); padding: 8px 16px 16px;
  }
  .links a { padding: 12px 0; border-bottom: 1px solid var(--line); font-size: 15px; }
  .open .links { display: flex; }
  .burger { display: flex; margin-left: auto; }
  .cta { display: none; }
}
</style>
