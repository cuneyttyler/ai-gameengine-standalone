<script setup lang="ts">
// Edit ▸ Search in project (Ctrl+Shift+F) and Show uses (projectSearch.ts): a sheet on the window's right edge, over
// the panels but not in their way (no backdrop), so results can be opened one after another. Grouped by file, each
// line clickable; a scene's lines name the thing they're in.
import { computed, nextTick, ref, watch } from 'vue'
import { keyLabel } from '../keys'
import { trackOverlay, untrackOverlay } from '../overlay'
import {
  closeSearch,
  KIND_LABELS,
  openResult,
  runSearch,
  SEARCH_GROUPS,
  searchState,
  showUses,
  type SearchFile,
} from '../projectSearch'

const sheet = ref<HTMLElement | null>(null)
const box = ref<HTMLInputElement | null>(null)
const folded = ref<Record<string, boolean>>({})

watch(
  () => searchState.open,
  async (open, was) => {
    await nextTick()
    if (open && sheet.value) trackOverlay(sheet.value)
    if (!open && was && sheet.value) untrackOverlay(sheet.value)
  },
)
watch(
  () => searchState.focusAt,
  async () => {
    await nextTick()
    box.value?.focus()
    box.value?.select()
  },
)
// New results: every file open again.
watch(
  () => searchState.files,
  () => (folded.value = {}),
)

let timer: number | undefined
function typed(): void {
  window.clearTimeout(timer)
  timer = window.setTimeout(runSearch, 250)
}
function toggleGroup(id: string): void {
  const on = searchState.groups.includes(id)
  searchState.groups = on ? searchState.groups.filter((g) => g !== id) : [...searchState.groups, id]
  if (searchState.mode === 'search') runSearch()
}
function close(): void {
  if (sheet.value) untrackOverlay(sheet.value)
  closeSearch()
}

const hitCount = computed(() => searchState.files.reduce((n, f) => n + Math.max(1, f.hits.length), 0))
const title = computed(() =>
  searchState.mode === 'uses' ? `Uses of ${searchState.uses?.label || searchState.uses?.id || searchState.uses?.path || ''}` : 'Search in project',
)
const fileName = (f: SearchFile) => f.path.split('/').pop() ?? f.path
const folder = (f: SearchFile) => f.path.slice(0, Math.max(0, f.path.lastIndexOf('/')))

