<script setup lang="ts">
// The World Bible (development_plan.md 3.1): the game's canon — characters,
// factions, places, items, events, lore and the tone guide. Each entry is a
// file in the project (world/<kind>/<id>.md, engine/world); the host reads and
// writes them, this panel lists, searches and edits them.
import { computed, onMounted, reactive, ref, watch } from 'vue'
import { editorState } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'
import { confirmDialog } from '../dialogs'

interface Relation {
  type: string
  target: string
}

interface Entry {
  key: string
  kind: string
  id: string
  name: string
  summary: string
  tags: string[]
  relations: Relation[]
  body: string
}

interface Hit {
  key: string
  name: string
  kind: string
  summary: string
  score: number
}

const kinds = ref<string[]>(['character', 'faction', 'location', 'item', 'event', 'lore', 'style'])
const entries = ref<Entry[]>([])
const problems = ref<{ key: string; message: string }[]>([])
const query = ref('')
const meaningHits = ref<Hit[]>([])
const meaningState = ref<'idle' | 'searching' | 'done' | 'unavailable'>('idle')
const collapsed = reactive<Record<string, boolean>>({})

// The form edits a draft; `originalKey` is the entry it came from ('' = new).
const selectedKey = ref<string | null>(null)
const draft = reactive({
  originalKey: '',
  kind: 'character',
  id: '',
  name: '',
  summary: '',
  tags: '',
  relations: [] as Relation[],
  body: '',
})
const idEdited = ref(false)
// "Check against canon" (development_plan.md 3.3) for the open entry.
const check = ref<{ key: string; state: 'checking' | 'done' | 'failed'; issues: { quote: string; conflictsWith: string; canonSays: string; explanation: string; severity: string }[]; error: string } | null>(null)
const saveError = ref('')
const savedSnapshot = ref('')

const kindLabels: Record<string, string> = {
  character: 'Characters',
  faction: 'Factions',
  location: 'Locations',
  item: 'Items',
  event: 'Events',
  lore: 'Lore',
  style: 'Tone & style',
}

onHostMessage((message) => {
  if (message.type === 'world:list') {
    kinds.value = (message.kinds as string[]) ?? kinds.value
    entries.value = message.entries as Entry[]
    problems.value = message.problems as { key: string; message: string }[]
  } else if (message.type === 'world:saved') {
    saveError.value = (message.error as string | null) ?? ''
    if (!saveError.value) {
      // Re-open from the saved entry once the list arrives.
      savedSnapshot.value = snapshot()
      pendingSelect = message.key as string
    }
  } else if (message.type === 'world:checkResult') {
    const result = message.result as { ok?: boolean; checked?: boolean; issues?: never[]; errors?: string[] }
    check.value = {
      key: message.key as string,
      state: result.ok ? 'done' : 'failed',
      issues: result.issues ?? [],
      error: result.errors?.[0] ?? '',
    }
  } else if (message.type === 'world:searchResults') {
    const result = message.result as { ok: boolean; hits?: Hit[] }
    if (result.ok) {
      meaningHits.value = result.hits ?? []
      meaningState.value = 'done'
    } else {
      meaningHits.value = []
      meaningState.value = 'unavailable'
    }
  }
})

let pendingSelect: string | null = null
watch(entries, () => {
  if (pendingSelect) {
    const key = pendingSelect
    pendingSelect = null
    select(key)
  }
})

onMounted(() => sendToHost({ type: 'world:request' }))

const filtered = computed(() => {
  const q = query.value.trim().toLowerCase()
  if (!q) return entries.value
  return entries.value.filter((e) =>
    [e.name, e.summary, e.tags.join(' '), e.key, e.body].some((text) => text.toLowerCase().includes(q)),
  )
})

const byKind = computed(() =>
  kinds.value.map((kind) => ({ kind, entries: filtered.value.filter((e) => e.kind === kind) })),
)

function problemsFor(key: string): string[] {
  return problems.value.filter((p) => p.key === key).map((p) => p.message)
}

