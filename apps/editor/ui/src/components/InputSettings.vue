<script setup lang="ts">
// Project Settings ▸ Input (development_plan.md 12.3, docs/systems/actions.md): the game's actions (move, attack,
// interact… and the project's own: farm, parry, kick) and what triggers each: keys, mouse buttons and gamepad buttons
// and sticks. An action of the project's can be added and removed, and can play a clip on the player or emit an event
// when pressed; things react to it with an Action component. Saved to the project's input.json.
import { onMounted, onUnmounted, ref, watch } from 'vue'
import { onHostMessage, sendToHost } from '../ipc'
import { promptDialog } from '../dialogs'
import { engineKey } from '../inputActions'

interface KeyBinding {
  key: string
  scale?: number
}
interface AxisBinding {
  axis: string
  scale: number
  deadzone: number
}
interface Action {
  name: string
  description: string
  context: string
  axis?: boolean
  keys: (string | KeyBinding)[]
  axes?: AxisBinding[]
  plays?: string // a clip the player plays when it's pressed (parry, attack_kick)
  event?: string // emitted when it's pressed, for scripts
  effect?: string // an effect played on the player when it's pressed
}

const actions = ref<Action[]>([])
const builtIn = ref<string[]>([]) // the engine's own actions: rebound, never removed
const newAction = ref('')
const keyNames = ref<string[]>([])
const axisNames = ref<string[]>([])
const capturing = ref<{ action: string; scale: number } | null>(null)
const error = ref('')
const saved = ref(false)
// What plays / event / effect can be (input:choices): the characters' clips, the project's events and effects.
const choices = ref<{ clips: string[]; events: string[]; effects: string[] }>({ clips: [], events: [], effects: [] })

onHostMessage((message) => {
  if (message.type === 'input:data') {
    actions.value = (message.map as { actions: Action[] }).actions
    keyNames.value = message.keys as string[]
    axisNames.value = message.axes as string[]
    builtIn.value = (message.builtIn as string[]) ?? []
    error.value = ((message.errors as string[]) ?? []).join('; ')
  } else if (message.type === 'input:choices') {
    choices.value = { clips: (message.clips as string[]) ?? [], events: (message.events as string[]) ?? [], effects: (message.effects as string[]) ?? [] }
  } else if (message.type === 'input:saved') {
    error.value = (message.error as string | null) ?? ''
    saved.value = !error.value
  }
})
onMounted(() => {
  sendToHost({ type: 'input:request' })
  sendToHost({ type: 'input:choices' })
})
// Capturing a key: read on the window (the + Key button clicked is gone, the focus with it), before anything else.
const onWindowKey = (event: KeyboardEvent) => onCapture(event)
watch(capturing, (now) => {
  if (now) window.addEventListener('keydown', onWindowKey, true)
  else window.removeEventListener('keydown', onWindowKey, true)
})
onUnmounted(() => window.removeEventListener('keydown', onWindowKey, true))
// An event of its own, named here (the event picker's New event…).
async function newEvent(action: Action, select: HTMLSelectElement): Promise<void> {
  const name = await promptDialog({ title: 'New event', message: `The event ${action.name} sends when it's pressed (scripts, triggers and quests react to it).`, placeholder: 'cooked', confirmLabel: 'Use' })
  if (name && name.trim()) setField(action, 'event', name.trim())
  select.value = action.event ?? ''
}

const keyOf = (b: string | KeyBinding) => (typeof b === 'string' ? b : b.key)
const scaleOf = (b: string | KeyBinding) => (typeof b === 'string' ? 1 : b.scale ?? 1)

function save(): void {
  saved.value = false
  sendToHost({ type: 'input:save', map: { schema: 'aige.input/1', actions: actions.value } })
}

function removeKey(action: Action, index: number): void {
  action.keys.splice(index, 1)
  save()
}

