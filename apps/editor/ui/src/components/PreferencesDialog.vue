<script setup lang="ts">
// File ▸ Preferences (development_plan.md 4.2, 8.1): the per-user choices, in one
// place (also reachable from the AI panel's ⚙). Sections on the left, the chosen
// one on the right.
import { computed, nextTick, ref, watch } from 'vue'
import { editorState, setAiEnabled } from '../editorState'
import { sendToHost } from '../ipc'
import { trackOverlay, untrackOverlay } from '../overlay'
import { projectState } from '../projects'

const backdrop = ref<HTMLElement | null>(null)

type Section = 'ai' | 'project' | 'keyboard'
const SECTIONS: { id: Section; label: string; hint: string }[] = [
  { id: 'ai', label: 'AI', hint: 'the model, and how its changes are applied' },
  { id: 'project', label: 'Projects', hint: 'what opens at launch' },
  { id: 'keyboard', label: 'Keyboard', hint: 'shortcuts' },
]
const section = ref<Section>('ai')

watch(
  () => projectState.showPreferences,
  async (open) => {
    await nextTick()
    if (open && backdrop.value) {
      trackOverlay(backdrop.value)
      backdrop.value.focus()
    }
  },
)

const models = computed(() => editorState.ai.models[editorState.ai.provider] ?? [])
function listModels(): void {
  if (editorState.ai.enabled) sendToHost({ type: 'ai:listModels', provider: editorState.ai.provider })
}
function setProvider(provider: string): void {
  // A new provider starts on its default model; its list comes once it's up.
  sendToHost({ type: 'ai:setProvider', provider, model: provider === 'mistral' ? 'ministral-8b-latest' : '' })
}
function setModel(model: string): void {
  sendToHost({ type: 'ai:setProvider', provider: editorState.ai.provider, model })
}
watch(
  () => [projectState.showPreferences, editorState.ai.state, editorState.ai.provider] as const,
  ([open, state]) => {
    if (open && state === 'ready' && !editorState.ai.models[editorState.ai.provider]) listModels()
  },
)

function close(): void {
  if (backdrop.value) untrackOverlay(backdrop.value)
  projectState.showPreferences = false
}
</script>

