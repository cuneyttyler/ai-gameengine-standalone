<script setup lang="ts">
// The ontology (development_plan.md 9): everything that can be built, and what you add to
// it. The engine's modules are read-only; yours live in your folder and are used by every
// project; the Build panel and the AI work from the whole. Sections on the left, the
// chosen one on the right.
import { computed, nextTick, reactive, ref, watch } from 'vue'
import { editorState } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'
import { trackOverlay, untrackOverlay } from '../overlay'

interface Field {
  name: string
  group: 'quantity' | 'quality'
  type: string
  default?: unknown
  values?: string[]
  vocabulary?: string
  unit?: string
  ask?: boolean
  description?: string
}
interface Module {
  id: string
  name: string
  description: string
  level: string
  classes: string[]
  buildTypes: string[]
  fields: Field[]
  slots: { name: string; roles: string[]; essential: boolean; primary: boolean }[]
  children: { module: string; min: number; max?: number; weight: number; count?: string; when?: string }[]
  parents: string[]
  source: string
  editable: boolean
}
interface Build {
  id: string
  name: string
  root: string
  components: string[]
  unit: string
}
interface Extensions {
  fields: Record<string, { quantity?: Record<string, unknown>; quality?: Record<string, unknown> }>
  children: Record<string, Record<string, unknown>>
  components: Record<string, string[]>
}
interface State {
  modules: Module[]
  builds: Build[]
  classes: { id: string; label: string }[]
  roles: { id: string; label: string }[]
  schemes: { id: string; values: string[] }[]
  fieldTypes: string[]
  levels: string[]
  userDir: string
  extensions: Extensions
  userModules: Record<string, unknown>[]
  error?: string
}

const backdrop = ref<HTMLElement | null>(null)
const state = ref<State | null>(null)
const error = ref('')
const search = ref('')
const selected = ref<{ kind: 'build' | 'module'; id: string } | null>(null)

onHostMessage((message) => {
  if (message.type !== 'ontology:state') return
  state.value = message as unknown as State
  error.value = (message.error as string) ?? ''
  if (!error.value && editing.value && saving.value) {
    // Saved: show what it is now.
    selected.value = { kind: 'module', id: form.id }
    editing.value = false
  }
  saving.value = false
})

watch(
  () => editorState.showOntology,
  async (open) => {
    if (open) sendToHost({ type: 'ontology:get' })
    await nextTick()
    if (open && backdrop.value) {
      trackOverlay(backdrop.value)
      backdrop.value.focus()
    }
  },
)
function close(): void {
  if (backdrop.value) untrackOverlay(backdrop.value)
  editorState.showOntology = false
}

const LEVEL_NAMES: Record<string, string> = {
  site: 'Sites and roots',
  network: 'Networks',
  zone: 'Zones',
  parcel: 'Parcels',
  feature: 'Features',
  dressing: 'Dressing',
  abstract: 'Abstract',
}
const byLevel = computed(() => {
  const s = search.value.trim().toLowerCase()
  const groups: Record<string, Module[]> = {}
  for (const m of state.value?.modules ?? []) {
    if (s && !m.id.includes(s) && !m.name.toLowerCase().includes(s)) continue
    ;(groups[m.level] ??= []).push(m)
  }
  return (state.value?.levels ?? []).concat(['abstract']).filter((l) => groups[l]?.length).map((l) => ({ level: l, modules: groups[l] ?? [] }))
})
const module = computed(() => (selected.value?.kind === 'module' ? state.value?.modules.find((m) => m.id === selected.value?.id) : undefined))
const build = computed(() => (selected.value?.kind === 'build' ? state.value?.builds.find((b) => b.id === selected.value?.id) : undefined))
const nameOf = (id: string): string => state.value?.modules.find((m) => m.id === id)?.name ?? id
function show(v: unknown): string {
  if (v === undefined || v === null) return ''
  return typeof v === 'string' ? v : JSON.stringify(v)
}

