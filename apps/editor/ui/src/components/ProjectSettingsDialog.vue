<script setup lang="ts">
// File ▸ Project Settings (development_plan.md 5.5): the project-wide settings (renderer, camera, combat, game, time…),
// generated from the registry, laid out as Preferences is: its sections on the left; the chosen one on the right, each
// setting a row with its name, its control and what it does.
import { computed, nextTick, reactive, ref, watch } from 'vue'
import { askAiAbout, editorState } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'
import { trackOverlay, untrackOverlay } from '../overlay'
import { projectState } from '../projects'
import FieldEditor, { type FieldData } from './FieldEditor.vue'
import InputSettings from './InputSettings.vue'

interface SettingsSection {
  name: string
  description: string
  fields: FieldData[]
}

const backdrop = ref<HTMLElement | null>(null)
const sections = ref<SettingsSection[]>([])
const errors = reactive<Record<string, string>>({})
const page = ref('')
const INPUT = 'Input' // the input map: its own editor, not reflected settings

onHostMessage((message) => {
  if (message.type === 'settings:data') {
    sections.value = message.sections as SettingsSection[]
    if (!page.value || (page.value !== INPUT && !sections.value.some((s) => s.name === page.value))) page.value = sections.value[0]?.name ?? INPUT
  } else if (message.type === 'inspector:fieldError') {
    const key = `${message.component}.${message.field}`
    if (message.error) errors[key] = message.error as string
    else delete errors[key]
  }
})
const current = computed(() => sections.value.find((s) => s.name === page.value))

watch(
  () => projectState.showProjectSettings,
  async (open) => {
    if (open) sendToHost({ type: 'settings:request' })
    await nextTick()
    if (open && backdrop.value) {
      trackOverlay(backdrop.value)
      backdrop.value.focus()
    }
  },
)

function close(): void {
  if (backdrop.value) untrackOverlay(backdrop.value)
  projectState.showProjectSettings = false
}

function set(section: string, field: FieldData, value: unknown): void {
  sendToHost({ type: 'settings:set', section, field: field.name, value })
}

function askAi(section: string, field: string): void {
  askAiAbout(`Settings.${section}.${field}`)
  close()
}
// "startScene" → "Start scene".
function title(name: string): string {
  const words = name.replace(/([a-z0-9])([A-Z])/g, '$1 $2').toLowerCase()
  return words.charAt(0).toUpperCase() + words.slice(1)
}
</script>

<template>
  <div v-if="projectState.showProjectSettings" ref="backdrop" class="backdrop ed-form" tabindex="-1" @mousedown.self="close" @keydown.esc.stop="close">
    <div class="sheet">
      <header>
        <h3>Project Settings</h3>
        <span class="muted">Saved with the project (settings.aige) when you save; changes apply right away.</span>
        <button class="close" title="Close (Esc)" @click="close">×</button>
      </header>
      <div class="panes">
        <nav>
          <button v-for="s in sections" :key="s.name" :class="{ active: page === s.name }" :title="s.description" @click="page = s.name">
            <span class="label">{{ s.name }}</span>
          </button>
          <button :class="{ active: page === INPUT }" title="What the player presses for each action" @click="page = INPUT">
            <span class="label">Input</span>
          </button>
        </nav>
        <main>
          <template v-if="page === INPUT">
            <h4>Input</h4>
            <p class="lead">Actions and their keys, mouse and gamepad buttons (input.json).</p>
            <section class="group">
              <h5>Actions</h5>
              <InputSettings />
            </section>
          </template>
          <template v-else-if="current">
            <h4>{{ current.name }}</h4>
            <p class="lead">{{ current.description }}</p>
            <section class="group">
              <h5>Settings</h5>
              <template v-for="field in current.fields" :key="field.name">
                <div class="field">
                  <label>
                    {{ title(field.name) }}
                    <button v-if="editorState.ai.state === 'ready'" class="ask-ai" title="Ask AI about this setting" @click="askAi(current.name, field.name)">✦</button>
                  </label>
                  <div class="control">
                    <FieldEditor :field="field" :disabled="!!field.locked" :title="field.locked || undefined" @change="set(current.name, field, $event)" />
                    <p v-if="field.description" class="desc">{{ field.description }}</p>
                    <p v-if="errors[`${current.name}.${field.name}`]" class="field-error">{{ errors[`${current.name}.${field.name}`] }}</p>
                  </div>
                </div>
              </template>
            </section>
          </template>
        </main>
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
  outline: none;
}
.sheet {
  width: min(1000px, 94vw);
  height: min(680px, 90vh);
  display: flex;
  flex-direction: column;
  background: #181825;
  border: 1px solid #333;
  border-top: 2px solid #3b82f6;
}
header {
  display: flex;
  align-items: baseline;
  gap: 12px;
  padding: 12px 18px;
  border-bottom: 1px solid #2a2a3a;
}
header .close {
  margin-left: auto;
  width: 26px;
  height: 26px;
  border: 1px solid transparent;
  border-radius: 0;
  background: transparent;
  color: #cdd6f4;
  font-size: 16px;
  cursor: pointer;
}
header .close:hover {
  border-color: #45475a;
}
h3 {
  margin: 0;
}
.panes {
  flex: 1;
  display: flex;
  min-height: 0;
}
nav {
  width: 220px;
  flex-shrink: 0;
  display: flex;
  flex-direction: column;
  border-right: 1px solid #2a2a3a;
  background: #141420;
  padding: 8px 0;
  overflow: auto;
}
nav button {
  padding: 9px 16px;
  border: none;
  border-left: 3px solid transparent;
  border-radius: 0;
  background: transparent;
  color: #cdd6f4;
  font: inherit;
  text-align: left;
  cursor: pointer;
}
nav button:hover {
  background: #1e1e2e;
}
nav button.active {
  background: #1e1e2e;
  border-left-color: #3b82f6;
}
main {
  flex: 1;
  overflow: auto;
  padding: 16px 24px 24px;
}
h4 {
  margin: 0 0 4px;
  font-size: 1.15em;
}
.lead {
  margin: 0 0 14px;
  opacity: 0.6;
  font-size: 0.9em;
}
.group {
  margin: 0 0 22px;
}
h5 {
  margin: 0;
  padding: 0 0 6px;
  font-size: 0.78em;
  text-transform: uppercase;
  letter-spacing: 0.06em;
  opacity: 0.7;
  border-bottom: 1px solid #3b3b52;
}
.field {
  display: grid;
  grid-template-columns: 210px 1fr;
  gap: 16px;
  padding: 10px 0;
  border-bottom: 1px solid #25253a;
  align-items: start;
}
.field > label {
  padding-top: 4px;
  font-weight: 500;
}
.control {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 4px;
  min-width: 0;
}
.desc {
  margin: 0;
  opacity: 0.6;
  font-size: 0.88em;
}
.ask-ai {
  border: none;
  background: none;
  color: #93c5fd;
  cursor: pointer;
  padding: 0 4px;
}
.field-error {
  color: #f59e0b;
  font-size: 0.85em;
  margin: 0;
}
.muted {
  opacity: 0.6;
  font-size: 0.9em;
}
</style>
