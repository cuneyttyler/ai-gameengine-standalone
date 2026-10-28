<script setup lang="ts">
// File ▸ Preferences (development_plan.md 4.2, 8.1): the per-user choices, in one place (also reachable from the AI
// panel's ⚙). Pages on the left; on the right the chosen page in groups, each setting a row: its name, then its control
// and what it does.
import { computed, nextTick, ref, watch } from 'vue'
import { editorState, setAiEnabled } from '../editorState'
import { sendToHost } from '../ipc'
import { trackOverlay, untrackOverlay } from '../overlay'
import { prefs, requestPrefs, setPref } from '../prefs'
import { projectState } from '../projects'

const backdrop = ref<HTMLElement | null>(null)

type Page = 'general' | 'viewport' | 'ai' | 'art' | 'new' | 'keyboard'
const PAGES: { id: Page; label: string; hint: string }[] = [
  { id: 'general', label: 'General', hint: 'launch, interface, recovery' },
  { id: 'viewport', label: 'Viewport', hint: 'grid, zoom' },
  { id: 'ai', label: 'AI', hint: 'the model, how its changes apply' },
  { id: 'art', label: 'Art and painting', hint: 'who draws and paints' },
  { id: 'new', label: 'New projects', hint: 'what they start with' },
  { id: 'keyboard', label: 'Keyboard', hint: 'shortcuts' },
]
const page = ref<Page>('general')