// The matching words picked out in a line.
function pieces(text: string): { text: string; hit: boolean }[] {
  const words = searchState.mode === 'search' ? (searchState.exact ? [searchState.searched] : searchState.searched.toLowerCase().split(/\s+/)).filter(Boolean) : []
  if (!words.length) return [{ text, hit: false }]
  const escaped = words.map((w) => w.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'))
  // Split on a capturing group: the matches are every other piece.
  const re = new RegExp(`(${escaped.join('|')})`, searchState.exact ? '' : 'i')
  return text
    .split(re)
    .map((t, i) => ({ text: t, hit: i % 2 === 1 }))
    .filter((p) => p.text)
}

// What refers to a result's own file: Show uses from a result.
function usesOf(f: SearchFile): void {
  showUses({ kind: f.kind, path: f.path, id: f.kind === 'package' || f.kind === 'record' ? f.id : undefined, label: f.path })
}
</script>

<template>
  <div v-if="searchState.open" ref="sheet" class="project-search" tabindex="-1" @keydown.esc.stop="close">
    <div class="head">
      <h3>{{ title }}</h3>
      <span class="key" v-if="searchState.mode === 'search'">{{ keyLabel('edit.searchProject') }}</span>
      <button class="x" title="Close (Esc)" @click="close">✕</button>
    </div>
    <div v-if="searchState.mode === 'search'" class="query">
      <input
        ref="box"
        v-model="searchState.query"
        type="search"
        spellcheck="false"
        placeholder="Text in scenes, prefabs, packages, story, scripts, UI; art by name"
        @input="typed"
        @keydown.enter="runSearch"
      />
      <label class="exact" title="Match case, and the whole text as typed (not each word on its own)">
        <input v-model="searchState.exact" type="checkbox" @change="runSearch" /> Aa
      </label>
    </div>
    <div v-else class="query uses">
      <span class="muted">Scenes (and the things in them), prefabs, packages, records, story, scripts and UI that name it.</span>
      <button class="small" @click="searchState.mode = 'search'; runSearch()">Back to search</button>
    </div>
    <div class="filters">
      <button
        v-for="g in SEARCH_GROUPS"
        :key="g.id"
        class="chip"
        :class="{ on: searchState.groups.includes(g.id) }"
        :title="`Only ${g.label.toLowerCase()} (several can be on; none: everything)`"
        @click="toggleGroup(g.id)"
      >
        {{ g.label }}
      </button>
    </div>
    <div class="status">
      <span v-if="searchState.busy">Searching…</span>
      <span v-else-if="searchState.mode === 'search' && !searchState.searched">Type to search the project's files.</span>
      <span v-else-if="!searchState.files.length">{{ searchState.mode === 'uses' ? 'Nothing refers to it.' : 'Nothing matches.' }}</span>
      <span v-else>{{ hitCount }} in {{ searchState.files.length }} {{ searchState.files.length === 1 ? 'file' : 'files' }}<template v-if="searchState.truncated"> (the first; narrow it down)</template></span>
    </div>
    <div class="results">
      <div v-for="f in searchState.files" :key="f.path" class="file">
        <div class="file-row" :title="`${f.path}: click to open`" @click="openResult(f)" @contextmenu.prevent="usesOf(f)">
          <button class="fold" :title="folded[f.path] ? 'Show its lines' : 'Hide its lines'" @click.stop="folded[f.path] = !folded[f.path]">
            {{ f.hits.length ? (folded[f.path] ? '▸' : '▾') : '·' }}
          </button>
          <span class="kind" :class="f.kind">{{ KIND_LABELS[f.kind] ?? f.kind }}</span>
          <span class="name">{{ f.kind === 'record' && f.recordType ? `${f.recordType} ${f.id}` : fileName(f) }}</span>
          <span class="folder">{{ folder(f) }}</span>
          <span class="count">{{ f.hits.length || (f.byName ? 'name' : '') }}</span>
          <button class="uses-btn" title="Show uses: what refers to this file" @click.stop="usesOf(f)">uses</button>
        </div>
        <template v-if="!folded[f.path]">
          <div v-for="h in f.hits" :key="h.line" class="hit" :title="`Line ${h.line}: click to open`" @click="openResult(f, h)">
            <span class="ln">{{ h.line }}</span>
            <span v-if="h.entity" class="entity" :title="`In the scene's thing ${h.entity.name}: opens the scene with it selected`">{{ h.entity.name }}</span>
            <span class="text"><template v-for="(p, i) in pieces(h.text)" :key="i"><mark v-if="p.hit">{{ p.text }}</mark><template v-else>{{ p.text }}</template></template></span>
          </div>
        </template>
      </div>
    </div>
  </div>
</template>

<style scoped>
.project-search {
  position: fixed;
  top: 27px;
  right: 0;
  bottom: 24px;
  width: min(480px, 46vw);
  z-index: 900;
  display: flex;
  flex-direction: column;
  background: #181825;
  border-left: 1px solid #333;
  border-top: 2px solid #3b82f6;
  box-shadow: -6px 0 18px rgba(0, 0, 0, 0.45);
  font-size: 0.85em;
  outline: none;
}
.head {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 10px 4px;
}
h3 {
  margin: 0;
  flex: 1;
  font-size: 1em;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.key {
  opacity: 0.5;
  font-size: 0.9em;
}
.x {
  min-height: 20px;
  padding: 0 6px;
  border: none;
  background: none;
}
.query {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 4px 10px;
}
.query input[type='search'] {
  flex: 1;
  min-width: 0;
}
.query.uses {
  justify-content: space-between;
}
.exact {
  display: flex;
  align-items: center;
  gap: 4px;
  opacity: 0.8;
  cursor: pointer;
}
.filters {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
  padding: 4px 10px 6px;
  border-bottom: 1px solid #2a2a3a;
}
.chip {
  min-height: 20px;
  padding: 1px 8px;
  font-size: 0.9em;
  background: #11111b;
  color: #aaa;
}
.chip.on {
  background: #313148;
  border-color: #3b82f6;
  color: #fff;
}
.status {
  padding: 4px 10px;
  opacity: 0.6;
}
.results {
  flex: 1;
  overflow: auto;
  padding-bottom: 8px;
}
.file-row {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 3px 10px 3px 4px;
  cursor: pointer;
  background: #1d1d2c;
  border-top: 1px solid #23233a;
}
.file-row:hover {
  background: #262637;
}
.fold {
  width: 18px;
  min-height: 18px;
  padding: 0;
  border: none;
  background: none;
  opacity: 0.7;
}
.kind {
  flex-shrink: 0;
  padding: 0 5px;
  font-size: 0.8em;
  text-transform: uppercase;
  letter-spacing: 0.04em;
  border-left: 2px solid #6c7086;
  color: #bbb;
}
.kind.scene { border-color: #3b82f6; }
.kind.prefab { border-color: #a78bfa; }
.kind.package, .kind.record { border-color: #f59e0b; }
.kind.quest, .kind.dialogue, .kind.schedule, .kind.cutscene, .kind.world, .kind.planner { border-color: #34d399; }
.kind.script { border-color: #f472b6; }
.kind.ui, .kind.loc { border-color: #22d3ee; }
.kind.art { border-color: #fb923c; }
.name {
  font-weight: 600;
  white-space: nowrap;
}
.folder {
  flex: 1;
  min-width: 0;
  opacity: 0.45;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.count {
  opacity: 0.5;
}
.uses-btn {
  min-height: 18px;
  padding: 0 6px;
  font-size: 0.85em;
  opacity: 0;
}
.file-row:hover .uses-btn {
  opacity: 1;
}
.hit {
  display: flex;
  gap: 8px;
  padding: 2px 10px 2px 28px;
  cursor: pointer;
  align-items: baseline;
}
.hit:hover {
  background: #262637;
}
.ln {
  flex-shrink: 0;
  width: 34px;
  text-align: right;
  opacity: 0.4;
  font-variant-numeric: tabular-nums;
}
.entity {
  flex-shrink: 0;
  max-width: 120px;
  padding: 0 5px;
  background: #23304a;
  color: #93c5fd;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.text {
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  font-family: Consolas, 'Cascadia Mono', monospace;
  font-size: 0.95em;
  opacity: 0.85;
}
mark {
  background: #5b4a12;
  color: #ffe08a;
}
.muted {
  opacity: 0.6;
}
</style>
