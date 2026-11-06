<script setup>
// Registering for the beta (POST /api/beta/register): the email, and on /beta a name and what they'd like to make.
import { ref } from 'vue'

const props = defineProps({ detailed: Boolean })
const email = ref('')
const name = ref('')
const note = ref('')
const website = ref('') // a hidden field: only bots fill it in
const state = ref('idle') // idle | sending | done | error
const message = ref('')
const already = ref(false)

async function submit() {
  state.value = 'sending'
  try {
    const response = await fetch('/api/beta/register', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ email: email.value, name: name.value, note: note.value, website: website.value }),
    })
    const body = await response.json()
    if (!body.ok) throw new Error(body.error || 'Something went wrong.')
    already.value = !!body.already
    state.value = 'done'
  } catch (error) {
    state.value = 'error'
    message.value = error.message
  }
}
</script>

<template>
  <form v-if="state !== 'done'" class="beta-form" :class="{ detailed: props.detailed }" @submit.prevent="submit">
    <div class="row">
      <label class="sr" for="beta-email">Email</label>
      <input id="beta-email" v-model="email" type="email" required placeholder="you@studio.com" autocomplete="email" />
      <template v-if="props.detailed">
        <label class="sr" for="beta-name">Name</label>
        <input id="beta-name" v-model="name" type="text" maxlength="80" placeholder="Your name (optional)" autocomplete="name" />
      </template>
    </div>
    <div v-if="props.detailed" class="row">
      <label class="sr" for="beta-note">What would you like to make?</label>
      <input id="beta-note" v-model="note" type="text" maxlength="300" placeholder="What would you like to make? (optional)" />
    </div>
    <input v-model="website" class="trap" tabindex="-1" autocomplete="off" aria-hidden="true" />
    <button class="btn primary" :disabled="state === 'sending'">{{ state === 'sending' ? 'Registering…' : 'Register for Beta' }}</button>
  </form>
  <div v-if="state === 'done'" class="done">
    <strong>{{ already ? "You're already registered." : "You're registered!" }}</strong>
    <span>We'll email your personal download link to {{ email }} soon. It works for 7 days.</span>
  </div>
  <p v-if="state === 'error'" class="error">{{ message }}</p>
</template>

<style scoped>
.beta-form { display: flex; width: 100%; max-width: 620px; border: 1px solid var(--line-2); }
.beta-form.detailed { flex-direction: column; border: 0; gap: 10px; }
.row { display: flex; flex: 1; gap: 10px; }
.beta-form:not(.detailed) .row { gap: 0; }
input { flex: 1; min-width: 0; background: var(--panel); color: var(--text); border: 0; padding: 14px 16px; font: 15px Inter, sans-serif; outline: none; }
.detailed input { border: 1px solid var(--line-2); }
input:focus { background: var(--panel-2); }
.detailed input:focus { border-color: var(--blue); }
.btn { border: 0; justify-content: center; }
.trap { position: absolute; left: -9999px; width: 1px; height: 1px; opacity: 0; }
.done { display: flex; flex-direction: column; gap: 6px; color: var(--muted); }
.done strong { color: var(--cyan); font-size: 18px; }
.error { color: #ff8a8a; margin-top: 14px; }
.sr { position: absolute; width: 1px; height: 1px; overflow: hidden; clip: rect(0 0 0 0); }
@media (max-width: 600px) {
  .beta-form:not(.detailed) { flex-direction: column; }
  .row { flex-direction: column; }
}
</style>
