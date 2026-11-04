<script setup>
import { ref } from 'vue'

defineProps({ data: Object })
const email = ref('')
const interest = ref('both')
const state = ref('idle') // idle | sending | done | error
const message = ref('')

async function submit() {
  state.value = 'sending'
  try {
    const response = await fetch('/api/signup', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ email: email.value, interest: interest.value }),
    })
    const body = await response.json()
    if (!body.ok) throw new Error(body.error || 'Something went wrong.')
    state.value = 'done'
  } catch (error) {
    state.value = 'error'
    message.value = error.message
  }
}
</script>

<template>
  <section id="signup" class="signup">
    <div class="wrap inner reveal">
      <img src="/img/aige-mark.svg" alt="" class="mark" />
      <h2>{{ data.title }}</h2>
      <p class="lead">{{ data.lead }}</p>
      <form v-if="state !== 'done'" class="form" @submit.prevent="submit">
        <label class="sr" for="email">Email</label>
        <input id="email" v-model="email" type="email" required placeholder="you@studio.com" autocomplete="email" />
        <select v-model="interest" aria-label="What interests you">
          <option value="both">AI and by hand</option>
          <option value="ai">Mostly the AI</option>
          <option value="manual">Mostly by hand</option>
        </select>
        <button class="btn primary" :disabled="state === 'sending'">{{ state === 'sending' ? 'Sending…' : 'Notify me' }}</button>
      </form>
      <p v-if="state === 'done'" class="done">Thanks! We'll write when AIGE 1.0 is ready.</p>
      <p v-if="state === 'error'" class="error">{{ message }}</p>
    </div>
  </section>
</template>

<style scoped>
.signup { background: radial-gradient(700px 300px at 50% 0%, rgba(47, 124, 246, 0.25), transparent 70%), var(--bg); text-align: center; }
.inner { display: flex; flex-direction: column; align-items: center; }
.mark { width: 56px; height: 60px; margin-bottom: 24px; }
h2 { font-size: clamp(32px, 5vw, 52px); font-weight: 800; }
.lead { color: var(--muted); font-size: 18px; margin: 16px 0 32px; }
.form { display: flex; gap: 0; width: 100%; max-width: 620px; border: 1px solid var(--line-2); }
.form input, .form select {
  background: var(--panel); color: var(--text); border: 0; border-right: 1px solid var(--line-2);
  padding: 14px 16px; font: 15px Inter, sans-serif; outline: none;
}
.form input { flex: 1; min-width: 0; }
.form input:focus, .form select:focus { background: var(--panel-2); }
.form .btn { border: 0; }
@media (max-width: 600px) {
  .form { flex-direction: column; }
  .form input, .form select { border-right: 0; border-bottom: 1px solid var(--line-2); }
  .form .btn { justify-content: center; }
}
.done { color: var(--cyan); font-weight: 600; }
.error { color: #ff8a8a; margin-top: 14px; }
.sr { position: absolute; width: 1px; height: 1px; overflow: hidden; clip: rect(0 0 0 0); }
</style>