// --- Extending a built-in: a field on it, a part allowed in it, a component offered ---
const ext = (): Extensions => JSON.parse(JSON.stringify(state.value?.extensions ?? { fields: {}, children: {}, components: {} }))
function isUserField(moduleId: string, f: Field): boolean {
  return !!state.value?.extensions.fields?.[moduleId]?.[f.group]?.[f.name]
}
const newField = reactive({ name: '', group: 'quality' as 'quantity' | 'quality', type: 'bool', default: '', values: '', vocabulary: '', ask: false })
function fieldJson(f: typeof newField): Record<string, unknown> {
  const out: Record<string, unknown> = { type: f.type }
  if (f.default.trim() !== '') {
    try {
      out.default = JSON.parse(f.default)
    } catch {
      out.default = f.default.trim()
    }
  }
  if (f.values.trim()) out.values = f.values.split(',').map((v) => v.trim()).filter(Boolean)
  if (f.vocabulary) out.vocabulary = f.vocabulary
  if (f.ask) out.ask = true
  return out
}
function addFieldTo(moduleId: string): void {
  if (!newField.name.trim()) return
  const e = ext()
  ;((e.fields[moduleId] ??= {})[newField.group] ??= {})[newField.name.trim()] = fieldJson(newField)
  sendToHost({ type: 'ontology:saveExtensions', extensions: e })
  newField.name = ''
}
function removeFieldFrom(moduleId: string, f: Field): void {
  const e = ext()
  delete e.fields[moduleId]?.[f.group]?.[f.name]
  sendToHost({ type: 'ontology:saveExtensions', extensions: e })
}
const allowChild = reactive({ module: '', min: 0, max: 4, weight: 0.5 })
function allowIn(parent: string): void {
  if (!allowChild.module) return
  const e = ext()
  ;(e.children[parent] ??= {})[allowChild.module] = { min: allowChild.min, max: allowChild.max, weight: allowChild.weight }
  sendToHost({ type: 'ontology:saveExtensions', extensions: e })
  allowChild.module = ''
}
function isUserChild(parent: string, child: string): boolean {
  return !!state.value?.extensions.children?.[parent]?.[child]
}
function removeChild(parent: string, child: string): void {
  const e = ext()
  delete e.children[parent]?.[child]
  sendToHost({ type: 'ontology:saveExtensions', extensions: e })
}
const offer = ref('')
function offerIn(buildId: string): void {
  if (!offer.value) return
  const e = ext()
  const list = (e.components[buildId] ??= [])
  if (!list.includes(offer.value)) list.push(offer.value)
  sendToHost({ type: 'ontology:saveExtensions', extensions: e })
  offer.value = ''
}
function isUserComponent(buildId: string, id: string): boolean {
  return !!state.value?.extensions.components?.[buildId]?.includes(id)
}
function removeComponent(buildId: string, id: string): void {
  const e = ext()
  e.components[buildId] = (e.components[buildId] ?? []).filter((m) => m !== id)
  sendToHost({ type: 'ontology:saveExtensions', extensions: e })
}

