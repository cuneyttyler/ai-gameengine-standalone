<script setup lang="ts">
// File ▸ Project Settings (development_plan.md 5.5): the project-wide settings
// (renderer, camera, combat, game, time…) in their own window with a close
// button, instead of taking over the Inspector. Generated from the registry.
import { nextTick, reactive, ref, watch } from 'vue'
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
const collapsed = reactive<Record<string, boolean>>({})
const errors = reactive<Record<string, string>>({})

onHostMessage((message) => {
  if (message.type === 'settings:data') {
    sections.value = message.sections as SettingsSection[]
  } else if (message.type === 'inspector:fieldError') {
    const key = `${message.component}.${message.field}`
    if (message.error) errors[key] = message.error as string
    else delete errors[key]
  }
})

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
</script>

<template>
  <div
    v-if="projectState.showProjectSettings"
    ref="backdrop"
    class="backdrop ed-form"
    tabindex="-1"
    @mousedown.self="close"
    @keydown.esc.stop="close"
  >
    <div class="sheet">
      <header>
        <h3>Project Settings</h3>
        <button title="Close (Esc)" @click="close">×</button>
      </header>
      <p class="muted">Saved with the project (settings.aige) when you save. Changes apply right away.</p>
      <div class="sections">
        <section v-for="section in sections" :key="section.name">
          <button class="collapse" :title="section.description" @click="collapsed[section.name] = !collapsed[section.name]">
            {{ collapsed[section.name] ? '▸' : '▾' }} {{ section.name }}
            <span class="muted">{{ section.description }}</span>
          </button>
          <div v-if="!collapsed[section.name]" class="fields">
            <template v-for="field in section.fields" :key="field.name">
              <div class="field-row">
                <label :title="field.description">
                  {{ field.name }}
                  <span v-if="field.description" class="help" :title="field.description">?</span>
                  <button
                    v-if="editorState.ai.state === 'ready'"
                    class="ask-ai"
                    title="Ask AI about this setting"
                    @click="askAi(section.name, field.name)"
                  >
                    ✦
                  </button>
                </label>
                <FieldEditor :field="field" :disabled="!!field.locked" :title="field.locked || undefined" @change="set(section.name, field, $event)" />
              </div>
              <div v-if="errors[`${section.name}.${field.name}`]" class="field-error">
                {{ errors[`${section.name}.${field.name}`] }}
              </div>
            </template>
          </div>
        </section>
        <section>
          <button class="collapse" title="What the player presses for each action" @click="collapsed.Input = !collapsed.Input">
            {{ collapsed.Input ? '▸' : '▾' }} Input
            <span class="muted">Actions and their keys, mouse and gamepad buttons (input.json).</span>
          </button>
          <InputSettings v-if="!collapsed.Input" class="fields" />
        </section>
      </div>
    </div>
  </div>
</template>

<style scoped>
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
  width: min(640px, 92vw);
  max-height: 84vh;
  display: flex;
  flex-direction: column;
  background: #181825;
  border: 1px solid #333;
  border-top: 2px solid #3b82f6;
  padding: 14px 18px;
}
header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}
h3 {
  margin: 0;
}
.sections {
  overflow: auto;
  margin-top: 8px;
}
.collapse {
  width: 100%;
  text-align: left;
  border: none;
  background: #11111b;
  color: #eee;
  font: inherit;
  font-weight: 600;
  padding: 6px 8px;
  margin-top: 6px;
  cursor: pointer;
}
.collapse .muted {
  font-weight: 400;
  margin-left: 8px;
}
.fields {
  padding: 4px 8px;
}
.field-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  padding: 3px 0;
}
.field-row label {
  opacity: 0.8;
  white-space: nowrap;
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
  padding-bottom: 4px;
}
.muted {
  opacity: 0.6;
  font-size: 0.9em;
  margin: 4px 0;
}
</style>
