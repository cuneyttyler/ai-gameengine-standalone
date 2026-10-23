<script setup lang="ts">
// The game's UI (development_plan.md 17.7, docs/systems/game-ui.md): its screens as
// documents (RML with RCSS styles), the theme's tokens, notification rules and
// checks. The engine's documents are read-only; Customise copies one into the
// project's ui/ folder, where it wins. Preview shows a screen with sample data over
// the viewport while editing.
import { computed, onBeforeUnmount, onMounted, reactive, ref } from 'vue'
import { onHostMessage, sendToHost } from '../ipc'
import { editorState } from '../editorState'

interface Screen {
  id: string
  document: string
  layer: string
  pauses: boolean
  source: string // project | engine | '' (the generic menu)
  custom?: boolean
}

const screens = ref<Screen[]>([])
const themes = ref<string[]>([])
const tokens = ref<Record<string, string>>({})
const themeBase = ref('stone')
const overrides = reactive<Record<string, string>>({})
const rulesText = ref('')
const rulesFromProject = ref(false)
const problems = ref<string[]>([])
const checked = ref<{ screen: string; message: string }[] | null>(null)
const documentsOn = ref(true)
const selected = ref('')
const source = reactive({ document: '', text: '', project: false, dirty: false })
const preview = ref('')
const section = ref<'screens' | 'theme' | 'notifications'>('screens')
const newId = ref('')

onHostMessage((message) => {
  if (message.type === 'ui:model') {
    screens.value = message.screens as Screen[]
    themes.value = message.themes as string[]
    tokens.value = message.tokens as Record<string, string>
    const theme = message.theme as { base?: string; tokens?: Record<string, string> }
    themeBase.value = theme.base ?? 'stone'
    for (const key of Object.keys(overrides)) delete overrides[key]
    Object.assign(overrides, theme.tokens ?? {})
    const rules = message.notifications as { text: string; project: boolean } | null
    if (rules) {
      rulesText.value = rules.text
      rulesFromProject.value = rules.project
    }
    problems.value = message.problems as string[]
    documentsOn.value = message.documents as boolean
    preview.value = (message.preview as string) || ''
    if (selected.value) loadSource()
  } else if (message.type === 'ui:source') {
    Object.assign(source, { document: message.document, text: message.text, project: message.project, dirty: false })
  } else if (message.type === 'ui:checked') {
    checked.value = message.problems as { screen: string; message: string }[]
  }
})

onMounted(() => sendToHost({ type: 'ui:request' }))
onBeforeUnmount(() => preview.value && sendToHost({ type: 'ui:preview', id: '' }))

const current = computed(() => screens.value.find((s) => s.id === selected.value))

function pick(id: string): void {
  selected.value = id
  loadSource()
}
function loadSource(): void {
  const screen = current.value
  if (!screen) return
  const document = screen.source === 'project' ? screen.document : screen.document || 'screens/menu.rml'
  sendToHost({ type: 'ui:source', document })
}
function customise(): void {
  if (!current.value) return
  sendToHost({ type: 'ui:customise', id: current.value.id, document: current.value.document || 'screens/menu.rml' })
}
function saveSource(): void {
  sendToHost({ type: 'ui:save', path: `ui/${source.document}`, text: source.text })
  source.dirty = false
}
function togglePreview(id: string): void {
  preview.value = preview.value === id ? '' : id
  sendToHost({ type: 'ui:preview', id: preview.value })
}
function newScreen(): void {
  const id = newId.value.trim().toLowerCase().replace(/[^a-z0-9-]+/g, '-')
  if (!id) return
  const text = `<rml>
<head>
\t<title>${id}</title>
\t<link type="text/rcss" href="../styles/base.rcss"/>
</head>
<!-- A screen of the project's own: open it from Lua with open_screen("${id}");
     action('...') calls reach scripts as the "ui_action" event. -->
<body>
<div data-model="ui">
\t<div class="panel" style="position: absolute; left: 30%; top: 25%; width: 40%;">
\t\t<h1>${id}</h1>
\t\t<p data-event-click="action('close-${id}')">Close</p>
\t</div>
</div>
</body>
</rml>
`
  sendToHost({ type: 'ui:save', path: `ui/screens/${id}.rml`, text })
  newId.value = ''
  selected.value = id
}
function saveTheme(): void {
  const clean: Record<string, string> = {}
  for (const [k, v] of Object.entries(overrides)) if (v && v.trim()) clean[k] = v.trim()
  sendToHost({ type: 'ui:theme', base: themeBase.value, tokens: clean })
}
function saveRules(): void {
  sendToHost({ type: 'ui:save', path: 'ui/notifications.json', text: rulesText.value })
}
function check(): void {
  checked.value = null
  sendToHost({ type: 'ui:check' })
}
function isColour(value: string): boolean {
  return /^#[0-9a-fA-F]{6,8}$/.test(value)
}
</script>

