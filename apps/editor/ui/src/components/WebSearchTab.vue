<script setup lang="ts">
// The Assets panel's Web Search (2026-10-21): free art from the approved sites (OpenGameArt:
// CC0, CC-BY, OGA-BY; CC-BY-SA if allowed in the build's asset settings), searched like the
// Art tab generates: a term, a grid of options with their licence and author. Adding one
// downloads it into the project's assets (credited in CREDITS.md through its description)
// and keeps it in your library too.
import { computed, onBeforeUnmount, ref } from 'vue'
import { openContextMenu, type MenuItem } from '../contextMenu'
import { promptDialog } from '../dialogs'
import { onHostMessage, sendToHost } from '../ipc'

interface Result {
  page: string
  title: string
  licence: string
  author: string
  files: string[]
  file: string
  preview: string
  tags?: string[]
  score?: number
  source?: string // opengameart, wikimedia
  why?: string // why it ranks where it does: the words it names, its colours against the project's, its licence
  coherence?: number // 0-1: its colours against the project's style references
  size?: [number, number]
  query?: string // the search that found it
}

const query = ref('')
const searching = ref(false)
// Free-art sites to search by hand (no search the editor can run on them): each opens the
// search for what's typed, in the browser. Downloaded art goes into the Assets panel (drop it
// there), with its licence and author.
const SOURCES = [
  { name: 'OpenGameArt', licence: 'CC0, CC-BY, OGA-BY, GPL: per item', url: 'https://opengameart.org/art-search-advanced?keys={q}' },
  { name: 'Kenney', licence: 'CC0, everything', url: 'https://duckduckgo.com/?q=site%3Akenney.nl%2Fassets+{q}' },
  { name: 'itch.io (free game assets)', licence: 'per pack: check each', url: 'https://itch.io/search?q={q}+free+assets' },
  { name: 'CraftPix (freebies)', licence: 'CraftPix licence: check each', url: 'https://duckduckgo.com/?q=site%3Acraftpix.net%2Ffreebies+{q}' },
  { name: 'Game-icons.net', licence: 'CC BY 3.0 (credit the author)', url: 'https://duckduckgo.com/?q=site%3Agame-icons.net+{q}' },
  { name: 'Wikimedia Commons', licence: 'per file: CC0, CC-BY, public domain…', url: 'https://commons.wikimedia.org/w/index.php?search={q}+filetype%3Abitmap&ns6=1' },
  { name: 'Lospec (palettes)', licence: 'free to use', url: 'https://lospec.com/palette-list?filter={q}' },
]
function openSource(url: string): void {
  sendToHost({ type: 'web:open', url: url.replace('{q}', encodeURIComponent(query.value.trim() || 'pixel art')) })
}
const results = ref<Result[]>([])
const revised = ref<string[]>([]) // better searches the AI wrote when the first found little
const sources = ref<string[]>([])
const error = ref('')
const picked = ref<Result | null>(null)
const added = ref<Record<string, string>>({}) // file -> its project path
const adding = ref<Record<string, boolean>>({})
// While it searches: the seconds so far (a search reads each result's page: ten or twenty seconds).
const elapsed = ref(0)
let timer: ReturnType<typeof setInterval> | null = null
function stopTimer(): void {
  if (timer) clearInterval(timer)
  timer = null
}
onBeforeUnmount(stopTimer)

onHostMessage((message) => {
  if (message.type === 'web:results') {
    searching.value = false
    stopTimer()
    if (message.ok === false) {
      error.value = ((message.errors as string[]) ?? []).join('; ') || 'the search failed'
      results.value = []
      return
    }
    error.value = ''
    results.value = (message.results as Result[]) ?? []
    revised.value = (message.revisedQueries as string[]) ?? []
    sources.value = (message.sources as string[]) ?? []
    picked.value = results.value[0] ?? null
  } else if (message.type === 'web:added') {
    const file = message.file as string
    adding.value[file] = false
    added.value[file] = message.path as string
  }
})