// --- Your own modules: made and edited with a form ---
interface FormField {
  name: string
  group: 'quantity' | 'quality'
  type: string
  default: string
  values: string
  vocabulary: string
  ask: boolean
}
const editing = ref(false)
const saving = ref(false)
const form = reactive({
  id: '',
  name: '',
  level: 'parcel',
  description: '',
  classes: [] as string[],
  parents: [] as { module: string; min: number; max: number; weight: number }[],
  components: [] as string[],
  fields: [] as FormField[],
  slots: [] as { name: string; role: string; essential: boolean; primary: boolean }[],
  isNew: true,
})
function newModule(): void {
  Object.assign(form, { id: '', name: '', level: 'parcel', description: '', classes: [], parents: [], components: [], fields: [], slots: [], isNew: true })
  form.slots.push({ name: 'building', role: 'house', essential: true, primary: true })
  editing.value = true
  error.value = ''
}
function editModule(id: string): void {
  const raw = (state.value?.userModules ?? []).find((m) => m.id === id) as Record<string, any> | undefined
  if (!raw) return
  const ex = raw.extends ?? {}
  form.id = raw.id
  form.name = raw.name ?? ''
  form.level = raw.level ?? 'parcel'
  form.description = raw.description ?? ''
  form.classes = [...(raw.classes ?? [])]
  form.parents = Object.entries(ex.parents ?? {}).map(([module, r]: [string, any]) => ({ module, min: r?.min ?? 0, max: r?.max ?? 4, weight: r?.weight ?? 0.5 }))
  form.components = [...(ex.components ?? [])]
  form.fields = []
  for (const group of ['quantity', 'quality'] as const) {
    for (const [name, f] of Object.entries((raw[group] ?? {}) as Record<string, any>)) {
      form.fields.push({
        name,
        group,
        type: f.type ?? 'bool',
        default: f.default === undefined ? '' : typeof f.default === 'string' ? f.default : JSON.stringify(f.default),
        values: (f.values ?? []).join(', '),
        vocabulary: f.vocabulary ?? '',
        ask: !!f.ask,
      })
    }
  }
  form.slots = Object.entries((raw.slots ?? {}) as Record<string, any>).map(([name, s]) => ({
    name,
    role: s.role ?? '',
    essential: s.essential !== false,
    primary: !!s.primary,
  }))
  form.isNew = false
  editing.value = true
  error.value = ''
}
function saveModule(): void {
  const quantity: Record<string, unknown> = {}
  const quality: Record<string, unknown> = {}
  for (const f of form.fields) {
    if (!f.name.trim()) continue
    ;(f.group === 'quantity' ? quantity : quality)[f.name.trim()] = fieldJson({ ...f, name: f.name })
  }
  const slots: Record<string, unknown> = {}
  for (const s of form.slots) {
    if (s.name.trim() && s.role) slots[s.name.trim()] = { role: s.role, essential: s.essential, primary: s.primary }
  }
  const parents: Record<string, unknown> = {}
  for (const p of form.parents) {
    if (p.module) parents[p.module] = { min: p.min, max: p.max, weight: p.weight }
  }
  const module = {
    id: form.id.trim(),
    name: form.name.trim() || form.id.trim(),
    level: form.level,
    description: form.description,
    classes: form.classes,
    quantity,
    quality,
    slots,
    children: {},
    extends: { parents, components: form.components },
  }
  saving.value = true
  sendToHost({ type: 'ontology:saveModule', module })
}
const deleteAsk = ref('')
function deleteModule(id: string): void {
  deleteAsk.value = ''
  selected.value = null
  sendToHost({ type: 'ontology:deleteModule', id })
}
const parentChoices = computed(() => (state.value?.modules ?? []).filter((m) => ['site', 'zone', 'parcel', 'feature'].includes(m.level)))
</script>