function addKey(action: Action, key: string, scale = 1): void {
  if (!key) return
  if (action.keys.some((b) => keyOf(b) === key && scaleOf(b) === scale)) return
  action.keys.push(action.axis ? { key, scale } : key)
  save()
}

function onCapture(event: KeyboardEvent): void {
  if (!capturing.value) return
  event.preventDefault()
  event.stopPropagation()
  if (event.code === 'Escape') {
    capturing.value = null
    return
  }
  const key = engineKey(event)
  const action = actions.value.find((a) => a.name === capturing.value?.action)
  if (key && action) addKey(action, key, capturing.value.scale)
  capturing.value = null
}

function addAction(): void {
  const name = newAction.value.trim().toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_|_$/g, '')
  newAction.value = ''
  if (!name || actions.value.some((a) => a.name === name)) return
  actions.value.push({ name, description: '', context: 'gameplay', keys: [] })
  save()
}
function removeAction(action: Action): void {
  actions.value = actions.value.filter((a) => a !== action)
  save()
}
function setField(action: Action, field: 'description' | 'plays' | 'event' | 'effect' | 'context', value: string): void {
  if (field === 'plays' || field === 'event' || field === 'effect') {
    if (value.trim()) action[field] = value.trim()
    else delete action[field]
  } else {
    action[field] = value
  }
  save()
}

function resetDefaults(): void {
  sendToHost({ type: 'input:reset' })
}
</script>

<template>
  <div class="input-settings">
    <p class="muted">
      What the player presses for each action. Click <b>+ Key</b> and press a key, or pick a mouse or gamepad button.
      Saved to <code>input.json</code>.
    </p>
    <div class="add">
      <input v-model="newAction" placeholder="a new action: farm, parry, kick…" spellcheck="false" @keydown.enter="addAction" />
      <button :disabled="!newAction.trim()" @click="addAction">Add action</button>
      <span class="muted">Things react to it with an Action component (Inspector), or a part's input action.</span>
    </div>
    <table>
      <thead>
        <tr><th>Action</th><th>When</th><th>Bindings</th><th title="A clip the player plays when it's pressed, and an event for scripts">Plays / event</th><th></th></tr>
      </thead>
      <tbody>
        <tr v-for="action in actions" :key="action.name">
          <td :title="action.description">
            <b>{{ action.name }}</b><span v-if="action.axis" class="muted"> (axis)</span>
            <input
              v-if="!builtIn.includes(action.name)"
              class="desc"
              :value="action.description"
              placeholder="what it does"
              spellcheck="false"
              @change="setField(action, 'description', ($event.target as HTMLInputElement).value)"
            />
          </td>
          <td class="muted">
            <select v-if="!builtIn.includes(action.name)" class="small" :value="action.context" @change="setField(action, 'context', ($event.target as HTMLSelectElement).value)">
              <option v-for="c in ['gameplay', 'dialogue', 'menu', 'any']" :key="c" :value="c">{{ c }}</option>
            </select>
            <template v-else>{{ action.context }}</template>
          </td>
          <td class="bindings">
            <span v-for="(binding, i) in action.keys" :key="i" class="chip">
              <span v-if="action.axis" class="muted">{{ scaleOf(binding) > 0 ? '+' : '−' }}</span>{{ keyOf(binding) }}
              <button title="Remove" @click="removeKey(action, i)">×</button>
            </span>
            <span v-for="(axis, i) in action.axes ?? []" :key="'a' + i" class="chip stick">{{ axis.axis }}</span>
            <template v-if="capturing?.action === action.name">
              <span class="capture">Press a key… (Esc to cancel)</span>
            </template>
            <template v-else>
              <button v-if="!action.axis" class="small" @click="capturing = { action: action.name, scale: 1 }">+ Key</button>
              <template v-else>
                <button class="small" @click="capturing = { action: action.name, scale: -1 }">+ Key (−)</button>
                <button class="small" @click="capturing = { action: action.name, scale: 1 }">+ Key (+)</button>
              </template>
              <select class="small" :value="''" @change="addKey(action, ($event.target as HTMLSelectElement).value); ($event.target as HTMLSelectElement).value = ''">
                <option value="">+ Button…</option>
                <option v-for="name in keyNames.filter((k) => k.startsWith('Pad') || k.startsWith('Mouse'))" :key="name" :value="name">
                  {{ name }}
                </option>
              </select>
            </template>
          </td>
          <td>
            <template v-if="!action.axis">
              <select class="small-in" :value="action.plays ?? ''" title="A clip the player plays when it's pressed (the characters' clips)" @change="setField(action, 'plays', ($event.target as HTMLSelectElement).value)">
                <option value="">no clip</option>
                <option v-if="action.plays && !choices.clips.includes(action.plays)" :value="action.plays">{{ action.plays }} (missing)</option>
                <option v-for="c in choices.clips" :key="c" :value="c">{{ c }}</option>
              </select>
              <select class="small-in" :value="action.event ?? ''" title="An event sent when it's pressed: scripts, triggers and quests react to it"
                      @change="($event.target as HTMLSelectElement).value === '+new' ? newEvent(action, $event.target as HTMLSelectElement) : setField(action, 'event', ($event.target as HTMLSelectElement).value)">
                <option value="">no event</option>
                <option v-if="action.event && !choices.events.includes(action.event)" :value="action.event">{{ action.event }}</option>
                <option v-for="e in choices.events" :key="e" :value="e">{{ e }}</option>
                <option value="+new">New event…</option>
              </select>
              <select class="small-in" :value="action.effect ?? ''" title="An effect played on the player when it's pressed (Project ▸ Effects)" @change="setField(action, 'effect', ($event.target as HTMLSelectElement).value)">
                <option value="">no effect</option>
                <option v-if="action.effect && !choices.effects.includes(action.effect)" :value="action.effect">{{ action.effect }} (missing)</option>
                <option v-for="f in choices.effects" :key="f" :value="f">{{ f }}</option>
              </select>
            </template>
          </td>
          <td>
            <button v-if="!builtIn.includes(action.name)" class="small" title="Remove this action" @click="removeAction(action)">Remove</button>
          </td>
        </tr>
      </tbody>
    </table>
    <div class="actions">
      <button @click="resetDefaults">Reset to defaults</button>
      <span v-if="saved" class="muted">Saved</span>
      <span v-if="error" class="error">{{ error }}</span>
    </div>
  </div>