function search(): void {
  const q = query.value.trim()
  if (!q || searching.value) return
  searching.value = true
  error.value = ''
  results.value = []
  elapsed.value = 0
  stopTimer()
  timer = setInterval(() => (elapsed.value += 1), 1000)
  sendToHost({ type: 'web:search', query: q })
}
function add(r: Result, file?: string, folder?: string): void {
  const chosen = { ...r, file: file ?? r.file }
  adding.value[chosen.file] = true
  sendToHost({ type: 'web:add', result: chosen, folder: folder ?? '' })
}
async function addToFolder(r: Result, file?: string): Promise<void> {
  const name = await promptDialog({ title: 'Add to a folder', message: 'A folder under assets/web/ (created if new)', value: 'search' })
  if (!name) return
  const safe = name.toLowerCase().replace(/[^a-z0-9/-]+/g, '-').replace(/^-+|-+$/g, '') || 'search'
  add(r, file, 'assets/web/' + safe)
}
function menu(r: Result, e: MouseEvent): void {
  picked.value = r
  const items: MenuItem[] = [
    { label: 'Add to assets', action: () => add(r) },
    { label: 'Add to assets folder…', action: () => addToFolder(r) },
  ]
  if (r.files.length > 1) {
    items.push({
      label: 'Add another of its files',
      submenu: r.files.map((f) => ({ label: f.split('/').pop() ?? f, action: () => add(r, f) })),
    })
  }
  openContextMenu(e.clientX, e.clientY, items)
}
const name = (f: string): string => decodeURIComponent(f.split('/').pop() ?? f)
const pickedFiles = computed(() => picked.value?.files ?? [])
</script>

<template>
  <div class="web">
    <div class="bar">
      <input v-model="query" placeholder="What to look for, e.g. isometric wooden fence" @keydown.enter="search" />
      <button class="primary" :disabled="!query.trim() || searching" @click="search">{{ searching ? 'Searching…' : 'Search' }}</button>
    </div>
    <p class="dim small">
      OpenGameArt and Wikimedia Commons, licence-checked: CC0, CC-BY and OGA-BY (CC-BY-SA when allowed). Results rank by the words they
      name and by how their colours match your project's style; words like "isometric" rank, they don't filter.
      Added art is credited and kept in your library.
    </p>
    <details class="sources">
      <summary class="dim small">More sources: search them in your browser</summary>
      <div class="source-list">
        <button v-for="s in SOURCES" :key="s.name" class="small" :title="s.licence" @click="openSource(s.url)">{{ s.name }}</button>
      </div>
      <p class="dim tiny">Opens the search for “{{ query.trim() || 'pixel art' }}”. Drop what you download into the Assets panel and give its licence and author.</p>
    </details>
    <p v-if="error" class="error">{{ error }}</p>
    <p v-if="searching" class="dim">Searching OpenGameArt and Wikimedia Commons, reading each result's licence… {{ elapsed }}s (usually 10–40s)</p>
    <p v-if="!searching && results.length" class="dim small">
      {{ results.length }} results from {{ sources.join(' and ') || 'the web' }}<span v-if="revised.length">; the first search found little, so it also searched: {{ revised.join(', ') }}</span>
    </p>
    <div class="body">
      <div class="grid">
        <button
          v-for="r in results"
          :key="r.page"
          class="card"
          :class="{ picked: picked?.page === r.page }"
          :title="r.title + ' · ' + r.licence + ' · ' + r.author"
          @click="picked = r"
          @dblclick="add(r)"
          @contextmenu.prevent="menu(r, $event)"
        >
          <img :src="r.preview" alt="" loading="lazy" referrerpolicy="no-referrer" />
          <span class="name">{{ r.title }}</span>
          <span class="dim tiny">{{ r.licence }} · {{ r.author }}</span>
          <span class="dim tiny">{{ r.source === 'wikimedia' ? 'Wikimedia' : 'OpenGameArt' }}<template v-if="r.coherence !== undefined"> · style {{ Math.round(100 * r.coherence) }}%</template></span>
          <span v-if="added[r.file]" class="badge" title="Added">✓</span>
        </button>
        <p v-if="!searching && !results.length && !error" class="dim empty">Search for art to see options here.</p>
      </div>
      <section v-if="picked" class="details">
        <img :src="picked.file" alt="" referrerpolicy="no-referrer" />
        <strong>{{ picked.title }}</strong>
        <div class="dim">{{ picked.licence }} · by {{ picked.author || 'unknown' }} · {{ picked.source === 'wikimedia' ? 'Wikimedia Commons' : 'OpenGameArt' }}</div>
        <div v-if="picked.why" class="dim tiny">Why: {{ picked.why }}</div>
        <div v-if="picked.size && picked.size[0]" class="dim tiny">{{ picked.size[0] }} × {{ picked.size[1] }} px</div>
        <div v-if="picked.query" class="dim tiny">Found by: “{{ picked.query }}”</div>
        <a class="dim tiny" :href="picked.page" target="_blank" rel="noreferrer">Its page</a>
        <div v-if="picked.tags?.length" class="dim tiny">{{ picked.tags.join(', ') }}</div>
        <div class="row">
          <button class="primary" :disabled="adding[picked.file]" @click="add(picked)">
            {{ added[picked.file] ? 'Added ✓' : adding[picked.file] ? 'Adding…' : 'Add to assets' }}
          </button>
          <button @click="addToFolder(picked)">Add to folder…</button>
        </div>
        <template v-if="pickedFiles.length > 1">
          <div class="dim small">Its files:</div>
          <div class="files">
            <button v-for="f in pickedFiles" :key="f" class="small" :title="f" @click="add(picked, f)">
              {{ added[f] ? '✓ ' : '' }}{{ name(f) }}
            </button>
          </div>
        </template>
        <div class="dim tiny">From {{ picked.page }}</div>
      </section>
    </div>
  </div>