// "Related by meaning" (the AI service's index) — only while AI is on.
let searchTimer: number | undefined
watch(query, (value) => {
  window.clearTimeout(searchTimer)
  meaningHits.value = []
  if (!value.trim() || editorState.ai.state !== 'ready') {
    meaningState.value = editorState.ai.state === 'ready' ? 'idle' : 'unavailable'
    return
  }
  meaningState.value = 'searching'
  searchTimer = window.setTimeout(() => sendToHost({ type: 'world:search', query: value.trim() }), 400)
})

function snapshot(): string {
  return JSON.stringify({ ...draft, relations: draft.relations.map((r) => ({ ...r })) })
}

const dirty = computed(() => selectedKey.value !== null && snapshot() !== savedSnapshot.value)

async function select(key: string): Promise<void> {
  if (dirty.value && !(await confirmDialog({ message: 'Discard your unsaved changes to this entry?', confirmLabel: 'Discard', danger: true }))) {
    return
  }
  const entry = entries.value.find((e) => e.key === key)
  if (!entry) return
  selectedKey.value = key
  Object.assign(draft, {
    originalKey: entry.key,
    kind: entry.kind,
    id: entry.id,
    name: entry.name,
    summary: entry.summary,
    tags: entry.tags.join(', '),
    relations: entry.relations.map((r) => ({ ...r })),
    body: entry.body,
  })
  idEdited.value = true
  saveError.value = ''
  savedSnapshot.value = snapshot()
}

async function startNew(kind: string): Promise<void> {
  if (dirty.value && !(await confirmDialog({ message: 'Discard your unsaved changes to this entry?', confirmLabel: 'Discard', danger: true }))) {
    return
  }
  selectedKey.value = ''
  Object.assign(draft, { originalKey: '', kind, id: '', name: '', summary: '', tags: '', relations: [], body: '' })
  idEdited.value = false
  saveError.value = ''
  savedSnapshot.value = ''
}

// New entries take their id from the name until it's edited by hand.
watch(
  () => draft.name,
  (name) => {
    if (!idEdited.value) {
      draft.id = name
        .toLowerCase()
        .replace(/[^a-z0-9_]+/g, '-')
        .replace(/^-+|-+$/g, '')
    }
  },
)

function save(): void {
  saveError.value = ''
  sendToHost({
    type: 'world:save',
    originalKey: draft.originalKey,
    entry: {
      kind: draft.kind,
      id: draft.id.trim(),
      name: draft.name.trim(),
      summary: draft.summary.trim(),
      tags: draft.tags
        .split(',')
        .map((t) => t.trim())
        .filter(Boolean),
      relations: draft.relations.filter((r) => r.type.trim() && r.target),
      body: draft.body,
    },
  })
}

function revert(): void {
  if (draft.originalKey) {
    savedSnapshot.value = snapshot() // so select() doesn't ask
    select(draft.originalKey)
  } else {
    selectedKey.value = null
  }
}

async function remove(): Promise<void> {
  const entry = entries.value.find((e) => e.key === draft.originalKey)
  if (!entry) return
  const pointing = entries.value.filter((e) => e.relations.some((r) => r.target === entry.key))
  const warning = pointing.length
    ? `\n\n${pointing.length} other ${pointing.length === 1 ? 'entry refers' : 'entries refer'} to it (${pointing
        .map((e) => e.name)
        .join(', ')}); those links will show as problems.`
    : ''
  if (!(await confirmDialog({ message: `Delete "${entry.name}" (${entry.key})? This deletes its file.${warning}`, confirmLabel: 'Delete', danger: true }))) {
    return
  }
  sendToHost({ type: 'world:delete', key: entry.key })
  selectedKey.value = null
}

function checkAgainstCanon(): void {
  if (!draft.originalKey) return
  check.value = { key: draft.originalKey, state: 'checking', issues: [], error: '' }
  sendToHost({ type: 'world:check', key: draft.originalKey })
}