<template>
  <div class="uipanel ed-form">
    <div class="topbar">
      <div class="view-switch">
        <button :class="{ on: section === 'screens' }" @click="section = 'screens'">Screens</button>
        <button :class="{ on: section === 'theme' }" @click="section = 'theme'">Theme</button>
        <button :class="{ on: section === 'notifications' }" @click="section = 'notifications'">Notifications</button>
      </div>
      <span class="spacer"></span>
      <button title="Show every screen once and list what doesn't load" @click="check">Check all</button>
    </div>
    <p v-if="!documentsOn" class="note">
      Project Settings ▸ UI.documents is off: the game draws the built-in overlays instead of these documents.
    </p>
    <ul v-if="checked && checked.length" class="problems">
      <li v-for="(p, i) in checked" :key="i"><b>{{ p.screen }}</b>: {{ p.message }}</li>
    </ul>
    <p v-else-if="checked" class="muted small">Every screen loads cleanly.</p>
    <ul v-if="problems.length" class="problems">
      <li v-for="(p, i) in problems" :key="i">{{ p }}</li>
    </ul>

    <!-- Screens -------------------------------------------------------------->
    <div v-if="section === 'screens'" class="split">
      <aside class="list">
        <button v-for="s in screens" :key="s.id" class="row" :class="{ active: selected === s.id }" @click="pick(s.id)">
          <span class="name">{{ s.id }}</span>
          <span class="badge" :class="s.source">{{ s.source || 'menu' }}</span>
        </button>
        <div class="new-row">
          <input v-model="newId" placeholder="new screen id" spellcheck="false" @keydown.enter="newScreen" />
          <button :disabled="!newId.trim()" @click="newScreen">+</button>
        </div>
      </aside>
      <section v-if="current" class="editor">
        <div class="actions">
          <b>{{ current.id }}</b>
          <span class="muted small">{{ current.layer }}{{ current.pauses ? ', pauses' : '' }} · {{ source.document }}</span>
          <span class="spacer"></span>
          <button :class="{ on: preview === current.id }" :disabled="editorState.playing" @click="togglePreview(current.id)">
            {{ preview === current.id ? 'Stop preview' : 'Preview' }}
          </button>
          <button v-if="!source.project" title="Copy it into the project's ui/ folder to change it" @click="customise">
            Customise
          </button>
        </div>
        <p v-if="!source.project" class="muted small">
          The engine's document (read-only). Customise copies it into <code>ui/screens/{{ current.id }}.rml</code>.
        </p>
        <textarea
          v-model="source.text"
          class="code"
          spellcheck="false"
          :readonly="!source.project"
          @input="source.dirty = true"
        ></textarea>
        <div v-if="source.project" class="actions">
          <button class="primary" :disabled="!source.dirty" @click="saveSource">Save</button>
          <span v-if="source.dirty" class="muted small">Unsaved changes</span>
        </div>
        <p class="muted small">
          Models: <code>menu</code> (title, subtitle, body, items[label, enabled], highlighted, choose(i)),
          <code>dialogue</code> (speaker, text, portrait, choices, choose(i)), <code>hud</code> (health, health_max,
          mana, stamina, level, xp, gold, quest, objective, clock, prompt_key, prompt_text, subtitle, marker),
          <code>notes</code> (toast, feed, banner), <code>ui</code> (action('…') for scripts). Colours: <code>var(--token)</code>.
        </p>
      </section>
      <section v-else class="editor muted">Pick a screen.</section>
    </div>

    <!-- Theme ---------------------------------------------------------------->
    <section v-else-if="section === 'theme'" class="editor">
      <div class="actions">
        <label class="muted small">Base theme</label>
        <select v-model="themeBase">
          <option v-for="t in themes" :key="t" :value="t">{{ t }}</option>
        </select>
        <button class="primary" @click="saveTheme">Save theme</button>
        <button :class="{ on: preview === 'pause' }" :disabled="editorState.playing" @click="togglePreview('pause')">
          Preview
        </button>
      </div>
      <p class="muted small">Leave a value empty to use the base theme's. Changing the theme restyles every screen.</p>
      <div v-for="(value, name) in tokens" :key="name" class="token">
        <span class="swatch" :style="{ background: isColour(overrides[name] || value) ? overrides[name] || value : 'transparent' }"></span>
        <span class="tname">--{{ name }}</span>
        <span class="muted small">{{ value }}</span>
        <input v-model="overrides[name]" :placeholder="value" spellcheck="false" />
      </div>
    </section>

    <!-- Notifications --------------------------------------------------------->
    <section v-else class="editor">
      <p class="muted small">
        Rules turn the game's events into notifications: <code>{"event", "channel": toast|feed|banner|alert, "text"
        (with {amount}, {text}, {count}), "seconds", "coalesce": "text"}</code>.
        {{ rulesFromProject ? "The project's rules." : "The engine's rules; saving makes them the project's." }}
      </p>
      <textarea v-model="rulesText" class="code" spellcheck="false"></textarea>
      <div class="actions"><button class="primary" @click="saveRules">Save rules</button></div>
    </section>
  </div>
