<script setup lang="ts">
// New Project (development_plan.md 4.2, 5.4): every choice in one window — its name and where it goes, the grid, and
// the project's AI (each "Use app default" unless the project should have its own: Project Settings ▸ AI). Shown by
// the welcome window and by File ▸ New Project; the host creates it and opens it.
import { computed, ref, watch } from 'vue'
import { onHostMessage, sendToHost } from '../ipc'

const props = defineProps<{
  open: boolean
  flow: 'welcome' | 'editor' // which host makes it: the welcome window's (welcome:new) or the editor's (project:new)
  defaults?: { provider?: string; model?: string; art?: string; background?: string } // the app's choices, for the labels
}>()
const emit = defineEmits<{ close: [] }>()

const name = ref('my-game')
const location = ref('')
const grid = ref<'isometric' | 'topdown'>('isometric')
const template = ref('') // editor only: a game template (arpg, rpg, action-adventure, life-sim)
const provider = ref('')
const model = ref('')
const art = ref('')
const background = ref('')
const paintModel = ref('')
// How the AI builds places (Project Settings ▸ Places ▸ method); each build and plan can choose the other.
const buildMethod = ref<'Assembled' | 'Paint'>('Paint')
const METHOD_HELP: Record<string, string> = {
  Assembled: 'Each building, tree and prop gets its own art, put together on the grid over a painted background; every piece can be moved and edited. Steps: Confirm, Layout, Plan, Assets, Build.',
  Paint: "Each place is painted whole as one picture (as Baldur's Gate's areas are), then you mark where one can walk and what a character goes behind; only doors, chests, items and characters get art of their own. Steps: Confirm, Layout, Plan, Paint, Mark, Assets, Build.",
}
const error = ref('')

onHostMessage((message) => {
  if (message.type === 'project:browsed' && typeof message.path === 'string' && message.path) location.value = message.path
  // The folder the last project was created in, unless one is picked already.
  if (message.type === 'project:location' && typeof message.path === 'string' && message.path && !location.value) location.value = message.path
})

watch(
  () => props.open,
  (open) => {
    if (!open) return
    error.value = ''
    sendToHost({ type: 'project:location' })
  },
  { immediate: true },
)

const where = computed(() => (location.value ? `${location.value.replace(/[\\/]+$/, '')}\\${name.value.trim()}` : ''))
function appDefault(key: 'provider' | 'model' | 'art' | 'background', fallback: string): string {
  return `Use app default (${props.defaults?.[key] || fallback})`
}

function browse(): void {
  sendToHost({ type: 'project:browse', title: `Where to create "${name.value.trim() || 'the project'}"` })
}