function onKeydown(event: KeyboardEvent): void {
  // Ctrl+S in the form saves the entry, not the scene.
  if (event.ctrlKey && event.key.toLowerCase() === 's') {
    event.preventDefault()
    event.stopPropagation()
    save()
  }
}

function nameOf(key: string): string {
  return entries.value.find((e) => e.key === key)?.name ?? key
}

const targetOptions = computed(() =>
  entries.value.filter((e) => e.key !== draft.originalKey).map((e) => ({ key: e.key, label: `${e.name} (${e.key})` })),
)

// Entries that point at the one being edited.
const backlinks = computed(() =>
  entries.value.flatMap((e) =>
    e.relations.filter((r) => r.target === draft.originalKey).map((r) => ({ from: e, type: r.type })),
  ),
)
</script>

<template>
  <div class="world">
    <aside class="list">
      <input v-model="query" class="search" type="search" placeholder="Search the World Bible" spellcheck="false" />
      <div class="groups">
        <template v-if="query.trim()">
          <div class="group-head">
            <span>Matches</span><span class="count">{{ filtered.length }}</span>
          </div>
          <button
            v-for="entry in filtered"
            :key="entry.key"
            class="row"
            :class="{ active: selectedKey === entry.key }"
            :title="entry.summary"
            @click="select(entry.key)"
          >
            <span class="name">{{ entry.name }}</span>
            <span class="kind-tag">{{ entry.kind }}</span>
          </button>
          <div class="group-head">
            <span>Related by meaning</span>
          </div>
          <p v-if="meaningState === 'searching'" class="muted pad">Searching…</p>
          <p v-else-if="meaningState === 'unavailable'" class="muted pad">Needs AI to be on.</p>
          <button
            v-for="hit in meaningHits"
            v-else
            :key="'m-' + hit.key"
            class="row"
            :class="{ active: selectedKey === hit.key }"
            :title="hit.summary"
            @click="select(hit.key)"
          >
            <span class="name">{{ hit.name }}</span>
            <span class="kind-tag">{{ hit.kind }}</span>
          </button>
        </template>
        <template v-else>
          <div v-for="group in byKind" :key="group.kind" class="group">
            <div class="group-head">
              <button class="toggle" @click="collapsed[group.kind] = !collapsed[group.kind]">
                {{ collapsed[group.kind] ? '▸' : '▾' }} {{ kindLabels[group.kind] ?? group.kind }}
              </button>
              <span class="count">{{ group.entries.length }}</span>
              <button class="add" :title="`New ${group.kind}`" @click="startNew(group.kind)">+</button>
            </div>
            <template v-if="!collapsed[group.kind]">
              <button
                v-for="entry in group.entries"
                :key="entry.key"
                class="row"
                :class="{ active: selectedKey === entry.key }"
                :title="entry.summary"
                @click="select(entry.key)"
              >
                <span class="name">{{ entry.name || entry.id }}</span>
                <span v-if="problemsFor(entry.key).length" class="warn" :title="problemsFor(entry.key).join('\n')">
                  ⚠
                </span>
              </button>
            </template>
          </div>
        </template>
      </div>
    </aside>

    <section v-if="selectedKey === null" class="editor empty">
      <p>
        The World Bible is the game's canon: who's who, places, factions, history and the tone to write in. AI
        features use it to stay consistent.
      </p>
      <p class="muted">
        Pick an entry, or + to add one. Entries are files in <code>world/</code>, so you can also edit them in any
        text editor.
      </p>
      <ul v-if="problems.length" class="problems">
        <li v-for="p in problems" :key="p.key + p.message">
          <button class="link" @click="select(p.key)">{{ p.key }}</button>: {{ p.message }}
        </li>
      </ul>
    </section>

    <section v-else class="editor" @keydown="onKeydown">
      <div class="form-row">
        <label>Kind</label>
        <select v-model="draft.kind">
          <option v-for="kind in kinds" :key="kind" :value="kind">{{ kind }}</option>
        </select>
        <label class="narrow">Id</label>
        <input v-model="draft.id" spellcheck="false" title="File name: world/<kind>/<id>.md" @input="idEdited = true" />
      </div>
      <div class="form-row">
        <label>Name</label>
        <input v-model="draft.name" class="wide" />
      </div>
      <div class="form-row">
        <label>Summary</label>
        <input v-model="draft.summary" class="wide" placeholder="One sentence" />
      </div>
      <div class="form-row">
        <label>Tags</label>
        <input v-model="draft.tags" class="wide" placeholder="comma, separated" />
      </div>
      <div class="form-row top">
        <label>Relations</label>
        <div class="relations">
          <div v-for="(relation, i) in draft.relations" :key="i" class="relation">
            <input v-model="relation.type" placeholder="lives_in" spellcheck="false" />
            <select v-model="relation.target">
              <option v-if="!targetOptions.some((o) => o.key === relation.target)" :value="relation.target">
                {{ relation.target }} (missing)
              </option>
              <option v-for="option in targetOptions" :key="option.key" :value="option.key">{{ option.label }}</option>
            </select>
            <button title="Go to this entry" :disabled="!entries.some((e) => e.key === relation.target)" @click="select(relation.target)">
              →
            </button>
            <button title="Remove this relation" @click="draft.relations.splice(i, 1)">×</button>
          </div>
          <button class="add-relation" @click="draft.relations.push({ type: '', target: targetOptions[0]?.key ?? '' })">
            + Relation
          </button>
          <div v-if="backlinks.length" class="backlinks">
            Referenced by:
            <button v-for="link in backlinks" :key="link.from.key + link.type" class="link" @click="select(link.from.key)">
              {{ link.from.name }} ({{ link.type }})
            </button>
          </div>
        </div>
      </div>
      <textarea v-model="draft.body" class="body-text" placeholder="Description, history, personality, secrets…"></textarea>
      <ul v-if="draft.originalKey && problemsFor(draft.originalKey).length" class="problems">
        <li v-for="message in problemsFor(draft.originalKey)" :key="message">{{ message }}</li>
      </ul>
      <div v-if="saveError" class="problems">{{ saveError }}</div>
      <div v-if="check && check.key === draft.originalKey" class="check" :class="check.state">
        <template v-if="check.state === 'checking'">Checking against the rest of the World Bible…</template>
        <template v-else-if="check.state === 'failed'">Couldn't check: {{ check.error || 'AI is off' }}</template>
        <template v-else-if="check.issues.length === 0">✓ Consistent with the rest of the World Bible</template>
        <ul v-else>
          <li v-for="(issue, i) in check.issues" :key="i">
            <b>{{ issue.severity }}</b>: “{{ issue.quote }}” — {{ issue.conflictsWith }} says “{{ issue.canonSays }}”.
            <span class="muted">{{ issue.explanation }}</span>
          </li>
        </ul>
      </div>
      <div class="actions">
        <button class="primary" :disabled="!draft.name.trim()" title="Save (Ctrl+S)" @click="save">
          {{ draft.originalKey ? 'Save' : 'Create' }}
        </button>
        <button :disabled="!dirty && !!draft.originalKey" @click="revert">{{ draft.originalKey ? 'Revert' : 'Cancel' }}</button>
        <span v-if="dirty" class="muted">Unsaved changes</span>
        <button
          v-if="draft.originalKey"
          :disabled="dirty || editorState.ai.state !== 'ready'"
          :title="dirty ? 'Save first' : 'Ask the AI whether this entry contradicts the rest of the canon'"
          @click="checkAgainstCanon"
        >
          Check against canon
        </button>
        <span class="spacer"></span>
        <span v-if="draft.originalKey" class="muted path">world/{{ draft.kind }}/{{ draft.id }}.md</span>
        <button v-if="draft.originalKey" class="danger" @click="remove">Delete…</button>
      </div>
    </section>
  </div>
