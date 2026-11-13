<script setup lang="ts">
// File ▸ Preferences (development_plan.md 4.2, 8.1): the per-user choices, in one place (also reachable from the AI
// panel's ⚙). Pages on the left; on the right the chosen page in groups, each setting a row: its name, then its control
// and what it does.
import { computed, nextTick, ref, watch } from 'vue'
import { editorState, setAiEnabled } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'
import { trackOverlay, untrackOverlay } from '../overlay'
import { prefs, requestPrefs, setPref } from '../prefs'
import { BUILD_METHODS, normalBuildMethod } from '../buildMethods'
import { projectState } from '../projects'
import SearchSelect from './SearchSelect.vue'

const backdrop = ref<HTMLElement | null>(null)

type Page = 'general' | 'viewport' | 'ai' | 'art' | 'beta' | 'new' | 'keyboard'
const PAGES: { id: Page; label: string; hint: string }[] = [
  { id: 'general', label: 'General', hint: 'launch, interface, recovery' },
  { id: 'viewport', label: 'Viewport', hint: 'grid, zoom' },
  { id: 'ai', label: 'AI', hint: 'the model, how its changes apply' },
  { id: 'art', label: 'Art and painting', hint: 'who draws and paints' },
  { id: 'beta', label: 'Beta Settings', hint: 'models in use, API keys' },
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

// Preferences ▸ AI ▸ Checks: the contradiction checker and the vision model, each a provider and a model.
interface CheckChoice {
  which: 'check' | 'vision'
  label: string
  provider: 'checkProvider' | 'visionProvider'
  model: 'checkModel' | 'visionModel'
  auto: string
  free: string
  desc: string
}
const CHECKS: CheckChoice[] = [
  { which: 'check', label: 'Canon check', provider: 'checkProvider', model: 'checkModel', auto: 'Mistral Medium (default)',
    free: 'GPT-6 Luna; free: nvidia/nemotron-3-ultra-550b-a55b:free', desc: 'Reads new writing against the World Bible and flags contradictions.' },
  { which: 'vision', label: 'Vision', provider: 'visionProvider', model: 'visionModel', auto: 'Claude itself when it writes, else Mistral Large 4',
    free: 'GPT-6 Luna; free: nvidia/nemotron-3.5-super-vl:free', desc: 'Looks at pictures: generated art, maps, doors, storeys.' },
]
const askedModels = new Set<string>()
function checkModels(c: CheckChoice): { value: string; hint?: string }[] {
  const provider = editorState.ai[c.provider]
  const key = provider === 'openrouter' && c.which === 'vision' ? 'openrouter-vision' : provider
  if (!editorState.ai.models[key] && editorState.ai.enabled && !askedModels.has(key)) {
    askedModels.add(key) // once: the list comes back as ai:models
    sendToHost({ type: 'ai:listModels', provider: key })
  }
  return (editorState.ai.models[key] ?? []).map((m) => ({ value: m, hint: m.endsWith(':free') ? 'free' : undefined }))
}
function setCheck(c: CheckChoice, provider: string, model: string): void {
  sendToHost({ type: 'ai:setCheckModels', [c.provider]: provider, [c.model]: model })
}
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

// Beta Settings: every job the app gives a model, with its provider and model and whether its key is set; and the
// API keys, kept encrypted for this Windows user (engine/ai_bridge/ApiKeys) and given to the AI service when it starts.
interface KeyRow {
  env: string
  provider: string
  uses: string
  url: string
  set: boolean
  tail?: string
  inEnv: string
}
const keys = ref<KeyRow[]>([])
const editingKey = ref('')
const keyValue = ref('')
const betaExpires = ref('') // this beta build's last day ('' in a build that isn't a beta)
onHostMessage((message) => {
  if (message.type === 'beta:keys') {
    keys.value = (message.keys as KeyRow[]) ?? []
    betaExpires.value = (message.expires as string) ?? ''
  }
})
// Asked each time the page is shown (switched to, or Preferences opened on it).
watch(
  () => [page.value, projectState.showPreferences] as const,
  ([p, open]) => {
    if (p === 'beta' && open) sendToHost({ type: 'beta:keysRequest' })
  },
  { immediate: true },
)
function keyOf(provider: string): string {
  const p = provider.toLowerCase()
  if (p === 'auto' || p.startsWith('mistral')) return 'MISTRAL_API_KEY'
  if (p.startsWith('claude') || p.startsWith('anthropic')) return 'ANTHROPIC_API_KEY'
  if (p.startsWith('openrouter')) return 'OPENROUTER_API_KEY'
  if (p.startsWith('bfl') || p.startsWith('flux')) return 'BFL_API_KEY'
  if (p.startsWith('pixellab')) return 'PIXELLAB_API_KEY'
  if (p.startsWith('autosprite')) return 'AUTO_SPRITE_API_KEY'
  return ''
}
function hasKey(env: string): boolean {
  const k = keys.value.find((r) => r.env === env)
  return !!k && (k.set || !!k.inEnv)
}
interface Job {
  job: string
  kind: string
  provider: string
  model: string
  page: Page
}
const jobs = computed<Job[]>(() => {
  const ai = editorState.ai
  const own = (which: string, app: string) => ai.project[which] || app
  const head = (s: string) => s.split(':')[0] ?? s // bfl:flux-3-image → bfl
  const writer = own('provider', ai.provider)
  const checkProvider = ai.checkProvider === 'auto' ? 'mistral' : ai.checkProvider
  const visionProvider = ai.visionProvider === 'auto' ? (writer === 'claude' ? 'claude' : 'mistral') : ai.visionProvider
  const art = own('art', ai.artProvider)
  return [
    { job: 'Writing (the AI panel, builds, the Planner)', kind: 'text', provider: writer, model: own('model', ai.model) || 'its default', page: 'ai' },
    { job: 'Canon check', kind: 'text', provider: checkProvider,
      model: ai.checkInUse || ai.checkModel || (checkProvider === 'mistral' ? 'mistral-medium-latest' : 'its default'), page: 'ai' },
    { job: 'Vision (looking at art, maps, doors)', kind: 'vision', provider: visionProvider,
      model: ai.visionInUse || ai.visionModel || (visionProvider === 'mistral' ? 'mistral-large-4' : visionProvider === 'claude' ? 'the writing model' : 'its default'), page: 'ai' },
    { job: "Help search (the AI panel's answers)", kind: 'embeddings', provider: 'mistral', model: 'mistral-embed (keywords alone without it)', page: 'ai' },
    { job: 'Prefab art', kind: 'image', provider: art === 'auto' ? (hasKey('MISTRAL_API_KEY') ? 'mistral' : 'pixellab') : head(art), model: ai.art || art, page: 'art' },
    { job: 'Backgrounds', kind: 'image', provider: 'bfl', model: own('background', ai.backgroundProvider).replace(/^bfl:/, ''), page: 'art' },
    { job: 'Painting places (Paint method)', kind: 'image', provider: 'bfl', model: String(prefs.paintModel ?? 'flux-3-image'), page: 'art' },
    { job: 'Humanoid animation', kind: 'animation', provider: head(own('animate', ai.animateProvider)), model: own('animate', ai.animateProvider), page: 'art' },
    { job: 'Non-humanoid animation', kind: 'animation', provider: head(own('objectAnimate', ai.objectAnimateProvider)),
      model: own('objectAnimate', ai.objectAnimateProvider), page: 'art' },
  ]
})
function providerName(provider: string): string {
  return keys.value.find((k) => k.env === keyOf(provider))?.provider ?? provider
}
function startKey(env: string): void {
  editingKey.value = env
  keyValue.value = ''
}
function saveKey(): void {
  if (!keyValue.value.trim()) return
  sendToHost({ type: 'beta:setKey', env: editingKey.value, value: keyValue.value.trim() })
  editingKey.value = ''
  keyValue.value = ''
}
function removeKey(env: string): void {
  sendToHost({ type: 'beta:removeKey', env })
}

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
                    <option value="openrouter">OpenRouter{{ noKey('openrouter') }}</option>
                  </select>
                </div>
              </div>
              <div class="field">
                <label>Model</label>
                <div class="control">
                  <div class="row">
                    <SearchSelect :model-value="editorState.ai.model" :disabled="!editorState.ai.enabled" empty="the default model" custom
                                  :options="models.map((m) => ({ value: m, hint: m.endsWith(':free') ? 'free' : undefined }))"
                                  @update:model-value="setModel" />
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
              <h5>Checks</h5>
              <div v-for="c in CHECKS" :key="c.which" class="field">
                <label>{{ c.label }}</label>
                <div class="control">
                  <div class="row">
                    <select :value="editorState.ai[c.provider]" :disabled="!editorState.ai.enabled" @change="setCheck(c, ($event.target as HTMLSelectElement).value, '')">
                      <option value="auto">{{ c.auto }}</option>
                      <option value="mistral">Mistral{{ noKey('mistral') }}</option>
                      <option value="openrouter">OpenRouter{{ noKey('openrouter') }}</option>
                    </select>
                    <SearchSelect v-if="editorState.ai[c.provider] !== 'auto'" :model-value="editorState.ai[c.model]" :disabled="!editorState.ai.enabled"
                                  :options="checkModels(c)" :empty="`its default${editorState.ai[c.provider] === 'openrouter' ? ` (${c.free})` : ''}`" custom
                                  @update:model-value="(m) => setCheck(c, editorState.ai[c.provider], m)" />
                  </div>
                  <p class="desc">{{ c.desc }}<template v-if="c.which === 'check' ? editorState.ai.checkInUse : editorState.ai.visionInUse"> In use: {{ c.which === 'check' ? editorState.ai.checkInUse : editorState.ai.visionInUse }}.</template></p>
                </div>
              </div>
              <p v-if="editorState.ai.checkError" class="error">{{ editorState.ai.checkError }}</p>
              <p class="desc">OpenRouter's default is GPT-6 Luna (cheap, and right on our checks). Its free models (:free) cost nothing but are rate limited and can be busy; they're listed first.</p>
            </section>
            <section class="group">
              <h5>Keys</h5>
              <div class="field">
                <label>API keys</label>
                <div class="control">
                  <p class="desc">Set in Beta Settings (kept encrypted for your Windows user). They never leave this computer except to that provider.</p>
                  <button class="small" @click="page = 'beta'">Beta Settings…</button>
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
                  <p v-if="editorState.ai.project.art" class="override">
                    This project uses its own: <b>{{ editorState.ai.project.art }}</b> (Project Settings ▸ AI), so the choice above doesn't apply to it.
                    <button class="small" @click="sendToHost({ type: 'ai:projectFollowsApp', which: 'art' })">Use this choice in the project</button>
                  </p>
                  <p v-if="editorState.ai.project.art" class="desc">This project uses its own: {{ editorState.ai.project.art }}.</p>
                </div>
              </div>
              <div class="field">
                <label>Characters, animations</label>
                <div class="control"><p class="desc">Characters (Generate ▸ Character) are drawn by the art model above when it's a BFL one,
                  else by PixelLab; animations by the Humanoid and Non-humanoid animation choices below.</p></div>
              </div>
            </section>
            <section class="group">
              <h5>Painting</h5>
              <div class="field">
                <label>Background</label>
                <div class="control">
                  <select :value="editorState.ai.backgroundProvider" :disabled="!editorState.ai.enabled" @change="sendToHost({ type: 'ai:setBackgroundProvider', provider: ($event.target as HTMLSelectElement).value })">
                    <option value="bfl:flux-3-image">BFL FLUX.3 Image (keeps the map best){{ noKey('bfl') }}</option>
                    <option value="bfl:flux-2-klein-4b">BFL FLUX.2 klein (fast, loose){{ noKey('bfl') }}</option>
                  </select>
                  <p v-if="editorState.ai.backgroundError" class="error">{{ editorState.ai.backgroundError }}</p>
                  <p class="desc">Who paints a place's background: its ground, roads and floors as one picture, from its plan.</p>
                  <p v-if="editorState.ai.project.background" class="override">
                    This project uses its own: <b>{{ editorState.ai.project.background }}</b> (Project Settings ▸ AI), so the choice above doesn't apply to it.
                    <button class="small" @click="sendToHost({ type: 'ai:projectFollowsApp', which: 'background' })">Use this choice in the project</button>
                  </p>
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
                  <p v-if="editorState.ai.project.animate" class="override">
                    This project uses its own: <b>{{ editorState.ai.project.animate }}</b> (Project Settings ▸ AI), so the choice above doesn't apply to it.
                    <button class="small" @click="sendToHost({ type: 'ai:projectFollowsApp', which: 'animate' })">Use this choice in the project</button>
                  </p>
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
                  <p v-if="editorState.ai.project.objectAnimate" class="override">
                    This project uses its own: <b>{{ editorState.ai.project.objectAnimate }}</b> (Project Settings ▸ AI), so the choice above doesn't apply to it.
                    <button class="small" @click="sendToHost({ type: 'ai:projectFollowsApp', which: 'objectAnimate' })">Use this choice in the project</button>
                  </p>
                </div>
              </div>
              <div class="field">
                <label>Paint model</label>
                <div class="control">
                  <select :value="prefs.paintModel" @change="setPref('paintModel', ($event.target as HTMLSelectElement).value)">
                    <option value="flux-3-image">FLUX-3-Image (keeps the layout best)</option>
                    <option value="flux-2-klein-4b">FLUX-2-Klein-4b (fast, a rough draft)</option>
                  </select>
                  <p class="desc">Who paints a place whole for the Paint build method, when the project doesn't say. Dungeons are always painted with FLUX-3-Image (the others lose their rooms and walls).</p>
                </div>
              </div>
            </section>
          </template>

          <!-- Beta Settings -->
          <template v-else-if="page === 'beta'">
            <h4>Beta Settings</h4>
            <p class="muted">The models this app uses, and your keys for them. The beta comes without keys: each provider is paid
              for with your own account.</p>
            <p v-if="betaExpires" class="override">This beta build works until <b>{{ betaExpires }}</b>. After that, download the new
              one from aige-dev.com/beta: your projects and keys are kept.</p>
            <section class="group">
              <h5>Models in use</h5>
              <table class="jobs">
                <thead><tr><th>For</th><th>Kind</th><th>Provider</th><th>Model</th><th>Key</th><th></th></tr></thead>
                <tbody>
                  <tr v-for="j in jobs" :key="j.job">
                    <td>{{ j.job }}</td>
                    <td class="dim">{{ j.kind }}</td>
                    <td>{{ providerName(j.provider) }}</td>
                    <td class="model">{{ j.model }}</td>
                    <td :class="hasKey(keyOf(j.provider)) ? 'ok' : 'error'">{{ hasKey(keyOf(j.provider)) ? '✓' : 'no key' }}</td>
                    <td><button class="small" title="Choose another provider or model" @click="page = j.page">Change</button></td>
                  </tr>
                </tbody>
              </table>
              <p class="desc">A project can choose its own (Project Settings ▸ AI): this list shows what the open project uses.</p>
            </section>
            <section class="group">
              <h5>API keys</h5>
              <p v-if="!keys.length" class="desc">Asking the editor for the keys… <button class="small" @click="sendToHost({ type: 'beta:keysRequest' })">Ask again</button></p>
              <div v-for="k in keys" :key="k.env" class="field">
                <label>{{ k.provider }}</label>
                <div class="control">
                  <div v-if="editingKey === k.env" class="row">
                    <input v-model="keyValue" type="password" autocomplete="off" spellcheck="false" :placeholder="k.env"
                           @keydown.enter.prevent="saveKey" @keydown.esc.stop="editingKey = ''" />
                    <button class="primary" :disabled="!keyValue.trim()" @click="saveKey">Save</button>
                    <button @click="editingKey = ''">Cancel</button>
                  </div>
                  <div v-else class="row">
                    <span v-if="k.set" class="ok">Set ••••{{ k.tail }}</span>
                    <span v-else-if="k.inEnv" class="dim">From {{ k.inEnv }}</span>
                    <span v-else class="dim">Not set</span>
                    <button class="small" @click="startKey(k.env)">{{ k.set ? 'Change' : 'Set API Key' }}</button>
                    <button v-if="k.set" class="small" @click="removeKey(k.env)">Remove</button>
                    <button class="small link" :title="k.url" @click="sendToHost({ type: 'web:open', url: k.url })">Get a key</button>
                  </div>
                  <p class="desc">For {{ k.uses }}.</p>
                </div>
              </div>
              <p class="desc">Kept on this computer, encrypted for your Windows user, and sent only to that provider. Setting or removing
                a key starts the AI again with it. A key set here is used over one in the system's environment.</p>
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
                  <label v-for="m in BUILD_METHODS" :key="m.id" class="row" :title="m.title"><input type="radio" name="pref-method" :checked="normalBuildMethod(prefs.defaultBuildMethod) === m.id" @change="setPref('defaultBuildMethod', m.id)" />
                    {{ m.label }}: {{ m.short }}</label>
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
.jobs {
  width: 100%;
  border-collapse: collapse;
  font-size: 0.92em;
}
.jobs th {
  text-align: left;
  font-weight: 600;
  opacity: 0.7;
  padding: 3px 6px;
  border-bottom: 1px solid #333;
}
.jobs td {
  padding: 3px 6px;
  border-bottom: 1px solid #262636;
  vertical-align: top;
}
.jobs .model {
  font-family: ui-monospace, Consolas, monospace;
  font-size: 0.9em;
  word-break: break-all;
}
.ok {
  color: #86efac;
}
.dim {
  opacity: 0.65;
}
.link {
  background: none;
  border: none;
  color: #93c5fd;
  padding: 0 2px;
}
.override {
  margin: 4px 0 0;
  padding: 4px 6px;
  border-left: 2px solid #f59e0b;
  background: #f59e0b14;
  font-size: 11.5px;
}
.override button {
  margin-left: 6px;
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
