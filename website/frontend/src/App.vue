<script setup>
import { onMounted, ref, nextTick } from 'vue'
import SiteNav from './components/SiteNav.vue'
import HeroSection from './components/HeroSection.vue'
import AiPanelSection from './components/AiPanelSection.vue'
import PlannerSection from './components/PlannerSection.vue'
import BuilderSection from './components/BuilderSection.vue'
import MethodsSection from './components/MethodsSection.vue'
import OntologySection from './components/OntologySection.vue'
import ManualSection from './components/ManualSection.vue'
import LibrarySection from './components/LibrarySection.vue'
import SpeedSection from './components/SpeedSection.vue'
import RoadmapSection from './components/RoadmapSection.vue'
import SignupSection from './components/SignupSection.vue'

const content = ref(null)
const failed = ref(false)

// Sections fade in as they scroll into view.
function watchReveals() {
  const items = document.querySelectorAll('.reveal')
  if (!('IntersectionObserver' in window)) {
    items.forEach((el) => el.classList.add('shown'))
    return
  }
  const seen = new IntersectionObserver((entries) => {
    for (const entry of entries) {
      if (entry.isIntersecting) {
        entry.target.classList.add('shown')
        seen.unobserve(entry.target)
      }
    }
  }, { rootMargin: '0px 0px -8% 0px' })
  items.forEach((el) => seen.observe(el))
}

onMounted(async () => {
  try {
    const response = await fetch('/api/content')
    if (!response.ok) throw new Error(String(response.status))
    content.value = await response.json()
    await nextTick()
    watchReveals()
  } catch {
    failed.value = true
  }
})
</script>

<template>
  <SiteNav />
  <main v-if="content">
    <HeroSection :hero="content.hero" :pillars="content.pillars" />
    <AiPanelSection :data="content.aiPanel" />
    <PlannerSection :data="content.planner" />
    <BuilderSection :data="content.builder" />
    <MethodsSection :data="content.methods" />
    <OntologySection :data="content.ontology" />
    <ManualSection :data="content.manual" />
    <LibrarySection :data="content.library" />
    <SpeedSection :data="content.speed" />
    <RoadmapSection :data="content.roadmap" />
    <SignupSection :data="content.signup" />
  </main>
  <div v-else class="loading wrap">
    <p v-if="failed">The site's content couldn't be loaded. Is the server (website/backend/app.py) running?</p>
    <p v-else>Loading…</p>
  </div>
  <footer class="footer">
    <div class="wrap footer-inner">
      <img src="/img/aige-logo-dark.svg" alt="AIGE" class="footer-logo" />
      <p>AIGE · AI Game Engine. A game engine and editor for 2D games, with AI built in.</p>
      <p class="dim">© 2026 AIGE</p>
    </div>
  </footer>
</template>

<style scoped>
.loading { padding: 200px 24px; color: var(--muted); text-align: center; }
.footer { border-top: 1px solid var(--line); padding: 40px 0; background: #080c18; }
.footer-inner { display: flex; align-items: center; gap: 24px; flex-wrap: wrap; color: var(--muted); font-size: 14px; }
.footer-logo { height: 28px; width: auto; }
.footer .dim { margin-left: auto; color: var(--dim); }
@media (max-width: 640px) { .footer .dim { margin-left: 0; } }
</style>
