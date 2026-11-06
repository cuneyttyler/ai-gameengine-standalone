<script setup>
// The documentation (/docs, /docs/<area>/<page>#<section>): the editor's knowledge base (data/help) as pages. The
// sidebar lists areas, their groups and pages, and the open page's sections and subsections; examples are marked.
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue'

const tree = ref(null)
const page = ref(null)
const failed = ref('')
const route = ref({ area: '', page: '', hash: '' })
const query = ref('')
const hits = ref([])
const searching = ref(false)
const openAreas = ref(new Set())
const sidebarOpen = ref(false)
const current = ref('') // the section in view

function parseLocation() {
  const parts = location.pathname.replace(/^\/docs\/?/, '').split('/').filter(Boolean)
  route.value = { area: parts[0] ?? '', page: parts[1] ?? '', hash: decodeURIComponent(location.hash.slice(1)) }
}

function go(area, pageId, hash = '') {
  const url = area ? `/docs/${area}/${pageId}${hash ? '#' + hash : ''}` : '/docs'
  if (url !== location.pathname + location.hash) history.pushState({}, '', url)
  sidebarOpen.value = false
  query.value = ''
  parseLocation()
}

function onLink(event) {
  // Links inside the docs open in place.
  const a = event.target.closest('a')
  if (!a || a.target === '_blank') return
  const href = a.getAttribute('href') ?? ''
  const m = href.match(/^\/docs(?:\/([a-z0-9-]+)\/([a-z0-9-]+))?(?:#(.*))?$/)
  if (!m) return
  event.preventDefault()
  go(m[1] ?? '', m[2] ?? '', m[3] ?? '')
}

const areaOf = computed(() => tree.value?.areas.find((a) => a.id === route.value.area) ?? null)
// Every page in reading order, for previous / next.
const ordered = computed(() => {
  const out = []
  for (const area of tree.value?.areas ?? []) {
    for (const group of area.groups) for (const p of group.pages) out.push({ area: area.id, areaTitle: area.title, group: group.title, ...p })
  }
  return out
})
const position = computed(() => ordered.value.findIndex((p) => p.area === route.value.area && p.id === route.value.page))
const previous = computed(() => (position.value > 0 ? ordered.value[position.value - 1] : null))
const next = computed(() => (position.value >= 0 && position.value < ordered.value.length - 1 ? ordered.value[position.value + 1] : null))
const groupTitle = computed(() => (position.value >= 0 ? ordered.value[position.value].group : ''))

async function loadPage() {
  page.value = null
  failed.value = ''
  if (!route.value.area || !route.value.page) {
    document.title = 'Documentation · AIGE'
    window.scrollTo(0, 0)
    return
  }
  openAreas.value.add(route.value.area)
  try {
    const response = await fetch(`/api/docs/${route.value.area}/${route.value.page}`)
    if (!response.ok) throw new Error(String(response.status))
    page.value = await response.json()
    document.title = `${page.value.title} · AIGE Docs`
    await nextTick()
    if (route.value.hash) document.getElementById(route.value.hash)?.scrollIntoView()
    else window.scrollTo(0, 0)
    watchSections()
  } catch {
    failed.value = "That page couldn't be found."
  }
}

watch(() => [route.value.area, route.value.page], loadPage)
watch(() => route.value.hash, (hash) => {
  if (hash && page.value) nextTick(() => document.getElementById(hash)?.scrollIntoView())
})

// The section in view, highlighted in the sidebar.
let observer = null
function watchSections() {
  observer?.disconnect()
  if (!('IntersectionObserver' in window)) return
  observer = new IntersectionObserver((entries) => {
    const visible = entries.filter((e) => e.isIntersecting).sort((a, b) => a.boundingClientRect.top - b.boundingClientRect.top)
    if (visible.length) current.value = visible[0].target.id
  }, { rootMargin: '-80px 0px -65% 0px' })
  document.querySelectorAll('.doc-anchor').forEach((el) => observer.observe(el))
}

let searchTimer = 0
watch(query, (q) => {
  window.clearTimeout(searchTimer)
  if (!q.trim()) {
    hits.value = []
    return
  }
  searching.value = true
  searchTimer = window.setTimeout(async () => {
    try {
      const response = await fetch(`/api/docs-search?q=${encodeURIComponent(q)}`)
      hits.value = (await response.json()).hits ?? []
    } finally {
      searching.value = false
    }
  }, 180)
})

function toggleArea(id) {
  const set = new Set(openAreas.value)
  if (set.has(id)) set.delete(id)
  else set.add(id)
  openAreas.value = set
}

onMounted(async () => {
  parseLocation()
  window.addEventListener('popstate', parseLocation)
  try {
    const response = await fetch('/api/docs')
    if (!response.ok) throw new Error(String(response.status))
    tree.value = await response.json()
    if (!route.value.area) openAreas.value = new Set([tree.value.areas[0]?.id])
    await loadPage()
  } catch {
    failed.value = "The documentation couldn't be loaded. Is the server (website/backend/app.py) running?"
  }
})
onUnmounted(() => {
  window.removeEventListener('popstate', parseLocation)
  observer?.disconnect()
})
</script>

<template>
  <header class="docs-top">
    <div class="top-inner">
      <a href="/" class="brand" aria-label="AIGE home"><img src="/img/aige-mark.svg" alt="" class="mark" /><span class="word">AIGE</span></a>
      <a href="/docs" class="docs-label" @click.prevent="go('', '')">Docs</a>
      <div class="search">
        <input v-model="query" type="search" placeholder="Search the docs…" aria-label="Search the docs" />
        <div v-if="query.trim()" class="results">
          <p v-if="searching && !hits.length" class="dim">Searching…</p>
          <p v-else-if="!hits.length" class="dim">Nothing matches “{{ query }}”.</p>
          <a v-for="h in hits" :key="h.area + h.page + h.section" :href="`/docs/${h.area}/${h.page}#${h.section}`" class="hit"
             @click.prevent="go(h.area, h.page, h.section)">
            <span class="hit-where">{{ h.areaTitle }} › {{ h.pageTitle }}</span>
            <span class="hit-title">{{ h.title }}</span>
            <span class="hit-snippet">{{ h.snippet }}…</span>
          </a>
        </div>
      </div>
      <a href="/" class="back">← Back to the site</a>
      <button class="menu-btn" :aria-expanded="sidebarOpen" @click="sidebarOpen = !sidebarOpen">Contents</button>
    </div>
  </header>

  <div v-if="tree" class="docs-layout">
    <aside class="sidebar" :class="{ open: sidebarOpen }">
      <nav aria-label="Documentation">
        <div v-for="area in tree.areas" :key="area.id" class="area">
          <button class="area-title" :class="{ active: area.id === route.area }" :aria-expanded="openAreas.has(area.id)" @click="toggleArea(area.id)">
            <span class="chev" :class="{ open: openAreas.has(area.id) }">▸</span>{{ area.title }}
          </button>
          <div v-show="openAreas.has(area.id)" class="area-body">
            <div v-for="group in area.groups" :key="group.title" class="group">
              <div class="group-title">{{ group.title }}</div>
              <div v-for="p in group.pages" :key="p.id">
                <a :href="`/docs/${area.id}/${p.id}`" class="page-link" :class="{ active: area.id === route.area && p.id === route.page }"
                   @click.prevent="go(area.id, p.id)">{{ p.nav }}</a>
                <ul v-if="area.id === route.area && p.id === route.page" class="toc">
                  <li v-for="s in p.sections" :key="s.slug">
                    <a :href="`#${s.slug}`" :class="{ active: current === s.slug }" @click.prevent="go(area.id, p.id, s.slug)">{{ s.title }}</a>
                    <ul v-if="s.subsections.length" class="sub">
                      <li v-for="u in s.subsections" :key="u.slug">
                        <a :href="`#${u.slug}`" :class="{ active: current === u.slug, 'is-example': u.example }" @click.prevent="go(area.id, p.id, u.slug)">
                          <span v-if="u.example" class="tag">Example</span>{{ u.title }}
                        </a>
                      </li>
                    </ul>
                  </li>
                </ul>
              </div>
            </div>
          </div>
        </div>
      </nav>
    </aside>

    <main class="content" @click="onLink">
      <!-- The docs' home: the areas and their pages. -->
      <template v-if="!route.area">
        <span class="eyebrow">Documentation</span>
        <h1>{{ tree.title }}</h1>
        <p class="lead">{{ tree.intro }}</p>
        <div class="areas">
          <div v-for="area in tree.areas" :key="area.id" class="area-card">
            <h2>{{ area.title }}</h2>
            <p>{{ area.intro }}</p>
            <div v-for="group in area.groups" :key="group.title" class="card-group">
              <div class="group-title">{{ group.title }}</div>
              <a v-for="p in group.pages" :key="p.id" :href="`/docs/${area.id}/${p.id}`" @click.prevent="go(area.id, p.id)">{{ p.nav }}</a>
            </div>
          </div>
        </div>
      </template>

      <p v-else-if="failed" class="dim">{{ failed }}</p>
      <p v-else-if="!page" class="dim">Loading…</p>

      <article v-else class="page">
        <div class="crumbs">
          <a href="/docs" @click.prevent="go('', '')">Docs</a> › <span>{{ areaOf?.title }}</span> › <span>{{ groupTitle }}</span>
        </div>
        <h1>{{ page.title }}</h1>
        <div class="intro prose" v-html="page.intro"></div>
        <nav v-if="page.sections.length > 2" class="on-page" aria-label="On this page">
          <div class="group-title">On this page</div>
          <a v-for="s in page.sections" :key="s.slug" :href="`#${s.slug}`" @click.prevent="go(route.area, route.page, s.slug)">{{ s.title }}</a>
        </nav>

        <section v-for="s in page.sections" :key="s.slug" class="doc-section">
          <h2 :id="s.slug" class="doc-anchor"><a :href="`#${s.slug}`" class="hash" aria-hidden="true">#</a>{{ s.title }}</h2>
          <div class="prose" v-html="s.html"></div>
          <template v-for="u in s.subsections" :key="u.slug">
            <div v-if="u.example" :id="u.slug" class="example doc-anchor">
              <div class="example-label">Example</div>
              <h3>{{ u.title }}</h3>
              <div class="prose" v-html="u.html"></div>
            </div>
            <div v-else class="subsection">
              <h3 :id="u.slug" class="doc-anchor">{{ u.title }}</h3>
              <div class="prose" v-html="u.html"></div>
            </div>
          </template>
          <details v-if="s.questions.length" class="answers">
            <summary>Questions this answers</summary>
            <ul><li v-for="q in s.questions" :key="q">{{ q }}</li></ul>
          </details>
        </section>

        <nav class="pager">
          <a v-if="previous" :href="`/docs/${previous.area}/${previous.id}`" class="prev" @click.prevent="go(previous.area, previous.id)">
            <span class="dim">← Previous</span><span>{{ previous.nav }}</span>
          </a>
          <span v-else></span>
          <a v-if="next" :href="`/docs/${next.area}/${next.id}`" class="next" @click.prevent="go(next.area, next.id)">
            <span class="dim">Next →</span><span>{{ next.nav }}</span>
          </a>
        </nav>
      </article>
    </main>
  </div>
  <p v-else class="loading dim">{{ failed || 'Loading…' }}</p>
</template>

<style scoped>
.docs-top { position: sticky; top: 0; z-index: 40; background: rgba(11, 16, 32, 0.95); backdrop-filter: blur(10px); border-bottom: 1px solid var(--line); }
.top-inner { height: 60px; display: flex; align-items: center; gap: 18px; padding: 0 20px; }
.brand { display: flex; align-items: center; gap: 10px; color: var(--text); }
.mark { width: 24px; height: 26px; }
.word { font-weight: 800; letter-spacing: 0.06em; font-size: 17px; }
.docs-label { font: 500 12px/1 var(--mono); letter-spacing: 0.14em; text-transform: uppercase; color: var(--cyan); padding-left: 16px; border-left: 1px solid var(--line-2); }
.search { position: relative; flex: 1; max-width: 520px; margin-left: 12px; }
.search input { width: 100%; padding: 9px 12px; background: var(--panel); border: 1px solid var(--line-2); color: var(--text); font: 14px Inter, sans-serif; outline: none; }
.search input:focus { border-color: var(--blue); }
.results { position: absolute; top: 42px; left: 0; right: 0; max-height: 70vh; overflow-y: auto; background: var(--panel); border: 1px solid var(--line-2); box-shadow: 0 12px 32px rgba(0, 0, 0, 0.5); padding: 6px; }
.results .dim { padding: 10px; }
.hit { display: flex; flex-direction: column; gap: 2px; padding: 9px 10px; border-bottom: 1px solid var(--line); color: var(--text); }
.hit:hover { background: var(--panel-2); color: var(--text); }
.hit-where { font: 11px/1.3 var(--mono); color: var(--cyan); }
.hit-title { font-weight: 600; font-size: 14px; }
.hit-snippet { font-size: 12.5px; color: var(--muted); }
.back { margin-left: auto; font-size: 14px; color: var(--muted); white-space: nowrap; }
.menu-btn { display: none; background: none; border: 1px solid var(--line-2); color: var(--text); padding: 7px 12px; font: 14px Inter, sans-serif; cursor: pointer; }

.docs-layout { display: grid; grid-template-columns: 300px minmax(0, 1fr); min-height: calc(100vh - 60px); }
.sidebar { position: sticky; top: 60px; height: calc(100vh - 60px); overflow-y: auto; border-right: 1px solid var(--line); background: var(--bg-2); padding: 14px 0 40px; }
.area { border-bottom: 1px solid var(--line); }
.area-title { width: 100%; display: flex; align-items: center; gap: 8px; background: none; border: none; color: var(--text); font: 700 14px Inter, sans-serif; padding: 12px 18px; cursor: pointer; text-align: left; }
.area-title.active { color: var(--ice); }
.chev { display: inline-block; font-size: 11px; color: var(--dim); transition: transform 0.15s; }
.chev.open { transform: rotate(90deg); }
.area-body { padding: 0 0 10px; }
.group { margin-top: 6px; }
.group-title { font: 500 10.5px/1 var(--mono); letter-spacing: 0.12em; text-transform: uppercase; color: var(--dim); padding: 8px 18px 6px 34px; }
.page-link { display: block; padding: 6px 18px 6px 34px; font-size: 14px; color: var(--muted); border-left: 2px solid transparent; }
.page-link:hover { color: var(--text); background: rgba(255, 255, 255, 0.02); }
.page-link.active { color: #fff; border-left-color: var(--cyan); background: var(--panel); font-weight: 600; }
.toc { list-style: none; margin: 2px 0 8px; padding: 0 0 0 34px; }
.toc a { display: block; padding: 4px 14px 4px 12px; font-size: 13px; line-height: 1.35; color: var(--muted); border-left: 1px solid var(--line-2); }
.toc a:hover { color: var(--text); }
.toc a.active { color: var(--cyan); border-left-color: var(--cyan); }
.toc .sub { list-style: none; margin: 0; padding: 0 0 0 12px; }
.toc .sub a { font-size: 12.5px; color: var(--dim); }
.toc .sub a.is-example { color: var(--dim); }
.tag { font: 600 9.5px/1 var(--mono); letter-spacing: 0.08em; text-transform: uppercase; color: var(--gold); border: 1px solid rgba(245, 199, 107, 0.45); padding: 2px 4px; margin-right: 6px; vertical-align: 1px; }

.content { padding: 40px 56px 96px; max-width: 980px; }
.content h1 { font-size: clamp(28px, 3.4vw, 40px); font-weight: 800; margin: 10px 0 14px; }
.lead { color: var(--muted); font-size: 18px; max-width: 760px; }
.areas { display: grid; grid-template-columns: repeat(auto-fill, minmax(280px, 1fr)); gap: 16px; margin-top: 36px; }
.area-card { background: var(--panel); border: 1px solid var(--line); padding: 20px 22px; }
.area-card h2 { font-size: 19px; margin-bottom: 8px; }
.area-card > p { color: var(--muted); font-size: 14px; margin-bottom: 10px; }
.card-group { margin-top: 10px; }
.card-group .group-title { padding: 6px 0; }
.card-group a { display: block; font-size: 14px; padding: 3px 0; }

.crumbs { font: 12px/1.4 var(--mono); color: var(--dim); }
.crumbs a { color: var(--cyan); }
.intro { font-size: 17px; color: var(--muted); margin-bottom: 24px; }
.on-page { border: 1px solid var(--line); background: var(--bg-2); padding: 12px 16px 14px; margin: 0 0 32px; display: flex; flex-wrap: wrap; gap: 6px 18px; }
.on-page .group-title { width: 100%; padding: 0 0 4px; }
.on-page a { font-size: 14px; }
.doc-section { padding: 26px 0 8px; border-top: 1px solid var(--line); }
.doc-section h2 { font-size: 23px; font-weight: 750; margin-bottom: 14px; scroll-margin-top: 76px; position: relative; }
.hash { position: absolute; left: -22px; color: var(--dim); opacity: 0; font-weight: 400; }
.doc-section h2:hover .hash { opacity: 1; }
.subsection h3 { font-size: 17px; margin: 22px 0 10px; scroll-margin-top: 76px; }
.example { margin: 22px 0 10px; background: linear-gradient(180deg, rgba(47, 124, 246, 0.07), rgba(34, 211, 238, 0.03)); border: 1px solid var(--line-2); border-left: 3px solid var(--cyan); padding: 16px 20px 6px; scroll-margin-top: 76px; }
.example-label { font: 600 10.5px/1 var(--mono); letter-spacing: 0.14em; text-transform: uppercase; color: var(--cyan); margin-bottom: 8px; }
.example h3 { font-size: 16.5px; margin-bottom: 10px; }
.answers { margin: 14px 0 6px; font-size: 13.5px; color: var(--muted); }
.answers summary { cursor: pointer; color: var(--dim); font-size: 13px; }
.answers ul { margin: 8px 0 0; padding-left: 20px; }
.pager { display: flex; justify-content: space-between; gap: 16px; margin-top: 48px; padding-top: 24px; border-top: 1px solid var(--line); }
.pager a { display: flex; flex-direction: column; gap: 4px; border: 1px solid var(--line); padding: 12px 16px; min-width: 200px; color: var(--text); }
.pager a:hover { border-color: var(--blue); }
.pager .next { text-align: right; margin-left: auto; }
.pager .dim { font-size: 12px; }
.dim { color: var(--dim); }
.loading { padding: 160px 24px; text-align: center; }

/* The pages' own HTML (from Markdown). */
.prose :deep(p) { margin: 0 0 14px; color: #cdd7f0; line-height: 1.7; }
.prose :deep(ul), .prose :deep(ol) { margin: 0 0 14px; padding-left: 22px; color: #cdd7f0; }
.prose :deep(li) { margin: 4px 0; line-height: 1.6; }
.prose :deep(strong) { color: #fff; }
.prose :deep(code) { font: 13px var(--mono); background: rgba(165, 220, 255, 0.08); border: 1px solid var(--line); padding: 1px 5px; color: var(--pale); }
.prose :deep(pre) { background: #070b17; border: 1px solid var(--line); padding: 14px 16px; overflow-x: auto; margin: 0 0 16px; }
.prose :deep(pre code) { background: none; border: none; padding: 0; font-size: 13px; line-height: 1.55; }
.prose :deep(table) { border-collapse: collapse; margin: 0 0 16px; font-size: 14px; width: 100%; }
.prose :deep(th), .prose :deep(td) { border: 1px solid var(--line); padding: 7px 10px; text-align: left; vertical-align: top; }
.prose :deep(th) { background: var(--panel); }
.prose :deep(blockquote) { margin: 0 0 14px; padding: 8px 14px; border-left: 3px solid var(--blue); background: var(--panel); color: var(--pale); }
.prose :deep(blockquote p) { margin: 0; }

@media (max-width: 960px) {
  .docs-layout { grid-template-columns: 1fr; }
  .sidebar { display: none; position: fixed; top: 60px; left: 0; right: 0; bottom: 0; height: auto; z-index: 30; }
  .sidebar.open { display: block; }
  .menu-btn { display: block; }
  .back { display: none; }
  .content { padding: 28px 20px 72px; }
}
@media (max-width: 640px) {
  .docs-label { display: none; }
  .search { margin-left: 0; }
}
</style>
