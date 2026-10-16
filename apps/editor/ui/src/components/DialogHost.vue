<script setup lang="ts">
// Shows the first queued dialog (dialogs.ts) over everything, the viewport
// included (the backdrop is cut out of it).
import { computed, nextTick, ref, watch } from 'vue'
import { dialogState, finishDialog } from '../dialogs'
import { trackOverlay, untrackOverlay } from '../overlay'

const current = computed(() => dialogState.queue[0] ?? null)
const backdrop = ref<HTMLElement | null>(null)
const input = ref<HTMLInputElement | null>(null)
const okButton = ref<HTMLButtonElement | null>(null)
const value = ref('')

watch(
  current,
  async (now, before) => {
    if (now && !before) {
      await nextTick()
      if (backdrop.value) trackOverlay(backdrop.value)
    }
    if (!now && before && backdrop.value) {
      untrackOverlay(backdrop.value)
    }
    if (now) {
      value.value = now.value
      await nextTick()
      if (now.kind === 'prompt') {
        input.value?.focus()
        input.value?.select()
      } else {
        okButton.value?.focus()
      }
    }
  },
  { flush: 'post' },
)

function ok(): void {
  finishDialog(current.value?.kind === 'prompt' ? value.value : true)
}

function cancel(): void {
  finishDialog(current.value?.kind === 'prompt' ? null : false)
}

function onKeydown(event: KeyboardEvent): void {
  event.stopPropagation() // the editor's shortcuts wait while a dialog is open
  if (event.key === 'Escape') {
    event.preventDefault()
    cancel()
  } else if (event.key === 'Enter' && current.value?.kind !== 'info' && !(event.target instanceof HTMLButtonElement)) {
    event.preventDefault()
    ok()
  }
}
</script>

<template>
  <div v-if="current" ref="backdrop" class="backdrop" @keydown="onKeydown" @mousedown.self="cancel">
    <div class="dialog" role="dialog" aria-modal="true" :aria-label="current.title || current.message">
      <h3 v-if="current.title">{{ current.title }}</h3>
      <p class="message">{{ current.message }}</p>
      <input
        v-if="current.kind === 'prompt'"
        ref="input"
        v-model="value"
        :placeholder="current.placeholder"
        spellcheck="false"
      />
      <div class="buttons">
        <button v-if="current.kind !== 'info'" @click="cancel">{{ current.cancelLabel }}</button>
        <button ref="okButton" class="primary" :class="{ danger: current.danger }" @click="ok">
          {{ current.confirmLabel }}
        </button>
      </div>
    </div>
  </div>
</template>

<style scoped>
.backdrop {
  position: fixed;
  inset: 0;
  z-index: 1000;
  background: rgba(5, 5, 10, 0.55);
  display: flex;
  align-items: center;
  justify-content: center;
}
.dialog {
  min-width: 340px;
  max-width: 520px;
  padding: 16px 18px;
  background: #181825;
  border: 1px solid #333;
  border-top: 2px solid #3b82f6;
  box-shadow: 0 10px 40px rgba(0, 0, 0, 0.5);
}
h3 {
  margin: 0 0 8px;
  font-size: 1em;
}
.message {
  margin: 0 0 12px;
  white-space: pre-wrap;
  line-height: 1.45;
  opacity: 0.9;
}
input {
  width: 100%;
  box-sizing: border-box;
  margin-bottom: 12px;
  padding: 5px 8px;
  border: 1px solid #333;
  border-radius: 0;
  background: #11111b;
  color: #eee;
  font: inherit;
}
.buttons {
  display: flex;
  justify-content: flex-end;
  gap: 6px;
}
button {
  padding: 4px 14px;
  border: 1px solid #333;
  border-radius: 0;
  background: #11111b;
  color: #eee;
  font: inherit;
  cursor: pointer;
}
button:hover {
  background: #262637;
}
button.primary {
  background: #3b82f6;
  border-color: #3b82f6;
}
button.primary.danger {
  background: #b91c1c;
  border-color: #b91c1c;
}
</style>