function create(): void {
  const n = name.value.trim()
  if (!n) {
    error.value = 'Give the project a name.'
    return
  }
  if (/[\\/:*?"<>|]/.test(n)) {
    error.value = 'A name can’t have \\ / : * ? " < > |'
    return
  }
  if (!location.value) {
    error.value = 'Pick where to create it (Browse…).'
    return
  }
  sendToHost({
    type: props.flow === 'welcome' ? 'welcome:new' : 'project:new',
    name: n,
    location: location.value,
    topDown: grid.value === 'topdown',
    template: props.flow === 'editor' ? template.value : '',
    ai: { provider: provider.value, model: model.value.trim(), art: art.value, background: background.value, paint: paintModel.value },
    buildMethod: buildMethod.value,
  })
  emit('close')
}
</script>

<template>
  <div v-if="open" class="backdrop ed-form" tabindex="-1" @mousedown.self="emit('close')" @keydown.esc.stop="emit('close')">
    <div class="sheet">
      <header>
        <h3>New Project</h3>
        <button title="Close (Esc)" @click="emit('close')">×</button>
      </header>
      <div class="rows">
        <label>Name</label>
        <input v-model="name" type="text" spellcheck="false" placeholder="my-game" @keydown.enter="create" />

        <label>Location</label>
        <div class="pick">
          <input v-model="location" type="text" spellcheck="false" placeholder="the folder to create it in" />
          <button @click="browse">Browse…</button>
        </div>
        <span />
        <span class="muted small">{{ where ? `Created as ${where}` : '' }}</span>

        <label>Grid</label>
        <div class="choice">
          <label><input v-model="grid" type="radio" value="isometric" /> Isometric (diamond cells)</label>
          <label><input v-model="grid" type="radio" value="topdown" /> Top-down (square cells)</label>
        </div>

        <template v-if="flow === 'editor'">
          <label>Template</label>
          <select v-model="template">
            <option value="">Blank project</option>
            <option value="arpg">Action RPG (isometric)</option>
            <option value="rpg">RPG (top-down)</option>
            <option value="action-adventure">Action-adventure</option>
            <option value="life-sim">Life sim</option>
          </select>
        </template>

        <label>Build method <span class="help" :title="'How the AI builds places (each build and plan can choose the other). ' + METHOD_HELP[buildMethod]">?</span></label>
        <div class="choice">
          <label :title="METHOD_HELP.Paint"><input v-model="buildMethod" type="radio" value="Paint" /> Paint (painted whole, marked by you)</label>
          <label :title="METHOD_HELP.Assembled"><input v-model="buildMethod" type="radio" value="Assembled" /> Assembled (each piece's art)</label>
        </div>

        <h4>AI <span class="muted small">this project's own; change later in Project Settings ▸ AI</span></h4>
        <label>LLM provider</label>
        <select v-model="provider">
          <option value="">{{ appDefault('provider', 'Mistral') }}</option>
          <option value="mistral">Mistral</option>
          <option value="claude">Claude</option>
        </select>

        <label>LLM model</label>
        <input v-model="model" type="text" spellcheck="false" :placeholder="provider ? 'the provider’s default' : appDefault('model', 'the provider’s default')" />

        <label>Prefab art</label>
        <select v-model="art">
          <option value="">{{ appDefault('art', 'follow the provider') }}</option>
          <option value="mistral">Mistral</option>
          <option value="pixellab">PixelLab</option>
          <option value="bfl:flux-2-klein-4b">BFL FLUX.2 klein</option>
          <option value="bfl:flux-3-image">BFL FLUX.3 Image</option>
          <option value="bfl:flux-2-pro">BFL FLUX.2 pro</option>
        </select>

        <label>Background</label>
        <select v-model="background">
          <option value="">{{ appDefault('background', 'BFL FLUX.3 Image') }}</option>
          <option value="bfl:flux-3-image">BFL FLUX.3 Image</option>
        </select>

        <label>Paint model <span class="help" title="Who paints a place whole for the Paint method. FLUX-3-Image keeps the plan's layout best; FLUX-2-Klein-4b is fast and follows the plan as a rough draft; FLUX-2-Pro and FLUX-2-Max have rich looks but may drift from the layout; FLUX-3-Image-to-Pro and -to-Klein: FLUX-3-Image paints the plan, then FLUX.2 pro or klein repaints its look.">?</span></label>
        <select v-model="paintModel">
          <option value="">FLUX-3-Image (default)</option>
          <option value="flux-2-klein-4b">FLUX-2-Klein-4b</option>
          <option value="flux-2-pro">FLUX-2-Pro</option>
          <option value="flux-2-max">FLUX-2-Max</option>
          <option value="flux-3-image">FLUX-3-Image</option>
          <option value="flux-3-to-pro">FLUX-3-Image-to-Pro</option>
          <option value="flux-3-to-klein">FLUX-3-Image-to-Klein</option>
        </select>
      </div>
      <p v-if="error" class="error">{{ error }}</p>
      <footer>
        <button @click="emit('close')">Cancel</button>
        <button class="primary" @click="create">Create</button>
      </footer>
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
  outline: none;
}
.sheet {
  width: min(560px, 92vw);
  max-height: 88vh;
  overflow: auto;
  display: flex;
  flex-direction: column;
  gap: 10px;
  background: #181825;
  border: 1px solid #333;
  border-top: 2px solid #3b82f6;
  padding: 14px 18px;
}
header,
footer {
  display: flex;
  align-items: center;
  justify-content: space-between;
}
footer {
  justify-content: flex-end;
  gap: 8px;
}
h3 {
  margin: 0;
}
h4 {
  grid-column: 1 / -1;
  margin: 8px 0 0;
  border-top: 1px solid #2a2a3a;
  padding-top: 8px;
}
.rows {
  display: grid;
  grid-template-columns: 120px 1fr;
  gap: 6px 10px;
  align-items: center;
}
.pick {
  display: flex;
  gap: 6px;
}
.pick input {
  flex: 1;
}
.help {
  display: inline-block;
  width: 14px;
  height: 14px;
  line-height: 14px;
  text-align: center;
  font-size: 10px;
  border: 1px solid currentColor;
  opacity: 0.7;
  cursor: help;
  margin-left: 4px;
}
.choice {
  display: flex;
  gap: 16px;
}
.choice label {
  display: flex;
  gap: 4px;
  align-items: center;
}
input[type='text'],
select {
  width: 100%;
  box-sizing: border-box;
}
.muted {
  color: #8a8aa0;
}
.small {
  font-size: 11px;
  font-weight: normal;
}
.error {
  color: #f87171;
  margin: 0;
}
</style>
