<script setup>
import BetaForm from '../beta/BetaForm.vue'

defineProps({ data: Object })
</script>

<template>
  <section id="signup" class="signup">
    <div class="wrap inner reveal">
      <img src="/img/aige-mark.svg" alt="" class="mark" />
      <h2>{{ data.title }}</h2>
      <p class="lead">{{ data.lead }}</p>
      <BetaForm />
      <a href="/beta" class="more">What's in the beta →</a>
    </div>
  </section>
</template>

<style scoped>
.signup { background: radial-gradient(700px 300px at 50% 0%, rgba(47, 124, 246, 0.25), transparent 70%), var(--bg); text-align: center; }
.inner { display: flex; flex-direction: column; align-items: center; }
.mark { width: 56px; height: 60px; margin-bottom: 24px; }
h2 { font-size: clamp(32px, 5vw, 52px); font-weight: 800; }
.lead { color: var(--muted); font-size: 18px; margin: 16px 0 32px; }
.more { margin-top: 18px; font-size: 14px; }
</style>