</template>

<style scoped>
.uipanel {
  display: flex;
  flex-direction: column;
  height: 100%;
  min-height: 0;
}
.topbar {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 6px 8px;
  border-bottom: 1px solid #2a2a3a;
}
.view-switch {
  display: flex;
}
.view-switch button {
  padding: 2px 10px;
}
.view-switch button.on,
button.on {
  background: #313148;
  border-color: #3b82f6;
}
.split {
  display: flex;
  flex: 1;
  min-height: 0;
}
.list {
  width: 190px;
  flex-shrink: 0;
  overflow-y: auto;
  border-right: 1px solid #2a2a3a;
  display: flex;
  flex-direction: column;
}
.row {
  display: flex;
  gap: 6px;
  padding: 3px 8px !important;
  border: none !important;
  background: none !important;
  text-align: left;
}
.row:hover {
  background: #262637 !important;
}
.row.active {
  background: #313148 !important;
}
.name {
  flex: 1;
}
.badge {
  font-size: 0.75em;
  padding: 0 5px;
  border: 1px solid #3a3a50;
  opacity: 0.8;
}
.badge.project {
  border-color: #fbbf24;
  color: #fbbf24;
}
.new-row {
  display: flex;
  gap: 4px;
  padding: 6px;
}
.new-row input {
  flex: 1;
  min-width: 0;
}
.editor {
  flex: 1;
  min-width: 0;
  overflow: auto;
  padding: 8px 10px;
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.actions {
  display: flex;
  align-items: center;
  gap: 6px;
  flex-wrap: wrap;
}
.code {
  flex: 1;
  min-height: 260px;
  font-family: Consolas, monospace;
  font-size: 12px;
  line-height: 1.45;
  resize: vertical;
  white-space: pre;
}
.token {
  display: grid;
  grid-template-columns: 18px 150px 120px 1fr;
  align-items: center;
  gap: 6px;
}
.swatch {
  width: 14px;
  height: 14px;
  border: 1px solid #3a3a50;
}
.tname {
  font-family: Consolas, monospace;
  font-size: 0.9em;
}
.problems {
  margin: 4px 8px;
  padding-left: 18px;
  color: #f59e0b;
  font-size: 0.9em;
}
.note {
  margin: 4px 8px;
  color: #fbbf24;
  font-size: 0.9em;
}
.spacer {
  flex: 1;
}
.muted {
  opacity: 0.6;
}
.small {
  font-size: 0.8em;
}
</style>
