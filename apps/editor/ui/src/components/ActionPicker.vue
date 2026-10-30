<script setup lang="ts">
// A part's input action (Animate a part, the building editor, the Inspector): the project's actions to pick from, and
// New action… for one of its own: its name and the key that presses it, added to Project Settings ▸ Input.
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue'
import { actionName, addInputAction, engineKey, inputActions, isInputAction, requestInputActions } from '../inputActions'
import { trackOverlay, untrackOverlay } from '../overlay'

const props = defineProps<{ modelValue: string; disabled?: boolean }>()
const emit = defineEmits<{ (e: 'update:modelValue', value: string): void }>()

onMounted(() => {
  if (!inputActions.value.length) requestInputActions()
})

const adding = ref(false)
const name = ref('')
const key = ref('')
const description = ref('')
const capturing = ref(false)
const problem = ref('')
const popup = ref<HTMLElement | null>(null)
// The actions the key pressed already triggers (it may: both happen when it's pressed).
const sharing = computed(() => (key.value ? inputActions.value.filter((a) => a.keys.split(', ').includes(key.value)).map((a) => a.name) : []))

async function openNew(): Promise<void> {
  adding.value = true
  name.value = props.modelValue && !isInputAction(props.modelValue) ? props.modelValue : ''
  key.value = ''
  description.value = ''
  problem.value = ''
  capturing.value = false
  await nextTick()
  if (popup.value) trackOverlay(popup.value), popup.value.focus()
}
function closeNew(): void {
  if (popup.value) untrackOverlay(popup.value)
  adding.value = false
}
function onKey(event: KeyboardEvent): void {
  if (!capturing.value) return
  event.preventDefault()
  event.stopPropagation()
  capturing.value = false
  if (event.code !== 'Escape') key.value = engineKey(event) ?? key.value
}
// Read on the window while capturing (wherever the focus is), before anything else.
watch(capturing, (now) => {
  if (now) window.addEventListener('keydown', onKey, true)
  else window.removeEventListener('keydown', onKey, true)
})
onUnmounted(() => window.removeEventListener('keydown', onKey, true))
function save(): void {
  const n = actionName(name.value)
  if (!n) return void (problem.value = 'Give it a name')
  if (isInputAction(n)) return void (problem.value = `There's already an action named ${n}`)
  if (!key.value) return void (problem.value = 'Press the key that triggers it')
  if (!addInputAction(n, key.value, description.value.trim())) return void (problem.value = "The project's actions aren't loaded yet; try again")
  emit('update:modelValue', n)
  closeNew()
}
</script>

<template>
  <span class="action-picker">
    <select :value="modelValue" :disabled="disabled" @change="emit('update:modelValue', ($event.target as HTMLSelectElement).value)">
      <option value="" disabled>an action…</option>
      <option v-if="modelValue && !isInputAction(modelValue)" :value="modelValue">{{ modelValue }} (not an input action)</option>
      <option v-for="a in inputActions" :key="a.name" :value="a.name" :title="a.description">{{ a.name }}{{ a.keys ? ` (${a.keys})` : '' }}</option>
    </select>
    <button type="button" class="small" :disabled="disabled" title="An action of the project's own, and the key that presses it" @click="openNew">New action…</button>
    <span v-if="modelValue && inputActions.length && !isInputAction(modelValue)" class="warn">
      {{ modelValue }} isn't an input action yet: no key presses it. Add it with New action….</span>
    <div v-if="adding" class="backdrop" @mousedown.self="closeNew">
      <div ref="popup" class="popup ed-form" tabindex="-1" @keydown.esc="!capturing && closeNew()">
        <h4>New input action</h4>
        <label>Name <input v-model="name" spellcheck="false" placeholder="cook" /></label>
        <label>Key
          <button type="button" :class="{ active: capturing }" @click="capturing = true">
            {{ capturing ? 'Press a key…' : key || 'Click, then press a key' }}</button>
        </label>
        <p v-if="sharing.length" class="muted small">{{ key }} also triggers {{ sharing.join(', ') }}: pressing it does both (near a part,
          the nearest one usable reacts).</p>
        <label>Description <input v-model="description" placeholder="Cook at a stove or a fire" /></label>
        <p class="muted small">Saved in Project Settings ▸ Input, where its keys (and gamepad buttons) can be changed.</p>
        <p v-if="problem" class="warn">{{ problem }}</p>
        <div class="buttons">
          <button type="button" @click="closeNew">Cancel</button>
          <button type="button" class="primary" @click="save">Add</button>
        </div>
      </div>
    </div>
  </span>
</template>

<style scoped>
.action-picker {
  display: inline-flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 4px;
}
.warn {
  color: #e6a23c;
  font-size: 0.82em;
  width: 100%;
}
.backdrop {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.45);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 3000;
}
.popup {
  background: #22222e;
  border: 1px solid #3a3a4c;
  padding: 14px;
  width: 340px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.popup label {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 8px;
}
.popup button.active {
  border-color: #3b82f6;
}
.buttons {
  display: flex;
  justify-content: flex-end;
  gap: 6px;
}
.muted {
  opacity: 0.6;
}
.small {
  font-size: 0.85em;
}
</style>
