<script setup lang="ts">
// Project ▸ Scripts: every Lua script in the project (scripts/), searched, with what uses each (things in this scene,
// packages); opened in the script editor, or a new one started.
import { computed, onMounted, ref } from 'vue'
import { editorState } from '../editorState'
import { promptDialog } from '../dialogs'
import { lookups, requestLookups } from '../lookups'

const query = ref('')
const shown = computed(() => {
  const words = query.value.toLowerCase().split(/\s+/).filter(Boolean)
  return lookups.scripts.filter((s) => words.every((w) => `${s} ${(lookups.scriptUsers[s] ?? []).join(' ')}`.toLowerCase().includes(w)))
})
onMounted(requestLookups)

function open(path: string): void {
  editorState.scriptEditor = path
}
async function create(): Promise<void> {
  const name = await promptDialog({
    title: 'New script',
    message: 'Its name (letters, digits, - and _). It goes in scripts/, and opens in the script editor to write.',
    placeholder: 'villager-day',
  })
  const slug = (name ?? '').trim().toLowerCase().replace(/\.lua$/, '').replace(/[^a-z0-9_/-]+/g, '-').replace(/^-+|-+$/g, '')
  if (slug) open(`scripts/${slug}.lua`)
}
</script>

<template>
  <div class="scripts">
    <div class="bar">
      <input v-model="query" class="search" spellcheck="false" placeholder="Search scripts and what uses them" />
      <button title="Look again for scripts and their users" @click="requestLookups">Refresh</button>
      <button class="primary" title="A new script in scripts/, opened in the script editor" @click="create">New script…</button>
    </div>
    <p v-if="!lookups.scripts.length" class="muted pad">No scripts yet. Most things need none; New script… starts one.</p>
    <div v-else class="list">
      <div v-for="s in shown" :key="s" class="row" :title="s" @dblclick="open(s)">
        <span class="icon">{ }</span>
        <span class="name">{{ s.replace(/^scripts\//, '') }}</span>
        <span class="users muted">{{ (lookups.scriptUsers[s] ?? []).join(', ') || 'not used here' }}</span>
        <button class="small" title="Open in the script editor" @click="open(s)">✎ Edit</button>
      </div>
      <p v-if="!shown.length" class="muted pad">Nothing matches.</p>
    </div>
  </div>
</template>

<style scoped>
.scripts {
  display: flex;
  flex-direction: column;
  min-height: 0;
}
.bar {
  display: flex;
  gap: 6px;
  padding: 6px;
  border-bottom: 1px solid #2a2a3a;
}
.search {
  flex: 1;
}
.list {
  overflow-y: auto;
}
.row {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 4px 8px;
  border-bottom: 1px solid #23232e;
  cursor: default;
}
.row:hover {
  background: #2a2a3a;
}
.icon {
  font-family: Consolas, monospace;
  color: #60a5fa;
  font-size: 11px;
}
.name {
  font-family: Consolas, monospace;
  font-size: 12px;
}
.users {
  flex: 1;
  font-size: 11px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.muted {
  opacity: 0.6;
}
.pad {
  padding: 8px;
}
</style>