</template>

<style scoped>
.web {
  flex: 1;
  width: 100%;
  display: flex;
  flex-direction: column;
  min-height: 0;
}
.bar {
  display: flex;
  gap: 6px;
  padding: 6px;
}
.bar input {
  flex: 1;
}
.body {
  flex: 1;
  display: flex;
  min-height: 0;
}
.grid {
  flex: 1;
  overflow: auto;
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(110px, 1fr));
  gap: 6px;
  padding: 6px;
  align-content: start;
}
.card {
  position: relative;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
  padding: 4px;
  border: 1px solid #2a2a3a;
  background: #141420;
  cursor: pointer;
}
.card.picked {
  border-color: #3b82f6;
}
.card img {
  width: 96px;
  height: 96px;
  object-fit: contain;
  image-rendering: pixelated;
}
.card .name {
  font-size: 0.8em;
  max-width: 100%;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.badge {
  position: absolute;
  top: 2px;
  right: 4px;
  color: #a6e3a1;
}
.details {
  width: 220px;
  flex-shrink: 0;
  overflow: auto;
  border-left: 1px solid #262637;
  padding: 6px 8px;
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.details img {
  width: 100%;
  max-height: 200px;
  object-fit: contain;
  image-rendering: pixelated;
  background: #11111b;
}
.row,
.files {
  display: flex;
  gap: 4px;
  flex-wrap: wrap;
}
.dim {
  opacity: 0.6;
}
.small {
  font-size: 0.85em;
  margin: 0 6px;
}
.tiny {
  font-size: 0.75em;
  word-break: break-all;
}
.error {
  color: #f38ba8;
  margin: 0 6px;
}
.empty {
  grid-column: 1 / -1;
}
.source-list {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
  margin: 4px 0;
}
</style>