</template>

<style scoped>
.desc {
  display: block;
  width: 100%;
  margin-top: 2px;
  font-size: 0.85em;
}
.small-in {
  width: 80px;
  font-size: 0.85em;
}
.add {
  display: flex;
  gap: 6px;
  align-items: center;
  margin-top: 8px;
}
.input-settings {
  display: flex;
  flex-direction: column;
  gap: 6px;
}
table {
  border-collapse: collapse;
  width: 100%;
}
th {
  text-align: left;
  font-weight: normal;
  opacity: 0.6;
  font-size: 0.85em;
  padding: 2px 6px;
}
td {
  padding: 3px 6px;
  border-top: 1px solid #2a2a3a;
  vertical-align: top;
}
.bindings {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
  align-items: center;
}
.chip {
  display: inline-flex;
  align-items: center;
  gap: 3px;
  padding: 1px 4px;
  border: 1px solid #3a3a52;
  background: #22222f;
}
.chip button {
  border: none;
  background: none;
  padding: 0 2px;
  color: inherit;
  cursor: pointer;
}
.chip.stick {
  opacity: 0.7;
}
.small {
  height: 22px;
  font-size: 0.85em;
}
.capture {
  color: #60a5fa;
}
.actions {
  display: flex;
  gap: 8px;
  align-items: center;
}
.muted {
  opacity: 0.6;
}
.error {
  color: #f87171;
}
</style>