watch(
  () => projectState.showPreferences,
  async (open) => {
    await nextTick()
    if (open && backdrop.value) {
      trackOverlay(backdrop.value)
      backdrop.value.focus()
      requestPrefs()
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
const noKey = (which: string): string => (editorState.ai.available[which as keyof typeof editorState.ai.available] === false ? ' (no key)' : '')

function close(): void {
  if (backdrop.value) untrackOverlay(backdrop.value)
  projectState.showPreferences = false
}
</script>

<template>
  <div v-if="projectState.showPreferences" ref="backdrop" class="backdrop ed-form" tabindex="-1" @mousedown.self="close" @keydown.esc.stop="close">
    <div class="sheet">
      <header>
        <h3>Preferences</h3>
        <span class="muted">Yours, on this computer: not part of the project.</span>
        <button class="close" title="Close (Esc)" @click="close">×</button>
      </header>
      <div class="panes">
        <nav>
          <button v-for="p in PAGES" :key="p.id" :class="{ active: page === p.id }" @click="page = p.id">
            <span class="label">{{ p.label }}</span>
            <span class="hint">{{ p.hint }}</span>
          </button>
        </nav>
        <main>
          <!-- General -->
          <template v-if="page === 'general'">
            <h4>General</h4>
            <section class="group">
              <h5>Launch</h5>
              <div class="field">
                <label>Open the last project</label>
                <div class="control">
                  <input type="checkbox" :checked="projectState.openLast" @change="sendToHost({ type: 'prefs:openLast', on: ($event.target as HTMLInputElement).checked })" />
                  <p class="desc">At launch, open {{ projectState.name || 'the last project' }} directly instead of the welcome window.</p>
                </div>
              </div>
              <div class="field">
                <label>This project</label>
                <div class="control"><p class="desc path">{{ projectState.current }}</p></div>
              </div>
            </section>
            <section class="group">
              <h5>Interface</h5>
              <div class="field">
                <label>Interface scale</label>
                <div class="control">
                  <select :value="prefs.uiScale" @change="setPref('uiScale', Number(($event.target as HTMLSelectElement).value))">
                    <option v-for="s in [0.8, 0.9, 1, 1.1, 1.25, 1.5]" :key="s" :value="s">{{ Math.round(s * 100) }}%</option>
                  </select>
                  <p class="desc">The size of the panels' text and controls (the viewport isn't scaled).</p>
                </div>
              </div>
            </section>
            <section class="group">
              <h5>Recovery</h5>
              <div class="field">
                <label>Keep unsaved changes</label>
                <div class="control">
                  <select :value="prefs.recoveryMinutes" @change="setPref('recoveryMinutes', Number(($event.target as HTMLSelectElement).value))">
                    <option :value="1">every minute</option>
                    <option :value="2">every 2 minutes</option>
                    <option :value="5">every 5 minutes</option>
                    <option :value="10">every 10 minutes</option>
                    <option :value="0">never</option>
                  </select>
                  <p class="desc">A copy of the open scene's unsaved changes, offered back if the editor closes without saving.</p>
                </div>
              </div>
            </section>
          </template>

          <!-- Viewport -->
          <template v-else-if="page === 'viewport'">
            <h4>Viewport</h4>
            <section class="group">
              <h5>Grid</h5>
              <div class="field">
                <label>Show the grid at start</label>
                <div class="control">
                  <input type="checkbox" :checked="prefs.showGridAtStart" @change="setPref('showGridAtStart', ($event.target as HTMLInputElement).checked)" />
                  <p class="desc">Tile borders on empty ground when the editor opens (View ▸ Grid turns it on and off).</p>
                </div>
              </div>
            </section>
            <section class="group">
              <h5>Camera</h5>
              <div class="field">
                <label>Zoom speed</label>
                <div class="control">
                  <select :value="prefs.zoomSpeed" @change="setPref('zoomSpeed', Number(($event.target as HTMLSelectElement).value))">
                    <option :value="1.05">slow</option>
                    <option :value="1.1">medium</option>
                    <option :value="1.15">normal</option>
                    <option :value="1.25">fast</option>
                  </select>
                  <p class="desc">How far one step of the wheel zooms.</p>
                </div>
              </div>
              <div class="field">
                <label>Invert the wheel</label>
                <div class="control">
                  <input type="checkbox" :checked="prefs.invertZoom" @change="setPref('invertZoom', ($event.target as HTMLInputElement).checked)" />
                  <p class="desc">Wheel up zooms out.</p>
                </div>
              </div>
              <div class="field">
                <label>Moving the camera</label>
                <div class="control">
                  <p class="desc">Drag empty ground with the Select tool, or drag with the right or middle button. Shift+drag draws a selection box.</p>
                </div>
              </div>
            </section>
          </template>

          <!-- AI -->
          <template v-else-if="page === 'ai'">
            <h4>AI</h4>
            <section class="group">
              <h5>The AI</h5>
              <div class="field">
                <label>Use AI</label>
                <div class="control">
                  <input type="checkbox" :checked="editorState.ai.enabled" @change="setAiEnabled(($event.target as HTMLInputElement).checked)" />
                  <p class="desc">When off, the AI service isn't started and nothing is sent to a model.</p>
                </div>
              </div>
              <div class="field">
                <label>Its changes</label>
                <div class="control">
                  <label class="row"><input type="radio" name="pref-review" :checked="editorState.ai.reviewPolicy === 'reviewAll'" :disabled="!editorState.ai.enabled"
                         @change="sendToHost({ type: 'ai:setReviewPolicy', policy: 'reviewAll' })" /> Review every change before it's applied</label>
                  <label class="row"><input type="radio" name="pref-review" :checked="editorState.ai.reviewPolicy === 'autoApplyTweaks'" :disabled="!editorState.ai.enabled"
                         @change="sendToHost({ type: 'ai:setReviewPolicy', policy: 'autoApplyTweaks' })" /> Apply small tweaks directly (with Undo)</label>
                </div>
              </div>
            </section>
            <section class="group">
              <h5>Language model</h5>
              <div class="field">
                <label>Provider</label>
                <div class="control">
                  <select :value="editorState.ai.provider" :disabled="!editorState.ai.enabled" @change="setProvider(($event.target as HTMLSelectElement).value)">
                    <option value="mistral">Mistral{{ noKey('mistral') }}</option>
                    <option value="claude">Claude{{ noKey('claude') }}</option>
                  </select>
                </div>
              </div>
              <div class="field">
                <label>Model</label>
                <div class="control">
                  <div class="row">
                    <select :value="editorState.ai.model" :disabled="!editorState.ai.enabled" @focus="listModels" @change="setModel(($event.target as HTMLSelectElement).value)">
                      <option v-if="!models.includes(editorState.ai.model)" :value="editorState.ai.model">{{ editorState.ai.model || 'the default model' }}</option>
                      <option v-for="m in models" :key="m" :value="m">{{ m }}</option>
                    </select>
                    <button :disabled="!editorState.ai.enabled" title="Ask the provider for its models again" @click="listModels">↻</button>
                  </div>
                  <p v-if="editorState.ai.modelsError" class="error">{{ editorState.ai.modelsError }}</p>
                  <p v-if="editorState.ai.providerError" class="error">{{ editorState.ai.providerError }}</p>
                  <p v-if="editorState.ai.project.provider || editorState.ai.project.model" class="desc">
                    This project uses its own: {{ [editorState.ai.project.provider, editorState.ai.project.model].filter(Boolean).join(' ') }} (Project Settings ▸ AI).
                  </p>
                </div>
              </div>
            </section>
            <section class="group">
              <h5>Keys</h5>
              <div class="field">
                <label>API keys</label>
                <div class="control">
                  <p class="desc">In ai_service/.env (MISTRAL_API_KEY, ANTHROPIC_API_KEY, PIXELLAB_API_KEY, BFL_API_KEY). They never leave this computer except to that service.</p>
                </div>
              </div>
            </section>
          </template>

          <!-- Art and painting -->
          <template v-else-if="page === 'art'">
            <h4>Art and painting</h4>
            <p class="muted">The app's choices; a project can have its own (Project Settings ▸ AI), and a build its own (Build ▸ Assets).</p>
            <section class="group">
              <h5>Art</h5>
              <div class="field">
                <label>Prefab art</label>
                <div class="control">
                  <select :value="editorState.ai.artProvider" :disabled="!editorState.ai.enabled" @change="sendToHost({ type: 'ai:setArtProvider', provider: ($event.target as HTMLSelectElement).value })">
                    <option value="auto">Follow the provider{{ editorState.ai.provider === 'claude' ? ' (Claude draws nothing: Mistral, else PixelLab)' : '' }}</option>
                    <option value="mistral">Mistral{{ noKey('mistral') }}</option>
                    <option value="pixellab">PixelLab{{ noKey('pixellab') }}</option>
                    <option value="bfl:flux-2-klein-4b">BFL FLUX.2 klein{{ noKey('bfl') }}</option>
                    <option value="bfl:flux-3-image">BFL FLUX.3 Image{{ noKey('bfl') }}</option>
                    <option value="bfl:flux-2-pro">BFL FLUX.2 pro{{ noKey('bfl') }}</option>
                  </select>
                  <p v-if="editorState.ai.artError" class="error">{{ editorState.ai.artError }}</p>
                  <p class="desc">Who draws the prefabs' art: every piece made in one style (Assembled builds), and the Art tool's single pieces.</p>
                  <p v-if="editorState.ai.project.art" class="desc">This project uses its own: {{ editorState.ai.project.art }}.</p>
                </div>
              </div>
              <div class="field">
                <label>Characters, animations</label>
                <div class="control"><p class="desc">Always PixelLab's (Generate ▸ Character, Animate).</p></div>
              </div>
            </section>
            <section class="group">
              <h5>Painting</h5>
              <div class="field">
                <label>Background</label>
                <div class="control">
                  <select :value="editorState.ai.backgroundProvider" :disabled="!editorState.ai.enabled" @change="sendToHost({ type: 'ai:setBackgroundProvider', provider: ($event.target as HTMLSelectElement).value })">
                    <option value="bfl:flux-3-image">BFL FLUX.3 Image (keeps the map best){{ noKey('bfl') }}</option>
                    <option value="bfl:flux-2-max">BFL FLUX.2 max (as good, slower){{ noKey('bfl') }}</option>
                    <option value="bfl:flux-2-pro">BFL FLUX.2 pro{{ noKey('bfl') }}</option>
                    <option value="bfl:flux-2-klein-4b">BFL FLUX.2 klein (fast, loose){{ noKey('bfl') }}</option>
                  </select>
                  <p v-if="editorState.ai.backgroundError" class="error">{{ editorState.ai.backgroundError }}</p>
                  <p class="desc">Who paints a place's background: its ground, roads and floors as one picture, from its plan.</p>
                  <p v-if="editorState.ai.project.background" class="desc">This project uses its own: {{ editorState.ai.project.background }}.</p>
                </div>
              </div>
              <div class="field">
                <label>Humanoid animation</label>
                <div class="control">
                  <select :value="editorState.ai.animateProvider" :disabled="!editorState.ai.enabled" @change="sendToHost({ type: 'ai:setAnimateProvider', provider: ($event.target as HTMLSelectElement).value })">
                    <option value="autosprite">AutoSprite (its motions or your own; a character's 8 directions at once){{ noKey('autosprite') }}</option>
                    <option value="pixellab:animate-v3">PixelLab Animate with text v3 (8 frames, up to 256 px; slow){{ noKey('pixellab') }}</option>
                  </select>
                  <p class="desc">What makes a person or a creature move (Generate ▸ Animate, a character in every direction).</p>
                </div>
              </div>
              <div class="field">
                <label>Non-humanoid animation</label>
                <div class="control">
                  <select :value="editorState.ai.objectAnimateProvider" :disabled="!editorState.ai.enabled" @change="sendToHost({ type: 'ai:setObjectAnimateProvider', provider: ($event.target as HTMLSelectElement).value })">
                    <option value="pixellab">PixelLab Animate with text v3 (the thing kept as drawn; 8 frames){{ noKey('pixellab') }}</option>
                    <option value="bfl:flux-video">BFL FLUX video (a 5 s clip, 12 frames taken from it){{ noKey('bfl') }}</option>
                  </select>
                  <p class="desc">What makes a thing move: a door opening (Build ▸ Animate), a chest, a lever, a flag. AutoSprite animates
                    characters: given a thing, it adds a person.</p>
                </div>
              </div>
              <div class="field">
                <label>Paint model</label>
                <div class="control">
                  <select :value="prefs.paintModel" @change="setPref('paintModel', ($event.target as HTMLSelectElement).value)">
                    <option value="flux-3-image">FLUX-3-Image (keeps the layout best)</option>
                    <option value="flux-2-klein-4b">FLUX-2-Klein-4b (fast, a rough draft)</option>
                    <option value="flux-2-pro">FLUX-2-Pro</option>
                    <option value="flux-2-max">FLUX-2-Max</option>
                    <option value="flux-3-to-pro">FLUX-3-Image-to-Pro</option>
                    <option value="flux-3-to-klein">FLUX-3-Image-to-Klein</option>
                  </select>
                  <p class="desc">Who paints a place whole for the Paint build method, when the project doesn't say.</p>
                </div>
              </div>
            </section>
          </template>

          <!-- New projects -->
          <template v-else-if="page === 'new'">
            <h4>New projects</h4>
            <section class="group">
              <h5>Places</h5>
              <div class="field">
                <label>Build method</label>
                <div class="control">
                  <label class="row"><input type="radio" name="pref-method" :checked="prefs.defaultBuildMethod === 'Paint'" @change="setPref('defaultBuildMethod', 'Paint')" />
                    Paint: each place painted whole, then marked by you</label>
                  <label class="row"><input type="radio" name="pref-method" :checked="prefs.defaultBuildMethod === 'Assembled'" @change="setPref('defaultBuildMethod', 'Assembled')" />
                    Assembled: each piece's art put together over a painted background</label>
                  <p class="desc">What File ▸ New Project starts with (it can be changed there, and later in Project Settings ▸ Places).</p>
                </div>
              </div>
            </section>
          </template>

          <!-- Keyboard -->
          <template v-else>
            <h4>Keyboard</h4>
            <section class="group">
              <h5>Shortcuts</h5>
              <div class="field">
                <label>Key bindings</label>
                <div class="control">
                  <button class="primary" @click="(close(), (editorState.showShortcuts = true))">Keyboard shortcuts…</button>
                  <p class="desc">Every action's keys, and your own bindings.</p>
                </div>
              </div>
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
}
nav button {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 2px;
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
nav .hint {
  font-size: 0.8em;
  opacity: 0.55;
}
main {
  flex: 1;
  overflow: auto;
  padding: 16px 24px 24px;
}
h4 {
  margin: 0 0 12px;
  font-size: 1.15em;
}
/* A group: its title over a line, then its settings, each a row with a line under it. */
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
.control select {
  min-width: 260px;
}
.desc {
  margin: 0;
  opacity: 0.6;
  font-size: 0.88em;
}
.row {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 2px 0;
}
.error {
  color: #f38ba8;
  margin: 0;
}
.muted {
  opacity: 0.6;
  font-size: 0.9em;
  margin: -4px 0 14px;
}
.path {
  word-break: break-all;
}
</style>