</template>

<style scoped>
.world {
  display: flex;
  min-height: 0;
  height: 100%;
}
.list {
  display: flex;
  flex-direction: column;
  width: 240px;
  flex-shrink: 0;
  border-right: 1px solid #2a2a3a;
  min-height: 0;
}
.search {
  margin: 6px;
}
.groups {
  flex: 1;
  overflow-y: auto;
}
.group-head {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 4px 6px;
  font-size: 0.8em;
  text-transform: uppercase;
  letter-spacing: 0.04em;
  opacity: 0.75;
}
.group-head .toggle {
  border: none;
  background: none;
  padding: 0;
  font: inherit;
  text-transform: inherit;
  color: inherit;
  cursor: pointer;
}
.count {
  opacity: 0.6;
}
.add {
  margin-left: auto;
  width: 20px;
  height: 18px;
  padding: 0;
}
.row {
  display: flex;
  align-items: center;
  gap: 6px;
  width: 100%;
  padding: 3px 6px 3px 18px;
  border: none;
  background: none;
  color: inherit;
  font: inherit;
  text-align: left;
  cursor: pointer;
}
.row:hover {
  background: #262637;
}
.row.active {
  background: #313148;
}
.name {
  flex: 1;
  min-width: 0;
  overflow: hidden;
  white-space: nowrap;
  text-overflow: ellipsis;
}
.kind-tag {
  font-size: 0.75em;
  opacity: 0.55;
}
.warn {
  color: #f59e0b;
}
.editor {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 6px;
  padding: 8px 10px;
  overflow-y: auto;
}
.editor.empty {
  max-width: 560px;
}
.form-row {
  display: flex;
  align-items: center;
  gap: 8px;
}
.form-row.top {
  align-items: flex-start;
}
.form-row label {
  width: 70px;
  flex-shrink: 0;
  font-size: 0.85em;
  opacity: 0.75;
}
.form-row label.narrow {
  width: auto;
}
.wide {
  flex: 1;
}
.relations {
  display: flex;
  flex-direction: column;
  gap: 4px;
  flex: 1;
}
.relation {
  display: flex;
  gap: 4px;
}
.relation input {
  width: 140px;
}
.relation select {
  flex: 1;
  min-width: 0;
}
.add-relation {
  align-self: flex-start;
}
.backlinks {
  font-size: 0.85em;
  opacity: 0.8;
}
.body-text {
  flex: 1;
  min-height: 120px;
  resize: vertical;
  font: inherit;
  line-height: 1.45;
}
.actions {
  display: flex;
  align-items: center;
  gap: 6px;
}
.spacer {
  flex: 1;
}
.path {
  font-size: 0.8em;
}
.check {
  padding: 6px 8px;
  border-left: 2px solid #3b82f6;
  background: #11111b;
  font-size: 0.9em;
}
.check.done:not(:has(ul)) {
  border-left-color: #22c55e;
}
.check:has(ul) {
  border-left-color: #f59e0b;
}
.check ul {
  margin: 0;
  padding-left: 16px;
}
.problems {
  margin: 0;
  padding-left: 18px;
  color: #f59e0b;
  font-size: 0.9em;
}
.muted {
  opacity: 0.6;
}
.pad {
  margin: 4px 18px;
}
.link {
  border: none;
  background: none;
  padding: 0 4px 0 0;
  color: #93c5fd;
  font: inherit;
  cursor: pointer;
}
.link:hover {
  text-decoration: underline;
}
input,
select,
textarea {
  border: 1px solid #333;
  border-radius: 0;
  background: #11111b;
  color: #eee;
  padding: 3px 6px;
  font: inherit;
}
button {
  border: 1px solid #333;
  border-radius: 0;
  background: #11111b;
  color: #eee;
  font: inherit;
  padding: 3px 10px;
  cursor: pointer;
}
button:hover:not(:disabled) {
  background: #262637;
}
button:disabled {
  opacity: 0.4;
  cursor: default;
}
button.primary:not(:disabled) {
  background: #3b82f6;
  border-color: #3b82f6;
}
button.danger:hover:not(:disabled) {
  border-color: #ef4444;
}
code {
  font-size: 0.9em;
}
</style>