<template>
  <div
    v-if="editorState.showOntology"
    ref="backdrop"
    class="backdrop ed-form"
    tabindex="-1"
    @mousedown.self="close"
    @keydown.esc.stop="close"
  >
    <div class="sheet">
      <header>
        <h3>Ontology</h3>
        <span class="muted">What can be built. The engine's is read-only; yours is used by every project.</span>
        <div class="tools">
          <button class="primary" @click="newModule()">New module</button>
          <button title="Add modules and extensions from a file" @click="sendToHost({ type: 'ontology:import' })">Import…</button>
          <button title="Save what you added as one file" @click="sendToHost({ type: 'ontology:export' })">Export…</button>
        </div>
        <button class="close" title="Close (Esc)" @click="close">×</button>
      </header>
      <p v-if="error" class="error banner">{{ error }}</p>
      <div v-if="state" class="panes">
        <nav>
          <input v-model="search" class="search" placeholder="Find a module" />
          <h5>Build types</h5>
          <button
            v-for="b in state.builds"
            :key="b.id"
            :class="{ active: selected?.kind === 'build' && selected.id === b.id }"
            @click="(selected = { kind: 'build', id: b.id }), (editing = false)"
          >
            {{ b.name }}
          </button>
          <template v-for="g in byLevel" :key="g.level">
            <h5>{{ LEVEL_NAMES[g.level] ?? g.level }}</h5>
            <button
              v-for="m in g.modules"
              :key="m.id"
              :class="{ active: selected?.kind === 'module' && selected.id === m.id }"
              :title="m.description"
              @click="(selected = { kind: 'module', id: m.id }), (editing = false)"
            >
              {{ m.name }}<span v-if="m.source === 'user'" class="mine">yours</span><span v-else-if="m.source !== 'engine'" class="mine">{{ m.source }}</span>
            </button>
          </template>
        </nav>

        <main>
          <!-- A module of yours, made or edited. -->
          <template v-if="editing">
            <h4>{{ form.isNew ? 'New module' : 'Edit ' + form.id }}</h4>
            <div class="grid">
              <label>Id</label>
              <input v-model="form.id" :disabled="!form.isNew" placeholder="watermill-plot (lowercase, dashes)" />
              <label>Name</label>
              <input v-model="form.name" placeholder="Watermill" />
              <label>Level</label>
              <select v-model="form.level">
                <option v-for="l in state.levels" :key="l" :value="l">{{ l }}</option>
              </select>
              <label>Description</label>
              <textarea v-model="form.description" rows="2" placeholder="What it is: the AI reads this" />
              <label>Classes</label>
              <div class="chips">
                <button
                  v-for="c in state.classes"
                  :key="c.id"
                  :class="{ on: form.classes.includes(c.id) }"
                  @click="form.classes.includes(c.id) ? form.classes.splice(form.classes.indexOf(c.id), 1) : form.classes.push(c.id)"
                >
                  {{ c.label }}
                </button>
              </div>
              <label>Goes in</label>
              <div>
                <div v-for="(p, i) in form.parents" :key="i" class="row">
                  <select v-model="p.module">
                    <option value="">a module…</option>
                    <option v-for="m in parentChoices" :key="m.id" :value="m.id">{{ m.name }} ({{ m.level }})</option>
                  </select>
                  <span class="muted">min</span><input v-model.number="p.min" class="num" type="number" min="0" />
                  <span class="muted">max</span><input v-model.number="p.max" class="num" type="number" min="0" />
                  <span class="muted">how typical</span><input v-model.number="p.weight" class="num" type="number" min="0" max="1" step="0.1" />
                  <button @click="form.parents.splice(i, 1)">×</button>
                </div>
                <button @click="form.parents.push({ module: '', min: 0, max: 4, weight: 0.5 })">+ where it can go</button>
              </div>
              <label>Offered by</label>
              <div class="chips">
                <button
                  v-for="b in state.builds"
                  :key="b.id"
                  :class="{ on: form.components.includes(b.id) }"
                  :title="'A component the ' + b.name + ' build offers'"
                  @click="form.components.includes(b.id) ? form.components.splice(form.components.indexOf(b.id), 1) : form.components.push(b.id)"
                >
                  {{ b.name }}
                </button>
              </div>
            </div>
            <h5>Fields</h5>
            <table>
              <thead><tr><th>Name</th><th>Kind</th><th>Type</th><th>Default</th><th>Values or vocabulary</th><th>Asked</th><th></th></tr></thead>
              <tbody>
                <tr v-for="(f, i) in form.fields" :key="i">
                  <td><input v-model="f.name" placeholder="wheel" /></td>
                  <td>
                    <select v-model="f.group">
                      <option value="quantity">quantity</option>
                      <option value="quality">quality</option>
                    </select>
                  </td>
                  <td>
                    <select v-model="f.type">
                      <option v-for="t in state.fieldTypes" :key="t" :value="t">{{ t }}</option>
                    </select>
                  </td>
                  <td><input v-model="f.default" placeholder='e.g. 3, [2, 5], "oak", {"w": [6, 8], "h": [6, 8]}' /></td>
                  <td>
                    <input v-if="f.type === 'enum' || f.type === 'enumSet'" v-model="f.values" placeholder="a, b, c (or a vocabulary)" />
                    <select v-if="['enum', 'enumSet', 'mix'].includes(f.type)" v-model="f.vocabulary">
                      <option value="">no vocabulary</option>
                      <option v-for="s in state.schemes" :key="s.id" :value="s.id">{{ s.id }}</option>
                    </select>
                  </td>
                  <td><input v-model="f.ask" type="checkbox" title="Asked in ① Confirm" /></td>
                  <td><button @click="form.fields.splice(i, 1)">×</button></td>
                </tr>
              </tbody>
            </table>
            <button @click="form.fields.push({ name: '', group: 'quality', type: 'enum', default: '', values: '', vocabulary: '', ask: false })">+ field</button>
            <h5>Slots <span class="muted">(what art it shows)</span></h5>
            <table>
              <thead><tr><th>Name</th><th>Role</th><th>Needed</th><th>Main</th><th></th></tr></thead>
              <tbody>
                <tr v-for="(s, i) in form.slots" :key="i">
                  <td><input v-model="s.name" /></td>
                  <td>
                    <select v-model="s.role">
                      <option v-for="r in state.roles" :key="r.id" :value="r.id">{{ r.label }}</option>
                    </select>
                  </td>
                  <td><input v-model="s.essential" type="checkbox" /></td>
                  <td><input v-model="s.primary" type="checkbox" /></td>
                  <td><button @click="form.slots.splice(i, 1)">×</button></td>
                </tr>
              </tbody>
            </table>
            <button @click="form.slots.push({ name: '', role: 'house', essential: false, primary: false })">+ slot</button>
            <div class="actions">
              <span class="muted">Checked against the rest before it's kept, in {{ state.userDir }}</span>
              <button @click="editing = false">Cancel</button>
              <button class="primary" :disabled="!form.id.trim() || saving" @click="saveModule()">{{ saving ? 'Saving…' : 'Save' }}</button>
            </div>
          </template>

          <!-- A build type: what it offers. -->
          <template v-else-if="build">
            <h4>{{ build.name }} <span class="muted">· {{ build.id }}, counted in {{ build.unit }}</span></h4>
            <p class="muted">Its root: <a href="#" @click.prevent="selected = { kind: 'module', id: build.root }">{{ nameOf(build.root) }}</a></p>
            <h5>Components it offers</h5>
            <div class="chips">
              <span v-for="c in build.components" :key="c" class="chip" :class="{ mine: isUserComponent(build.id, c) }">
                <a href="#" @click.prevent="selected = { kind: 'module', id: c }">{{ nameOf(c) }}</a>
                <button v-if="isUserComponent(build.id, c)" title="Take it out" @click="removeComponent(build.id, c)">×</button>
              </span>
            </div>
            <div class="row add">
              <select v-model="offer">
                <option value="">offer another module…</option>
                <option v-for="m in state.modules.filter((x) => !build?.components.includes(x.id) && x.level !== 'site')" :key="m.id" :value="m.id">
                  {{ m.name }} ({{ m.level }})
                </option>
              </select>
              <button :disabled="!offer" @click="offerIn(build.id)">Offer it</button>
            </div>
          </template>

          <!-- A module: what it is; yours editable, the engine's extendable. -->
          <template v-else-if="module">
            <h4>
              {{ module.name }}
              <span class="muted">· {{ module.id }} · {{ module.level }} · {{ module.source === 'user' ? 'yours' : module.source }}</span>
            </h4>
            <p>{{ module.description }}</p>
            <div v-if="module.editable" class="row">
              <button class="primary" @click="editModule(module.id)">Edit</button>
              <button @click="deleteAsk = module.id">Delete</button>
              <span v-if="deleteAsk === module.id" class="ask">
                Delete {{ module.name }}? Builds that use it keep what they built.
                <button @click="deleteAsk = ''">Keep it</button>
                <button class="primary" @click="deleteModule(module.id)">Delete it</button>
              </span>
            </div>
            <p v-else class="muted">Built in: read-only. You can add fields to it and allow more parts in it (below).</p>
            <p v-if="module.classes.length" class="muted">Classes: {{ module.classes.join(', ') }}</p>
            <p v-if="module.parents.length" class="muted">
              Goes in:
              <template v-for="(p, i) in module.parents" :key="p"><span v-if="i">, </span><a href="#" @click.prevent="selected = { kind: 'module', id: p }">{{ nameOf(p) }}</a></template>
            </p>
            <h5>Fields</h5>
            <table v-if="module.fields.length">
              <thead><tr><th>Name</th><th>Kind</th><th>Type</th><th>Default</th><th>Values</th><th></th></tr></thead>
              <tbody>
                <tr v-for="f in module.fields" :key="f.group + f.name" :class="{ mine: isUserField(module.id, f) }">
                  <td>{{ f.name }}<span v-if="f.ask" class="muted"> · asked</span></td>
                  <td class="muted">{{ f.group }}</td>
                  <td>{{ f.type }}</td>
                  <td class="muted">{{ show(f.default) }}</td>
                  <td class="muted">{{ f.values?.join(', ') ?? f.vocabulary ?? '' }}</td>
                  <td><button v-if="isUserField(module.id, f)" title="Remove the field you added" @click="removeFieldFrom(module.id, f)">×</button></td>
                </tr>
              </tbody>
            </table>
            <p v-else class="muted">None.</p>
            <div v-if="!module.editable" class="row add">
              <input v-model="newField.name" placeholder="a new field's name" />
              <select v-model="newField.group">
                <option value="quantity">quantity</option>
                <option value="quality">quality</option>
              </select>
              <select v-model="newField.type">
                <option v-for="t in state.fieldTypes" :key="t" :value="t">{{ t }}</option>
              </select>
              <input v-model="newField.default" class="short" placeholder="default" />
              <input v-if="newField.type === 'enum'" v-model="newField.values" placeholder="values: a, b" />
              <button :disabled="!newField.name.trim()" @click="addFieldTo(module.id)">Add the field</button>
            </div>
            <h5>Slots</h5>
            <p v-if="!module.slots.length" class="muted">None.</p>
            <div v-for="s in module.slots" :key="s.name" class="muted">
              {{ s.name }}: {{ s.roles.join(' or ') }}{{ s.primary ? ' · main' : '' }}{{ s.essential ? '' : ' · optional' }}
            </div>
            <h5>Parts it may hold</h5>
            <table v-if="module.children.length">
              <thead><tr><th>Part</th><th>Min</th><th>Max</th><th>How typical</th><th></th></tr></thead>
              <tbody>
                <tr v-for="c in module.children" :key="c.module" :class="{ mine: isUserChild(module.id, c.module) }">
                  <td>
                    <a v-if="!c.module.startsWith('class:')" href="#" @click.prevent="selected = { kind: 'module', id: c.module }">{{ nameOf(c.module) }}</a>
                    <span v-else>any {{ c.module.slice(6) }}</span>
                    <span v-if="c.count" class="muted"> · as many as its {{ c.count }}</span>
                  </td>
                  <td>{{ c.min }}</td>
                  <td>{{ c.max ?? 'any' }}</td>
                  <td>{{ c.weight }}</td>
                  <td><button v-if="isUserChild(module.id, c.module)" @click="removeChild(module.id, c.module)">×</button></td>
                </tr>
              </tbody>
            </table>
            <p v-else class="muted">None.</p>
            <div v-if="!module.editable && ['site', 'zone', 'parcel', 'feature'].includes(module.level)" class="row add">
              <select v-model="allowChild.module">
                <option value="">allow another part in it…</option>
                <option v-for="m in state.modules.filter((x) => x.id !== module?.id && !module?.children.some((c) => c.module === x.id))" :key="m.id" :value="m.id">
                  {{ m.name }} ({{ m.level }})
                </option>
              </select>
              <span class="muted">min</span><input v-model.number="allowChild.min" class="num" type="number" min="0" />
              <span class="muted">max</span><input v-model.number="allowChild.max" class="num" type="number" min="0" />
              <button :disabled="!allowChild.module" @click="allowIn(module.id)">Allow it</button>
            </div>
          </template>
          <p v-else class="muted">Pick a build type or a module on the left, or make a new module.</p>
        </main>
      </div>
      <p v-else class="muted pad">Loading…</p>
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
  width: min(1100px, 96vw);
  height: min(760px, 92vh);
  display: flex;
  flex-direction: column;
  background: #181825;
  border: 1px solid #333;
  border-top: 2px solid #3b82f6;
}
header {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 10px 14px;
  border-bottom: 1px solid #2a2a3a;
}
header h3 {
  margin: 0;
}
header .tools {
  margin-left: auto;
  display: flex;
  gap: 6px;
}
header .close {
  width: 26px;
  height: 26px;
  border: 1px solid transparent;
  background: transparent;
  font-size: 16px;
}
.banner {
  margin: 0;
  padding: 6px 14px;
  border-bottom: 1px solid #2a2a3a;
}
.panes {
  flex: 1;
  display: flex;
  min-height: 0;
}
nav {
  width: 240px;
  flex-shrink: 0;
  overflow: auto;
  border-right: 1px solid #2a2a3a;
  background: #141420;
  padding: 6px 0;
}
nav .search {
  margin: 4px 10px 6px;
  width: calc(100% - 20px);
  box-sizing: border-box;
}
nav h5 {
  margin: 10px 12px 2px;
}
nav button {
  display: flex;
  justify-content: space-between;
  width: 100%;
  padding: 4px 14px;
  border: none;
  border-left: 2px solid transparent;
  background: transparent;
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
.mine {
  font-size: 0.75em;
  color: #a6e3a1;
  margin-left: 6px;
}
tr.mine td:first-child,
.chip.mine {
  color: #a6e3a1;
}
main {
  flex: 1;
  overflow: auto;
  padding: 12px 18px;
}
h4 {
  margin: 0 0 6px;
}
h5 {
  margin: 14px 0 4px;
  font-size: 0.78em;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  opacity: 0.6;
}
table {
  border-collapse: collapse;
  width: 100%;
  margin-bottom: 6px;
}
th {
  text-align: left;
  font-weight: normal;
  opacity: 0.6;
  border-bottom: 1px solid #333;
  padding: 3px 6px;
}
td {
  border-bottom: 1px solid #262637;
  padding: 3px 6px;
  vertical-align: middle;
}
td input:not([type='checkbox']),
td select {
  width: 100%;
  box-sizing: border-box;
}
.grid {
  display: grid;
  grid-template-columns: 110px 1fr;
  gap: 6px 10px;
  align-items: start;
}
.grid textarea {
  resize: vertical;
}
.row {
  display: flex;
  align-items: center;
  gap: 6px;
  flex-wrap: wrap;
  margin: 3px 0;
}
.row.add {
  margin-top: 6px;
}
.num {
  width: 56px;
}
.short {
  width: 90px;
}
.chips {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}
.chips button.on {
  background: #3b82f6;
  border-color: #3b82f6;
}
.chip {
  border: 1px solid #333;
  padding: 2px 6px;
  display: inline-flex;
  gap: 4px;
  align-items: center;
}
.chip button {
  padding: 0 4px;
}
a {
  color: #89b4fa;
}
.ask {
  margin-left: 8px;
}
.actions {
  display: flex;
  gap: 8px;
  justify-content: flex-end;
  align-items: center;
  margin-top: 14px;
}
.actions .muted {
  margin-right: auto;
}
.muted {
  opacity: 0.6;
  font-size: 0.9em;
}
.error {
  color: #f38ba8;
}
.pad {
  padding: 14px;
}
</style>