<template>
  <div
    v-if="projectState.showPreferences"
    ref="backdrop"
    class="backdrop ed-form"
    tabindex="-1"
    @mousedown.self="close"
    @keydown.esc.stop="close"
  >
    <div class="sheet">
      <header>
        <h3>Preferences</h3>
        <span class="muted">Yours, on this computer: not part of the project.</span>
        <button class="close" title="Close (Esc)" @click="close">×</button>
      </header>
      <div class="panes">
        <nav>
          <button v-for="s in SECTIONS" :key="s.id" :class="{ active: section === s.id }" @click="section = s.id">
            <span class="label">{{ s.label }}</span>
            <span class="hint">{{ s.hint }}</span>
          </button>
        </nav>
        <main>
          <template v-if="section === 'ai'">
            <h4>AI</h4>
            <label class="row">
              <input
                type="checkbox"
                :checked="editorState.ai.enabled"
                @change="setAiEnabled(($event.target as HTMLInputElement).checked)"
              />
              Use AI <span class="muted">(when off, the AI service isn't started and nothing is sent to a model)</span>
            </label>
            <h5>Its changes</h5>
            <label class="row">
              <input
                type="radio"
                name="pref-review"
                :checked="editorState.ai.reviewPolicy === 'reviewAll'"
                :disabled="!editorState.ai.enabled"
                @change="sendToHost({ type: 'ai:setReviewPolicy', policy: 'reviewAll' })"
              />
              Review every AI change before it's applied
            </label>
            <label class="row">
              <input
                type="radio"
                name="pref-review"
                :checked="editorState.ai.reviewPolicy === 'autoApplyTweaks'"
                :disabled="!editorState.ai.enabled"
                @change="sendToHost({ type: 'ai:setReviewPolicy', policy: 'autoApplyTweaks' })"
              />
              Apply small tweaks directly (with Undo)
            </label>
            <h5>Provider</h5>
            <div class="row">
              <select
                :value="editorState.ai.provider"
                :disabled="!editorState.ai.enabled"
                @change="setProvider(($event.target as HTMLSelectElement).value)"
              >
                <option value="mistral">Mistral{{ editorState.ai.available.mistral === false ? ' (no key)' : '' }}</option>
                <option value="claude">Claude{{ editorState.ai.available.claude === false ? ' (no key)' : '' }}</option>
              </select>
            </div>
            <h5>Model</h5>
            <div class="row">
              <select
                :value="editorState.ai.model"
                :disabled="!editorState.ai.enabled"
                @focus="listModels"
                @change="setModel(($event.target as HTMLSelectElement).value)"
              >
                <option v-if="!models.includes(editorState.ai.model)" :value="editorState.ai.model">
                  {{ editorState.ai.model || 'the default model' }}
                </option>
                <option v-for="m in models" :key="m" :value="m">{{ m }}</option>
              </select>
              <button :disabled="!editorState.ai.enabled" title="Ask the provider for its models again" @click="listModels">↻</button>
            </div>
            <p v-if="editorState.ai.modelsError" class="error">{{ editorState.ai.modelsError }}</p>
            <p v-if="editorState.ai.providerError" class="error">{{ editorState.ai.providerError }}</p>
            <h5>Art</h5>
            <div class="row">
              <select
                :value="editorState.ai.artProvider"
                :disabled="!editorState.ai.enabled"
                @change="sendToHost({ type: 'ai:setArtProvider', provider: ($event.target as HTMLSelectElement).value })"
              >
                <option value="auto">Follow the provider{{ editorState.ai.provider === 'claude' ? ' (Claude draws nothing: Mistral, else PixelLab)' : '' }}</option>
                <option value="mistral">Mistral{{ editorState.ai.available.mistral === false ? ' (no key)' : '' }}</option>
                <option value="pixellab">PixelLab{{ editorState.ai.available.pixellab === false ? ' (no key)' : '' }}</option>
              </select>
              <span v-if="editorState.ai.art" class="muted">drawing with {{ editorState.ai.art }}</span>
            </div>
            <p v-if="editorState.ai.artError" class="error">{{ editorState.ai.artError }}</p>
            <h5>Paint Provider</h5>
            <p class="muted">Who paints places from their plan, when a project builds them Painted (Project Settings ▸ Places).</p>
            <div class="row">
              <select
                :value="editorState.ai.paintProvider"
                :disabled="!editorState.ai.enabled"
                @change="sendToHost({ type: 'ai:setPaintProvider', provider: ($event.target as HTMLSelectElement).value, model: editorState.ai.paintModel })"
              >
                <option value="bfl">Black Forest Labs (FLUX)</option>
              </select>
              <select
                :value="editorState.ai.paintModel"
                :disabled="!editorState.ai.enabled"
                @change="sendToHost({ type: 'ai:setPaintProvider', provider: editorState.ai.paintProvider, model: ($event.target as HTMLSelectElement).value })"
              >
                <option value="flux-3-image">FLUX.3 Image: reads the plan best</option>
                <option value="flux-2-klein-4b">FLUX.2 klein: the best look, cheap</option>
                <option value="flux-2-max">FLUX.2 max: rich art, looser layout</option>
                <option value="flux-3-to-klein">FLUX.3 → klein: FLUX.3's layout, klein's look</option>
                <option value="flux-3-to-pro">FLUX.3 → FLUX.2 pro: FLUX.3's layout, refined by FLUX.2 pro</option>
              </select>
            </div>
            <p v-if="editorState.ai.paintError" class="error">{{ editorState.ai.paintError }}</p>
            <p class="muted">
              Keys go in ai_service/.env (MISTRAL_API_KEY, ANTHROPIC_API_KEY, PIXELLAB_API_KEY, BFL_API_KEY); they never leave this
              computer except to that service. Characters and animations (Generate ▸ Character, Animate) are always PixelLab's.
            </p>
          </template>
          <template v-else-if="section === 'project'">
            <h4>Projects</h4>
            <p class="muted path">{{ projectState.current }}</p>
            <label class="row">
              <input
                type="checkbox"
                :checked="projectState.openLast"
                @change="sendToHost({ type: 'prefs:openLast', on: ($event.target as HTMLInputElement).checked })"
              />
              Open the last project directly at launch
            </label>
            <p class="muted">Otherwise the welcome window opens; File ▸ Close Project shows it too.</p>
            <p class="muted">Project-wide settings (camera, combat, time…) are in File ▸ Project Settings…</p>
          </template>
          <template v-else>
            <h4>Keyboard</h4>
            <p class="muted">Every action's keys, and your own bindings.</p>
            <button class="primary" @click="(close(), (editorState.showShortcuts = true))">Keyboard shortcuts…</button>
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
  width: min(720px, 94vw);
  height: min(460px, 86vh);
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
  padding: 12px 16px;
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
  width: 190px;
  flex-shrink: 0;
  display: flex;
  flex-direction: column;
  border-right: 1px solid #2a2a3a;
  background: #141420;
  padding: 6px 0;
}
nav button {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 2px;
  padding: 8px 14px;
  border: none;
  border-left: 2px solid transparent;
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
nav .hint {
  font-size: 0.8em;
  opacity: 0.55;
}
main {
  flex: 1;
  overflow: auto;
  padding: 12px 18px;
}
h4 {
  margin: 0 0 10px;
  font-size: 1em;
}
h5 {
  margin: 14px 0 4px;
  font-size: 0.78em;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  opacity: 0.6;
}
.row {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 3px 0;
}
.error {
  color: #f38ba8;
}
.muted {
  opacity: 0.6;
  font-size: 0.9em;
  margin: 4px 0;
}
.path {
  word-break: break-all;
}
</style>
